#include "ApplicationCore.h"
#include "../UICore/CrashRecoveryDialogComponent.h"
#include "../VocalTuneCore/IApexTuneEngineAdapter.h"
#include "../VocalTuneCore/ApexTuneIntegrationCore.h"
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include <unordered_map>

// Diagnostic logging from the audio thread causes disk I/O that exceeds
// the 1.45ms budget at 64-sample buffer, producing zipper/dropouts.
// Disabled by default. Enable only for development debugging.
#ifndef APEX_APP_DIAG_LOGS
#define APEX_APP_DIAG_LOGS 0
#endif

namespace DAW {

namespace
{
    static void appendStartupTrace(const juce::String& stage)
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        dir.createDirectory();

        auto file = dir.getChildFile("startup_trace.log");
        auto line = juce::Time::getCurrentTime().toString(true, true)
            + " [ApplicationCore] " + stage + "\n";
        file.appendText(line, false, false, "\n");
    }

    class ApplicationCoreVocalTuneAdapter final : public apex::vocaltune::IApexTuneEngineAdapter
    {
    public:
        explicit ApplicationCoreVocalTuneAdapter(ApplicationCore& owner)
            : owner_(owner)
        {
        }

        apex::vocaltune::ApexTuneAudioPayload loadClipMonoAudio(const juce::String& clipId) override
        {
            apex::vocaltune::ApexTuneAudioPayload payload;

            auto* clip = dynamic_cast<AudioClip*>(owner_.getClipManager().getClip(clipId));
            if (clip == nullptr)
                return payload;

            const auto sourceFile = clip->getSourceFile();
            if (!sourceFile.existsAsFile())
                return payload;

            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
                return payload;

            juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true);

            payload.samples.assign((size_t) reader->lengthInSamples, 0.0f);
            if (reader->numChannels == 1)
            {
                const auto* src = buffer.getReadPointer(0);
                std::copy(src, src + reader->lengthInSamples, payload.samples.begin());
            }
            else
            {
                const float invNumChannels = 1.0f / (float) reader->numChannels;
                for (int sample = 0; sample < (int) reader->lengthInSamples; ++sample)
                {
                    float sum = 0.0f;
                    for (int channel = 0; channel < (int) reader->numChannels; ++channel)
                        sum += buffer.getReadPointer(channel)[sample];
                    payload.samples[(size_t) sample] = sum * invNumChannels;
                }
            }

            payload.sampleRate = reader->sampleRate;
            payload.sourceFile = sourceFile;
            return payload;
        }

        void scheduleBackgroundJob(std::function<void()> job) override
        {
            owner_.queueVocalTuneBackgroundJob(std::move(job));
        }

        void postToMessageThread(std::function<void()> task) override
        {
            juce::MessageManager::callAsync(std::move(task));
        }

        void showEditorWindow(std::unique_ptr<juce::Component> editor,
                              const juce::String& title) override
        {
            juce::DialogWindow::LaunchOptions opts;
            opts.content.setOwned(editor.release());
            opts.dialogTitle = title;
            opts.dialogBackgroundColour = juce::Colours::black;
            opts.escapeKeyTriggersCloseButton = true;
            opts.useNativeTitleBar = true;
            opts.resizable = true;
            opts.launchAsync();
        }

        void markProjectDirty() override
        {
            owner_.markProjectDirty("vocal_tune_changed");
        }

        void showError(const juce::String& title, const juce::String& message) override
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, title, message);
        }

        int64_t getTransportSamplePosition() const override
        {
            return (int64_t) owner_.getTransport().getPosition();
        }

    private:
        ApplicationCore& owner_;
    };
}

ApplicationCore::ApplicationCore()
{
}

ApplicationCore::~ApplicationCore()
{
    shutdown();
}

void ApplicationCore::initialize()
{
    DBG("ApplicationCore::initialize() - Starting DAW subsystems");
    appendStartupTrace("initialize.begin");

    // ── Crash recovery & session lock — must be first, before any UI ─────────
    recoverySession_  = std::make_unique<RecoverySessionCore>();
    crashRecovery_    = std::make_unique<CrashRecoveryCore>();
    autosaveManager_  = std::make_unique<AutosaveManagerCore>();

    recoverySession_->startSession(ProjectInfo::versionString);
    appendStartupTrace("initialize.recovery.ready");

    // Session-global fader format core — single source of truth for all
    // fader/meter format math across audio, UI, and persistence.
    FaderRangeCore::setGlobalInstance(&faderRangeCore_);
    faderRangeCore_.addListener(this);
    appendStartupTrace("initialize.fader.ready");

    // Initialize subsystems in dependency order
    transport_ = std::make_unique<TransportController>(appState_);
    trackManager_ = std::make_unique<TrackManager>();
    clipManager_ = std::make_unique<ClipManager>();
    markerManager_ = std::make_unique<MarkerManager>();
    routingGraph_ = std::make_unique<RoutingGraph>();

    // Quick Track Builder: project-scoped role-color families. Seed is
    // random per session; New Project reseeds so each song develops its
    // own visual language while saved projects restore their exact map.
    quickTrackColors_ = std::make_unique<QuickTrackColorSystem>(
        juce::Random::getSystemRandom().nextInt64());
    appendStartupTrace("initialize.quick-track-colors.ready");

    // C11: refuse deletion of any track with an actively-recording take so
    // the recording engine's resolved take pointers can never dangle.
    trackManager_->trackDeletionGate = [this](const TrackID& id)
    {
        return ! recordingEngine_.isTrackBeingRecorded(id);
    };
    vocalTuneEngineAdapter_ = std::make_unique<ApplicationCoreVocalTuneAdapter>(*this);
    vocalTuneIntegration_ = std::make_unique<apex::vocaltune::ApexTuneIntegrationCore>(*vocalTuneEngineAdapter_);
    appendStartupTrace("initialize.core-subsystems.ready");

    // Project manager — wired to all serializable subsystems
    projectManager_ = std::make_unique<ProjectManager>();
    // AutosaveManagerCore is the sole production autosave authority.  Keep
    // ProjectManager's legacy path available for standalone tools, but do not
    // allow a second timer writer in the application.
    projectManager_->setExternalAutosaveManaged(true);
    projectManager_->setSubsystems({
        trackManager_.get(),
        clipManager_.get(),
        transport_.get(),
        markerManager_.get(),
        routingGraph_.get(),
        &faderRangeCore_,
        &appState_,
        this,  // ApplicationCore for plugin serialization
        &folderBus_,
        bubblegumV2_.getMasterRoute(),
        &automationManager_  // automation lanes — save/restore
    });
    appendStartupTrace("initialize.project-manager.ready");

    // ── Autosave manager — wire to project manager ────────────────────────────
    autosaveManager_->prepare(*projectManager_, recoverySession_.get());
    appendStartupTrace("initialize.autosave.ready");

    // Wire query lambdas so autosave gates poll live state without callbacks
    autosaveManager_->setIsRecordingQuery([this]() { return recordingEngine_.isRecording(); });
    autosaveManager_->setIsExportingQuery([this]()
    {
        return isOfflineRendering_.load(std::memory_order_acquire)
            || projectStateRestoreSuspensionActive_.load(std::memory_order_acquire);
    });

    // Wire autosave status callbacks
    autosaveManager_->onAutosaveSucceeded = [](const juce::String& t)
    {
        juce::Logger::writeToLog("[Autosave] Success: " + t);
    };
    autosaveManager_->onAutosaveFailed = [](const juce::String& reason)
    {
        juce::Logger::writeToLog("[Autosave] FAILED: " + reason);
    };

    // Force CommandManager singleton initialization (wires EditUndo/EditRedo)
    CommandManager::getInstance();

    // Audio engine — wired to track, clip, transport, routing, and audio file subsystems
    pluginPlayheadInfoCore_.setTransportController(transport_.get());
    clipRegionPluginCore_.setPlayheadInfoCore(&pluginPlayheadInfoCore_);
    audioEngine_.setSubsystems(trackManager_.get(), clipManager_.get(),
                               transport_.get(), routingGraph_.get(),
                               &audioFileManager_);
    publishPluginChainsSnapshot();   // C3: initial (likely empty) chain-map snapshot
    // C10: pre-create heavy per-clip DSP cores on the message thread at
    // clip creation / recreation (import, project load, undo/redo).
    clipManager_->onAudioClipReady = [this](const ClipID& clipId)
    {
        audioEngine_.prewarmClipDSP(clipId);
    };
    audioEngine_.setPluginPlayheadInfoCore(&pluginPlayheadInfoCore_);
    audioEngine_.setClipRegionPluginCore(&clipRegionPluginCore_);
    audioEngine_.setFolderBusStateModel(&folderBusStateModel_);
    audioEngine_.setMidiPlayback(&midiPlayback_);
    audioEngine_.setMidiInput(&midiInput_);
    audioEngine_.setVirtualKeyboard(&virtualKeyboard_);
    audioEngine_.setAutomationManager(&automationManager_);
    audioEngine_.setTrackPeakMeterManager(&trackPeakMeterManager_);
    audioEngine_.setVocalTuneIntegration(vocalTuneIntegration_.get());
    appendStartupTrace("initialize.audio-engine.wired");

    // Register all existing tracks now (handles case where prepareToPlay
    // ran before the manager was wired, e.g. on project load / startup).
    if (currentSampleRate_ > 0.0)
        trackPeakMeterManager_.prepare(currentSampleRate_, juce::jmax(1, currentBlockSize_));
    if (trackManager_)
    {
        for (int i = 0; i < trackManager_->getNumTracks(); ++i)
            if (auto* t = trackManager_->getTrack(i))
                if (!t->isMaster())
                    trackPeakMeterManager_.registerTrack(t->getID());
        if (trackManager_->hasMasterTrack())
            if (auto* mt = trackManager_->getMasterTrack())
                trackPeakMeterManager_.registerTrack(mt->getID());
    }
    midiInput_.setSubsystems(trackManager_.get(), clipManager_.get(), &appState_, transport_.get(), &virtualKeyboard_);
    midiInput_.setAllAvailableInputsEnabled(true);
    clickEngine_.setSubsystems(transport_.get(), &clickState_);
    pluginAutomationRecorderCore_.setSubsystems(&automationManager_,
                                                &pluginAutomationGestureCore_,
                                                [this] { return transport_ != nullptr && transport_->isPlaying(); },
                                                [this] { return transport_ != nullptr ? (int64_t)transport_->getPosition() : 0; });
    pluginAutomationRecorderCore_.setLaneWrittenCallback([this](const LastTouchedPluginParameter& target, const juce::String& parameterId)
    {
        if (trackManager_ == nullptr || !target.isValid())
            return;

        if (auto* track = trackManager_->getTrack(target.trackId))
        {
            track->setAutomationVisible(true);
            track->setActiveAutomationParameterId(parameterId);
            auto order = track->getAutomationParameterOrder();
            if (!order.contains(parameterId))
            {
                order.add(parameterId);
                track->setAutomationParameterOrder(order);
            }
        }
    });
    pluginAutomationRecorderCore_.start60Hz();
    appendStartupTrace("initialize.automation.ready");

    // Recording engine — captures input audio to armed tracks
    recordingEngine_.setSubsystems(trackManager_.get(), clipManager_.get(),
                                    transport_.get(), &audioFileManager_);
    recordingEngine_.setRecordingFinalizedCallback([this]
    {
        if (autosaveManager_)
            autosaveManager_->onRecordingFinalized();
    });
    liveInputMonitor_.setSubsystems(trackManager_.get(), transport_.get());

    // Sync routing graph with track lifecycle
    trackManager_->addListener(this);
    routingGraph_->addListener(this);
    transport_->addListener(this);

    auto* masterTrack = trackManager_->createMasterTrack();
    masterTrack->addListener(this);
    masterBus_.bindMasterTrack(masterTrack);
    masterBus_.setPluginChain(getPluginChain(masterTrack->getID()));
    masterTrack->setPluginChain(getPluginChain(masterTrack->getID()));
    appendStartupTrace("initialize.master-track.ready");
    DBG("[ApplicationCore] Master identity source = Track::isMaster(), id=" + masterTrack->getID());

    // Sync the routing graph master node's trackId to the real master Track ID.
    // The graph initialises it as "master" (a sentinel string) but cable routing
    // resolves anchors by TrackID — it must match the actual master strip ID.
    if (auto* masterNode = routingGraph_->getMasterNode())
        masterNode->trackId = masterTrack->getID();

    // Initialize interaction mode manager (auto-detects touch/mouse)
    interactionManager_ = std::make_unique<InteractionModeManager>();

    // Initialize gamepad support (disabled by default)
    gamepadManager_ = std::make_unique<GamepadManager>();
    gamepadMapper_ = std::make_unique<GamepadMapper>(*gamepadManager_);

    // Gamepad is OFF by default
    gamepadManager_->setEnabled(false);
    gamepadMapper_->setEnabled(false);

    // Initialize Bubblegum V2 system — send-only routing with visual feedback
    bubblegumV2_.init(*routingGraph_, *trackManager_);
    appendStartupTrace("initialize.bubblegum.ready");

    // Create a default track for testing
    trackManager_->createTrack("Track 1");
    appendStartupTrace("initialize.default-track.ready");

    DBG("ApplicationCore::initialize() - DAW subsystems initialized successfully");
    DBG("ApplicationCore: Gamepad support available (disabled by default)");
    appendStartupTrace("initialize.complete");
}

void ApplicationCore::shutdown()
{
    if (shutdownComplete_)
        return;

    // Close the realtime admission boundary before releasing chain ownership.
    // MainComponent normally removes/closes the device first, but ApplicationCore
    // also owns a public shutdown path; this makes the lifetime contract
    // explicit for both callers and reuses the existing callback drain rather
    // than adding a sleep-based grace period.
    const bool wasAlreadySuspended =
        projectStateRestoreSuspensionActive_.exchange(true, std::memory_order_acq_rel);
    if (!waitForRealtimeDeviceCallbacksToDrain(2000))
    {
        if (!wasAlreadySuspended)
            projectStateRestoreSuspensionActive_.store(false, std::memory_order_release);
        jassertfalse;
        juce::Logger::writeToLog(
            "[APPLICATION] shutdown aborted: realtime callbacks did not drain");
        return;
    }

    DBG("ApplicationCore::shutdown() - Shutting down DAW subsystems");

    recordingEngine_.shutdown();

    // ── Clean shutdown marker — must be before any subsystem teardown ─────────
    if (autosaveManager_)
    {
        const auto autosaveDrained = autosaveManager_->shutdown();
        if (!autosaveDrained)
        {
            juce::Logger::writeToLog(
                "[APPLICATION] autosave worker did not drain before bounded shutdown; "
                "publication authority was revoked");
        }
    }
    if (recoverySession_) recoverySession_->markCleanShutdown();

    if (trackManager_)
    {
        for (int i = 0; i < trackManager_->getNumTracks(); ++i)
            if (auto* track = trackManager_->getTrack(i))
                track->removeListener(this);

        if (trackManager_->hasMasterTrack())
            trackManager_->getMasterTrack()->removeListener(this);
    }

    // Shutdown in reverse order of initialization
    vocalTuneIntegration_.reset();
    vocalTuneEngineAdapter_.reset();
    audioEngine_.setVocalTuneIntegration(nullptr);
    vocalTuneThreadPool_.removeAllJobs(true, 5000);
    releaseAllPluginChainsForShutdown();
    gamepadMapper_.reset();
    gamepadManager_.reset();
    interactionManager_.reset();
    midiInput_.shutdown();
    projectManager_.reset();
    if (trackManager_) trackManager_->removeListener(this);
    if (routingGraph_) routingGraph_->removeListener(this);
    if (transport_)    transport_->removeListener(this);
    pluginAutomationRecorderCore_.stop60Hz();
    routingGraph_.reset();
    markerManager_.reset();
    clipManager_.reset();
    trackManager_.reset();
    transport_.reset();
    faderRangeCore_.removeListener(this);
    FaderRangeCore::setGlobalInstance(nullptr);

    shutdownComplete_ = true;
    projectStateRestoreSuspensionActive_.store(false, std::memory_order_release);

    DBG("ApplicationCore::shutdown() - DAW subsystems shut down successfully");
}

void ApplicationCore::queueVocalTuneBackgroundJob(std::function<void()> job)
{
    class VocalTuneLambdaJob final : public juce::ThreadPoolJob
    {
    public:
        explicit VocalTuneLambdaJob(std::function<void()> fn)
            : juce::ThreadPoolJob("VocalTuneJob")
            , fn_(std::move(fn))
        {
        }

        JobStatus runJob() override
        {
            if (fn_)
                fn_();
            return jobHasFinished;
        }

    private:
        std::function<void()> fn_;
    };

    vocalTuneThreadPool_.addJob(new VocalTuneLambdaJob(std::move(job)), true);
}

void ApplicationCore::releaseAllPluginChainsForShutdown()
{
    masterBus_.setPluginChain(nullptr);
    // Publish an empty chain-map snapshot first so the audio thread never
    // holds a snapshot that references the chains about to be released.
    audioEngine_.publishPluginChainsSnapshot(std::make_shared<AudioEngine::PluginChainSnapshotMap>());
    // The callback gate in shutdown() has already drained admitted callbacks.
    // Release the audio engine's retained block snapshot on this control thread
    // before clearing the final map owners, so chain destructors can hand off
    // bypass retirement and the final drain can reclaim it deterministically.
    audioEngine_.releasePluginChainsSnapshotForControlPlane();

    for (auto& [trackId, chain] : pluginChains_)
    {
        juce::ignoreUnused(trackId);
        if (chain)
        {
            chain->onChainChanged = nullptr;
            chain->closeAllEditors();
            chain->releaseResources();
        }
    }

    pluginChains_.clear();
    PluginChainCore::drainAllRetiredBypassCores();
}

void ApplicationCore::closeAllPluginEditors()
{
    jassert(juce::MessageManager::existsAndIsCurrentThread());

    for (auto& [trackId, chain] : pluginChains_)
    {
        juce::ignoreUnused(trackId);
        if (chain)
            chain->closeAllEditors();
    }
}

void ApplicationCore::syncPluginChainSidechainBusConfig()
{
    lastSidechainError_.clear();
    std::unordered_map<TrackID, std::unordered_map<int, juce::Array<int>>> busesByTrack;

    // Resolve the plugin slot a sidechain connection targets: explicit
    // destinationPluginId first, then the first slot whose processor declares
    // an input bus beyond the destination bus index.
    auto resolveTargetSlot = [](PluginChainCore& chain, const RoutingConnection& conn) -> int
    {
        if (conn.destinationPluginId.isNotEmpty())
        {
            for (int slotIndex = 0; slotIndex < chain.getNumSlots(); ++slotIndex)
            {
                auto* slot = chain.getSlot(slotIndex);
                if (slot != nullptr && slot->getPluginInstanceId() == conn.destinationPluginId)
                    return slotIndex;
            }
        }

        for (int slotIndex = 0; slotIndex < chain.getNumSlots(); ++slotIndex)
        {
            auto* slot = chain.getSlot(slotIndex);
            auto* processor = slot != nullptr ? slot->getProcessor() : nullptr;
            if (processor != nullptr && processor->getBusCount(true) > conn.destinationBusIndex)
                return slotIndex;
        }

        // A sandbox slot has no parent processor by design.  If the route has
        // no stable destinationPluginId, still resolve it to a sandbox slot so
        // the unsupported operation is rejected explicitly instead of being
        // lost as an unresolvable sidechain target.
        for (int slotIndex = 0; slotIndex < chain.getNumSlots(); ++slotIndex)
            if (auto* slot = chain.getSlot(slotIndex); slot != nullptr && slot->isSandboxed())
                return slotIndex;

        return -1;
    };

    if (routingGraph_ != nullptr)
    {
        for (auto* conn : routingGraph_->getAllConnections())
        {
            if (conn == nullptr
                || conn->type != ConnectionType::Sidechain
                || !conn->active.load(std::memory_order_relaxed)
                || conn->bypassed.load(std::memory_order_relaxed))
            {
                continue;
            }

            auto* destNode = routingGraph_->getNode(conn->destNodeId);
            if (destNode == nullptr || destNode->trackId.isEmpty())
                continue;

            auto chainIt = pluginChains_.find(destNode->trackId);
            if (chainIt == pluginChains_.end() || !chainIt->second)
                continue;

            const int targetSlotIndex = resolveTargetSlot(*chainIt->second, *conn);
            if (targetSlotIndex < 0)
                continue;

            // The sidechain bus is an AUXILIARY input bus. A connection stored
            // with bus index 0 (older/legacy creations, project files) was
            // skipped by the `> 0` test below, so the plugin's auxiliary bus
            // was never enabled: the cable stayed visible while the sidechain
            // delivered nothing. Resolve it to the plugin's first available
            // auxiliary input bus instead of dropping the route.
            int busIndex = conn->destinationBusIndex;
            if (busIndex <= 0)
            {
                if (auto* targetSlot = chainIt->second->getSlot(targetSlotIndex))
                {
                    if (auto* targetProc = targetSlot->getProcessor())
                    {
                        for (int b = 1; b < targetProc->getBusCount(true); ++b)
                        {
                            if (auto* in = targetProc->getBus(true, b))
                            {
                                // Generic bus resolution: the sidechain is the
                                // first NON-MAIN input bus. Assuming index 1
                                // breaks plugins that order their buses
                                // differently.
                                if (! in->isMain()
                                    && (in->isEnabled() || ! in->getCurrentLayout().isDisabled()))
                                {
                                    busIndex = b;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            auto& busList = busesByTrack[destNode->trackId][targetSlotIndex];
            if (busIndex > 0 && !busList.contains(busIndex))
                busList.add(busIndex);
        }
    }

    // C1: live re-prepare calls setBusesLayout/prepareToPlay on loaded
    // plugins — the audio thread must never be inside those instances.
    // Pre-check whether any chain would actually re-prepare a slot; only
    // then suppress realtime processing and drain in-flight callbacks.
    static const std::unordered_map<int, juce::Array<int>> emptySidechainConfig;
    bool anyReprepareNeeded = false;
    for (auto& [trackId, chain] : pluginChains_)
    {
        if (!chain)
            continue;
        auto it = busesByTrack.find(trackId);
        const auto& newConfig = (it != busesByTrack.end()) ? it->second : emptySidechainConfig;
        if (chain->wouldSidechainBusConfigChange(newConfig))
        {
            anyReprepareNeeded = true;
            break;
        }
    }

    if (anyReprepareNeeded)
    {
        pluginReprepareSuspensionActive_.store(true, std::memory_order_release);
        if (!waitForRealtimeDeviceCallbacksToDrain(2000))
        {
            pluginReprepareSuspensionActive_.store(false, std::memory_order_release);
            juce::Logger::writeToLog("[SIDECHAIN] re-prepare SKIPPED: realtime callbacks did not drain "
                "within 2000 ms — deactivating sidechain routes whose bus config could not be committed.");
            rollbackUncommittedSidechainRoutes(resolveTargetSlot);
            return;
        }
    }

    for (auto& [trackId, chain] : pluginChains_)
    {
        if (!chain)
            continue;

        auto it = busesByTrack.find(trackId);
        if (it != busesByTrack.end())
        {
            juce::String sidechainError;
            if (! chain->setActiveSidechainBusConfig(it->second, sidechainError)
                && sidechainError.isNotEmpty())
            {
                if (lastSidechainError_.isNotEmpty())
                    lastSidechainError_ += " ";
                lastSidechainError_ += "Track '" + trackId + "': " + sidechainError;
                juce::Logger::writeToLog("[SIDECHAIN] " + lastSidechainError_);
            }
        }
        else
            chain->setActiveSidechainBusConfig({});
    }

    if (anyReprepareNeeded)
        pluginReprepareSuspensionActive_.store(false, std::memory_order_release);

    // C6-sidechain-transaction: the graph may only report a sidechain route
    // as ACTIVE when the plugin's auxiliary bus is actually committed and
    // enabled. Plugins can reject layouts (verifyActiveSidechainBuses prunes
    // them) or the drain can fail above; either way, any active connection
    // whose bus is not committed is rolled back to inactive and republished
    // so UI state and plugin bus state can never diverge.
    rollbackUncommittedSidechainRoutes(resolveTargetSlot);

    dumpSidechainDiagnostics("after bus-config sync");
}

int ApplicationCore::migrateLegacySidechainBusIndices(ProjectUpgradeReport* report)
{
    if (routingGraph_ == nullptr)
        return 0;

    int resolved = 0;
    int reactivated = 0;
    int pluginIdsFilled = 0;

    for (auto* conn : routingGraph_->getAllConnections())
    {
        if (conn == nullptr || conn->type != ConnectionType::Sidechain)
            continue;

        // Old builds silently DEACTIVATED sidechain routes whose bus could not
        // be committed (rollbackUncommittedSidechainRoutes), and that inactive
        // flag was saved into the project. On load the bus sync skips inactive
        // connections, so an old project showed the cable while EXT received
        // nothing. Reactivate them here — the whole point of the migration is
        // to make them work with the now-correct bus handling.
        if (! conn->active.load(std::memory_order_relaxed)
            && ! conn->bypassed.load(std::memory_order_relaxed))
        {
            conn->active.store(true, std::memory_order_relaxed);
            ++reactivated;
        }

        // Resolve the destination chain ONCE — needed for both the plugin
        // target and the auxiliary-bus index below.
        auto* destNode = routingGraph_->getNode(conn->destNodeId);
        const bool hasChain = destNode != nullptr && ! destNode->trackId.isEmpty();
        auto chainIt = hasChain ? pluginChains_.find(destNode->trackId) : pluginChains_.end();
        const bool chainOk = chainIt != pluginChains_.end() && chainIt->second;

        // Legacy builds saved the sidechain's plugin target EMPTY
        // (scPluginId=""), which breaks the UI cable identification and
        // degrades the slot resolution to a fallback. Fill it from the same
        // scan that resolves the bus: the first slot exposing a non-main
        // input bus.
        if (conn->destinationPluginId.isEmpty() && chainOk)
        {
            for (int slotIndex = 0; slotIndex < chainIt->second->getNumSlots(); ++slotIndex)
            {
                auto* slot = chainIt->second->getSlot(slotIndex);
                auto* proc = slot != nullptr ? slot->getProcessor() : nullptr;
                if (proc == nullptr)
                    continue;

                bool hasNonMainInputBus = false;
                for (int b = 1; b < proc->getBusCount(true); ++b)
                {
                    if (auto* in = proc->getBus(true, b))
                    {
                        if (! in->isMain())
                        {
                            hasNonMainInputBus = true;
                            break;
                        }
                    }
                }

                if (hasNonMainInputBus)
                {
                    conn->destinationPluginId = slot->getPluginInstanceId();
                    ++pluginIdsFilled;
                    break;
                }
            }
        }

        // Already on an auxiliary bus: nothing more to resolve (idempotent).
        if (conn->destinationBusIndex > 0 || ! chainOk)
            continue;

        // Generic resolution: the sidechain is the first input bus that is not
        // the plugin's main one.
        int busIndex = 0;
        for (int slotIndex = 0; slotIndex < chainIt->second->getNumSlots() && busIndex == 0; ++slotIndex)
        {
            auto* slot = chainIt->second->getSlot(slotIndex);
            auto* proc = slot != nullptr ? slot->getProcessor() : nullptr;
            if (proc == nullptr)
                continue;

            for (int b = 1; b < proc->getBusCount(true); ++b)
            {
                if (auto* in = proc->getBus(true, b))
                {
                    if (! in->isMain())
                    {
                        busIndex = b;
                        break;
                    }
                }
            }
        }

        if (busIndex > 0)
        {
            conn->destinationBusIndex = busIndex;
            ++resolved;
        }
    }

    // NOTE: an active Send between the same node pair as a sidechain edge is
    // NOT a legacy defect — it is the standard reverb-send + sidechain-duck
    // setup (vocal sent to the reverb track, vocal sidechained into the
    // compressor on that track to duck it). An earlier revision of this
    // migration muted those routes; that broke legitimate projects and has
    // been removed. Duplicate Sends are legitimate user data and are left
    // untouched.

    if (report != nullptr)
    {
        report->add("Sidechain routes reactivated", reactivated);
        report->add("Sidechain bus indices resolved", resolved);
        report->add("Sidechain plugin targets restored", pluginIdsFilled);
    }

    if (resolved > 0 || reactivated > 0 || pluginIdsFilled > 0)
    {
        juce::Logger::writeToLog("[MIGRATION] v8->v9: " + juce::String(resolved)
            + " sidechain connection(s) resolved to their auxiliary bus, "
            + juce::String(reactivated) + " reactivated, "
            + juce::String(pluginIdsFilled) + " plugin target(s) filled.");

        // The audio engine consumes an IMMUTABLE published snapshot of the
        // routing graph (AudioEngine.h: "check for sidechain input using
        // snapshot edges (no RoutingGraph access)") and only adopts a new
        // generation when the graph notifies a change. Reactivating a
        // connection here bypasses the graph's own mutation API, so without
        // this notification the engine keeps seeing the route as INACTIVE:
        // the cable is visible and the bus is committed, yet the sidechain
        // buffer is never filled — the "EXT does not recognise the sidechain"
        // symptom on migrated projects. Notify whenever anything changed.
        routingGraph_->notifyGraphChanged();

        // Publish the resolved routes so the engine and the bus config agree
        // immediately (and the migrated project saves the clean format).
        syncPluginChainSidechainBusConfig();
    }

    dumpSidechainDiagnostics("after migration + sync");

    return resolved;
}

void ApplicationCore::dumpSidechainDiagnostics(const juce::String& context)
{
    juce::StringArray lines;
    lines.add("=== SIDECHAIN DIAGNOSTIC: " + context + " ===");

    if (routingGraph_ == nullptr)
    {
        lines.add("routingGraph: NULL");
    }
    else
    {
        int sidechainCount = 0;

        for (auto* conn : routingGraph_->getAllConnections())
        {
            if (conn == nullptr || conn->type != ConnectionType::Sidechain)
                continue;

            ++sidechainCount;
            lines.add("conn " + conn->sourceNodeId + " -> " + conn->destNodeId
                + " active=" + juce::String(conn->active.load(std::memory_order_relaxed) ? 1 : 0)
                + " bypassed=" + juce::String(conn->bypassed.load(std::memory_order_relaxed) ? 1 : 0)
                + " busIndex=" + juce::String(conn->destinationBusIndex)
                + " pluginId=\"" + conn->destinationPluginId + "\"");

            // Duplicate routes between the same pair (legacy Send cables).
            for (auto* other : routingGraph_->getAllConnections())
            {
                if (other == nullptr || other == conn)
                    continue;
                if (other->type == ConnectionType::Sidechain
                    || other->type == ConnectionType::FolderSum)
                    continue;
                if (other->sourceNodeId != conn->sourceNodeId
                    || other->destNodeId != conn->destNodeId)
                    continue;

                lines.add("   duplicate route " + other->id
                    + " type=" + juce::String((int) other->type)
                    + " active=" + juce::String(other->active.load(std::memory_order_relaxed) ? 1 : 0));
            }

            auto* destNode = routingGraph_->getNode(conn->destNodeId);
            if (destNode == nullptr)
            {
                lines.add("   destNode: NOT FOUND");
                continue;
            }

            lines.add("   destTrack=" + destNode->trackId);

            const auto chainIt = pluginChains_.find(destNode->trackId);
            if (chainIt == pluginChains_.end() || ! chainIt->second)
            {
                lines.add("   chain: NOT FOUND");
                continue;
            }

            const int numSlots = chainIt->second->getNumSlots();
            lines.add("   chain slots=" + juce::String(numSlots));

            for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
            {
                auto* slot = chainIt->second->getSlot(slotIndex);
                auto* proc = slot != nullptr ? slot->getProcessor() : nullptr;
                if (proc == nullptr)
                {
                    lines.add("     slot " + juce::String(slotIndex) + ": no processor");
                    continue;
                }

                juce::String busInfo;
                for (int b = 0; b < proc->getBusCount(true); ++b)
                {
                    auto* in = proc->getBus(true, b);
                    if (in == nullptr)
                    {
                        busInfo += " [" + juce::String(b) + " null]";
                        continue;
                    }

                    busInfo += " [" + juce::String(b)
                        + (in->isMain() ? " main" : " aux")
                        + " ch=" + juce::String(in->getNumberOfChannels())
                        + " en=" + juce::String(in->isEnabled() ? 1 : 0) + "]";
                }

                const bool auxActive = conn->destinationBusIndex > 0
                    && chainIt->second->isAuxInputBusActive(slotIndex, conn->destinationBusIndex);

                lines.add("     slot " + juce::String(slotIndex)
                    + " '" + proc->getName() + "'"
                    + " auxActive=" + juce::String(auxActive ? 1 : 0)
                    + " inBuses:" + (busInfo.isEmpty() ? juce::String(" none") : busInfo));
            }
        }

        lines.add("total sidechain connections: " + juce::String(sidechainCount));
    }

    for (const auto& line : lines)
    {
        juce::Logger::writeToLog(line);
        PluginScanAuditLogCore::appendLine("sidechain_diagnostic.log", line);
    }
}

void ApplicationCore::rollbackUncommittedSidechainRoutes(
    const std::function<int(PluginChainCore&, const RoutingConnection&)>& resolveTargetSlot)
{
    if (routingGraph_ == nullptr)
        return;

    bool anyRolledBack = false;
    juce::StringArray rolledBackRoutes;

    for (auto* conn : routingGraph_->getAllConnections())
    {
        if (conn == nullptr
            || conn->type != ConnectionType::Sidechain
            || !conn->active.load(std::memory_order_relaxed)
            || conn->bypassed.load(std::memory_order_relaxed))
        {
            continue;
        }

        auto* destNode = routingGraph_->getNode(conn->destNodeId);
        if (destNode == nullptr || destNode->trackId.isEmpty())
            continue;

        auto chainIt = pluginChains_.find(destNode->trackId);
        if (chainIt == pluginChains_.end() || !chainIt->second)
            continue;

        const int slotIndex = resolveTargetSlot(*chainIt->second, *conn);
        if (slotIndex < 0)
            continue;

        if (chainIt->second->isAuxInputBusActive(slotIndex, conn->destinationBusIndex))
            continue;   // committed — route stays active

        conn->active.store(false, std::memory_order_relaxed);
        anyRolledBack = true;
        rolledBackRoutes.add(conn->sourceNodeId + " -> " + conn->destNodeId);
        juce::Logger::writeToLog("[SIDECHAIN] route deactivated (bus not committed): "
            + conn->sourceNodeId + " -> " + conn->destNodeId
            + " slot=" + juce::String(slotIndex)
            + " bus=" + juce::String(conn->destinationBusIndex));
    }

    if (anyRolledBack)
    {
        // Republish so the audio thread and Bubblegum UI observe the
        // rolled-back state in the same generation.
        routingGraph_->notifyGraphChanged();
    }
}

void ApplicationCore::setGamepadEnabled(bool enabled)
{
    if (gamepadManager_ && gamepadMapper_)
    {
        gamepadManager_->setEnabled(enabled);
        gamepadMapper_->setEnabled(enabled);

        if (enabled)
        {
            DBG("==============================================");
            DBG("GAMEPAD SUPPORT: ENABLED");
            DBG("==============================================");
            DBG("Supported controllers:");
            DBG("- Legion Go controllers");
            DBG("- Xbox controllers");
            DBG("- Any XInput-compatible gamepad");
            DBG("");
            DBG("Default mapping:");
            DBG("  A = Play");
            DBG("  B = Stop");
            DBG("  Y = Record");
            DBG("  X = Toggle Loop");
            DBG("  LB/RB = Previous/Next Track");
            DBG("  D-Pad = Scroll");
            DBG("  Start = Open Mixer");
            DBG("  Back = Open Settings");
            DBG("  Left Stick = Scroll");
            DBG("  Right Stick X = Pan");
            DBG("  Triggers = Volume Control");
            DBG("==============================================");
        }
        else
        {
            DBG("GamepadMapper: Gamepad support disabled");
        }
    }
}

bool ApplicationCore::isGamepadEnabled() const
{
    return gamepadManager_ && gamepadManager_->isEnabled();
}

void ApplicationCore::checkAndShowRecoveryPromptIfNeeded(juce::Component* /*parent*/)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (!crashRecovery_ || !crashRecovery_->hasCrashedPreviousSession())
        return;

    auto info = crashRecovery_->findRecoveryInfo();
    if (!info.available)
        return;

    // Capture references for callbacks
    auto* pm   = projectManager_.get();
    auto* asMgr = autosaveManager_.get();
    auto* rec  = crashRecovery_.get();

    recoveryDialogWindow_ = CrashRecoveryDialogComponent::showAsync(
        info,
        /* onRecoverAutosave */
        [this, pm, asMgr](const juce::File& autosaveFile)
        {
            if (pm && pm->loadFromFile(autosaveFile))
            {
                isRecoveredProject_ = true;
                if (asMgr) asMgr->markDirty("recovered_project");
                juce::Logger::writeToLog("[Autosave] Recovered from autosave: "
                                         + autosaveFile.getFullPathName());
            }
            else if (pm)
            {
                juce::Logger::writeToLog("[Autosave] Recovery load failed: "
                    + pm->getLastLoadError());
            }
        },
        /* onOpenOriginal */
        [pm](const juce::File& origFile)
        {
            if (pm && origFile.existsAsFile())
                if (!pm->loadFromFile(origFile))
                    juce::Logger::writeToLog("[Autosave] Failed to open original project: "
                        + pm->getLastLoadError());
        },
        /* onDiscard */
        [info, rec]()
        {
            rec->discardRecovery(info);
        }
    );
}

void ApplicationCore::trackAdded(Track* track)
{
    if (!routingGraph_ || !track) return;

    track->addListener(this);

    // Prepare the new track's input trim/meter cores with the CURRENT engine
    // configuration. prepareTrackInputProcessors() only runs inside
    // prepareToPlay(), so any track added mid-session would otherwise keep
    // InputMeterCore's default peakDecayPerSample_ = 1.0 (NO decay) and the
    // VU needle would peg at maximum on the first signal and never fall —
    // the "frozen needle in large projects" bug, only fixed by app restart.
    // This also covers tracks restored while the device is already running.
    {
        const double sr = (currentSampleRate_ > 0.0) ? currentSampleRate_ : 44100.0;
        const int    bs = (currentBlockSize_  > 0)   ? currentBlockSize_  : 512;
        TrackInputProcessorCore::prepareTrack(*track, sr, bs);
    }

    // The master track is not a routing source — it's the final output processor.
    // Do not add a routing node for it; its plugins/fader are applied in MasterBusEngine.
    if (track->isMaster()) return;

    if (trackManager_ != nullptr && trackManager_->isRestoringState())
    {
        // The authoritative graph and plugin chains are restored in later
        // project stages. Avoid publishing two temporary graph generations and
        // a growing chain-map snapshot for every loaded track.
        trackPeakMeterManager_.registerTrack(track->getID());
        return;
    }

    // Map TrackRole to RoutingNodeType
    RoutingNodeType nodeType = RoutingNodeType::Track;
    switch (track->getRole())
    {
        case TrackRole::Aux:
        case TrackRole::Bus:        nodeType = RoutingNodeType::Bus;       break;
        case TrackRole::FolderBus:  nodeType = RoutingNodeType::FolderBus; break;
        default:                    nodeType = RoutingNodeType::Track;     break;
    }

    auto* node = routingGraph_->addNode(track->getName(), nodeType, track->getID());

    // Master routing: RoutingGraph::addNode() already established a Direct→master
    // edge for Track/Bus nodes. That single edge is the sole source of truth for
    // "this track's output goes to master" and is managed exclusively by
    // MasterRouteStateCore. Do NOT create a parallel Send→master edge here —
    // that caused a double summation and a route that Bubblegum could not
    // see or delete (master-leak bug).
    juce::ignoreUnused(node);
    trackPeakMeterManager_.registerTrack(track->getID());
    track->setPluginChain(getPluginChain(track->getID()));
    syncPluginChainSidechainBusConfig();
    rebuildFolderBusSnapshot();

    if (trackManager_ == nullptr || !trackManager_->isTopologyMutationActive())
    {
        if (autosaveManager_) autosaveManager_->markDirty("track_added");
        if (projectManager_)  projectManager_->markDirty();
    }
}

void ApplicationCore::faderRangeChanged()
{
    auto clampTrackVolume = [this](Track* track)
    {
        if (!track)
            return;

        const float db = FaderRangeCore::gainToDb(track->getVolume());
        const float clampedDb = faderRangeCore_.clampDb(db);
        const float clampedGain = faderRangeCore_.dbToGain(clampedDb);

        if (std::abs(clampedGain - track->getVolume()) > 0.0001f)
            track->setVolume(clampedGain);
    };

    if (trackManager_)
    {
        for (int i = 0; i < trackManager_->getNumTracks(); ++i)
            clampTrackVolume(trackManager_->getTrack(i));

        if (trackManager_->hasMasterTrack())
            clampTrackVolume(trackManager_->getMasterTrack());
    }
}

void ApplicationCore::trackRemoved(const TrackID& trackID)
{
    if (!routingGraph_) return;

    if (trackManager_ != nullptr && trackManager_->isRestoringState())
    {
        trackPeakMeterManager_.removeTrack(trackID);
        return;
    }

    if (trackManager_)
        if (auto* track = trackManager_->getTrack(trackID))
            track->removeListener(this);

    if (auto* node = routingGraph_->getNodeByTrackId(trackID))
        routingGraph_->removeNode(node->id);
    trackPeakMeterManager_.removeTrack(trackID);
    syncPluginChainSidechainBusConfig();
    rebuildFolderBusSnapshot();

    if (trackManager_ == nullptr || !trackManager_->isTopologyMutationActive())
    {
        if (autosaveManager_) autosaveManager_->markDirty("track_removed");
        if (projectManager_)  projectManager_->markDirty();
    }
}

void ApplicationCore::connectionAdded(RoutingConnection* conn)
{
    juce::ignoreUnused(conn);
    syncPluginChainSidechainBusConfig();
}

void ApplicationCore::connectionRemoved(const RouteID& connId)
{
    juce::ignoreUnused(connId);
    syncPluginChainSidechainBusConfig();
}

void ApplicationCore::graphChanged()
{
    syncPluginChainSidechainBusConfig();
}

void ApplicationCore::transportStateChanged()
{
    const bool recordingStateNow = recordingEngine_.isRecording();
    const bool recordingStateChanged = recordingStateNow != wasRecordingLastTick_;

    // Detect recording start so autosave stays gated until RecordingEngine
    // signals that clip/file finalization is actually complete.
    if (autosaveManager_ && recordingStateChanged)
    {
        wasRecordingLastTick_ = recordingStateNow;
        if (wasRecordingLastTick_)
        {
            autosaveManager_->onRecordingStarted();
            // Also mark dirty so the post-record autosave will fire
            autosaveManager_->markDirty("recording_started");
            if (projectManager_) projectManager_->markDirty();
        }
    }

    if (recordingStateChanged && transport_ != nullptr && transport_->isRecording())
    {
        diagInputCallbackCount_.store(0, std::memory_order_relaxed);
        diagInputSamplesExpected_.store(0, std::memory_order_relaxed);
        diagInputSamplesPreserved_.store(0, std::memory_order_relaxed);
        diagInputChannelsSeen_.store(0, std::memory_order_relaxed);
        diagInputShortBlockCount_.store(0, std::memory_order_relaxed);
        diagInputZeroBlockCount_.store(0, std::memory_order_relaxed);
        diagLastInputLogMs_.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    }

    if (recordingStateChanged && transport_ != nullptr && !transport_->isRecording())
    {
        const auto recDiag = recordingEngine_.getDiagnosticsSnapshot();
        juce::String summary = "[APEX-DIAG-SUMMARY] dur="
            + juce::String(currentSampleRate_ > 0.0 ? (double) recDiag.recSamplesOffered / currentSampleRate_ : 0.0, 3)
            + "s cb=" + juce::String(diagInputCallbackCount_.load(std::memory_order_relaxed))
            + " | IN expect=" + juce::String(diagInputSamplesExpected_.load(std::memory_order_relaxed))
            + " preserved=" + juce::String(diagInputSamplesPreserved_.load(std::memory_order_relaxed))
            + " short=" + juce::String(diagInputShortBlockCount_.load(std::memory_order_relaxed))
            + " zeroIn=" + juce::String(diagInputZeroBlockCount_.load(std::memory_order_relaxed))
            + " | REC offered=" + juce::String(recDiag.recSamplesOffered)
            + " shortVsCb=" + juce::String(recDiag.recShortVsCb)
            + " zeroOffered=" + juce::String(recDiag.recZeroOffered);

        for (const auto& trackDiag : recDiag.tracks)
        {
            summary << " | track=" << trackDiag.trackId
                    << " WR attempt=" << trackDiag.writer.pushAttempts
                    << " acc=" << trackDiag.writer.pushAccepted
                    << " rej=" << trackDiag.writer.pushRejected
                    << " sAcc=" << trackDiag.writer.pushSamplesAccepted
                    << " sRej=" << trackDiag.writer.pushSamplesRejected
                    << " | DUP dupInBlk=" << trackDiag.dupInputBlocks;
        }

        juce::Logger::writeToLog(summary);
    }
}

void ApplicationCore::trackPropertyChanged(Track* track)
{
    if (!trackManager_ || track == nullptr)
        return;

    rebuildFolderBusSnapshot();

    // TrackManager emits property notifications while a topology operation
    // reindexes or dissolves several tracks.  The enclosing user operation
    // owns the single dirty/Undo boundary; these internal notifications must
    // not turn one delete into one dirty event per shifted track.
    if (trackManager_->isTopologyMutationActive())
        return;

    // Mark project dirty (track rename, recolor, mute, solo, arm, etc.)
    if (autosaveManager_) autosaveManager_->markDirty("track_property");
    if (projectManager_)  projectManager_->markDirty();
}

void ApplicationCore::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = samplesPerBlock;

    const int safeBlockSize = juce::jmax(1, samplesPerBlock);
    const int safeInputChannels = juce::jmax(
        2, hardwareInputChannelCount_.load(std::memory_order_acquire));
    preservedHardwareInput_.setSize(safeInputChannels, juce::jmax(safeBlockSize, 8192), false, false, true);

    syncPluginChainSidechainBusConfig();
    prepareRenderGraph(sampleRate, samplesPerBlock);

    DBG("ApplicationCore::prepareToPlay() - Sample rate: " + juce::String(sampleRate) 
        + ", Block size: " + juce::String(samplesPerBlock));
}

void ApplicationCore::prepareRenderGraph(double sampleRate, int samplesPerBlock)
{
    // Guard: can be called between shutdown() (which resets routingGraph_) and
    // a racing ASIO prepareToPlay callback.
    if (!routingGraph_) return;

    if (trackManager_)
    {
        auto* masterTrack = trackManager_->createMasterTrack();
        masterBus_.bindMasterTrack(masterTrack);
        masterBus_.setPluginChain(getPluginChain(masterTrack->getID()));
    }

    midiPlayback_.prepare(sampleRate, samplesPerBlock);
    midiPlayback_.setTempo(transport_->getTempo());
    midiPlayback_.setSubsystems(clipManager_.get(), trackManager_.get());
    midiInput_.prepare(sampleRate, samplesPerBlock);
    clipRegionPluginCore_.prepare(sampleRate, samplesPerBlock);
    audioEngine_.prepare(sampleRate, samplesPerBlock);
    recordingEngine_.prepare(sampleRate, samplesPerBlock);
    liveInputMonitor_.prepare(sampleRate, samplesPerBlock);
    clickEngine_.prepare(sampleRate, samplesPerBlock);

    // Prepare track peak meter manager and register all existing tracks
    trackPeakMeterManager_.prepare(sampleRate, samplesPerBlock);
    if (trackManager_)
    {
        for (int i = 0; i < trackManager_->getNumTracks(); ++i)
        {
            if (auto* track = trackManager_->getTrack(i))
            {
                track->getInputTrim().prepare(sampleRate, samplesPerBlock);
                track->getInputMeter().prepare(sampleRate);
                track->getInputFxChain().prepare(sampleRate, samplesPerBlock);
                if (!track->isMaster())
                    trackPeakMeterManager_.registerTrack(track->getID());
            }
        }
    }
    for (auto& [id, chain] : pluginChains_)
        if (chain) chain->prepare(sampleRate, samplesPerBlock);
    publishPluginChainLatencies();
    masterBus_.prepare(sampleRate, samplesPerBlock);
    controlRoom_.prepare(sampleRate, samplesPerBlock);
}

void ApplicationCore::releaseResources()
{
    audioEngine_.releaseResources();
    // Device playback has stopped: complete the JUCE processor lifecycle for
    // every track/bus/master chain before any later prepareToPlay(). A plugin
    // that has processed audio may retain resources which are invalid across a
    // block-size/device lifetime transition.
    for (auto& [id, chain] : pluginChains_)
        if (chain) chain->releaseResources();
    clipRegionPluginCore_.releaseResources();
    liveInputMonitor_.releaseResources();
    clickEngine_.releaseResources();
    masterBus_.releaseResources();
    controlRoom_.releaseResources();
    DBG("ApplicationCore::releaseResources()");
}

bool ApplicationCore::beginRealtimeDeviceCallback() noexcept
{
    activeDeviceCallbacks_.fetch_add(1, std::memory_order_acq_rel);
    if (! OfflineRenderBarrierCore::shouldProcessRealtimePath(
            isOfflineRendering_.load(std::memory_order_acquire),
            projectStateRestoreSuspensionActive_.load(std::memory_order_acquire)))
    {
        activeDeviceCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }

    return true;
}

void ApplicationCore::endRealtimeDeviceCallback() noexcept
{
    activeDeviceCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
}

bool ApplicationCore::waitForRealtimeDeviceCallbacksToDrain(uint32_t timeoutMs) const noexcept
{
    const auto waitStart = juce::Time::getMillisecondCounter();
    while (! OfflineRenderBarrierCore::callbacksDrained(
        activeDeviceCallbacks_.load(std::memory_order_acquire)))
    {
        if (juce::Time::getMillisecondCounter() - waitStart >= timeoutMs)
            return false;

        juce::Thread::yield();
    }

    return true;
}

bool ApplicationCore::beginProjectStateRestore(uint32_t timeoutMs) noexcept
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Export has a separate owner and recording finalization can retain track
    // and clip references after transport stop. Reject both before mutation.
    if (isOfflineRendering_.load(std::memory_order_acquire)
        || recordingEngine_.isRecording())
        return false;

    const bool wasAlreadySuspended =
        projectStateRestoreSuspensionActive_.exchange(true, std::memory_order_acq_rel);

    if (!waitForRealtimeDeviceCallbacksToDrain(timeoutMs))
    {
        // Preserve an existing failure latch. If this call established the
        // gate, undo it because no destructive mutation has started.
        if (!wasAlreadySuspended)
            projectStateRestoreSuspensionActive_.store(false, std::memory_order_release);
        return false;
    }

    if (transport_)
        transport_->stop();
    audioEngine_.clearLiveInputBuffer();
    return true;
}

void ApplicationCore::endProjectStateRestore(bool restoreSucceeded) noexcept
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (restoreSucceeded)
    {
        // C2-device-lifecycle: re-prepare the engine while realtime callbacks
        // are STILL suspended (the gate flag is cleared below) so the restored
        // topology's per-node routing buffers and PDC lines are sized on the
        // message thread. The first audio callback after a project load must
        // never allocate inside syncNodeBuffers() — with a 2048 device block
        // the previous behavior allocated every node buffer on the audio
        // thread, stalling the callback and cracking.
        if (currentSampleRate_ > 0.0 && currentBlockSize_ > 0)
            audioEngine_.prepare(currentSampleRate_, currentBlockSize_);
        projectStateRestoreSuspensionActive_.store(false, std::memory_order_release);
    }
}

bool ApplicationCore::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill,
                                        const juce::AudioBuffer<float>* hardwareInputBuffer,
                                        int hardwareInputNumSamples,
                                        int validInputChannels)
{

    // This is the first shared-engine decision in the device path. Callbacks
    // already in flight are counted; callbacks entering after the flag publish
    // return without touching engine, monitor, or recorder state.
    if (! OfflineRenderBarrierCore::shouldProcessRealtimePath(
            isOfflineRendering_.load(std::memory_order_acquire),
            projectStateRestoreSuspensionActive_.load(std::memory_order_acquire)))
    {
        bufferToFill.clearActiveBufferRegion();
        return false;
    }

    setHardwareInputChannelCount(validInputChannels);

    // PROFESSIONAL SIGNAL FLOW:
    //
    // 0a. Receive the true hardware input snapshot captured from the device callback.
    // 0b. Live input monitor mix is built from the hardware input.
    // 0c. Recording capture reads the same hardware input.
    // 1. AudioEngine clears the buffer then renders playback mix.
    // 2. Live monitor mix is summed into playback output.
    // 3. MasterBusEngine applies master inserts/fader/meter/render tap.
    // 4. ControlRoomEngine applies monitor-only processing.
    //
    // Render/export reads from step 3’s RenderSourceEngine.
    // Monitor processing in step 4 does NOT affect exported files.

    pluginPlayheadInfoCore_.flushPendingTransportControlRequests();

    // ── Callback stage timing (armed only by APEX_CALLBACK_AUDIT=1) ──────
    // When disarmed this costs one relaxed atomic load per callback.
    const bool stageAudit = callbackStageTimingArmed_.load (std::memory_order_relaxed);
    if (stageAudit)
        callbackStageTelemetry_.stageTicks.fill (0);
    int64_t stagePrevTicks = stageAudit ? juce::Time::getHighResolutionTicks() : 0;
    const auto markStage = [&] (size_t stage) noexcept
    {
        if (! stageAudit) return;
        const int64_t now = juce::Time::getHighResolutionTicks();
        callbackStageTelemetry_.stageTicks[stage] = now - stagePrevTicks;
        stagePrevTicks = now;
    };

#if APEX_APP_DIAG_LOGS
    static int callbackBlockTraceRemaining = 20;
    if (callbackBlockTraceRemaining > 0)
    {
        --callbackBlockTraceRemaining;
        juce::Logger::writeToLog("[APEX-DIAG-CB] numSamples=" + juce::String(bufferToFill.numSamples)
            + " startSample=" + juce::String(bufferToFill.startSample)
            + " callbackChannels=" + juce::String(bufferToFill.buffer != nullptr ? bufferToFill.buffer->getNumChannels() : 0)
            + " hwInCount=" + juce::String(hardwareInputChannelCount_.load(std::memory_order_acquire)));
    }
#endif

    int inputChannels = 0;
    int preservedChannels = 0;
    int preservedSamples = 0;
    bool hasFreshHardwareInput = false;
    const int hardwareInputChannelCount = juce::jmax(0, validInputChannels);

    if (hardwareInputBuffer != nullptr && hardwareInputChannelCount > 0)
    {
        inputChannels = juce::jmin(hardwareInputChannelCount,
                                   hardwareInputBuffer->getNumChannels());
        preservedChannels = juce::jmin(inputChannels, preservedHardwareInput_.getNumChannels());
        preservedSamples = juce::jmin(bufferToFill.numSamples,
                                      hardwareInputNumSamples,
                                      preservedHardwareInput_.getNumSamples());
        if (preservedChannels > 0
            && preservedSamples == bufferToFill.numSamples)
        {
            for (int ch = 0; ch < preservedChannels; ++ch)
                preservedHardwareInput_.copyFrom(ch, 0, *hardwareInputBuffer, ch, 0, preservedSamples);

            RecordingInputValidityCore::clearInvalidTailChannels(
                preservedHardwareInput_, preservedChannels, 0, preservedSamples);
            hasFreshHardwareInput = true;
        }
    }

    diagInputCallbackCount_.fetch_add(1, std::memory_order_relaxed);
    diagInputChannelsSeen_.store(inputChannels, std::memory_order_relaxed);
    diagInputSamplesExpected_.fetch_add(bufferToFill.numSamples, std::memory_order_relaxed);
    diagInputSamplesPreserved_.fetch_add(preservedSamples, std::memory_order_relaxed);
    if (preservedSamples < bufferToFill.numSamples)
        diagInputShortBlockCount_.fetch_add(1, std::memory_order_relaxed);
    if (preservedChannels == 0 || preservedSamples == 0)
        diagInputZeroBlockCount_.fetch_add(1, std::memory_order_relaxed);

    markStage (CallbackStageInput);

    constexpr bool offlineRendering = false;
    const bool routingGraphAvailable =
        routingGraph_ != nullptr && routingGraph_->getMasterNode() != nullptr;

    if (hasFreshHardwareInput)
        audioEngine_.setLiveInputBuffer(
            &preservedHardwareInput_, preservedSamples, preservedChannels);
    else
        audioEngine_.clearLiveInputBuffer();

    if (RecordingCallbackPolicyCore::shouldFeedRecorder(
            recordingEngine_.isActivelyRecording(), offlineRendering, routingGraphAvailable))
    {
        const int recordedSamples = juce::jmin(
            bufferToFill.numSamples, preservedHardwareInput_.getNumSamples());

        // TAPE-MACHINE GUARANTEE: every active realtime recording callback
        // reaches the writer, including callbacks without a valid input route.
        if (!hasFreshHardwareInput && preservedHardwareInput_.getNumChannels() > 0 && recordedSamples > 0)
        {
            preservedHardwareInput_.clear(0, recordedSamples);

            if (!recordingSilenceFallbackAnnounced_.exchange(true, std::memory_order_acq_rel))
                recGuardSilenceCount_.fetch_add(1, std::memory_order_relaxed);
        }
        else if (hasFreshHardwareInput && recordingSilenceFallbackAnnounced_.exchange(false, std::memory_order_acq_rel))
        {
            recGuardRestoredCount_.fetch_add(1, std::memory_order_relaxed);
        }

        recordingEngine_.addRecorderInputDiagnosticBlock(
            bufferToFill.numSamples,
            preservedHardwareInput_.getNumChannels() > 0 ? recordedSamples : 0);
        if (preservedHardwareInput_.getNumChannels() > 0 && recordedSamples > 0)
            recordingEngine_.processBlock(
                preservedHardwareInput_, recordedSamples,
                hasFreshHardwareInput ? preservedChannels : 0);
    }

    markStage (CallbackStageRecording);

    // C1: a live plugin re-prepare is in progress on the message thread
    // (sidechain bus reconfiguration). Recording capture above is
    // unaffected; engine/plugin processing is suppressed for the bounded
    // drain window instead of racing setBusesLayout/prepareToPlay.
    if (pluginReprepareSuspensionActive_.load(std::memory_order_acquire))
    {
        bufferToFill.clearActiveBufferRegion();
        audioEngine_.clearLiveInputBuffer();
        return false;
    }

    // In Release builds jassert is a no-op. Recording has already received
    // this block, but rendering still returns silence when the graph is absent.
    if (!routingGraphAvailable)
    {
        bufferToFill.clearActiveBufferRegion();
        audioEngine_.clearLiveInputBuffer();
        return true;
    }

    const int64_t blockStart = transport_ ? (int64_t) transport_->getPosition() : 0;
    const bool countInFinished = clickEngine_.processBlock(blockStart, bufferToFill.numSamples);
    markStage (CallbackStageClick);
    static bool loggedHardwarePath = false;
    if (!loggedHardwarePath)
    {
        loggedHardwarePath = true;
        DBG("[ApplicationCore] Master node id = master");
        DBG("[ApplicationCore] Hardware output source = post-master device buffer via AudioEngine -> MasterBusEngine -> ControlRoomEngine");
    }

    if (hasFreshHardwareInput)
        liveInputMonitor_.processBlock(
            preservedHardwareInput_, preservedSamples, preservedChannels);
    markStage (CallbackStageMonitorTrim);

    audioEngine_.process(bufferToFill);
    markStage (CallbackStageEngine);

    if (bufferToFill.buffer && bufferToFill.buffer->getNumChannels() >= 2)
    {
        float* L = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
        float* R = bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);
        const int n = bufferToFill.numSamples;

        masterBus_.processBlock(L, R, n);
        markStage (CallbackStageMasterBus);
        // C9: the click joins the engine's transport fade (no hard-cut bursts).
        clickEngine_.sumIntoOutput(L, R, n, audioEngine_.getTransportFadeRamp(n));
        markStage (CallbackStageClickSum);
    }

    controlRoom_.processBlock(bufferToFill);
    markStage (CallbackStageControlRoom);

    // Spike context for the forensic record (cheap atomics/counters only).
    if (stageAudit)
    {
        callbackStageTelemetry_.flags = (uint8_t) (
            (transport_ != nullptr && transport_->isPlaying() ? 1 : 0)
            | (transport_ != nullptr && transport_->isRecording() ? 2 : 0)
            | (hasFreshHardwareInput ? 4 : 0));
        callbackStageTelemetry_.trackCount = (uint32_t) (trackManager_ != nullptr
            ? trackManager_->getNumTracks() : 0);
        callbackStageTelemetry_.graphVersion = routingGraph_ != nullptr
            ? routingGraph_->getGraphVersion() : 0;
    }

#if APEX_APP_DIAG_LOGS
    const auto nowMs = juce::Time::getMillisecondCounter();
    auto lastLogMs = diagLastInputLogMs_.load(std::memory_order_relaxed);
    if (nowMs - lastLogMs >= 1000
        && diagLastInputLogMs_.compare_exchange_strong(lastLogMs, nowMs, std::memory_order_relaxed))
    {
        const auto recDiag = recordingEngine_.getDiagnosticsSnapshot();
        juce::Logger::writeToLog("[APEX-DIAG-IN] cb=" + juce::String(diagInputCallbackCount_.load(std::memory_order_relaxed))
            + " inCh=" + juce::String(diagInputChannelsSeen_.load(std::memory_order_relaxed))
            + " expect=" + juce::String(diagInputSamplesExpected_.load(std::memory_order_relaxed))
            + " preserved=" + juce::String(diagInputSamplesPreserved_.load(std::memory_order_relaxed))
            + " short=" + juce::String(diagInputShortBlockCount_.load(std::memory_order_relaxed))
            + " zeroIn=" + juce::String(diagInputZeroBlockCount_.load(std::memory_order_relaxed)));

        juce::Logger::writeToLog("[APEX-DIAG-REC-IN] recBlk=" + juce::String(recDiag.recBlocks)
            + " offered=" + juce::String(recDiag.recSamplesOffered)
            + " shortVsCb=" + juce::String(recDiag.recShortVsCb)
            + " zeroOffered=" + juce::String(recDiag.recZeroOffered));

        for (const auto& trackDiag : recDiag.tracks)
        {
            juce::Logger::writeToLog("[APEX-DIAG-WR] track=" + trackDiag.trackId
                + " attempt=" + juce::String(trackDiag.writer.pushAttempts)
                + " acc=" + juce::String(trackDiag.writer.pushAccepted)
                + " rej=" + juce::String(trackDiag.writer.pushRejected)
                + " sAttempt=" + juce::String(trackDiag.writer.pushSamplesAttempted)
                + " sAcc=" + juce::String(trackDiag.writer.pushSamplesAccepted)
                + " sRej=" + juce::String(trackDiag.writer.pushSamplesRejected));

            juce::Logger::writeToLog("[APEX-DIAG-DUP] track=" + trackDiag.trackId
                + " dupInBlk=" + juce::String(trackDiag.dupInputBlocks));
        }
    }
#endif

    audioEngine_.clearLiveInputBuffer();
    if (countInFinished)
    {
        juce::WeakReference<ApplicationCore> weakThis(this);
        juce::MessageManager::callAsync([weakThis]()
        {
            if (auto* self = weakThis.get())
                if (self->transport_)
                    self->transport_->recordWithoutSafetyCheck();
        });
    }
    return true;
}

bool ApplicationCore::beginOfflineRender(int blockSize)
{
    lastOfflineRenderError_.clear();

    // Sandboxed plugins are intentionally not part of the offline transport
    // contract yet.  Reject before taking the callback barrier or preparing
    // any render graph so an export cannot silently process a different owner
    // (or silently omit a plugin).
    for (const auto& [trackId, chain] : pluginChains_)
    {
        juce::ignoreUnused(trackId);
        if (! chain)
            continue;

        juce::String chainError;
        if (! chain->supportsOfflineRender(chainError))
        {
            lastOfflineRenderError_ =
                "Offline rendering is not available while sandboxed plugins are active in this build.";
            if (chainError.isNotEmpty())
                lastOfflineRenderError_ += " " + chainError;
            return false;
        }
    }

    offlineRenderReady_.store(false, std::memory_order_release);
    isOfflineRendering_.store(true, std::memory_order_release);

    constexpr uint32_t callbackDrainTimeoutMs = 2000;
    if (!waitForRealtimeDeviceCallbacksToDrain(callbackDrainTimeoutMs))
    {
        lastOfflineRenderError_ = "Could not acquire the offline render callback barrier.";
        isOfflineRendering_.store(false, std::memory_order_release);
        return false;
    }

    offlineRestoreSampleRate_ = currentSampleRate_;
    offlineRestoreBlockSize_ = currentBlockSize_;

    audioEngine_.clearLiveInputBuffer();

    // Publish a fresh routing snapshot so flag changes are visible to the render thread.
    if (routingGraph_)
        routingGraph_->publishSnapshotOnly();

    prepareRenderGraph(currentSampleRate_, blockSize);
    preparePluginChainsForOffline(currentSampleRate_, blockSize);
    logOfflinePluginChains(currentSampleRate_, blockSize);
    audioEngine_.resetOfflineRenderBuffers(blockSize);
    offlineRenderReady_.store(true, std::memory_order_release);

    // ── Zipper-noise pre-roll ──────────────────────────────────────────────
    // All per-track smoothers (VolumeRampCore, MuteFadeCore, connectionGainRamps)
    // were live at some non-zero gain state when the user hit Export.
    // resetOfflineRenderBuffers() resets pitch/DSP cores but does NOT settle
    // the gain smoothers — they need real block calls to ramp to their targets.
    // Run two silent blocks through the engine so every smoother converges
    // before sample 0 is written to the output file.
    {
        juce::AudioBuffer<float> silentBuf(2, blockSize);
        silentBuf.clear();
        renderOfflineBlock(silentBuf, blockSize, 0);
        silentBuf.clear();
        renderOfflineBlock(silentBuf, blockSize, 0);
    }

    for (auto& [id, chain] : pluginChains_)
        if (chain) chain->resetOfflineDebugLogging(3);
    audioEngine_.resetOfflineDebugLogging(3);
    offlineMasterDebugBlocksRemaining_ = 3;
    return true;
}

bool ApplicationCore::beginOfflineStemPass(
    std::shared_ptr<const AudioEngine::OfflineStemRenderMask> mask,
    int blockSize,
    int64_t timelineSample,
    bool applyMasterProcessing)
{
    if (!OfflineRenderBarrierCore::canRender(
            isOfflineRendering_.load(std::memory_order_acquire),
            offlineRenderReady_.load(std::memory_order_acquire))
        || mask == nullptr)
        return false;

    audioEngine_.setOfflineStemRenderMask(std::move(mask));
    audioEngine_.resetOfflineRenderBuffers(blockSize);
    for (auto& [id, chain] : pluginChains_)
        if (chain) chain->reset();
    if (applyMasterProcessing)
        masterBus_.prepare(currentSampleRate_, blockSize);

    juce::AudioBuffer<float> preRoll(2, blockSize);
    for (int i = 0; i < 2; ++i)
    {
        preRoll.clear();
        if (!renderOfflineBlock(preRoll, blockSize, timelineSample, applyMasterProcessing))
            return false;
    }
    return true;
}

bool ApplicationCore::renderOfflineBlock(juce::AudioBuffer<float>& output, int numSamples,
                                         int64_t timelineSample, bool applyMasterProcessing)
{
    if (! OfflineRenderBarrierCore::canRender(
            isOfflineRendering_.load(std::memory_order_acquire),
            offlineRenderReady_.load(std::memory_order_acquire)))
        return false;

    if (output.getNumChannels() < 2 || numSamples <= 0)
        return false;

    output.clear(0, numSamples);
    if (!audioEngine_.renderOfflineBlock(output, numSamples, timelineSample))
        return false;

    float* L = output.getWritePointer(0);
    float* R = output.getWritePointer(1);
    const bool logMaster = offlineMasterDebugBlocksRemaining_ > 0;
    const float masterPreRms = logMaster
        ? 0.5f * (output.getRMSLevel(0, 0, numSamples) + output.getRMSLevel(1, 0, numSamples))
        : 0.0f;
    if (applyMasterProcessing)
        masterBus_.processBlock(L, R, numSamples);
    if (logMaster)
    {
        --offlineMasterDebugBlocksRemaining_;
        int masterPluginCount = 0;
        if (trackManager_ && trackManager_->hasMasterTrack())
            if (auto* chain = getPluginChain(trackManager_->getMasterTrack()->getID()))
                masterPluginCount = chain->getNumSlots();

        const float masterPostRms = 0.5f * (output.getRMSLevel(0, 0, numSamples) + output.getRMSLevel(1, 0, numSamples));
        juce::Logger::writeToLog("[EXPORT MASTER] pluginCount=" + juce::String(masterPluginCount)
            + " preRMS=" + juce::String(masterPreRms)
            + " postRMS=" + juce::String(masterPostRms));
    }
    return true;
}

void ApplicationCore::endOfflineRender()
{
    audioEngine_.clearOfflineStemRenderMask();
    const bool wasReady = offlineRenderReady_.exchange(false, std::memory_order_acq_rel);
    if (!wasReady)
    {
        isOfflineRendering_.store(false, std::memory_order_release);
        return;
    }

    if (transport_ != nullptr)
        pluginPlayheadInfoCore_.updateSnapshot({});

    if (offlineRestoreSampleRate_ > 0.0 && offlineRestoreBlockSize_ > 0)
    {
        restorePluginChainsAfterOffline(offlineRestoreSampleRate_, offlineRestoreBlockSize_);
        prepareRenderGraph(offlineRestoreSampleRate_, offlineRestoreBlockSize_);
    }

    offlineRestoreSampleRate_ = 0.0;
    offlineRestoreBlockSize_ = 0;
    offlineMasterDebugBlocksRemaining_ = 0;

    // Re-enable the device callback path so live playback resumes.
    isOfflineRendering_.store(false, std::memory_order_release);
}

void ApplicationCore::preparePluginChainsForOffline(double sampleRate, int samplesPerBlock)
{
    for (auto& [id, chain] : pluginChains_)
    {
        if (!chain) continue;
        if (trackManager_)
            if (auto* track = trackManager_->getTrack(id))
                chain->setDebugName(track->isMaster() ? "master" : (track->getName() + " [" + id + "]"));
        if (chain->getDebugName().isEmpty())
            chain->setDebugName("chain [" + id + "]");
        syncPluginChainSidechainBusConfig();
        chain->prepareForOffline(sampleRate, samplesPerBlock);
    }

    clipRegionPluginCore_.prepareForOffline(sampleRate, samplesPerBlock);
}

void ApplicationCore::restorePluginChainsAfterOffline(double sampleRate, int samplesPerBlock)
{
    for (auto& [id, chain] : pluginChains_)
    {
        juce::ignoreUnused(id);
        syncPluginChainSidechainBusConfig();
        if (chain) chain->restoreRealtimePrepare(sampleRate, samplesPerBlock);
    }

    clipRegionPluginCore_.restoreRealtimePrepare(sampleRate, samplesPerBlock);
}

void ApplicationCore::logOfflinePluginChains(double sampleRate, int samplesPerBlock) const
{
    for (const auto& [id, chain] : pluginChains_)
    {
        if (!chain || chain->getNumSlots() <= 0) continue;

        juce::String name = chain->getDebugName();
        if (name.isEmpty()) name = "chain [" + id + "]";
        juce::Logger::writeToLog("[EXPORT CHAIN FOUND] name=" + name
            + " id=" + chain->getStableDebugId()
            + " pluginCount=" + juce::String(chain->getNumSlots())
            + " pluginNames=\"" + chain->getPluginNamesForDebug() + "\""
            + " preparedSR=" + juce::String(chain->getPreparedSampleRate())
            + " preparedBlock=" + juce::String(chain->getPreparedBlockSize())
            + " exportSR=" + juce::String(sampleRate)
            + " exportBlock=" + juce::String(samplesPerBlock));
    }
}

void ApplicationCore::reloadAudioFiles(std::function<void(int, int, const juce::String&)> progress)
{
    audioFileManager_.clearAll();
    struct ReloadRequest
    {
        TrackID clipId;
        juce::File source;
        size_t preparedIndex = 0;
    };

    std::vector<ReloadRequest> reloadRequests;
    reloadRequests.reserve((size_t) clipManager_->getAllClips().size());
    std::vector<juce::File> uniqueSources;
    std::unordered_map<std::string, size_t> sourceIndices;
    for (auto* clip : clipManager_->getAllClips())
    {
        if (auto* ac = dynamic_cast<AudioClip*>(clip))
        {
            const auto source = ac->getSourceFile();
            if (source.existsAsFile())
            {
                const auto key = source.getFullPathName().toLowerCase().toStdString();
                auto [it, inserted] = sourceIndices.emplace(key, uniqueSources.size());
                if (inserted)
                    uniqueSources.push_back(source);
                reloadRequests.push_back({ ac->getID(), source, it->second });
            }
        }
    }

    // Decode distinct sources on a bounded control-plane pool. Project restore
    // already owns the realtime suspension gate. Publication remains on the
    // message thread after every immutable worker result is complete.
    std::vector<AudioFileManager::PreparedAudio> prepared(uniqueSources.size());
    if (uniqueSources.size() == 1)
    {
        prepared[0] = audioFileManager_.prepareAudioFile(uniqueSources[0], true);
    }
    else if (!uniqueSources.empty())
    {
        struct PrepareProjectAudioJob final : juce::ThreadPoolJob
        {
            PrepareProjectAudioJob(AudioFileManager& managerToUse,
                                   juce::File sourceToUse,
                                   AudioFileManager::PreparedAudio& destinationToUse)
                : juce::ThreadPoolJob("APEX project audio decode"),
                  manager(managerToUse), source(std::move(sourceToUse)), destination(destinationToUse) {}

            JobStatus runJob() override
            {
                destination = manager.prepareAudioFile(source, true);
                return jobHasFinished;
            }

            AudioFileManager& manager;
            juce::File source;
            AudioFileManager::PreparedAudio& destination;
        };

        const int workerCount = juce::jlimit(1, 4,
            juce::jmax(1, juce::SystemStats::getNumCpus() / 2));
        juce::ThreadPool decodePool(workerCount);
        std::vector<std::unique_ptr<PrepareProjectAudioJob>> jobs;
        jobs.reserve(uniqueSources.size());
        for (size_t i = 0; i < uniqueSources.size(); ++i)
        {
            auto job = std::make_unique<PrepareProjectAudioJob>(audioFileManager_, uniqueSources[i], prepared[i]);
            decodePool.addJob(job.get(), false);
            jobs.push_back(std::move(job));
        }
        for (const auto& job : jobs)
            decodePool.waitForJobToFinish(job.get(), -1);
    }

    const int total = (int) reloadRequests.size();
    int completed = 0;
    for (const auto& request : reloadRequests)
    {
        audioFileManager_.publishPreparedForClip(request.clipId, prepared[request.preparedIndex]);
        ++completed;
        if (progress) progress(completed, total, request.source.getFileName());
    }
}

bool ApplicationCore::capturePluginChainsState(juce::ValueTree& chains,
                                               juce::String& error) const
{
    chains = juce::ValueTree("PluginChains");
    error.clear();
    for (const auto& [trackId, chain] : pluginChains_)
    {
        if (!chain) continue;
        juce::ValueTree trackChain("TrackPlugins");
        trackChain.setProperty("trackId", trackId, nullptr);
        juce::ValueTree chainState;
        juce::String chainError;
        if (! chain->captureStateForPersistence(chainState, chainError))
        {
            error = "Track '" + trackId + "' plugin state capture failed"
                + (chainError.isNotEmpty() ? ": " + chainError : juce::String());
            chains = juce::ValueTree();
            return false;
        }
        trackChain.addChild(chainState, -1, nullptr);
        chains.addChild(trackChain, -1, nullptr);
    }
    return true;
}

juce::ValueTree ApplicationCore::getPluginChainsState() const
{
    juce::ValueTree chains;
    juce::String error;
    if (! capturePluginChainsState(chains, error) && error.isNotEmpty())
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "ApplicationCore::capturePluginChainsState failure error=" + error);
    return chains;
}

bool ApplicationCore::restorePluginChainsState(const juce::ValueTree& chains,
                                               juce::String& error)
{
    error.clear();
    if (!chains.isValid()) return true;

    // Plugin creation, prepareToPlay and setStateInformation are control-plane
    // lifecycle operations. Suppress plugin processing and wait for any
    // in-flight realtime callback before restoring live chains.
    pluginReprepareSuspensionActive_.store(true, std::memory_order_release);
    if (!waitForRealtimeDeviceCallbacksToDrain(2000))
    {
        pluginReprepareSuspensionActive_.store(false, std::memory_order_release);
        error = "Realtime callbacks did not drain within 2000 ms.";
        juce::Logger::writeToLog("[RESTORE] plugin state restore skipped: realtime callbacks did not drain within 2000 ms");
        return false;
    }

    struct RestoreSuspensionGuard
    {
        std::atomic<bool>& flag;
        ~RestoreSuspensionGuard() { flag.store(false, std::memory_order_release); }
    } restoreGuard { pluginReprepareSuspensionActive_ };

    beginPluginChainPublicationBatch();
    struct PluginPublicationBatchGuard
    {
        std::function<void()> finish;
        ~PluginPublicationBatchGuard() { if (finish) finish(); }
    } publicationGuard { [this] { endPluginChainPublicationBatch(); } };

    auto appendRestoreDiagnostic = [&error](const juce::String& diagnostic)
    {
        if (diagnostic.isEmpty())
            return;
        if (error.isNotEmpty())
            error += "; ";
        error += diagnostic;
    };

    for (int i = 0; i < chains.getNumChildren(); ++i)
    {
        auto trackChain = chains.getChild(i);
        if (!trackChain.hasType("TrackPlugins")) continue;

        juce::String trackId = trackChain.getProperty("trackId", "");
        if (trackId.isEmpty()) continue;

        auto pluginChainTree = trackChain.getChildWithName("PluginChain");
        if (!pluginChainTree.isValid()) continue;

        // Get or create the chain for this track
        auto* chain = getPluginChain(trackId);
        if (chain)
        {
            juce::String chainError;
            if (! chain->restoreState(pluginChainTree,
                                      pluginScanner_.getFormatManager(),
                                      chainError))
            {
                appendRestoreDiagnostic(
                    "Track '" + trackId + "' plugin restore had rejected slot(s)"
                        + (chainError.isNotEmpty() ? ": " + chainError : juce::String()));
            }
        }
        else
        {
            appendRestoreDiagnostic(
                "Plugin chain target track '" + trackId + "' is unavailable.");
        }
    }

    syncPluginChainSidechainBusConfig();
    return true;
}

bool ApplicationCore::restorePluginChainsState(const juce::ValueTree& chains)
{
    juce::String error;
    return restorePluginChainsState(chains, error);
}

juce::ValueTree ApplicationCore::getClickStateTree() const
{
    juce::ValueTree state("ClickState");
    state.setProperty("activeMode", clickState_.activeMode.load(std::memory_order_relaxed), nullptr);
    state.setProperty("countInBars", clickState_.countInBars.load(std::memory_order_relaxed), nullptr);
    state.setProperty("volumeLinear", clickState_.volumeLinear.load(std::memory_order_relaxed), nullptr);
    state.setProperty("muted", clickState_.muted.load(std::memory_order_relaxed), nullptr);
    state.setProperty("beatsPerBar", clickState_.beatsPerBar.load(std::memory_order_relaxed), nullptr);
    state.setProperty("beatUnit", clickState_.beatUnit.load(std::memory_order_relaxed), nullptr);
    return state;
}

void ApplicationCore::restoreClickStateTree(const juce::ValueTree& state)
{
    if (!state.isValid()) return;

    clickState_.activeMode.store((int) state.getProperty("activeMode", (int) ClickActiveMode::OnDuringRecord), std::memory_order_relaxed);
    clickState_.countInBars.store((int) state.getProperty("countInBars", (int) ClickCountInBars::OneBar), std::memory_order_relaxed);
    clickState_.volumeLinear.store((float) state.getProperty("volumeLinear", 0.6f), std::memory_order_relaxed);
    clickState_.muted.store((bool) state.getProperty("muted", false), std::memory_order_relaxed);
    clickState_.beatsPerBar.store((int) state.getProperty("beatsPerBar", 4), std::memory_order_relaxed);
    clickState_.beatUnit.store((int) state.getProperty("beatUnit", 4), std::memory_order_relaxed);
}

juce::ValueTree ApplicationCore::getQuickTrackColorsState() const
{
    if (quickTrackColors_ == nullptr)
    {
        juce::ValueTree empty("QuickTrackColors");
        return empty;
    }
    return quickTrackColors_->getState();
}

bool ApplicationCore::restoreQuickTrackColorsState(const juce::ValueTree& colors)
{
    if (quickTrackColors_ == nullptr || !colors.isValid())
        return false;
    // The map is restored as-is: same normalized role key → same color,
    // and no rerandomization of existing families after reopen.
    quickTrackColors_->restoreState(colors);
    return true;
}

void ApplicationCore::resetQuickTrackColorsForNewProject()
{
    if (quickTrackColors_ == nullptr)
        return;
    // Fresh per-project map + fresh palette shuffle: Song B may assign
    // different colors to the same roles without touching the global
    // plugin registry or the application-global template store.
    quickTrackColors_->reset(juce::Random::getSystemRandom().nextInt64());
}

// TransportController::Listener — propagate tempo changes to audio engine for tempo-relative clip playback
void ApplicationCore::tempoChanged(double newTempo)
{
    audioEngine_.setCurrentTempo(newTempo);
}

} // namespace DAW

#include "ApplicationCore.h"
#include "../UICore/CrashRecoveryDialogComponent.h"
#include "../VocalTuneCore/IApexTuneEngineAdapter.h"
#include "../VocalTuneCore/ApexTuneIntegrationCore.h"
#include <unordered_map>

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
    vocalTuneEngineAdapter_ = std::make_unique<ApplicationCoreVocalTuneAdapter>(*this);
    vocalTuneIntegration_ = std::make_unique<apex::vocaltune::ApexTuneIntegrationCore>(*vocalTuneEngineAdapter_);
    appendStartupTrace("initialize.core-subsystems.ready");

    // Project manager — wired to all serializable subsystems
    projectManager_ = std::make_unique<ProjectManager>();
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
    autosaveManager_->setIsExportingQuery([this]() { return isOfflineRendering_.load(); });

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
    audioEngine_.setPluginChains(&pluginChains_);
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
    recordingEngine_.setLivePrintedCachePrepareCallback([this](const juce::Array<TrackID>& trackIds, int numSamples)
    {
        recordingSilenceFallbackAnnounced_.store(false, std::memory_order_release);
        audioEngine_.prepareLivePrintedInputCache(trackIds, numSamples);
    });
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

    DBG("ApplicationCore::shutdown() - Shutting down DAW subsystems");

    // ── Clean shutdown marker — must be before any subsystem teardown ─────────
    if (autosaveManager_) autosaveManager_->shutdown();
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
    audioEngine_.setPluginChains(nullptr);

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
}

void ApplicationCore::syncPluginChainSidechainBusConfig()
{
    std::unordered_map<TrackID, std::unordered_map<int, juce::Array<int>>> busesByTrack;

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

            auto& chain = *chainIt->second;
            int targetSlotIndex = -1;

            if (conn->destinationPluginId.isNotEmpty())
            {
                for (int slotIndex = 0; slotIndex < chain.getNumSlots(); ++slotIndex)
                {
                    auto* slot = chain.getSlot(slotIndex);
                    if (slot != nullptr && slot->getPluginInstanceId() == conn->destinationPluginId)
                    {
                        targetSlotIndex = slotIndex;
                        break;
                    }
                }
            }

            if (targetSlotIndex < 0)
            {
                for (int slotIndex = 0; slotIndex < chain.getNumSlots(); ++slotIndex)
                {
                    auto* slot = chain.getSlot(slotIndex);
                    auto* processor = slot != nullptr ? slot->getProcessor() : nullptr;
                    if (processor != nullptr && processor->getBusCount(true) > conn->destinationBusIndex)
                    {
                        targetSlotIndex = slotIndex;
                        break;
                    }
                }
            }

            if (targetSlotIndex < 0)
                continue;

            auto& busList = busesByTrack[destNode->trackId][targetSlotIndex];
            if (conn->destinationBusIndex > 0 && !busList.contains(conn->destinationBusIndex))
                busList.add(conn->destinationBusIndex);
        }
    }

    for (auto& [trackId, chain] : pluginChains_)
    {
        if (!chain)
            continue;

        auto it = busesByTrack.find(trackId);
        if (it != busesByTrack.end())
            chain->setActiveSidechainBusConfig(it->second);
        else
            chain->setActiveSidechainBusConfig({});
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
        },
        /* onOpenOriginal */
        [pm](const juce::File& origFile)
        {
            if (pm && origFile.existsAsFile())
                pm->loadFromFile(origFile);
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

    // The master track is not a routing source — it's the final output processor.
    // Do not add a routing node for it; its plugins/fader are applied in MasterBusEngine.
    if (track->isMaster()) return;

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

    if (autosaveManager_) autosaveManager_->markDirty("track_added");
    if (projectManager_)  projectManager_->markDirty();
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

    if (trackManager_)
        if (auto* track = trackManager_->getTrack(trackID))
            track->removeListener(this);

    if (auto* node = routingGraph_->getNodeByTrackId(trackID))
        routingGraph_->removeNode(node->id);
    trackPeakMeterManager_.removeTrack(trackID);
    syncPluginChainSidechainBusConfig();
    rebuildFolderBusSnapshot();

    if (autosaveManager_) autosaveManager_->markDirty("track_removed");
    if (projectManager_)  projectManager_->markDirty();
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

    // Mark project dirty (track rename, recolor, mute, solo, arm, etc.)
    if (autosaveManager_) autosaveManager_->markDirty("track_property");
    if (projectManager_)  projectManager_->markDirty();
}

void ApplicationCore::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = samplesPerBlock;

    const int safeBlockSize = juce::jmax(1, samplesPerBlock);
    const int safeInputChannels = juce::jmax(2, hardwareInputChannelCount_);
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
    masterBus_.prepare(sampleRate, samplesPerBlock);
    controlRoom_.prepare(sampleRate, samplesPerBlock);
}

void ApplicationCore::releaseResources()
{
    audioEngine_.releaseResources();
    clipRegionPluginCore_.releaseResources();
    liveInputMonitor_.releaseResources();
    clickEngine_.releaseResources();
    masterBus_.releaseResources();
    controlRoom_.releaseResources();
    DBG("ApplicationCore::releaseResources()");
}

void ApplicationCore::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill,
                                        const juce::AudioBuffer<float>* hardwareInputBuffer,
                                        int hardwareInputNumSamples)
{
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

    static int callbackBlockTraceRemaining = 20;
    if (callbackBlockTraceRemaining > 0)
    {
        --callbackBlockTraceRemaining;
        juce::Logger::writeToLog("[APEX-DIAG-CB] numSamples=" + juce::String(bufferToFill.numSamples)
            + " startSample=" + juce::String(bufferToFill.startSample)
            + " callbackChannels=" + juce::String(bufferToFill.buffer != nullptr ? bufferToFill.buffer->getNumChannels() : 0)
            + " hwInCount=" + juce::String(hardwareInputChannelCount_));
    }

    int inputChannels = 0;
    int preservedChannels = 0;
    int preservedSamples = 0;
    bool hasFreshHardwareInput = false;

    if (hardwareInputBuffer != nullptr)
    {
        inputChannels = hardwareInputBuffer->getNumChannels();
        preservedChannels = juce::jmin(inputChannels, preservedHardwareInput_.getNumChannels());
        preservedSamples = juce::jmin(bufferToFill.numSamples,
                                      hardwareInputNumSamples,
                                      preservedHardwareInput_.getNumSamples());
        if (preservedChannels > 0 && preservedSamples > 0)
        {
            for (int ch = 0; ch < preservedChannels; ++ch)
                preservedHardwareInput_.copyFrom(ch, 0, *hardwareInputBuffer, ch, 0, preservedSamples);

            audioEngine_.setLiveInputBuffer(&preservedHardwareInput_, preservedSamples);
            hasFreshHardwareInput = true;
        }
        else
        {
            audioEngine_.clearLiveInputBuffer();
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

    // In Release builds jassert is a no-op. Guard explicitly so a racing ASIO
    // re-init that fires before the routing graph is ready produces silence
    // instead of an access violation reading 0xFFFFFFFFFFFFFFFF.
    if (routingGraph_ == nullptr || routingGraph_->getMasterNode() == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    const int64_t blockStart = transport_ ? (int64_t) transport_->getPosition() : 0;
    const bool countInFinished = clickEngine_.processBlock(blockStart, bufferToFill.numSamples);
    static bool loggedHardwarePath = false;
    if (!loggedHardwarePath)
    {
        loggedHardwarePath = true;
        DBG("[ApplicationCore] Master node id = master");
        DBG("[ApplicationCore] Hardware output source = post-master device buffer via AudioEngine -> MasterBusEngine -> ControlRoomEngine");
    }

    // Export thread owns the engine while isOfflineRendering_ is set.
    // Output silence from the device callback so the two threads never
    // race on shared AudioEngine buffers (trackBuffer_, nodeBuffers_, etc.).
    // That race was the root cause of heap corruption and 32-sample silent
    // dropouts in exported files.
    if (isOfflineRendering_.load(std::memory_order_acquire))
    {
        bufferToFill.clearActiveBufferRegion();
    }
    else
    {
        if (hasFreshHardwareInput)
            liveInputMonitor_.processBlock(preservedHardwareInput_, preservedSamples);

        audioEngine_.process(bufferToFill);

        if (bufferToFill.buffer && bufferToFill.buffer->getNumChannels() >= 2)
        {
            float* L = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
            float* R = bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);
            const int n = bufferToFill.numSamples;

            if (hasFreshHardwareInput)
                liveInputMonitor_.sumIntoOutput(L, R, juce::jmin(n, preservedSamples));
            masterBus_.processBlock(L, R, n);
            clickEngine_.sumIntoOutput(L, R, n);
        }

        if (recordingEngine_.isActivelyRecording())
        {
            auto copyPrintedWetBlock = [](void* context, const TrackID& trackId, float* destL, float* destR, int numSamples) -> bool
            {
                return static_cast<AudioEngine*>(context)->copyLivePrintedInputBlock(trackId, destL, destR, numSamples);
            };

            const int recordedSamples = juce::jmin(bufferToFill.numSamples, preservedHardwareInput_.getNumSamples());

            // TAPE-MACHINE GUARANTEE: once recording rolls, the recorder is fed
            // EVERY block. If the hardware input vanished this block (device
            // hiccup, zero active input channels, WASAPI session drop), write
            // silence so the take stays sample-aligned and ALWAYS materializes
            // as a clip at stop — a take is never silently discarded again.
            if (!hasFreshHardwareInput && preservedHardwareInput_.getNumChannels() > 0 && recordedSamples > 0)
            {
                preservedHardwareInput_.clear(0, recordedSamples);

                if (!recordingSilenceFallbackAnnounced_.exchange(true, std::memory_order_acq_rel))
                    juce::Logger::writeToLog("[REC-GUARD] No live hardware input while recording - writing silence to keep the take alive. "
                                             "Check input device / Windows microphone privacy settings.");
            }
            else if (hasFreshHardwareInput && recordingSilenceFallbackAnnounced_.exchange(false, std::memory_order_acq_rel))
            {
                juce::Logger::writeToLog("[REC-GUARD] Live hardware input restored - recording real audio again.");
            }

            recordingEngine_.addRecorderInputDiagnosticBlock(bufferToFill.numSamples,
                                                             preservedHardwareInput_.getNumChannels() > 0 ? recordedSamples : 0);
            if (preservedHardwareInput_.getNumChannels() > 0 && recordedSamples > 0)
                recordingEngine_.processBlock(preservedHardwareInput_, recordedSamples, &audioEngine_, copyPrintedWetBlock);
        }

        controlRoom_.processBlock(bufferToFill);
    }

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
}

void ApplicationCore::beginOfflineRender(int blockSize)
{
    // Block the audio device callback from touching the engine while we own it.
    isOfflineRendering_.store(true, std::memory_order_release);

    offlineRestoreSampleRate_ = currentSampleRate_;
    offlineRestoreBlockSize_ = currentBlockSize_;

    // Give any in-flight getNextAudioBlock() call time to exit before we
    // touch shared engine buffers. One device block is at most ~23 ms at 44.1 kHz
    // with a 1024-sample buffer, so 40 ms covers even the largest common block size.
    juce::Thread::sleep(40);

    // Publish a fresh routing snapshot so flag changes are visible to the render thread.
    if (routingGraph_)
        routingGraph_->publishSnapshotOnly();

    prepareRenderGraph(currentSampleRate_, blockSize);
    preparePluginChainsForOffline(currentSampleRate_, blockSize);
    logOfflinePluginChains(currentSampleRate_, blockSize);
    audioEngine_.resetOfflineRenderBuffers(blockSize);

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
}

bool ApplicationCore::renderOfflineBlock(juce::AudioBuffer<float>& output, int numSamples, int64_t timelineSample)
{
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

void ApplicationCore::reloadAudioFiles()
{
    audioFileManager_.clearAll();
    for (auto* clip : clipManager_->getAllClips())
    {
        if (auto* ac = dynamic_cast<AudioClip*>(clip))
        {
            if (ac->getSourceFile().existsAsFile())
                audioFileManager_.loadForClip(ac->getID(), ac->getSourceFile());
        }
    }
}

juce::ValueTree ApplicationCore::getPluginChainsState() const
{
    juce::ValueTree chains("PluginChains");
    for (const auto& [trackId, chain] : pluginChains_)
    {
        if (!chain) continue;
        juce::ValueTree trackChain("TrackPlugins");
        trackChain.setProperty("trackId", trackId, nullptr);
        trackChain.addChild(chain->getState(), -1, nullptr);
        chains.addChild(trackChain, -1, nullptr);
    }
    return chains;
}

void ApplicationCore::restorePluginChainsState(const juce::ValueTree& chains)
{
    if (!chains.isValid()) return;

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
            chain->restoreState(pluginChainTree, pluginScanner_.getFormatManager());
    }

    syncPluginChainSidechainBusConfig();
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

// TransportController::Listener — propagate tempo changes to audio engine for tempo-relative clip playback
void ApplicationCore::tempoChanged(double newTempo)
{
    audioEngine_.setCurrentTempo(newTempo);
}

} // namespace DAW

#pragma once

#include "PluginSandboxAudioTransportShared.h"
#include "PluginSandboxAutomationTransportShared.h"   // Phase E2A (additive)
#include "PluginSandboxDiagnosticsCore.h"

#include <atomic>
#include <memory>
#include <mutex>

#if JUCE_WINDOWS
 #include <TlHelp32.h>
 #include <functional>
#endif

namespace DAW {

/** Worker-only owner of a hosted VST3 module and instance.

    This type is never constructed by the parent proxy. Creation, bus layout,
    prepare/process/release, and destruction all remain in the worker process.
*/
class PluginWorkerHostedPluginCore
{
public:
    struct EditorInfo
    {
        std::uint64_t nativeWindowHandle = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    struct AsyncEditorStatus
    {
        PluginSandboxWin32::EditorAsyncStatus status =
            PluginSandboxWin32::EditorAsyncStatus::Closed;
        EditorInfo info;
        std::uint64_t requestSequence = 0;
        bool desiredOpen = false;
        juce::String pluginInstanceId;
        juce::String error;
    };

    /**
        Validate only the filesystem form and basic identity fields of a VST3
        request.  Windows VST3 components may be represented by either a
        .vst3 directory/bundle or a regular .vst3 file (for example, a vendor
        shell).  Component enumeration and exact identity matching remain the
        responsibility of create() after this gate.
    */
    static bool isValidVst3PathForm(const juce::String& requestedPath,
                                    const juce::String& requestedFormat,
                                    const juce::String& requestedName)
    {
        const juce::File bundle(requestedPath);
        const bool supportedFileSystemForm = bundle.existsAsFile()
                                           || bundle.isDirectory();
        return requestedFormat == "VST3"
            && requestedName.isNotEmpty()
            && juce::File::isAbsolutePath(requestedPath)
            && bundle.getFileExtension().equalsIgnoreCase(".vst3")
            && supportedFileSystemForm;
    }

    bool create(const PluginSandboxWin32::PluginCreateMessage& request,
                juce::String& error,
                bool failBeforeInstanceForTest = false)
    {
       #if JUCE_WINDOWS && JUCE_PLUGINHOST_VST3
        const auto recordPhase = [&](const char* phase,
                                     const juce::String& detail = {})
        {
            PluginSandboxDiagnosticsCore::appendPluginCreatePhase(
                juce::String::fromUTF8(request.sessionToken),
                static_cast<std::uint32_t>(GetCurrentProcessId()),
                request.audioGeneration,
                phase,
                detail);
        };

        close();

        juce::String requestedPath;
        juce::String requestedName;
        juce::String requestedManufacturer;
        juce::String requestedVersion;
        juce::String requestedFormat;
        if (! PluginSandboxWin32::readBoundedUtf8(request.fileOrIdentifier,
                                                   requestedPath, error,
                                                   "fileOrIdentifier")
            || ! PluginSandboxWin32::readBoundedUtf8(request.name,
                                                      requestedName, error, "name")
            || ! PluginSandboxWin32::readBoundedUtf8(request.manufacturer,
                                                      requestedManufacturer, error,
                                                      "manufacturer")
            || ! PluginSandboxWin32::readBoundedUtf8(request.version,
                                                      requestedVersion, error, "version")
            || ! PluginSandboxWin32::readBoundedUtf8(request.pluginFormat,
                                                      requestedFormat, error,
                                                      "pluginFormat"))
            return false;

        const juce::File bundle(requestedPath);
        if (! isValidVst3PathForm(requestedPath, requestedFormat, requestedName))
        {
            error = "worker rejected invalid or non-VST3 plugin identity";
            recordPhase("VST3_PATH_VALIDATION_FAILURE", error);
            return false;
        }
        recordPhase("VST3_PATH_VALIDATED");

        if (request.audioGeneration == 0
            || request.sampleRate < 8000.0 || request.sampleRate > 384000.0
            || request.maximumBlockSamples == 0
            || request.maximumBlockSamples
                   > PluginSandboxAudioShared::kPhysicalMaxSamples
            || request.inputChannels == 0
            || request.outputChannels == 0
            || request.inputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels
            || request.outputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels)
        {
            error = "worker rejected invalid plugin preparation metadata";
            recordPhase("PREPARATION_VALIDATION_FAILURE", error);
            return false;
        }

        recordPhase("VST3_ENUMERATION_BEGIN");
        formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
        auto* format = formatManager_.getFormat(0);
        if (format == nullptr)
        {
            error = "worker VST3 format is unavailable";
            recordPhase("VST3_ENUMERATION_FAILURE", error);
            return false;
        }

        juce::OwnedArray<juce::PluginDescription> discovered;
        format->findAllTypesForFile(discovered, bundle.getFullPathName());

        juce::PluginDescription selected;
        int exactMatches = 0;
        int componentMatches = 0;
        juce::PluginDescription discoveredIdentity;
        for (const auto* candidate : discovered)
        {
            if (candidate == nullptr
                || candidate->pluginFormatName != "VST3"
                || candidate->name != requestedName
                || (requestedManufacturer.isNotEmpty()
                    && candidate->manufacturerName != requestedManufacturer)
                || (requestedVersion.isNotEmpty()
                    && candidate->version != requestedVersion))
                continue;

            if (componentMatches++ == 0)
                discoveredIdentity = *candidate;
            if ((request.uniqueId != 0 && candidate->uniqueId != request.uniqueId)
                || (request.deprecatedUid != 0
                    && candidate->deprecatedUid != request.deprecatedUid))
                continue;

            selected = *candidate;
            ++exactMatches;
        }

        recordPhase("VST3_ENUMERATION_COMPLETE",
                    "componentMatches=" + juce::String(componentMatches)
                        + " exactMatches=" + juce::String(exactMatches));

        if (exactMatches != 1)
        {
            error = "worker did not resolve exactly one requested VST3 component"
                " requestedUniqueId=" + juce::String(request.uniqueId)
                + " discoveredUniqueId=" + juce::String(discoveredIdentity.uniqueId)
                + " discoveredDeprecatedUid=" + juce::String(discoveredIdentity.deprecatedUid)
                + " componentMatches=" + juce::String(componentMatches);
            recordPhase("IDENTITY_MATCH_FAILURE", error);
            return false;
        }
        recordPhase("IDENTITY_MATCH_COMPLETE");

        juce::String creationError;
        recordPhase("CREATE_INSTANCE_BEGIN");
       #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (failBeforeInstanceForTest)
        {
            error = "test-injected PluginCreate instance failure";
            recordPhase("CREATE_INSTANCE_FAILURE", error);
            return false;
        }
       #endif
        instance_ = formatManager_.createPluginInstance(selected,
                                                         request.sampleRate,
                                                         static_cast<int>(request.maximumBlockSamples),
                                                         creationError);
        if (instance_ == nullptr)
        {
            error = "worker VST3 creation failed: " + creationError;
            recordPhase("CREATE_INSTANCE_FAILURE", error);
            return false;
        }
        recordPhase("CREATE_INSTANCE_COMPLETE");

        moduleLoadedInWorker_ = isModuleLoadedFromBundle(bundle);

        description_ = selected;
        inputChannels_ = static_cast<int>(request.inputChannels);
        outputChannels_ = static_cast<int>(request.outputChannels);
        maximumBlockSamples_ = static_cast<int>(request.maximumBlockSamples);
        sampleRate_ = request.sampleRate;
        generation_ = request.audioGeneration;

        recordPhase("LAYOUT_PREPARE_BEGIN");
        if (! prepareCurrentLayout(error))
        {
            recordPhase("LAYOUT_PREPARE_FAILURE", error);
            close();
            return false;
        }
        recordPhase("LAYOUT_PREPARE_COMPLETE");

        // Phase E2A: pre-resolve compact parameter ordinals → real parameter
        // pointers. Bounded by kMaxAutomationParameters; RT never resolves.
        recordPhase("PARAMETER_BIND_BEGIN");
        parameterTargetCount_ = 0;
        std::fill(std::begin(parameterTargets_), std::end(parameterTargets_), nullptr);
        {
            const auto& params = instance_->getParameters();
            const auto count = juce::jmin<int>(params.size(),
                static_cast<int>(
                    PluginSandboxAutomationShared::kMaxAutomationParameters));
            for (int i = 0; i < count; ++i)
                parameterTargets_[i] = params[i];
            parameterTargetCount_ = static_cast<std::uint32_t>(count);
        }
        recordPhase("PARAMETER_BIND_COMPLETE",
                    "count=" + juce::String(static_cast<juce::int64>(
                        parameterTargetCount_)));
        return true;
       #else
        juce::ignoreUnused(request);
        juce::ignoreUnused(failBeforeInstanceForTest);
        error = "worker VST3 hosting is unavailable in this build";
        return false;
       #endif
    }

    bool reprepare(double sampleRate,
                   int maximumBlockSamples,
                   int inputChannels,
                   int outputChannels,
                   std::uint64_t generation,
                   juce::String& error)
    {
        if (instance_ == nullptr || generation == 0
            || maximumBlockSamples <= 0
            || maximumBlockSamples > PluginSandboxAudioShared::kPhysicalMaxSamples
            || inputChannels <= 0 || outputChannels <= 0
            || inputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels
            || outputChannels > PluginSandboxAudioShared::kPhysicalMaxChannels)
        {
            error = "worker rejected invalid VST3 reprepare metadata";
            return false;
        }

        releaseResources();
        sampleRate_ = sampleRate;
        maximumBlockSamples_ = maximumBlockSamples;
        inputChannels_ = inputChannels;
        outputChannels_ = outputChannels;
        generation_ = generation;
        return prepareCurrentLayout(error);
    }

    void process(PluginSandboxAudioShared::SharedAudioSlot& slot)
    {
        if (! prepared_ || instance_ == nullptr
            || slot.metadata.generation != generation_)
        {
            slot.metadata.flags |= PluginSandboxAudioShared::SlotFlagInvalidMetadata;
            return;
        }

        const int samples = static_cast<int>(slot.metadata.numSamples);
        const int inputChannels = static_cast<int>(slot.metadata.inputChannels);
        const int outputChannels = static_cast<int>(slot.metadata.outputChannels);
        if (samples <= 0 || samples > maximumBlockSamples_
            || inputChannels != inputChannels_
            || outputChannels != outputChannels_)
        {
            slot.metadata.flags |= PluginSandboxAudioShared::SlotFlagInvalidMetadata;
            return;
        }

        for (int channel = 0; channel < processChannels_; ++channel)
        {
            auto* destination = processBuffer_.getWritePointer(channel);
            if (channel < inputChannels)
                std::copy_n(PluginSandboxAudioShared::channelData(slot.input,
                                                                  static_cast<std::uint32_t>(channel)),
                            samples, destination);
            else
                std::fill_n(destination, samples, 0.0f);
        }

        juce::AudioBuffer<float> active(processBuffer_.getArrayOfWritePointers(),
                                        processChannels_, samples);
        midi_.clear();
        // Deliberately uncaught: a plugin exception or native fault terminates
        // only this worker. The parent realtime path retains bounded fallback.
        instance_->processBlock(active, midi_);

        for (int channel = 0; channel < outputChannels; ++channel)
            std::copy_n(processBuffer_.getReadPointer(channel), samples,
                        PluginSandboxAudioShared::channelData(slot.output,
                                                              static_cast<std::uint32_t>(channel)));
    }

    /** Phase E2A: process one continuous sub-block slice [startSample,
        startSample + numSamples) of the prepared quantum. Same persistent
        instance, same prepared state — no prepare/reset/latency change
        between segments. The AudioBuffer view is non-owning (proven
        allocation-free by the existing whole-quantum path). */
    void processSlice(PluginSandboxAudioShared::SharedAudioSlot& slot,
                      int startSample,
                      int numSamples) noexcept
    {
        if (! prepared_ || instance_ == nullptr || numSamples <= 0
            || startSample < 0
            || startSample + numSamples > static_cast<int>(slot.metadata.numSamples))
            return;

        const int inputChannels = static_cast<int>(slot.metadata.inputChannels);
        const int outputChannels = static_cast<int>(slot.metadata.outputChannels);

        for (int channel = 0; channel < processChannels_; ++channel)
        {
            auto* destination = processBuffer_.getWritePointer(channel);
            if (channel < inputChannels)
                std::copy_n(PluginSandboxAudioShared::channelData(
                                slot.input, static_cast<std::uint32_t>(channel))
                                + startSample,
                            numSamples, destination);
            else
                std::fill_n(destination, numSamples, 0.0f);
        }

        juce::AudioBuffer<float> active(processBuffer_.getArrayOfWritePointers(),
                                        processChannels_, numSamples);
        midi_.clear();
        // Deliberately uncaught (same contract as the whole-quantum path).
        instance_->processBlock(active, midi_);

        for (int channel = 0; channel < outputChannels; ++channel)
            std::copy_n(processBuffer_.getReadPointer(channel), numSamples,
                        PluginSandboxAudioShared::channelData(
                            slot.output, static_cast<std::uint32_t>(channel))
                            + startSample);
    }

    /** Phase E2A: pre-resolved parameter targets for compact RT ordinals.
        Filled once at create() — the RT path never searches by string. */
    std::uint32_t parameterTargetCount() const noexcept { return parameterTargetCount_; }
    juce::AudioProcessorParameter* const* parameterTargets() const noexcept
    {
        return parameterTargets_;
    }

    void releaseResources() noexcept
    {
        if (instance_ != nullptr && prepared_)
        {
            instance_->releaseResources();
            prepared_ = false;
        }
    }

    void close() noexcept
    {
       #if JUCE_WINDOWS
        if (editor_ != nullptr)
        {
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            jassert(mm != nullptr && mm->isThisTheMessageThread());
            if (mm != nullptr && mm->isThisTheMessageThread())
                destroyEditorOnMessageThread();
        }
       #endif
        releaseResources();
        instance_.reset();
        processBuffer_.setSize(0, 0);
        midi_.clear();
        description_ = {};
        inputChannels_ = 0;
        outputChannels_ = 0;
        processChannels_ = 0;
        maximumBlockSamples_ = 0;
        sampleRate_ = 0.0;
        generation_ = 0;
        moduleLoadedInWorker_ = false;
    }

    /** Phase F1: create or reuse the worker-owned editor. This method may be
        called by the worker service thread; all JUCE editor operations are
        synchronously marshalled to the worker MessageManager thread. */
    bool createEditorOnMessageThread(const juce::String& pluginInstanceId,
                                     EditorInfo& result,
                                     DWORD timeoutMs,
                                     juce::String& error,
                                     std::uint32_t testDelayMilliseconds = 0)
    {
       #if JUCE_WINDOWS
        EditorOperationResult operation;
        if (! invokeOnMessageThread(
                 [this, pluginInstanceId, testDelayMilliseconds]
                 (EditorOperationResult& target)
                 {
                    #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                     if (testDelayMilliseconds > 0)
                         Sleep(testDelayMilliseconds);
                    #endif

                     if (pluginInstanceId.isEmpty() || instance_ == nullptr)
                    {
                        target.error = "worker has no plugin for editor create";
                        return;
                    }

                    if (editor_ != nullptr)
                    {
                        if (editorPluginInstanceId_ != pluginInstanceId)
                        {
                            target.error = "editor identity is already bound to another plugin";
                            return;
                        }
                        target.info = currentEditorInfoOnMessageThread();
                        if (target.info.nativeWindowHandle != 0)
                        {
                            target.success = true;
                            return;
                        }

                        // A top-level native peer can disappear through its
                        // window manager without an IPC close request. Reconcile
                        // that worker-owned stale component before creating the
                        // replacement; the parent never destroys the HWND.
                        destroyEditorOnMessageThread();
                    }

                    auto* created = instance_->createEditor();
                    if (created == nullptr)
                    {
                        target.error = "worker plugin does not provide an editor";
                        return;
                    }

                    editor_.reset(created);
                    editorPluginInstanceId_ = pluginInstanceId;
                    const int width = juce::jmax(1, editor_->getWidth());
                    const int height = juce::jmax(1, editor_->getHeight());
                     const auto canonicalTitle = description_.name.isNotEmpty()
                         ? description_.name
                         : instance_->getName();
                     editor_->setName(canonicalTitle);
                     editor_->setSize(width, height);
                     editor_->addToDesktop(juce::ComponentPeer::windowIsTemporary
                                           | juce::ComponentPeer::windowHasTitleBar
                                           | juce::ComponentPeer::windowHasCloseButton);
                     if (auto* peer = editor_->getPeer())
                     {
                         auto* hwnd = reinterpret_cast<HWND>(peer->getNativeHandle());
                         if (hwnd != nullptr && canonicalTitle.isNotEmpty())
                             SetWindowTextW(hwnd, canonicalTitle.toWideCharPointer());
                     }
                     editor_->setVisible(true);
                    editor_->toFront(true);
                    target.info = currentEditorInfoOnMessageThread();
                    target.success = target.info.nativeWindowHandle != 0;
                    if (! target.success)
                    {
                        target.error = "worker editor did not create a native window";
                        destroyEditorOnMessageThread();
                    }
                }, timeoutMs, operation))
        {
            error = operation.error;
            return false;
        }
        if (! operation.success)
        {
            error = operation.error.isNotEmpty()
                ? operation.error : "worker rejected editor create";
            return false;
        }
        result = operation.info;
        return true;
       #else
         juce::ignoreUnused(pluginInstanceId, result, timeoutMs,
                            testDelayMilliseconds);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    bool resizeEditorOnMessageThread(const juce::String& pluginInstanceId,
                                     std::uint64_t nativeWindowHandle,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     EditorInfo& result,
                                     DWORD timeoutMs,
                                     juce::String& error,
                                     std::uint32_t testDelayMilliseconds = 0)
    {
       #if JUCE_WINDOWS
        EditorOperationResult operation;
        if (! invokeOnMessageThread(
                 [this, pluginInstanceId, nativeWindowHandle, width, height,
                  testDelayMilliseconds]
                 (EditorOperationResult& target)
                 {
                    #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                     if (testDelayMilliseconds > 0)
                         Sleep(testDelayMilliseconds);
                    #endif

                    if (editor_ == nullptr
                        || editorPluginInstanceId_ != pluginInstanceId)
                    {
                        target.error = "worker editor identity is not active";
                        return;
                    }
                    const auto current = currentEditorInfoOnMessageThread();
                    if (current.nativeWindowHandle != nativeWindowHandle)
                    {
                        target.error = "worker editor HWND identity mismatch";
                        return;
                    }
                    if (width == 0 || height == 0 || width > 4096 || height > 4096)
                    {
                        target.error = "worker editor dimensions are out of bounds";
                        return;
                    }
                    editor_->setSize(static_cast<int>(width), static_cast<int>(height));
                    target.info = currentEditorInfoOnMessageThread();
                    target.success = true;
                }, timeoutMs, operation))
        {
            error = operation.error;
            return false;
        }
        if (! operation.success)
        {
            error = operation.error.isNotEmpty()
                ? operation.error : "worker rejected editor resize";
            return false;
        }
        result = operation.info;
        return true;
       #else
         juce::ignoreUnused(pluginInstanceId, nativeWindowHandle, width, height,
                            result, timeoutMs, testDelayMilliseconds);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    bool closeEditorOnMessageThread(const juce::String& pluginInstanceId,
                                    std::uint64_t nativeWindowHandle,
                                    DWORD timeoutMs,
                                    juce::String& error,
                                    std::uint32_t testDelayMilliseconds = 0)
    {
       #if JUCE_WINDOWS
        EditorOperationResult operation;
        if (! invokeOnMessageThread(
                 [this, pluginInstanceId, nativeWindowHandle, testDelayMilliseconds]
                 (EditorOperationResult& target)
                 {
                    #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
                     if (testDelayMilliseconds > 0)
                         Sleep(testDelayMilliseconds);
                    #endif

                    if (editor_ == nullptr
                        || editorPluginInstanceId_ != pluginInstanceId)
                    {
                        target.error = "worker editor identity is not active";
                        return;
                    }
                    if (currentEditorInfoOnMessageThread().nativeWindowHandle
                            != nativeWindowHandle)
                    {
                        target.error = "worker editor HWND identity mismatch";
                        return;
                    }
                    destroyEditorOnMessageThread();
                    target.success = true;
                }, timeoutMs, operation))
        {
            error = operation.error;
            return false;
        }
        if (! operation.success)
        {
            error = operation.error.isNotEmpty()
                ? operation.error : "worker rejected editor close";
            return false;
        }
        return true;
       #else
         juce::ignoreUnused(pluginInstanceId, nativeWindowHandle, timeoutMs,
                            testDelayMilliseconds);
        error = "worker editor hosting is Windows-only";
        return false;
        #endif
    }

    /** Phase F3 submit. This method only records bounded lifecycle intent and
        posts the actual vendor editor construction to the worker message
        thread. It never waits for createEditor(). */
    bool submitEditorOpenAsync(const juce::String& pluginInstanceId,
                               std::uint64_t requestSequence,
                               std::uint32_t testDelayMilliseconds,
                               AsyncEditorStatus& result,
                               juce::String& error)
    {
       #if JUCE_WINDOWS
        result = {};
        if (pluginInstanceId.isEmpty() || requestSequence == 0
            || instance_ == nullptr)
        {
            error = "worker rejected invalid Phase F3 editor submit identity";
            return false;
        }

        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (asyncPluginInstanceId_ != pluginInstanceId
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Closed
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Cancelled
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Failed)
            {
                error = "worker editor is bound to another request";
                return false;
            }

            if (asyncPluginInstanceId_ == pluginInstanceId
                && asyncRequestSequence_ == requestSequence
                && asyncDesiredOpen_
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::CancelPending
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Cancelled)
            {
                result = asyncEditorStatusLocked();
                result.status = result.status == PluginSandboxWin32::EditorAsyncStatus::Ready
                    ? PluginSandboxWin32::EditorAsyncStatus::AlreadyReady
                    : PluginSandboxWin32::EditorAsyncStatus::AlreadyCreating;
                return true;
            }

            asyncPluginInstanceId_ = pluginInstanceId;
            asyncRequestSequence_ = requestSequence;
            asyncDesiredOpen_ = true;
            asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Accepted;
            asyncInfo_ = {};
            asyncError_.clear();
            asyncCancelRequested_.store(false, std::memory_order_release);
            result = asyncEditorStatusLocked();
        }

        if (! juce::MessageManager::callAsync(
                [this, pluginInstanceId, requestSequence, testDelayMilliseconds]
                {
                    runAsyncEditorCreateOnMessageThread(pluginInstanceId,
                                                        requestSequence,
                                                        testDelayMilliseconds);
                }))
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (asyncRequestSequence_ == requestSequence
                && asyncPluginInstanceId_ == pluginInstanceId)
            {
                asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Failed;
                asyncDesiredOpen_ = false;
                asyncError_ = "worker MessageManager rejected asynchronous editor submit";
            }
            error = "worker MessageManager rejected asynchronous editor submit";
            return false;
        }
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence,
                           testDelayMilliseconds, result);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    /** Phase F3 status query. This is a bounded snapshot operation; it never
        performs vendor work and may be called by the worker service thread. */
    bool queryEditorStatusAsync(const juce::String& pluginInstanceId,
                                std::uint64_t requestSequence,
                                AsyncEditorStatus& result,
                                juce::String& error) const
    {
       #if JUCE_WINDOWS
        result = {};
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (pluginInstanceId != asyncPluginInstanceId_
            || requestSequence != asyncRequestSequence_)
        {
            result.status = PluginSandboxWin32::EditorAsyncStatus::Stale;
            result.requestSequence = requestSequence;
            result.pluginInstanceId = pluginInstanceId;
            result.error = "worker editor status request is stale";
            return true;
        }
        result = asyncEditorStatusLocked();
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    /** Phase F3 cancel. Cancellation is intent, not a synchronous destruction
        request. The worker message thread reconciles any editor it may have
        created before publishing a final Cancelled/Closed state. */
    bool cancelEditorAsync(const juce::String& pluginInstanceId,
                           std::uint64_t requestSequence,
                           AsyncEditorStatus& result,
                           juce::String& error)
    {
       #if JUCE_WINDOWS
        result = {};
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (pluginInstanceId != asyncPluginInstanceId_
                || requestSequence != asyncRequestSequence_)
            {
                result.status = PluginSandboxWin32::EditorAsyncStatus::Stale;
                result.requestSequence = requestSequence;
                result.pluginInstanceId = pluginInstanceId;
                result.error = "worker editor cancel request is stale";
                return true;
            }

            asyncDesiredOpen_ = false;
            asyncCancelRequested_.store(true, std::memory_order_release);
            if (asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Closed
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Cancelled
                && asyncState_ != PluginSandboxWin32::EditorAsyncStatus::Failed)
                asyncState_ = PluginSandboxWin32::EditorAsyncStatus::CancelPending;
            result = asyncEditorStatusLocked();
        }

        if (! juce::MessageManager::callAsync(
                [this, pluginInstanceId, requestSequence]
                {
                    finishAsyncEditorCancelOnMessageThread(pluginInstanceId,
                                                            requestSequence);
                }))
        {
            // A queued create callback still observes asyncCancelRequested_.
            // Preserve the intent and let the normal callback settle it.
            error = "worker MessageManager rejected asynchronous editor cancel";
            return false;
        }
        return true;
       #else
        juce::ignoreUnused(pluginInstanceId, requestSequence, result);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    /** Shutdown-only close path. It is intentionally identity-free because
        the worker is already stopping and the worker-owned editor must be
        destroyed before the hosted AudioPluginInstance. */
    bool closeEditorForShutdown(DWORD timeoutMs, juce::String& error)
    {
       #if JUCE_WINDOWS
        EditorOperationResult operation;
        if (! invokeOnMessageThread(
                [this](EditorOperationResult& target)
                {
                    if (editor_ != nullptr)
                        destroyEditorOnMessageThread();
                    target.success = true;
                }, timeoutMs, operation))
        {
            error = operation.error;
            return false;
        }
        if (! operation.success)
        {
            error = operation.error.isNotEmpty()
                ? operation.error : "worker editor shutdown failed";
            return false;
        }
        return true;
       #else
        juce::ignoreUnused(timeoutMs);
        error = "worker editor hosting is Windows-only";
        return false;
       #endif
    }

    // ── Phase E1: parameter / state control (control plane only) ─────────────
    // The worker main loop calls these between audio quanta; the parent audio
    // callback never touches this code path.

    /** Serialize automatable-parameter metadata into a bounded payload:
        count, then per entry: index, id, display name, default value,
        step count, discrete/boolean flags. */
    bool buildParameterMetadata(juce::MemoryBlock& payload, juce::String& error) const
    {
        payload.reset();
        if (instance_ == nullptr)
        {
            error = "worker has no plugin instance";
            return false;
        }

        const auto& params = instance_->getParameters();
        int automatable = 0;
        for (auto* p : params)
            if (p != nullptr && p->isAutomatable())
                ++automatable;
        if (automatable > static_cast<int>(
                PluginSandboxWin32::kE1MaximumParameters))
        {
            error = "plugin parameter count exceeds the E1 bound";
            return false;
        }

        juce::MemoryOutputStream stream(payload, false);
        stream.writeInt(automatable);
        for (auto* p : params)
        {
            if (p == nullptr || ! p->isAutomatable())
                continue;
            const int index = p->getParameterIndex();
            juce::String parameterId;
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                parameterId = withId->paramID;
            if (parameterId.isEmpty())
                parameterId = "param_" + juce::String(index);
            const juce::String name = p->getName(128);

            stream.writeInt(index);
            stream.writeString(parameterId);
            stream.writeString(name);
            stream.writeFloat(p->getDefaultValue());
            stream.writeInt(p->getNumSteps());
            stream.writeByte(static_cast<char>((p->isDiscrete() ? 1 : 0)
                                             | (p->isBoolean() ? 2 : 0)));
        }
        return true;
    }

    /** Resolve a parameter by its stable ID (JUCE paramID, falling back to
        the "param_N" index form). Never by display name. */
    juce::AudioProcessorParameter* findParameterById(const juce::String& parameterId) const noexcept
    {
        if (instance_ == nullptr || parameterId.isEmpty())
            return nullptr;

        const auto& params = instance_->getParameters();
        for (auto* p : params)
        {
            if (p == nullptr)
                continue;
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (withId->paramID == parameterId)
                    return p;
        }
        if (parameterId.startsWith("param_"))
        {
            const int index = parameterId.substring(6).getIntValue();
            if (index >= 0 && index < params.size())
                return params[index];
        }
        return nullptr;
    }

    bool setParameter(const juce::String& parameterId,
                      float normalizedValue,
                      float& appliedValue,
                      juce::String& error) const
    {        if (instance_ == nullptr)
        {
            error = "worker has no plugin instance";
            return false;
        }
        if (normalizedValue < 0.0f || normalizedValue > 1.0f)
        {
            error = "normalized parameter value is out of range";
            return false;
        }
        auto* parameter = findParameterById(parameterId);
        if (parameter == nullptr)
        {
            error = "unknown parameter ID";
            return false;
        }
        parameter->setValueNotifyingHost(normalizedValue);
        appliedValue = parameter->getValue();
        return true;
    }

    /** Phase E2B (control plane): read the ACTUAL live normalized parameter
        values from the hosted AudioProcessor. Bounded to the frozen E2A
        worker automation ordinal space (128 indices). No defaults, no E1
        shadow, no cached metadata values — the live plugin only. */
    bool readLiveParameterValues(juce::MemoryBlock& payload,
                                 juce::String& error) const
    {
        payload.reset();
        if (instance_ == nullptr)
        {
            error = "worker has no plugin instance";
            return false;
        }
        juce::MemoryOutputStream stream(payload, true);
        const auto& params = instance_->getParameters();
        const auto count = juce::jmin<std::uint32_t>(
            static_cast<std::uint32_t>(params.size()),
            PluginSandboxWin32::kE2BMaximumLiveValueEntries);
        stream.writeInt(static_cast<int>(count));
        for (std::uint32_t i = 0; i < count; ++i)
        {
            stream.writeInt(static_cast<int>(i));
            stream.writeFloat(params[i] != nullptr ? params[i]->getValue() : 0.0f);
        }
        return true;
    }

    bool captureState(juce::MemoryBlock& state, juce::String& error) const
    {
        state.reset();
        if (instance_ == nullptr)
        {
            error = "worker has no plugin instance";
            return false;
        }
        instance_->getStateInformation(state);
        if (state.getSize() > static_cast<size_t>(
                PluginSandboxWin32::kE1MaximumPayloadBytes))
        {
            error = "plugin state exceeds the E1 payload bound";
            state.reset();
            return false;
        }
        return true;
    }

    bool restoreState(const void* data, int size, juce::String& error) const
    {
        if (instance_ == nullptr)
        {
            error = "worker has no plugin instance";
            return false;
        }
        if (data == nullptr || size <= 0
            || size > static_cast<int>(PluginSandboxWin32::kE1MaximumPayloadBytes))
        {
            error = "invalid E1 state chunk";
            return false;
        }
        instance_->setStateInformation(data, size);
        return true;
    }

    ~PluginWorkerHostedPluginCore() { close(); }

    bool isPrepared() const noexcept { return prepared_; }    int latencySamples() const noexcept
    {
        return instance_ != nullptr ? juce::jmax(0, instance_->getLatencySamples()) : 0;
    }
    int inputChannels() const noexcept { return inputChannels_; }
    int outputChannels() const noexcept { return outputChannels_; }
    int uniqueId() const noexcept { return description_.uniqueId; }
    int deprecatedUid() const noexcept { return description_.deprecatedUid; }
    bool moduleLoadedInWorker() const noexcept { return moduleLoadedInWorker_; }

private:
    struct EditorOperationResult
    {
        bool success = false;
        EditorInfo info;
        juce::String error;
    };

   #if JUCE_WINDOWS
    struct PendingEditorOperation
    {
        juce::WaitableEvent completed;
        std::atomic<bool> cancelled { false };
        EditorOperationResult result;
    };

    AsyncEditorStatus asyncEditorStatusLocked() const
    {
        AsyncEditorStatus result;
        result.status = asyncState_;
        result.info = asyncInfo_;
        result.requestSequence = asyncRequestSequence_;
        result.desiredOpen = asyncDesiredOpen_;
        result.pluginInstanceId = asyncPluginInstanceId_;
        result.error = asyncError_;
        return result;
    }

    void runAsyncEditorCreateOnMessageThread(
        const juce::String& pluginInstanceId,
        std::uint64_t requestSequence,
        std::uint32_t testDelayMilliseconds)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (asyncPluginInstanceId_ != pluginInstanceId
                || asyncRequestSequence_ != requestSequence)
                return;
            if (! asyncDesiredOpen_
                || asyncCancelRequested_.load(std::memory_order_acquire))
            {
                asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Cancelled;
                asyncInfo_ = {};
                asyncError_.clear();
                return;
            }
            asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Creating;
        }

        EditorInfo info;
        juce::String operationError;
        const bool created = createEditorOnMessageThread(
            pluginInstanceId, info, kPluginSandboxEditorCreateTimeoutMs,
            operationError, testDelayMilliseconds);

        const bool cancelled = asyncCancelRequested_.load(std::memory_order_acquire);
        if (cancelled || ! asyncDesiredOpenForRequest(pluginInstanceId, requestSequence))
        {
            if (created && editor_ != nullptr)
                destroyEditorOnMessageThread();
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            if (asyncPluginInstanceId_ == pluginInstanceId
                && asyncRequestSequence_ == requestSequence)
            {
                asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Cancelled;
                asyncDesiredOpen_ = false;
                asyncInfo_ = {};
                asyncError_.clear();
            }
            return;
        }

        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (asyncPluginInstanceId_ != pluginInstanceId
            || asyncRequestSequence_ != requestSequence)
            return;
        if (created)
        {
            asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Ready;
            asyncInfo_ = info;
            asyncError_.clear();
        }
        else
        {
            asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Failed;
            asyncDesiredOpen_ = false;
            asyncInfo_ = {};
            asyncError_ = operationError.isNotEmpty()
                ? operationError : "worker asynchronous editor create failed";
        }
    }

    bool asyncDesiredOpenForRequest(const juce::String& pluginInstanceId,
                                    std::uint64_t requestSequence) const noexcept
    {
        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        return asyncPluginInstanceId_ == pluginInstanceId
            && asyncRequestSequence_ == requestSequence
            && asyncDesiredOpen_
            && ! asyncCancelRequested_.load(std::memory_order_acquire);
    }

    void finishAsyncEditorCancelOnMessageThread(
        const juce::String& pluginInstanceId,
        std::uint64_t requestSequence)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        const bool matches = [&]
        {
            const std::lock_guard<std::mutex> lock(editorStateMutex_);
            return asyncPluginInstanceId_ == pluginInstanceId
                && asyncRequestSequence_ == requestSequence;
        }();
        if (! matches)
            return;

        if (editor_ != nullptr)
            destroyEditorOnMessageThread();

        const std::lock_guard<std::mutex> lock(editorStateMutex_);
        if (asyncPluginInstanceId_ == pluginInstanceId
            && asyncRequestSequence_ == requestSequence)
        {
            asyncState_ = PluginSandboxWin32::EditorAsyncStatus::Cancelled;
            asyncDesiredOpen_ = false;
            asyncInfo_ = {};
            asyncError_.clear();
        }
    }

    bool invokeOnMessageThread(std::function<void(EditorOperationResult&)>&& operation,
                               DWORD timeoutMs,
                               EditorOperationResult& result)
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr)
        {
            result.error = "worker MessageManager is unavailable";
            return false;
        }
        if (mm->isThisTheMessageThread())
        {
            operation(result);
            return true;
        }

        auto pending = std::make_shared<PendingEditorOperation>();
        const bool posted = juce::MessageManager::callAsync(
            [pending, operation = std::move(operation)]() mutable
            {
                if (! pending->cancelled.load(std::memory_order_acquire))
                    operation(pending->result);
                pending->completed.signal();
            });
        if (! posted)
        {
            result.error = "worker MessageManager rejected editor operation";
            return false;
        }
        if (! pending->completed.wait(timeoutMs))
        {
            pending->cancelled.store(true, std::memory_order_release);
            result.error = "worker GUI editor operation timed out";
            return false;
        }
        result = pending->result;
        return true;
    }

    EditorInfo currentEditorInfoOnMessageThread() const noexcept
    {
        EditorInfo result;
        if (editor_ == nullptr || editor_->getPeer() == nullptr)
            return result;
        result.nativeWindowHandle = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(editor_->getPeer()->getNativeHandle()));
        result.width = static_cast<std::uint32_t>(juce::jmax(0, editor_->getWidth()));
        result.height = static_cast<std::uint32_t>(juce::jmax(0, editor_->getHeight()));
        return result;
    }

    void destroyEditorOnMessageThread() noexcept
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        if (editor_ != nullptr)
        {
            editor_->setVisible(false);
            editor_->removeFromDesktop();
            editor_.reset();
        }
        editorPluginInstanceId_.clear();
    }
   #endif

    bool prepareCurrentLayout(juce::String& error)
    {
        if (instance_ == nullptr)
            return false;

        auto layout = instance_->getBusesLayout();
        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty())
        {
            error = "worker VST3 has no main audio buses";
            return false;
        }

        layout.getChannelSet(true, 0) = channelSetFor(inputChannels_);
        layout.getChannelSet(false, 0) = channelSetFor(outputChannels_);
        for (int bus = 1; bus < layout.inputBuses.size(); ++bus)
            layout.getChannelSet(true, bus) = juce::AudioChannelSet::disabled();
        for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
            layout.getChannelSet(false, bus) = juce::AudioChannelSet::disabled();

        instance_->setRateAndBufferSizeDetails(sampleRate_, maximumBlockSamples_);
        if (! instance_->setBusesLayout(layout))
        {
            error = "worker VST3 rejected the requested main-bus layout";
            return false;
        }

        processChannels_ = juce::jmax(inputChannels_, outputChannels_);
        processBuffer_.setSize(processChannels_, maximumBlockSamples_,
                               false, true, false);
        processBuffer_.clear();
        instance_->prepareToPlay(sampleRate_, maximumBlockSamples_);
        prepared_ = true;
        return true;
    }

    static juce::AudioChannelSet channelSetFor(int channels)
    {
        if (channels == 1) return juce::AudioChannelSet::mono();
        if (channels == 2) return juce::AudioChannelSet::stereo();
        return juce::AudioChannelSet::discreteChannels(channels);
    }

    static bool isModuleLoadedFromBundle(const juce::File& bundle) noexcept
    {
       #if JUCE_WINDOWS
        PluginSandboxWin32::UniqueHandle snapshot(
            CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                     GetCurrentProcessId()));
        if (! snapshot.isValid())
            return false;

        const auto canonicalBundle = bundle.getFullPathName().toLowerCase();
        MODULEENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        if (! Module32FirstW(snapshot.get(), &entry))
            return false;
        do
        {
            const juce::String modulePath(entry.szExePath);
            if (modulePath.toLowerCase().startsWith(canonicalBundle)
                && modulePath.endsWithIgnoreCase(".vst3"))
                return true;
        }
        while (Module32NextW(snapshot.get(), &entry));
       #else
        juce::ignoreUnused(bundle);
       #endif
        return false;
    }

    // Declaration order is intentional: instance_ is destroyed before its
    // format manager and therefore before the loaded VST3 module.
    juce::AudioPluginFormatManager formatManager_;
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    juce::String editorPluginInstanceId_;
    mutable std::mutex editorStateMutex_;
    PluginSandboxWin32::EditorAsyncStatus asyncState_ =
        PluginSandboxWin32::EditorAsyncStatus::Closed;
    EditorInfo asyncInfo_;
    std::uint64_t asyncRequestSequence_ = 0;
    bool asyncDesiredOpen_ = false;
    juce::String asyncPluginInstanceId_;
    juce::String asyncError_;
    std::atomic<bool> asyncCancelRequested_ { false };
    juce::PluginDescription description_;
    juce::AudioBuffer<float> processBuffer_;
    juce::MidiBuffer midi_;
    int inputChannels_ = 0;
    int outputChannels_ = 0;
    int processChannels_ = 0;
    int maximumBlockSamples_ = 0;
    double sampleRate_ = 0.0;
    std::uint64_t generation_ = 0;
    bool prepared_ = false;
    bool moduleLoadedInWorker_ = false;

    // Phase E2A: pre-resolved compact ordinal → parameter pointers (bounded).
    juce::AudioProcessorParameter* parameterTargets_[
        PluginSandboxAutomationShared::kMaxAutomationParameters] {};
    std::uint32_t parameterTargetCount_ = 0;
};

} // namespace DAW

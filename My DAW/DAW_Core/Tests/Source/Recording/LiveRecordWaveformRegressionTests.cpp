#include <JuceHeader.h>
#include <limits>
#include "../../../Source/RecordingCore/LiveRecordWaveformCore.h"
#include "../../../Source/RecordingCore/OfflineRenderBarrierCore.h"
#include "../../../Source/RecordingCore/RecordingCallbackPolicyCore.h"
#include "../../../Source/RecordingCore/RecordingInputValidityCore.h"
#include "../../../Source/RecordingCore/RecordingLifecycleStateCore.h"
#include "../../../Source/RecordingCore/RecordingOverlayStateCore.h"
#include "../../../Source/RecordingCore/RecordingWriterStateCore.h"
#include "../../../Source/RenderCore/ExportPublicationCore.h"
#include "../../../Source/RenderCore/AudioResourceReleaseStateCore.h"
#include "../../../Source/RenderCore/AsyncCompletionStateCore.h"
#include "../../../Source/RenderCore/ExportThreadStartStateCore.h"
#include "../../../Source/RenderCore/ExportShutdownStateCore.h"
#include "../../../Source/DeviceCore/PendingAudioPreparationGateCore.h"

class LiveRecordWaveformRegressionTests final : public juce::UnitTest
{
public:
    LiveRecordWaveformRegressionTests()
        : juce::UnitTest ("recording.live-waveform-low-buffer.v1", "APEX.Recording") {}

    void runTest() override
    {
        beginTest ("valid low-buffer peaks remain visible at supported zooms");

        constexpr double sampleRate = 48000.0;
        const int blockSizes[] = { 64, 128, 512 };
        const double pixelsPerSecond[] = { 5.0, 100.0, 800.0 };

        for (const int blockSize : blockSizes)
        {
            for (const double pps : pixelsPerSecond)
            {
                DAW::LiveRecordWaveformCore waveform;
                waveform.reset (blockSize);
                waveform.setInputAvailable (true);
                for (int i = 0; i < 512; ++i)
                    waveform.pushPeak (-0.75f, 0.75f);

                juce::Image image (juce::Image::ARGB, 512, 80, true);
                {
                    juce::Graphics graphics (image);
                    waveform.draw (graphics,
                                   image.getBounds().toFloat(),
                                   juce::Colours::white,
                                   pps / sampleRate,
                                   512.0 * blockSize);
                }

                bool foundPaintedPixel = false;
                const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < image.getHeight() && !foundPaintedPixel; ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (pixels.getPixelColour (x, y).getAlpha() != 0)
                        {
                            foundPaintedPixel = true;
                            break;
                        }

                expect (foundPaintedPixel,
                        "waveform missing for block=" + juce::String (blockSize)
                        + " pps=" + juce::String (pps));
            }
        }

        beginTest ("invalid pixel scales do not paint");

        struct InvalidScale
        {
            double value;
            const char* name;
        };

        const InvalidScale invalidScales[] = {
            { 0.0, "zero" },
            { -1.0, "negative" },
            { std::numeric_limits<double>::quiet_NaN(), "NaN" },
            { std::numeric_limits<double>::infinity(), "positive infinity" }
        };

        for (const auto& scale : invalidScales)
        {
            DAW::LiveRecordWaveformCore waveform;
            waveform.reset (64);
            waveform.setInputAvailable (true);
            for (int i = 0; i < 512; ++i)
                waveform.pushPeak (-0.75f, 0.75f);

            juce::Image image (juce::Image::ARGB, 512, 80, true);
            {
                juce::Graphics graphics (image);
                waveform.draw (graphics,
                               image.getBounds().toFloat(),
                               juce::Colours::white,
                               scale.value,
                               512.0 * 64.0);
            }

            bool foundPaintedPixel = false;
            const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < image.getHeight() && !foundPaintedPixel; ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    if (pixels.getPixelColour (x, y).getAlpha() != 0)
                    {
                        foundPaintedPixel = true;
                        break;
                    }

            expect (! foundPaintedPixel,
                    "waveform painted for invalid scale=" + juce::String (scale.name));
        }

        beginTest ("prepared capacity is not valid hardware input");
        expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (0, 8), 0);
        expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (2, 8), 2);
        expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (12, 8), 8);

        beginTest ("mono and stereo routes require their opened channels");
        expect (! DAW::RecordingInputValidityCore::isRouteAvailable (0, true, 0));
        expect (DAW::RecordingInputValidityCore::isRouteAvailable (0, true, 1));
        expect (! DAW::RecordingInputValidityCore::isRouteAvailable (0, false, 1));
        expect (DAW::RecordingInputValidityCore::isRouteAvailable (0, false, 2));
        expect (! DAW::RecordingInputValidityCore::isRouteAvailable (2, true, 2));

        beginTest ("unavailable input draws no stale waveform");
        DAW::LiveRecordWaveformCore unavailable;
        unavailable.reset (64);
        unavailable.setInputAvailable (true);
        unavailable.pushPeak (-0.8f, 0.8f);
        unavailable.setInputAvailable (false);
        expect (! unavailable.isInputAvailable());

        juce::Image noInputImage (juce::Image::ARGB, 64, 40, true);
        {
            juce::Graphics noInputGraphics (noInputImage);
            unavailable.draw (noInputGraphics, noInputImage.getBounds().toFloat(),
                              juce::Colours::white, 100.0 / 48000.0, 64.0);
        }

        bool noInputPainted = false;
        const juce::Image::BitmapData noInputPixels (
            noInputImage, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < noInputImage.getHeight() && ! noInputPainted; ++y)
            for (int x = 0; x < noInputImage.getWidth(); ++x)
                if (noInputPixels.getPixelColour (x, y).getAlpha() != 0)
                {
                    noInputPainted = true;
                    break;
                }
        expect (! noInputPainted,
                "unavailable input must not draw a fake or stale waveform");

        beginTest ("reduced valid input clears stale prepared tail channels");
        juce::AudioBuffer<float> preservedInput (2, 16);
        for (int channel = 0; channel < preservedInput.getNumChannels(); ++channel)
            for (int sample = 0; sample < preservedInput.getNumSamples(); ++sample)
                preservedInput.setSample (channel, sample, (float) (channel + 1));

        DAW::RecordingInputValidityCore::clearInvalidTailChannels (
            preservedInput, 1, 0, preservedInput.getNumSamples());

        for (int sample = 0; sample < preservedInput.getNumSamples(); ++sample)
        {
            expectWithinAbsoluteError (preservedInput.getSample (0, sample), 1.0f, 0.0f);
            expectWithinAbsoluteError (preservedInput.getSample (1, sample), 0.0f, 0.0f);
        }
        expect (! DAW::RecordingInputValidityCore::isRouteAvailable (1, true, 1));

        beginTest ("route sources preserve valid input and prepare full-block stereo silence");
        juce::AudioBuffer<float> routeInput (2, 16);
        for (int sample = 0; sample < routeInput.getNumSamples(); ++sample)
        {
            routeInput.setSample (0, sample, 0.1f + (float) sample * 0.01f);
            routeInput.setSample (1, sample, -0.2f - (float) sample * 0.01f);
        }

        juce::AudioBuffer<float> silenceScratch (2, 16);
        silenceScratch.clear();
        const float* routeSources[2] = { nullptr, nullptr };
        expect (DAW::RecordingInputValidityCore::prepareRouteSources (
            routeInput, 0, false, 2, silenceScratch, 16, routeSources));
        for (int sample = 0; sample < routeInput.getNumSamples(); ++sample)
        {
            expectWithinAbsoluteError (routeSources[0][sample], routeInput.getSample (0, sample), 0.0f);
            expectWithinAbsoluteError (routeSources[1][sample], routeInput.getSample (1, sample), 0.0f);
        }

        for (int channel = 0; channel < silenceScratch.getNumChannels(); ++channel)
            for (int sample = 0; sample < silenceScratch.getNumSamples(); ++sample)
                silenceScratch.setSample (channel, sample, 0.75f);
        expect (! DAW::RecordingInputValidityCore::prepareRouteSources (
            routeInput, 1, true, 1, silenceScratch, 16, routeSources));
        for (int sample = 0; sample < silenceScratch.getNumSamples(); ++sample)
        {
            expectWithinAbsoluteError (routeSources[0][sample], 0.0f, 0.0f);
            expectWithinAbsoluteError (routeSources[1][sample], 0.0f, 0.0f);
        }

        for (int channel = 0; channel < silenceScratch.getNumChannels(); ++channel)
            for (int sample = 0; sample < silenceScratch.getNumSamples(); ++sample)
                silenceScratch.setSample (channel, sample, -0.5f);
        expect (! DAW::RecordingInputValidityCore::prepareRouteSources (
            routeInput, 0, false, 1, silenceScratch, 16, routeSources));
        for (int sample = 0; sample < silenceScratch.getNumSamples(); ++sample)
        {
            expectWithinAbsoluteError (routeSources[0][sample], 0.0f, 0.0f);
            expectWithinAbsoluteError (routeSources[1][sample], 0.0f, 0.0f);
        }

        beginTest ("writer-backed take activity is explicit and arm-independent");
        DAW::LiveRecordWaveformCore takeState;
        expect (! takeState.isTakeActive());
        takeState.setTakeActive (true);
        expect (takeState.isTakeActive());
        expect (DAW::RecordingOverlayStateCore::shouldShowOverlay (
            takeState.isTakeActive(), true));
        expect (DAW::RecordingOverlayStateCore::shouldShowOverlay (
            takeState.isTakeActive(), false),
            "disarming must not hide writer-backed take activity");
        expect (! DAW::RecordingOverlayStateCore::shouldShowOverlay (false, true),
                "arming without an active writer must not show an overlay");
        takeState.reset (64);
        expect (! takeState.isTakeActive(), "reset must clear take activity");
        takeState.setTakeActive (true);
        takeState.setTakeActive (false);
        expect (! takeState.isTakeActive(), "explicit stop must clear take activity");

        beginTest ("recorder feed policy ignores graph availability but excludes offline rendering");
        expect (DAW::RecordingCallbackPolicyCore::shouldFeedRecorder (true, false, false));
        expect (DAW::RecordingCallbackPolicyCore::shouldFeedRecorder (true, false, true));
        expect (! DAW::RecordingCallbackPolicyCore::shouldFeedRecorder (true, true, false));
        expect (! DAW::RecordingCallbackPolicyCore::shouldFeedRecorder (false, false, false));

        beginTest ("writer rejection deactivates take and requests one control-thread stop");
        DAW::LiveRecordWaveformCore rejectedTake;
        rejectedTake.reset (64);
        rejectedTake.setInputAvailable (true);
        rejectedTake.setTakeActive (true);
        std::atomic<bool> trackWriterFailed { false };
        std::atomic<bool> stopRequested { false };
        DAW::RecordingWriterStateCore::publishWriteResult (
            false, rejectedTake, trackWriterFailed, stopRequested);
        expect (! rejectedTake.isTakeActive());
        expect (! rejectedTake.isInputAvailable());
        expect (trackWriterFailed.load (std::memory_order_acquire));
        expect (DAW::RecordingWriterStateCore::consumeStopRequest (stopRequested));
        expect (! DAW::RecordingWriterStateCore::consumeStopRequest (stopRequested),
                "writer rejection must request transport stop only once");

        beginTest ("offline barrier requires drained callbacks and published readiness");
        expect (DAW::OfflineRenderBarrierCore::callbacksDrained (0));
        expect (! DAW::OfflineRenderBarrierCore::callbacksDrained (1));
        expect (DAW::OfflineRenderBarrierCore::canRender (true, true));
        expect (! DAW::OfflineRenderBarrierCore::canRender (true, false));
        expect (! DAW::OfflineRenderBarrierCore::canRender (false, true));

        beginTest ("offline ownership suppresses the remaining realtime device path");
        expect (DAW::OfflineRenderBarrierCore::shouldProcessRealtimePath (false, false));
        expect (! DAW::OfflineRenderBarrierCore::shouldProcessRealtimePath (true, false));

        beginTest ("destructive project restore suppresses callback admission independently of export");
        expect (! DAW::OfflineRenderBarrierCore::shouldProcessRealtimePath (false, true));
        expect (! DAW::OfflineRenderBarrierCore::shouldProcessRealtimePath (true, true));

        beginTest ("deferred stop drains normally or reaches terminal failure without sleeps");
        using Lifecycle = DAW::RecordingLifecycleStateCore;
        Lifecycle drainedLifecycle;
        drainedLifecycle.beginDeferredStop();
        const auto pendingDrainSnapshot = drainedLifecycle.getSnapshot();
        expectEquals ((int) pendingDrainSnapshot.stopState,
                      (int) Lifecycle::StopState::pendingDrain);
        expect (! pendingDrainSnapshot.restartAllowed);
        expect (! drainedLifecycle.requestRestartAfterFinalization());
        expectEquals ((int) drainedLifecycle.pollDeferredStop (false, false),
                      (int) Lifecycle::StopPollAction::wait);
        expectEquals ((int) drainedLifecycle.pollDeferredStop (true, true),
                      (int) Lifecycle::StopPollAction::finalize);
        drainedLifecycle.markSafeCleanupComplete();
        expectEquals ((int) drainedLifecycle.getSnapshot().stopState,
                      (int) Lifecycle::StopState::ready);
        expect (drainedLifecycle.getSnapshot().restartAllowed);

        Lifecycle failedLifecycle;
        expect (failedLifecycle.requestRestartAfterFinalization());
        failedLifecycle.beginDeferredStop();
        expectEquals ((int) failedLifecycle.pollDeferredStop (false, true),
                      (int) Lifecycle::StopPollAction::terminalFailure);
        const auto failedSnapshot = failedLifecycle.getSnapshot();
        expectEquals ((int) failedSnapshot.stopState,
                      (int) Lifecycle::StopState::terminalFailure);
        expect (! failedSnapshot.pendingRestart);
        expect (! failedSnapshot.restartAllowed);
        expect (! failedLifecycle.requestRestartAfterFinalization());
        failedLifecycle.markSafeCleanupComplete();
        expectEquals ((int) failedLifecycle.getSnapshot().stopState,
                      (int) Lifecycle::StopState::terminalFailure);

        beginTest ("pending finalization restart is one-shot and cancellable");
        Lifecycle restartLifecycle;
        expect (restartLifecycle.requestRestartAfterFinalization());
        expect (restartLifecycle.getSnapshot().pendingRestart);
        expect (restartLifecycle.consumeRestartAfterFinalization (true));
        expect (! restartLifecycle.consumeRestartAfterFinalization (true),
                "a pending restart must be consumed exactly once");

        expect (restartLifecycle.requestRestartAfterFinalization());
        expect (! restartLifecycle.consumeRestartAfterFinalization (false),
                "transport leaving record must cancel the retry");
        expect (! restartLifecycle.getSnapshot().pendingRestart);

        expect (restartLifecycle.requestRestartAfterFinalization());
        restartLifecycle.cancelPendingRestart();
        expect (! restartLifecycle.consumeRestartAfterFinalization (true));

        expect (restartLifecycle.requestRestartAfterFinalization());
        restartLifecycle.beginShutdown();
        const auto shutdownSnapshot = restartLifecycle.getSnapshot();
        expect (shutdownSnapshot.shutdownStarted);
        expect (! shutdownSnapshot.pendingRestart);
        expect (! shutdownSnapshot.restartAllowed);
        expect (! restartLifecycle.consumeRestartAfterFinalization (true));

        beginTest ("export shutdown suppresses callbacks before requesting cancellation");
        DAW::ExportShutdownStateCore exportShutdown;
        expect (! exportShutdown.shouldPostCallbacks());
        expect (! exportShutdown.isCancellationRequested());

        exportShutdown.beginExport();
        expect (exportShutdown.shouldPostCallbacks());
        expect (! exportShutdown.isCancellationRequested());

        exportShutdown.prepareForShutdown();
        expect (! exportShutdown.shouldPostCallbacks());
        expect (exportShutdown.isCancellationRequested());

        exportShutdown.prepareForShutdown();
        expect (! exportShutdown.shouldPostCallbacks());
        expect (exportShutdown.isCancellationRequested());

        beginTest ("device stop before export claim rejects ownership");
        using ReleaseState = DAW::AudioResourceReleaseStateCore;
        ReleaseState stoppedBeforeClaim;
        expectEquals ((juce::int64) stoppedBeforeClaim.deviceStarted(), (juce::int64) 1);
        expectEquals ((int) stoppedBeforeClaim.deviceStopped(),
                      (int) ReleaseState::StopAction::release);
        expectEquals ((juce::int64) stoppedBeforeClaim.claimExport(), (juce::int64) 0);
        expect (! stoppedBeforeClaim.isExportOwned());

        beginTest ("export claim before device stop defers release atomically");
        ReleaseState deferredRelease;
        const auto claimedGeneration = deferredRelease.deviceStarted();
        expectEquals ((juce::int64) deferredRelease.claimExport(),
                      (juce::int64) claimedGeneration);
        expect (deferredRelease.isExportOwned());
        expectEquals ((int) deferredRelease.deviceStopped(),
                       (int) ReleaseState::StopAction::defer);
        expect (deferredRelease.isReleaseDeferred());

        beginTest ("same stopped generation releases exactly once after export completion");
        expectEquals ((int) deferredRelease.finishExport(),
                      (int) ReleaseState::FinishAction::release);
        expectEquals ((int) deferredRelease.finishExport(),
                      (int) ReleaseState::FinishAction::none);
        expect (! deferredRelease.isReleaseDeferred());

        beginTest ("device restart preserves export ownership and latest generation");
        ReleaseState restartedRelease;
        const auto stoppedGeneration = restartedRelease.deviceStarted();
        expectEquals ((juce::int64) restartedRelease.claimExport(),
                      (juce::int64) stoppedGeneration);
        expectEquals ((int) restartedRelease.deviceStopped(),
                       (int) ReleaseState::StopAction::defer);
        const auto replacementGeneration = restartedRelease.deviceStarted();
        expect (replacementGeneration > stoppedGeneration);
        expect (restartedRelease.isExportOwned());
        expectEquals ((juce::int64) restartedRelease.getGeneration(),
                      (juce::int64) replacementGeneration);
        expect (! restartedRelease.canApplyPreparation (stoppedGeneration));
        expect (! restartedRelease.canApplyPreparation (replacementGeneration));
        expectEquals ((int) restartedRelease.finishExport(),
                      (int) ReleaseState::FinishAction::none);
        expect (restartedRelease.isDeviceRunning());
        expect (! restartedRelease.isExportOwned());
        expect (! restartedRelease.canApplyPreparation (stoppedGeneration));
        expect (restartedRelease.canApplyPreparation (replacementGeneration));

        beginTest ("control shutdown consumes deferred release without duplication");
        ReleaseState controlRelease;
        controlRelease.deviceStarted();
        expect (controlRelease.claimExport() != 0);
        expectEquals ((int) controlRelease.deviceStopped(),
                       (int) ReleaseState::StopAction::defer);
        expectEquals ((int) controlRelease.finishExport(),
                      (int) ReleaseState::FinishAction::release);
        expectEquals ((int) controlRelease.deviceStopped(),
                       (int) ReleaseState::StopAction::none);

        beginTest ("durable async completion is consumed exactly once");
        DAW::AsyncCompletionStateCore<int> completion;
        int completionValue = 0;
        expect (! completion.consume (completionValue));
        expect (completion.publish (42));
        expect (completion.consume (completionValue));
        expectEquals (completionValue, 42);
        expect (! completion.consume (completionValue));

        beginTest ("suppressed async completion cannot be published or consumed");
        DAW::AsyncCompletionStateCore<int> suppressedCompletion;
        suppressedCompletion.suppress();
        expect (! suppressedCompletion.publish (7));
        expect (! suppressedCompletion.consume (completionValue));

        beginTest ("recording finalizer post failure cancels restart without sticking lifecycle");
        Lifecycle finalizerPostFailure;
        expect (finalizerPostFailure.requestRestartAfterFinalization());
        finalizerPostFailure.markFinalizationDispatchFailed();
        const auto finalizerFailureSnapshot = finalizerPostFailure.getSnapshot();
        expect (finalizerFailureSnapshot.finalizationDispatchFailed);
        expect (! finalizerFailureSnapshot.pendingRestart);
        expect (finalizerFailureSnapshot.restartAllowed);

        beginTest ("native export thread start failure releases running ownership");
        const auto startedThread = DAW::ExportThreadStartStateCore::resolve (true);
        expect (startedThread.running);
        expect (! startedThread.releaseOwnership);
        const auto failedThread = DAW::ExportThreadStartStateCore::resolve (false);
        expect (! failedThread.running);
        expect (failedThread.releaseOwnership);

        beginTest ("deferred preparation gate tracks only the latest generation");
        DAW::PendingAudioPreparationGateCore preparationGate;
        expect (! preparationGate.shouldGateCallback());
        preparationGate.defer (7);
        expect (preparationGate.shouldGateCallback());
        expect (preparationGate.isCurrent (7));
        preparationGate.defer (8);
        expect (! preparationGate.isCurrent (7));
        expect (preparationGate.isCurrent (8));
        expect (! preparationGate.complete (7));
        expect (preparationGate.shouldGateCallback());
        expect (preparationGate.complete (8));
        expect (! preparationGate.shouldGateCallback());

        beginTest ("deferred preparation gate cancels explicitly");
        preparationGate.defer (9);
        preparationGate.cancel();
        expect (! preparationGate.shouldGateCallback());
        expect (! preparationGate.isCurrent (9));

        beginTest ("callback gate rejects before prepared buffer access");
        using PreparationGate = DAW::PendingAudioPreparationGateCore;
        expectEquals ((int) preparationGate.callbackAction (false),
                      (int) PreparationGate::CallbackAction::clearOutputAndReturn);
        preparationGate.defer (10);
        expectEquals ((int) preparationGate.callbackAction (true),
                      (int) PreparationGate::CallbackAction::clearOutputAndReturn);
        preparationGate.cancel();
        expectEquals ((int) preparationGate.callbackAction (true),
                      (int) PreparationGate::CallbackAction::accessPreparedResources);

        beginTest ("pending preparation waits for admitted callbacks to drain");
        preparationGate.defer (11);
        expectEquals ((int) preparationGate.prepareAction (11, true, 1),
                      (int) PreparationGate::PrepareAction::waitForCallbacks);
        expectEquals ((int) preparationGate.prepareAction (11, true, 0),
                      (int) PreparationGate::PrepareAction::prepare);
        expectEquals ((int) preparationGate.prepareAction (10, false, 0),
                      (int) PreparationGate::PrepareAction::stale);
        expect (preparationGate.shouldGateCallback());
        preparationGate.cancel();

        beginTest ("export publication preserves the previous valid destination on failure");
        const auto publicationDirectory = juce::File::getSpecialLocation (
            juce::File::tempDirectory).getNonexistentChildFile (
                "apex_export_publication", {}, false);
        expect (publicationDirectory.createDirectory().wasOk());

        const auto destination = publicationDirectory.getChildFile ("mix.wav");
        const auto replacement = publicationDirectory.getChildFile ("mix.wav.tmp");
        expect (destination.replaceWithText ("previous"));
        expect (replacement.replaceWithText ("replacement"));
        expect (DAW::ExportPublicationCore::publish (replacement, destination));
        expectEquals (destination.loadFileAsString(), juce::String ("replacement"));

        expect (destination.replaceWithText ("previous"));
        expect (! DAW::ExportPublicationCore::publish (replacement, destination));
        expectEquals (destination.loadFileAsString(), juce::String ("previous"));
        expect (publicationDirectory.deleteRecursively());
    }
};

static LiveRecordWaveformRegressionTests liveRecordWaveformRegressionTests;

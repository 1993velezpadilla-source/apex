#pragma once
#include <JuceHeader.h>
#include "../TrackCore/Track.h"
#include "InputTrimCore.h"
#include "InputMeterCore.h"

namespace DAW {

/**
 * TrackInputProcessorCore
 *
 * Bridges the per-track InputTrimCore + InputMeterCore (owned by Track) to
 * the audio engine's per-block processing. Single concern: applying the trim
 * gain to the track's audio buffer and publishing the resulting post-trim,
 * pre-FX signal to the gain-staging meter. This signal includes whatever is
 * actually entering the channel (clips, incoming sends, and monitored hardware).
 * LiveInputMonitorEngine owns the exceptional armed-but-not-monitored hardware
 * input meter path, since that signal is intentionally absent from trackBuffer.
 *
 * Stateless. All state lives inside the Track's owned InputTrimCore and
 * InputMeterCore instances. Each Track has unique instances, so there is
 * no possibility of cross-track bleed by construction.
 *
 * --------------------------------------------------------------------------
 *  THREE INTEGRATION POINTS in AudioEngine
 * --------------------------------------------------------------------------
 *
 *  (1) Per-block — inside AudioEngine::processTrackNode():
 *      Insert AFTER trackBuffer_ has been populated by clip rendering and
 *      incoming send mixing, and BEFORE the preFxBuffer_ snapshot:
 *
 *          {
 *              auto* tbL = trackBuffer_.getWritePointer(0);
 *              auto* tbR = trackBuffer_.getWritePointer(
 *                  juce::jmin(1, trackBuffer_.getNumChannels() - 1));
 *              TrackInputProcessorCore::processTrack(*track, tbL, tbR, numSamples);
 *          }
 *
 *      A complete paste-over of processTrackNode() is provided as a
 *      companion file (AudioEngine_processTrackNode_patched.cpp).
 *
 *  (2) Engine prepare — inside AudioEngine::prepareToPlay() (or wherever the
 *      engine prepares per-track audio resources):
 *
 *          for (int i = 0; i < tracks_->getNumTracks(); ++i)
 *              if (auto* t = tracks_->getTrack(i))
 *                  TrackInputProcessorCore::prepareTrack(*t, sampleRate, samplesPerBlock);
 *
 *          if (auto* m = tracks_->getMasterTrack())
 *              TrackInputProcessorCore::prepareTrack(*m, sampleRate, samplesPerBlock);
 *
 *  (3) New tracks added mid-session — inside AudioEngine's TrackManager
 *      Listener implementation, in trackAdded():
 *
 *          void trackAdded(Track* track) override
 *          {
 *              ...existing per-track init...
 *              TrackInputProcessorCore::prepareTrack(*track, currentSampleRate_,
 *                                                    currentBlockSize_);
 *          }
 *
 * --------------------------------------------------------------------------
 *  WHY THIS LIVES AS A SEPARATE NUCLEO
 * --------------------------------------------------------------------------
 *
 *  - AudioEngine remains agnostic of InputTrimCore / InputMeterCore internals.
 *  - The "what to do for input processing" is single-sourced here, so adding
 *    InputFxChain to the pre-FX stage in the future is a one-file change.
 *  - The bug fix becomes visible as a single, named integration point in
 *    AudioEngine instead of two raw method calls scattered inline.
 */
class TrackInputProcessorCore
{
public:
    /** One-time per-block call. Apply trim and optionally publish this input stage. */
    static void processTrack(Track& track,
                             float* L,
                             float* R,
                             int    numSamples,
                             bool   publishMeter = true) noexcept
    {
        // Pre-FX input trim — multiplies samples by the smoothed gain set
        // through InputTrimCore::setTargetGainDb(). This is what makes the
        // panel knob actually affect audio.
        track.getInputTrim().applyToStereoBuffer(L, R, numSamples);

        if (publishMeter)
            track.getInputMeter().processBlock(L, R, numSamples);
    }

    /** Called from prepareToPlay() and trackAdded(). */
    static void prepareTrack(Track& track,
                             double sampleRate,
                             int    maxBlockSize) noexcept
    {
        track.getInputTrim().prepare(sampleRate, maxBlockSize);
        track.getInputMeter().prepare(sampleRate);
    }

    /** Called on releaseResources() or when a track is being destroyed. */
    static void resetTrack(Track& track) noexcept
    {
        track.getInputTrim().reset();
        track.getInputMeter().reset();
    }

private:
    TrackInputProcessorCore() = delete;
};

} // namespace DAW

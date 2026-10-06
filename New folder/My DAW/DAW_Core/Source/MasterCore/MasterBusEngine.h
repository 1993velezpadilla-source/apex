#pragma once
#include <JuceHeader.h>
#include "MasterTrackStateModel.h"
#include "MasterInsertChain.h"
#include "MasterMeterEngine.h"
#include "MasterAutomationEngine.h"
#include "RenderSourceEngine.h"
#include "MasterGainRampCore.h"
#include "MasterCeilingCore.h"
#include "MasterDitherCore.h"
#include "../MeteringCore/MeteringFacadeCore.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * MasterBusEngine — internal master bus signal processor.
 *
 * Signal flow (all on audio thread):
 *   Main Mix (from AudioEngine) → [Master Insert Chain] → [Master Fader/Gain]
 *     → [Master Meter tap] → [Render Source tap] → output
 *
 * This is the BACKEND processing engine. The visible Master Track in the mixer
 * is MasterTrackUI, which reads state from MasterTrackStateModel.
 *
 * Architectural rule:
 *   MasterBusEngine = backend signal path
 *   MasterTrackUI   = visible mixer channel strip
 */
class MasterBusEngine
{
public:
    void bindMasterTrack(Track* masterTrack) noexcept
    {
        masterTrack_ = masterTrack;
        if (masterTrack_)
            DBG("[MasterBusEngine] Master track bound: id=" + masterTrack_->getID()
                + " name=" + masterTrack_->getName());
    }

    void setPluginChain(PluginChainCore* pluginChain) noexcept
    {
        masterPluginChain_.store(pluginChain, std::memory_order_release);
        if (pluginChain != nullptr)
            DBG("[MasterBusEngine] Master FX chain bound");
    }

    void prepare(double sampleRate, int blockSize)
    {
        inserts_.prepare(sampleRate, blockSize);
        renderSource_.prepare(2, blockSize);
        gainRamp_.prepare(sampleRate, blockSize);
        ceiling_.prepare(sampleRate, blockSize);
        dither_.prepare(sampleRate, blockSize, 24);
        preMeter_.prepare(sampleRate, blockSize);
        postMeter_.prepare(sampleRate, blockSize);
    }

    void releaseResources()
    {
        inserts_.releaseResources();
        renderSource_.releaseResources();
    }

    /**
     * Processes the master bus in-place.
     * Called from audio thread after all tracks have been mixed.
     */
    void processBlock(float* L, float* R, int numSamples) noexcept
    {
        jassert(L != nullptr && R != nullptr && numSamples > 0);

        if (masterTrack_ != nullptr)
        {
            state_.masterGain.store(masterTrack_->getVolume(), std::memory_order_relaxed);
            state_.masterMute.store(masterTrack_->isMuted(), std::memory_order_relaxed);
        }

        // 1. Master insert chain (part of render path)
        inserts_.process(L, R, numSamples);

        // 2. Real master plugin chain (same visible chain used by the mixer strip)
        if (auto* chain = masterPluginChain_.load(std::memory_order_acquire))
        {
            float* channels[] = { L, R };
            juce::AudioBuffer<float> masterView(channels, 2, numSamples);
            chain->processBlock(masterView, numSamples);

            // Note: getNumSlots() must NOT be called on the audio thread (data race with slots_ vector).
                // Signal presence alone is sufficient to confirm the chain is active.
                if (!debugLoggedFxActivity_
                    && (masterView.getMagnitude(0, 0, numSamples) > 0.0f
                        || masterView.getMagnitude(1, 0, numSamples) > 0.0f))
                {
                    debugLoggedFxActivity_ = true;
                    DBG("[MasterBusEngine] Master FX chain processing active audio");
                }
        }

        // 3. Master pre-fader meter tap
        preMeter_.processBlock(L, R, numSamples);
        preMeter_.setPreFaderMetrics(preMeter_.getMetrics());

        // 4. Master fader gain + mute on the same final signal path
        float gain = state_.masterGain.load(std::memory_order_relaxed);
        bool  mute = state_.masterMute.load(std::memory_order_relaxed);

        // Future: multiply by automation gain
        // gain *= automation_.getAutomatedGain(position);

        if (mute)
        {
            juce::FloatVectorOperations::clear(L, numSamples);
            juce::FloatVectorOperations::clear(R, numSamples);

            if (!debugLoggedMuteZero_)
            {
                debugLoggedMuteZero_ = true;
                DBG("[MasterBusEngine] Master mute active = true, output zeroed");
            }
        }
        else
        {
            debugLoggedMuteZero_ = false;
            gainRamp_.setTargetGain(gain);
            gainRamp_.applyToStereoBuffer(L, R, numSamples);
        }

        // 5. Master post-fader meter tap, then ceiling and final dither.
        postMeter_.processBlock(L, R, numSamples);
        postMeter_.setPostFaderMetrics(postMeter_.getMetrics());
        ceiling_.process(L, R, numSamples);
        dither_.process(L, R, numSamples);

        // 6. Legacy master meter bridge (post-fader, post-mute, post-ceiling)
        meter_.pushBlock(L, R, numSamples);
        state_.peakL.store(meter_.getPeakL(), std::memory_order_relaxed);
        state_.peakR.store(meter_.getPeakR(), std::memory_order_relaxed);
        if (meter_.isClipped())
            state_.clipped.store(true, std::memory_order_relaxed);

        if (masterTrack_ != nullptr)
            masterTrack_->setPeakLevels(meter_.getPeakL(), meter_.getPeakR());

        // 7. Render source capture (BEFORE monitor section)
        renderSource_.captureBlock(L, R, numSamples);
    }

    // ── Accessors ────────────────────────────────────────────────────────
    MasterTrackStateModel&  getState()        noexcept { return state_; }
    MasterInsertChain&      getInserts()      noexcept { return inserts_; }
    MasterMeterEngine&      getMeter()        noexcept { return meter_; }
    MasterAutomationEngine& getAutomation()   noexcept { return automation_; }
    RenderSourceEngine&     getRenderSource() noexcept { return renderSource_; }
    MasterGainRampCore&     getGainRamp()     noexcept { return gainRamp_; }
    MasterCeilingCore&      getCeiling()      noexcept { return ceiling_; }
    MasterDitherCore&       getDither()       noexcept { return dither_; }
    MeteringFacadeCore&     getPreMeter()     noexcept { return preMeter_; }
    MeteringFacadeCore&     getPostMeter()    noexcept { return postMeter_; }

private:
    MasterTrackStateModel  state_;
    MasterInsertChain      inserts_;
    MasterMeterEngine      meter_;
    MasterAutomationEngine automation_;
    RenderSourceEngine     renderSource_;
    MasterGainRampCore     gainRamp_;
    MasterCeilingCore      ceiling_;
    MasterDitherCore       dither_;
    MeteringFacadeCore     preMeter_;
    MeteringFacadeCore     postMeter_;
    Track*                 masterTrack_ = nullptr;
    std::atomic<PluginChainCore*> masterPluginChain_ { nullptr };
    bool                   debugLoggedMuteZero_ = false;
    bool                   debugLoggedFxActivity_ = false;
};

} // namespace DAW

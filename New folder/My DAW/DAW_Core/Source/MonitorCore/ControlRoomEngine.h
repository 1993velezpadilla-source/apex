#pragma once
#include <JuceHeader.h>
#include "MonitorStateModel.h"
#include "MonitorEngine.h"
#include "SpeakerSetManager.h"
#include "../MeteringCore/PhaseWidthFacadeCore.h"

namespace DAW {

/**
 * ControlRoomEngine — top-level orchestrator for the monitor/control-room subsystem.
 *
 * Architectural position in the audio callback:
 *
 *   AudioEngine::process()            → fills hardware output buffer (main mix)
 *   [RENDER TAP]                      → this is where export/bounce reads from
 *   ControlRoomEngine::processBlock() → modifies hardware output IN-PLACE
 *                                        (monitor path only — does not affect render tap)
 *
 * Ownership:
 *   - MonitorStateModel   (shared with UI — atomics bridge audio↔UI)
 *   - MonitorEngine       (DSP chain: FX → DimMuteMono → trim → meter)
 *   - SpeakerSetManager   (speaker A/B/C configuration)
 *
 * Future extensions (architecture is ready):
 *   - CueEngine          (artist cue / headphone path)
 *   - ListenBusEngine    (AFL / PFL / SIP)
 *   - ReferenceSource    (reference player routing into monitor path)
 *   - MonitorOutputRouter (hardware output channel assignment per speaker set)
 */
class ControlRoomEngine
{
public:
    ControlRoomEngine() = default;

    void prepare(double sampleRate, int blockSize) noexcept
    {
        engine_.prepare(sampleRate, blockSize);
        phaseWidth_.prepare(sampleRate, blockSize);
    }

    void releaseResources() noexcept
    {
        engine_.releaseResources();
        phaseWidth_.releaseResources();
    }

    /**
     * Called from the audio thread AFTER AudioEngine::process() fills the buffer.
     * This is the ONLY entry point for monitor processing.
     * The buffer passed in is the hardware output; we modify it in-place.
     */
    void processBlock(const juce::AudioSourceChannelInfo& hw) noexcept
    {
        if (!hw.buffer || hw.buffer->getNumChannels() < 1 || hw.numSamples <= 0)
            return;

        const int ns = hw.numSamples;
        const int ch = hw.buffer->getNumChannels();
        float* L = hw.buffer->getWritePointer(0, hw.startSample);
        float* R = ch >= 2 ? hw.buffer->getWritePointer(1, hw.startSample) : L;

        // Phase/width metering + audible mono check.
        // CRITICAL: Runs BEFORE the regular monitor engine because:
        //   1. correlation/width are read on the original signal
        //   2. mono check (if enabled) writes (L+R)/2 into both channels,
        //      and that result is what the engineer should hear
        phaseWidth_.processBlock(L, R, ns);

        engine_.processMonitorPath(L, R, ns, state_, speakers_);
    }

    // ── Accessors (UI thread) ────────────────────────────────────────────
    MonitorStateModel&       getState()    noexcept { return state_; }
    const MonitorStateModel& getState()    const noexcept { return state_; }
    SpeakerSetManager&       getSpeakers() noexcept { return speakers_; }
    const SpeakerSetManager& getSpeakers() const noexcept { return speakers_; }
    PhaseWidthFacadeCore&       getPhaseWidth()       noexcept { return phaseWidth_; }
    const PhaseWidthFacadeCore& getPhaseWidth() const noexcept { return phaseWidth_; }

private:
    MonitorStateModel    state_;
    MonitorEngine        engine_;
    SpeakerSetManager    speakers_;
    PhaseWidthFacadeCore phaseWidth_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlRoomEngine)
};

} // namespace DAW

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "../ClipCore/Clip.h"
#include "../MidiCore/MidiClip.h"
#include "../AutomationCore/AutomationManagerCore.h"

namespace DAW::ProjectSampleRateReconcile {

/**
    Project sample-rate identity (2026-07-26, pre-beta project integrity).

    The project file persists the authoritative (device-granted) sample rate
    it was authored against. On reopen, the stored rate is compared with the
    currently granted device rate and a seconds-preserving reconcile runs
    ONLY when both rates are proven (Brain §3: the result of negotiation is
    authoritative; never invent a rate).

    - AutomationManagerCore's lanes and clip regions use sample-domain
      positions and must scale with the timeline. The separate
      AutomationLaneStoreCore serializes timePPQ and remains rate-independent.
    - AudioClip's source-file sample bounds (sourceStart/EndSample) are
      source-domain and are never scaled. Clip start/length/sourceOffset are
      engine-domain and are scaled.
    - MIDI clips reconcile through the tested MidiClip::setSampleRate path
      (seconds-preserving length rescale + rate update); tick content
      (piano-roll) is musical truth and is untouched.
*/

enum class Action
{
    none,               // rates match — no reconcile
    reconcile,          // both rates proven and different — seconds-preserving reconcile
    legacyUnverified,   // project has no (provable) rate metadata — do NOT reinterpret
    deviceRateUnknown   // project rate exists but device rate cannot be proven — do NOT fabricate
};

struct Plan
{
    Action action = Action::none;
    double projectRate = 0.0;
    double deviceRate = 0.0;
    double factor = 1.0;         // deviceRate / projectRate when action == reconcile
    bool metadataPresent = false;
};

/** Pure decision. Reconcile only from proven rates (Brain §3). */
inline Plan makePlan (double storedProjectRate, double deviceRate, bool metadataPresent) noexcept
{
    Plan p;
    p.projectRate = storedProjectRate;
    p.deviceRate = deviceRate;
    p.metadataPresent = metadataPresent;

    if (! metadataPresent || storedProjectRate <= 0.0)
    {
        p.action = Action::legacyUnverified;
        return p;
    }
    if (deviceRate <= 0.0)
    {
        p.action = Action::deviceRateUnknown;
        return p;
    }
    if (std::abs (deviceRate - storedProjectRate) < 0.01)
    {
        p.action = Action::none;
        return p;
    }
    p.action = Action::reconcile;
    p.factor = deviceRate / storedProjectRate;
    return p;
}

/** The caller's guard: reconcile ONLY for a proven mismatch. */
inline bool shouldReconcile (const Plan& p) noexcept
{
    return p.action == Action::reconcile;
}

/** Seconds-preserving scale for one sample-domain position. */
inline int64_t scalePosition (int64_t v, double factor) noexcept
{
    return (int64_t) std::llround ((double) v * factor);
}

/** Seconds-preserving reconcile of every clip's engine-domain timeline
    fields. Message thread only (project load / undo). */
inline void reconcileClips (ClipManager& clips, double factor, double deviceRate)
{
    if (factor <= 0.0 || deviceRate <= 0.0)
        return;

    for (auto* clip : clips.getAllClips())
    {
        if (clip == nullptr)
            continue;

        clip->setStartPosition ((SamplePosition) scalePosition ((int64_t) clip->getStartPosition(), factor));
        clip->setSourceOffset (juce::jmax ((SamplePosition) 0,
            (SamplePosition) scalePosition ((int64_t) clip->getSourceOffset(), factor)));

        if (auto* midiClip = dynamic_cast<MidiClip*> (clip))
        {
            // Tested seconds-preserving path: rescales length AND updates the
            // clip's rate used for tick<->sample conversion.
            midiClip->setSampleRate (deviceRate);
            if (midiClip->getLength() < (SamplePosition) 1)
                midiClip->setLength ((SamplePosition) 1);
        }
        else
        {
            clip->setLength (juce::jmax ((SamplePosition) 1,
                (SamplePosition) scalePosition ((int64_t) clip->getLength(), factor)));
        }
    }
}

/** Seconds-preserving reconcile for AutomationManagerCore's sample-domain
    lanes, clip-region bounds, and clip-local point positions. */
inline void reconcileAutomation (AutomationManagerCore& automation, double factor)
{
    if (! std::isfinite(factor) || factor <= 0.0)
        return;

    automation.scaleSamplePositions(factor);
}

} // namespace DAW::ProjectSampleRateReconcile

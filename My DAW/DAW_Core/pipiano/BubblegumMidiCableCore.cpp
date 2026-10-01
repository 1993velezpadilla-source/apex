#include "BubblegumMidiCableCore.h"

namespace xziel::midi
{

//==============================================================================
// CONSTRUCTION
//==============================================================================

BubblegumMidiCableCore::BubblegumMidiCableCore()
{
    lastTimerMs = juce::Time::currentTimeMillis();
    startTimerHz (60); // 60 fps pulse advancement
}

BubblegumMidiCableCore::~BubblegumMidiCableCore()
{
    stopTimer();
}

//==============================================================================
// SUBSCRIPTION
//==============================================================================

void BubblegumMidiCableCore::attachToModel (PianoRollClipModel& model)
{
    model.addPlaybackListener (this);
}

void BubblegumMidiCableCore::detachFromModel (PianoRollClipModel& model)
{
    model.removePlaybackListener (this);
}

void BubblegumMidiCableCore::registerRoute (PianoRollClipModel* model, RouteInfo route)
{
    if (model == nullptr) return;
    routes.set (model, route);
}

void BubblegumMidiCableCore::unregisterRoute (PianoRollClipModel* model)
{
    if (model == nullptr) return;
    routes.remove (model);
}

//==============================================================================
// CONFIG
//==============================================================================

void BubblegumMidiCableCore::setPitchClassColoringEnabled (bool b)
{
    if (pitchClassColoring == b) return;
    pitchClassColoring = b;
    notifyListeners();
}

void BubblegumMidiCableCore::setIntensityByVelocityEnabled (bool b)
{
    if (intensityByVelocity == b) return;
    intensityByVelocity = b;
    notifyListeners();
}

void BubblegumMidiCableCore::setSpeedMultiplier (float m)
{
    speedMultiplier = juce::jlimit (0.1f, 5.0f, m);
}

//==============================================================================
// COLOR
//==============================================================================

juce::Colour BubblegumMidiCableCore::getPitchClassColor (int pitchClass) const
{
    pitchClass = ((pitchClass % 12) + 12) % 12;
    return pitchClassPalette[pitchClass];
}

//==============================================================================
// PULSE STATE
//==============================================================================

std::vector<BubblegumMidiCableCore::Pulse>
BubblegumMidiCableCore::getActivePulses() const
{
    const juce::ScopedLock sl (pulseLock);
    return pulses; // value copy — caller gets stable view
}

//==============================================================================
// noteFired — THE BUBBLEGUM HOOK FIRES HERE
//==============================================================================

void BubblegumMidiCableCore::noteFired (const PianoRollClipModel::NoteEvent& note)
{
    // We need to figure out WHICH model called us so we can look up the route.
    // PlaybackListener::noteFired doesn't pass the model pointer, so for now
    // we spawn a pulse for ALL registered routes. A future refinement is to
    // extend the listener interface to include the source model.
    //
    // For Phase 1, the simple path: emit one pulse per registered route.
    // Most sessions have one clip → one route, so this is fine.

    Pulse p;
    p.id            = nextPulseId.fetch_add (1);
    p.pitch         = note.pitch;
    p.intensity     = intensityByVelocity ? (note.velocity / 127.0f) : 1.0f;
    p.progress      = 0.0f;
    p.speedPerSec   = 1.4f * speedMultiplier;
    p.lifetimeMs    = 800.0f;
    p.spawnedAtMs   = juce::Time::currentTimeMillis();

    {
        const juce::ScopedLock sl (pulseLock);

        for (auto it = routes.begin(); it.next();)
        {
            const auto& route = it.getValue();
            Pulse routedPulse  = p;
            routedPulse.cableId       = route.cableId;
            routedPulse.sourceTrackId = route.sourceTrackId;
            routedPulse.targetTrackId = route.targetTrackId;
            pulses.push_back (routedPulse);
        }

        // Cap pulse count for safety (heavy session protection)
        const size_t maxPulses = 512;
        if (pulses.size() > maxPulses)
            pulses.erase (pulses.begin(), pulses.begin() + (pulses.size() - maxPulses));
    }

    notifyListeners();
}

//==============================================================================
// TIMER  (advances pulse progress and removes expired ones)
//==============================================================================

void BubblegumMidiCableCore::timerCallback()
{
    const auto nowMs   = juce::Time::currentTimeMillis();
    const auto deltaMs = nowMs - lastTimerMs;
    lastTimerMs = nowMs;

    if (deltaMs <= 0) return;

    const float deltaSec = (float) deltaMs / 1000.0f;
    bool changed = false;

    {
        const juce::ScopedLock sl (pulseLock);

        for (auto it = pulses.begin(); it != pulses.end();)
        {
            auto& p = *it;

            // Advance progress along cable
            if (p.progress < 1.0f)
            {
                p.progress += deltaSec * p.speedPerSec;
                if (p.progress >= 1.0f)
                {
                    p.progress  = 1.0f;
                    p.hitTarget = true;
                }
                changed = true;
            }
            else
            {
                // Already at target, advance splash
                p.splashProgress += deltaSec * 2.5f;  // splash lasts ~400ms
                changed = true;
            }

            // Remove pulses past their full lifetime
            const auto ageMs = nowMs - p.spawnedAtMs;
            if (ageMs > (juce::int64) p.lifetimeMs)
            {
                it = pulses.erase (it);
                changed = true;
            }
            else
            {
                ++it;
            }
        }
    }

    if (changed)
        notifyListeners();
}

//==============================================================================
// LISTENERS
//==============================================================================

void BubblegumMidiCableCore::notifyListeners()
{
    listeners.call ([] (Listener& l) { l.pulseStateChanged(); });
}

} // namespace xziel::midi

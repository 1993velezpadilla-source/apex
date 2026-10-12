#include "../../Source/TuneLiveCore/ApexTuneLiveVoiceControl.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace APEX::TuneLive;
static PitchTracker::Result pitch(float midi) {
    const float hz = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
    return { hz, 0.98f, true };
}
int main() {
    VoiceControl tune;
    VoiceSettings natural{};
    natural.root=0; natural.scale=1; natural.correction=1.0f;
    natural.humanize=0.0f; natural.noteHysteresis=0.20f;
    const auto start=tune.update(pitch(60.1f),natural);
    assert(start.voiced && start.targetMidi==60);
    const auto nearBoundary=tune.update(pitch(61.08f),natural);
    assert(nearBoundary.targetMidi==60); // avoid repeated C/D note flapping
    const auto crossed=tune.update(pitch(61.40f),natural);
    assert(crossed.targetMidi==62);
    const auto backNear=tune.update(pitch(61.03f),natural);
    assert(backNear.targetMidi==62); // hysteresis on downward crossing
    const auto backFar=tune.update(pitch(60.60f),natural);
    assert(backFar.targetMidi==60);
    tune.reset();
    const auto robot=tune.update(pitch(60.25f),natural);
    natural.humanize=1.0f;
    tune.reset();
    const auto expressive=tune.update(pitch(60.25f),natural);
    assert(std::abs(expressive.shiftSemitones) < std::abs(robot.shiftSemitones));
    // Invalid F0 and unvoiced bursts must not drive an insane transpose.
    assert(!tune.update({0.0f,0.0f,false},natural).voiced);
    assert(!tune.update({NAN,0.99f,true},natural).voiced);
    tune.reset();
    const auto before=tune.update(pitch(60.15f),natural);
    assert(before.targetMidi==60);
    natural.root=1; // Db major: C is not legal, must move to Db
    const auto after=tune.update(pitch(60.15f),natural);
    assert(after.targetMidi==61);
    std::cout << "PASS: stable note hysteresis; expressive humanize; key switch; unvoiced/NaN gates\n";
}

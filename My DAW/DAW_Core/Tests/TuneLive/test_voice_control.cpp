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
    // Humanize is a sustained-note behaviour: establish the note centre,
    // then add a quick inflection. A one-frame note onset is fully corrected.
    tune.reset();
    for (int i=0; i<100; ++i) tune.update(pitch(60.0f),natural);
    const auto robot=tune.update(pitch(60.25f),natural);
    natural.humanize=1.0f;
    tune.reset();
    for (int i=0; i<100; ++i) tune.update(pitch(60.0f),natural);
    const auto expressive=tune.update(pitch(60.25f),natural);
    assert(std::abs(expressive.shiftSemitones) < std::abs(robot.shiftSemitones)*0.20f);
    // 5Hz human vibrato, +/-25 cents: a natural correction must preserve
    // at least 60% of the expressive variation, while robotic correction
    // should fully target the musical centre.
    auto vibratoRms = [](float humanize) {
        VoiceControl vocal;
        VoiceSettings p{};
        p.root=0; p.scale=1; p.correction=1.0f;
        p.humanize=humanize;
        p.hopMilliseconds=5.33333f;
        double squared=0.0;
        int n=0;
        for(int i=0;i<400;++i) {
            const double time=i*p.hopMilliseconds/1000.0;
            const float sung=60.0f+0.25f*std::sin(static_cast<float>(6.283185307179586*5.0*time));
            const auto d=vocal.update(pitch(sung),p);
            if(i>=100) {const double error=sung+d.shiftSemitones-60.0; squared+=error*error; ++n;}
        }
        return std::sqrt(squared/n);
    };
    const double naturalVibrato=vibratoRms(0.85f);
    const double roboticVibrato=vibratoRms(0.0f);
    const double originalVibrato=0.25/std::sqrt(2.0);
    assert(naturalVibrato > originalVibrato*0.60);
    assert(naturalVibrato < originalVibrato*1.20);
    assert(roboticVibrato < originalVibrato*0.10);

    // Flex Tune should preserve fine intonation at note centres while
    // leaving a large detuned note eligible for substantial correction.
    natural.humanize=0.0f;
    natural.flexTune=0.0f;
    tune.reset();
    const auto hardSmall=tune.update(pitch(60.06f), natural);
    natural.flexTune=1.0f;
    tune.reset();
    const auto flexSmall=tune.update(pitch(60.06f), natural);
    assert(std::abs(hardSmall.shiftSemitones)>0.04f);
    assert(std::abs(flexSmall.shiftSemitones)<0.002f);
    tune.reset();
    const auto flexLarge=tune.update(pitch(60.40f),natural);
    assert(std::abs(flexLarge.shiftSemitones)>0.19f);

    // Invalid F0 and unvoiced bursts must not drive an insane transpose.
    assert(!tune.update({0.0f,0.0f,false},natural).voiced);
    assert(!tune.update({NAN,0.99f,true},natural).voiced);
    tune.reset();
    const auto before=tune.update(pitch(60.15f),natural);
    assert(before.targetMidi==60);
    natural.root=1; // Db major: C is not legal, must move to Db
    const auto after=tune.update(pitch(60.15f),natural);
    assert(after.targetMidi==61);
    std::cout << "PASS: stable notes; 5Hz vibrato preservation; key switch; unvoiced/NaN gates\n";
}

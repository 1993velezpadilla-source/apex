#include "../../Source/TuneLiveCore/ApexTuneLivePitchTracker.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    // 12 deterministic F0 cases, at standard 44.1/48/96 kHz host rates.
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (const double hz : {110.0, 220.0, 440.0, 880.0}) {
            APEX::TuneLive::PitchTracker tracker;
            tracker.prepare(sr);
            for (int i = 0; i < sr * 0.35; ++i)
                tracker.push(0.40f * std::sin(6.283185307179586 * hz * i / sr));
            const auto result = tracker.latest();
            if (!result.voiced || std::abs(result.hz - hz) > hz * 0.015) {
                std::cerr << "FAIL: rate=" << sr << " f0=" << hz
                          << " measured=" << result.hz
                          << " voiced=" << result.voiced << "\n";
                return 1;
            }
        }
    }
    APEX::TuneLive::PitchTracker silence;
    silence.prepare(48000.0);
    for (int i = 0; i < 24000; ++i) silence.push(0.0f);
    assert(!silence.latest().voiced);
    assert(APEX::TuneLive::nearestScaleMidi(61.0f, 0, 1) == 60);
    assert(APEX::TuneLive::nearestScaleMidi(63.0f, 0, 2) == 63);
    assert(APEX::TuneLive::nearestScaleMidi(63.0f, 0, 1) == 62);
    std::cout << "PASS: F0 at 44.1/48/96k, silence gate, C major/minor quantization\n";
    return 0;
}

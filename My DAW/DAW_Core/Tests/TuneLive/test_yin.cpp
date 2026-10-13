#include "../../Source/TuneLiveCore/ApexTuneLivePitchTracker.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    // 15 deterministic F0 cases including low male C2, at 44.1/48/96 kHz.
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (const double hz : {65.41, 110.0, 220.0, 440.0, 880.0}) {
            APEX::TuneLive::PitchTracker tracker;
            tracker.prepare(sr);
            for (int i = 0; i < sr * 0.35; ++i)
                tracker.push(static_cast<float>(0.40 * std::sin(6.283185307179586 * hz * i / sr)));
            const auto result = tracker.latest();
            if (!result.voiced || std::abs(result.hz - hz) > hz * 0.015) {
                std::cerr << "FAIL: rate=" << sr << " f0=" << hz
                          << " measured=" << result.hz
                          << " voiced=" << result.voiced << "\n";
                return 1;
            }
        }
        // A voiced A4/YIN can stop at the first harmonic trough instead of
        // computing all ~220-256 lags for every analysis hop on the audio thread.
        APEX::TuneLive::PitchTracker fast;
        fast.prepare(sr);
        for (int i=0;i<static_cast<int>(sr*0.25);++i)
            fast.push(static_cast<float>(0.35*std::sin(6.283185307179586*440.0*i/sr)));
        if (!fast.latest().voiced || fast.lastLagEvaluations() >= 95) {
            std::cerr << "FAIL: early-trough CPU optimization ineffective at " << sr
                      << "Hz. lags=" << fast.lastLagEvaluations() << "\\n";
            return 1;
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

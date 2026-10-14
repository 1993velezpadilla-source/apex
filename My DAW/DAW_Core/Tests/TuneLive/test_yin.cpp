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
    // Startup latency regression: a soprano/upper vocal A4 should be
    // identified with only 28ms of input (no 43ms full-window wait).
    APEX::TuneLive::PitchTracker early;
    early.prepare(48000.0);
    for(int i=0;i<1344;++i)
        early.push(static_cast<float>(0.4*std::sin(6.283185307179586*440.0*i/48000.0)));
    if(!early.latest().voiced || std::abs(early.latest().hz-440.0f)>8.0f) {
        std::cerr<<"FAIL: high-voice warmup should detect by 28ms, observed "
                 <<early.latest().hz<<" Hz\\n";
        return 1;
    }
    // The early short-window never invents a bass note's octave. A low C2
    // must wait until the longer detector window contains enough cycles.
    APEX::TuneLive::PitchTracker low;
    low.prepare(48000.0);
    for(int i=0;i<1344;++i)
        low.push(static_cast<float>(0.4*std::sin(6.283185307179586*65.41*i/48000.0)));
    if(low.latest().voiced) {
        std::cerr<<"FAIL: short pitch detector guessed an octave for C2\\n";
        return 1;
    }
    for(int i=1344;i<16000;++i)
        low.push(static_cast<float>(0.4*std::sin(6.283185307179586*65.41*i/48000.0)));
    if(!low.latest().voiced || std::abs(low.latest().hz-65.41f)>3.0f) {
        std::cerr<<"FAIL: low-register long window lost C2 pitch\\n";
        return 1;
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

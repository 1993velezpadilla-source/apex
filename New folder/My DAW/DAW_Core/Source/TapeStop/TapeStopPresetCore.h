#pragma once
#include "TapeStopTypes.h"
#include "TapeStopAutomationCore.h"
#include <cmath>

namespace APEX {
namespace TapeStop {

// All preset insertion logic lives here as static functions. PresetCore has
// no state — it just translates a (PresetType, startSample, bpm) request
// into a sequence of addPoint() calls on an AutomationCore.

class PresetCore
{
public:
    // Insert the given preset starting at startSamples on the timeline.
    // The current project BPM is required to convert beat-based preset
    // durations into samples. The sample rate is read from the core.
    //
    // Behaviour:
    // 1. Compute the preset's total duration in samples from BPM.
    // 2. Remove any existing points in [startSamples, startSamples+duration).
    // 3. Add the preset's points to the core.
    // 4. Notify listeners (via the core's mutators, which call it for us).
    //
    // Returns the duration in samples that was inserted.
    static int64_t insertPreset(AutomationCore& core,
                                PresetType preset,
                                int64_t startSamples,
                                double bpm) noexcept
    {
        const double sr = core.getSampleRate();
        const double secondsPerBeat = (bpm > 1.0) ? (60.0 / bpm) : 0.5; // fallback 120 BPM

        auto beatsToSamples = [sr, secondsPerBeat](double beats) -> int64_t {
            return (int64_t) std::llround(beats * secondsPerBeat * sr);
        };

        switch (preset)
        {
            case PresetType::QuickStopQuarter: {
                const int64_t dur = beatsToSamples(0.25);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,       0.0f, CurveType::Exponential });
                core.addPoint({ startSamples + dur, 1.0f, CurveType::SCurve     });
                return dur;
            }
            case PresetType::TapeStopHalf: {
                const int64_t dur = beatsToSamples(0.5);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,       0.0f, CurveType::SCurve });
                core.addPoint({ startSamples + dur, 1.0f, CurveType::SCurve });
                return dur;
            }
            case PresetType::TapeStopOne: {
                const int64_t dur = beatsToSamples(1.0);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,       0.0f, CurveType::SCurve });
                core.addPoint({ startSamples + dur, 1.0f, CurveType::SCurve });
                return dur;
            }
            case PresetType::TapeStopTwo: {
                const int64_t dur = beatsToSamples(2.0);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,       0.0f, CurveType::SCurve });
                core.addPoint({ startSamples + dur, 1.0f, CurveType::SCurve });
                return dur;
            }
            case PresetType::VinylBrake: {
                const int64_t dur  = beatsToSamples(1.0);
                const int64_t mid  = beatsToSamples(0.5);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,        0.00f, CurveType::VinylBrake });
                core.addPoint({ startSamples + mid,  0.35f, CurveType::VinylBrake });
                core.addPoint({ startSamples + dur,  1.00f, CurveType::VinylBrake });
                return dur;
            }
            case PresetType::SlowDownReturn: {
                const int64_t dur  = beatsToSamples(2.0);
                const int64_t mid  = beatsToSamples(1.0);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,        0.0f, CurveType::SCurve });
                core.addPoint({ startSamples + mid,  1.0f, CurveType::SCurve });
                core.addPoint({ startSamples + dur,  0.0f, CurveType::SCurve });
                return dur;
            }
            case PresetType::HardStop: {
                const int64_t dur  = beatsToSamples(0.125);  // 1/8 beat
                const int64_t hold = beatsToSamples(0.0625); // brief hold at full stop
                core.removePointsInRange(startSamples, startSamples + dur + hold);
                core.addPoint({ startSamples,              0.0f, CurveType::Exponential });
                core.addPoint({ startSamples + dur,        1.0f, CurveType::Linear      });
                core.addPoint({ startSamples + dur + hold, 1.0f, CurveType::Linear      });
                return dur + hold;
            }
            case PresetType::VocalWordStop: {
                const int64_t dur = beatsToSamples(0.5);
                core.removePointsInRange(startSamples, startSamples + dur);
                core.addPoint({ startSamples,       0.0f, CurveType::VinylBrake });
                core.addPoint({ startSamples + dur, 1.0f, CurveType::VinylBrake });
                return dur;
            }
            case PresetType::BeatDropStop: {
                const int64_t dur  = beatsToSamples(1.0);
                const int64_t hold = beatsToSamples(0.25);
                core.removePointsInRange(startSamples, startSamples + dur + hold);
                core.addPoint({ startSamples,              0.0f, CurveType::VinylBrake });
                core.addPoint({ startSamples + dur,        1.0f, CurveType::Linear     });
                core.addPoint({ startSamples + dur + hold, 1.0f, CurveType::Linear     });
                return dur + hold;
            }
        }
        return 0;
    }

    // Returns the human-readable display name of a preset, for menu items.
    static const char* getDisplayName(PresetType p) noexcept
    {
        switch (p)
        {
            case PresetType::QuickStopQuarter: return "Quick Stop 1/4 Beat";
            case PresetType::TapeStopHalf:     return "Tape Stop 1/2 Beat";
            case PresetType::TapeStopOne:      return "Tape Stop 1 Beat";
            case PresetType::TapeStopTwo:      return "Tape Stop 2 Beats";
            case PresetType::VinylBrake:       return "Vinyl Brake";
            case PresetType::SlowDownReturn:   return "Slow Down + Return";
            case PresetType::HardStop:         return "Hard Stop";
            case PresetType::VocalWordStop:    return "Vocal Word Stop";
            case PresetType::BeatDropStop:     return "Beat Drop Stop";
        }
        return "Unknown";
    }
};

} // namespace TapeStop
} // namespace APEX

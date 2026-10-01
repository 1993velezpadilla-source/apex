#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4SmootherTests — permanent regression for the one-pole smoother's exact
// convergence contract.
//
// Historical bug: C4Smoother::advance() stagnated permanently below its
// target (measured 5.999828 dB instead of 6.0 dB) because the per-sample
// update `value += coeff * diff` fell below half an ULP of the current value
// — no further iteration could make representable progress, and the snap
// tolerance (6e-7) was never reached. The neutral gate (isSettledNeutral)
// therefore never became exactly true after a non-neutral excursion.
//
// Fix: explicit representable no-progress detection — if the prospective
// update is numerically identical to the current value, snap exactly to the
// target. The tolerance snap is preserved.
//
// This suite proves exact convergence for:
//   positive -> zero, negative -> zero, positive -> positive,
//   negative -> negative, tiny delta (the historical stagnation case),
//   large delta, exact arrival, repeated excursions,
//   every supported sample rate, and the engine-level neutral gate.
// ============================================================================

using APEX::C4::C4Smoother;
using APEX::C4::C4EngineCore;
using APEX::C4::C4BandId;
using APEX::C4::C4ParamIndex;
using APEX::C4::makeProfileVariantB;

class C4SmootherTests final : public juce::UnitTest
{
public:
    C4SmootherTests() : juce::UnitTest ("C4.Smoother", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

        beginTest ("Smoother exact convergence at every supported sample rate");
        for (double rate : rates)
        {
            // Positive -> zero.
            expect (convergesExactly (rate, 6.0f, 0.0f),
                    "positive->zero at " + juce::String (rate));
            // Negative -> zero.
            expect (convergesExactly (rate, -6.0f, 0.0f),
                    "negative->zero at " + juce::String (rate));
            // Positive -> positive.
            expect (convergesExactly (rate, 3.0f, 9.0f),
                    "positive->positive at " + juce::String (rate));
            // Negative -> negative.
            expect (convergesExactly (rate, -3.0f, -9.0f),
                    "negative->negative at " + juce::String (rate));
            // Large delta.
            expect (convergesExactly (rate, -15.0f, 15.0f),
                    "large delta at " + juce::String (rate));
        }

        beginTest ("Tiny delta (historical stagnation case): 0 -> 6 exactly");
        {
            // 0 -> 6 used to stall at 5.999828 forever. With the no-progress
            // snap the value must reach EXACTLY 6.0f within a bounded number
            // of samples (~16 time constants + 1 snap).
            C4Smoother s;
            s.prepare (48000.0, 15.0f);
            s.reset (0.0f);
            s.setTarget (6.0f);
            bool reached = false;
            for (int n = 0; n < 20000; ++n)
                if (s.advance() == 6.0f)
                {
                    reached = true;
                    break;
                }
            expect (reached, "0 -> 6 must reach exactly 6.0f (no stagnation)");
        }

        beginTest ("Exact arrival: target == current value stays put");
        {
            C4Smoother s;
            s.prepare (48000.0, 15.0f);
            s.reset (4.5f);
            s.setTarget (4.5f);
            for (int n = 0; n < 1000; ++n)
                expect (s.advance() == 4.5f, "exact arrival must stay exact");
        }

        beginTest ("Repeated excursions converge exactly every time");
        {
            C4Smoother s;
            s.prepare (48000.0, 15.0f);
            s.reset (0.0f);
            const float targets[] = { 6.0f, 0.0f, -6.0f, 0.0f, 12.0f, 0.0f, -12.0f, 0.0f };
            for (float t : targets)
            {
                s.setTarget (t);
                bool reached = false;
                for (int n = 0; n < 40000; ++n)
                    if (s.advance() == t)
                    {
                        reached = true;
                        break;
                    }
                expect (reached, "excursion to " + juce::String (t) + " must converge exactly");
                if (! reached)
                    break;
            }
        }

        beginTest ("Engine neutral gate becomes exactly true after non-neutral excursion");
        for (double rate : rates)
        {
            auto proc = C4Test::makePreparedProcessor (rate, 128);
            juce::AudioBuffer<float> buf (1, 128);
            juce::MidiBuffer midi;

            proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("12.0"));
            proc->getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kBiteGain)->getValueForText ("-9.0"));
            proc->processBlock (buf, midi);
            expect (! proc->getEngine().isSettledNeutral(),
                    "gate closed during excursion at " + juce::String (rate));

            proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kWeightGain)->getDefaultValue());
            proc->getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kBiteGain)->getDefaultValue());

            bool gateOpened = false;
            for (int b = 0; b < (int) (rate / 128) + 200; ++b)
            {
                proc->processBlock (buf, midi);
                if (proc->getEngine().isSettledNeutral())
                {
                    gateOpened = true;
                    break;
                }
            }
            expect (gateOpened,
                    "gate must re-open EXACTLY after convergence at " + juce::String (rate));
        }
    }

private:
    /** True when the smoother reaches the target EXACTLY (bit-equal) within
        a bounded number of samples. */
    static bool convergesExactly (double rate, float from, float to)
    {
        C4Smoother s;
        s.prepare (rate, 15.0f);
        s.reset (from);
        s.setTarget (to);
        const int maxSamples = (int) (rate * 1.0); // 1 s is far beyond ~16 tau
        for (int n = 0; n < maxSamples; ++n)
            if (s.advance() == to)
                return true;
        return false;
    }
};

static C4SmootherTests c4SmootherTests;

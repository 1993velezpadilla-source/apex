#include <JuceHeader.h>
#include "../../../Source/SoundEngineCore/ApexPdcDelayLineCore.h"
#include "../../../Source/ClickCore/ClickRoutingCore.h"

/**
    Regression tests for the Stage C low-buffer click/pop hardening:

    C7 — PDC delay lines crossfade effective-delay changes (~5 ms) instead
         of stepping the read tap. Covers PDC resyncs and monitoring-PDC
         Bypass/Reduced toggles.

    C9 — Click bursts carry truncated tails across block boundaries and the
         click bus can be faded with the engine's transport fade ramp, so a
         transport stop/start cannot hard-cut a click burst.
*/
class PdcClickRtTests final : public juce::UnitTest
{
public:
    PdcClickRtTests() : juce::UnitTest ("engine.pdc-click-rt.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("C7: delay change crossfades instead of stepping");
        {
            DAW::SoundEngine::ApexPdcDelayLineCore line;
            line.prepare (256);
            line.setCrossfadeSamples (16);

            // Push 64 ones at delay 8. Ring starts empty: the first 8 output
            // samples are legitimately silent (no history yet).
            std::vector<float> ones (64, 1.0f);
            std::vector<float> outL (64), outR (64);
            line.setDelaySamples (8);
            line.push (ones.data(), ones.data(), 64);
            line.read (outL.data(), outR.data(), 64);
            expectWithinAbsoluteError (outL[7], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[8], 1.0f, 1.0e-6f);

            // Push 64 twos, read at the SAME delay: output drains 8 ones
            // then crosses into the twos region.
            std::vector<float> twos (64, 2.0f);
            line.push (twos.data(), twos.data(), 64);
            line.read (outL.data(), outR.data(), 64);
            expectWithinAbsoluteError (outL[0], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[8], 2.0f, 1.0e-6f);

            // Change effective delay to 4 — crossfade must arm and blend the
            // two taps over 16 samples (mid-fade strictly between 1 and 2),
            // then settle on the new tap.
            line.read (outL.data(), outR.data(), 64, 4);
            expect (outL[5] > 1.0f && outL[5] < 2.0f);
            expectWithinAbsoluteError (outL[32], 2.0f, 1.0e-4f);
            expectWithinAbsoluteError (outL[63], 2.0f, 1.0e-4f);
        }

        beginTest ("C7: no fade on first use or when crossfade disabled");
        {
            DAW::SoundEngine::ApexPdcDelayLineCore line;
            line.prepare (128);
            std::vector<float> ones (32, 1.0f), outL (32), outR (32);
            line.setDelaySamples (8);
            line.push (ones.data(), ones.data(), 32);
            // No crossfade armed (fadeSamples == 0) and no history beyond the
            // 32 pushed samples: reading at delay 4 yields 4 silent samples
            // then the pushed ones — a clean step, no blending.
            line.read (outL.data(), outR.data(), 32, 4);
            expectWithinAbsoluteError (outL[0], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[3], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[4], 1.0f, 1.0e-6f);
        }

        beginTest ("C9: truncated click burst tail carries into the next block");
        {
            DAW::ClickRoutingCore routing;
            routing.prepare (64);
            std::vector<float> burst (32, 1.0f);

            routing.clear (64);
            routing.addClickAtOffset (48, burst.data(), 32, 1.0f);   // 16 fit, 16 tail

            std::vector<float> outL (64, 0.0f), outR (64, 0.0f);
            routing.sumIntoOutput (outL.data(), outR.data(), 64);
            expectWithinAbsoluteError (outL[48], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[63], 1.0f, 1.0e-6f);

            // Next block: clear + carry — the missing 16 samples land at 0..15.
            routing.clear (64);
            routing.carryTailIntoBlock();
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            routing.sumIntoOutput (outL.data(), outR.data(), 64);
            expectWithinAbsoluteError (outL[0], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[15], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[16], 0.0f, 1.0e-6f);   // tail fully consumed

            // Third block: nothing left to carry.
            routing.clear (64);
            routing.carryTailIntoBlock();
            expect (! routing.hasContent());
        }

        beginTest ("C9: gain ramp fades the click bus");
        {
            DAW::ClickRoutingCore routing;
            routing.prepare (64);
            std::vector<float> burst (16, 1.0f);
            routing.clear (64);
            routing.addClickAtOffset (0, burst.data(), 16, 1.0f);

            std::vector<float> ramp (64, 0.5f);
            std::vector<float> outL (64, 0.0f), outR (64, 0.0f);
            routing.sumIntoOutput (outL.data(), outR.data(), 64, ramp.data());
            expectWithinAbsoluteError (outL[0], 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (outL[15], 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (outR[8], 0.5f, 1.0e-6f);

            // Ramp to zero = transport stop: no click content in the output.
            std::fill (ramp.begin(), ramp.end(), 0.0f);
            std::fill (outL.begin(), outL.end(), 0.0f);
            routing.sumIntoOutput (outL.data(), outR.data(), 64, ramp.data());
            expectWithinAbsoluteError (outL[0], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (outL[15], 0.0f, 1.0e-6f);
        }
    }
};

static PdcClickRtTests pdcClickRtTests;

#include <JuceHeader.h>

// Production one-pole smoothing core under test (header-only).
#include "../../../Source/AutomationCore/AutomationSmootherCore.h"

//==============================================================================
/**
    @test    automation.smoother.block-invariance.v1
    @verify  DAW::AutomationSmootherCore::advance() is block-size invariant:

             - the smoothed value after N total samples must be identical
               regardless of host block segmentation
               (4 x 512 vs 2 x 1024 vs 1 x 2048);
             - the closed form matches the former iteration loop
               `s += (target - s) * coeff` repeated numSamples times;
             - no movement / active ramp / completed ramp behave correctly;
             - non-power-of-two block sizes are handled.

    The production call sites (PluginChainCore::applyAutomationAtSample and
    PluginInstanceCore::applyAutomationAtSample) previously iterated only
    jmin(numSamples, 1024) times, so host blocks above 1024 (e.g. 2048)
    applied a stale mid-ramp value every block.
*/
class AutomationSmootherCoreTests : public juce::UnitTest
{
public:
    AutomationSmootherCoreTests()
        : juce::UnitTest ("automation.smoother.block-invariance.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationSmootherCore;

        constexpr double kSampleRate = 44100.0;
        constexpr double kTau = 0.010;
        const float coeff = AutomationSmootherCore::makeCoeff(kSampleRate, kTau);
        constexpr float kLast = 0.25f;
        constexpr float kTarget = 0.9f;

        beginTest ("makeCoeff produces a valid one-pole coefficient");
        {
            expect (coeff > 0.0f && coeff < 1.0f, "coefficient in (0,1)");
            // tau=10ms at 44.1kHz -> roughly 1 - exp(-1/441) ~ 0.00226
            expect (std::abs(coeff - 0.00226f) < 0.0002f, "coefficient magnitude sane");
        }

        beginTest ("block segmentation invariance (4x512 vs 2x1024 vs 1x2048)");
        {
            // Advance 2048 total samples in three different segmentations.
            float s1 = kLast;
            for (int i = 0; i < 4; ++i)
                s1 = AutomationSmootherCore::advance(s1, kTarget, coeff, 512);

            float s2 = kLast;
            for (int i = 0; i < 2; ++i)
                s2 = AutomationSmootherCore::advance(s2, kTarget, coeff, 1024);

            const float s3 = AutomationSmootherCore::advance(kLast, kTarget, coeff, 2048);

            expect (std::abs(s1 - s3) < 1e-6f, "4x512 equals 1x2048");
            expect (std::abs(s2 - s3) < 1e-6f, "2x1024 equals 1x2048");
        }

        beginTest ("closed form equals iteration for many block sizes");
        {
            const int blockSizes[] = { 256, 480, 512, 1000, 1024, 1500, 2048, 3000 };
            for (int n : blockSizes)
            {
                // Iteration reference (the pre-fix loop, uncapped).
                float iterated = kLast;
                for (int si = 0; si < n; ++si)
                    iterated += (kTarget - iterated) * coeff;

                const float closed = AutomationSmootherCore::advance(kLast, kTarget, coeff, n);
                expect (std::abs(iterated - closed) < 1e-5f,
                        juce::String("closed form matches iteration at n=") + juce::String(n));
            }
        }

        beginTest ("no movement leaves value unchanged");
        {
            const float v = AutomationSmootherCore::advance(kTarget, kTarget, coeff, 2048);
            expectEquals (v, kTarget, "target == last -> unchanged");
        }

        beginTest ("completed ramp snaps to target");
        {
            // A very long ramp (100 seconds of audio) must land exactly on target.
            const float v = AutomationSmootherCore::advance(0.0f, 1.0f, coeff, (int)(100.0 * kSampleRate));
            expectEquals (v, 1.0f, "long ramp snaps to target");
        }

        beginTest ("active ramp advances monotonically toward target");
        {
            const float v1 = AutomationSmootherCore::advance(kLast, kTarget, coeff, 512);
            const float v2 = AutomationSmootherCore::advance(v1, kTarget, coeff, 512);
            expect (v1 > kLast && v1 < kTarget, "first advance between last and target");
            expect (v2 > v1 && v2 < kTarget, "second advance continues toward target");
        }

        beginTest ("non-positive numSamples returns target");
        {
            expectEquals (AutomationSmootherCore::advance(kLast, kTarget, coeff, 0), kTarget, "n=0 -> target");
            expectEquals (AutomationSmootherCore::advance(kLast, kTarget, coeff, -5), kTarget, "n<0 -> target");
        }
    }
};

static AutomationSmootherCoreTests automationSmootherCoreTests;

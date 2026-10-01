#include <JuceHeader.h>

#include "../../../Source/DynamicsCore/ApexDynamicsCore.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
using namespace APEX::Dynamics;

constexpr double kRate = 48000.0;
} // namespace

class ApexDynamicsCoreTests final : public juce::UnitTest
{
public:
    ApexDynamicsCoreTests()
        : UnitTest ("APEX.Dynamics", "APEX.Dynamics") {}

    void runTest() override
    {
        testStaticTransferCurve();
        testSoftKneeContinuity();
        testRangeClamp();
        testExtremeRatiosAndBoundaryContinuity();
        testDetectorPeakAndRms();
        testDetectorConvergenceAcrossRates();
        testEnvelopeConvergenceAndStagnationGuard();
        testEnvelopeTimingLawAcrossRates();
        testProcessorGainAndStereoLink();
        testSidechainReadyInputs();
        testBlockInvarianceAndResetDeterminism();
        testSilenceAndBurstRecovery();
        testHostileTransitionsAndExtremeInputs();
        testRealtimeAllocation();
        testParameterSanitisation();
        testDiagnosticCpuCharacterization();
    }

private:
    void testStaticTransferCurve()
    {
        beginTest ("static transfer curve implements the downward compression law");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.kneeDb = 0.0;

        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-40.0, p), 0.0, 1.0e-12,
            "below threshold: no reduction");
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-20.0, p), 0.0, 1.0e-12,
            "at threshold: no reduction");
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (0.0, p), 15.0, 1.0e-12,
            "20 dB over with ratio 4: 15 dB reduction");
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (12.0, p), 24.0, 1.0e-12,
            "32 dB over with ratio 4: 24 dB reduction");
    }

    void testSoftKneeContinuity()
    {
        beginTest ("soft knee is continuous in value at both knee edges");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.kneeDb = 6.0;

        const auto lowerEdge = GainComputer::gainReductionDb (-26.0, p);
        const auto upperEdge = GainComputer::gainReductionDb (-14.0, p);
        expectWithinAbsoluteError (lowerEdge, 0.0, 1.0e-12,
                                   "below knee start: zero");
        // 6 dB over threshold with ratio 4 -> 6 * (1 - 1/4) = 4.5 dB.
        expectWithinAbsoluteError (upperEdge, 4.5, 1.0e-12,
                                   "above knee end: 4.5 dB reduction (6 dB over, ratio 4)");

        double previous = lowerEdge;
        double maximumJump = 0.0;
        for (double level = -26.0; level <= -14.0; level += 0.25)
        {
            const auto value = GainComputer::gainReductionDb (level, p);
            maximumJump = std::max (maximumJump, std::abs (value - previous));
            previous = value;
            expect (value >= 0.0, "knee reduction non-negative");
        }
        expect (maximumJump < 0.5,
                "knee must not jump; max step " + juce::String (maximumJump, 6));

        // Hard knee is the limit of the soft knee as knee -> 0.
        DynamicsParameters hard = p;
        hard.kneeDb = 0.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-10.0, p),
            GainComputer::gainReductionDb (-10.0, hard), 1.0e-12);
    }

    void testRangeClamp()
    {
        beginTest ("maximum reduction range clamps the curve");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.rangeDb = 10.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (40.0, p), 10.0, 1.0e-12,
            "reduction clamped to range");
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-15.0, p), 3.75, 1.0e-12,
            "in-range reduction unaffected");
    }

    void testDetectorPeakAndRms()
    {
        beginTest ("peak and RMS detectors measure their declared quantities");
        Detector detector;
        detector.prepare (kRate);

        expectWithinAbsoluteError (detector.processMono (0.5, DetectorMode::Peak),
                                   Detector::linearToDb (0.5), 1.0e-12);
        expectWithinAbsoluteError (detector.processMono (-0.25, DetectorMode::Peak),
                                   Detector::linearToDb (0.25), 1.0e-12);

        // Steady sine of amplitude A settles to RMS level A / sqrt(2).
        Detector rms;
        rms.prepare (kRate);
        for (int sample = 0; sample < 48000; ++sample)
            rms.processMono (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi
                                             * 1000.0 * sample / kRate),
                             DetectorMode::Rms);
        expectWithinAbsoluteError (rms.processMono (0.0, DetectorMode::Rms),
                                   Detector::linearToDb (0.5 / std::sqrt (2.0)),
                                   0.1, "settled RMS level");

        // Linked stereo reports the louder channel.
        expectWithinAbsoluteError (
            detector.processStereo (0.9, 0.1, DetectorMode::Peak, true),
            Detector::linearToDb (0.9), 1.0e-12);
        expect (detector.processStereo (0.9, -0.9, DetectorMode::Peak, true)
                    == Detector::linearToDb (0.9),
                "linked stereo ignores the quieter channel");
    }

    void testEnvelopeConvergenceAndStagnationGuard()
    {
        beginTest ("attack/release converge exactly and never stall");
        Envelope envelope;
        envelope.prepare (kRate, 0.010, 0.100);

        const auto settled = envelope.process (6.0);
        expect (settled > 0.0 && settled <= 6.0, "attack advances toward target");
        for (int sample = 0; sample < 48000 * 2; ++sample)
            envelope.process (6.0);
        expectWithinAbsoluteError (envelope.process (6.0), 6.0, 1.0e-12,
                                   "attack reaches the target exactly");

        envelope.reset();
        envelope.process (12.0);
        for (int sample = 0; sample < 48000 * 2; ++sample)
            envelope.process (0.0);
        // 20 release time constants: residual <= 12 * e^-20 dB.
        expect (std::abs (envelope.process (0.0)) < 1.0e-6,
                "release decays below 1e-6 dB within 20 time constants");

        // Attack is faster than release for the same window, with the
        // envelope pre-charged to the settled target first.
        Envelope timings;
        timings.prepare (kRate, 0.010, 0.100);
        for (int sample = 0; sample < 48000; ++sample)
            timings.process (24.0); // settle attack exactly
        timings.reset();
        double attackProgress = 0.0;
        for (int sample = 0; sample < 480; ++sample)
            attackProgress = timings.process (24.0); // 10 ms attack
        const double releaseStart = timings.process (24.0);
        for (int sample = 0; sample < 480; ++sample)
            timings.process (0.0);
        const double releaseResidual = timings.process (0.0);
        expect (attackProgress > releaseStart - releaseResidual,
                "attack advances farther than release in the same window");
    }

    void testProcessorGainAndStereoLink()
    {
        beginTest ("processor produces unity gain below threshold and reduction above");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.kneeDb = 0.0;
        p.attackSeconds = 0.001;
        p.releaseSeconds = 0.050;
        p.detectorMode = DetectorMode::Peak;
        p.stereoLinked = true;

        DynamicsProcessor processor;
        processor.prepare (kRate, p);

        expectWithinAbsoluteError (processor.processMono (0.01), 1.0, 1.0e-12,
                                   "unity below threshold");
        for (int sample = 0; sample < 48000; ++sample)
            processor.processMono (1.0);
        const auto settledGain = processor.processMono (1.0);
        // 0 dBFS against -20 dB threshold, ratio 4 -> 15 dB reduction.
        expectWithinAbsoluteError (
            Detector::linearToDb (settledGain), -15.0, 0.05,
            "settled reduction for a full-scale signal");

        // Linked stereo gain equals the louder channel's mono gain.
        DynamicsProcessor linked;
        linked.prepare (kRate, p);
        for (int sample = 0; sample < 48000; ++sample)
            linked.processLinkedGain (1.0, 0.1);
        DynamicsProcessor mono;
        mono.prepare (kRate, p);
        for (int sample = 0; sample < 48000; ++sample)
            mono.processMono (1.0);
        expectWithinAbsoluteError (linked.processLinkedGain (1.0, 0.1),
                                   mono.processMono (1.0), 1.0e-9,
                                   "linked stereo matches the louder channel");
    }

    void testParameterSanitisation()
    {
        beginTest ("parameter sanitisation is total");
        DynamicsParameters hostile;
        hostile.thresholdDb = std::numeric_limits<double>::quiet_NaN();
        hostile.ratio = 0.5;
        hostile.kneeDb = -3.0;
        hostile.rangeDb = -9.0;
        hostile.attackSeconds = 0.0;
        hostile.releaseSeconds = std::numeric_limits<double>::infinity();
        hostile.makeupGainDb = std::numeric_limits<double>::quiet_NaN();

        const auto safe = DynamicsParameters::sanitised (hostile);
        expect (std::isfinite (safe.thresholdDb));
        expect (safe.ratio >= 1.0);
        expect (safe.kneeDb >= 0.0);
        expect (safe.rangeDb >= 0.0);
        expect (safe.attackSeconds >= 0.0005);
        expect (std::isfinite (safe.releaseSeconds));
        expect (std::isfinite (safe.makeupGainDb));

        DynamicsProcessor processor;
        processor.prepare (kRate, hostile);
        expect (std::isfinite (processor.processMono (1.0)),
                "hostile parameters cannot poison the processor");
    }

    void testExtremeRatiosAndBoundaryContinuity()
    {
        beginTest ("extreme ratios and unity boundary behave exactly");
        DynamicsParameters unity;
        unity.thresholdDb = -10.0;
        unity.ratio = 1.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (30.0, unity), 0.0, 1.0e-12,
            "ratio 1.0 is exactly unity everywhere");

        DynamicsParameters limiting;
        limiting.thresholdDb = -10.0;
        limiting.ratio = 100.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (30.0, limiting), 39.6, 1.0e-12,
            "ratio 100 approaches the limiter law (40 dB over -> 39.6 dB)");

        // Continuity exactly at the threshold boundary for hard and soft knee.
        DynamicsParameters hard;
        hard.thresholdDb = -18.0;
        hard.ratio = 2.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-18.0 + 1.0e-9, hard), 0.0, 1.0e-9,
            "hard knee continuous at threshold");
        DynamicsParameters soft = hard;
        soft.kneeDb = 3.0;
        expectWithinAbsoluteError (
            GainComputer::gainReductionDb (-18.0 - 3.0, soft), 0.0, 1.0e-12,
            "soft knee continuous at knee start");
    }

    void testDetectorConvergenceAcrossRates()
    {
        beginTest ("RMS detector converges at every supported sample rate");
        constexpr std::array<double, 6> rates {
            44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
        };
        for (const auto rate : rates)
        {
            Detector detector;
            detector.prepare (rate);
            const int samples = static_cast<int> (rate);
            for (int sample = 0; sample < samples; ++sample)
                detector.processMono (0.5 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0
                    * sample / rate), DetectorMode::Rms);
            expectWithinAbsoluteError (
                detector.processMono (0.0, DetectorMode::Rms),
                Detector::linearToDb (0.5 / std::sqrt (2.0)), 0.1,
                "settled RMS @ " + juce::String (rate, 0));
        }
    }

    void testEnvelopeTimingLawAcrossRates()
    {
        beginTest ("attack/release follow the one-pole timing law at six rates");
        constexpr std::array<double, 6> rates {
            44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
        };
        for (const auto rate : rates)
        {
            constexpr double attackSeconds = 0.010;
            constexpr double releaseSeconds = 0.100;
            Envelope envelope;
            envelope.prepare (rate, attackSeconds, releaseSeconds);
            const double attackPerSample = std::exp (-1.0 / (rate * attackSeconds));
            const double releasePerSample = std::exp (-1.0 / (rate * releaseSeconds));

            const auto first = envelope.process (24.0);
            const auto second = envelope.process (24.0);
            const double attackRatio = (24.0 - second) / (24.0 - first);
            expectWithinAbsoluteError (attackRatio, attackPerSample, 1.0e-9,
                                       "attack coefficient @ " + juce::String (rate, 0));

            for (int sample = 0; sample < 48000; ++sample)
                envelope.process (24.0);
            const auto releaseA = envelope.process (0.0);
            const auto releaseB = envelope.process (0.0);
            const double releaseRatio = releaseB / releaseA;
            expectWithinAbsoluteError (releaseRatio, releasePerSample, 1.0e-9,
                                       "release coefficient @ " + juce::String (rate, 0));
        }

        // Extreme fast and long timings remain total.
        Envelope fast;
        fast.prepare (48000.0, 0.0005, 20.0);
        expect (std::isfinite (fast.process (12.0)), "extreme timings stay finite");
    }

    void testSidechainReadyInputs()
    {
        beginTest ("detector key inputs decouple the sidechain from the main signal");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.detectorMode = DetectorMode::Peak;
        p.stereoLinked = true;

        DynamicsProcessor processor;
        processor.prepare (kRate, p);
        for (int sample = 0; sample < 48000; ++sample)
            processor.processMonoFromKey (0.01, 1.0); // loud key, quiet main
        const auto gain = processor.processMonoFromKey (0.01, 1.0);
        expectWithinAbsoluteError (Detector::linearToDb (gain), -15.0, 0.05,
                                   "key signal drives the gain computation");

        DynamicsProcessor linked;
        linked.prepare (kRate, p);
        for (int sample = 0; sample < 48000; ++sample)
            linked.processLinkedGainFromKey (0.0, 0.0, 1.0, 0.1);
        expectWithinAbsoluteError (
            Detector::linearToDb (linked.processLinkedGainFromKey (0.0, 0.0, 1.0, 0.1)),
            -15.0, 0.05, "linked stereo key matches the louder key channel");
    }

    void testBlockInvarianceAndResetDeterminism()
    {
        beginTest ("processing is block-size invariant and reset deterministic");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.detectorMode = DetectorMode::Rms;
        p.stereoLinked = true;

        auto render = [&] (int blockSize)
        {
            DynamicsProcessor processor;
            processor.prepare (kRate, p);
            std::vector<double> result;
            const int total = 96000;
            result.reserve (static_cast<std::size_t> (total));
            int position = 0;
            while (position < total)
            {
                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                    result.push_back (processor.processMono (0.5 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 500.0
                        * (position + sample) / kRate)));
                position += count;
            }
            return result;
        };

        const auto one = render (1);
        const auto thirtyOne = render (31);
        const auto fiveTwelve = render (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
        {
            expectEquals (one[sample], thirtyOne[sample],
                          "block invariance 1 vs 31 @ " + juce::String (
                              static_cast<int> (sample)));
            expectEquals (one[sample], fiveTwelve[sample],
                          "block invariance 1 vs 512 @ " + juce::String (
                              static_cast<int> (sample)));
        }

        auto first = render (512);
        auto second = render (512);
        for (std::size_t sample = 0; sample < first.size(); ++sample)
            expectEquals (first[sample], second[sample],
                          "reset/reprepare determinism @ " + juce::String (
                              static_cast<int> (sample)));
    }

    void testSilenceAndBurstRecovery()
    {
        beginTest ("silence and burst recovery stay finite and bounded");
        DynamicsParameters p;
        p.thresholdDb = -24.0;
        p.ratio = 6.0;
        p.detectorMode = DetectorMode::Rms;
        p.stereoLinked = true;

        DynamicsProcessor processor;
        processor.prepare (kRate, p);

        for (int sample = 0; sample < 48000; ++sample)
            expect (std::isfinite (processor.processMono (0.0)),
                    "silence stays finite");
        for (int sample = 0; sample < 4800; ++sample)
            expect (std::isfinite (processor.processMono (1.0)),
                    "full-scale burst stays finite");
        const auto duringBurst = processor.processMono (1.0);
        for (int sample = 0; sample < 48000 * 2; ++sample)
            processor.processMono (0.0);
        // 20 release time constants: the gain error bound is
        // reduction_max * e^-20 dB, far below the 1e-6 tolerance.
        expectWithinAbsoluteError (processor.processMono (0.0), 1.0, 1.0e-6,
                                   "gain returns to unity after silence");
        expect (duringBurst > 0.0 && duringBurst <= 1.0,
                "burst gain bounded below unity");
    }

    void testHostileTransitionsAndExtremeInputs()
    {
        beginTest ("hostile parameter transitions and extreme inputs are total");
        DynamicsProcessor processor;
        processor.prepare (48000.0, DynamicsParameters {});
        const double extremes[] = {
            1.0, -1.0, 1.0e30, -1.0e30, 1.0e-30, -1.0e-30,
            std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()
        };
        for (const auto extreme : extremes)
        {
            const auto gain = processor.processMono (extreme);
            expect (std::isfinite (gain) && gain >= 0.0,
                    "extreme input produces a finite non-negative gain");
        }

        for (int iteration = 0; iteration < 2000; ++iteration)
        {
            DynamicsParameters sweep;
            sweep.thresholdDb = 20.0 * std::sin (iteration * 0.01);
            sweep.ratio = 1.0 + 20.0 * (0.5 + 0.5 * std::sin (iteration * 0.007));
            sweep.kneeDb = 12.0 * (0.5 + 0.5 * std::cos (iteration * 0.003));
            sweep.attackSeconds = 0.001 + 0.05 * (0.5 + 0.5 * std::sin (iteration * 0.013));
            sweep.releaseSeconds = 0.01 + 0.5 * (0.5 + 0.5 * std::cos (iteration * 0.011));
            processor.setParameters (sweep);
            const auto gain = processor.processMono (0.7 * std::sin (iteration * 0.1));
            expect (std::isfinite (gain), "swept parameters stay finite");
        }
    }

    void testRealtimeAllocation()
    {
        beginTest ("processing allocates zero memory across detector modes");
        DynamicsParameters peak = DynamicsParameters {};
        peak.detectorMode = DetectorMode::Peak;
        DynamicsParameters rms = DynamicsParameters {};
        rms.detectorMode = DetectorMode::Rms;

        for (const auto* parameters : { &peak, &rms })
        {
            DynamicsProcessor processor;
            processor.prepare (192000.0, *parameters);
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int iteration = 0; iteration < 20000; ++iteration)
                {
                    processor.processMono (0.25);
                    processor.processLinkedGain (0.25, -0.5);
                    processor.processMonoFromKey (0.25, 0.5);
                }
            }
        }
    }

    void testDiagnosticCpuCharacterization()
    {
        beginTest ("diagnostic CPU characterization (no pass/fail gate)");
        DynamicsParameters p;
        p.thresholdDb = -20.0;
        p.ratio = 4.0;
        p.detectorMode = DetectorMode::Rms;
        DynamicsProcessor processor;
        processor.prepare (48000.0, p);

        constexpr int samples = 4 * 1000 * 1000;
        const auto start = std::clock();
        for (int sample = 0; sample < samples; ++sample)
            processor.processLinkedGain (0.3, -0.3);
        const auto elapsed = std::clock() - start;
        const double seconds = static_cast<double> (elapsed) / CLOCKS_PER_SEC;
        const double nanosecondsPerSample = seconds * 1.0e9 / samples;
        logMessage ("APEX.Dynamics diagnostic: " + juce::String (
            nanosecondsPerSample, 2) + " ns per stereo-linked sample ("
            + juce::String (samples / 1000000) + "M samples, "
            + juce::String (seconds, 3) + " s)");
        // Evidence only: no pass/fail gate. Wall-clock CPU measurement is
        // environment-dependent (build config, thermal, scheduler) and must
        // never become a flaky correctness assertion.
    }
};

static ApexDynamicsCoreTests apexDynamicsCoreTests;

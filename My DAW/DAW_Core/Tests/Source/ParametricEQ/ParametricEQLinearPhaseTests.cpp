#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQCore/ParametricEQLinearPhase.h"

#include <array>
#include <cmath>
#include <vector>

namespace
{

using namespace APEX::ParametricEQ;

constexpr std::array<double, 4> kRates { 44100.0, 48000.0, 96000.0, 192000.0 };

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void enableBell (Processor& processor, int band, float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
}

void processSilence (Processor& processor, int blocks, int blockSize = 512)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

// Measure the processor's output amplitude at a bin-exact frequency for a
// settled steady-state sine, in dB relative to the input amplitude. The sine
// advances by frequency/rate per sample at the ACTUAL sample rate so the
// excitation lands exactly on a 512-sample FFT bin at every rate.
double measuredLevelDb (Processor& processor, double rate, double frequency,
                        double amplitude, int blocks = 16,
                        int blockSize = 512)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    int position = 0;
    double lastSum = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<float> (amplitude * std::sin (
                2.0 * juce::MathConstants<double>::pi * frequency
                * (position + sample) / rate));
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }
        processor.processBlock (buffer, midi);
        position += blockSize;
        double sum = 0.0;
        for (int sample = 0; sample < blockSize; ++sample)
            sum += static_cast<double> (buffer.getSample (0, sample))
                 * buffer.getSample (0, sample);
        lastSum = sum;
    }
    return 20.0 * std::log10 (std::sqrt (lastSum / blockSize)
                              / (amplitude / std::sqrt (2.0)));
}

} // namespace

class ParametricEQLinearPhaseTests final : public juce::UnitTest
{
public:
    ParametricEQLinearPhaseTests()
        : UnitTest ("ParametricEQ.LinearPhase", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testKernelSymmetryAndCentre();
        testNeutralKernelIsPureDelay();
        testMagnitudeMatchesCanonicalResponse();
        testImpulsePeakAndReportedLatency();
        testGroupDelayAndPhaseSlope();
        testModeSwitchingIsBlockInvariantAndClickBounded();
        testDynamicEqAndAuditionLimitations();
        testLifecycleAndSampleRates();
        testRealtimeAllocation();
        testCpuCharacterizationEvidenceOnly();
    }

private:
    void testKernelSymmetryAndCentre()
    {
        beginTest ("the FIR is exactly symmetric around the expected centre");
        std::array<BandSettings, kMaxBands> settings {};
        settings[0] = ParametricEQTest::band (FilterShape::Bell, 1000.0, 6.0, 2.0);
        settings[1] = ParametricEQTest::band (FilterShape::HighShelf, 6000.0, -4.0, 0.8);
        settings[0].placement = ChannelPlacement::Mid;
        settings[1].placement = ChannelPlacement::Side;

        LinearPhaseKernels kernels;
        LinearPhaseKernelBuilder builder;
        builder.build (settings, DesignMode::Realtime, 48000.0, kernels);

        constexpr int centre = kLinearPhaseLatencySamples;
        for (int offset = 1; offset <= centre; ++offset)
        {
            const auto left = kernels.ll[static_cast<std::size_t> (centre - offset)];
            const auto right = kernels.ll[static_cast<std::size_t> (centre + offset)];
            expectWithinAbsoluteError (left, right, 1.0e-9,
                                       "LL symmetry at offset " + juce::String (offset));
            const auto leftRr = kernels.rr[static_cast<std::size_t> (centre - offset)];
            const auto rightRr = kernels.rr[static_cast<std::size_t> (centre + offset)];
            expectWithinAbsoluteError (leftRr, rightRr, 1.0e-9,
                                       "RR symmetry at offset " + juce::String (offset));
        }
        for (int offset = 0; offset < kLinearPhaseKernelLength; ++offset)
        {
            const auto lr = kernels.lr[static_cast<std::size_t> (offset)];
            const auto rl = kernels.rl[static_cast<std::size_t> (kLinearPhaseKernelLength
                                                                 - 1 - offset)];
            expectWithinAbsoluteError (lr, rl, 1.0e-9,
                                       "crossfeed mirror symmetry at tap " + juce::String (offset));
        }
        expect (std::abs (kernels.ll[static_cast<std::size_t> (centre)]) > 0.0,
                "the centre tap carries the main lobe");
    }

    void testNeutralKernelIsPureDelay()
    {
        beginTest ("a flat configuration produces the exact centred delta");
        std::array<BandSettings, kMaxBands> settings {};
        LinearPhaseKernels kernels;
        LinearPhaseKernelBuilder builder;
        builder.build (settings, DesignMode::Realtime, 48000.0, kernels);
        expect (kernels.isNeutral(), "neutral kernels are a pure delay");
        for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
        {
            const double expected = tap == kLinearPhaseLatencySamples ? 1.0 : 0.0;
            expectWithinAbsoluteError (
                kernels.ll[static_cast<std::size_t> (tap)], expected, 1.0e-9,
                "neutral tap " + juce::String (tap));
        }
    }

    void testMagnitudeMatchesCanonicalResponse()
    {
        beginTest ("linear-phase magnitude tracks the canonical 2x2 response");
        const double frequency = 937.5; // bin-exact for 512-sample blocks
        for (const auto rate : kRates)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (rate, 512);
            expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                    "linear phase settles @ " + juce::String (rate, 0));

            const auto measured = measuredLevelDb (processor, rate, frequency, 0.2);
            BandSettings reference;
            reference.enabled = true;
            reference.shape = FilterShape::Bell;
            reference.placement = ChannelPlacement::Stereo;
            reference.frequencyHz = 1000.0;
            reference.gainDb = 6.0;
            reference.q = 2.0;
            const auto target = 20.0 * std::log10 (std::abs (
                ResponseCore::bandResponse (reference, DesignMode::Realtime,
                                            rate, frequency)));
            // Tolerance from windowed frequency sampling: the Hann window
            // smooths the target between FFT bins; for Q=2 the band is far
            // smoother than the ~fs/4096 bin spacing, bounding the
            // interpolation error well below 0.1 dB.
            expectWithinAbsoluteError (measured, target, 0.1,
                                       "magnitude @ " + juce::String (rate, 0));
        }
    }

    void testImpulsePeakAndReportedLatency()
    {
        beginTest ("the impulse peak and reported latency are both 2047");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                "the kernel is published and linear phase settled before the impulse");
        expectEquals (processor.getLatencySamples(), kLinearPhaseLatencySamples);

        juce::AudioBuffer<float> impulse (2, 512);
        impulse.clear();
        juce::MidiBuffer midi;
        std::vector<float> captured;
        for (int block = 0; block < 8; ++block)
        {
            if (block == 0)
                impulse.setSample (0, 0, 1.0f);
            else
                impulse.clear();
            processor.processBlock (impulse, midi);
            for (int sample = 0; sample < 512; ++sample)
                captured.push_back (impulse.getSample (0, sample));
        }

        int peak = 0;
        float peakValue = 0.0f;
        for (std::size_t sample = 0; sample < captured.size(); ++sample)
            if (std::abs (captured[sample]) > peakValue)
            {
                peakValue = std::abs (captured[sample]);
                peak = static_cast<int> (sample);
            }
        expectEquals (peak, kLinearPhaseLatencySamples,
                      "impulse peak lands exactly at the latency sample");
        expect (peakValue > 0.05f, "the main lobe is present");
    }

    void testGroupDelayAndPhaseSlope()
    {
        beginTest ("group delay is constant and the phase slope is linear");
        std::array<BandSettings, kMaxBands> settings {};
        settings[0] = ParametricEQTest::band (FilterShape::Bell, 1000.0, 6.0, 2.0);
        LinearPhaseKernels kernels;
        LinearPhaseKernelBuilder builder;
        builder.build (settings, DesignMode::Realtime, 48000.0, kernels);

        constexpr double rate = 48000.0;
        auto kernelResponse = [&] (double frequencyHz)
        {
            std::complex<double> response {};
            for (int tap = 0; tap < kLinearPhaseKernelLength; ++tap)
            {
                const double phase = -2.0 * kPi * frequencyHz * tap / rate;
                response += kernels.ll[static_cast<std::size_t> (tap)]
                          * std::polar (1.0, phase);
            }
            return response;
        };

        // Every analysis point must align with the linear slope -2 pi f tau
        // (modulo 2 pi): this is the direct, wrap-safe linear-phase proof.
        constexpr double tau = static_cast<double> (kLinearPhaseLatencySamples);
        for (double frequency = 200.0; frequency <= 20000.0; frequency += 200.0)
        {
            const auto response = kernelResponse (frequency);
            if (std::abs (response) < 1.0e-6)
                continue;
            const double expectedPhase = std::arg (std::polar (
                1.0, -2.0 * kPi * frequency * tau / rate));
            const double phaseDifference = std::abs (
                std::arg (response * std::conj (std::polar (1.0, expectedPhase))));
            expect (phaseDifference < 0.01,
                    "phase follows the linear slope @ "
                        + juce::String (frequency, 0) + " Hz");
        }

        // Group delay between two widely separated analysis points,
        // unwrapped against the expected delay so the 2 pi ambiguity of the
        // raw phase difference (far larger than pi at these separations)
        // cannot corrupt the measurement.
        const double f1 = 1000.0;
        const double f2 = 1400.0;
        const double phase1 = std::arg (kernelResponse (f1));
        const double phase2 = std::arg (kernelResponse (f2));
        double delta = phase2 - phase1;
        const double expectedDelta
            = -2.0 * kPi * (f2 - f1) * tau / rate;
        delta += 2.0 * kPi
                 * std::round ((expectedDelta - delta) / (2.0 * kPi));
        const double deltaOmega = 2.0 * kPi * (f2 - f1) / rate;
        const double groupDelay = -delta / deltaOmega;
        expectWithinAbsoluteError (groupDelay, tau, 0.5,
                                   "group delay between 1.0 and 1.4 kHz");
    }

    void testModeSwitchingIsBlockInvariantAndClickBounded()
    {
        beginTest ("mode switching is click-bounded and deterministic");
        constexpr int total = 16384;
        auto render = [&] (int blockSize)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, blockSize);
            // Prime both renders identically: the kernel must be published
            // and the LP path fully settled BEFORE the measurement interval,
            // so the mid-stream toggle measures processor behavior, not
            // worker scheduling variance.
            expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                    "linear phase is settled before the invariance measurement");

            std::vector<float> result;
            result.reserve (static_cast<std::size_t> (total));
            juce::AudioBuffer<float> block (2, blockSize);
            block.clear();
            juce::MidiBuffer midi;
            int position = 0;
            bool toggled = false;
            while (position < total)
            {
                if (! toggled && position >= 8192)
                {
                    setUnits (processor, kPhaseModeParameter, 0.0f);
                    toggled = true;
                }
                const int count = std::min (blockSize, total - position);
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto value = static_cast<float> (0.3 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 937.5
                        * (position + sample) / 48000.0));
                    block.setSample (0, sample, value);
                    block.setSample (1, sample, value);
                }
                juce::AudioBuffer<float> view (block.getArrayOfWritePointers(),
                                               2, 0, count);
                processor.processBlock (view, midi);
                for (int sample = 0; sample < count; ++sample)
                    result.push_back (block.getSample (0, sample));
                position += count;
            }
            return result;
        };

        const auto one = render (1);
        const auto fiveTwelve = render (512);
        for (std::size_t sample = 0; sample < one.size(); ++sample)
            expectEquals (one[sample], fiveTwelve[sample],
                          "mode switch is block-size invariant @ "
                              + juce::String (static_cast<int> (sample)));
        double maximumJump = 0.0;
        for (std::size_t sample = 1; sample < one.size(); ++sample)
            maximumJump = std::max (maximumJump,
                                    std::abs (static_cast<double> (one[sample])
                                            - one[sample - 1]));
        expect (maximumJump < 0.3,
                "mode switch stays click-bounded; max "
                    + juce::String (maximumJump, 6));
    }

    void testDynamicEqAndAuditionLimitations()
    {
        beginTest ("linear phase suppresses dynamics and audition deterministically");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                "linear phase is settled before the limitation checks");
        expectWithinAbsoluteError (processor.getDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "dynamic gain publishes zero in linear phase");
        expect (! processor.beginAudition (0, 91),
                "audition is rejected in settled linear phase");

        setUnits (processor, kPhaseModeParameter, 0.0f);
        processSilence (processor, 8);
        expectEquals (processor.getLatencySamples(), 0,
                      "returning to minimum phase restores zero latency");
        expect (processor.beginAudition (0, 92),
                "audition returns with minimum phase");
        processor.endAudition (92);
    }

    void testLifecycleAndSampleRates()
    {
        beginTest ("reset/reprepare and sample rates keep linear phase valid");
        for (const auto rate : kRates)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (rate, 512);
            expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                    "linear phase settles @ " + juce::String (rate, 0));
            expectEquals (processor.getLatencySamples(), kLinearPhaseLatencySamples,
                          "latency @ " + juce::String (rate, 0));

            juce::AudioBuffer<float> buffer (2, 8193);
            ParametricEQTest::fillDeterministic (buffer, 0x1F4Cu);
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            expect (ParametricEQTest::allFinite (buffer),
                    "finite @ " + juce::String (rate, 0));

            processor.reset();
            processor.prepareToPlay (rate, 256);
            processSilence (processor, 64, 256);
            juce::AudioBuffer<float> rebound (2, 8193);
            ParametricEQTest::fillDeterministic (rebound, 0x1F4Cu);
            processor.processBlock (rebound, midi);
            expect (ParametricEQTest::allFinite (rebound),
                    "reprepare stays finite @ " + juce::String (rate, 0));
        }
    }

    void testRealtimeAllocation()
    {
        beginTest ("linear phase steady state and mode transitions allocate nothing");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                "linear phase is settled before the allocation sweep");

        juce::AudioBuffer<float> buffer (2, 128);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        {
            juce::UnitTestAllocationChecker checker (*this);
            for (int iteration = 0; iteration < 128; ++iteration)
            {
                if (iteration == 64)
                    setUnits (processor, kPhaseModeParameter, 0.0f);
                processor.processBlock (buffer, midi);
            }
        }
        expect (ParametricEQTest::allFinite (buffer));
    }

    void testCpuCharacterizationEvidenceOnly()
    {
        // Evidence-only: environment-dependent measurements, never pass/fail
        // gates. Logged via logMessage for the Phase 6 evidence record.
        beginTest ("CPU characterization (evidence only, no pass/fail)");
        constexpr int blockSize = 512;
        constexpr int iterations = 64;

        auto measure = [&] (Processor& processor, const char* label)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            ParametricEQTest::fillDeterministic (buffer, 0xC0FFEEu);
            juce::MidiBuffer midi;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int iteration = 0; iteration < iterations; ++iteration)
                processor.processBlock (buffer, midi);
            const auto elapsed = juce::Time::getHighResolutionTicks() - start;
            const double microsecondsPerBlock = elapsed * 1.0e6
                / juce::Time::getHighResolutionTicksPerSecond() / iterations;
            logMessage (juce::String (label) + ": "
                        + juce::String (microsecondsPerBlock, 1) + " us/block");
        };

        auto enableRepresentativeCurve = [] (Processor& processor, int bands)
        {
            for (int band = 0; band < bands; ++band)
                enableBell (processor, band,
                            static_cast<float> (100.0 * std::pow (2.0, band / 3.0)),
                            (band % 2 == 0) ? 6.0f : -4.0f,
                            1.0f + 0.3f * band);
        };

        for (const auto rate : kRates)
        {
            {
                auto processorStorage = std::make_unique<Processor>();
                auto& processor = *processorStorage;
                enableRepresentativeCurve (processor, 6);
                processor.prepareToPlay (rate, blockSize);
                processSilence (processor, 16, blockSize);
                measure (processor,
                         (juce::String ("CPU MP 6-band ") + juce::String (rate, 0)
                          + " Hz").toRawUTF8());
            }
            {
                auto processorStorage = std::make_unique<Processor>();
                auto& processor = *processorStorage;
                enableRepresentativeCurve (processor, 6);
                processor.prepareToPlay (rate, blockSize);
                expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                        "linear phase settles before CPU measurement");
                measure (processor,
                         (juce::String ("CPU LP 6-band ") + juce::String (rate, 0)
                          + " Hz").toRawUTF8());
            }
            {
                auto processorStorage = std::make_unique<Processor>();
                auto& processor = *processorStorage;
                enableRepresentativeCurve (processor, kMaxBands);
                processor.prepareToPlay (rate, blockSize);
                expect (ParametricEQTest::requestLinearPhaseAndSettle (processor),
                        "linear phase settles before 24-band CPU measurement");
                measure (processor,
                         (juce::String ("CPU LP 24-band ") + juce::String (rate, 0)
                          + " Hz").toRawUTF8());
            }
        }

        // Kernel generation cost: the full worker-side build (24 active
        // bands, both rates), averaged over several builds.
        for (const auto rate : { 48000.0, 96000.0 })
        {
            std::array<BandSettings, kMaxBands> settings {};
            for (int band = 0; band < kMaxBands; ++band)
            {
                settings[static_cast<std::size_t> (band)] = ParametricEQTest::band (
                    FilterShape::Bell, 100.0 * std::pow (2.0, band / 3.0),
                    (band % 2 == 0) ? 6.0 : -4.0, 1.0 + 0.3 * band);
            }
            LinearPhaseKernelBuilder builder;
            LinearPhaseKernels kernels;
            constexpr int builds = 8;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int build = 0; build < builds; ++build)
                builder.build (settings, DesignMode::Realtime, rate, kernels);
            const auto elapsed = juce::Time::getHighResolutionTicks() - start;
            const double microsecondsPerBuild = elapsed * 1.0e6
                / juce::Time::getHighResolutionTicksPerSecond() / builds;
            logMessage (juce::String ("kernel generation 24-band ")
                        + juce::String (rate, 0) + " Hz: "
                        + juce::String (microsecondsPerBuild, 1) + " us/build");
        }
    }
};

static ParametricEQLinearPhaseTests parametricEQLinearPhaseTests;

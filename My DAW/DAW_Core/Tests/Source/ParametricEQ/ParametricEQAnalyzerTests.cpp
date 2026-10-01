#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

#include <array>
#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;
using APEX::Analysis::SpectrumAnalyzerCore;
using APEX::Analysis::SpectrumTapMode;

constexpr std::array<double, 6> kRates {
    44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
};

void setUnitsForAnalyzerTest (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void fillBinSine (juce::AudioBuffer<float>& buffer, int& samplePosition,
                  double sampleRate, int fftBin, float leftGain = 1.0f,
                  float rightGain = 1.0f, float amplitude = 0.5f)
{
    const double frequency = sampleRate * fftBin / SpectrumAnalyzerCore::kFftSize;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = static_cast<float> (amplitude * std::sin (
            2.0 * juce::MathConstants<double>::pi * frequency
            * samplePosition++ / sampleRate));
        buffer.setSample (0, sample, value * leftGain);
        if (buffer.getNumChannels() > 1)
            buffer.setSample (1, sample, value * rightGain);
    }
}

void feedTone (Processor& processor, juce::AudioBuffer<float>& buffer,
               juce::MidiBuffer& midi, int& samplePosition,
               double sampleRate, int fftBin, float leftGain = 1.0f,
               float rightGain = 1.0f, int blocks = 12)
{
    for (int block = 0; block < blocks; ++block)
    {
        fillBinSine (buffer, samplePosition, sampleRate, fftBin,
                     leftGain, rightGain);
        processor.processBlock (buffer, midi);
    }
}

} // namespace

class ParametricEQAnalyzerTests final : public juce::UnitTest
{
public:
    ParametricEQAnalyzerTests()
        : UnitTest ("ParametricEQ.Analyzer", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testClosedIsDormant();
        testStereoPowerSemanticsAcrossRates();
        testBothMeasuresTransferAcrossRates();
        testObservationalAndRealtimeSafe();
        testPublicationAndModeEpochs();
        testSnapshotPinningAndFreshRecovery();
    }

private:
    void testClosedIsDormant()
    {
        beginTest ("closed default is dormant and performs no callback capture");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        expect (processor.getAnalyzerMode() == SpectrumTapMode::Closed);
        expect (! processor.getAnalyzer().isWorkerRunning());

        juce::AudioBuffer<float> buffer (2, 512);
        ParametricEQTest::fillDeterministic (buffer);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        expectEquals (processor.getAnalyzer().prePushCount(), 0);
        expectEquals (processor.getAnalyzer().postPushCount(), 0);
        expect (! processor.getAnalyzer().acquireSnapshot().isValid());
        expectEquals (processor.getAnalyzer().waitForSnapshot (-1, 20), -1);
    }

    float measurePreLevel (double sampleRate, float leftGain, float rightGain)
    {
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (sampleRate, 512);
        processor.setAnalyzerMode (SpectrumTapMode::Pre);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        int position = 0;
        feedTone (processor, buffer, midi, position, sampleRate, 85,
                  leftGain, rightGain, 16);

        const int waited = processor.getAnalyzer().waitForSnapshot (-1, 3000);
        expect (waited > 0, "worker must publish a spectrum");
        float level = -140.0f;
        {
            auto snapshot = processor.getAnalyzer().acquireSnapshot();
            expect (snapshot.isValid());
            if (snapshot.isValid())
            {
                expect (snapshot.index() >= waited,
                        "wait-visible sequence must have a published slot");
                level = snapshot.preDb()[85];
            }
        }
        processor.setAnalyzerMode (SpectrumTapMode::Closed);
        return level;
    }

    void testStereoPowerSemanticsAcrossRates()
    {
        beginTest ("stereo spectra use equal-power energy across six rates");
        for (const auto rate : kRates)
        {
            const auto inPhase = measurePreLevel (rate, 1.0f, 1.0f);
            const auto antiPhase = measurePreLevel (rate, 1.0f, -1.0f);
            const auto leftOnly = measurePreLevel (rate, 1.0f, 0.0f);
            const auto rightOnly = measurePreLevel (rate, 0.0f, 1.0f);
            const auto suffix = " @ " + juce::String (rate, 0) + " Hz";

            expectWithinAbsoluteError (inPhase, -6.0206f, 0.35f,
                                       "in-phase calibration" + suffix);
            expectWithinAbsoluteError (antiPhase, inPhase, 0.05f,
                                       "anti-phase must not cancel" + suffix);
            expectWithinAbsoluteError (leftOnly, rightOnly, 0.05f,
                                       "left/right symmetry" + suffix);
            expectWithinAbsoluteError (inPhase - leftOnly, 3.0103f, 0.08f,
                                       "single-channel power delta" + suffix);
        }
    }

    void testBothMeasuresTransferAcrossRates()
    {
        beginTest ("PRE/POST spectra measure the true +6 dB transfer at six rates");
        for (const auto rate : kRates)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            setUnitsForAnalyzerTest (processor, parameterIndex (
                0, BandParameterOffset::Enabled), 1.0f);
            setUnitsForAnalyzerTest (processor, parameterIndex (
                0, BandParameterOffset::Frequency), static_cast<float> (
                    rate * 85.0 / SpectrumAnalyzerCore::kFftSize));
            setUnitsForAnalyzerTest (processor, parameterIndex (
                0, BandParameterOffset::Gain), 6.0f);
            setUnitsForAnalyzerTest (processor, parameterIndex (
                0, BandParameterOffset::Q), 4.0f);
            processor.prepareToPlay (rate, 512);
            processor.setAnalyzerMode (SpectrumTapMode::Both);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            int position = 0;
            feedTone (processor, buffer, midi, position, rate, 85,
                      1.0f, 1.0f, 16);

            const int waited = processor.getAnalyzer().waitForSnapshot (-1, 3000);
            expect (waited > 0, "worker must publish @ "
                                     + juce::String (rate, 0));
            {
                auto snapshot = processor.getAnalyzer().acquireSnapshot();
                expect (snapshot.isValid());
                if (snapshot.isValid())
                {
                    expect (snapshot.preActive() && snapshot.postActive());
                    expectWithinAbsoluteError (snapshot.sampleRate(),
                                               static_cast<float> (rate), 0.01f);
                    expectWithinAbsoluteError (snapshot.preDb()[85],
                                               -6.0206f, 0.5f);
                    expectWithinAbsoluteError (snapshot.postDb()[85]
                                                   - snapshot.preDb()[85],
                                               6.0f, 0.75f);
                }
            }
            processor.setAnalyzerMode (SpectrumTapMode::Closed);
        }
    }

    void testObservationalAndRealtimeSafe()
    {
        beginTest ("all analyzer modes are observational and allocation-free");
        for (const auto rate : kRates)
        {
            for (const auto mode : { SpectrumTapMode::Pre,
                                     SpectrumTapMode::Post,
                                     SpectrumTapMode::Both })
            {
                auto closedStorage = std::make_unique<Processor>();
                auto openStorage = std::make_unique<Processor>();
                auto& closed = *closedStorage;
                auto& open = *openStorage;
                closed.prepareToPlay (rate, 128);
                open.prepareToPlay (rate, 128);
                open.setAnalyzerMode (mode);

                juce::AudioBuffer<float> a (2, 8193);
                juce::AudioBuffer<float> b (2, 8193);
                juce::MidiBuffer midi;
                ParametricEQTest::fillDeterministic (
                    a, 0x10203040u + static_cast<std::uint32_t> (rate));
                b.makeCopyOf (a);
                closed.processBlock (a, midi);
                {
                    juce::UnitTestAllocationChecker checker (*this);
                    open.processBlock (b, midi);
                }
                expect (ParametricEQTest::bitEqual (a, b),
                        "analyzer changed audio @ " + juce::String (rate, 0));
                open.setAnalyzerMode (SpectrumTapMode::Closed);
            }
        }
    }

    void testPublicationAndModeEpochs()
    {
        beginTest ("publication is correlated and every mode change starts a fresh epoch");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        processor.setAnalyzerMode (SpectrumTapMode::Pre);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        int position = 0;
        feedTone (processor, buffer, midi, position, 48000.0, 43,
                  1.0f, 1.0f, 12);
        const int first = processor.getAnalyzer().waitForSnapshot (-1, 3000);
        expect (first > 0);
        {
            auto snapshot = processor.getAnalyzer().acquireSnapshot();
            expect (snapshot.isValid() && snapshot.index() >= first);
            expect (snapshot.isValid() && snapshot.preActive()
                    && ! snapshot.postActive());
        }

        processor.setAnalyzerMode (SpectrumTapMode::Both);
        expect (! processor.getAnalyzer().acquireSnapshot().isValid(),
                "mode boundary must invalidate the old frame immediately");
        feedTone (processor, buffer, midi, position, 48000.0, 86,
                  1.0f, -1.0f, 12);
        const int second = processor.getAnalyzer().waitForSnapshot (first, 3000);
        expect (second > first);
        {
            auto snapshot = processor.getAnalyzer().acquireSnapshot();
            expect (snapshot.isValid() && snapshot.index() >= second);
            if (snapshot.isValid())
            {
                expect (snapshot.preActive() && snapshot.postActive());
                expect (snapshot.discontinuous(),
                        "first frame in a new mode epoch must mark discontinuity");
            }
        }

        processor.getAnalyzer().signalDiscontinuity();
        juce::Thread::sleep (60);
        expect (! processor.getAnalyzer().acquireSnapshot().isValid(),
                "explicit discontinuity must invalidate old analysis");
        feedTone (processor, buffer, midi, position, 48000.0, 120,
                  1.0f, 0.0f, 12);
        const int third = processor.getAnalyzer().waitForSnapshot (second, 3000);
        expect (third > second);
        {
            auto snapshot = processor.getAnalyzer().acquireSnapshot();
            expect (snapshot.isValid());
            if (snapshot.isValid())
                expect (snapshot.discontinuous());
        }

        processor.setAnalyzerMode (SpectrumTapMode::Closed);
        expect (! processor.getAnalyzer().isWorkerRunning());
        expect (! processor.getAnalyzer().acquireSnapshot().isValid());
        expectEquals (processor.getAnalyzer().waitForSnapshot (-1, 30), -1,
                      "closed analyzer must not report an old completion");
    }

    void testSnapshotPinningAndFreshRecovery()
    {
        beginTest ("pinned slots stay immutable while analysis advances and recovers fresh");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        processor.setAnalyzerMode (SpectrumTapMode::Pre);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        int position = 0;

        feedTone (processor, buffer, midi, position, 48000.0, 31,
                  1.0f, 1.0f, 10);
        expect (processor.getAnalyzer().waitForSnapshot (-1, 3000) > 0);
        auto first = processor.getAnalyzer().acquireSnapshot();
        expect (first.isValid());
        const float firstValue = first.isValid() ? first.preDb()[31] : 0.0f;

        const int firstIndex = first.isValid() ? first.index() : -1;
        feedTone (processor, buffer, midi, position, 48000.0, 47,
                  1.0f, 1.0f, 10);
        expect (processor.getAnalyzer().waitForSnapshot (firstIndex, 3000)
                > firstIndex);
        auto second = processor.getAnalyzer().acquireSnapshot();
        expect (second.isValid());

        const int secondIndex = second.isValid() ? second.index() : firstIndex;
        feedTone (processor, buffer, midi, position, 48000.0, 63,
                  1.0f, 1.0f, 10);
        expect (processor.getAnalyzer().waitForSnapshot (secondIndex, 3000)
                > secondIndex);
        auto third = processor.getAnalyzer().acquireSnapshot();
        expect (third.isValid());
        const int thirdIndex = third.isValid() ? third.index() : secondIndex;

        feedTone (processor, buffer, midi, position, 48000.0, 95,
                  1.0f, 1.0f, 12);
        juce::Thread::sleep (80);
        expect (processor.getAnalyzer().snapshotDropCount() > 0,
                "all pinned slots must drop publication, not stall analysis");
        if (first.isValid())
            expectEquals (first.preDb()[31], firstValue,
                          "worker overwrote a pinned immutable frame");

        first = {};
        feedTone (processor, buffer, midi, position, 48000.0, 127,
                  1.0f, -1.0f, 12);
        const int recovered = processor.getAnalyzer().waitForSnapshot (
            thirdIndex, 3000);
        expect (recovered > thirdIndex,
                "publication must resume after one slot is released");
        {
            auto latest = processor.getAnalyzer().acquireSnapshot();
            expect (latest.isValid() && latest.index() >= recovered);
            if (latest.isValid())
                expect (latest.preDb()[127] > latest.preDb()[31] + 20.0f,
                        "recovered frame must represent the newest tone");
        }

        processor.setAnalyzerMode (SpectrumTapMode::Closed);
    }
};

static ParametricEQAnalyzerTests parametricEQAnalyzerTests;

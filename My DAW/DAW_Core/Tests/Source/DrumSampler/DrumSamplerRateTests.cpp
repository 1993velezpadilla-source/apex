#include <JuceHeader.h>
#include "../../../Source/DrumSamplerCore/DrumSamplerEngine.h"
#include <cmath>

/**
    DrumSampler sample-rate correctness (2026-07-25 expansion).

    Before this fix, DrumSamplerVoice computed ADSR stage lengths at a
    hardcoded 44100 Hz and played every pad with sampleRateRatio = 1.0f,
    so pads played at the wrong pitch/speed whenever the file rate
    differed from the engine rate, and envelope times were wrong at any
    rate other than 44.1 kHz. These suites prove: envelope stage lengths
    are millisecond-invariant across engine rates, and pad pitch/duration
    are preserved across the full professional rate matrix (incl. 32-sample
    blocks).
*/
class DrumSamplerRateTests final : public juce::UnitTest
{
public:
    DrumSamplerRateTests() : juce::UnitTest ("drumsampler.rate.v1", "APEX.DrumSampler") {}

    static DAW::DrumPadConfig makeSinePad (double fileRate, double freqHz, int numSamples)
    {
        DAW::DrumPadConfig pad;
        pad.attackMs = 0.0f;   // envelope-neutral: instant attack, full sustain
        pad.decayMs = 0.0f;
        pad.sustainLevel = 1.0f;
        pad.releaseMs = 0.0f;
        pad.gainDb = 0.0f;

        DAW::VelocityLayer layer;
        layer.sampleRate = (int) fileRate;
        layer.numSamples = numSamples;
        layer.audioData.setSize (1, numSamples);
        for (int i = 0; i < numSamples; ++i)
            layer.audioData.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * i / fileRate));
        pad.layers.push_back (std::move (layer));
        return pad;
    }

    static DAW::DrumPadConfig makeDcPad (double fileRate, float value, int numSamples)
    {
        DAW::DrumPadConfig pad;
        pad.attackMs = 10.0f;
        pad.decayMs = 0.0f;
        pad.sustainLevel = 1.0f;
        pad.releaseMs = 0.0f;
        pad.gainDb = 0.0f;

        DAW::VelocityLayer layer;
        layer.sampleRate = (int) fileRate;
        layer.numSamples = numSamples;
        layer.audioData.setSize (1, numSamples);
        layer.audioData.clear();
        for (int i = 0; i < numSamples; ++i)
            layer.audioData.setSample (0, i, value);
        pad.layers.push_back (std::move (layer));
        return pad;
    }

    static double countZeroCrossingRate (const juce::AudioBuffer<float>& buffer, double engineRate)
    {
        int crossings = 0;
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                ++crossings;
        const double seconds = buffer.getNumSamples() / engineRate;
        return seconds > 0.0 ? (crossings / 2.0) / seconds : 0.0;
    }

    void runTest() override
    {
        beginTest ("pad pitch is preserved across engine rates (44.1k file)");
        {
            const double engineRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                auto pad = makeSinePad (44100.0, 1000.0, 44100); // 1 s of 1 kHz
                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                const int outSamples = (int) (engineRate * 0.5); // first 500 ms
                juce::AudioBuffer<float> out (2, outSamples);
                out.clear();
                voice.process (out, 0, outSamples);

                const double measured = countZeroCrossingRate (out, engineRate);
                expect (std::abs (measured - 1000.0) <= 30.0,
                        juce::String ("pitch at engine rate ") + juce::String (engineRate)
                            + ": measured " + juce::String (measured, 1) + " Hz");
            }
        }

        beginTest ("pad pitch is preserved when file rate exceeds engine rate");
        {
            auto pad = makeSinePad (96000.0, 1000.0, 96000);
            DAW::DrumSamplerVoice voice;
            voice.setSampleRate (48000.0);
            voice.start (pad, 1.0f, 0);

            juce::AudioBuffer<float> out (2, 24000);
            out.clear();
            voice.process (out, 0, 24000);

            expect (std::abs (countZeroCrossingRate (out, 48000.0) - 1000.0) <= 30.0);
        }

        beginTest ("pad duration in seconds is preserved across engine rates (32-sample blocks)");
        {
            const double engineRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                const int fileSamples = 22050; // 0.5 s at 44.1k
                auto pad = makeSinePad (44100.0, 440.0, fileSamples);
                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                juce::AudioBuffer<float> out (2, 32);
                int rendered = 0;
                while (voice.isActive() && rendered < (int) (engineRate * 2.0))
                {
                    out.clear();
                    voice.process (out, 0, 32);
                    rendered += 32;
                }

                const double expectedSamples = 0.5 * engineRate;
                expect (std::abs ((double) rendered - expectedSamples) <= 0.02 * engineRate,
                        juce::String ("duration at engine rate ") + juce::String (engineRate)
                            + ": rendered " + juce::String (rendered));
            }
        }

        beginTest ("ADSR stage lengths are millisecond-invariant across rates");
        {
            const double engineRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                auto pad = makeDcPad (44100.0, 1.0f, 44100); // DC: output == envelope value
                pad.attackMs = 10.0f;

                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                // Ramp must complete within 10 ms (+ one 32-sample block of slack).
                const int probe = (int) (0.010 * engineRate) + 32;
                juce::AudioBuffer<float> out (2, probe);
                out.clear();
                voice.process (out, 0, probe);
                expect (voice.isActive());

                expectWithinAbsoluteError (out.getSample (0, probe - 1), 1.0f, 0.05f);

                // And must NOT have settled far too early (< 90% at 5 ms).
                const int mid = (int) (0.005 * engineRate);
                expect (out.getSample (0, mid) < 0.9f,
                        juce::String ("attack prematurely complete at ") + juce::String (engineRate));
            }
        }

        beginTest ("engine prepare propagates the rate to voices");
        {
            DAW::DrumSamplerEngine engine (1);
            engine.getPad (0) = makeSinePad (44100.0, 1000.0, 22050);
            engine.getPad (0).midiNote = 36;
            engine.prepare (96000.0);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> out (2, 48000); // 0.5 s at 96k
            out.clear();
            engine.processBlock (midi, out, 0, 48000);

            expect (std::abs (countZeroCrossingRate (out, 96000.0) - 1000.0) <= 30.0);
        }

        beginTest ("default-constructed voice keeps legacy 44.1k behaviour (no prepare)");
        {
            auto pad = makeSinePad (44100.0, 1000.0, 22050);
            DAW::DrumSamplerVoice voice; // no setSampleRate: defaults to 44100
            voice.start (pad, 1.0f, 0);
            juce::AudioBuffer<float> out (2, 11025);
            out.clear();
            voice.process (out, 0, 11025);
            expect (std::abs (countZeroCrossingRate (out, 44100.0) - 1000.0) <= 30.0);
        }
    }
};

static DrumSamplerRateTests drumSamplerRateTests;

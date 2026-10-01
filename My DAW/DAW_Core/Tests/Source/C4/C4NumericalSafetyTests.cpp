#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4NumericalSafetyTests — every control combination must remain finite
// (spec §21). No NaN, no Inf, no denormal runaway, no unstable coefficients.
//
// Matrix: all six sample rates x extreme settings {min/max freq per band,
// min/max gain, min/max Q, all bands max boost, all bands max cut, adjacent
// overlap, HPF/LPF extremes, HALO-mode at max OPEN frequency (Phase 1 plain
// shelf), max BLOOM, max trims} x {noise, sine, silence} x long automation
// stress. Output must be finite AND bounded.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;

class C4NumericalSafetyTests final : public juce::UnitTest
{
public:
    C4NumericalSafetyTests() : juce::UnitTest ("C4.NumericalSafety", "APEX.C4") {}

    void runTest() override
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

        beginTest ("Extreme settings: finite and bounded at every sample rate");
        for (double rate : rates)
            for (int cfg = 0; cfg < 14; ++cfg)
                runExtremeConfig (rate, cfg);

        beginTest ("Silence and tiny signals stay clean (denormal protection)");
        for (double rate : rates)
            runDenormalCheck (rate);

        beginTest ("Automation stress: finite output (no runaway states)");
        for (double rate : { 44100.0, 192000.0 })
            runAutomationStress (rate);
    }

private:
    /** Apply one extreme configuration and process noise + sine. */
    void runExtremeConfig (double rate, int cfg)
    {
        const int block = 256;
        auto proc = C4Test::makePreparedProcessor (rate, block);

        auto setParam = [&proc] (C4ParamIndex idx, const juce::String& text)
        {
            proc->getC4Parameter (idx)->setValue (
                proc->getC4Parameter (idx)->getValueForText (text));
        };

        switch (cfg)
        {
        case 0: // all bands max boost at min freq
            setParam (C4ParamIndex::kWeightFreq, "30");  setParam (C4ParamIndex::kWeightGain, "15");
            setParam (C4ParamIndex::kSculptFreq, "120"); setParam (C4ParamIndex::kSculptGain, "15");
            setParam (C4ParamIndex::kBiteFreq, "400");   setParam (C4ParamIndex::kBiteGain, "15");
            setParam (C4ParamIndex::kOpenFreq, "1500");  setParam (C4ParamIndex::kOpenGain, "15");
            break;
        case 1: // all bands max cut at max freq
            setParam (C4ParamIndex::kWeightFreq, "450"); setParam (C4ParamIndex::kWeightGain, "-15");
            setParam (C4ParamIndex::kSculptFreq, "2500");setParam (C4ParamIndex::kSculptGain, "-15");
            setParam (C4ParamIndex::kBiteFreq, "9000");  setParam (C4ParamIndex::kBiteGain, "-15");
            setParam (C4ParamIndex::kOpenFreq, "20000"); setParam (C4ParamIndex::kOpenGain, "-15");
            break;
        case 2: // min Q everywhere, boosts
            setParam (C4ParamIndex::kSculptQ, "0.5"); setParam (C4ParamIndex::kBiteQ, "0.6");
            setParam (C4ParamIndex::kSculptGain, "15"); setParam (C4ParamIndex::kBiteGain, "15");
            break;
        case 3: // max Q everywhere, cuts
            setParam (C4ParamIndex::kSculptQ, "2.0"); setParam (C4ParamIndex::kBiteQ, "3.0");
            setParam (C4ParamIndex::kSculptGain, "-15"); setParam (C4ParamIndex::kBiteGain, "-15");
            break;
        case 4: // adjacent heavy overlap: same frequency pairs
            setParam (C4ParamIndex::kWeightFreq, "300"); setParam (C4ParamIndex::kWeightGain, "15");
            setParam (C4ParamIndex::kSculptFreq, "300"); setParam (C4ParamIndex::kSculptGain, "15");
            setParam (C4ParamIndex::kBiteFreq, "3000");  setParam (C4ParamIndex::kBiteGain, "15");
            setParam (C4ParamIndex::kOpenFreq, "3000");  setParam (C4ParamIndex::kOpenGain, "15");
            break;
        case 5: // HPF extreme 20 Hz + LPF extreme 24 kHz + all bands +15
            setParam (C4ParamIndex::kHpf, "20");
            setParam (C4ParamIndex::kLpf, "24000");
            setParam (C4ParamIndex::kWeightGain, "15"); setParam (C4ParamIndex::kSculptGain, "15");
            setParam (C4ParamIndex::kBiteGain, "15");   setParam (C4ParamIndex::kOpenGain, "15");
            break;
        case 6: // HPF extreme 1.5 kHz + LPF extreme 1.5 kHz (hardest squeeze)
            setParam (C4ParamIndex::kHpf, "1500");
            setParam (C4ParamIndex::kLpf, "1500");
            break;
        case 7: // HALO (shelf) mode with OPEN at max frequency
            setParam (C4ParamIndex::kOpenFreq, "20000");
            setParam (C4ParamIndex::kOpenMode, "1");
            setParam (C4ParamIndex::kOpenGain, "15");
            break;
        case 8: // max BLOOM
            setParam (C4ParamIndex::kBloom, "10");
            break;
        case 9: // max input AND output trim with boosts
            setParam (C4ParamIndex::kInput, "18"); setParam (C4ParamIndex::kOutput, "18");
            setParam (C4ParamIndex::kWeightGain, "15"); setParam (C4ParamIndex::kOpenGain, "15");
            break;
        case 10: // min trims with cuts
            setParam (C4ParamIndex::kInput, "-18"); setParam (C4ParamIndex::kOutput, "-18");
            setParam (C4ParamIndex::kSculptGain, "-15"); setParam (C4ParamIndex::kBiteGain, "-15");
            break;
        case 11: // WEIGHT shelf + OPEN shelf + everything else boosted
            setParam (C4ParamIndex::kWeightMode, "1"); setParam (C4ParamIndex::kOpenMode, "1");
            setParam (C4ParamIndex::kWeightGain, "15"); setParam (C4ParamIndex::kOpenGain, "15");
            setParam (C4ParamIndex::kSculptGain, "12"); setParam (C4ParamIndex::kBiteGain, "12");
            break;
        case 12: // auto gain on + all boosted
            setParam (C4ParamIndex::kAutoGain, "1");
            setParam (C4ParamIndex::kWeightGain, "15"); setParam (C4ParamIndex::kSculptGain, "15");
            setParam (C4ParamIndex::kBiteGain, "15");   setParam (C4ParamIndex::kOpenGain, "15");
            break;
        default: // bypass on with everything extreme (dry path)
            setParam (C4ParamIndex::kBypass, "1");
            setParam (C4ParamIndex::kWeightGain, "15"); setParam (C4ParamIndex::kSculptGain, "-15");
            setParam (C4ParamIndex::kBiteGain, "15");   setParam (C4ParamIndex::kOpenGain, "-15");
            setParam (C4ParamIndex::kHpf, "1000");      setParam (C4ParamIndex::kLpf, "8000");
            break;
        }

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;

        // Noise pass.
        C4Test::fillDeterministic (buf, 0xBEEF0000u + (juce::uint32) cfg);
        for (int b = 0; b < 200; ++b)
        {
            C4Test::fillDeterministic (buf, 0xBEEF0000u + (juce::uint32) (cfg * 1000 + b));
            proc->processBlock (buf, midi);
            if (! C4Test::allFinite (buf))
            {
                expect (false, "NaN/Inf in noise pass, cfg=" + juce::String (cfg)
                                   + " rate=" + juce::String (rate));
                return;
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (std::abs (buf.getSample (ch, i)) > 1.0e6f)
                    {
                        expect (false, "runaway sample, cfg=" + juce::String (cfg)
                                           + " rate=" + juce::String (rate));
                        return;
                    }
        }

        // Sine pass at a mid frequency.
        for (int b = 0; b < 200; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, 0.8f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1000.0
                        * (b * block + i) / rate));
            proc->processBlock (buf, midi);
            if (! C4Test::allFinite (buf))
            {
                expect (false, "NaN/Inf in sine pass, cfg=" + juce::String (cfg)
                                   + " rate=" + juce::String (rate));
                return;
            }
        }
    }

    void runDenormalCheck (double rate)
    {
        const int block = 256;
        auto proc = C4Test::makePreparedProcessor (rate, block);
        proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("12.0"));

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        bool finite = true;

        // Deep silence (filter states decay toward denormals).
        for (int b = 0; b < 500; ++b)
        {
            buf.clear();
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
        }
        // Tiny alternating signal.
        for (int b = 0; b < 200; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, (b % 2 == 0 ? 1.0e-25f : -1.0e-25f));
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
        }
        // Silence again, then a normal signal must still be clean.
        for (int b = 0; b < 100; ++b)
        {
            buf.clear();
            proc->processBlock (buf, midi);
            finite = finite && C4Test::allFinite (buf);
        }
        C4Test::fillDeterministic (buf, 0x12345678u);
        proc->processBlock (buf, midi);
        finite = finite && C4Test::allFinite (buf);

        expect (finite, "denormal handling at " + juce::String (rate) + " Hz");
    }

    void runAutomationStress (double rate)
    {
        const int block = 128;
        auto proc = C4Test::makePreparedProcessor (rate, block);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        C4Test::fillDeterministic (buf, 0xDEAD0000u);

        // Rapid alternating extremes: the full control surface moves every
        // block. Everything must stay finite and bounded.
        for (int b = 0; b < 4000; ++b)
        {
            auto* gain = proc->getC4Parameter (C4ParamIndex::kSculptGain);
            gain->setValue (b % 2 == 0 ? 1.0f : 0.0f);
            auto* q = proc->getC4Parameter (C4ParamIndex::kBiteQ);
            q->setValue (b % 3 == 0 ? 1.0f : 0.0f);
            auto* open = proc->getC4Parameter (C4ParamIndex::kOpenFreq);
            open->setValue (b % 4 == 0 ? 1.0f : 0.0f);
            auto* mode = proc->getC4Parameter (C4ParamIndex::kWeightMode);
            mode->setValue (b % 5 == 0 ? 1.0f : 0.0f);
            auto* hpf = proc->getC4Parameter (C4ParamIndex::kHpf);
            hpf->setValue (b % 7 == 0 ? hpf->getValueForText ("800") : 0.0f);
            auto* bypass = proc->getC4Parameter (C4ParamIndex::kBypass);
            bypass->setValue (b % 11 == 0 ? 1.0f : 0.0f);

            if (b % 2 == 0)
                C4Test::fillDeterministic (buf, 0xDEAD0000u + (juce::uint32) b);
            proc->processBlock (buf, midi);

            if (! C4Test::allFinite (buf))
            {
                expect (false, "NaN/Inf during automation stress at " + juce::String (rate));
                return;
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (std::abs (buf.getSample (ch, i)) > 1.0e6f)
                    {
                        expect (false, "runaway during automation stress at " + juce::String (rate));
                        return;
                    }
        }
    }
};

static C4NumericalSafetyTests c4NumericalSafetyTests;

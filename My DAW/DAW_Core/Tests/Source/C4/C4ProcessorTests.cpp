#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>

// ============================================================================
// C4ProcessorTests — host-facing contract (audit F):
//
//   - the authoritative 19-parameter table (ID, kind, range, default,
//     mapping, automation/serialization behavior)
//   - unique stable IDs, stable ordering (semantic index mapping)
//   - text round trips for every kind
//   - HPF/LPF OFF representation: unambiguous, round-trips, and never
//     collides with a real frequency
//   - state save/reload (tolerant restore: missing/garbage/non-finite)
//   - block-size independence
//   - mono == stereo-left determinism
//   - oversized-block chunking equals sequential legal blocks
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4Parameter;
using APEX::C4::C4ParamIndex;
using APEX::C4::kNumParams;

class C4ProcessorTests final : public juce::UnitTest
{
public:
    C4ProcessorTests() : juce::UnitTest ("C4.Processor", "APEX.C4") {}

    void runTest() override
    {
        testParameterTable();
        testTextRoundTrips();
        testHpfLpfOffRepresentation();
        testStateRoundTrip();
        testStateTolerance();
        testBlockSizeIndependence();
        testMonoStereo();
        testOversizedChunking();
    }

private:
    // ---- Audit F: the authoritative parameter table -----------------------

    struct ParamSpec
    {
        const char* id;
        const char* name;
        C4Parameter::Kind kind;
        float min;
        float max;
        float def;
    };

    void testParameterTable()
    {
        beginTest ("Authoritative 19-parameter table (IDs, kinds, ranges, defaults)");

        static const ParamSpec expected[kNumParams] =
        {
            { "c4.weight.freq", "WEIGHT Freq", C4Parameter::Kind::BandFreq, 30.0f,  450.0f, 100.0f   },
            { "c4.weight.gain", "WEIGHT Gain", C4Parameter::Kind::BandGain, -15.0f, 15.0f,  0.0f    },
            { "c4.weight.mode", "WEIGHT Mode", C4Parameter::Kind::Mode,     0.0f,   1.0f,   0.0f    },
            { "c4.sculpt.freq", "SCULPT Freq", C4Parameter::Kind::BandFreq, 120.0f, 2500.0f, 400.0f  },
            { "c4.sculpt.gain", "SCULPT Gain", C4Parameter::Kind::BandGain, -15.0f, 15.0f,  0.0f    },
            { "c4.sculpt.q",    "SCULPT Q",    C4Parameter::Kind::BandQ,    0.5f,   2.0f,   1.0f    },
            { "c4.bite.freq",   "BITE Freq",   C4Parameter::Kind::BandFreq, 400.0f, 9000.0f, 2500.0f },
            { "c4.bite.gain",   "BITE Gain",   C4Parameter::Kind::BandGain, -15.0f, 15.0f,  0.0f    },
            { "c4.bite.q",      "BITE Q",      C4Parameter::Kind::BandQ,    0.6f,   3.0f,   1.0f    },
            { "c4.open.freq",   "OPEN Freq",   C4Parameter::Kind::BandFreq, 1500.0f,20000.0f,10000.0f},
            { "c4.open.gain",   "OPEN Gain",   C4Parameter::Kind::BandGain, -15.0f, 15.0f,  0.0f    },
            { "c4.open.mode",   "OPEN Mode",   C4Parameter::Kind::Mode,     0.0f,   1.0f,   0.0f    },
            { "c4.hpf",         "HPF",         C4Parameter::Kind::Hpf,      0.0f,   1.0f,   0.0f    },
            { "c4.lpf",         "LPF",         C4Parameter::Kind::Lpf,      0.0f,   1.0f,   0.0f    },
            { "c4.bloom",       "BLOOM",       C4Parameter::Kind::Bloom,    0.0f,   10.0f,  4.0f    },
            { "c4.input",       "Input",       C4Parameter::Kind::Trim,     -18.0f, 18.0f,  0.0f    },
            { "c4.output",      "Output",      C4Parameter::Kind::Trim,     -18.0f, 18.0f,  0.0f    },
            { "c4.autogain",    "Auto Gain",   C4Parameter::Kind::Toggle,   0.0f,   1.0f,   0.0f    },
            { "c4.bypass",      "Bypass",      C4Parameter::Kind::Toggle,   0.0f,   1.0f,   0.0f    }
        };

        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        expect (proc->getParameters().size() == kNumParams,
                "exactly " + juce::String (kNumParams) + " hosted parameters");

        // Stable IDs are unique.
        juce::StringArray seen;
        for (auto* p : proc->getParameters())
        {
            auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
            expect (pwid != nullptr, "every C4 parameter must expose its stable ID");
            if (pwid != nullptr)
            {
                expect (! seen.contains (pwid->paramID),
                        "duplicate parameter ID: " + pwid->paramID);
                seen.add (pwid->paramID);
            }
        }

        // Every expected parameter exists with the right kind/range/default.
        for (int i = 0; i < kNumParams; ++i)
        {
            auto* p = proc->getC4Parameter (i);
            expect (p != nullptr, "parameter " + juce::String (i) + " exists");
            if (p == nullptr)
                continue;

            const ParamSpec& spec = expected[i];
            expect (p->paramID == spec.id, "semantic index " + juce::String (i)
                + " must be " + juce::String (spec.id) + ", found " + p->paramID);
            expect (p->name == juce::String (spec.name),
                    "name mismatch at " + juce::String (spec.id));
            expect (p->getKind() == spec.kind, "kind mismatch at " + juce::String (spec.id));

            const float normDef = p->getDefaultValue();
            const float unitDef = p->getUnitsValue();
            expect (std::abs (normDef - p->getValue()) < 1.0e-6f,
                    "fresh parameter must sit at its default: " + juce::String (spec.id));

            // Mapping law per kind (unit-space round trip from the default).
            switch (spec.kind)
            {
            case C4Parameter::Kind::BandFreq:
                expect (unitDef >= spec.min && unitDef <= spec.max,
                        "default freq in range: " + juce::String (spec.id));
                break;
            case C4Parameter::Kind::BandGain:
            case C4Parameter::Kind::Trim:
            case C4Parameter::Kind::Bloom:
                expect (std::abs (unitDef - spec.def) < 1.0e-3f,
                        "default unit value: " + juce::String (spec.id));
                break;
            case C4Parameter::Kind::BandQ:
                expect (unitDef >= spec.min && unitDef <= spec.max,
                        "default Q in range: " + juce::String (spec.id));
                break;
            default:
                break;
            }
        }

        // Semantic <-> hosted mapping is identity for C4 (all params hosted).
        for (int i = 0; i < kNumParams; ++i)
            expect (proc->getSemanticParameterIndex (i) == i,
                    "hosted order must equal semantic order (stable ordering)");

        // Bypass parameter is the processor bypass (no duplicate host bypass).
        expect (proc->getBypassParameter() == proc->getC4Parameter (C4ParamIndex::kBypass),
                "processor bypass must be c4.bypass");
    }

    void testTextRoundTrips()
    {
        beginTest ("Text round trips (getText <-> getValueForText) for every kind");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        // Band gains: "+6.0 dB" <-> 6 dB.
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kSculptGain);
            const float norm = p->getValueForText ("6.0");
            const float parsed = p->getText (norm, 1).getFloatValue();
            expect (std::abs (parsed - 6.0f) < 0.05f,
                    "gain text round trip: " + juce::String (parsed, 3));
        }

        // Q round trip (text is "Q x.xx" — parse after the prefix).
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kSculptQ);
            const float norm = p->getValueForText ("1.4");
            const juce::String text = p->getText (norm, 1);
            expect (text.startsWith ("Q "), "Q text format: " + text);
            const float parsed = text.substring (2).getFloatValue();
            expect (std::abs (parsed - 1.4f) < 0.01f,
                    "Q text round trip: " + juce::String (parsed, 3));
        }

        // Frequency round trip (unit-space).
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kBiteFreq);
            const float norm = p->getValueForText ("3000");
            const float hz = C4Parameter::freqHzFromNorm (norm, 400.0f, 9000.0f);
            expect (std::abs (hz - 3000.0f) < 0.5f,
                    "freq law round trip: " + juce::String (hz, 2));
        }

        // Bloom round trip.
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kBloom);
            const float norm = p->getValueForText ("7.5");
            const float parsed = p->getText (norm, 1).getFloatValue();
            expect (std::abs (parsed - 7.5f) < 0.05f,
                    "bloom text round trip: " + juce::String (parsed, 3));
        }

        // Trims round trip.
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kInput);
            const float norm = p->getValueForText ("-12.0");
            const float parsed = p->getText (norm, 1).getFloatValue();
            expect (std::abs (parsed + 12.0f) < 0.05f,
                    "trim text round trip: " + juce::String (parsed, 3));
        }

        // Mode text: Bell/Shelf/HALO.
        {
            auto* p = proc->getC4Parameter (C4ParamIndex::kWeightMode);
            expect (p->getText (p->getValueForText ("Shelf"), 1) == "Shelf",
                    "WEIGHT shelf text");
            auto* open = proc->getC4Parameter (C4ParamIndex::kOpenMode);
            expect (open->getText (open->getValueForText ("HALO"), 1) == "HALO",
                    "OPEN HALO text");
        }
    }

    void testHpfLpfOffRepresentation()
    {
        beginTest ("HPF/LPF OFF: unambiguous, round-trips, never collides with a frequency");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        // OFF is exactly norm 0; text is "Off"; round trip back to 0.
        auto* hpf = proc->getC4Parameter (C4ParamIndex::kHpf);
        expect (hpf->getValue() == 0.0f, "HPF default is exactly OFF");
        expect (hpf->getText (0.0f, 1) == "Off", "HPF OFF text");
        expect (hpf->getValueForText ("Off") == 0.0f, "HPF OFF parses back");
        expect (C4Parameter::hpfHzFromNorm (0.0f) == 0.0f,
                "HPF OFF law returns exactly 0 Hz (sentinel, never a real cutoff)");
        expect (C4Parameter::hpfNormFromHz (0.0f) == 0.0f,
                "HPF 0 Hz encodes to exactly OFF");

        // A real minimum cutoff (20 Hz) is strictly positive.
        const float minNorm = C4Parameter::hpfNormFromHz (20.0f);
        expect (minNorm > 0.0f && minNorm < 1.0f,
                "minimum HPF cutoff maps strictly inside (0,1)");

        auto* lpf = proc->getC4Parameter (C4ParamIndex::kLpf);
        expect (lpf->getValue() == 0.0f, "LPF default is exactly OFF");
        expect (lpf->getText (0.0f, 1) == "Off", "LPF OFF text");
        expect (C4Parameter::lpfHzFromNorm (0.0f) == 0.0f, "LPF OFF sentinel");

        // State round trip of OFF survives serialization.
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        auto restored = C4Test::makePreparedProcessor (48000.0, 128);
        restored->setStateInformation (state.getData(), (int) state.getSize());
        expect (restored->getC4Parameter (C4ParamIndex::kHpf)->getValue() == 0.0f,
                "HPF OFF survives state round trip");
        expect (restored->getC4Parameter (C4ParamIndex::kLpf)->getValue() == 0.0f,
                "LPF OFF survives state round trip");
    }

    void testStateRoundTrip()
    {
        beginTest ("State save/reload preserves every parameter");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        // Set a non-trivial configuration.
        proc->getC4Parameter (C4ParamIndex::kWeightFreq)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightFreq)->getValueForText ("250"));
        proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("7.5"));
        proc->getC4Parameter (C4ParamIndex::kWeightMode)->setValue (1.0f);
        proc->getC4Parameter (C4ParamIndex::kSculptQ)->setValue (
            proc->getC4Parameter (C4ParamIndex::kSculptQ)->getValueForText ("1.8"));
        proc->getC4Parameter (C4ParamIndex::kOpenMode)->setValue (1.0f);
        proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
            proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("90"));
        proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (
            proc->getC4Parameter (C4ParamIndex::kLpf)->getValueForText ("14000"));
        proc->getC4Parameter (C4ParamIndex::kBloom)->setValue (
            proc->getC4Parameter (C4ParamIndex::kBloom)->getValueForText ("4.0"));
        proc->getC4Parameter (C4ParamIndex::kInput)->setValue (
            proc->getC4Parameter (C4ParamIndex::kInput)->getValueForText ("-3.0"));
        proc->getC4Parameter (C4ParamIndex::kOutput)->setValue (
            proc->getC4Parameter (C4ParamIndex::kOutput)->getValueForText ("2.0"));

        juce::MemoryBlock state;
        proc->getStateInformation (state);
        expect (state.getSize() > 0, "state must serialize");

        auto restored = C4Test::makePreparedProcessor (48000.0, 128);
        restored->setStateInformation (state.getData(), (int) state.getSize());

        for (int i = 0; i < kNumParams; ++i)
        {
            const float a = proc->getC4Parameter (i)->getValue();
            const float b = restored->getC4Parameter (i)->getValue();
            expect (a == b,
                    "parameter " + juce::String (i) + " must survive state: "
                        + juce::String (a, 6) + " vs " + juce::String (b, 6));
        }
    }

    void testStateTolerance()
    {
        beginTest ("State restore tolerance: missing, garbage, non-finite, clamped");
        auto proc = C4Test::makePreparedProcessor (48000.0, 128);

        juce::ValueTree state ("c4state");
        state.setProperty ("version", 1, nullptr);
        // Unknown field (ignored).
        state.setProperty ("c4.unknown.thing", 0.5f, nullptr);
        // Missing fields keep defaults (only two fields present).
        state.setProperty ("c4.sculpt.gain", -999.0f, nullptr);   // clamps to -15
        state.setProperty ("c4.bloom", juce::var (std::numeric_limits<float>::quiet_NaN()), nullptr);

        juce::MemoryBlock block;
        juce::MemoryOutputStream stream (block, false);
        state.writeToStream (stream);

        // Non-finite value: keep default (Phase 5 production default 4.0 —
        // compared against the parameter's own default, never a literal).
        proc->setStateInformation (block.getData(), (int) block.getSize());
        const auto* bloom = proc->getC4Parameter (C4ParamIndex::kBloom);
        expect (std::abs (bloom->getValue() - bloom->getDefaultValue()) < 1.0e-6f,
                "non-finite state value must keep the default");
        // Out-of-range clamps: -999 dB -> -15 dB -> norm 0.
        expect (proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValue() == 0.0f,
                "clamped-to-min gain is norm 0");
        // Missing fields keep defaults (0 dB gain == normalized 0.5).
        expect (std::abs (proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValue() - 0.5f) < 1.0e-6f,
                "missing fields keep defaults");

        // Null/empty state must not crash.
        proc->setStateInformation (nullptr, 0);
        proc->setStateInformation (block.getData(), 0);
    }

    void testBlockSizeIndependence()
    {
        beginTest ("Block-size independence: identical output for 32/128/512/1024");
        const double rate = 48000.0;

        std::vector<float> signal (rate); // 1 s
        for (int i = 0; i < (int) signal.size(); ++i)
            signal[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / rate)
                               + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1234.0 * i / rate);

        auto runWithBlocks = [&] (int block) -> std::vector<float>
        {
            auto proc = C4Test::makePreparedProcessor (rate, block);
            proc->getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("6.0"));
            proc->getC4Parameter (C4ParamIndex::kSculptFreq)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptFreq)->getValueForText ("1000"));
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("-4.0"));
            proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("60"));

            std::vector<float> out (signal.size(), 0.0f);
            juce::AudioBuffer<float> buf (1, block);
            juce::MidiBuffer midi;
            size_t pos = 0;
            while (pos < signal.size())
            {
                const int n = (int) juce::jmin ((size_t) block, signal.size() - pos);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, signal[pos + (size_t) i]);
                proc->processBlock (buf, midi);
                for (int i = 0; i < n; ++i)
                    out[pos + (size_t) i] = buf.getSample (0, i);
                pos += (size_t) n;
            }
            return out;
        };

        const auto out32 = runWithBlocks (32);
        const auto out128 = runWithBlocks (128);
        const auto out512 = runWithBlocks (512);
        const auto out1024 = runWithBlocks (1024);

        bool identical = out32 == out128 && out128 == out512 && out512 == out1024;
        expect (identical, "output must be identical across block sizes");
        if (! identical)
        {
            size_t firstDiff = 0;
            while (firstDiff < out32.size() && out32[firstDiff] == out128[firstDiff])
                ++firstDiff;
            expect (false, "first difference at sample " + juce::String ((int) firstDiff));
        }
    }

    void testMonoStereo()
    {
        beginTest ("Mono == stereo-left (deterministic per-sample control state)");
        const double rate = 48000.0;
        const int block = 128;

        auto mono = C4Test::makePreparedProcessor (rate, block);
        auto stereo = C4Test::makePreparedProcessor (rate, block);

        for (auto* proc : { mono.get(), stereo.get() })
        {
            proc->getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("9.0"));
            proc->getC4Parameter (C4ParamIndex::kBiteQ)->setValue (
                proc->getC4Parameter (C4ParamIndex::kBiteQ)->getValueForText ("2.5"));
            proc->getC4Parameter (C4ParamIndex::kLpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kLpf)->getValueForText ("16000"));
        }

        std::vector<float> signal (rate / 2);
        for (int i = 0; i < (int) signal.size(); ++i)
            signal[(size_t) i] = 0.4f * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * 880.0 * i / rate);

        expect (C4Test::monoMatchesStereoLeft (*mono, *stereo, signal, block),
                "mono output must equal stereo-left sample-for-sample");
    }

    void testOversizedChunking()
    {
        beginTest ("Oversized host block chunking equals sequential legal blocks");
        const double rate = 48000.0;
        const int preparedBlock = 512;
        const int oversized = 5000; // host violates prepared max

        auto chunked = C4Test::makePreparedProcessor (rate, preparedBlock);
        auto sequential = C4Test::makePreparedProcessor (rate, preparedBlock);

        for (auto* proc : { chunked.get(), sequential.get() })
        {
            proc->getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
                proc->getC4Parameter (C4ParamIndex::kBiteGain)->getValueForText ("10.0"));
            proc->getC4Parameter (C4ParamIndex::kOpenFreq)->setValue (
                proc->getC4Parameter (C4ParamIndex::kOpenFreq)->getValueForText ("8000"));
            proc->getC4Parameter (C4ParamIndex::kHpf)->setValue (
                proc->getC4Parameter (C4ParamIndex::kHpf)->getValueForText ("40"));
        }

        std::vector<float> signal ((size_t) oversized);
        for (int i = 0; i < oversized; ++i)
            signal[(size_t) i] = 0.5f * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * 300.0 * i / rate);

        // Chunked path: one giant block.
        juce::AudioBuffer<float> big (1, oversized);
        for (int i = 0; i < oversized; ++i)
            big.setSample (0, i, signal[(size_t) i]);
        juce::MidiBuffer midi;
        chunked->processBlock (big, midi);

        // Sequential path: legal 512-sample blocks.
        juce::AudioBuffer<float> buf (1, preparedBlock);
        std::vector<float> seqOut ((size_t) oversized);
        size_t pos = 0;
        while (pos < signal.size())
        {
            const int n = (int) juce::jmin ((size_t) preparedBlock, signal.size() - pos);
            for (int i = 0; i < n; ++i)
                buf.setSample (0, i, signal[pos + (size_t) i]);
            sequential->processBlock (buf, midi);
            for (int i = 0; i < n; ++i)
                seqOut[pos + (size_t) i] = buf.getSample (0, i);
            pos += (size_t) n;
        }

        bool identical = true;
        size_t firstDiff = 0;
        for (int i = 0; i < oversized; ++i)
            if (big.getSample (0, i) != seqOut[(size_t) i])
            {
                identical = false;
                firstDiff = (size_t) i;
                break;
            }
        expect (identical,
                "chunked oversized block must equal sequential blocks (first diff at "
                    + juce::String ((int) firstDiff) + ")");
    }
};

static C4ProcessorTests c4ProcessorTests;

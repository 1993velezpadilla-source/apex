#include <JuceHeader.h>
#include "C4TestUtils.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4RenderPackTests — Phase 5 evidence: deterministic, level-matched render
// pack from the ONLY real musical source in the repository
// (stftPitchShift-main/examples/voice.wav).
//
// This suite is measurement infrastructure, NOT a subjective listening
// claim. It renders fixed C4 states to WAV and records the measured RMS/peak
// deltas so the pack can be reviewed externally with exact reproducibility.
// It never asserts that one render "sounds better" than another.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::makeProfileProduction;

class C4RenderPackTests final : public juce::UnitTest
{
public:
    C4RenderPackTests() : juce::UnitTest ("C4.RenderPack", "APEX.C4") {}

    void runTest() override
    {
        beginTest ("Phase 5 render pack: deterministic WAV renders + measured deltas");

        const juce::File source = locateSource();
        if (! source.existsAsFile())
        {
            logMessage ("C4.RenderPack: voice.wav not found — pack skipped (no fabrication).");
            return;
        }

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (source));
        if (reader == nullptr)
        {
            logMessage ("C4.RenderPack: could not open the source reader — skipped.");
            return;
        }

        const double rate = reader->sampleRate;
        const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
        const juce::int64 numFrames = reader->lengthInSamples;
        const int block = 512;

        juce::AudioBuffer<float> src (channels, (int) numFrames);
        reader->read (src.getArrayOfWritePointers(), channels, 0, (int) numFrames);

        const juce::File outDir = juce::File::getCurrentWorkingDirectory()
            .getChildFile ("evidence/renderpacks/C4_PHASE5");
        outDir.createDirectory();

        logMessage (juce::String ("C4.RenderPack source: ") + source.getFullPathName()
                    + " rate=" + juce::String (rate, 0) + " ch=" + juce::String (channels)
                    + " frames=" + juce::String (numFrames));

        // ---- variants -----------------------------------------------------
        struct Variant
        {
            const char* file;
            float bloomNorm;   // -1 = bypass
            float wGain, sGain, bGain, oGain;
            float outTrimDb;
        };
        const Variant variants[] =
        {
            { "01_REF_bypass.wav",           -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "02_C4_BLOOM0_FLAT.wav",        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { "03_C4_BOOM_BLOOM0.wav",        0.0f, 3.0f, 2.0f, 2.0f, 3.0f, 0.0f },
            { "04_C4_BOOM_BLOOM4.wav",        0.4f, 3.0f, 2.0f, 2.0f, 3.0f, 0.0f },
            { "06_C4_BOOM_BLOOM10.wav",       1.0f, 3.0f, 2.0f, 2.0f, 3.0f, 0.0f },
        };
        const int numVariants = 5;

        RenderResult results[numVariants + 1];

        for (int v = 0; v < numVariants; ++v)
        {
            const auto& cfg = variants[v];
            auto proc = std::make_unique<C4Processor> (makeProfileProduction());
            proc->prepareToPlay (rate, block);

            const auto setText = [&] (C4ParamIndex idx, const juce::String& text)
            {
                proc->getC4Parameter (idx)->setValue (
                    proc->getC4Parameter (idx)->getValueForText (text));
            };
            const auto setBandGain = [&] (C4BandId band, float db)
            {
                const auto gainIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightGain + (int) band * 3);
                setText (gainIdx, juce::String (db, 1));
            };

            if (cfg.bloomNorm < 0.0f)
            {
                proc->getC4Parameter (C4ParamIndex::kBypass)->setValue (1.0f);
            }
            else
            {
                proc->getC4Parameter (C4ParamIndex::kBloom)->setValue (cfg.bloomNorm);
                setBandGain (C4BandId::Weight, cfg.wGain);
                setBandGain (C4BandId::Sculpt, cfg.sGain);
                setBandGain (C4BandId::Bite, cfg.bGain);
                setBandGain (C4BandId::Open, cfg.oGain);
                if (cfg.outTrimDb != 0.0f)
                    setText (C4ParamIndex::kOutput, juce::String (cfg.outTrimDb, 3));
            }

            // Settle (0.4 s of silence) so every smoother is at target before
            // the source starts — the renders begin at the true source start.
            juce::AudioBuffer<float> silence (channels, block);
            juce::MidiBuffer midi;
            const int settleBlocks = (int) (rate * 0.4) / block;
            for (int b = 0; b < settleBlocks; ++b)
            {
                silence.clear();
                proc->processBlock (silence, midi);
            }

            // Render.
            juce::AudioBuffer<float> out (channels, (int) numFrames);
            juce::AudioBuffer<float> inBuf (channels, block);
            int pos = 0;
            while (pos < (int) numFrames)
            {
                const int n = juce::jmin (block, (int) numFrames - pos);
                for (int ch = 0; ch < channels; ++ch)
                    inBuf.copyFrom (ch, 0, src, ch, pos, n);
                proc->processBlock (inBuf, midi);
                for (int ch = 0; ch < channels; ++ch)
                    out.copyFrom (ch, pos, inBuf, ch, 0, n);
                pos += n;
            }

            const juce::File wav = outDir.getChildFile (cfg.file);
            writeWav (wav, out, rate);
            results[v].file = cfg.file;
            results[v].samples = (int) numFrames;
            results[v].rmsDb = rmsDbOf (out);
            results[v].peakDb = peakDbOf (out);

            expect (wav.existsAsFile() && wav.getSize() > 0,
                    "render must be written: " + juce::String (cfg.file));
            expect (C4Test::allFinite (out), "render must be finite: " + juce::String (cfg.file));
        }

        // ---- level-matched variant 05 --------------------------------
        const float refRms = results[0].rmsDb;
        const float boomRms = results[3].rmsDb; // 04_C4_BOOM_BLOOM4
        const float delta = boomRms - refRms;
        expect (delta > 0.2f && delta < 6.0f,
                "BOOM/BLOOM4 RMS delta must be a sane boost (measured "
                    + juce::String (delta, 2) + " dB)");

        {
            auto proc = std::make_unique<C4Processor> (makeProfileProduction());
            proc->prepareToPlay (rate, block);
            const auto setText = [&] (C4ParamIndex idx, const juce::String& text)
            {
                proc->getC4Parameter (idx)->setValue (
                    proc->getC4Parameter (idx)->getValueForText (text));
            };
            const auto setBandGain = [&] (C4BandId band, float db)
            {
                const auto gainIdx = (C4ParamIndex) ((int) C4ParamIndex::kWeightGain + (int) band * 3);
                setText (gainIdx, juce::String (db, 1));
            };
            proc->getC4Parameter (C4ParamIndex::kBloom)->setValue (0.4f);
            setBandGain (C4BandId::Weight, 3.0f);
            setBandGain (C4BandId::Sculpt, 2.0f);
            setBandGain (C4BandId::Bite, 2.0f);
            setBandGain (C4BandId::Open, 3.0f);
            setText (C4ParamIndex::kOutput, juce::String (-delta, 3));

            juce::AudioBuffer<float> silence (channels, block);
            juce::MidiBuffer midi;
            const int settleBlocks = (int) (rate * 0.4) / block;
            for (int b = 0; b < settleBlocks; ++b)
            {
                silence.clear();
                proc->processBlock (silence, midi);
            }

            juce::AudioBuffer<float> out (channels, (int) numFrames);
            juce::AudioBuffer<float> inBuf (channels, block);
            int pos = 0;
            while (pos < (int) numFrames)
            {
                const int n = juce::jmin (block, (int) numFrames - pos);
                for (int ch = 0; ch < channels; ++ch)
                    inBuf.copyFrom (ch, 0, src, ch, pos, n);
                proc->processBlock (inBuf, midi);
                for (int ch = 0; ch < channels; ++ch)
                    out.copyFrom (ch, pos, inBuf, ch, 0, n);
                pos += n;
            }

            const juce::File wav = outDir.getChildFile ("05_C4_BOOM_BLOOM4_MATCHED.wav");
            writeWav (wav, out, rate);
            const float matchedRms = rmsDbOf (out);
            const float matchedPeak = peakDbOf (out);

            logMessage (juce::String ("C4.RenderPack 05 matched: trim=") + juce::String (-delta, 3)
                        + " dB, RMS delta vs REF=" + juce::String (matchedRms - refRms, 2)
                        + " dB, peak delta vs REF=" + juce::String (matchedPeak - results[0].peakDb, 2) + " dB");
            expect (std::abs (matchedRms - refRms) < 0.15f,
                    "level-matched render must sit within 0.15 dB of the reference RMS");

            results[numVariants] = { "05_C4_BOOM_BLOOM4_MATCHED.wav", matchedRms, matchedPeak, (int) numFrames };
        }

        // ---- measured log + manifest -----------------------------------
        juce::String log = "C4.RenderPack measured deltas vs REF:\n";
        for (int v = 1; v <= numVariants; ++v)
            logMessage (juce::String (results[v].file)
                        + ": RMS " + juce::String (results[v].rmsDb - refRms, 2)
                        + " dB, peak " + juce::String (results[v].peakDb - results[0].peakDb, 2) + " dB");

        writeManifest (outDir, source, rate, channels, numFrames, delta,
                       results[0], results[3], results[numVariants]);
        logMessage (juce::String ("C4.RenderPack written to ") + outDir.getFullPathName());
    }

private:
    struct RenderResult
    {
        juce::String file;
        float rmsDb = -300.0f;
        float peakDb = -300.0f;
        int samples = 0;
    };

    static juce::File locateSource()
    {
        const juce::String rel = "stftPitchShift-main/examples/voice.wav";
        juce::File dir = juce::File::getCurrentWorkingDirectory();
        for (int up = 0; up < 4; ++up)
        {
            const juce::File f = dir.getChildFile (rel);
            if (f.existsAsFile())
                return f;
            dir = dir.getParentDirectory();
        }
        return {};
    }

    static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& buf, double rate)
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> fos (f.createOutputStream());
        if (fos == nullptr)
            return;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (fos.get(), rate, (unsigned int) buf.getNumChannels(),
                                    16, {}, 0));
        if (writer == nullptr)
            return;
        fos.release(); // writer owns the stream now
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    static float rmsDbOf (const juce::AudioBuffer<float>& buf)
    {
        double sum = 0.0;
        int count = 0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = buf.getSample (ch, i);
                sum += (double) v * v;
                ++count;
            }
        return (float) (10.0 * std::log10 (std::max (1e-30, sum / std::max (1, count))));
    }

    static float peakDbOf (const juce::AudioBuffer<float>& buf)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (ch, i)));
        return (float) (20.0 * std::log10 (std::max (1e-12f, peak)));
    }

    static void writeManifest (const juce::File& outDir, const juce::File& source,
                               double rate, int channels, juce::int64 frames,
                               float trimDb,
                               const RenderResult& ref,
                               const RenderResult& boom4,
                               const RenderResult& matched)
    {
        juce::String m;
        m << "# C4 Phase 5 Render Pack - MANIFEST (measured facts only)\n\n"
          << "Generated deterministically by `C4.RenderPack` (APEX.C4 suite).\n\n"
          << "Source: " << source.getFullPathName() << "\n"
          << "Format: " << juce::String (rate, 0) << " Hz, " << juce::String (channels)
          << " ch, " << juce::String (frames) << " frames\n"
          << "Block size: 512 (bounded chunking; block-size independent by contract)\n\n"
          << "Processor: APEX C4 production profile (Phase 5 frozen),\n"
          << "rendered at the source rate.\n"
          << "Production profile: BLOOM default 4.0; coupling 0.50/0.32/1.1/1.6;\n"
          << "color W/S/B/O 0.055/0.055/0.050/0.028, evenW 0.75/0.65/0.50/0.40,\n"
          << "activationExp 0.85, cutSuppression 1.0, bloomLaw 0.85, OS 2x;\n"
          << "HALO softness 0.5, amount unity (see C4_PHASE5_PERCEPTUAL_TUNING.md).\n\n"
          << "Settings (BOOM): WEIGHT +3 dB @100 Hz, SCULPT +2 dB @400 Hz,\n"
          << "BITE +2 dB @2.5 kHz, OPEN +3 dB @10 kHz (bell modes).\n"
          << "No limiter, no compressor, no external saturation.\n\n"
          << "Files:\n"
          << "- 01_REF_bypass.wav             C4 bypassed (reference)\n"
          << "- 02_C4_BLOOM0_FLAT.wav         BLOOM 0, all bands 0 dB\n"
          << "- 03_C4_BOOM_BLOOM0.wav         BOOM settings, BLOOM 0 (linear contour)\n"
          << "- 04_C4_BOOM_BLOOM4.wav         BOOM settings, BLOOM 4.0 (production default)\n"
          << "- 05_C4_BOOM_BLOOM4_MATCHED.wav BOOM settings, BLOOM 4.0, output trim "
          << juce::String (-trimDb, 3) << " dB (level-matched to REF)\n"
          << "- 06_C4_BOOM_BLOOM10.wav        BOOM settings, BLOOM 10 (high reference)\n\n"
          << "MEASURED (this run):\n"
          << "- 04 vs 01 RMS delta: " << juce::String (boom4.rmsDb - ref.rmsDb, 2) << " dB\n"
          << "- 04 vs 01 peak delta: " << juce::String (boom4.peakDb - ref.peakDb, 2) << " dB\n"
          << "- 05 vs 01 RMS delta: " << juce::String (matched.rmsDb - ref.rmsDb, 2)
          << " dB (level-match compensation = " << juce::String (-trimDb, 3) << " dB)\n"
          << "- 05 vs 01 peak delta: " << juce::String (matched.peakDb - ref.peakDb, 2) << " dB\n"
          << "- Crest factor (peak - RMS): 01=" << juce::String (ref.peakDb - ref.rmsDb, 2)
          << " dB, 04=" << juce::String (boom4.peakDb - boom4.rmsDb, 2)
          << " dB, 05=" << juce::String (matched.peakDb - matched.rmsDb, 2) << " dB\n"
          << "- File hashes: not available in the test project (juce_cryptography is\n"
          << "  not part of APEXTests); file sizes are verifiable on disk.\n\n"
          << "Known limitation: this is the ONLY real musical source in the\n"
          << "repository (a vocal example). No bass/drums/bus/mix corpus claims\n"
          << "are made. The listening pass criterion (level-matched bypass feels\n"
          << "smaller) is a human judgment — not asserted by this suite.\n";

        outDir.getChildFile ("RENDER_MANIFEST.md").replaceWithText (m);
    }
};

static C4RenderPackTests c4RenderPackTests;

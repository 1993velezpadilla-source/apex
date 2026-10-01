#include <JuceHeader.h>
#include "C4TestUtils.h"
#include "../../../Source/C4UI/C4Editor.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4VisualPreviewTests - Phase 7 visual-review harness.
//
// Two jobs, both driven from the REAL compiled editor (never a disconnected
// mock):
//
//   1. Renders the visual review set to
//      evidence/renderpacks/C4_PHASE7_PREVIEW/ (01..10 PNGs) and writes
//      C4_PHASE7_REVIEW.html â€” a local review board of fresh captures.
//
//   2. LIVE NATIVE PREVIEW: with env C4_PREVIEW_LIVE=1 the harness opens the
//      ACTUAL C4Editor in a real native window and pumps the message loop
//      for C4_PREVIEW_HOLD_MS (default 60000 ms). Rerun any time:
//
//        $env:C4_PREVIEW_LIVE='1'
//        $env:C4_PREVIEW_HOLD_MS='120000'
//        powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 `
//            -Configuration Debug -Category APEX.C4 -Name C4.Preview -Seed 0xA9E12026
//
// The sound is never exercised here beyond feeding the analyzer a short
// tone so the PRE/POST spectra have real content to draw.
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4BandId;
using APEX::C4::C4SpectrumTapMode;
using APEX::C4::makeProfileProduction;

class C4VisualPreviewTests final : public juce::UnitTest
{
public:
    C4VisualPreviewTests() : juce::UnitTest ("C4.Preview", "APEX.C4") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        // Output location: env override (C4_PREVIEW_DIR) for a guaranteed-local
        // non-synced review surface; the workspace evidence path is the
        // default. OneDrive cloud placeholders can serve stale bytes for the
        // synced path, so the live review should use the override.
        const juce::String overrideDir = juce::SystemStats::getEnvironmentVariable (
            "C4_PREVIEW_DIR", juce::String());
        const juce::File outDir = overrideDir.isNotEmpty()
            ? juce::File (overrideDir)
            : juce::File::getCurrentWorkingDirectory()
                  .getChildFile ("evidence/renderpacks/C4_PHASE7_PREVIEW");
        outDir.createDirectory();

        C4Processor proc (makeProfileProduction());
        proc.prepareToPlay (48000.0, 512);

        // A representative console state (the BOOM contour at the production
        // BLOOM default 4.0) so every capture shows a shaped, live C4.
        auto setText = [&] (C4ParamIndex idx, const juce::String& t)
        {
            proc.getC4Parameter (idx)->setValue (proc.getC4Parameter (idx)->getValueForText (t));
        };
        setText (C4ParamIndex::kWeightGain, "3.0");
        setText (C4ParamIndex::kWeightFreq, "100");
        setText (C4ParamIndex::kSculptGain, "6.5");
        setText (C4ParamIndex::kSculptFreq, "400");
        setText (C4ParamIndex::kBiteGain, "-2.5");
        setText (C4ParamIndex::kBiteFreq, "2500");
        setText (C4ParamIndex::kOpenGain, "3.0");
        setText (C4ParamIndex::kOpenFreq, "10000");
        // BLOOM stays at the production default 4.0.

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "the preview must own a real editor");

        auto renderToPng = [&] (const juce::String& name, juce::Component& comp) -> juce::File
        {
            logMessage ("C4.Preview render: " + name + " component size = "
                        + juce::String (comp.getWidth()) + "x" + juce::String (comp.getHeight()));
            juce::Image img (juce::Image::ARGB, juce::jmax (1, comp.getWidth()),
                             juce::jmax (1, comp.getHeight()), true);
            {
                juce::Graphics g (img);
                comp.paintEntireComponent (g, false);
            }
            const juce::File f = outDir.getChildFile (name);
            f.deleteFile(); // remove any cloud placeholder / stale bytes
            juce::FileOutputStream fos (f);
            if (fos.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (img, fos);
            }
            return f;
        };

        auto feedAnalyzer = [&] (double seconds)
        {
            // Two tones so the spectra show structure (1 kHz + 5 kHz).
            juce::AudioBuffer<float> buf (1, 512);
            juce::MidiBuffer midi;
            const int total = (int) (48000.0 * seconds);
            int pos = 0;
            while (pos < total)
            {
                const int n = juce::jmin (512, total - pos);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, (float) (0.35 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1000.0 * (pos + i) / 48000.0)
                        + 0.15 * std::sin (2.0 * juce::MathConstants<double>::pi
                                          * 5000.0 * (pos + i) / 48000.0)));
                proc.processBlock (buf, midi);
                pos += n;
                juce::Thread::sleep (8);
            }
        };

        auto waitSpectrum = [&] (const juce::String& tag)
        {
            // The worker keeps up with real-time feeding, so `before` must be
            // captured BETWEEN two feeds: new data must exist for the index
            // to advance past it.
            feedAnalyzer (0.5); // settle
            const int before = proc.getSpectrumCore().getSnapshot().index;
            feedAnalyzer (0.4); // measured window
            const int after = proc.getSpectrumCore().waitForSnapshot (before, 3000);
            const bool ok = after > before;
            if (! ok)
                logMessage ("C4.Preview wait FAILED (" + tag
                            + "): workerRunning=" + juce::String ((int) proc.getSpectrumCore().isWorkerRunning())
                            + " mode=" + juce::String ((int) proc.getSpectrumMode())
                            + " idx=" + juce::String (after)
                            + " prePush=" + juce::String (proc.getSpectrumCore().prePushCount())
                            + " preDrops=" + juce::String (proc.getSpectrumCore().preDropCount())
                            + " preConsumed=" + juce::String (proc.getSpectrumCore().preConsumed())
                            + " postPush=" + juce::String (proc.getSpectrumCore().postPushCount())
                            + " postConsumed=" + juce::String (proc.getSpectrumCore().postConsumed()));
            return ok;
        };

        // 01 - compact, Spectrum Flag CLOSED (the default plugin view).
        // TALL VERTICAL CONSOLE proportions.
        editor->setSize (660, 640);
        renderToPng ("01_C4_Compact.png", *editor);

        // 02 - Spectrum Flag expanded (BOTH) with live spectra.
        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Both); else proc.setSpectrumMode (C4SpectrumTapMode::Both);
        editor->setSize (660, 880);
        expect (waitSpectrum ("expanded"), "expanded capture must have analyzer data");
        renderToPng ("02_C4_Spectrum_Expanded.png", *editor);

        // 03 - PRE mode.
        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Pre); else proc.setSpectrumMode (C4SpectrumTapMode::Pre);
        expect (waitSpectrum ("PRE"), "PRE capture must have analyzer data");
        renderToPng ("03_C4_Spectrum_PRE.png", *editor);

        // 04 - POST mode.
        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Post); else proc.setSpectrumMode (C4SpectrumTapMode::Post);
        expect (waitSpectrum ("POST"), "POST capture must have analyzer data");
        renderToPng ("04_C4_Spectrum_POST.png", *editor);

        // 05 - BOTH mode (final expanded state).
        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Both); else proc.setSpectrumMode (C4SpectrumTapMode::Both);
        expect (waitSpectrum ("BOTH"), "BOTH capture must have analyzer data");
        renderToPng ("05_C4_Spectrum_BOTH.png", *editor);

        // 06 - one vertical console strip close-up (SCULPT strip, real bounds).
        if (auto* strip = dynamic_cast<APEX::C4::C4Editor*> (editor.get())
                ->getBandStripForPreview (1))
            renderToPng ("06_C4_Bands_Closeup.png", *strip);

        // 07 - BLOOM bottom rail close-up (real component bounds).
        if (auto* bloom = dynamic_cast<APEX::C4::C4Editor*> (editor.get())
                ->getBloomSectionForPreview())
            renderToPng ("07_C4_Bloom_TopRail_Closeup.png", *bloom);

        // 11 - the four vertical strips together (band-area crop of the
        // compact capture).
        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Closed); else proc.setSpectrumMode (C4SpectrumTapMode::Closed);
        editor->setSize (660, 640);
        {
            juce::Image full (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            {
                juce::Graphics g (full);
                editor->paintEntireComponent (g, false);
            }
            const juce::Rectangle<int> strips (8, 92, 644, 440);
            juce::Image crop = full.getClippedImage (strips.getIntersection (full.getBounds()));
            juce::File f = outDir.getChildFile ("11_C4_Strips_Closeup.png");
            juce::FileOutputStream fos (f);
            if (fos.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (crop, fos);
            }
        }

        // 08/09/10 - the resizable console at three honest sizes.
        editor->setSize (560, 640);
        renderToPng ("08_C4_MinimumSize.png", *editor);
        editor->setSize (660, 640);
        renderToPng ("09_C4_NormalSize.png", *editor);
        editor->setSize (880, 760);
        renderToPng ("10_C4_LargerSize.png", *editor);

        if (auto* ed = dynamic_cast<APEX::C4::C4Editor*> (editor.get())) ed->applySpectrumMode (C4SpectrumTapMode::Closed); else proc.setSpectrumMode (C4SpectrumTapMode::Closed);

        writeReviewPage (outDir);

        // ---- LIVE NATIVE PREVIEW (optional; env-gated) --------------------
        const juce::String liveEnv = juce::SystemStats::getEnvironmentVariable (
            "C4_PREVIEW_LIVE", juce::String());
        if (liveEnv.equalsIgnoreCase ("1"))
        {
            logMessage ("C4.Preview: opening the LIVE native editor window...");
            juce::DocumentWindow window ("APEX C4 - Visual Preview",
                                         juce::Colours::black,
                                         juce::DocumentWindow::allButtons);
            window.setContentOwned (proc.createEditor(), true);
            window.setResizable (true, true);
            window.centreWithSize (660, 880);
            window.setVisible (true);

            const int holdMs = juce::SystemStats::getEnvironmentVariable (
                "C4_PREVIEW_HOLD_MS", "60000").getIntValue();
            const juce::uint32 deadline = juce::Time::getMillisecondCounter()
                                        + (juce::uint32) juce::jmax (1000, holdMs);
            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            logMessage ("C4.Preview: live window closed after hold.");
        }
        else
        {
            logMessage ("C4.Preview: set C4_PREVIEW_LIVE=1 to open the live native window.");
        }
    }

private:
    static void writeReviewPage (const juce::File& dir)
    {
        juce::String html;
        html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             << "<title>APEX C4 - Phase 7 Visual Review</title>"
             << "<style>body{background:#0c0b09;color:#e8e2d4;font-family:Segoe UI,sans-serif;"
                "margin:24px;}h1{color:#c9a45c;}h2{color:#c9a45c;margin-top:32px;}"
                "img{max-width:100%;border:1px solid #2a241c;margin:8px 0;}"
                "p{color:#93897a;max-width:1060px;}</style></head><body>"
             << "<h1>APEX C4 - Phase 7 Visual Review (fresh captures from the compiled editor)</h1>"
             << "<p>These images are rendered from the ACTUAL C4Editor implementation "
                "(Source/C4UI/C4Editor.h) via the C4.Preview harness. The native window "
                "preview can be reopened any time with C4_PREVIEW_LIVE=1.</p>";

        const struct { const char* file; const char* caption; } shots[] =
        {
            { "01_C4_Compact.png",           "01 - Compact (Spectrum Flag CLOSED, 660x640)" },
            { "02_C4_Spectrum_Expanded.png", "02 - Spectrum Flag expanded, BOTH, 660x880" },
            { "03_C4_Spectrum_PRE.png",      "03 - Spectrum Flag PRE mode" },
            { "04_C4_Spectrum_POST.png",     "04 - Spectrum Flag POST mode" },
            { "05_C4_Spectrum_BOTH.png",     "05 - Spectrum Flag BOTH mode" },
            { "06_C4_Bands_Closeup.png",     "06 - One vertical console strip (SCULPT)" },
            { "07_C4_Bloom_TopRail_Closeup.png", "07 - BLOOM bottom rail close-up" },
            { "11_C4_Strips_Closeup.png",    "11 - The four vertical console strips together" },
            { "08_C4_MinimumSize.png",       "08 - Minimum size (560x640)" },
            { "09_C4_NormalSize.png",        "09 - Normal size (660x640)" },
            { "10_C4_LargerSize.png",        "10 - Larger size (880x760)" },
        };

        for (const auto& s : shots)
        {
            html << "<h2>" << s.caption << "</h2>"
                 << "<img src=\"" << s.file << "\" alt=\"" << s.caption << "\">";
        }

        html << "<h2>Review notes</h2>"
             << "<p>VERTICAL CONSOLE architecture: the four tall strips (WEIGHT bronze / "
                "SCULPT amber / BITE red-orange / OPEN ice-blue) are the hero; the top "
                "utility row is supporting; BLOOM (gold, 0-10, default 4.0) owns the bottom "
                "rail; the Spectrum Flag expands underneath the console. The analytic "
                "response curve (gold) is the LINEAR transfer only - BLOOM harmonics are "
                "not drawn as a curve. No draggable spectrum nodes. DSP (Phases 5-6) is "
                "unchanged by any visual work.</p>"
             << "</body></html>";

        dir.getChildFile ("C4_PHASE7_REVIEW.html").replaceWithText (html);
    }
};

static C4VisualPreviewTests c4VisualPreviewTests;

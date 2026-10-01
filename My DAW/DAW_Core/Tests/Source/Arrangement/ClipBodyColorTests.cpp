// ===========================================================================
// ClipBodyColorTests.cpp
// The live issue: the audio clip BODY/HEADER stayed the old fixed
// violet-purple family (engine clip default colour) even though the waveform
// correctly followed the Track colour — the clip no longer felt unified.
//
// Root cause (production): ClipRenderCore::drawBackground derived the body
// from m_model->colour (the engine clip colour — violet default), not from
// the canonical ArrangementClipModel::trackColour.
//
// Production fix (applied):
//   * ClipBodyColourCore — canonical dark track-tint resolver (hue preserved,
//     saturation substantially reduced, brightness very low; selection/hover
//     lift brightness; muted drops alpha/saturation; explicit per-clip
//     override replaces the tint SOURCE hue — same dark language)
//   * ClipRenderCore::drawBackground resolves body + header from
//     m_model->trackColour via ClipBodyColourCore
//   * ArrangementClipModel::explicitClipColourOverride — set by
//     ClipColourPickerCore (the explicit per-clip tint path); default false
//
// Pixel methodology (same as WaveformColorTests): the fixture renders BEFORE
// waveform peaks exist (body-only pass) and AFTER priming (waveform pass).
//   * WAVEFORM pixels  = pixels that CHANGED between the passes
//   * BODY pixels      = pixels that did NOT change (in the band below the
//                        header, excluding the border)
// IMPORTANT: the body-only pass MUST be captured AFTER the full visual state
// (track colour, clip colour, selection, mute, override) is set — the body
// pixels must be identical between the two passes except for the waveform.
// The OLD violet body fails the discriminators (body violet-family count
// >= track-family count).
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipWaveformColourCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipBodyColourCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipModel.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipRenderCore.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include "../../../Source/QuickTrackCore/QuickTrackBuilderCore.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"

namespace
{

using namespace ArrangementEditor;
using namespace DAW;

const juce::Colour kEngineClipDefaultViolet(0xFFB661F4);

struct ClipBodyRenderFixture
{
    ArrangementZoomCore zoom;
    ArrangementClipModel model;
    std::unique_ptr<ClipRenderCore> renderer;

    ClipBodyRenderFixture()
    {
        model.id = juce::Uuid();
        model.trackIndex = 0;
        model.startTime = 0.0;
        model.length = 1.0;
        model.clipName = "test-clip";
        model.colour = kEngineClipDefaultViolet;   // engine clip default (violet)
        model.trackColour = juce::Colour(0xFF087BFF);
        renderer = std::make_unique<ClipRenderCore>(model, zoom);
        renderer->setSize(320, 120);
        renderer->setEngineSampleRate(44100.0);
    }

    void setTrackColour(const juce::Colour& trackColour)
    {
        model.trackColour = trackColour;
        renderer->updateClipData(model);
    }

    // Set the FULL visual state, then capture the body-only pass (BEFORE
    // priming — drawWaveform bails at isReady()), then prime. The returned
    // image is the correct body baseline for the differential classifiers.
    bool prepare(const juce::Colour& trackColour, juce::Image& bodyOut)
    {
        setTrackColour(trackColour);
        bodyOut = render();
        return primeWaveform();
    }

    bool primeWaveform()
    {
        juce::AudioBuffer<float> buf(2, 8192);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const double t = (double)i / 44100.0;
                buf.setSample(c, i,
                    (float)(0.30 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                          + 0.20 * std::sin(2.0 * juce::MathConstants<double>::pi * 330.0 * t)));
            }
        }
        auto& cache = renderer->getWaveformCache();
        cache.requestPeaksFromBuffer(buf, 320);
        for (int i = 0; i < 1000; ++i)
        {
            if (cache.isReady())
                return true;
            juce::Thread::sleep(10);
        }
        return cache.isReady();
    }

    juce::Image render()
    {
        juce::Image img(juce::Image::ARGB, 320, 120, true);
        juce::Graphics g(img);
        renderer->paint(g);
        return img;
    }

    static bool isSamePixel(const juce::Colour& a, const juce::Colour& b)
    {
        return std::fabs(a.getHue() - b.getHue()) < 0.02f
            && std::fabs(a.getAlpha() - b.getAlpha()) < 0.02f;
    }

    // Waveform pixels: changed between passes, in the production waveform
    // band (drawWaveform areaTop = kNameH = 16), classified by hue family.
    static int countWaveformPixelsInFamily(const juce::Image& body, const juce::Image& wf,
                                           float familyHue, float tolerance = 0.13f)
    {
        constexpr int kNameH = 16;
        int count = 0;
        for (int y = kNameH; y < wf.getHeight() - 4; ++y)
        {
            for (int x = 0; x < wf.getWidth(); ++x)
            {
                const auto pw = wf.getPixelAt(x, y);
                if (pw.getAlpha() < 8 || isSamePixel(pw, body.getPixelAt(x, y)))
                    continue;
                const float d = std::fabs(pw.getHue() - familyHue);
                if (d < tolerance || (1.0f - d) < tolerance)
                    ++count;
            }
        }
        return count;
    }

    // Body pixels: UNCHANGED between passes, below the header, inside the
    // clip, classified by hue family.
    static int countBodyPixelsInFamily(const juce::Image& body, const juce::Image& wf,
                                       float familyHue, float tolerance = 0.13f)
    {
        constexpr int kNameH = 16;
        int count = 0;
        for (int y = kNameH + 2; y < wf.getHeight() - 4; ++y)
        {
            for (int x = 2; x < wf.getWidth() - 2; ++x)
            {
                const auto pb = body.getPixelAt(x, y);
                const auto pw = wf.getPixelAt(x, y);
                if (pb.getAlpha() < 8 || !isSamePixel(pb, pw))
                    continue; // not body (transparent or waveform column)
                const float d = std::fabs(pb.getHue() - familyHue);
                if (d < tolerance || (1.0f - d) < tolerance)
                    ++count;
            }
        }
        return count;
    }

    // Header pixels (name strip, band [0..kNameH)): never waveform-contaminated
    // — a direct classifier of the CURRENT state (no differential needed).
    static int countHeaderPixelsInFamily(const juce::Image& img,
                                         float familyHue, float tolerance = 0.13f)
    {
        constexpr int kNameH = 16;
        int count = 0;
        for (int y = 0; y < kNameH; ++y)
        {
            for (int x = 2; x < img.getWidth() - 2; ++x)
            {
                const auto p = img.getPixelAt(x, y);
                if (p.getAlpha() < 8)
                    continue;
                const float d = std::fabs(p.getHue() - familyHue);
                if (d < tolerance || (1.0f - d) < tolerance)
                    ++count;
            }
        }
        return count;
    }

    static float luminance(const juce::Colour& c)
    {
        // Normalized 0-1 channel accessors: getRed/Green/Blue return 0-255
        // integer-style values in JUCE (same class of bug as the alpha check).
        return 0.2126f * c.getFloatRed()
             + 0.7152f * c.getFloatGreen()
             + 0.0722f * c.getFloatBlue();
    }

    // Mean luminance of true body pixels (unchanged, below header, inside a
    // chrome-free window: x [40..w-40], y [40..h-20] — avoids badges, side
    // buttons, handles and the gain line while staying inside the body).
    static float meanBodyLuminance(const juce::Image& body, const juce::Image& wf)
    {
        constexpr int kNameH = 16;
        double sum = 0.0; int n = 0;
        for (int y = juce::jmax(kNameH + 2, 40); y < wf.getHeight() - 20; ++y)
        {
            for (int x = 40; x < wf.getWidth() - 40; ++x)
            {
                const auto pb = body.getPixelAt(x, y);
                if (pb.getAlpha() < 8 || !isSamePixel(pb, wf.getPixelAt(x, y)))
                    continue;
                sum += luminance(pb); ++n;
            }
        }
        return n > 0 ? (float)(sum / n) : 0.0f;
    }

    // Mean luminance of true waveform pixels (changed) in the same
    // chrome-free window — a consistent basis for contrast comparisons.
    static float meanWaveformLuminance(const juce::Image& body, const juce::Image& wf)
    {
        constexpr int kNameH = 16;
        double sum = 0.0; int n = 0;
        for (int y = juce::jmax(kNameH, 40); y < wf.getHeight() - 20; ++y)
        {
            for (int x = 40; x < wf.getWidth() - 40; ++x)
            {
                const auto pw = wf.getPixelAt(x, y);
                if (pw.getAlpha() < 8 || isSamePixel(pw, body.getPixelAt(x, y)))
                    continue;
                sum += luminance(pw); ++n;
            }
        }
        return n > 0 ? (float)(sum / n) : 0.0f;
    }

    // Mean alpha of true body pixels (muted-state comparison).
    static float meanBodyAlpha(const juce::Image& body, const juce::Image& wf)
    {
        constexpr int kNameH = 16;
        double sum = 0.0; int n = 0;
        for (int y = kNameH + 2; y < wf.getHeight() - 4; ++y)
        {
            for (int x = 2; x < wf.getWidth() - 2; ++x)
            {
                const auto pb = body.getPixelAt(x, y);
                if (pb.getAlpha() < 8 || !isSamePixel(pb, wf.getPixelAt(x, y)))
                    continue;
                sum += pb.getAlpha(); ++n;
            }
        }
        return n > 0 ? (float)(sum / n) : 0.0f;
    }
};

class ClipBodyColorTests final : public juce::UnitTest
{
public:
    ClipBodyColorTests() : juce::UnitTest("ClipBodyColor", "APEX.ClipBodyColor") {}

    void runTest() override
    {
        beginTest("FollowsTrackColor");
        {
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            juce::Image body;
            expect(f.prepare(green, body), "waveform peaks ready");

            const auto img = f.render();
            const auto greenBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, green.getHue());
            const auto violetBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());
            expect(greenBody > 50, "body tint in the track hue family");
            expect(violetBody < greenBody, "body no longer the default violet (discriminator)");
        }

        beginTest("IsDarkVariantOfTrackColor");
        {
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            juce::Image body;
            expect(f.prepare(green, body), "waveform peaks ready");

            // Resolver-level darkness proof (exact, deterministic): the
            // canonical tint output is a dark variant of the track colour.
            const auto bodyColour = ClipBodyColourCore::resolveClipBodyColour(green, false, false, false, nullptr);
            expect(ClipBodyRenderFixture::luminance(bodyColour) < 0.30f,
                   "resolver produces a dark body variant (luminance < 0.30)");
            expect(ClipWaveformColourCore::sameHueFamily(bodyColour, green),
                   "resolver keeps the track hue family");

            // Pixel-level relative contrast (robust): the rendered body is
            // materially darker than the rendered waveform.
            const auto img = f.render();
            const float bodyLum = ClipBodyRenderFixture::meanBodyLuminance(body, img);
            const float wfLum = ClipBodyRenderFixture::meanWaveformLuminance(body, img);
            expect(bodyLum > 0.01f, "body pixels present (classifier sanity)");
            expect(bodyLum < wfLum / 2.0f, "body materially darker than the waveform");
        }

        beginTest("WaveformMaintainsReadableContrast");
        {
            ClipBodyRenderFixture f;
            const juce::Colour cyan(0xFF00E5FF);
            juce::Image body;
            expect(f.prepare(cyan, body), "waveform peaks ready");

            const auto img = f.render();
            const float bodyLum = ClipBodyRenderFixture::meanBodyLuminance(body, img);
            const float wfLum = ClipBodyRenderFixture::meanWaveformLuminance(body, img);
            expect(wfLum > 2.0f * bodyLum, "waveform remains clearly readable over the tint");
        }

        beginTest("ManualTrackColorChangeUpdatesImmediately");
        {
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            const juce::Colour cyan(0xFF00E5FF);
            juce::Image body;
            expect(f.prepare(green, body), "waveform peaks ready");

            const auto imgGreen = f.render();
            const auto greenHeaderBefore = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgGreen, green.getHue(), 0.05f);
            expect(greenHeaderBefore > 3000, "green-family header before the change");

            f.setTrackColour(cyan);
            const auto imgCyan = f.render();
            // Tight tolerance: the exact-hue tint dominates the header, so the
            // new family count is huge and the old family count is ~stray AA.
            const auto cyanHeader = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgCyan, cyan.getHue(), 0.05f);
            const auto greenHeaderLeft = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgCyan, green.getHue(), 0.05f);
            expect(cyanHeader > 3000, "header updates immediately to the new track colour");
            expect(greenHeaderLeft < 300, "old tint gone after the change");
        }

        beginTest("MovingClipToDifferentTrackUpdatesTint");
        {
            ClipBodyRenderFixture f;
            const juce::Colour yellow(0xFFFFD500);
            const juce::Colour cyan(0xFF00E5FF);
            juce::Image body;
            expect(f.prepare(yellow, body), "waveform peaks ready");

            const auto imgYellow = f.render();
            const auto yellowHeaderBefore = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgYellow, yellow.getHue(), 0.05f);
            expect(yellowHeaderBefore > 3000, "yellow-family header on Track A");

            f.setTrackColour(cyan); // clip moved to Track B (canonical model update)
            const auto imgCyan = f.render();
            const auto cyanHeader = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgCyan, cyan.getHue(), 0.05f);
            const auto yellowHeaderLeft = ClipBodyRenderFixture::countHeaderPixelsInFamily(imgCyan, yellow.getHue(), 0.05f);
            expect(cyanHeader > 3000, "header adopts the destination track tint");
            expect(yellowHeaderLeft < 300, "no stale tint after the move");
        }

        beginTest("QuickTrackAssignedColorPropagates");
        {
            TrackManager tracks;
            RoutingGraph graph;
            MasterRouteStateCore masterRoute { graph };
            BubblegumSendStateCore sends;
            QuickTrackColorSystem colors { 70707 };
            QuickTrackBuilderCore builder { tracks, graph, masterRoute, sends, colors };
            tracks.createMasterTrack();

            const auto* lead = QuickTrackRoleCatalog::findById("lead_vocal");
            auto result = builder.createBatch({ { lead, 1 } });
            expect(result.ok());
            Track* created = tracks.getTrack(0);
            expect(created != nullptr);
            const juce::Colour assigned = created->getColor();

            ClipBodyRenderFixture f;
            juce::Image body;
            expect(f.prepare(assigned, body), "waveform peaks ready");
            expect(ClipBodyRenderFixture::countBodyPixelsInFamily(body, f.render(), assigned.getHue()) > 50,
                   "body follows the QuickTrack-assigned track colour");
        }

        beginTest("ProjectReloadUsesPersistedTrackColor");
        {
            TrackManager tracks;
            tracks.createMasterTrack();
            auto* track = tracks.createTrack("Reloaded");
            expect(track != nullptr);

            const juce::Colour orange(0xFFFF6D00);
            track->setColor(orange);          // persisted value restored
            expectEquals(track->getColor().getARGB(), orange.getARGB(), "restored track colour");

            ClipBodyRenderFixture f;
            juce::Image body;
            expect(f.prepare(track->getColor(), body), "waveform peaks ready");
            expect(ClipBodyRenderFixture::countBodyPixelsInFamily(body, f.render(), orange.getHue()) > 50,
                   "body uses the persisted track colour after reload");
        }

        beginTest("RecordedClipUsesTrackTint");
        {
            ClipBodyRenderFixture f;
            const juce::Colour pink(0xFFF50057);
            f.model.colour = kEngineClipDefaultViolet; // recorded-clip default
            juce::Image body;
            expect(f.prepare(pink, body), "waveform peaks ready");

            const auto img = f.render();
            const auto pinkBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, pink.getHue());
            const auto violetBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());
            expect(pinkBody > 50, "recorded clip body uses the track tint");
            expect(violetBody < pinkBody, "recorded violet default does not leak");
        }

        beginTest("ImportedClipUsesDestinationTrackTint");
        {
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            f.model.colour = kEngineClipDefaultViolet; // import default
            juce::Image body;
            expect(f.prepare(green, body), "waveform peaks ready");

            const auto img = f.render();
            const auto greenBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, green.getHue());
            const auto violetBody = ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());
            expect(greenBody > 50, "imported clip body uses the destination track tint");
            expect(violetBody < greenBody, "import violet default does not leak");
        }

        beginTest("MutedStateStaysInTrackHueFamily");
        {
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            f.setTrackColour(green);

            // Body-only passes captured BEFORE priming, in each state.
            const auto bodyNormal = f.render();
            f.model.muted = true;
            f.renderer->updateClipData(f.model);
            const auto bodyMuted = f.render();
            f.model.muted = false;
            f.renderer->updateClipData(f.model);
            expect(f.primeWaveform(), "waveform peaks ready");

            const auto img = f.render();
            const float normalAlpha = ClipBodyRenderFixture::meanBodyAlpha(bodyNormal, img);
            expect(normalAlpha > 0.85f, "normal body alpha near 0.92");

            f.model.muted = true;
            f.renderer->updateClipData(f.model);
            const auto imgMuted = f.render();
            const float mutedAlpha = ClipBodyRenderFixture::meanBodyAlpha(bodyMuted, imgMuted);
            const float normalLum = ClipBodyRenderFixture::meanBodyLuminance(bodyNormal, img);
            const float mutedLum = ClipBodyRenderFixture::meanBodyLuminance(bodyMuted, imgMuted);
            expect(mutedAlpha < normalAlpha - 0.2f, "muted body alpha reduced (canonical muted treatment)");
            expect(mutedLum < normalLum, "muted body dimmed by the canonical mute overlay");

            // The TINT under the overlay is the resolver's — it must stay in
            // the track hue family (the dim overlay is a separate canonical
            // layer preserved by design, so pixel hue is overlay-blended).
            const auto mutedTint = ClipBodyColourCore::resolveClipBodyColour(green, false, true, false, nullptr);
            expect(ClipWaveformColourCore::sameHueFamily(mutedTint, green),
                   "muted tint stays in the track hue family — no purple fallback");
            expect(!ClipWaveformColourCore::sameHueFamily(mutedTint, kEngineClipDefaultViolet),
                   "muted tint is not the old violet default");
        }

        beginTest("SelectedStateRemainsReadable");
        {
            ClipBodyRenderFixture f;
            const juce::Colour cyan(0xFF00E5FF);
            f.setTrackColour(cyan);

            // Body-only passes captured BEFORE priming, per selection state.
            const auto bodyNormal = f.render();
            f.renderer->setSelected(true);
            const auto bodySelected = f.render();
            f.renderer->setSelected(false);
            expect(f.primeWaveform(), "waveform peaks ready");

            const auto imgNormal = f.render();
            const float normalLum = ClipBodyRenderFixture::meanBodyLuminance(bodyNormal, imgNormal);

            f.renderer->setSelected(true);
            const auto imgSelected = f.render();
            const float selectedLum = ClipBodyRenderFixture::meanBodyLuminance(bodySelected, imgSelected);
            expect(selectedLum > normalLum, "selected body lifted for readability");
            expect(ClipBodyRenderFixture::countBodyPixelsInFamily(bodySelected, imgSelected, cyan.getHue()) > 50,
                   "selected body stays in the track hue family");
        }

        beginTest("ColorChangeDoesNotReanalyzeWaveform");
        {
            ClipBodyRenderFixture f;
            expect(f.primeWaveform(), "waveform peaks ready");

            auto& cache = f.renderer->getWaveformCache();
            juce::CriticalSection::ScopedLockType lock(cache.getPeaksLock());
            const auto peaksBefore = cache.getPeaks();

            const juce::Colour yellow(0xFFFFD500);
            const juce::Colour cyan(0xFF00E5FF);
            f.setTrackColour(yellow);
            f.render();
            f.setTrackColour(cyan);
            f.render();

            const auto& peaksAfter = cache.getPeaks();
            expect(peaksAfter.valid == peaksBefore.valid, "peak validity unchanged");
            expectEquals(peaksAfter.totalSamples, peaksBefore.totalSamples, "no re-analysis (sample count identical)");
            expectEquals(peaksAfter.samplesPerPeak, peaksBefore.samplesPerPeak, "peak resolution unchanged");
            expect(peaksAfter.minPeaks == peaksBefore.minPeaks, "min peaks byte-identical");
            expect(peaksAfter.maxPeaks == peaksBefore.maxPeaks, "max peaks byte-identical");
        }

        beginTest("ExplicitClipOverridePreserved");
        {
            // An explicit per-clip tint (ClipColourPickerCore) replaces the
            // body/header tint SOURCE hue; the WAVEFORM still follows the
            // track colour (single waveform authority untouched).
            ClipBodyRenderFixture f;
            const juce::Colour green(0xFF00C853);
            const juce::Colour pink(0xFFF50057);
            f.setTrackColour(green);
            f.model.colour = pink;
            f.model.explicitClipColourOverride = true;
            f.renderer->updateClipData(f.model);

            // Body-only pass captured with the override ALREADY active.
            const auto body = f.render();
            expect(f.primeWaveform(), "waveform peaks ready");

            const auto img = f.render();
            expect(ClipBodyRenderFixture::countBodyPixelsInFamily(body, img, pink.getHue()) > 50,
                   "explicit override drives the body tint hue");
            expect(ClipBodyRenderFixture::countWaveformPixelsInFamily(body, img, green.getHue()) > 25,
                   "waveform keeps the canonical track colour (override does not replace it)");
        }
    }
};

static ClipBodyColorTests clipBodyColorTests;

} // namespace

// ===========================================================================
// WaveformColorTests.cpp
// The live bug: waveforms kept the default violet render even when the Track
// colour was Yellow/Cyan/Green/Pink etc. Root cause (production): ClipRenderCore
// drew the waveform from the CLIP colour (m_model->colour — the engine clip
// default) instead of the canonical TRACK colour.
//
// Production wiring (already applied):
//   * ArrangementClipModel::trackColour — canonical mirror of Track::getColor
//   * ClipWaveformColourCore::resolveWaveformColour — the single waveform
//     colour authority (track hue family, brightness/alpha adjustments)
//   * ClipRenderCore::drawWaveform resolves from m_model->trackColour
//   * ArrangementView mirrorEngineClipIntoArrangement sets model.trackColour
//     from the engine track; syncTrackColourToClips re-mirrors on colour change
//
// Pixel methodology (robust to body/header fills):
//   ClipRenderCore paints the clip BODY with the clip colour
//   (drawBackground: bodyCol = m_model->colour.darker(0.55f).withAlpha(0.92f),
//   header = kNameH (16 px) in the solid clip colour). Those are legitimate
//   non-waveform pixels and MUST NOT be classified as waveform.
//   The classifier therefore renders the SAME fixture BEFORE the waveform
//   peaks exist (body-only pass) and AFTER priming (waveform pass), then
//   counts only pixels that CHANGED between the two passes — the actual
//   waveform columns — and classifies their hue family. The waveform band is
//   derived from the production geometry (drawWaveform: areaTop = kNameH).
//
// The OLD renderer (violet clip-colour waveform) fails the discriminators:
//   its waveform columns are violet-family, so violet count >= track count.
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipWaveformColourCore.h"
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

// Engine clip default colour (recorded/imported default) — the old renderer
// used a brighter variant of this for the waveform regardless of the track.
const juce::Colour kEngineClipDefaultViolet(0xFFB661F4);

struct WaveformRenderFixture
{
    ArrangementZoomCore zoom;
    ArrangementClipModel model;
    std::unique_ptr<ClipRenderCore> renderer;

    WaveformRenderFixture()
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

    // Prime the waveform cache with a deterministic synthetic buffer and wait
    // (bounded) for peak generation. Returns true when peaks are ready.
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

    void setTrackColour(const juce::Colour& trackColour)
    {
        model.trackColour = trackColour;
        renderer->updateClipData(model);
    }

    juce::Image render()
    {
        juce::Image img(juce::Image::ARGB, 320, 120, true);
        juce::Graphics g(img);
        renderer->paint(g);
        return img;
    }

    // Body-only pass: render while the waveform peaks are NOT yet primed, so
    // drawWaveform bails at isReady() and only the background/header/border
    // paint. Everything that differs between this image and the primed render
    // is a true waveform pixel.
    juce::Image renderBodyOnly()
    {
        return render();
    }

    // Count TRUE waveform pixels in the track hue family: pixels inside the
    // production waveform band (drawWaveform: areaTop = kNameH) that CHANGED
    // between the body-only pass and the primed pass, whose resulting hue is
    // within `tolerance` of familyHue (wrap-aware). Alpha/blend-tolerant.
    static int countWaveformPixelsInFamily(const juce::Image& body,
                                           const juce::Image& wf,
                                           float familyHue,
                                           float tolerance = 0.13f)
    {
        int count = 0;
        // Production waveform band: drawWaveform's areaTop = ClipRenderCore::kNameH
        // (static constexpr int kNameH = 16; private in the renderer).
        constexpr int kNameH = 16;
        const int yStart = kNameH;
        const int yEnd   = wf.getHeight() - 4;          // below the border
        for (int y = yStart; y < yEnd; ++y)
        {
            for (int x = 0; x < wf.getWidth(); ++x)
            {
                const auto pb = body.getPixelAt(x, y);
                const auto pw = wf.getPixelAt(x, y);
                if (pw.getAlpha() < 8)
                    continue;
                // Unchanged pixel = background/header/border, not waveform.
                const float hueDelta = std::fabs(pw.getHue() - pb.getHue());
                const float alphaDelta = std::fabs(pw.getAlpha() - pb.getAlpha());
                if (hueDelta < 0.02f && alphaDelta < 0.02f)
                    continue;
                const float d = std::fabs(pw.getHue() - familyHue);
                if (d < tolerance || (1.0f - d) < tolerance)
                    ++count;
            }
        }
        return count;
    }
};

class WaveformColorTests final : public juce::UnitTest
{
public:
    WaveformColorTests() : juce::UnitTest("WaveformColor", "APEX.WaveformColor") {}

    void runTest() override
    {
        beginTest("FollowsTrackColor");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly(); // before priming: no waveform
            expect(f.primeWaveform(), "waveform peaks ready");

            const juce::Colour yellow(0xFFFFD500);
            const juce::Colour cyan(0xFF00E5FF);
            f.setTrackColour(yellow);
            const auto yellowPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue());
            expect(yellowPixels > 25, "yellow track renders yellow-family waveform");

            f.setTrackColour(cyan);
            const auto cyanPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), cyan.getHue());
            const auto yellowPixelsAfter = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue());
            expect(cyanPixels > 25, "cyan track renders cyan-family waveform");
            expect(yellowPixelsAfter < yellowPixels / 2,
                   "waveform no longer yellow-family after the track colour changed");
        }

        beginTest("ManualTrackColorChangeUpdatesWaveform");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");

            const juce::Colour yellow(0xFFFFD500);
            const juce::Colour blue(0xFF087BFF);
            f.setTrackColour(yellow);
            expect(WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue()) > 25,
                   "yellow before the manual change");

            // Manual Track colour change → canonical propagation
            // (trackPropertyChanged → syncTrackColourToClips → mirror →
            // model.trackColour). The renderer must reflect it immediately.
            f.setTrackColour(blue);
            const auto bluePixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), blue.getHue());
            const auto yellowPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue());
            expect(bluePixels > 25, "waveform follows the manually-changed track colour");
            expect(yellowPixels < bluePixels / 2, "old hue family gone after the change");
        }

        beginTest("AllClipsOnTrackShareTrackColor");
        {
            // Two clips on the SAME track: identical waveform hue family even
            // though their clip colours differ.
            WaveformRenderFixture a;
            WaveformRenderFixture b;
            const auto bodyA = a.renderBodyOnly();
            const auto bodyB = b.renderBodyOnly();
            expect(a.primeWaveform() && b.primeWaveform(), "peaks ready for both clips");

            const juce::Colour green(0xFF00C853);
            a.setTrackColour(green);
            b.setTrackColour(green);
            b.model.colour = juce::Colour(0xFF222222); // different clip colour

            const auto aPixels = WaveformRenderFixture::countWaveformPixelsInFamily(bodyA, a.render(), green.getHue());
            const auto bPixels = WaveformRenderFixture::countWaveformPixelsInFamily(bodyB, b.render(), green.getHue());
            expect(aPixels > 25 && bPixels > 25, "both clips share the track hue family");
        }

        beginTest("MovingClipToAnotherTrackUpdatesColor");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");

            // Track A = Yellow, Track B = Cyan. The canonical model path for a
            // move sets trackColour to the destination track's colour (the
            // mirror does this when the clip's track changes).
            const juce::Colour yellow(0xFFFFD500);
            const juce::Colour cyan(0xFF00E5FF);
            f.setTrackColour(yellow);
            const auto before = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue());
            expect(before > 25, "waveform yellow-family on Track A");

            f.setTrackColour(cyan); // clip moved to Track B (canonical model update)
            const auto after = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), cyan.getHue());
            const auto yellowLeft = WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), yellow.getHue());
            expect(after > 25, "waveform adopts the destination track colour");
            expect(yellowLeft < before / 2, "track colour is not baked into the clip waveform");
        }

        beginTest("DoesNotUseDefaultPurpleWhenTrackHasColor");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");

            const juce::Colour yellow(0xFFFFD500);
            f.model.colour = kEngineClipDefaultViolet; // engine clip default (the old source)
            f.setTrackColour(yellow);

            const auto img = f.render();
            const auto yellowPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, yellow.getHue());
            const auto violetPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());

            // The OLD renderer (clip-colour brighter) fails this: its waveform
            // columns are violet-family, so violet >= yellow.
            expect(yellowPixels > 25, "waveform drawn in the track hue family");
            expect(violetPixels < yellowPixels,
                   "violet clip default must not leak into the waveform");
        }

        beginTest("ColorChangeDoesNotReanalyzeAudio");
        {
            WaveformRenderFixture f;
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

            // Same cache object, same peak data: colour changes are
            // presentation-only — no re-analysis, no peak recomputation.
            const auto& peaksAfter = cache.getPeaks();
            expect(peaksAfter.valid == peaksBefore.valid, "peak validity unchanged");
            expectEquals(peaksAfter.totalSamples, peaksBefore.totalSamples, "no re-analysis (sample count identical)");
            expectEquals(peaksAfter.samplesPerPeak, peaksBefore.samplesPerPeak, "peak resolution unchanged");
            expect(peaksAfter.minPeaks == peaksBefore.minPeaks, "min peaks byte-identical");
            expect(peaksAfter.maxPeaks == peaksBefore.maxPeaks, "max peaks byte-identical");
        }

        beginTest("ImportedClipUsesDestinationTrackColor");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");

            // Imported clips keep the engine default clip colour; the mirror
            // sets model.trackColour from the DESTINATION track.
            const juce::Colour green(0xFF00C853);
            f.model.colour = kEngineClipDefaultViolet;
            f.setTrackColour(green);

            const auto img = f.render();
            const auto greenPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, green.getHue());
            const auto violetPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());
            expect(greenPixels > 25, "imported clip waveform uses the destination track colour");
            expect(violetPixels < greenPixels, "import default violet does not leak");
        }

        beginTest("RecordedClipUsesTrackColor");
        {
            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");

            const juce::Colour pink(0xFFF50057);
            f.model.colour = kEngineClipDefaultViolet; // recorded-clip default
            f.setTrackColour(pink);

            const auto img = f.render();
            const auto pinkPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, pink.getHue());
            const auto violetPixels = WaveformRenderFixture::countWaveformPixelsInFamily(body, img, kEngineClipDefaultViolet.getHue());
            expect(pinkPixels > 25, "recorded clip waveform uses the track colour");
            expect(violetPixels < pinkPixels, "recorded violet default does not leak");
        }

        beginTest("QuickTrackAssignedColorPropagates");
        {
            // Quick Track Builder assigns canonical Track colours from the
            // project palette. The waveform authority consumes that exact
            // colour — no QuickTrack-specific waveform hack.
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
            expect(assigned != kEngineClipDefaultViolet, "QuickTrack assigns a real colour");

            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");
            f.setTrackColour(assigned);
            expect(WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), assigned.getHue()) > 25,
                   "waveform follows the QuickTrack-assigned track colour");
        }

        beginTest("ProjectReloadUsesPersistedTrackColor");
        {
            // A restored project brings the track colour back via
            // Track::setColor; the mirror then feeds it to the waveform.
            TrackManager tracks;
            tracks.createMasterTrack();
            auto* track = tracks.createTrack("Reloaded");
            expect(track != nullptr);

            const juce::Colour orange(0xFFFF6D00);
            track->setColor(orange);          // persisted value restored
            expectEquals(track->getColor().getARGB(), orange.getARGB(),
                         "restored track colour");

            WaveformRenderFixture f;
            const auto body = f.renderBodyOnly();
            expect(f.primeWaveform(), "waveform peaks ready");
            f.setTrackColour(track->getColor());
            expect(WaveformRenderFixture::countWaveformPixelsInFamily(body, f.render(), orange.getHue()) > 25,
                   "waveform uses the persisted track colour after reload");
        }

        beginTest("SelectionMutedVariantsStayInTrackHueFamily");
        {
            // The resolution authority must keep EVERY variant inside the
            // track's hue family — no hue shifts for selection/mute.
            const juce::Colour colours[] = {
                juce::Colour(0xFFFFD500),  // yellow
                juce::Colour(0xFF00E5FF),  // cyan
                juce::Colour(0xFF00C853),  // green
                juce::Colour(0xFFF50057),  // pink
                juce::Colour(0xFF087BFF),  // blue
                juce::Colour(0xFFFF6D00)   // orange
            };

            for (const auto& track : colours)
            {
                const auto normal = ClipWaveformColourCore::resolveWaveformColour(track, false, false);
                const auto muted  = ClipWaveformColourCore::resolveWaveformColour(track, true, false);
                const auto selected = ClipWaveformColourCore::resolveWaveformColour(track, false, true);
                expect(ClipWaveformColourCore::sameHueFamily(normal, track), "normal stays in hue family");
                expect(ClipWaveformColourCore::sameHueFamily(muted, track), "muted stays in hue family");
                expect(ClipWaveformColourCore::sameHueFamily(selected, track), "selected stays in hue family");
                // juce::Colour::getAlpha() returns 0-255 (integer-style float).
                expectEquals(juce::roundToInt(muted.getAlpha()), juce::roundToInt(0.45f * 255.0f),
                             "muted alpha 0.45");
                expectEquals(juce::roundToInt(selected.getAlpha()), juce::roundToInt(0.95f * 255.0f),
                             "selected alpha 0.95");
            }

            // Degenerate input: no valid track colour → canonical APEX blue.
            const auto fallback = ClipWaveformColourCore::resolveWaveformColour(juce::Colours::transparentBlack, false, false);
            expect(fallback.isOpaque() || fallback.getAlpha() > 0.99f, "fallback is opaque");
            expect(ClipWaveformColourCore::sameHueFamily(fallback, juce::Colour(0xFF087BFF)),
                   "fallback is APEX signal blue");
        }
    }
};

static WaveformColorTests waveformColorTests;

} // namespace

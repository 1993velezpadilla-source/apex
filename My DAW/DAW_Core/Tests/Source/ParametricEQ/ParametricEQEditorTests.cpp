#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQCore/ParametricEQSpectrumGrabCore.h"
#include "../../../Source/ParametricEQCore/ParametricEQMatchCore.h"
#include "../../../Source/ParametricEQUI/ParametricEQEditor.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{

using namespace APEX::ParametricEQ;

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

void processBlocks (Processor& processor, int blocks = 4)
{
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

struct SizeCase
{
    int width;
    int height;
    ParametricEQEditor::LayoutMode expected;
};

constexpr std::array<SizeCase, 8> kSizes {{
    { 320, 568, ParametricEQEditor::LayoutMode::CompactPhone },
    { 480, 800, ParametricEQEditor::LayoutMode::LargePhone },
    { 640, 400, ParametricEQEditor::LayoutMode::LargePhone },
    { 700, 500, ParametricEQEditor::LayoutMode::Tablet },
    { 768, 1024, ParametricEQEditor::LayoutMode::Tablet },
    { 900, 300, ParametricEQEditor::LayoutMode::LargePhone },
    { 1280, 720, ParametricEQEditor::LayoutMode::Desktop },
    { 1920, 1080, ParametricEQEditor::LayoutMode::LargeDesktop }
}};

} // namespace

class ParametricEQEditorTests final : public juce::UnitTest
{
public:
    ParametricEQEditorTests()
        : UnitTest ("ParametricEQ.Editor", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testResponsiveLayoutMatrix();
        testStaticEvoEyeStamp();
        testPurpleAnalyzerAndPresetRestore();
        testPrismOffscreenRenderAndSnapshots();
        testDoubleClickCreatesNativeBandAndWheelEditsQ();
        testNodeDragUpdatesParametersThroughRealHierarchy();
        testPlacementButtonsAreTouchAccessibleAndFunctional();
        testAuditionHoldGestureLifecycle();
        testLongPressCreatesBand();
        testSketchGestureCreatesBands();
        testSpectrumGrabAndTouchToBand();
        testNativeBandCopyPasteAcrossInstances();
        testMeasuredEqMatchAndGuidedNativeApply();
        testSpectralInspectorAndDeferredPdc();
        testPhaseSelectorDrivesCanonicalParameter();
    }

private:
    void testStaticEvoEyeStamp()
    {
        beginTest ("APEX native plugin stamp is static, text-free, noninteractive "
                   "and remains small across desktop/mobile sizes");
        APEX::UI::EvoEyeStamp eye;
        juce::Component window;
        for (const auto& dimensions : std::array<juce::Point<int>, 3> {{
                 {320, 568}, {900, 620}, {1920, 1080}
             }})
        {
            window.setSize (dimensions.x, dimensions.y);
            eye.placeInCorner (window);
            const auto bounds = eye.getBounds();
            expect (window.getLocalBounds().contains (bounds),
                    "decorative stamp must remain inside plugin");
            expect (bounds.getWidth() <= APEX::UI::EvoEyeStamp::kWidth
                    && bounds.getHeight() <= APEX::UI::EvoEyeStamp::kHeight,
                    "no huge boot-up logo inside plugins");
            expect (bounds.getWidth() >= 28 && bounds.getHeight() >= 16,
                    "tiny stamp stays legible");
            bool capturesSelf = true, capturesChildren = true;
            eye.getInterceptsMouseClicks (capturesSelf, capturesChildren);
            expect (! capturesSelf && ! capturesChildren,
                    "eye cannot steal a slider, knob or node gesture");
        }
        eye.setSize (APEX::UI::EvoEyeStamp::kWidth,
                     APEX::UI::EvoEyeStamp::kHeight);
        juce::Image image (juce::Image::ARGB, eye.getWidth(), eye.getHeight(), true);
        {
            juce::Graphics graphics (image);
            eye.paint (graphics);
        }
        int coloredPixels = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (image.getPixelAt (x, y).getAlpha() > 80)
                    ++coloredPixels;
        expect (coloredPixels > 80,
                "the eye, brows and pupil must actually render as JUCE paths");
        beginTest ("plugin brand stamp does not introduce state or frames");
        const int once = coloredPixels;
        {
            juce::Graphics graphics (image);
            eye.paint (graphics);
        }
        expectEquals (coloredPixels, once,
                      "static eye repaint preserves the same path footprint");
    }

    void testPurpleAnalyzerAndPresetRestore()
    {
        beginTest ("editor-on analyzer shows normal dBFS material full-height "
                   "and releases its worker when closed");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 512);
        expect (processor->getAnalyzerMode()
            == APEX::Analysis::SpectrumTapMode::Closed);
        {
            auto editor = std::make_unique<ParametricEQEditor> (*processor);
            editor->setSize (1280, 720);
            expect (processor->getAnalyzerMode()
                == APEX::Analysis::SpectrumTapMode::Both,
                "GUI opens the actual pre/post FFT without changing no-GUI default");
            const float upper = editor->analyzerYForTesting (-6.0f);
            const float mid = editor->analyzerYForTesting (-38.0f);
            const float lower = editor->analyzerYForTesting (-96.0f);
            expect (upper < mid && mid < lower);
            expect (lower - mid > editor->getGraphBoundsForTesting()
                          .getHeight() * 0.3f,
                    "ordinary -38 dBFS analyzer peaks are no longer squashed "
                    "onto the graph baseline");

            beginTest ("phone effect uses real hosted low/high cuts and preset "
                       "undo restores 24-band identity");
            enableBell (*processor, 7, 1880.0f, -3.5f, 2.6f);
            setUnits (*processor, placementParameterIndex (7), 3.0f);
            std::array<std::array<float, 7>, kMaxBands> before {};
            for (int band = 0; band < kMaxBands; ++band)
                for (int off = 0; off < 7; ++off)
                    before[static_cast<std::size_t> (band)]
                          [static_cast<std::size_t> (off)] =
                        processor->getParametricEQParameter (parameterIndex (
                            band, static_cast<BandParameterOffset> (off)))->getValue();
            editor->applyPresetForTesting (2);
            const auto enabled = [&] (int band)
            {
                return processor->getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Enabled))->getBool();
            };
            expect (enabled (0) && enabled (1) && enabled (2));
            expect (! enabled (7), "old bands are disabled not destructively removed");
            expectEquals (processor->getParametricEQParameter (parameterIndex (
                0, BandParameterOffset::Shape))->getChoiceIndex(),
                static_cast<int> (FilterShape::LowCut));
            expectEquals (processor->getParametricEQParameter (parameterIndex (
                1, BandParameterOffset::Shape))->getChoiceIndex(),
                static_cast<int> (FilterShape::HighCut));
            expectWithinAbsoluteError (
                processor->getParametricEQParameter (parameterIndex (
                    0, BandParameterOffset::Frequency))->getUnitsValue(),
                340.0f, 2.0f);
            editor->applyPresetForTesting (7);
            for (int band = 0; band < kMaxBands; ++band)
                for (int off = 0; off < 7; ++off)
                    expectWithinAbsoluteError (
                        processor->getParametricEQParameter (parameterIndex (
                            band, static_cast<BandParameterOffset> (off)))
                            ->getValue(),
                        before[static_cast<std::size_t> (band)]
                              [static_cast<std::size_t> (off)], 1.0e-6f,
                        "preset undo preserves stored hosted band IDs");
            expectWithinAbsoluteError (
                processor->getParametricEQParameter (
                    placementParameterIndex (7))->getUnitsValue(),
                3.0f, 0.01f, "undo restores original Mid placement");
        }
        expect (processor->getAnalyzerMode()
            == APEX::Analysis::SpectrumTapMode::Closed,
            "closing editor restores dormant analyzer unless changed by user");
    }

    void testResponsiveLayoutMatrix()
    {
        beginTest ("responsive layout at phone, tablet and desktop sizes");
        for (const auto& sizeCase : kSizes)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            enableBell (processor, 1, 5200.0f, -5.0f, 3.0f);
            processor.prepareToPlay (48000.0, 512);

            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (sizeCase.width, sizeCase.height);
            expectEquals (static_cast<int> (editor->getLayoutMode()),
                          static_cast<int> (sizeCase.expected));
            editor->setSelectedBandForTesting (0);

            const auto editorBounds = editor->getLocalBounds();
            const auto topBar = editor->getTopBarBoundsForTesting();
            const auto graph = editor->getGraphBoundsForTesting();
            const auto inspector = editor->getInspectorBoundsForTesting();
            // Premium graph has four visible 44px+ actions; remaining
            // clipboard/reference features live in the contextual menu.
            const std::array<juce::TextButton*, 4> tools {{
                &editor->getSketchButtonForTesting(),
                &editor->getGrabButtonForTesting(),
                &editor->getMatchButtonForTesting(),
                &editor->getToolsButtonForTesting()
            }};
            for (auto* hidden : { &editor->getCopyButtonForTesting(),
                                   &editor->getPasteButtonForTesting(),
                                   &editor->getMatchSaveButtonForTesting(),
                                   &editor->getMatchLoadButtonForTesting() })
            {
                expect (! hidden->isVisible(),
                    "contextual features must not crowd the EQ graph");
                expect (hidden->getWidth() >= 44
                    && hidden->getHeight() >= 44,
                    "hidden action handlers remain accessible and testable");
            }
            for (std::size_t tool = 0; tool < tools.size(); ++tool)
            {
                const auto b = tools[tool]->getBounds();
                expect (b.getWidth() >= 44 && b.getHeight() >= 44,
                        "graph actions have >=44px touch targets");
                expect (juce::Rectangle<int> (0, 0, graph.getWidth(),
                                                graph.getHeight()).contains (b),
                        "graph actions fit their own panel");
                if (tool > 0)
                    expect (! b.intersects (tools[tool - 1]->getBounds()),
                            "adjacent graph actions cannot overlap");
            }
            if (graph.getHeight() <= 200)
            {
                // Real 640x400 native screenshots previously hid the boosted
                // EQ handle behind EQ MATCH. Test the source of the problem,
                // not just whether the seven action buttons fit on screen.
                for (int band = 0; band < 2; ++band)
                {
                    const auto node = editor->getNodeBoundsForTesting (band);
                    for (const auto* tool : tools)
                        expect (! node.intersects (tool->getBounds()),
                                "short landscape EQ node is NOT concealed "
                                "behind any top-row graph action");
                }
            }
            expectEquals (editor->getMatchButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("MATCH")
                                      : juce::String ("EQ MATCH"),
                "compact EQ Match caption stays readable");
            expectEquals (editor->getMatchSaveButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("SAVE")
                                      : juce::String ("REF SAVE"),
                "compact reference save caption stays readable");
            expectEquals (editor->getMatchLoadButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("LOAD")
                                      : juce::String ("REF LOAD"),
                "compact reference load caption stays readable");

            const auto primary = editor->getPrimaryDialsForTesting();
            for (const auto* dial : primary)
            {
                expect (dial != nullptr, "every primary EQ dial exists");
                if (dial == nullptr)
                    continue;
                expect (dial->getWidth() >= 44,
                        "primary parameter keeps a safe 44px touch target");
                if (sizeCase.width >= 900 && sizeCase.height >= 500)
                {
                    expect (dial->getSliderStyle()
                            == juce::Slider::RotaryHorizontalVerticalDrag,
                            "floating HUD uses real interactive APEX rotary dial");
                    expect (dial->getHeight() >= 110,
                            "floating dial stays legible and touch safe");
                }
                else
                {
                    expect (dial->getSliderStyle()
                            == juce::Slider::LinearHorizontal,
                            "phone/tablet sheet retains horizontal touch sliders");
                    const bool shortSheet = sizeCase.height < 500;
                    expect (dial->getHeight() >= (shortSheet ? 88 : 96)
                            && dial->getWidth() >= 80,
                            "touch quick-edit strip keeps real FREQ/GAIN/Q "
                            "controls large enough for fingers and text");
                    expect (dial->getY() < 70,
                            "touch EQ essentials immediately visible before "
                            "secondary inspector controls");
                }
            }
            if (sizeCase.width < 900 || sizeCase.height < 500)
            {
                expectEquals (primary[0]->getY(), primary[1]->getY(),
                              "quick-edit band strip aligns FREQ and GAIN");
                expectEquals (primary[1]->getY(), primary[2]->getY(),
                              "quick-edit band strip aligns GAIN and Q");
                expect (! primary[0]->getBounds().intersects (
                            primary[1]->getBounds()),
                        "FREQ and GAIN hit targets cannot overlap");
                expect (! primary[1]->getBounds().intersects (
                            primary[2]->getBounds()),
                        "GAIN and Q hit targets cannot overlap");
                if (sizeCase.height < 500)
                    for (const auto* dial : primary)
                        expect (dial->getBottom() + 10 <= inspector.getHeight(),
                            "short landscape numeric text box needs at least "
                            "10px breathing room BEFORE the bottom command "
                            "dock (previous native PNG cropped Hz/dB/Q)");
            }

            auto inBounds = [] (const juce::Rectangle<int>& child,
                                const juce::Rectangle<int>& parent)
            {
                return child.getWidth() > 0 && child.getHeight() > 0
                    && parent.contains (child);
            };
            expect (inBounds (topBar, editorBounds), "bottom dock in bounds");
            expect (inBounds (graph, editorBounds), "graph in bounds");
            expect (inBounds (inspector, editorBounds), "inspector in bounds");
            expect (! graph.intersects (topBar), "graph/bottom dock overlap");
            expect (! inspector.intersects (topBar), "inspector/bottom dock overlap");
            expectEquals (topBar.getBottom(), editorBounds.getBottom(),
                          "Pro-Q-style bottom command dock sits at window foot");

            const bool touchSheet = sizeCase.width < 900
                || sizeCase.height < 500;
            if (touchSheet)
                expect (! graph.intersects (inspector),
                        "touch phone/tablet inspector stays BELOW graph in "
                        "both landscape and portrait orientations");
            else
            {
                expect (graph.contains (inspector),
                        "floating APEX band HUD stays INSIDE graph");
                expectEquals (graph.getWidth(), sizeCase.width,
                              "immersive desktop graph uses the entire width");
                const auto selectedNode = editor->getNodeBoundsForTesting (0)
                    .translated (graph.getX(), graph.getY());
                expect (! inspector.intersects (selectedNode),
                        "floating band HUD cannot cover selected node");
                const auto otherNode = editor->getNodeBoundsForTesting (1)
                    .translated (graph.getX(), graph.getY());
                expect (! inspector.intersects (otherNode),
                        "floating HUD must preserve access to other EQ nodes");
                editor->setSelectedBandForTesting (1);
                const auto movedInspector = editor->getInspectorBoundsForTesting();
                expect (! movedInspector.intersects (
                    editor->getNodeBoundsForTesting (0).translated (
                        graph.getX(), graph.getY())),
                    "switching bands repositions the HUD without hiding prior band");
            }
            expect (graph.getWidth() >= 0 && graph.getHeight() >= 0);
            expect (graph.getHeight() >= 140, "graph must remain usable");
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= editorBounds.getWidth() * editorBounds.getHeight(),
                    "graph must stay a dominant surface");
            if (touchSheet)
                expect (graph.getWidth() * graph.getHeight() * 100
                            >= editorBounds.getWidth()
                                 * editorBounds.getHeight() * 45,
                        "touch canvas must reserve 45% of screen for response "
                        "in both landscape and portrait");

            const auto& placements = editor->getPlacementButtonsForTesting();
            expectEquals (placements.size(), static_cast<std::size_t> (5));
            for (const auto& button : placements)
            {
                expect (button->isEnabled(), "placement enabled");
                const auto bounds = button->getBounds();
                expect (bounds.getWidth() >= 40 && bounds.getHeight() >= 40,
                        "placement touch target size");
                expect (bounds.getX() >= 0 && bounds.getY() >= 0,
                        "placement non-negative");
            }
            for (const auto& button : placements)
                expect (! button->isVisible(),
                    "five legacy channel buttons must not clutter the HUD");
            auto& channel = editor->getPlacementComboForTesting();
            expect (channel.isVisible(), "single channel dropdown is visible");
            expectEquals (channel.getNumItems(), 5,
                          "Stereo/Left/Right/Mid/Side all remain available");
            expectEquals (channel.getSelectedId(), 1,
                          "per-band channel defaults to Stereo");
            expect (channel.getWidth() >= 44 && channel.getHeight() >= 40,
                    "channel selector stays touch accessible");

            const auto audition = editor->getAuditionButtonForTesting().getBounds();
            expect (editor->getAuditionButtonForTesting().isEnabled(),
                    "audition enabled with a selected band");
            expect (audition.getHeight() >= 44, "audition touch target height");
            expect (audition.getWidth() >= 60, "audition touch target width");
            expect (audition.getX() >= 0 && audition.getY() >= 0,
                    "audition non-negative");

            editor->setSelectedBandForTesting (-1);
            expect (! editor->getAuditionButtonForTesting().isEnabled(),
                    "audition disabled without selection");
            for (const auto& button : placements)
                expect (! button->isEnabled(), "placement disabled without selection");
            expect (! channel.isEnabled(), "channel selector disabled without band");
        }
    }

    void testPrismOffscreenRenderAndSnapshots()
    {
        beginTest ("APEX Prism native vector GUI renders at phone, tablet and desktop sizes");
        const auto workspace = juce::SystemStats::getEnvironmentVariable (
            "GITHUB_WORKSPACE", {});
        for (const auto& sizeCase : kSizes)
        {
            auto holder = std::make_unique<Processor>();
            auto& processor = *holder;
            enableBell (processor, 0, 930.0f, 8.0f, 2.4f);
            enableBell (processor, 1, 5200.0f, -5.0f, 3.0f);
            processor.prepareToPlay (48000.0, 128);
            processBlocks (processor);
            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (sizeCase.width, sizeCase.height);
            editor->setSelectedBandForTesting (0);
            editor->refreshResponseForTesting();
            // The host plugin window is opaque: use an opaque RGB backing
            // store instead of an initially transparent ARGB screenshot.
            // Release the Graphics renderer BEFORE querying pixels/PNG so
            // the backend can finish any pending rasterisation.
            juce::Image image (juce::Image::RGB, sizeCase.width,
                               sizeCase.height, true);
            {
                juce::Graphics graphics (image);
                editor->paintEntireComponent (graphics, false);
            }
            const auto graphBounds = editor->getGraphBoundsForTesting();
            const auto headerPixel = image.getPixelAt (4, 4);
            const auto graphPixel = image.getPixelAt (
                graphBounds.getCentreX(), graphBounds.getCentreY());
            // A genuinely painted native GUI has distinct navy header
            // and dark blue response-canvas surfaces. Merely allocating an
            // RGB image cannot satisfy this: all-clear-black is caught.
            expect (headerPixel != graphPixel,
                    "Prism offscreen screenshot must contain multiple "
                    "rendered UI surfaces: "
                        + juce::String (sizeCase.width) + "x"
                        + juce::String (sizeCase.height)
                        + " header=" + headerPixel.toDisplayString (true)
                        + " graph=" + graphPixel.toDisplayString (true));
            const auto graph = editor->getGraphBoundsForTesting();
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= sizeCase.width * sizeCase.height,
                    "frequency graph remains dominant at every layout");

            // Export true software-rendered native JUCE screenshots from
            // Windows CI. The snapshots are QA previews, not AI mockups or
            // proof of audio quality. No files are written on user machines.
            // Save the ENTIRE responsive matrix to catch visual regressions
            // not observable from JUCE geometry assertions alone, including
            // 640x400 / 700x500 landscape and 900x300 short desktop.
            const bool snapshot = true;
            if (snapshot && workspace.isNotEmpty())
            {
                auto dir = juce::File (workspace).getChildFile (
                    "apex-eq-ui-snapshots");
                expect (dir.createDirectory().wasOk(),
                        "CI Prism snapshot folder must be writable");
                const auto file = dir.getChildFile (
                    "APEX-Prism-EQ-" + juce::String (sizeCase.width) + "x"
                    + juce::String (sizeCase.height) + ".png");
                auto stream = file.createOutputStream();
                expect (stream != nullptr, "CI PNG stream must open");
                if (stream != nullptr)
                {
                    juce::PNGImageFormat png;
                    expect (png.writeImageToStream (image, *stream),
                            "native Prism preview PNG must be valid");
                    stream->flush();
                }
            }
        }
    }

    void testDoubleClickCreatesNativeBandAndWheelEditsQ()
    {
        beginTest ("immersive graph double-click creates band and wheel edits Q");
        auto holder = std::make_unique<Processor>();
        auto& processor = *holder;
        processor.prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        const auto graph = editor->getGraphBoundsForTesting();
        const auto point = juce::Point<float> (
            static_cast<float> (graph.getWidth()) * 0.39f,
            static_cast<float> (graph.getHeight()) * 0.43f);
        const auto band = 0;
        auto* enabled = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Enabled));
        expect (! enabled->getBool(), "new processor begins with no active band");
        editor->injectGraphDoubleClickForTesting (point);
        expect (enabled->getBool(), "double-click creates a real hosted band");
        expectEquals (editor->getSelectedBandForTesting(), band);
        expect (graph.contains (editor->getInspectorBoundsForTesting()),
                "creation opens native floating inspector within canvas");
        const auto frequency = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Frequency))
                ->getUnitsValue();
        const auto expectedFrequency = 20.0 * std::pow (
            1200.0, 0.39);
        expectWithinAbsoluteError (
            static_cast<double> (frequency), expectedFrequency,
            expectedFrequency * 0.025, "created band's direct Hz location");

        auto* q = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Q));
        const float oldQ = q->getValue();
        const auto centre = editor->getNodeBoundsForTesting (band).getCentre();
        editor->injectGraphWheelForTesting (centre.toFloat(), 0.75f);
        expect (q->getValue() > oldQ,
                "hovering band wheel increases canonical hosted Q");
        expect (std::isfinite (q->getUnitsValue()),
                "wheel editing keeps Q finite");
    }

    void testNodeDragUpdatesParametersThroughRealHierarchy()
    {
        beginTest ("node drag updates frequency and gain with balanced gestures");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 0.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        processBlocks (processor);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        editor->setSelectedBandForTesting (0);
        const auto graphBounds = editor->getGraphBoundsForTesting();

        const auto start = editor->getNodeBoundsForTesting (0).getCentre();
        const double targetRatioX = 0.5;
        const double targetRatioY = 0.25;
        // Graph-local coordinates (the injected events target the graph).
        const auto target = juce::Point<float> (
            static_cast<float> (targetRatioX * graphBounds.getWidth()),
            static_cast<float> (targetRatioY * graphBounds.getHeight()));

        editor->injectGraphMouseDownForTesting (start.toFloat());
        editor->injectGraphMouseDragForTesting (target);
        editor->injectGraphMouseUpForTesting (target);
        // The draggable response nodes must stay interactable after their
        // parameter values move: the floating native HUD follows the band.
        const auto inspector = editor->getInspectorBoundsForTesting();
        const auto graphAfter = editor->getGraphBoundsForTesting();
        expect (graphAfter.contains (inspector),
                "moving a band keeps floating HUD inside graph");
        expect (! inspector.intersects (
                    editor->getNodeBoundsForTesting (0).translated (
                        graphAfter.getX(), graphAfter.getY())),
                "moving a band cannot strand its node underneath the HUD");

        const double logMin = std::log10 (20.0);
        const double logMax = std::log10 (24000.0);
        const double expectedFrequency = std::pow (10.0,
            logMin + targetRatioX * (logMax - logMin));
        const double expectedGain = 18.0 * (1.0 - 2.0 * targetRatioY);
        const double actualFrequency = static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))->getUnitsValue());
        const double actualGain = static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))->getUnitsValue());
        expectWithinAbsoluteError (actualFrequency, expectedFrequency,
                                   expectedFrequency * 0.05 + 1.0,
                                   "dragged frequency");
        expectWithinAbsoluteError (actualGain, expectedGain, 0.75,
                                   "dragged gain");
    }

    void testPlacementButtonsAreTouchAccessibleAndFunctional()
    {
        beginTest ("single Channel dropdown drives the stable placement parameter");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);
        const auto& placements = editor->getPlacementButtonsForTesting();

        // Real JUCE click command path: triggerClick posts the click command
        // message and the dispatch loop delivers it to the same
        // internalClickCallback a genuine pointer press uses.
        juce::MessageManager::getInstance();
        auto press = [] (juce::TextButton& button)
        {
            button.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };

        auto& channel = editor->getPlacementComboForTesting();
        expectEquals (channel.getSelectedId(), 1, "Stereo is the default");
        for (int choice = 0; choice < kChannelPlacementCount; ++choice)
        {
            channel.setSelectedId (choice + 1, juce::sendNotificationSync);
            expectEquals (processor.getParametricEQParameter (
                              placementParameterIndex (0))->getChoiceIndex(),
                          choice, "dropdown changes canonical hosted parameter");
            expectEquals (channel.getText(), juce::String (channelPlacementName (
                              static_cast<ChannelPlacement> (choice))),
                          "dropdown displays active placement");
        }
        // Old API is preserved for backwards-compatible programmatic use,
        // but its five buttons never appear in the on-screen inspector.
        for (const auto& button : placements)
            expect (! button->isVisible(), "no duplicate channel buttons");
        press (*placements[0]);
        expectEquals (processor.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 0);
        editor->setSelectedBandForTesting (0);
        expectEquals (channel.getSelectedId(), 1);
    }

    void testAuditionHoldGestureLifecycle()
    {
        beginTest ("audition hold gesture begins, ends and survives editor teardown");
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);

            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (900, 620);
            editor->setSelectedBandForTesting (0);
            editor->beginAuditionGesture();
            expect (editor->isAuditionArmedForTesting());
            processBlocks (processor, 2);
            expect (processor.isAuditionHoldingForTesting(),
                    "processor must hold audition after press");
            editor->endAuditionGesture();
            expect (! editor->isAuditionArmedForTesting());
            processBlocks (processor, 8);
            expect (! processor.isAuditionHoldingForTesting(),
                    "processor must release after the matching end");
        }

        // Editor teardown must never leave audition latched.
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        {
            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (900, 620);
            editor->setSelectedBandForTesting (0);
            editor->beginAuditionGesture();
            processBlocks (processor, 2);
            expect (processor.isAuditionHoldingForTesting());
        }
        processBlocks (processor, 8);
        expect (! processor.isAuditionHoldingForTesting(),
                "editor destruction must cancel audition");
    }

    void testLongPressCreatesBand()
    {
        beginTest ("long-press on empty graph creates a band");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        for (int band = 0; band < kMaxBands; ++band)
            expect (! processor.getParametricEQParameter (
                        parameterIndex (band, BandParameterOffset::Enabled))->getBool());

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        const auto graphBounds = editor->getGraphBoundsForTesting();

        juce::MessageManager::getInstance();
        // Graph-local coordinates for the injected events.
        const auto position = juce::Point<float> (
            static_cast<float> (graphBounds.getWidth() * 0.4),
            static_cast<float> (graphBounds.getHeight() * 0.5));
        editor->injectGraphMouseDownForTesting (position);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
        editor->injectGraphMouseUpForTesting (position);

        int created = -1;
        for (int band = 0; band < kMaxBands; ++band)
            if (processor.getParametricEQParameter (
                    parameterIndex (band, BandParameterOffset::Enabled))->getBool())
            {
                created = band;
                break;
            }
        expect (created >= 0, "long-press must enable a band");
        expectEquals (editor->getSelectedBandForTesting(), created,
                      "created band becomes the selection");
    }

    void testSketchGestureCreatesBands()
    {
        beginTest ("EQ Sketch converts a freehand gesture into native bands");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        auto& sketch = editor->getSketchButtonForTesting();
        expect (sketch.getBounds().getHeight() >= 44,
                "Sketch must retain a touch-safe target");

        juce::MessageManager::getInstance();
        sketch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("SKETCH ON"), sketch.getButtonText());

        const auto graph = editor->getGraphBoundsForTesting();
        const auto pointFor = [&graph] (double xRatio, double gainDb)
        {
            const auto yRatio = 0.5 * (1.0 - gainDb / 18.0);
            return juce::Point<float> (
                static_cast<float> (xRatio * graph.getWidth()),
                static_cast<float> (yRatio * graph.getHeight()));
        };

        editor->injectGraphMouseDownForTesting (pointFor (0.20, 0.0));
        for (int step = 1; step <= 12; ++step)
        {
            const double x = 0.20 + 0.05 * step;
            const double dx = (x - 0.50) / 0.09;
            const double gain = 8.0 * std::exp (-0.5 * dx * dx);
            editor->injectGraphMouseDragForTesting (pointFor (x, gain));
        }
        editor->injectGraphMouseUpForTesting (pointFor (0.80, 0.0));

        int enabled = 0;
        for (int band = 0; band < kMaxBands; ++band)
            enabled += processor.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled))->getBool()
                     ? 1 : 0;

        expect (enabled >= 1, "Sketch should publish at least one band");
        expectEquals (juce::String ("SKETCH"), sketch.getButtonText(),
                      "Sketch returns to one-shot idle mode after commit");
        expect (editor->getSelectedBandForTesting() >= 0,
                "last created Sketch band becomes selected");
    }


    void testSpectrumGrabAndTouchToBand()
    {
        beginTest ("Spectrum Grab detects true resonances, rejects flat/NaN, "
                   "and inserts a native EQ band via a 44px touch marker");
        constexpr int bins = APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        constexpr double binHz = 48000.0
            / APEX::Analysis::SpectrumAnalyzerCore::kFftSize;
        std::array<float, bins> spectrum {};
        spectrum.fill (-40.0f);
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, binHz).count, 0,
            "a flat loud spectrum has no narrow resonances");
        spectrum.fill (-120.0f);
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, binHz).count, 0,
            "silence must not create candidates");
        expectEquals (SpectrumGrab::analyze (
            nullptr, bins, binHz).count, 0, "null frame");
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, std::numeric_limits<double>::quiet_NaN())
            .count, 0, "nonfinite bin frequency");

        for (int bin = 0; bin < bins; ++bin)
        {
            const double f = std::max (1.0, bin * binHz);
            const double low = std::log2 (f / 1500.0) / 0.025;
            const double high = std::log2 (f / 5400.0) / 0.030;
            spectrum[static_cast<std::size_t> (bin)] = static_cast<float> (
                -92.0 + 75.0 * std::exp (-0.5 * low * low)
                      + 60.0 * std::exp (-0.5 * high * high));
        }
        const auto plan = SpectrumGrab::analyze (
            spectrum.data(), bins, binHz);
        expect (plan.count >= 2, "both resonances should be detected");
        bool foundLow = false, foundHigh = false;
        for (int i = 0; i < plan.count; ++i)
        {
            const auto& peak = plan.peaks[static_cast<std::size_t> (i)];
            expect (std::isfinite (peak.frequencyHz)
                    && std::isfinite (peak.suggestedQ));
            expect (peak.suggestedQ >= 0.7 && peak.suggestedQ <= 20.0);
            foundLow |= std::abs (peak.frequencyHz - 1500.0) < 50.0;
            foundHigh |= std::abs (peak.frequencyHz - 5400.0) < 80.0;
        }
        expect (foundLow && foundHigh, "resonance center frequencies");

        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        auto& grab = editor->getGrabButtonForTesting();
        expect (grab.getBounds().getHeight()
                >= ParametricEQEditor::kTouchTarget);
        expect (! grab.getBounds().intersects (
            editor->getSketchButtonForTesting().getBounds()),
            "touch buttons do not overlap");
        editor->setGrabSpectrumForTesting (spectrum.data(), bins, binHz);
        expectEquals (juce::String ("GRAB ON"), grab.getButtonText());
        const auto marker = editor->getGrabPeakBoundsForTesting (0);
        expect (marker.getWidth() >= ParametricEQEditor::kTouchTarget);
        editor->injectGraphMouseDownForTesting (marker.getCentre().toFloat());
        editor->injectGraphMouseUpForTesting (marker.getCentre().toFloat());
        const auto* enabled = processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled));
        expect (enabled->getBool(), "tapping Grab marker enables a native band");
        expectWithinAbsoluteError (static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                    ->getUnitsValue()), plan.peaks[0].frequencyHz,
            40.0, "grab frequency is published to the stable parameter");
        expectWithinAbsoluteError (static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                    ->getUnitsValue()), -3.0, 0.1,
            "initial resonance cut starts at -3 dB");
        expectEquals (juce::String ("GRAB"), grab.getButtonText(),
                      "one-shot preview automatically disarms");
    }


    void testNativeBandCopyPasteAcrossInstances()
    {
        beginTest ("APEX copy/paste transfers full native band to another EQ "
                   "instance without mutating the source or parameter ABI");
        auto sourceStorage = std::make_unique<Processor>();
        auto destinationStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        auto& destination = *destinationStorage;
        enableBell (source, 0, 2400.0f, -6.5f, 3.2f);
        setUnits (source, placementParameterIndex (0), 4.0f);
        setUnits (source, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (source, dynamicParameterIndex (
            0, DynamicBandOffset::Range), -4.0f);
        setUnits (source, dynamicFilterParameterIndex (0), 1.0f);
        setUnits (source, spectralBandParameterIndex (0), 1.0f);
        source.prepareToPlay (48000.0, 256);
        destination.prepareToPlay (48000.0, 256);
        auto sourceEditor = std::make_unique<ParametricEQEditor> (source);
        auto targetEditor = std::make_unique<ParametricEQEditor> (destination);
        sourceEditor->setSize (320, 568);
        targetEditor->setSize (320, 568);
        sourceEditor->setSelectedBandForTesting (0);

        auto& copy = sourceEditor->getCopyButtonForTesting();
        auto& paste = targetEditor->getPasteButtonForTesting();
        const auto sourceGraph = sourceEditor->getGraphBoundsForTesting();
        const auto targetGraph = targetEditor->getGraphBoundsForTesting();
        expect (copy.getBounds().getHeight() >= 44
                && paste.getBounds().getHeight() >= 44,
                "clipboard controls are touch-safe");
        expect (copy.getRight() <= sourceGraph.getWidth()
                && paste.getRight() <= targetGraph.getWidth(),
                "clipboard controls fit on narrow phones");

        juce::MessageManager::getInstance();
        copy.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        paste.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (destination.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled))->getBool(),
            "paste enabled a new band in another native EQ instance");
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                ->getUnitsValue()), 2400.0, 1.0);
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                ->getUnitsValue()), -6.5, 0.05);
        expectEquals (destination.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 4);
        expect (destination.getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool());
        expect (destination.getParametricEQParameter (
            dynamicFilterParameterIndex (0))->getBool());
        expect (destination.getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool(),
            "native clipboard must include the appended per-band spectral mode");
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Range))
                ->getUnitsValue()), -4.0, 0.1);
        expectEquals (targetEditor->getSelectedBandForTesting(), 0);
        // Source and destination are independent native processor instances.
        expectWithinAbsoluteError (static_cast<double> (
            source.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                ->getUnitsValue()), -6.5, 0.05);
    }

    void testMeasuredEqMatchAndGuidedNativeApply()
    {
        beginTest ("APEX Match measured PRE reference versus input, level "
                   "invariance, safety guards and guided hosted band insert");
        constexpr int bins = APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        constexpr double binHz = 48000.0
            / APEX::Analysis::SpectrumAnalyzerCore::kFftSize;
        std::array<float, bins> input {}, reference {}, silent {};
        input.fill (-44.0f);
        silent.fill (-120.0f);
        for (int bin = 0; bin < bins; ++bin)
        {
            const double hz = std::max (1.0, bin * binHz);
            const double normalized = std::log2 (hz / 1400.0) / 0.20;
            reference[static_cast<std::size_t> (bin)] = static_cast<float> (
                -44.0 + 8.5 * std::exp (-0.5 * normalized * normalized));
        }

        MatchCapture referenceCapture, inputCapture;
        expect (! referenceCapture.append (nullptr, bins, binHz),
                "null frame is rejected");
        expect (! referenceCapture.append (silent.data(), bins, binHz),
                "silence must never count as a reference frame");
        auto corrupt = reference;
        corrupt.fill (std::numeric_limits<float>::quiet_NaN());
        expect (! referenceCapture.append (corrupt.data(), bins, binHz),
                "NaN frames are rejected atomically");
        expectEquals (referenceCapture.frameCount(), 0,
                      "invalid frames did not advance capture");

        for (int frame = 0; frame < MatchCapture::kRequiredFrames; ++frame)
        {
            expect (referenceCapture.append (reference.data(), bins, binHz),
                    "reference capture must accept finite live frames");
            expect (inputCapture.append (input.data(), bins, binHz),
                    "input capture must accept finite live frames");
        }
        expect (referenceCapture.ready() && inputCapture.ready());
        expect (! inputCapture.append (input.data(), bins, binHz),
                "bounded capture stops at its fixed frame budget");
        const auto fitted = MatchEngine::fit (referenceCapture, inputCapture);
        expect (fitted.count >= 1,
                "reference 1.4 kHz peak must yield an APEX fit proposal");
        bool located = false;
        for (int i = 0; i < fitted.count; ++i)
        {
            const auto& suggestion = fitted.bands[
                static_cast<std::size_t> (i)];
            located |= std::abs (suggestion.frequencyHz - 1400.0) < 250.0;
            expect (std::isfinite (suggestion.frequencyHz)
                && std::isfinite (suggestion.gainDb)
                && std::isfinite (suggestion.q),
                "match suggestions must be finite");
            expect (std::abs (suggestion.gainDb) <= 9.001,
                    "matching must remain bounded to +/-9 dB");
        }
        expect (located, "native fitter found the deliberately colored range");

        MatchCapture louder;
        auto boosted = input;
        boosted.fill (-29.0f);  // 15 dB whole-signal level difference
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            expect (louder.append (boosted.data(), bins, binHz));
        expectEquals (MatchEngine::fit (louder, inputCapture).count, 0,
            "level-only difference is not a tonal mismatch");
        expectEquals (MatchEngine::fit (referenceCapture, MatchCapture {})
            .count, 0, "partial/empty input must not create bands");

        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        // Reuse a disabled slot with stale spectral/dynamic values: EQ Match
        // must create a plain fresh band, not re-enable hidden old effects.
        setUnits (processor, spectralBandParameterIndex (0), 1.0f);
        setUnits (processor, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, parameterIndex (
            0, BandParameterOffset::Bypass), 1.0f);
        setUnits (processor, placementParameterIndex (0), 4.0f);
        processor.prepareToPlay (48000.0, 512);
        processor.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Pre);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (320, 568);
        auto& match = editor->getMatchButtonForTesting();
        expect (match.getBounds().getHeight() >= 44,
                "EQ MATCH touch button fits compact phone");
        expect (match.getBounds().getRight()
                <= editor->getGraphBoundsForTesting().getWidth());
        juce::MessageManager::getInstance();
        auto click = [&match]()
        {
            match.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };
        click();
        expectEquals (juce::String ("REF..."), match.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            editor->feedMatchSpectrumForTesting (
                reference.data(), bins, binHz);
        expectEquals (juce::String ("GET INPUT"), match.getButtonText());

        // A second EQ instance can explicitly adopt the measured reference
        // without borrowing the first processor's lifetime or analyzer frame.
        auto& save = editor->getMatchSaveButtonForTesting();
        auto& load = editor->getMatchLoadButtonForTesting();
        expect (save.getBounds().getHeight() >= 44
            && load.getBounds().getHeight() >= 44);
        expect (! save.isVisible() && ! load.isVisible(),
            "reference actions remain reachable via TOOLS without adding "
            "overlapping visible graph buttons");
        save.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("SAVED"), save.getButtonText());
        auto secondProcessorStorage = std::make_unique<Processor>();
        auto& secondProcessor = *secondProcessorStorage;
        secondProcessor.prepareToPlay (48000.0, 512);
        secondProcessor.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Pre);
        auto secondEditor = std::make_unique<ParametricEQEditor> (
            secondProcessor);
        secondEditor->setSize (320, 568);
        secondEditor->getMatchLoadButtonForTesting().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("GET INPUT"),
            secondEditor->getMatchButtonForTesting().getButtonText(),
            "a second EQ instance loaded the independently owned reference");

        click();
        expectEquals (juce::String ("INPUT..."), match.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            editor->feedMatchSpectrumForTesting (input.data(), bins, binHz);
        expectEquals (juce::String ("APPLY EQ"), match.getButtonText());
        click();
        int inserted = 0;
        for (int band = 0; band < kMaxBands; ++band)
            inserted += processor.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled))->getBool()
                ? 1 : 0;
        expect (inserted >= 1, "Apply inserts real APEX EQ bands");
        expect (! processor.getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool(),
            "newly created Match band cannot inherit stale spectral enable");
        expect (! processor.getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
            "newly created Match band cannot inherit hidden old dynamics");
        expect (! processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Bypass))->getBool(),
            "newly created Match band must actually affect output");
        expectEquals (processor.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 0,
            "newly created Match band defaults to true stereo");
        expectEquals (juce::String ("MATCH"), match.getButtonText(),
                      "matching resets to compact-width button text after apply");

        // The second instance uses the shared reference against its own
        // input, applying a *new* band without changing the source instance.
        auto& secondMatch = secondEditor->getMatchButtonForTesting();
        secondMatch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("INPUT..."), secondMatch.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            secondEditor->feedMatchSpectrumForTesting (
                input.data(), bins, binHz);
        expectEquals (juce::String ("APPLY EQ"), secondMatch.getButtonText());
        secondMatch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (secondProcessor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled))->getBool(),
            "second plugin independently fits and applies shared reference");
    }

    void testSpectralInspectorAndDeferredPdc()
    {
        beginTest ("native spectral inspector preserves touch dimensions "
                   "and advertises transport restart for PDC");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (*processor);
        editor->setSize (320, 568);
        editor->setSelectedBandForTesting (0);
        auto& arm = editor->getSpectralArmButtonForTesting();
        auto& band = editor->getSpectralBandButtonForTesting();

        expect (arm.getBounds().getHeight() >= 44);
        expect (band.getBounds().getHeight() >= 44);
        expect (! arm.getBounds().intersects (band.getBounds()));
        expect (arm.isEnabled());
        expect (band.isEnabled());
        expect (! processor->getParametricEQParameter (
            kSpectralArmParameter)->getBool());

        arm.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (processor->getParametricEQParameter (
            kSpectralArmParameter)->getBool());
        expect (arm.getButtonText().containsIgnoreCase ("RESTART"),
                "user must not be misled that host PDC switches mid-recording");
        expect (processor->isSpectralRepreparePendingForTesting());
        expectEquals (processor->getLatencySamples(), 0);

        band.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (processor->getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool());
        processor->prepareToPlay (48000.0, 128);
        editor->setSelectedBandForTesting (0);
        expectEquals (processor->getLatencySamples(), 1024);
        expect (! processor->isSpectralRepreparePendingForTesting());
        expect (arm.getButtonText().containsIgnoreCase ("ON"),
                "inspector reflects actual prepared spectral path");

        for (int index = 0; index < kNumParameters; ++index)
            expect (! Processor::getParameterId (index).isEmpty(),
                    "spectral extension must retain stable hosted identity");
    }

    void testPhaseSelectorDrivesCanonicalParameter()
    {
        beginTest ("phase selector drives peq.phase and reflects external state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        auto& phaseButton = editor->getPhaseButtonForTesting();
        auto* phaseParameter = processor.getParametricEQParameter (
            kPhaseModeParameter);

        expectEquals (juce::String ("Minimum"), phaseButton.getButtonText());
        expect (! phaseParameter->getBool());

        auto press = [] (juce::TextButton& button)
        {
            button.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };

        // Minimum -> Linear through the real click command path.
        press (phaseButton);
        expect (phaseParameter->getBool(),
                "click switches peq.phase to linear");
        expectEquals (juce::String ("Linear"), phaseButton.getButtonText());

        // Linear -> Minimum.
        press (phaseButton);
        expect (! phaseParameter->getBool(),
                 "second click returns peq.phase to minimum");
        expectEquals (juce::String ("Minimum"), phaseButton.getButtonText());

        // External host/state changes must reach the top bar through the
        // low-rate synchronization timer, not only through button clicks.
        phaseParameter->setValueNotifyingHost (1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        expectEquals (juce::String ("Linear"), phaseButton.getButtonText(),
                      "external parameter change is reflected by the GUI");

        // Editor reopen must synchronize with the canonical parameter.
        editor.reset();
        auto reopened = std::make_unique<ParametricEQEditor> (processor);
        reopened->setSize (900, 620);
        expectEquals (juce::String ("Linear"),
                      reopened->getPhaseButtonForTesting().getButtonText(),
                      "editor reopen reflects the canonical parameter");

        // State restore into a fresh processor must carry peq.phase.
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (state.getData(),
                                      static_cast<int> (state.getSize()));
        expect (restored.getParametricEQParameter (kPhaseModeParameter)->getBool(),
                "peq.phase survives a state round-trip");
        auto restoredEditor = std::make_unique<ParametricEQEditor> (restored);
        restoredEditor->setSize (900, 620);
        expectEquals (juce::String ("Linear"),
                      restoredEditor->getPhaseButtonForTesting().getButtonText(),
                      "restored editor shows the restored phase mode");

        // The hosted Phase control lives in Utility instead of adding a
        // sixth permanent footer button; hidden click path is still tested.
        for (const auto& sizeCase : kSizes)
        {
            restoredEditor->setSize (sizeCase.width, sizeCase.height);
            const auto topBar = restoredEditor->getTopBarBoundsForTesting();
            // Phase button bounds are LOCAL to its bottom-dock parent;
            // translate into editor coordinates before containment. The
            // pre-V2 test only worked because the bar started at (0, 0).
            const auto bounds = restoredEditor->getPhaseButtonForTesting()
                .getBounds().translated (topBar.getX(), topBar.getY());
            expect (topBar.contains (bounds),
                    "phase button stays inside its bottom command dock");
            expect (bounds.getWidth() >= 44, "phase touch target width");
            expect (bounds.getHeight() >= 32, "phase touch target height");
        }
    }
};

static ParametricEQEditorTests parametricEQEditorTests;

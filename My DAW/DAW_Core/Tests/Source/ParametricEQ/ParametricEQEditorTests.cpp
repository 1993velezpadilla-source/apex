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

constexpr std::array<SizeCase, 5> kSizes {{
    { 320, 568, ParametricEQEditor::LayoutMode::CompactPhone },
    { 480, 800, ParametricEQEditor::LayoutMode::LargePhone },
    { 768, 1024, ParametricEQEditor::LayoutMode::Tablet },
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
        testPrismOffscreenRenderAndSnapshots();
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
    void testResponsiveLayoutMatrix()
    {
        beginTest ("responsive layout at phone, tablet and desktop sizes");
        for (const auto& sizeCase : kSizes)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
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

            const bool portrait = sizeCase.width < 900
                && sizeCase.height >= static_cast<int> (sizeCase.width * 1.05);
            if (portrait)
                expect (! graph.intersects (inspector),
                        "touch portrait inspector stays BELOW graph");
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
            }
            expect (graph.getWidth() >= 0 && graph.getHeight() >= 0);
            expect (graph.getHeight() >= 140, "graph must remain usable");
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= editorBounds.getWidth() * editorBounds.getHeight(),
                    "graph must stay a dominant surface");
            if (sizeCase.width < 900 && sizeCase.height
                                              >= sizeCase.width * 1.05)
                expect (graph.getWidth() * graph.getHeight() * 100
                            >= editorBounds.getWidth()
                                 * editorBounds.getHeight() * 45,
                        "Prism portrait canvas must reserve 45% of total "
                        "display for the actionable EQ response");

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
            for (std::size_t first = 0; first < placements.size(); ++first)
                for (std::size_t second = first + 1; second < placements.size(); ++second)
                    expect (! placements[first]->getBounds().intersects (
                                placements[second]->getBounds()),
                            "placement buttons must not overlap");

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
            const bool snapshot = sizeCase.width == 320
                                  || sizeCase.width == 1280;
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
        beginTest ("placement segmented control switches the stable parameter");
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

        for (int choice = 0; choice < kChannelPlacementCount; ++choice)
        {
            press (*placements[static_cast<std::size_t> (choice)]);
            expectEquals (processor.getParametricEQParameter (
                              placementParameterIndex (0))->getChoiceIndex(),
                          choice);
            expectEquals (juce::String (channelPlacementName (
                              static_cast<ChannelPlacement> (choice))),
                          placements[static_cast<std::size_t> (choice)]->getButtonText());
        }
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
        expect (! match.getBounds().intersects (save.getBounds()));
        expect (! save.getBounds().intersects (load.getBounds()));
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
        expectEquals (juce::String ("EQ MATCH"), match.getButtonText(),
                      "matching resets after a completed apply");

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

        // The control must remain visible, inside the top bar, and touch-safe
        // at every responsive breakpoint.
        for (const auto& sizeCase : kSizes)
        {
            restoredEditor->setSize (sizeCase.width, sizeCase.height);
            const auto topBar = restoredEditor->getTopBarBoundsForTesting();
            const auto bounds
                = restoredEditor->getPhaseButtonForTesting().getBounds();
            expect (topBar.contains (bounds),
                    "phase button stays inside the top bar");
            expect (bounds.getWidth() >= 44, "phase touch target width");
            expect (bounds.getHeight() >= 32, "phase touch target height");
        }
    }
};

static ParametricEQEditorTests parametricEQEditorTests;

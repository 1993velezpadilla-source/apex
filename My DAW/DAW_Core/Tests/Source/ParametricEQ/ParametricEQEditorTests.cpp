#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQUI/ParametricEQEditor.h"

#include <array>
#include <cmath>

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
        testNodeDragUpdatesParametersThroughRealHierarchy();
        testPlacementButtonsAreTouchAccessibleAndFunctional();
        testAuditionHoldGestureLifecycle();
        testLongPressCreatesBand();
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
            expect (inBounds (topBar, editorBounds), "top bar in bounds");
            expect (inBounds (graph, editorBounds), "graph in bounds");
            expect (inBounds (inspector, editorBounds), "inspector in bounds");
            expect (! graph.intersects (inspector), "graph/inspector overlap");
            expect (! graph.intersects (topBar), "graph/top bar overlap");
            expect (! inspector.intersects (topBar), "inspector/top bar overlap");
            expect (graph.getWidth() >= 0 && graph.getHeight() >= 0);
            expect (graph.getHeight() >= 140, "graph must remain usable");
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= editorBounds.getWidth() * editorBounds.getHeight(),
                    "graph must stay a dominant surface");

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

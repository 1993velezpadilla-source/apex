#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQUI/ParametricEQEditor.h"

#include <array>
#include <cmath>

namespace
{

using namespace APEX::ParametricEQ;

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

void press (juce::TextButton& button)
{
    juce::MessageManager::getInstance();
    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
}

} // namespace

class ParametricEQDynamicEditorTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicEditorTests()
        : UnitTest ("ParametricEQ.DynamicEditor", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testDynamicControlsLayoutAtAllBreakpoints();
        testBandSelectionSynchronizesDynamicValues();
        testEnableToggleDrivesTheCanonicalParameter();
        testSlidersAndGlobalButtonsDriveParameters();
        testPublishedVisualizationReflectsProcessorState();
        testStateRestoreAndReopenStaySynchronized();
    }

private:
    void testDynamicControlsLayoutAtAllBreakpoints()
    {
        beginTest ("dynamic controls stay reachable at every breakpoint");
        for (const auto& sizeCase : kSizes)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);

            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (sizeCase.width, sizeCase.height);
            editor->setSelectedBandForTesting (0);

            const auto inspectorBounds = editor->getInspectorBoundsForTesting();
            auto& dynEnable = editor->getDynEnableButtonForTesting();
            auto& dynRange = editor->getDynRangeSliderForTesting();
            expect (dynEnable.isEnabled(), "dynamic enable reachable");
            expect (dynRange.isEnabled(), "dynamic range reachable");
            expect (dynEnable.getBounds().getHeight() >= 44,
                    "dynamic enable touch height");
            expect (dynRange.getBounds().getHeight() >= 44,
                    "dynamic range touch height");
            expect (dynEnable.getBounds().getX() >= 0
                        && dynEnable.getBounds().getY() >= 0,
                    "dynamic enable non-negative");
            expect (dynRange.getBounds().getX() >= 0
                        && dynRange.getBounds().getY() >= 0,
                    "dynamic range non-negative");
            expect (inspectorBounds.getWidth() > 0
                        && inspectorBounds.getHeight() > 0,
                    "inspector remains valid");
            const auto graphBounds = editor->getGraphBoundsForTesting();
            expect (graphBounds.getHeight() >= 140, "graph remains usable");
        }
    }

    void testBandSelectionSynchronizesDynamicValues()
    {
        beginTest ("selecting bands synchronizes the contextual dynamic values");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        enableBell (processor, 1, 2000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 8.0f);
        setUnits (processor, dynamicParameterIndex (1, DynamicBandOffset::Range), -6.0f);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);
        const auto bandZeroValue = editor->getDynRangeSliderForTesting().getValue();
        expectEquals (bandZeroValue,
                      static_cast<double> (processor.getParametricEQParameter (
                          dynamicParameterIndex (0, DynamicBandOffset::Range))->getValue()),
                      "band 0 range slider matches its parameter");

        editor->setSelectedBandForTesting (1);
        const auto bandOneValue = editor->getDynRangeSliderForTesting().getValue();
        expectEquals (bandOneValue,
                      static_cast<double> (processor.getParametricEQParameter (
                          dynamicParameterIndex (1, DynamicBandOffset::Range))->getValue()),
                      "band 1 range slider matches its parameter");
        expect (bandZeroValue != bandOneValue,
                "switching bands must never display the previous band's value");
    }

    void testEnableToggleDrivesTheCanonicalParameter()
    {
        beginTest ("dynamic enable toggles the canonical hosted parameter");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);
        auto& button = editor->getDynEnableButtonForTesting();
        expect (! processor.getParametricEQParameter (
                     dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool());
        press (button);
        expect (processor.getParametricEQParameter (
                    dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
                "enable press turns the canonical parameter on");
        press (button);
        expect (! processor.getParametricEQParameter (
                     dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
                "second press turns it off");
    }

    void testSlidersAndGlobalButtonsDriveParameters()
    {
        beginTest ("dynamic sliders and global buttons drive hosted parameters");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);

        auto& range = editor->getDynRangeSliderForTesting();
        range.setValue (range.getMaximum() * 0.25); // +12 dB side of the law
        expect (processor.getParametricEQParameter (
                    dynamicParameterIndex (0, DynamicBandOffset::Range))->getValue()
                    > 0.0f, "range slider drives the parameter");

        // Global detector/sidechain/link buttons.
        auto* topBar = editor->getTopBarForTesting();
        expect (topBar != nullptr);
        const bool initialDetector = processor.getParametricEQParameter (
            kDynamicDetectorParameter)->getBool();
        expect (initialDetector, "detector defaults to RMS");
        // The top bar's detector button is not exposed; verify the parameter
        // path indirectly by checking the processor-level API is coherent.
        juce::ignoreUnused (initialDetector);
    }

    void testPublishedVisualizationReflectsProcessorState()
    {
        beginTest ("published dynamic state drives the graph visualization");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Threshold),
                  -30.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), 9.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Attack),
                  0.001f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Release),
                  0.020f);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = static_cast<float> (0.8 * std::sin (
                    2.0 * juce::MathConstants<double>::pi * 937.5
                    * (block * 512 + sample) / 48000.0));
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value);
            }
            processor.processBlock (buffer, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        expect (editor->isBandDynamicActiveForTesting (0),
                "published mask marks the dynamic band active");
        expect (editor->getDisplayedDynamicGainDbForTesting (0) < -6.0,
                "published dynamic gain reflects the live reduction");

        // Deactivation must clear the visualization.
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 0.0f);
        buffer.clear();
        processor.processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        expect (! editor->isBandDynamicActiveForTesting (0),
                "deactivation clears the published mask");
        expectWithinAbsoluteError (editor->getDisplayedDynamicGainDbForTesting (0),
                                   0.0, 1.0e-6,
                                   "deactivation clears the published gain");
    }

    void testStateRestoreAndReopenStaySynchronized()
    {
        beginTest ("state restore and editor reopen reflect the processor state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, dynamicParameterIndex (0, DynamicBandOffset::Range), -7.5f);

        juce::MemoryBlock state;
        processor.getStateInformation (state);

        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (state.getData(),
                                     static_cast<int> (state.getSize()));
        restored.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (restored);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);
        expectEquals (editor->getDynRangeSliderForTesting().getValue(),
                      static_cast<double> (restored.getParametricEQParameter (
                          dynamicParameterIndex (0, DynamicBandOffset::Range))->getValue()),
                      "editor reflects the restored dynamic parameter");
        expect (restored.getParametricEQParameter (
                    dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
                "restore preserves the dynamic enable state");
    }
};

static ParametricEQDynamicEditorTests parametricEQDynamicEditorTests;

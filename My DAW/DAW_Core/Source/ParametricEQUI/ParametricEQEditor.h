#pragma once

#include <JuceHeader.h>

#include "../ParametricEQCore/ParametricEQProcessor.h"

namespace APEX::ParametricEQ
{

// Cross-platform, responsive, touch-capable editor for the APEX Parametric
// EQ. The same component hierarchy is intended to be hosted on Windows,
// macOS, Android and iOS: all layout is relative and breakpoint-driven, all
// essential interactions are reachable through single-pointer input with
// touch-sized targets, and no platform-specific behaviour lives here.
//
// Separation is strict: DSP and the 194-parameter ABI live in the Processor;
// this editor only projects parameters, consumes published immutable
// response frames, and interprets input gestures.
class ParametricEQEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ParametricEQEditor (Processor& processor);
    ~ParametricEQEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    enum class LayoutMode
    {
        CompactPhone,   // portrait phone width
        LargePhone,     // large phone / small tablet
        Tablet,         // iPad-class
        Desktop,        // normal desktop window
        LargeDesktop    // large high-DPI desktop
    };

    static constexpr int kMinimumWidth = 320;
    static constexpr int kMinimumHeight = 480;
    static constexpr int kTouchTarget = 44; // logical px minimum hit size
    static constexpr int kDisplayMinimumFrequency = 20;
    static constexpr int kDisplayMaximumFrequency = 24000;

    LayoutMode getLayoutMode() const noexcept { return layoutMode_; }
    Processor& getProcessor() noexcept { return processor_; }

    // Audition gesture lease (editor-owned token; the processor remains the
    // transient-state authority).
    void beginAuditionGesture();
    void endAuditionGesture();

    class TopBarComponent;
    class EqGraphComponent;
    class InspectorComponent;

    // Test accessors (kept narrow; production code uses the component tree).
    TopBarComponent* getTopBarForTesting() noexcept { return topBar_.get(); }
    EqGraphComponent* getGraphForTesting() noexcept { return graph_.get(); }
    InspectorComponent* getInspectorForTesting() noexcept { return inspector_.get(); }
    int getSelectedBandForTesting() const noexcept { return selectedBand_; }
    juce::Rectangle<int> getNodeBoundsForTesting (int band) const;
    juce::TextButton& getAuditionButtonForTesting();
    juce::TextButton& getPhaseButtonForTesting();
    juce::TextButton& getSketchButtonForTesting();
    const std::vector<std::unique_ptr<juce::TextButton>>&
        getPlacementButtonsForTesting() const;
    juce::TextButton& getDynEnableButtonForTesting();
    juce::TextButton& getDynFilterButtonForTesting();
    juce::Slider& getDynRangeSliderForTesting();
    bool isBandDynamicActiveForTesting (int band) const;
    double getDisplayedDynamicGainDbForTesting (int band) const;
    void setSelectedBandForTesting (int band);
    bool isAuditionArmedForTesting() const noexcept { return auditionArmed_; }

    // Bounds and pointer injection for layout/gesture tests. These keep the
    // nested component definitions private while allowing the responsive
    // behaviour to be exercised through the real component hierarchy.
    juce::Rectangle<int> getTopBarBoundsForTesting() const;
    juce::Rectangle<int> getGraphBoundsForTesting() const;
    juce::Rectangle<int> getInspectorBoundsForTesting() const;
    void injectGraphMouseDownForTesting (juce::Point<float> position);
    void injectGraphMouseDragForTesting (juce::Point<float> position);
    void injectGraphMouseUpForTesting (juce::Point<float> position);

private:
    void selectBand (int band);
    void applyLayout();
    bool isCompactLayout() const noexcept;

    Processor& processor_;
    LayoutMode layoutMode_ = LayoutMode::Desktop;
    int selectedBand_ = -1;
    bool auditionArmed_ = false;
    std::uint32_t auditionToken_ = 0;

    std::unique_ptr<TopBarComponent> topBar_;
    std::unique_ptr<EqGraphComponent> graph_;
    std::unique_ptr<InspectorComponent> inspector_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametricEQEditor)
};

} // namespace APEX::ParametricEQ

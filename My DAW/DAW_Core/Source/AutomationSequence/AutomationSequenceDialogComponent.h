// ============================================================
// AutomationSequenceDialogComponent.h  — v3 (Playground edition)
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// Main dialog component.  Expanded layout:
//   - Dialog grows to 880 × 660 (was 720 × 484)
//   - 11-category × up-to-8-shape browser replaces old selector
//   - 2-column 8-knob layout (added Chaos and Repeat)
//   - 7-flavor Randomize button (popup menu)
//   - Library button (opens LibraryComponent popup)
//   - Save button (saves current params under user-supplied name)
//   - Unique-seed counter shown subtly in the title bar
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "AutomationSequenceTypes.h"
#include "AutomationSequenceGeneratorCore.h"
#include "AutomationSequenceHistoryCore.h"
#include "AutomationSequenceRandomizerCore.h"
#include "AutomationSequenceLibraryComponent.h"
#include "../UICore/CursorThemeCore.h"
#include <memory>
#include <functional>
#include <array>

namespace APEX {
namespace AutomationSeq {

// ============================================================
// SequenceLAF
// ============================================================
class SequenceLAF final : public DAW::ApexCursorLookAndFeel {
public:
    SequenceLAF();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                               const juce::Colour& bg,
                               bool highlighted, bool down) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool highlighted, bool down) override;

    // ---- Palette ------------------------------------------
    static juce::Colour colBg()         { return juce::Colour(0xFF15101A); }
    static juce::Colour colPanel()      { return juce::Colour(0xFF1E1525); }
    static juce::Colour colPink()       { return juce::Colour(0xFFFF5FA0); }
    static juce::Colour colPinkDim()    { return juce::Colour(0xFF99305A); }
    static juce::Colour colPinkBright() { return juce::Colour(0xFFFFAFD2); }
    static juce::Colour colText()       { return juce::Colour(0xFFEDDBE6); }
    static juce::Colour colSubtext()    { return juce::Colour(0xFF8E7080); }
    static juce::Colour colAccept()     { return juce::Colour(0xFF60E0A0); }
    static juce::Colour colGrid()       { return juce::Colour(0xFF261D30); }
    static juce::Colour colGridLight()  { return juce::Colour(0xFF38273E); }

    static juce::Colour colForCategory(ShapeCategory cat) noexcept {
        switch (cat) {
            case ShapeCategory::Envelope: return juce::Colour(0xFFFF5FA0);
            case ShapeCategory::Ramp:     return juce::Colour(0xFFFF8C42);
            case ShapeCategory::Wave:     return juce::Colour(0xFFB06AFF);
            case ShapeCategory::Physics:  return juce::Colour(0xFF42C8FF);
            case ShapeCategory::Organic:  return juce::Colour(0xFF60E0A0);
            case ShapeCategory::Curve:    return juce::Colour(0xFFFFD666);
            case ShapeCategory::Random:   return juce::Colour(0xFFFF9AD5);
            case ShapeCategory::Glitch:   return juce::Colour(0xFFE040FB);
            case ShapeCategory::Pulse:    return juce::Colour(0xFF7FE5FF);
            case ShapeCategory::Mod:      return juce::Colour(0xFFFFB066);
            case ShapeCategory::Algorithm:return juce::Colour(0xFFFFE066);
            default:                      return juce::Colour(0xFFFF5FA0);
        }
    }
};

// ============================================================
// AutomationSequenceDialogComponent
// ============================================================
class AutomationSequenceDialogComponent final
    : public juce::Component,
      private juce::Timer
{
public:
    static constexpr int kDialogW = 880;
    static constexpr int kDialogH = 660;

    explicit AutomationSequenceDialogComponent(
        std::function<void(const GeneratedCurve&, const SequenceParams&)> onAccept,
        std::function<void(const GeneratedCurve&, const SequenceParams&)> onApply = nullptr,
        std::function<void(const GeneratedCurve&, const SequenceParams&)> onApplyAll = nullptr,
        std::function<void()> onCancel = nullptr);

    ~AutomationSequenceDialogComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setInitialParams(const SequenceParams& p);
    const SequenceParams& getCurrentParams() const noexcept { return params_; }

private:
    void timerCallback() override;

    // =======================================================
    // StepGridComponent
    // =======================================================
    class StepGridComponent final : public juce::Component {
    public:
        std::function<void(int, bool)> onStepToggled;
        void setParams(const SequenceParams* p);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
    private:
        const SequenceParams* params_ = nullptr;
        bool dragState_ = false;
        int  hitTestStep(juce::Point<float> pos) const;
    };

    // =======================================================
    // CurvePreviewComponent
    // =======================================================
    class CurvePreviewComponent final : public juce::Component {
    public:
        void setCurve(const GeneratedCurve* c);
        void setPlayheadPhase(float ph);
        void setNumSteps(int n)         { numSteps_ = juce::jlimit(1, kMaxSteps, n); }
        void setAccentColour(juce::Colour c) { accentColour_ = c; repaint(); }
        void paint(juce::Graphics& g) override;
    private:
        const GeneratedCurve* curve_ = nullptr;
        float playheadPhase_ = 0.0f;
        int   numSteps_      = 16;
        juce::Colour accentColour_ = juce::Colour(0xFFFFAFD2);
    };

    // =======================================================
    // BarViewComponent
    // =======================================================
    class BarViewComponent final : public juce::Component {
    public:
        std::function<void(int, float)> onBarDragged;
        BarViewMode activeMode = BarViewMode::AttackLevel;
        void setBarValues(const std::array<float, kMaxSteps>& vals, int numSteps);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
    private:
        std::array<float, kMaxSteps> values_{};
        int numSteps_ = 16;
        int barHitTest(juce::Point<float> pos) const;
        float yToValue(float y) const;
    };

    // =======================================================
    // BarTabsComponent
    // =======================================================
    class BarTabsComponent final : public juce::Component {
    public:
        BarViewMode selectedMode = BarViewMode::AttackLevel;
        std::function<void(BarViewMode)> onModeChanged;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
    };

    // =======================================================
    // CategoryBrowserComponent
    //   3 rows × 4 columns of category pills (one empty cell)
    //   2 rows × 4 columns of shape pills (the selected category)
    // =======================================================
    class CategoryBrowserComponent final : public juce::Component {
    public:
        ShapeCategory selectedCategory = ShapeCategory::Envelope;
        ShapeMode     selectedMode     = ShapeMode::Normal;

        std::function<void(ShapeMode)> onShapeSelected;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

    private:
        int hoveredCat   = -1;
        int hoveredShape = -1;
        juce::Rectangle<int> catPillBounds  (int catIdx)   const;
        juce::Rectangle<int> shapePillBounds(int shapeIdx) const;
        int hitTestCat  (juce::Point<float> pos) const;
        int hitTestShape(juce::Point<float> pos) const;
    };

    // ---- knob factory --------------------------------------
    struct LabeledKnob {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  label;
    };
    LabeledKnob makeKnob(const juce::String& name, double mn, double mx, double def);

    // ---- layout helpers ------------------------------------
    juce::Rectangle<int> leftArea()   const;
    juce::Rectangle<int> rightPanel() const;

    // ---- state ---------------------------------------------
    SequenceParams params_;
    GeneratedCurve cachedCurve_;
    bool  curveDirty_    = true;
    float playheadPhase_ = 0.0f;
    bool  animPlaying_   = true;

    // ---- sub-components ------------------------------------
    std::unique_ptr<StepGridComponent>        stepGrid_;
    std::unique_ptr<CurvePreviewComponent>    curvePreview_;
    std::unique_ptr<BarViewComponent>         barView_;
    std::unique_ptr<BarTabsComponent>         barTabs_;
    std::unique_ptr<CategoryBrowserComponent> catBrowser_;

    LabeledKnob knobTimeMul_;
    LabeledKnob knobSwing_;
    LabeledKnob knobAttack_;
    LabeledKnob knobDecay_;
    LabeledKnob knobSustain_;
    LabeledKnob knobGate_;
    LabeledKnob knobChaos_;
    LabeledKnob knobRepeat_;

    std::unique_ptr<juce::TextButton> btnReset_;
    std::unique_ptr<juce::TextButton> btnRandomize_;
    std::unique_ptr<juce::TextButton> btnLibrary_;
    std::unique_ptr<juce::TextButton> btnSave_;
    std::unique_ptr<juce::TextButton> btnAccept_;

    std::unique_ptr<SequenceLAF> laf_;

    std::function<void(const GeneratedCurve&, const SequenceParams&)> onAcceptCb_;
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onApplyCb_;
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onApplyAllCb_;
    std::function<void()>                                              onCancelCb_;

    // For non-blocking dialogs we keep references alive
    juce::Component::SafePointer<juce::DialogWindow> libraryWindow_;
    juce::Component::SafePointer<juce::AlertWindow>  saveDialog_;

    // ---- internal methods ----------------------------------
    void rebuildCurve();
    void refreshBarView();
    void syncKnobsToParams();
    void hookKnobCallbacks();

    void doReset();
    void doRandomize();
    void doShowLibrary();
    void doSave();
    void doAccept();

    void showRandomizeMenu();
    void applyRandomizedParams(const SequenceParams& p);
    void loadParamsFromLibrary(const SequenceParams& p);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationSequenceDialogComponent)
};

} // namespace AutomationSeq
} // namespace APEX

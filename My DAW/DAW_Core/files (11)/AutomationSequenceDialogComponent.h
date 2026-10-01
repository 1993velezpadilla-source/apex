// ============================================================
// AutomationSequenceDialogComponent.h
// APEX — Create Sequence Dialog  |  Source/AutomationSequence/
//
// The main JUCE component for the Create Sequence popup.
// Shows a step grid, live 60fps curve preview, per-step bar
// view, ADSR knobs, and Accept/Reset/Randomize/Humanize buttons.
//
// DO NOT modify cable files, routing files, or audio-thread
// code when adding this dialog.
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "AutomationSequenceTypes.h"
#include "AutomationSequenceGeneratorCore.h"
#include <memory>
#include <functional>
#include <array>

namespace APEX {
namespace AutomationSeq {

// ============================================================
// SequenceLAF
//   Custom LookAndFeel: Bubblegum pink rotary knobs, dark grid
//   backgrounds, styled text buttons.
// ============================================================
class SequenceLAF final : public juce::LookAndFeel_V4 {
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

    // ---- Bubblegum palette ---------------------------------
    static juce::Colour colBg()          { return juce::Colour(0xFF18121A); }
    static juce::Colour colPanel()       { return juce::Colour(0xFF221A26); }
    static juce::Colour colPink()        { return juce::Colour(0xFFFF5FA0); }
    static juce::Colour colPinkDim()     { return juce::Colour(0xFF99305A); }
    static juce::Colour colPinkBright()  { return juce::Colour(0xFFFFAFD2); }
    static juce::Colour colText()        { return juce::Colour(0xFFEDDBE6); }
    static juce::Colour colSubtext()     { return juce::Colour(0xFF8E7080); }
    static juce::Colour colAccept()      { return juce::Colour(0xFF60E0A0); }
    static juce::Colour colGrid()        { return juce::Colour(0xFF2A2030); }
    static juce::Colour colGridLight()   { return juce::Colour(0xFF3A2A42); }
};

// ============================================================
// AutomationSequenceDialogComponent
// ============================================================
class AutomationSequenceDialogComponent final
    : public juce::Component,
      private juce::Timer
{
public:
    // Recommended dialog size
    static constexpr int kDialogW = 720;
    static constexpr int kDialogH = 484;

    // onAccept fires when user clicks Accept — receives the final curve + params.
    // onCancel fires if the dialog needs to be closed without committing
    // (caller is responsible for actually removing the component from its parent).
    explicit AutomationSequenceDialogComponent(
        std::function<void(const GeneratedCurve&, const SequenceParams&)> onAccept,
        std::function<void()> onCancel = nullptr);

    ~AutomationSequenceDialogComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setInitialParams(const SequenceParams& p);
    const SequenceParams& getCurrentParams() const noexcept { return params_; }

private:
    // ---- 60fps timer ---------------------------------------
    void timerCallback() override;

    // =======================================================
    // Step Grid — 16 toggleable step buttons across the top
    // =======================================================
    class StepGridComponent final : public juce::Component {
    public:
        std::function<void(int stepIdx, bool newState)> onStepToggled;

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
    // Curve Preview — the main animated visualisation
    // =======================================================
    class CurvePreviewComponent final : public juce::Component {
    public:
        void setCurve(const GeneratedCurve* c);
        void setPlayheadPhase(float ph01);
        void setNumSteps(int n) { numSteps_ = juce::jlimit(1, kMaxSteps, n); }
        void paint(juce::Graphics& g) override;

    private:
        const GeneratedCurve* curve_ = nullptr;
        float playheadPhase_ = 0.0f;
        int   numSteps_ = 16;
    };

    // =======================================================
    // Bar View — per-step draggable scalar bars
    // =======================================================
    class BarViewComponent final : public juce::Component {
    public:
        std::function<void(int stepIdx, float newValue01)> onBarDragged;

        void setBarValues(const std::array<float, kMaxSteps>& vals, int numSteps);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;

    private:
        std::array<float, kMaxSteps> values_{};
        int numSteps_ = 16;
        int dragStep_ = -1;
        int   barHitTest(juce::Point<float> pos) const;
        float yToValue(float y) const;
    };

    // =======================================================
    // Bar Tabs — label strip switching the bar view target
    // =======================================================
    class BarTabsComponent final : public juce::Component {
    public:
        BarViewMode selectedMode = BarViewMode::AttackLevel;
        std::function<void(BarViewMode)> onModeChanged;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
    };

    // =======================================================
    // Mode Selector — small "◀ Normal ▶" control at top of right panel
    // =======================================================
    class ModeSelectorComponent final : public juce::Component {
    public:
        ShapeMode selectedMode = ShapeMode::Normal;
        std::function<void(ShapeMode)> onModeChanged;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override { hovering_ = true;  repaint(); }
        void mouseExit (const juce::MouseEvent& e) override { hovering_ = false; repaint(); }
    private:
        bool hovering_ = false;
    };

    // ---- Knob factory -------------------------------------
    struct LabeledKnob {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  label;
    };
    LabeledKnob makeKnob(const juce::String& name, double min, double max, double def);

    // ---- Layout helpers ------------------------------------
    juce::Rectangle<int> leftArea()   const;
    juce::Rectangle<int> rightPanel() const;

    // ---- Internal state ------------------------------------
    SequenceParams params_;
    GeneratedCurve cachedCurve_;
    bool  curveDirty_     = true;
    float playheadPhase_  = 0.0f;
    bool  animatePlaying_ = true;   // playhead loops continuously

    // ---- Sub-components ------------------------------------
    std::unique_ptr<StepGridComponent>     stepGrid_;
    std::unique_ptr<CurvePreviewComponent> curvePreview_;
    std::unique_ptr<BarViewComponent>      barView_;
    std::unique_ptr<BarTabsComponent>      barTabs_;
    std::unique_ptr<ModeSelectorComponent> modeSelector_;

    LabeledKnob knobTimeMul_;
    LabeledKnob knobSwing_;
    LabeledKnob knobAttack_;
    LabeledKnob knobDecay_;
    LabeledKnob knobSustain_;
    LabeledKnob knobGate_;

    std::unique_ptr<juce::TextButton> btnReset_;
    std::unique_ptr<juce::TextButton> btnRandomize_;
    std::unique_ptr<juce::TextButton> btnHumanize_;
    std::unique_ptr<juce::TextButton> btnAccept_;

    std::unique_ptr<SequenceLAF> laf_;

    std::function<void(const GeneratedCurve&, const SequenceParams&)> onAcceptCb_;
    std::function<void()> onCancelCb_;

    // ---- Internal methods ---------------------------------
    void rebuildCurve();
    void refreshBarView();
    void syncKnobsToParams();
    void hookKnobCallbacks();

    void doReset();
    void doRandomize();
    void doHumanize();
    void doAccept();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationSequenceDialogComponent)
};

} // namespace AutomationSeq
} // namespace APEX

// ============================================================
// AutomationSequenceDialogComponent.cpp
// APEX — Create Sequence Dialog  |  Source/AutomationSequence/
// ============================================================
#include "AutomationSequenceDialogComponent.h"
#include <random>

namespace APEX {
namespace AutomationSeq {

// ============================================================
// SequenceLAF
// ============================================================
SequenceLAF::SequenceLAF() {
    setColour(juce::Slider::thumbColourId,       colPink());
    setColour(juce::Slider::trackColourId,       colPinkDim());
    setColour(juce::Slider::backgroundColourId,  colGrid());
    setColour(juce::TextButton::buttonColourId,  colGrid());
    setColour(juce::TextButton::textColourOnId,  colText());
    setColour(juce::TextButton::textColourOffId, colSubtext());
    setColour(juce::Label::textColourId,         colText());
}

void SequenceLAF::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                    float sliderPos, float startAngle, float endAngle,
                                    juce::Slider& /*slider*/)
{
    const float radius = static_cast<float>(juce::jmin(w, h)) * 0.5f - 4.0f;
    const float cx     = x + w * 0.5f;
    const float cy     = y + h * 0.5f;

    // ---- Outer track arc ----
    juce::Path track;
    track.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(colGrid());
    g.strokePath(track, juce::PathStrokeType(3.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ---- Filled value arc ----
    const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, valueAngle, true);
    g.setColour(colPink());
    g.strokePath(value, juce::PathStrokeType(3.0f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ---- Inner body fill ----
    const float innerR = radius * 0.62f;
    juce::ColourGradient bodyGrad(colPanel().brighter(0.08f),
                                   cx, cy - innerR,
                                   colPanel().darker(0.20f),
                                   cx, cy + innerR, false);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);

    // ---- Indicator line ----
    const float aRad = valueAngle - juce::MathConstants<float>::halfPi;
    const float ix1  = cx + std::cos(aRad) * innerR * 0.35f;
    const float iy1  = cy + std::sin(aRad) * innerR * 0.35f;
    const float ix2  = cx + std::cos(aRad) * innerR * 0.92f;
    const float iy2  = cy + std::sin(aRad) * innerR * 0.92f;
    g.setColour(colPinkBright());
    g.drawLine(ix1, iy1, ix2, iy2, 2.2f);
}

void SequenceLAF::drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                                        const juce::Colour& /*bg*/,
                                        bool highlighted, bool down)
{
    auto bounds = btn.getLocalBounds().toFloat().reduced(1.0f);

    juce::Colour fill = colGrid();
    if (btn.getButtonText() == "Accept") {
        fill = down ? colAccept().darker(0.20f) :
               highlighted ? colAccept() : colAccept().darker(0.10f);
    } else {
        fill = down ? colPinkDim().brighter(0.10f)
                    : (highlighted ? colGrid().brighter(0.20f) : colGrid());
    }

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(highlighted ? colPink() : colGridLight());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

void SequenceLAF::drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                                  bool highlighted, bool /*down*/)
{
    juce::Colour textCol = colText();
    if (btn.getButtonText() == "Accept") textCol = juce::Colours::black;
    else if (highlighted)                textCol = colPinkBright();

    g.setColour(textCol);
    g.setFont(juce::Font("Inter", 11.5f, juce::Font::bold));
    g.drawFittedText(btn.getButtonText(), btn.getLocalBounds(),
                     juce::Justification::centred, 1);
}

// ============================================================
// StepGridComponent
// ============================================================
void AutomationSequenceDialogComponent::StepGridComponent::setParams(const SequenceParams* p) {
    params_ = p;
    repaint();
}

int AutomationSequenceDialogComponent::StepGridComponent::hitTestStep(juce::Point<float> pos) const {
    if (!params_) return -1;
    const int N = juce::jlimit(1, kMaxSteps, params_->numSteps);
    const float stepW = static_cast<float>(getWidth()) / N;
    int idx = static_cast<int>(pos.x / stepW);
    return juce::jlimit(0, N - 1, idx);
}

void AutomationSequenceDialogComponent::StepGridComponent::paint(juce::Graphics& g) {
    if (!params_) return;

    const int N = juce::jlimit(1, kMaxSteps, params_->numSteps);
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    const float stepW = w / N;
    const float gap = 2.0f;

    for (int i = 0; i < N; ++i) {
        const bool active = params_->steps[i].active;
        juce::Rectangle<float> r(i * stepW + gap, gap,
                                  stepW - gap * 2.0f, h - gap * 2.0f);

        if (active) {
            // Filled pink pill
            juce::ColourGradient pillGrad(SequenceLAF::colPinkBright(),
                                           r.getX(), r.getY(),
                                           SequenceLAF::colPink(),
                                           r.getX(), r.getBottom(), false);
            g.setGradientFill(pillGrad);
            g.fillRoundedRectangle(r, 3.5f);

            // Top gloss highlight
            juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.22f),
                                        r.getX(), r.getY(),
                                        juce::Colours::transparentWhite,
                                        r.getX(), r.getY() + r.getHeight() * 0.55f, false);
            g.setGradientFill(gloss);
            g.fillRoundedRectangle(r.reduced(1.0f, 1.0f), 3.0f);
        } else {
            g.setColour(SequenceLAF::colGrid());
            g.fillRoundedRectangle(r, 3.5f);
            g.setColour(SequenceLAF::colPinkDim().withAlpha(0.45f));
            g.drawRoundedRectangle(r, 3.5f, 1.0f);
        }

        // Step number
        g.setColour(active ? juce::Colours::white.withAlpha(0.9f)
                           : SequenceLAF::colSubtext());
        g.setFont(juce::Font("Inter", 9.5f, juce::Font::bold));
        g.drawText(juce::String(i + 1), r.toNearestInt(),
                   juce::Justification::centred, false);
    }
}

void AutomationSequenceDialogComponent::StepGridComponent::mouseDown(const juce::MouseEvent& e) {
    int idx = hitTestStep(e.position);
    if (idx < 0 || !params_) return;
    dragState_ = !params_->steps[idx].active;     // flip on mousedown
    if (onStepToggled) onStepToggled(idx, dragState_);
}

void AutomationSequenceDialogComponent::StepGridComponent::mouseDrag(const juce::MouseEvent& e) {
    int idx = hitTestStep(e.position);
    if (idx < 0) return;
    if (onStepToggled) onStepToggled(idx, dragState_);
}

// ============================================================
// CurvePreviewComponent
// ============================================================
void AutomationSequenceDialogComponent::CurvePreviewComponent::setCurve(const GeneratedCurve* c) {
    curve_ = c;
    repaint();
}

void AutomationSequenceDialogComponent::CurvePreviewComponent::setPlayheadPhase(float ph) {
    playheadPhase_ = juce::jlimit(0.0f, 1.0f, ph);
    repaint();
}

void AutomationSequenceDialogComponent::CurvePreviewComponent::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();

    // ---- Background ----
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(b, 5.0f);

    // ---- Step boundary grid lines ----
    g.setColour(SequenceLAF::colGrid().withAlpha(0.55f));
    for (int s = 1; s < numSteps_; ++s) {
        float x = b.getX() + (static_cast<float>(s) / numSteps_) * b.getWidth();
        g.drawVerticalLine(static_cast<int>(x),
                           b.getY() + 3.0f, b.getBottom() - 3.0f);
    }

    // ---- Horizontal level lines ----
    g.setColour(SequenceLAF::colGrid().brighter(0.10f));
    for (float lv : { 0.25f, 0.5f, 0.75f }) {
        float y = b.getBottom() - lv * b.getHeight();
        g.drawHorizontalLine(static_cast<int>(y), b.getX() + 4.0f, b.getRight() - 4.0f);
    }

    if (!curve_) {
        g.setColour(SequenceLAF::colSubtext());
        g.setFont(12.0f);
        g.drawText("No active steps", b.toNearestInt(),
                   juce::Justification::centred);
        return;
    }

    const int n = static_cast<int>(curve_->samples.size());

    // ---- Build fill + stroke paths ----
    juce::Path fillPath, linePath;
    bool started = false;
    for (int i = 0; i < n; ++i) {
        float x = b.getX() + (i / static_cast<float>(n - 1)) * b.getWidth();
        float y = b.getBottom() - 4.0f - curve_->samples[i] * (b.getHeight() - 8.0f);
        if (!started) {
            fillPath.startNewSubPath(x, b.getBottom() - 2.0f);
            fillPath.lineTo(x, y);
            linePath.startNewSubPath(x, y);
            started = true;
        } else {
            fillPath.lineTo(x, y);
            linePath.lineTo(x, y);
        }
    }
    fillPath.lineTo(b.getRight(), b.getBottom() - 2.0f);
    fillPath.closeSubPath();

    // ---- Gradient fill under curve ----
    juce::ColourGradient grad(SequenceLAF::colPink().withAlpha(0.50f),
                               b.getX(), b.getY(),
                               SequenceLAF::colPink().withAlpha(0.04f),
                               b.getX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillPath(fillPath);

    // ---- Bright top line ----
    g.setColour(SequenceLAF::colPinkBright());
    g.strokePath(linePath, juce::PathStrokeType(1.6f,
                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ---- Animated playhead ----
    if (playheadPhase_ > 0.0f && n > 1) {
        const float phX = b.getX() + playheadPhase_ * b.getWidth();
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawVerticalLine(static_cast<int>(phX),
                           b.getY() + 4.0f, b.getBottom() - 4.0f);

        const int   idx = juce::jlimit(0, n - 1, static_cast<int>(playheadPhase_ * (n - 1)));
        const float phY = b.getBottom() - 4.0f
                          - curve_->samples[idx] * (b.getHeight() - 8.0f);
        g.setColour(SequenceLAF::colPinkBright());
        g.fillEllipse(phX - 4.0f, phY - 4.0f, 8.0f, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(phX - 4.0f, phY - 4.0f, 8.0f, 8.0f, 1.2f);
    }

    // ---- Border ----
    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
}

// ============================================================
// BarViewComponent
// ============================================================
void AutomationSequenceDialogComponent::BarViewComponent::setBarValues(
    const std::array<float, kMaxSteps>& vals, int numSteps)
{
    values_ = vals;
    numSteps_ = juce::jlimit(1, kMaxSteps, numSteps);
    repaint();
}

int AutomationSequenceDialogComponent::BarViewComponent::barHitTest(juce::Point<float> pos) const {
    const float barW = static_cast<float>(getWidth()) / numSteps_;
    int idx = static_cast<int>(pos.x / barW);
    return juce::jlimit(0, numSteps_ - 1, idx);
}

float AutomationSequenceDialogComponent::BarViewComponent::yToValue(float y) const {
    return juce::jlimit(0.0f, 1.0f, 1.0f - (y / static_cast<float>(getHeight())));
}

void AutomationSequenceDialogComponent::BarViewComponent::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();

    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(b, 5.0f);

    const float barW = b.getWidth() / numSteps_;
    const float gap  = 2.0f;

    for (int i = 0; i < numSteps_; ++i) {
        const float val   = values_[i];
        const float barH  = val * (b.getHeight() - 8.0f);
        const float barX  = i * barW + gap;
        const float barY  = b.getBottom() - 4.0f - barH;
        juce::Rectangle<float> r(barX, barY, barW - gap * 2.0f, std::max(barH, 0.0f));

        if (barH < 1.0f) {
            g.setColour(SequenceLAF::colPinkDim().withAlpha(0.45f));
            g.fillRect(juce::Rectangle<float>(barX, b.getBottom() - 6.0f,
                                                barW - gap * 2.0f, 2.0f));
            continue;
        }

        juce::ColourGradient barGrad(SequenceLAF::colPinkBright(),
                                      r.getX(), r.getY(),
                                      SequenceLAF::colPinkDim(),
                                      r.getX(), r.getBottom(), false);
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(r, 2.0f);

        // Top cap highlight
        g.setColour(juce::Colours::white.withAlpha(0.20f));
        g.fillRect(juce::Rectangle<float>(r.getX(), r.getY(),
                                            r.getWidth(), std::min(3.0f, r.getHeight())));
    }

    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
}

void AutomationSequenceDialogComponent::BarViewComponent::mouseDown(const juce::MouseEvent& e) {
    dragStep_ = barHitTest(e.position);
    if (dragStep_ >= 0 && onBarDragged) {
        onBarDragged(dragStep_, yToValue(e.position.y));
    }
}

void AutomationSequenceDialogComponent::BarViewComponent::mouseDrag(const juce::MouseEvent& e) {
    // Allow horizontal sweeping to "paint" bar values across multiple steps
    int idx = barHitTest(e.position);
    if (idx >= 0 && onBarDragged) {
        onBarDragged(idx, yToValue(e.position.y));
    }
}

// ============================================================
// BarTabsComponent
// ============================================================
void AutomationSequenceDialogComponent::BarTabsComponent::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    static const std::array<juce::String, 4> labels = {
        "Attack level", "Decay slope", "Sustain level", "Release slope"
    };
    const float tabW = b.getWidth() / 4.0f;

    for (int i = 0; i < 4; ++i) {
        const bool sel = (i == static_cast<int>(selectedMode));
        juce::Rectangle<float> r(i * tabW + 1.0f, 1.0f,
                                  tabW - 2.0f, b.getHeight() - 2.0f);

        g.setColour(sel ? SequenceLAF::colPinkDim() : SequenceLAF::colGrid());
        g.fillRoundedRectangle(r, 3.0f);

        if (sel) {
            g.setColour(SequenceLAF::colPink().withAlpha(0.30f));
            g.drawRoundedRectangle(r, 3.0f, 1.2f);
        }

        g.setColour(sel ? SequenceLAF::colPinkBright() : SequenceLAF::colSubtext());
        g.setFont(juce::Font("Inter", 10.0f, sel ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(labels[i], r.toNearestInt(), juce::Justification::centred, 1);
    }
}

void AutomationSequenceDialogComponent::BarTabsComponent::mouseDown(const juce::MouseEvent& e) {
    int idx = static_cast<int>(e.position.x / (getWidth() / 4.0f));
    idx = juce::jlimit(0, 3, idx);
    selectedMode = static_cast<BarViewMode>(idx);
    repaint();
    if (onModeChanged) onModeChanged(selectedMode);
}

// ============================================================
// ModeSelectorComponent
// ============================================================
void AutomationSequenceDialogComponent::ModeSelectorComponent::paint(juce::Graphics& g) {
    static const std::array<juce::String, 7> names = {
        "Normal", "Saw", "Rev Saw", "Triangle", "Gate", "Sine", "Bounce"
    };
    const auto b = getLocalBounds().toFloat();

    g.setColour(hovering_ ? SequenceLAF::colGrid().brighter(0.20f)
                          : SequenceLAF::colGrid());
    g.fillRoundedRectangle(b, 4.0f);

    g.setColour(SequenceLAF::colPinkBright());
    g.setFont(juce::Font("Inter", 10.5f, juce::Font::bold));

    const juce::String left  = juce::String::fromUTF8("\xe2\x97\x82");   // ◂
    const juce::String right = juce::String::fromUTF8("\xe2\x96\xb8");   // ▸
    const juce::String txt   = left + "   " + names[static_cast<int>(selectedMode)] + "   " + right;
    g.drawFittedText(txt, b.toNearestInt(), juce::Justification::centred, 1);

    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
}

void AutomationSequenceDialogComponent::ModeSelectorComponent::mouseDown(const juce::MouseEvent& e) {
    constexpr int kModeCount = 7;
    int cur = static_cast<int>(selectedMode);
    const bool right = e.position.x > getWidth() * 0.5f;
    cur = (cur + (right ? 1 : -1) + kModeCount) % kModeCount;
    selectedMode = static_cast<ShapeMode>(cur);
    repaint();
    if (onModeChanged) onModeChanged(selectedMode);
}

// ============================================================
// AutomationSequenceDialogComponent
// ============================================================
AutomationSequenceDialogComponent::AutomationSequenceDialogComponent(
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onAccept,
    std::function<void()> onCancel)
    : onAcceptCb_(std::move(onAccept))
    , onCancelCb_(std::move(onCancel))
{
    laf_ = std::make_unique<SequenceLAF>();
    setLookAndFeel(laf_.get());
    setSize(kDialogW, kDialogH);
    setInterceptsMouseClicks(true, true);

    // ---- Default all steps active ----
    for (auto& s : params_.steps) s.active = true;

    // ---- Step Grid ----
    stepGrid_ = std::make_unique<StepGridComponent>();
    stepGrid_->setParams(&params_);
    stepGrid_->onStepToggled = [this](int idx, bool state) {
        if (idx < 0 || idx >= kMaxSteps) return;
        params_.steps[idx].active = state;
        curveDirty_ = true;
        stepGrid_->repaint();
    };
    addAndMakeVisible(*stepGrid_);

    // ---- Curve Preview ----
    curvePreview_ = std::make_unique<CurvePreviewComponent>();
    curvePreview_->setNumSteps(params_.numSteps);
    addAndMakeVisible(*curvePreview_);

    // ---- Bar Tabs ----
    barTabs_ = std::make_unique<BarTabsComponent>();
    addAndMakeVisible(*barTabs_);

    // ---- Bar View ----
    barView_ = std::make_unique<BarViewComponent>();
    barView_->onBarDragged = [this](int step, float val) {
        if (step < 0 || step >= kMaxSteps) return;
        switch (barTabs_->selectedMode) {
            case BarViewMode::AttackLevel:  params_.steps[step].localAttackLevel  = val; break;
            case BarViewMode::DecaySlope:   params_.steps[step].localDecaySlope   = val; break;
            case BarViewMode::SustainLevel: params_.steps[step].localSustainLevel = val; break;
            case BarViewMode::ReleaseSlope: params_.steps[step].localReleaseSlope = val; break;
            default: break;
        }
        curveDirty_ = true;
    };
    addAndMakeVisible(*barView_);

    barTabs_->onModeChanged = [this](BarViewMode m) {
        barView_->activeMode = m;   // not used directly but keeps state consistent
        refreshBarView();
    };

    // (BarViewComponent doesn't actually need activeMode — leave for future)
    barView_->activeMode = BarViewMode::AttackLevel;

    // ---- Mode Selector ----
    modeSelector_ = std::make_unique<ModeSelectorComponent>();
    modeSelector_->onModeChanged = [this](ShapeMode m) {
        params_.mode = m;
        curveDirty_ = true;
    };
    addAndMakeVisible(*modeSelector_);

    // ---- Knobs ----
    knobTimeMul_ = makeKnob("Time mul", 0.0, 1.0, 0.5);
    knobSwing_   = makeKnob("Swing",    0.0, 1.0, 0.0);
    knobAttack_  = makeKnob("Attack",   0.0, 1.0, 1.0);
    knobDecay_   = makeKnob("Decay",    0.0, 1.0, 0.35);
    knobSustain_ = makeKnob("Sustain",  0.0, 1.0, 0.55);
    knobGate_    = makeKnob("Gate",     0.0, 1.0, 0.75);

    for (auto* k : { &knobTimeMul_, &knobSwing_, &knobAttack_,
                     &knobDecay_,   &knobSustain_, &knobGate_ }) {
        addAndMakeVisible(*k->slider);
        addAndMakeVisible(*k->label);
    }

    hookKnobCallbacks();

    // ---- Buttons ----
    btnReset_     = std::make_unique<juce::TextButton>("Reset");
    btnRandomize_ = std::make_unique<juce::TextButton>("Randomize");
    btnHumanize_  = std::make_unique<juce::TextButton>("Humanize");
    btnAccept_    = std::make_unique<juce::TextButton>("Accept");

    btnReset_    ->onClick = [this] { doReset();     };
    btnRandomize_->onClick = [this] { doRandomize(); };
    btnHumanize_ ->onClick = [this] { doHumanize();  };
    btnAccept_   ->onClick = [this] { doAccept();    };

    addAndMakeVisible(*btnReset_);
    addAndMakeVisible(*btnRandomize_);
    addAndMakeVisible(*btnHumanize_);
    addAndMakeVisible(*btnAccept_);

    rebuildCurve();
    startTimerHz(60);    // 60fps refresh
}

AutomationSequenceDialogComponent::~AutomationSequenceDialogComponent() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void AutomationSequenceDialogComponent::timerCallback() {
    if (curveDirty_) {
        rebuildCurve();
        curveDirty_ = false;
    }

    if (animatePlaying_) {
        playheadPhase_ += 1.0f / 240.0f;   // full cycle every 4 seconds
        if (playheadPhase_ > 1.0f) playheadPhase_ -= 1.0f;
        curvePreview_->setPlayheadPhase(playheadPhase_);
    }
}

juce::Rectangle<int> AutomationSequenceDialogComponent::leftArea() const {
    return juce::Rectangle<int>(8, 36,
                                 getWidth() - 188, getHeight() - 44);
}

juce::Rectangle<int> AutomationSequenceDialogComponent::rightPanel() const {
    return juce::Rectangle<int>(getWidth() - 172, 36,
                                 164, getHeight() - 44);
}

void AutomationSequenceDialogComponent::resized() {
    auto left  = leftArea();
    auto right = rightPanel();

    // ---- Left column ----
    stepGrid_->setBounds(left.removeFromTop(46));
    left.removeFromTop(6);

    curvePreview_->setBounds(left.removeFromTop(190));
    left.removeFromTop(6);

    barTabs_->setBounds(left.removeFromTop(24));
    left.removeFromTop(2);

    barView_->setBounds(left.removeFromTop(64));
    left.removeFromTop(6);

    auto btnRow = left.removeFromTop(34);
    const int btnGap = 6;
    const int btnW   = (btnRow.getWidth() - btnGap * 2) / 3;
    btnReset_    ->setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(btnGap);
    btnRandomize_->setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(btnGap);
    btnHumanize_ ->setBounds(btnRow);

    // ---- Right column ----
    modeSelector_->setBounds(right.removeFromTop(26));
    right.removeFromTop(6);

    auto layoutKnob = [&](LabeledKnob& k) {
        auto slot = right.removeFromTop(60);
        k.label->setBounds(slot.removeFromBottom(15));
        k.slider->setBounds(slot);
        right.removeFromTop(2);
    };
    layoutKnob(knobTimeMul_);
    layoutKnob(knobSwing_);
    layoutKnob(knobAttack_);
    layoutKnob(knobDecay_);
    layoutKnob(knobSustain_);
    layoutKnob(knobGate_);

    // Accept button: bottom of right panel
    btnAccept_->setBounds(getWidth() - 172, getHeight() - 42, 164, 34);
}

void AutomationSequenceDialogComponent::paint(juce::Graphics& g) {
    // Dialog background
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

    // Title bar
    auto title = getLocalBounds().toFloat().withHeight(30.0f);
    juce::ColourGradient titleGrad(SequenceLAF::colPanel().brighter(0.08f),
                                    title.getX(), title.getY(),
                                    SequenceLAF::colPanel(),
                                    title.getX(), title.getBottom(), false);
    g.setGradientFill(titleGrad);
    g.fillRoundedRectangle(title, 8.0f);
    g.fillRect(title.withTop(title.getBottom() - 8.0f));

    // Title label
    g.setColour(SequenceLAF::colPinkBright());
    g.setFont(juce::Font("Inter", 12.5f, juce::Font::bold));
    g.drawText("Create Sequence", title.toNearestInt(),
               juce::Justification::centred);

    // Right panel separator
    const float sepX = static_cast<float>(getWidth() - 180);
    g.setColour(SequenceLAF::colGridLight());
    g.drawVerticalLine(static_cast<int>(sepX),
                       36.0f, static_cast<float>(getHeight() - 8));

    // Outer border
    g.setColour(SequenceLAF::colPinkDim().withAlpha(0.55f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.5f);
}

void AutomationSequenceDialogComponent::rebuildCurve() {
    cachedCurve_ = GeneratorCore::generate(params_);
    curvePreview_->setCurve(&cachedCurve_);
    curvePreview_->setNumSteps(params_.numSteps);
    refreshBarView();
}

void AutomationSequenceDialogComponent::refreshBarView() {
    auto vals = GeneratorCore::barValues(params_, barTabs_->selectedMode);
    barView_->setBarValues(vals, params_.numSteps);
}

void AutomationSequenceDialogComponent::syncKnobsToParams() {
    knobTimeMul_.slider->setValue(params_.timeMul,      juce::dontSendNotification);
    knobSwing_  .slider->setValue(params_.swing,        juce::dontSendNotification);
    knobAttack_ .slider->setValue(params_.attackLevel,  juce::dontSendNotification);
    knobDecay_  .slider->setValue(params_.decaySlope,   juce::dontSendNotification);
    knobSustain_.slider->setValue(params_.sustainLevel, juce::dontSendNotification);
    knobGate_   .slider->setValue(params_.gate,         juce::dontSendNotification);
}

void AutomationSequenceDialogComponent::hookKnobCallbacks() {
    knobTimeMul_.slider->onValueChange = [this] {
        params_.timeMul = static_cast<float>(knobTimeMul_.slider->getValue());
        curveDirty_ = true;
    };
    knobSwing_.slider->onValueChange = [this] {
        params_.swing = static_cast<float>(knobSwing_.slider->getValue());
        curveDirty_ = true;
    };
    knobAttack_.slider->onValueChange = [this] {
        params_.attackLevel = static_cast<float>(knobAttack_.slider->getValue());
        curveDirty_ = true;
    };
    knobDecay_.slider->onValueChange = [this] {
        params_.decaySlope = static_cast<float>(knobDecay_.slider->getValue());
        curveDirty_ = true;
    };
    knobSustain_.slider->onValueChange = [this] {
        params_.sustainLevel = static_cast<float>(knobSustain_.slider->getValue());
        curveDirty_ = true;
    };
    knobGate_.slider->onValueChange = [this] {
        params_.gate = static_cast<float>(knobGate_.slider->getValue());
        curveDirty_ = true;
    };
}

void AutomationSequenceDialogComponent::doReset() {
    params_ = SequenceParams{};
    for (auto& s : params_.steps) s.active = true;
    modeSelector_->selectedMode = ShapeMode::Normal;
    modeSelector_->repaint();
    syncKnobsToParams();
    curveDirty_ = true;
    stepGrid_->repaint();
}

void AutomationSequenceDialogComponent::doRandomize() {
    juce::Random rng;
    const int N = params_.numSteps;
    for (int i = 0; i < N; ++i) {
        params_.steps[i].active            = rng.nextFloat() > 0.30f;
        params_.steps[i].probability       = 0.6f + rng.nextFloat() * 0.4f;
        params_.steps[i].localAttackLevel  = -1.0f;
        params_.steps[i].localDecaySlope   = -1.0f;
        params_.steps[i].localSustainLevel = -1.0f;
        params_.steps[i].localReleaseSlope = -1.0f;
    }
    params_.attackLevel  = 0.6f + rng.nextFloat() * 0.4f;
    params_.decaySlope   = rng.nextFloat();
    params_.sustainLevel = rng.nextFloat();
    params_.releaseSlope = rng.nextFloat();
    params_.gate         = 0.35f + rng.nextFloat() * 0.6f;
    params_.swing        = rng.nextFloat() * 0.40f;
    params_.seed         = static_cast<uint32_t>(rng.nextInt());

    syncKnobsToParams();
    curveDirty_ = true;
    stepGrid_->repaint();
}

void AutomationSequenceDialogComponent::doHumanize() {
    // Each press cycles through 4 humanize intensities then back to 0
    params_.humanize = std::fmod(params_.humanize + 0.25f, 1.001f);
    if (params_.humanize > 1.0f) params_.humanize = 0.0f;
    params_.seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
    curveDirty_ = true;
}

void AutomationSequenceDialogComponent::doAccept() {
    if (curveDirty_) { rebuildCurve(); curveDirty_ = false; }
    if (onAcceptCb_) onAcceptCb_(cachedCurve_, params_);
}

void AutomationSequenceDialogComponent::setInitialParams(const SequenceParams& p) {
    params_ = p;
    modeSelector_->selectedMode = p.mode;
    modeSelector_->repaint();
    syncKnobsToParams();
    curveDirty_ = true;
    if (stepGrid_) stepGrid_->repaint();
}

AutomationSequenceDialogComponent::LabeledKnob
AutomationSequenceDialogComponent::makeKnob(const juce::String& name,
                                             double mn, double mx, double def)
{
    LabeledKnob k;
    k.slider = std::make_unique<juce::Slider>(
        juce::Slider::RotaryHorizontalVerticalDrag,
        juce::Slider::NoTextBox);
    k.slider->setRange(mn, mx);
    k.slider->setValue(def, juce::dontSendNotification);
    k.slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    k.slider->setLookAndFeel(laf_.get());

    k.label = std::make_unique<juce::Label>("", name);
    k.label->setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
    k.label->setColour(juce::Label::textColourId, SequenceLAF::colSubtext());
    k.label->setJustificationType(juce::Justification::centred);

    return k;
}

} // namespace AutomationSeq
} // namespace APEX

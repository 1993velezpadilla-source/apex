// ============================================================
// AutomationSequenceDialogComponent.cpp  — v3 (Playground edition)
// APEX · Create Sequence  |  Source/AutomationSequence/
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
    setColour(juce::TextEditor::backgroundColourId, colGrid());
    setColour(juce::TextEditor::textColourId,    colText());
    setColour(juce::TextEditor::outlineColourId, colGridLight());
    setColour(juce::TextEditor::focusedOutlineColourId, colPink());
}

void SequenceLAF::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                    float sliderPos, float startAngle, float endAngle,
                                    juce::Slider& /*slider*/)
{
    const float radius = static_cast<float>(juce::jmin(w, h)) * 0.5f - 4.0f;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;

    juce::Path track;
    track.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(colGrid());
    g.strokePath(track, juce::PathStrokeType(3.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float vA = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path val;
    val.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, vA, true);
    g.setColour(colPink());
    g.strokePath(val, juce::PathStrokeType(3.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ir = radius * 0.62f;
    juce::ColourGradient body(colPanel().brighter(0.08f), cx, cy - ir,
                                colPanel().darker(0.20f),  cx, cy + ir, false);
    g.setGradientFill(body);
    g.fillEllipse(cx - ir, cy - ir, ir * 2, ir * 2);

    const float a = vA - juce::MathConstants<float>::halfPi;
    g.setColour(colPinkBright());
    g.drawLine(cx + std::cos(a) * ir * 0.35f, cy + std::sin(a) * ir * 0.35f,
                cx + std::cos(a) * ir * 0.92f, cy + std::sin(a) * ir * 0.92f, 2.2f);
}

void SequenceLAF::drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                                        const juce::Colour&, bool hi, bool dn)
{
    auto b = btn.getLocalBounds().toFloat().reduced(1.0f);
    juce::Colour fill = colGrid();
    if (btn.getButtonText() == "Accept") {
        fill = dn ? colAccept().darker(0.20f)
                  : hi ? colAccept() : colAccept().darker(0.10f);
    } else {
        fill = dn ? colPinkDim().brighter(0.10f)
                  : hi ? colGrid().brighter(0.20f) : colGrid();
    }
    g.setColour(fill);
    g.fillRoundedRectangle(b, 5.0f);
    g.setColour(hi ? colPink() : colGridLight());
    g.drawRoundedRectangle(b, 5.0f, 1.0f);
}

void SequenceLAF::drawButtonText(juce::Graphics& g, juce::TextButton& btn, bool hi, bool)
{
    juce::Colour tc = colText();
    if (btn.getButtonText() == "Accept") tc = juce::Colours::black;
    else if (hi) tc = colPinkBright();
    g.setColour(tc);
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
    return juce::jlimit(0, N - 1, static_cast<int>(pos.x / stepW));
}

void AutomationSequenceDialogComponent::StepGridComponent::paint(juce::Graphics& g) {
    if (!params_) return;
    const int N = juce::jlimit(1, kMaxSteps, params_->numSteps);
    const float w = getWidth(), h = getHeight();
    const float stepW = w / N;
    const float gap = 2.0f;
    for (int i = 0; i < N; ++i) {
        const bool act = params_->steps[i].active;
        juce::Rectangle<float> r(i * stepW + gap, gap, stepW - gap * 2, h - gap * 2);
        if (act) {
            juce::ColourGradient pillGrad(SequenceLAF::colPinkBright(), r.getX(), r.getY(),
                                            SequenceLAF::colPink(), r.getX(), r.getBottom(), false);
            g.setGradientFill(pillGrad);
            g.fillRoundedRectangle(r, 3.5f);
            juce::ColourGradient gl(juce::Colours::white.withAlpha(0.22f), r.getX(), r.getY(),
                                     juce::Colours::transparentWhite,
                                     r.getX(), r.getY() + r.getHeight() * 0.55f, false);
            g.setGradientFill(gl);
            g.fillRoundedRectangle(r.reduced(1.0f), 3.0f);
        } else {
            g.setColour(SequenceLAF::colGrid());
            g.fillRoundedRectangle(r, 3.5f);
            g.setColour(SequenceLAF::colPinkDim().withAlpha(0.45f));
            g.drawRoundedRectangle(r, 3.5f, 1.0f);
        }
        g.setColour(act ? juce::Colours::white.withAlpha(0.9f) : SequenceLAF::colSubtext());
        g.setFont(juce::Font("Inter", 9.5f, juce::Font::bold));
        g.drawText(juce::String(i + 1), r.toNearestInt(),
                   juce::Justification::centred, false);
    }
}

void AutomationSequenceDialogComponent::StepGridComponent::mouseDown(const juce::MouseEvent& e) {
    int idx = hitTestStep(e.position);
    if (idx < 0 || !params_) return;
    dragState_ = !params_->steps[idx].active;
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
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(b, 5.0f);

    // Step grid lines
    g.setColour(SequenceLAF::colGrid().withAlpha(0.55f));
    for (int s = 1; s < numSteps_; ++s) {
        float x = b.getX() + (s / (float)numSteps_) * b.getWidth();
        g.drawVerticalLine(static_cast<int>(x), b.getY() + 3.0f, b.getBottom() - 3.0f);
    }
    // Level lines
    g.setColour(SequenceLAF::colGrid().brighter(0.10f));
    for (float lv : { 0.25f, 0.5f, 0.75f }) {
        float y = b.getBottom() - lv * b.getHeight();
        g.drawHorizontalLine(static_cast<int>(y), b.getX() + 4.0f, b.getRight() - 4.0f);
    }

    if (!curve_) {
        g.setColour(SequenceLAF::colSubtext());
        g.setFont(12.0f);
        g.drawText("No active steps", b.toNearestInt(), juce::Justification::centred);
        return;
    }

    const int n = static_cast<int>(curve_->samples.size());
    juce::Path fill, line;
    bool started = false;
    for (int i = 0; i < n; ++i) {
        float x = b.getX() + (i / (float)(n - 1)) * b.getWidth();
        float y = b.getBottom() - 4.0f - curve_->samples[i] * (b.getHeight() - 8.0f);
        if (!started) {
            fill.startNewSubPath(x, b.getBottom() - 2.0f);
            fill.lineTo(x, y);
            line.startNewSubPath(x, y);
            started = true;
        } else {
            fill.lineTo(x, y);
            line.lineTo(x, y);
        }
    }
    fill.lineTo(b.getRight(), b.getBottom() - 2.0f);
    fill.closeSubPath();

    juce::ColourGradient grad(accentColour_.withAlpha(0.50f), b.getX(), b.getY(),
                                accentColour_.withAlpha(0.04f), b.getX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillPath(fill);

    g.setColour(accentColour_);
    g.strokePath(line, juce::PathStrokeType(1.8f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (playheadPhase_ > 0.0f && n > 1) {
        const float phX = b.getX() + playheadPhase_ * b.getWidth();
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawVerticalLine(static_cast<int>(phX), b.getY() + 4.0f, b.getBottom() - 4.0f);
        const int idx = juce::jlimit(0, n - 1, static_cast<int>(playheadPhase_ * (n - 1)));
        const float phY = b.getBottom() - 4.0f - curve_->samples[idx] * (b.getHeight() - 8.0f);
        g.setColour(accentColour_);
        g.fillEllipse(phX - 4.0f, phY - 4.0f, 8.0f, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(phX - 4.0f, phY - 4.0f, 8.0f, 8.0f, 1.2f);
    }

    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
}

// ============================================================
// BarViewComponent
// ============================================================
void AutomationSequenceDialogComponent::BarViewComponent::setBarValues(
    const std::array<float, kMaxSteps>& v, int n)
{
    values_   = v;
    numSteps_ = juce::jlimit(1, kMaxSteps, n);
    repaint();
}

int AutomationSequenceDialogComponent::BarViewComponent::barHitTest(juce::Point<float> pos) const {
    return juce::jlimit(0, numSteps_ - 1,
        static_cast<int>(pos.x / ((float)getWidth() / numSteps_)));
}
float AutomationSequenceDialogComponent::BarViewComponent::yToValue(float y) const {
    return juce::jlimit(0.0f, 1.0f, 1.0f - (y / (float)getHeight()));
}

void AutomationSequenceDialogComponent::BarViewComponent::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(b, 5.0f);
    const float barW = b.getWidth() / numSteps_;
    const float gap = 2.0f;
    for (int i = 0; i < numSteps_; ++i) {
        const float v = values_[i];
        const float h = v * (b.getHeight() - 8.0f);
        const float x = i * barW + gap;
        const float y = b.getBottom() - 4.0f - h;
        juce::Rectangle<float> r(x, y, barW - gap * 2, std::max(h, 0.0f));
        if (h < 1.0f) {
            g.setColour(SequenceLAF::colPinkDim().withAlpha(0.45f));
            g.fillRect(juce::Rectangle<float>(x, b.getBottom() - 6.0f, barW - gap * 2, 2.0f));
            continue;
        }
        juce::ColourGradient bg(SequenceLAF::colPinkBright(), r.getX(), r.getY(),
                                  SequenceLAF::colPinkDim(),    r.getX(), r.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.20f));
        g.fillRect(juce::Rectangle<float>(r.getX(), r.getY(),
                                            r.getWidth(), std::min(3.0f, r.getHeight())));
    }
    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(b.reduced(0.5f), 5.0f, 1.0f);
}

void AutomationSequenceDialogComponent::BarViewComponent::mouseDown(const juce::MouseEvent& e) {
    int idx = barHitTest(e.position);
    if (idx >= 0 && onBarDragged) onBarDragged(idx, yToValue(e.position.y));
}
void AutomationSequenceDialogComponent::BarViewComponent::mouseDrag(const juce::MouseEvent& e) {
    int idx = barHitTest(e.position);
    if (idx >= 0 && onBarDragged) onBarDragged(idx, yToValue(e.position.y));
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
        juce::Rectangle<float> r(i * tabW + 1.0f, 1.0f, tabW - 2.0f, b.getHeight() - 2.0f);
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
    int idx = juce::jlimit(0, 3, static_cast<int>(e.position.x / (getWidth() / 4.0f)));
    selectedMode = static_cast<BarViewMode>(idx);
    repaint();
    if (onModeChanged) onModeChanged(selectedMode);
}

// ============================================================
// CategoryBrowserComponent
//
// Layout (component is exactly 220px wide, ~130px tall):
//   Rows 1–3: category pills in a 3 × 4 grid (one empty cell)
//   Rows 4–5: shape pills for the selected category, 2 × 4 grid
// ============================================================
juce::Rectangle<int>
AutomationSequenceDialogComponent::CategoryBrowserComponent::catPillBounds(int catIdx) const {
    const int col = catIdx % 4;
    const int row = catIdx / 4;
    const float w = getWidth() / 4.0f;
    const float h = 22.0f;
    return juce::Rectangle<float>(col * w + 1.0f, row * (h + 2.0f) + 1.0f,
                                    w - 2.0f, h).toNearestInt();
}

juce::Rectangle<int>
AutomationSequenceDialogComponent::CategoryBrowserComponent::shapePillBounds(int shapeIdx) const {
    const int col = shapeIdx % 4;
    const int row = shapeIdx / 4;
    const float w = getWidth() / 4.0f;
    const float h = 22.0f;
    const float yOffset = 3 * (h + 2.0f) + 6.0f;  // below 3 cat rows + gap
    return juce::Rectangle<float>(col * w + 1.0f,
                                    yOffset + row * (h + 2.0f) + 1.0f,
                                    w - 2.0f, h).toNearestInt();
}

int AutomationSequenceDialogComponent::CategoryBrowserComponent::hitTestCat(juce::Point<float> pos) const {
    for (int i = 0; i < static_cast<int>(ShapeCategory::kCount); ++i)
        if (catPillBounds(i).toFloat().contains(pos)) return i;
    return -1;
}

int AutomationSequenceDialogComponent::CategoryBrowserComponent::hitTestShape(juce::Point<float> pos) const {
    const auto& info = kCategoryInfo[static_cast<int>(selectedCategory)];
    for (int i = 0; i < info.modeCount; ++i)
        if (shapePillBounds(i).toFloat().contains(pos)) return i;
    return -1;
}

void AutomationSequenceDialogComponent::CategoryBrowserComponent::paint(juce::Graphics& g) {
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

    // Category pills
    for (int i = 0; i < static_cast<int>(ShapeCategory::kCount); ++i) {
        const auto& info = kCategoryInfo[i];
        const auto r = catPillBounds(i).toFloat();
        const bool sel = (info.category == selectedCategory);
        const bool hov = (i == hoveredCat);
        const auto col = SequenceLAF::colForCategory(info.category);

        if (sel) {
            juce::ColourGradient grad(col.brighter(0.10f), r.getX(), r.getY(),
                                        col.darker(0.20f),  r.getX(), r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(r, 3.0f);
        } else {
            g.setColour(hov ? SequenceLAF::colGrid().brighter(0.30f)
                            : SequenceLAF::colGrid());
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(col.withAlpha(0.45f));
            g.drawRoundedRectangle(r, 3.0f, 1.0f);
        }
        g.setColour(sel ? juce::Colours::black : SequenceLAF::colText());
        g.setFont(juce::Font("Inter", 9.5f, juce::Font::bold));
        g.drawFittedText(info.label, r.toNearestInt(), juce::Justification::centred, 1);
    }

    // Shape pills for selected category
    const auto& info = kCategoryInfo[static_cast<int>(selectedCategory)];
    const auto catCol = SequenceLAF::colForCategory(info.category);
    for (int i = 0; i < info.modeCount; ++i) {
        const auto r   = shapePillBounds(i).toFloat();
        const auto mode = info.modes[i];
        const bool sel = (mode == selectedMode);
        const bool hov = (i == hoveredShape);

        if (sel) {
            juce::ColourGradient gd(SequenceLAF::colPinkBright(), r.getX(), r.getY(),
                                      SequenceLAF::colPink(),       r.getX(), r.getBottom(), false);
            g.setGradientFill(gd);
            g.fillRoundedRectangle(r, 3.0f);
        } else {
            g.setColour(hov ? SequenceLAF::colGrid().brighter(0.25f)
                            : SequenceLAF::colGrid());
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(catCol.withAlpha(0.30f));
            g.drawRoundedRectangle(r, 3.0f, 1.0f);
        }
        g.setColour(sel ? juce::Colours::black : SequenceLAF::colText());
        g.setFont(juce::Font("Inter", 9.0f, sel ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(getShapeLabel(mode), r.toNearestInt(),
                         juce::Justification::centred, 1);
    }

    g.setColour(SequenceLAF::colGridLight());
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
}

void AutomationSequenceDialogComponent::CategoryBrowserComponent::mouseDown(const juce::MouseEvent& e) {
    int ci = hitTestCat(e.position);
    if (ci >= 0) {
        selectedCategory = static_cast<ShapeCategory>(ci);
        repaint();
        return;
    }
    int si = hitTestShape(e.position);
    if (si >= 0) {
        const auto& info = kCategoryInfo[static_cast<int>(selectedCategory)];
        selectedMode = info.modes[si];
        repaint();
        if (onShapeSelected) onShapeSelected(selectedMode);
    }
}

void AutomationSequenceDialogComponent::CategoryBrowserComponent::mouseMove(const juce::MouseEvent& e) {
    const int newCat   = hitTestCat  (e.position);
    const int newShape = hitTestShape(e.position);
    if (newCat != hoveredCat || newShape != hoveredShape) {
        hoveredCat   = newCat;
        hoveredShape = newShape;
        repaint();
    }
}

void AutomationSequenceDialogComponent::CategoryBrowserComponent::mouseExit(const juce::MouseEvent&) {
    if (hoveredCat != -1 || hoveredShape != -1) {
        hoveredCat = hoveredShape = -1;
        repaint();
    }
}

// ============================================================
// AutomationSequenceDialogComponent — constructor / lifecycle
// ============================================================
AutomationSequenceDialogComponent::AutomationSequenceDialogComponent(
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onAccept,
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onApply,
    std::function<void(const GeneratedCurve&, const SequenceParams&)> onApplyAll,
    std::function<void()> onCancel)
    : onAcceptCb_(std::move(onAccept))
    , onApplyCb_(std::move(onApply))
    , onApplyAllCb_(std::move(onApplyAll))
    , onCancelCb_(std::move(onCancel))
{
    laf_ = std::make_unique<SequenceLAF>();
    setLookAndFeel(laf_.get());
    setInterceptsMouseClicks(true, true);

    for (auto& s : params_.steps) s.active = true;

    // Step grid
    stepGrid_ = std::make_unique<StepGridComponent>();
    stepGrid_->setParams(&params_);
    stepGrid_->onStepToggled = [this](int idx, bool st) {
        if (idx < 0 || idx >= kMaxSteps) return;
        params_.steps[idx].active = st;
        curveDirty_ = true;
        stepGrid_->repaint();
    };
    addAndMakeVisible(*stepGrid_);

    // Curve preview
    curvePreview_ = std::make_unique<CurvePreviewComponent>();
    curvePreview_->setNumSteps(params_.numSteps);
    addAndMakeVisible(*curvePreview_);

    // Bar tabs + bar view
    barTabs_ = std::make_unique<BarTabsComponent>();
    addAndMakeVisible(*barTabs_);

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
        barView_->activeMode = m;
        refreshBarView();
    };

    // Category browser
    catBrowser_ = std::make_unique<CategoryBrowserComponent>();
    catBrowser_->selectedCategory = getCategoryForMode(params_.mode);
    catBrowser_->selectedMode     = params_.mode;
    catBrowser_->onShapeSelected  = [this](ShapeMode m) {
        params_.mode = m;
        curveDirty_ = true;
        curvePreview_->setAccentColour(
            SequenceLAF::colForCategory(getCategoryForMode(m)).withAlpha(1.0f));
    };
    addAndMakeVisible(*catBrowser_);

    curvePreview_->setAccentColour(
        SequenceLAF::colForCategory(getCategoryForMode(params_.mode)).withAlpha(1.0f));

    // Knobs
    knobTimeMul_ = makeKnob("Time mul", 0.0, 1.0, 0.5);
    knobSwing_   = makeKnob("Swing",    0.0, 1.0, 0.0);
    knobAttack_  = makeKnob("Attack",   0.0, 1.0, 1.0);
    knobDecay_   = makeKnob("Decay",    0.0, 1.0, 0.35);
    knobSustain_ = makeKnob("Sustain",  0.0, 1.0, 0.55);
    knobGate_    = makeKnob("Gate",     0.0, 1.0, 0.75);
    knobChaos_   = makeKnob("Chaos",    0.0, 1.0, 0.0);
    knobRepeat_  = makeKnob("Repeat",   1.0, 8.0, 1.0);
    for (auto* k : { &knobTimeMul_, &knobSwing_, &knobAttack_, &knobDecay_,
                     &knobSustain_, &knobGate_, &knobChaos_, &knobRepeat_ }) {
        addAndMakeVisible(*k->slider);
        addAndMakeVisible(*k->label);
    }
    hookKnobCallbacks();

    // Buttons
    btnReset_     = std::make_unique<juce::TextButton>("Reset");
    btnRandomize_ = std::make_unique<juce::TextButton>("Randomize");
    btnLibrary_   = std::make_unique<juce::TextButton>("Library");
    btnSave_      = std::make_unique<juce::TextButton>("Save");
    btnAccept_    = std::make_unique<juce::TextButton>("Accept");

    btnReset_    ->onClick = [this] { doReset();       };
    btnRandomize_->onClick = [this] { showRandomizeMenu(); };
    btnLibrary_  ->onClick = [this] { doShowLibrary(); };
    btnSave_     ->onClick = [this] { doSave();        };
    btnAccept_   ->onClick = [this] { doAccept();      };

    addAndMakeVisible(*btnReset_);
    addAndMakeVisible(*btnRandomize_);
    addAndMakeVisible(*btnLibrary_);
    addAndMakeVisible(*btnSave_);
    addAndMakeVisible(*btnAccept_);

    rebuildCurve();
    setSize(kDialogW, kDialogH);
    startTimerHz(60);
}

AutomationSequenceDialogComponent::~AutomationSequenceDialogComponent() {
    stopTimer();
    if (libraryWindow_ != nullptr) libraryWindow_->exitModalState(0);
    if (saveDialog_    != nullptr) saveDialog_   ->exitModalState(0);
    setLookAndFeel(nullptr);
}

void AutomationSequenceDialogComponent::timerCallback() {
    if (curveDirty_) {
        rebuildCurve();
        curveDirty_ = false;
    }
    if (animPlaying_) {
        playheadPhase_ += 1.0f / 240.0f;
        if (playheadPhase_ > 1.0f) playheadPhase_ -= 1.0f;
        curvePreview_->setPlayheadPhase(playheadPhase_);
    }
}

// ============================================================
// Layout
// ============================================================
juce::Rectangle<int> AutomationSequenceDialogComponent::leftArea() const {
    return juce::Rectangle<int>(8, 36, getWidth() - 248, getHeight() - 44);
}

juce::Rectangle<int> AutomationSequenceDialogComponent::rightPanel() const {
    return juce::Rectangle<int>(getWidth() - 232, 36, 224, getHeight() - 44);
}

void AutomationSequenceDialogComponent::resized() {
    auto left  = leftArea();
    auto right = rightPanel();

    // ---- Left column ------------------------------------
    stepGrid_->setBounds(left.removeFromTop(50));
    left.removeFromTop(6);

    curvePreview_->setBounds(left.removeFromTop(330));
    left.removeFromTop(6);

    barTabs_->setBounds(left.removeFromTop(26));
    left.removeFromTop(2);

    barView_->setBounds(left.removeFromTop(90));
    left.removeFromTop(6);

    auto btnRow = left.removeFromTop(36);
    const int gap = 6;
    const int btnW = (btnRow.getWidth() - gap * 3) / 4;
    btnReset_    ->setBounds(btnRow.removeFromLeft(btnW)); btnRow.removeFromLeft(gap);
    btnRandomize_->setBounds(btnRow.removeFromLeft(btnW)); btnRow.removeFromLeft(gap);
    btnLibrary_  ->setBounds(btnRow.removeFromLeft(btnW)); btnRow.removeFromLeft(gap);
    btnSave_     ->setBounds(btnRow);

    // ---- Right panel ------------------------------------
    catBrowser_->setBounds(right.removeFromTop(130));
    right.removeFromTop(6);

    // 8 knobs in 2 columns × 4 rows
    auto knobsArea = right.removeFromTop(4 * 64 + 3 * 2);   // 268
    const auto rowH = 64;
    LabeledKnob* col1[] = { &knobTimeMul_, &knobAttack_,  &knobSustain_, &knobChaos_  };
    LabeledKnob* col2[] = { &knobSwing_,   &knobDecay_,   &knobGate_,    &knobRepeat_ };

    for (int r = 0; r < 4; ++r) {
        auto row = knobsArea.removeFromTop(rowH);
        if (r < 3) knobsArea.removeFromTop(2);
        const int colW = row.getWidth() / 2;
        auto cellA = row.removeFromLeft(colW);
        auto cellB = row;

        auto layout = [](LabeledKnob* k, juce::Rectangle<int> cell) {
            cell.reduce(4, 2);
            auto labelRow = cell.removeFromBottom(15);
            k->label ->setBounds(labelRow);
            k->slider->setBounds(cell);
        };
        layout(col1[r], cellA);
        layout(col2[r], cellB);
    }

    right.removeFromTop(6);
    btnAccept_->setBounds(getWidth() - 232, getHeight() - 42, 224, 34);
}

void AutomationSequenceDialogComponent::paint(juce::Graphics& g) {
    g.setColour(SequenceLAF::colBg());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

    // Title bar
    auto title = getLocalBounds().toFloat().withHeight(30.0f);
    juce::ColourGradient grad(SequenceLAF::colPanel().brighter(0.08f),
                                title.getX(), title.getY(),
                                SequenceLAF::colPanel(),
                                title.getX(), title.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(title, 8.0f);
    g.fillRect(title.withTop(title.getBottom() - 8.0f));

    g.setColour(SequenceLAF::colPinkBright());
    g.setFont(juce::Font("Inter", 12.5f, juce::Font::bold));
    g.drawText("Create Sequence", title.toNearestInt(),
               juce::Justification::centred);

    // Right side of title bar: unique seed counter
    g.setColour(SequenceLAF::colSubtext());
    g.setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
    g.drawText(juce::String(HistoryCore::getUniqueSeedCount()) + " unique patterns generated",
               title.toNearestInt().reduced(10, 0),
               juce::Justification::centredRight);

    // Right panel separator
    g.setColour(SequenceLAF::colGridLight());
    g.drawVerticalLine(getWidth() - 240, 36.0f, (float)(getHeight() - 8));

    // Outer border
    g.setColour(SequenceLAF::colPinkDim().withAlpha(0.55f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.0f, 1.5f);
}

// ============================================================
// Internal methods
// ============================================================
void AutomationSequenceDialogComponent::rebuildCurve() {
    cachedCurve_ = GeneratorCore::generate(params_);
    curvePreview_->setCurve(&cachedCurve_);
    curvePreview_->setNumSteps(params_.numSteps);
    refreshBarView();
}

void AutomationSequenceDialogComponent::refreshBarView() {
    auto v = GeneratorCore::barValues(params_, barTabs_->selectedMode);
    barView_->setBarValues(v, params_.numSteps);
}

void AutomationSequenceDialogComponent::syncKnobsToParams() {
    knobTimeMul_.slider->setValue(params_.timeMul,      juce::dontSendNotification);
    knobSwing_  .slider->setValue(params_.swing,        juce::dontSendNotification);
    knobAttack_ .slider->setValue(params_.attackLevel,  juce::dontSendNotification);
    knobDecay_  .slider->setValue(params_.decaySlope,   juce::dontSendNotification);
    knobSustain_.slider->setValue(params_.sustainLevel, juce::dontSendNotification);
    knobGate_   .slider->setValue(params_.gate,         juce::dontSendNotification);
    knobChaos_  .slider->setValue(params_.chaos,        juce::dontSendNotification);
    knobRepeat_ .slider->setValue(params_.repeatCount,  juce::dontSendNotification);
}

void AutomationSequenceDialogComponent::hookKnobCallbacks() {
    knobTimeMul_.slider->onValueChange = [this] {
        params_.timeMul = (float) knobTimeMul_.slider->getValue();
        curveDirty_ = true;
    };
    knobSwing_.slider->onValueChange = [this] {
        params_.swing = (float) knobSwing_.slider->getValue();
        curveDirty_ = true;
    };
    knobAttack_.slider->onValueChange = [this] {
        params_.attackLevel = (float) knobAttack_.slider->getValue();
        curveDirty_ = true;
    };
    knobDecay_.slider->onValueChange = [this] {
        params_.decaySlope = (float) knobDecay_.slider->getValue();
        curveDirty_ = true;
    };
    knobSustain_.slider->onValueChange = [this] {
        params_.sustainLevel = (float) knobSustain_.slider->getValue();
        curveDirty_ = true;
    };
    knobGate_.slider->onValueChange = [this] {
        params_.gate = (float) knobGate_.slider->getValue();
        curveDirty_ = true;
    };
    knobChaos_.slider->onValueChange = [this] {
        params_.chaos = (float) knobChaos_.slider->getValue();
        curveDirty_ = true;
    };
    knobRepeat_.slider->onValueChange = [this] {
        params_.repeatCount = juce::jlimit(1, 8,
            static_cast<int>(std::round(knobRepeat_.slider->getValue())));
        curveDirty_ = true;
    };
}

void AutomationSequenceDialogComponent::doReset() {
    params_ = SequenceParams{};
    for (auto& s : params_.steps) s.active = true;
    catBrowser_->selectedCategory = ShapeCategory::Envelope;
    catBrowser_->selectedMode     = ShapeMode::Normal;
    catBrowser_->repaint();
    curvePreview_->setAccentColour(SequenceLAF::colForCategory(ShapeCategory::Envelope));
    syncKnobsToParams();
    curveDirty_ = true;
    stepGrid_->repaint();
}

void AutomationSequenceDialogComponent::doRandomize() {
    // Default: complete randomized pattern
    applyRandomizedParams(RandomizerCore::randomize(RandomFlavor::Everything));
}

void AutomationSequenceDialogComponent::showRandomizeMenu() {
    juce::PopupMenu m;
    for (int i = 0; i < static_cast<int>(RandomFlavor::kCount); ++i) {
        const auto f = static_cast<RandomFlavor>(i);
        m.addItem(getRandomFlavorMenuLabel(f),
            [this, f] { applyRandomizedParams(RandomizerCore::randomize(f)); });
    }
    m.addSeparator();
    m.addItem("Clear seed history (reset uniqueness)", [this] {
        HistoryCore::clearSeedHistory();
        repaint();   // counter in title updates
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(btnRandomize_.get()));
}

void AutomationSequenceDialogComponent::applyRandomizedParams(const SequenceParams& p) {
    params_ = p;
    catBrowser_->selectedCategory = getCategoryForMode(p.mode);
    catBrowser_->selectedMode     = p.mode;
    catBrowser_->repaint();
    curvePreview_->setAccentColour(SequenceLAF::colForCategory(getCategoryForMode(p.mode)));
    syncKnobsToParams();
    curveDirty_ = true;
    stepGrid_->repaint();
    repaint();   // title counter
}

void AutomationSequenceDialogComponent::loadParamsFromLibrary(const SequenceParams& p) {
    applyRandomizedParams(p);
}

void AutomationSequenceDialogComponent::doShowLibrary() {
    auto* lib = new LibraryComponent();
    lib->onPatternSelected = [this](const SequenceParams& p) {
        loadParamsFromLibrary(p);
        if (libraryWindow_ != nullptr) libraryWindow_->exitModalState(0);
    };
    lib->onPatternApply = [this](const SequenceParams& p, bool applyToAll) {
        loadParamsFromLibrary(p);
        if (curveDirty_) { rebuildCurve(); curveDirty_ = false; }
        if (applyToAll) {
            if (onApplyAllCb_) onApplyAllCb_(cachedCurve_, params_);
        } else {
            if (onApplyCb_) onApplyCb_(cachedCurve_, params_);
        }
    };
    lib->onClose = [this] {
        if (libraryWindow_ != nullptr) libraryWindow_->exitModalState(0);
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(lib);
    opts.dialogTitle             = "";
    opts.dialogBackgroundColour  = SequenceLAF::colBg();
    opts.useNativeTitleBar       = false;
    opts.resizable               = false;
    opts.componentToCentreAround = this;
    libraryWindow_ = opts.launchAsync();
}

void AutomationSequenceDialogComponent::doSave() {
    auto* aw = new juce::AlertWindow("Save Pattern",
        "Enter a name for this pattern:",
        juce::AlertWindow::NoIcon, this);
    aw->setLookAndFeel(laf_.get());
    aw->addTextEditor("name", "", "Name");
    aw->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog_ = aw;

    SequenceParams pCopy = params_;
    aw->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, aw, pCopy](int result) {
            std::unique_ptr<juce::AlertWindow> owner(aw);
            if (result != 1) return;
            const auto rawName = aw->getTextEditorContents("name").trim();
            if (rawName.isEmpty()) return;

            // Sanitize filename
            const auto safe = juce::File::createLegalFileName(rawName);
            if (safe.isEmpty()) return;

            if (HistoryCore::patternExists(safe)) {
                auto* conf = new juce::AlertWindow("Overwrite?",
                    "A pattern named \"" + safe + "\" already exists. Overwrite?",
                    juce::AlertWindow::WarningIcon, this);
                conf->setLookAndFeel(laf_.get());
                conf->addButton("Overwrite", 1);
                conf->addButton("Cancel",    0);
                conf->enterModalState(true, juce::ModalCallbackFunction::create(
                    [safe, pCopy, conf](int r) {
                        std::unique_ptr<juce::AlertWindow> own(conf);
                        if (r == 1) HistoryCore::savePattern(safe, pCopy);
                    }), false);
            } else {
                HistoryCore::savePattern(safe, pCopy);
            }
        }), false);
}

void AutomationSequenceDialogComponent::doAccept() {
    if (curveDirty_) { rebuildCurve(); curveDirty_ = false; }
    if (onAcceptCb_) onAcceptCb_(cachedCurve_, params_);
}

void AutomationSequenceDialogComponent::setInitialParams(const SequenceParams& p) {
    params_ = p;
    catBrowser_->selectedCategory = getCategoryForMode(p.mode);
    catBrowser_->selectedMode     = p.mode;
    catBrowser_->repaint();
    curvePreview_->setAccentColour(SequenceLAF::colForCategory(getCategoryForMode(p.mode)));
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
                                   juce::MathConstants<float>::pi * 2.8f, true);
    k.slider->setLookAndFeel(laf_.get());

    k.label = std::make_unique<juce::Label>("", name);
    k.label->setFont(juce::Font("Inter", 10.0f, juce::Font::plain));
    k.label->setColour(juce::Label::textColourId, SequenceLAF::colSubtext());
    k.label->setJustificationType(juce::Justification::centred);
    return k;
}

} // namespace AutomationSeq
} // namespace APEX

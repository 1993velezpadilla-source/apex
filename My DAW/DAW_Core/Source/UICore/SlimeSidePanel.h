#pragma once

#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

class MixerPanel;

class SlimeSidePanel : public juce::Component, private juce::Timer
{
public:
    enum class VisualState { ClosedDrip, Opening, OpenPuddle, Closing };
    enum class AttachmentMode { AttachedToMixer, Detached };

    explicit SlimeSidePanel(juce::Component* fxContent);
    ~SlimeSidePanel() override = default;

    void toggle();
    void closePanel();
    void openPanel();
    bool isOpen() const noexcept { return state_ == VisualState::OpenPuddle || state_ == VisualState::Opening; }
    VisualState getState() const noexcept { return state_; }
    AttachmentMode getAttachmentMode() const noexcept { return attachmentMode_; }
    bool isAttached() const noexcept { return attachmentMode_ == AttachmentMode::AttachedToMixer; }

    void setAttachmentMode(AttachmentMode m);
    void toggleAttachment();

    void setMixerPanel(MixerPanel* mp) { mixerPanel_ = mp; }
    void setAnimationState(float t);

    juce::Rectangle<int> getContentArea() const noexcept { return contentArea_; }
    juce::ComponentDragger& getDragger() { return dragger_; }

    std::function<void(bool open)> onStateChanged;
    std::function<void()> onCloseClicked;
    std::function<void(bool attached)> onAttachmentChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool hitTest(int x, int y) override;

    static constexpr int kClosedW = 34;
    static constexpr int kClosedH = 60;
    static constexpr int kOpenW = 320;
    static constexpr int kOpenH = 370;

private:
    void timerCallback() override;

    static constexpr int kNumPts = 20;
    static constexpr int kAttachPtIndex = 0;

    struct PtArray { juce::Point<float> pts[kNumPts]; };

    PtArray closedPts_;
    PtArray openPts_;
    PtArray currentPts_;
    juce::Path currentPath_;
    juce::ComponentDragger dragger_;

    void defineClosedPoints(PtArray& p);
    void defineOpenPoints(PtArray& p);
    void interpolatePoints(float t);
    void rebuildPath();
    void syncBoundsAndContent();

    VisualState state_ = VisualState::ClosedDrip;
    AttachmentMode attachmentMode_ = AttachmentMode::AttachedToMixer;
    float animationT_ = 0.0f;
    juce::Rectangle<int> contentArea_;
    juce::Component* fxContent_ = nullptr;
    MixerPanel* mixerPanel_ = nullptr;
    juce::Point<int> lastMousePos_;
    bool wasDrag_ = false;

    static constexpr float kAnimDurationSec = 0.3f;
    static constexpr int kTimerIntervalMs = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlimeSidePanel)
};

inline SlimeSidePanel::SlimeSidePanel(juce::Component* fxContent)
    : fxContent_(fxContent)
{
    jassert(fxContent_ != nullptr);
    setOpaque(false);
    fxContent_->setVisible(false);
    addChildComponent(fxContent_);
    defineClosedPoints(closedPts_);
    defineOpenPoints(openPts_);
    interpolatePoints(0.0f);
    rebuildPath();
    setSize(kClosedW, kClosedH);
}

inline void SlimeSidePanel::toggle()
{
    if (state_ == VisualState::OpenPuddle || state_ == VisualState::Opening)
    {
        state_ = VisualState::Closing;
        animationT_ = 1.0f;
    }
    else
    {
        state_ = VisualState::Opening;
        animationT_ = 0.0f;
        setSize(kOpenW, kOpenH);
    }
    startTimer(kTimerIntervalMs);
}

inline void SlimeSidePanel::closePanel()
{
    if (state_ == VisualState::OpenPuddle || state_ == VisualState::Opening)
    {
        state_ = VisualState::Closing;
        animationT_ = 1.0f;
        startTimer(kTimerIntervalMs);
    }
}

inline void SlimeSidePanel::openPanel()
{
    if (state_ == VisualState::ClosedDrip)
    {
        state_ = VisualState::Opening;
        animationT_ = 0.0f;
        setSize(kOpenW, kOpenH);
        startTimer(kTimerIntervalMs);
    }
}

inline void SlimeSidePanel::setAttachmentMode(AttachmentMode m)
{
    if (attachmentMode_ != m)
    {
        attachmentMode_ = m;
        if (onAttachmentChanged) onAttachmentChanged(m == AttachmentMode::AttachedToMixer);
    }
}

inline void SlimeSidePanel::toggleAttachment()
{
    if (attachmentMode_ == AttachmentMode::AttachedToMixer)
    {
        attachmentMode_ = AttachmentMode::Detached;
        if (onAttachmentChanged) onAttachmentChanged(false);
    }
    else
    {
        attachmentMode_ = AttachmentMode::AttachedToMixer;
        if (onAttachmentChanged) onAttachmentChanged(true);
    }
}

inline void SlimeSidePanel::setAnimationState(float t)
{
    animationT_ = juce::jlimit(0.0f, 1.0f, t);
    if (t >= 1.0f)
    {
        state_ = VisualState::OpenPuddle;
        setSize(kOpenW, kOpenH);
    }
    else
    {
        state_ = VisualState::ClosedDrip;
        setSize(kClosedW, kClosedH);
    }
    interpolatePoints(animationT_);
    rebuildPath();
    syncBoundsAndContent();
    repaint();
    if (onStateChanged) onStateChanged(state_ == VisualState::OpenPuddle);
}

inline void SlimeSidePanel::defineClosedPoints(PtArray& p)
{
    const float pts[kNumPts][2] = {
        {0, 20}, {4, 10}, {12, 12}, {20, 18},
        {26, 26}, {28, 34}, {24, 42}, {18, 48},
        {10, 50}, {4, 46}, {0, 38}, {0, 28},
        {0, 20}, {0, 20}, {0, 20}, {0, 20},
        {0, 20}, {0, 20}, {0, 20}, {0, 20}
    };
    for (int i = 0; i < kNumPts; ++i)
        p.pts[i] = { pts[i][0], pts[i][1] };
}

inline void SlimeSidePanel::defineOpenPoints(PtArray& p)
{
    const float pts[kNumPts][2] = {
        {0, 20}, {8, 6}, {32, 4}, {70, 8},
        {120, 14}, {180, 22}, {240, 34}, {288, 52},
        {310, 82}, {312, 140}, {308, 196}, {298, 248},
        {274, 288}, {238, 318}, {194, 340}, {146, 346},
        {100, 338}, {60, 318}, {28, 282}, {8, 238}
    };
    for (int i = 0; i < kNumPts; ++i)
        p.pts[i] = { pts[i][0], pts[i][1] };
}

inline void SlimeSidePanel::interpolatePoints(float t)
{
    const float tt = juce::jlimit(0.0f, 1.0f, t);
    for (int i = 0; i < kNumPts; ++i)
    {
        currentPts_.pts[i].x = closedPts_.pts[i].x + (openPts_.pts[i].x - closedPts_.pts[i].x) * tt;
        currentPts_.pts[i].y = closedPts_.pts[i].y + (openPts_.pts[i].y - closedPts_.pts[i].y) * tt;
    }
}

inline void SlimeSidePanel::rebuildPath()
{
    currentPath_.clear();
    auto& p = currentPath_;
    p.startNewSubPath(currentPts_.pts[0]);

    const float tension = 0.28f;
    for (int i = 0; i < kNumPts; ++i)
    {
        const auto& curr = currentPts_.pts[i];
        const auto& next = currentPts_.pts[(i + 1) % kNumPts];
        const auto& prev = currentPts_.pts[(i - 1 + kNumPts) % kNumPts];
        const auto& afterNext = currentPts_.pts[(i + 2) % kNumPts];

        const float cpx1 = curr.x + (next.x - prev.x) * tension;
        const float cpy1 = curr.y + (next.y - prev.y) * tension;
        const float cpx2 = next.x - (afterNext.x - curr.x) * tension;
        const float cpy2 = next.y - (afterNext.y - curr.y) * tension;

        p.cubicTo(cpx1, cpy1, cpx2, cpy2, next.x, next.y);
    }
    p.closeSubPath();
}

inline void SlimeSidePanel::syncBoundsAndContent()
{
    bool showContent = (state_ == VisualState::OpenPuddle || state_ == VisualState::Opening);
    if (fxContent_)
    {
        const int inset = 14;
        const int bottomReserve = 52; // curved area reserved for Add button
        contentArea_ = { inset, inset + 2, kOpenW - inset * 2, kOpenH - inset * 2 - bottomReserve };
        fxContent_->setBounds(contentArea_);
        fxContent_->setVisible(showContent);
    }
}

inline void SlimeSidePanel::paint(juce::Graphics& g)
{
    auto& t = Theme::getInstance();

    // Closed-state pop: the drip must stay visible/noticeable when the panel
    // is collapsed. Keyed off the ClosedDrip state (or the tail of the closing
    // animation) so the emphasis appears as soon as the puddle finishes
    // closing — not just on the very next frame.
    const bool closedDrip = (state_ == VisualState::ClosedDrip || animationT_ < 0.1f);

    {
        // APEX deep-navy slime body — slim/flowing geometry preserved
        auto& a = t.apex;
        juce::ColourGradient bodyGrad(
            a.color.panelB.brighter(0.06f), 0, 0,
            a.color.deepestA, 0, (float)getHeight(), false);
        bodyGrad.addColour(0.25f, a.color.panelA);
        bodyGrad.addColour(0.65f, a.color.deepestB);
        g.setGradientFill(bodyGrad);
        g.fillPath(currentPath_);
    }

    if (closedDrip)
    {
        // Soft violet radial glow behind the drip — fill lift.
        auto b = currentPath_.getBounds();
        const float glowR = juce::jmax(b.getWidth(), b.getHeight()) * 0.75f;
        juce::ColourGradient glow(
            t.apex.color.violet.withAlpha(0.10f),
            b.getCentreX(), b.getCentreY(),
            t.apex.color.violet.withAlpha(0.0f),
            b.getCentreX() + glowR, b.getCentreY(),
            false);
        g.setGradientFill(glow);
        g.fillPath(currentPath_);
    }

    if (closedDrip)
    {
        // Closed-state emphasis — outer halo, stronger outline, inner accent.
        g.setColour(t.apex.color.violet.withAlpha(0.18f));
        g.strokePath(currentPath_, juce::PathStrokeType(3.5f)); // soft halo

        g.setColour(t.apex.color.violet.withAlpha(0.55f));
        g.strokePath(currentPath_, juce::PathStrokeType(1.5f)); // main outline

        g.setColour(t.apex.color.violetBright.withAlpha(0.45f));
        g.strokePath(currentPath_, juce::PathStrokeType(0.8f)); // inner accent

        // Small magenta presence dot at the drip's lower-left with a tiny glow —
        // a "status" dot that makes the closed state unmistakable.
        auto b = currentPath_.getBounds();
        const float dotX = b.getX() + b.getWidth() * 0.18f;
        const float dotY = b.getY() + b.getHeight() * 0.86f;
        g.setColour(t.apex.color.magenta.withAlpha(0.35f));
        g.fillEllipse(dotX - 4.5f, dotY - 4.5f, 9.0f, 9.0f);
        g.setColour(t.apex.color.magenta.withAlpha(0.85f));
        g.fillEllipse(dotX - 1.5f, dotY - 1.5f, 3.0f, 3.0f);
    }
    else
    {
        // Violet flowing edge — transformation accent (slightly raised while
        // open for consistency with the closed-state pop).
        g.setColour(t.apex.color.violet.withAlpha(0.38f));
        g.strokePath(currentPath_, juce::PathStrokeType(1.5f));
    }

    {
        auto b = currentPath_.getBounds();
        juce::Path hl;
        hl.addEllipse(b.getX() + b.getWidth() * 0.04f,
                      b.getY() + b.getHeight() * 0.01f,
                      b.getWidth() * 0.30f,
                      b.getHeight() * 0.08f);
        g.setColour(t.apex.color.violet.withAlpha(0.05f));
        g.fillPath(hl);
    }

    if (animationT_ > 0.6f)
    {
        auto b = currentPath_.getBounds();
        g.setColour(t.apex.color.magenta.withAlpha(0.10f));
        g.fillEllipse(b.getX() + b.getWidth() * 0.80f, b.getY() + b.getHeight() * 0.86f, 7, 7);
        g.setColour(t.apex.color.cyan.withAlpha(0.08f));
        g.fillEllipse(b.getX() + b.getWidth() * 0.22f, b.getY() + b.getHeight() * 0.91f, 4, 4);
    }
}

inline void SlimeSidePanel::resized()
{
    if (fxContent_)
        fxContent_->setBounds(getContentArea());
}

inline void SlimeSidePanel::mouseDown(const juce::MouseEvent& e)
{
    if (state_ == VisualState::ClosedDrip)
    {
        if (attachmentMode_ == AttachmentMode::Detached)
        {
            dragger_.startDraggingComponent(this, e);
            wasDrag_ = false;
        }
        else
        {
            toggle();
        }
    }
    else if (attachmentMode_ == AttachmentMode::Detached && isOpen())
    {
        dragger_.startDraggingComponent(this, e);
        wasDrag_ = false;
    }
}

inline void SlimeSidePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (attachmentMode_ == AttachmentMode::Detached
        && (isOpen() || state_ == VisualState::ClosedDrip))
    {
        dragger_.dragComponent(this, e, nullptr);
        wasDrag_ = true;
    }
}

inline void SlimeSidePanel::mouseUp(const juce::MouseEvent& e)
{
    if (state_ == VisualState::ClosedDrip && !wasDrag_)
        toggle();
}

inline bool SlimeSidePanel::hitTest(int x, int y)
{
    return currentPath_.contains((float)x, (float)y);
}

inline void SlimeSidePanel::timerCallback()
{
    const float step = (float)kTimerIntervalMs / 1000.0f / kAnimDurationSec;

    if (state_ == VisualState::Opening)
    {
        animationT_ = juce::jmin(1.0f, animationT_ + step);
        if (animationT_ >= 1.0f)
        {
            animationT_ = 1.0f;
            state_ = VisualState::OpenPuddle;
            stopTimer();
            if (onStateChanged) onStateChanged(true);
        }
    }
    else if (state_ == VisualState::Closing)
    {
        animationT_ = juce::jmax(0.0f, animationT_ - step);
        if (animationT_ <= 0.0f)
        {
            animationT_ = 0.0f;
            state_ = VisualState::ClosedDrip;
            setSize(kClosedW, kClosedH);
            stopTimer();
            if (onStateChanged) onStateChanged(false);
        }
    }
    else
    {
        stopTimer();
        return;
    }

    interpolatePoints(animationT_);
    rebuildPath();
    syncBoundsAndContent();
    repaint();
}

} // namespace DAW
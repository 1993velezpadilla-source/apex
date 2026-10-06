#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

/**
 * FloatingWindowBase
 * Base class for all floating panels in the DAW.
 *
 * Employs native JUCE component draggers and resizable borders to perfectly
 * match Windows/macOS native floating mechanics. Visually styled with hyper-realistic
 * glassy UI, dynamic gradients, and Logic Pro iPad style traffic lights.
 */
class FloatingWindowBase : public juce::Component,
                           private juce::KeyListener
{
public:
    static constexpr int kTitleH   = 32; // Taller premium title bar
    static constexpr int kBtnSize  = 12;
    static constexpr int kBtnGap   = 8;
    static constexpr int kBtnPadL  = 14;
    static constexpr int kResZone  = 6;  // Standard Windows edge resize size
    static constexpr int kMinW     = 200;
    static constexpr int kMinH     = 120;

    /** Hide only the close (red) traffic-light button. */
    void setHideTrafficLights(bool hide) { hideTrafficLights_ = hide; }

    void setUseOpaqueBackdrop(bool useOpaqueBackdrop)
    {
        useOpaqueBackdrop_ = useOpaqueBackdrop;
        setOpaque(useOpaqueBackdrop_);
        repaint();
    }

    /** Override minimum size (e.g. mixer needs more height than a generic window). */
    void setMinimumSize(int minW, int minH)
    {
        constrainer_.setMinimumSize(minW, minH);
    }
    static constexpr int kSnapZone = 50;

    enum class SnapMode { None, Left, Right, TopLeft, TopRight, Maximized };

    explicit FloatingWindowBase(const juce::String& title = "Window")
        : title_(title)
    {
        setOpaque(false);
        setBufferedToImage(true);
        constrainer_.setMinimumSize(kMinW, kMinH);
        
        border_ = std::make_unique<juce::ResizableBorderComponent>(this, &constrainer_);
        border_->setBorderThickness(juce::BorderSize<int>(kResZone));
        addAndMakeVisible(border_.get());
    }

    ~FloatingWindowBase() override = default;

    // ── Embedded mode ─────────────────────────────────────────────────────────
    void setEmbedded(bool e)
    {
        embedded_   = e;
        isDragging_ = false;
        snapPreview_= SnapMode::None;
        if (border_) border_->setVisible(!e);
        layoutContent();
        repaint();
    }
    bool isEmbedded() const noexcept { return embedded_; }

    void setWindowTitle(const juce::String& t) { title_ = t; repaint(); }

    bool     isMinimized() const noexcept { return minimized_; }
    bool     isMaximized() const noexcept { return maximized_; }
    SnapMode getSnapMode() const noexcept { return snapMode_;  }

    void restoreIfMinimized()
    {
        if (minimized_)
            toggleMinimize();
    }

    juce::Rectangle<int> getContentArea() const
    {
        return embedded_ ? getLocalBounds()
                         : getLocalBounds().withTrimmedTop(kTitleH);
    }

    std::function<void()> onCloseClicked;
    std::function<void()> onMinimized;
    std::function<void()> onRestored;
    std::function<void()> onDragStarted;
    std::function<void()> onDragEnded;
    std::function<bool(const juce::KeyPress&)> onKeyPress;

    // ── Title-bar right zone injection ────────────────────────────────────────
    // Set these to embed toolbar buttons into the title bar right side.
    // painter(g, rect)        — draw buttons into the given rect
    // hitTest(localPoint)     — return true if the point is over a button (suppresses drag)
    // mouseDown(localPoint)   — handle a click inside the right zone
    // mouseMove(localPoint)   — handle hover (for cursor / repaint)
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> titleBarRightPainter;
    std::function<bool(juce::Point<float>)>                      titleBarRightHitTest;
    std::function<void(juce::Point<float>)>                      titleBarRightMouseDown;
    std::function<void(juce::Point<float>)>                      titleBarRightMouseMove;

    /** Returns the rect reserved for custom right-zone content in the title bar. */
    juce::Rectangle<float> getTitleBarRightZone() const
    {
        // Mac buttons are on the left; right zone runs from (width - rightW) to right edge
        // with a small margin. We reserve 220 px which comfortably fits 5-6 icon buttons.
        constexpr float rightW  = 220.f;
        constexpr float marginR = 10.f;
        float y  = 0.f;
        float h  = (float)kTitleH;
        float x  = (float)getWidth() - rightW - marginR;
        return { x, y, rightW, h };
    }

    // ── paint (Hyper-Realistic Glass UI) ──────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        if (embedded_) return;

        auto& t = Theme::getInstance();
        auto bounds = getLocalBounds().toFloat();
        float corner = (float)t.sizing.windowCornerR;

        if (useOpaqueBackdrop_)
        {
            g.setColour(t.colors.backgroundDark.darker(0.18f));
            g.fillRect(getLocalBounds());
        }

        // Base translucent glass body
        g.setColour(t.colors.surface.withAlpha(0.92f));
        g.fillRoundedRectangle(bounds, corner);

        // Sub-pixel top highlight (glass edge reflection)
        // Immersive3D: brighter, wider top rim light
        float rimAlpha = t.isImmersive() ? 0.18f : 0.08f;
        g.setColour(juce::Colours::white.withAlpha(rimAlpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), corner, t.isImmersive() ? 1.5f : 1.f);

        // Title bar glossy gradient
        auto tR = bounds.removeFromTop((float)kTitleH);
        juce::ColourGradient tGrad(t.colors.titleBarTop.withAlpha(0.96f), 0, 0,
                                   t.colors.titleBarBot.withAlpha(0.96f), 0, tR.getHeight(), false);
        g.setGradientFill(tGrad);
        g.fillRoundedRectangle(tR.withHeight(tR.getHeight() + corner), corner);

        // Accent highlight along the top edge of the title bar
        // Immersive3D: stronger accent line + second softer glow band below it
        float accentAlpha = t.isImmersive() ? 0.70f : 0.45f;
        g.setColour(t.colors.accent.withAlpha(accentAlpha));
        g.fillRoundedRectangle(tR.getX() + corner, tR.getY() + 0.5f,
                               tR.getWidth() - corner * 2.f, 1.5f, 0.75f);
        if (t.isImmersive())
        {
            g.setColour(t.colors.accent.withAlpha(0.20f));
            g.fillRoundedRectangle(tR.getX() + corner, tR.getY() + 2.f,
                                   tR.getWidth() - corner * 2.f, 3.f, 1.5f);
        }

        // Bevel separator below title
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRect(tR.getX(), tR.getBottom() - 1.f, tR.getWidth(), 1.f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillRect(tR.getX(), tR.getBottom(), tR.getWidth(), 1.f);

        // Snap preview glow overlay
        if (isDragging_ && snapPreview_ != SnapMode::None)
        {
            g.setColour(t.colors.accent.withAlpha(0.35f));
            g.fillRoundedRectangle(bounds, corner);
        }

        // Title text (Centered, crisp subtle shadow for depth)
        g.setFont(t.fonts.bold);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawText(title_, tR.translated(0, 1), juce::Justification::centred);
        g.setColour(t.colors.text);
        g.drawText(title_, tR, juce::Justification::centred);

        // Outer dark thin line (crisp boundary)
        // Immersive3D: adds a faint accent-tinted outer halo
        if (t.isImmersive())
        {
            g.setColour(t.colors.accent.withAlpha(0.12f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().expanded(1.f), corner + 1.f, 1.5f);
        }
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), corner, 1.f);

        if (!hideTrafficLights_)
            drawMacButtons(g, tR);
        else
            drawMacButtonsNoClose(g, tR);

        // Custom right-zone content (e.g. Mixer toolbar buttons)
        if (titleBarRightPainter)
            titleBarRightPainter(g, getTitleBarRightZone());
    }

    // ── resized ───────────────────────────────────────────────────────────────
    void resized() override
    {
        if (border_) border_->setBounds(getLocalBounds());
        layoutContent();
        if (border_) border_->toFront(false);
    }

    void childrenChanged() override
    {
        if (border_) border_->toFront(false);
    }

    // ── mouse (Title bar dragging + snaps) ────────────────────────────────────
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (embedded_) return;
        toFront(true);
        grabKeyboardFocus();

        auto hit = hitTestBtns(e.position);
        if (hit == BtnHit::Close)    { if (onCloseClicked) onCloseClicked(); return; }
        if (hit == BtnHit::Minimize) { toggleMinimize(); return; }
        if (hit == BtnHit::Maximize)
        {
            if (e.mods.isRightButtonDown()) showSnapMenu();
            else                            toggleMaximize();
            return;
        }

        // Custom right-zone button hit
        if (titleBarRightHitTest && titleBarRightHitTest(e.position))
        {
            if (titleBarRightMouseDown) titleBarRightMouseDown(e.position);
            return;
        }

        if (e.y <= kTitleH)
        {
            if (maximized_)
            {
                // Exit fullscreen on title-bar grab, then start dragging
                auto screenMouse = e.getScreenPosition();
                applySnapMode(SnapMode::None);
                // Re-centre the restored window under the mouse
                auto restored = getBounds();
                int newX = screenMouse.x - restored.getWidth() / 2;
                int newY = screenMouse.y - kTitleH / 2;
                if (auto* parent = getParentComponent())
                {
                    auto lp = parent->getLocalPoint(nullptr, screenMouse);
                    newX = lp.x - restored.getWidth() / 2;
                    newY = lp.y - kTitleH / 2;
                }
                setTopLeftPosition(newX, newY);
            }
            isDragging_ = true;
            dragger_.startDraggingComponent(this, e);
            if (onDragStarted) onDragStarted();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (embedded_ || !isDragging_) return;

        dragger_.dragComponent(this, e, &constrainer_);

        updateSnapPreview(e.getScreenPosition());
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        bool wasDragging = isDragging_;
        if (!embedded_ && isDragging_ && snapPreview_ != SnapMode::None)
            applySnapMode(snapPreview_);
        isDragging_  = false;
        snapPreview_ = SnapMode::None;
        repaint();
        if (wasDragging && onDragEnded) onDragEnded();
    }

    void mouseMove(const juce::MouseEvent& e) override 
    {
        // Triggers repaint when moving over Mac buttons to show X/-/+ icons
        repaint(0, 0, kBtnPadL + 3 * (kBtnSize + kBtnGap), kTitleH);
        // Forward to custom right-zone for hover effects
        if (titleBarRightMouseMove)
            titleBarRightMouseMove(e.position);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!embedded_ && e.y <= kTitleH
            && hitTestBtns(e.position) == BtnHit::None
            && !(titleBarRightHitTest && titleBarRightHitTest(e.position)))
            toggleMaximize();
    }

    // ── Programmatic snap ─────────────────────────────────────────────────────
    void applySnapMode(SnapMode mode)
    {
        auto* parent = getParentComponent();
        if (!parent) return;

        if (mode != SnapMode::None && snapMode_ == SnapMode::None && !maximized_)
            preSnapBounds_ = getBounds();

        snapMode_  = mode;
        minimized_ = false;
        auto pb = parent->getLocalBounds();
        int hw = pb.getWidth() / 2, hh = pb.getHeight() / 2;

        switch (mode)
        {
            case SnapMode::Left:      setBounds(0, 0, hw, pb.getHeight()); break;
            case SnapMode::Right:     setBounds(hw, 0, hw, pb.getHeight()); break;
            case SnapMode::TopLeft:   setBounds(0, 0, hw, hh); break;
            case SnapMode::TopRight:  setBounds(hw, 0, hw, hh); break;
            case SnapMode::Maximized:
                preMaxBounds_ = getBounds();
                maximized_ = true;
                setBounds(pb);
                break;
            case SnapMode::None:
                restoreFromSnap();
                return;
        }
        
        // Hide resize boundaries when snapped to edges, like native windows
        if (border_) border_->setVisible(mode == SnapMode::None);
        layoutContent();
        repaint();
    }

protected:
    virtual void layoutContent() = 0;

    juce::String title_;
    bool embedded_  = false;
    bool minimized_ = false;
    bool maximized_ = false;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (onKeyPress)
            return onKeyPress(key);

        return false;
    }

    bool keyStateChanged(bool, juce::Component*) override
    {
        return false;
    }

    bool hideTrafficLights_ = false;
    bool useOpaqueBackdrop_ = false;
    juce::ComponentBoundsConstrainer constrainer_;
    std::unique_ptr<juce::ResizableBorderComponent> border_;
    juce::ComponentDragger dragger_;

    SnapMode             snapMode_    = SnapMode::None;
    SnapMode             snapPreview_ = SnapMode::None;
    juce::Rectangle<int> preMaxBounds_;
    juce::Rectangle<int> preSnapBounds_;
    int                  preMiniH_   = 300;
    bool                 isDragging_  = false;

    enum class BtnHit { None, Close, Maximize, Minimize };
    struct Btns { juce::Rectangle<float> close, minimize, maximize; }; 

    void toggleMinimize()
    {
        if (!minimized_) { preMiniH_ = getHeight(); minimized_ = true;  maximized_ = false; }
        else             {                           minimized_ = false; }
        if (border_) border_->setVisible(!minimized_ && !embedded_);
        setSize(getWidth(), minimized_ ? kTitleH : preMiniH_);
        layoutContent();
        repaint();
        if ( minimized_ && onMinimized) onMinimized();
        if (!minimized_ && onRestored)  onRestored();
    }

    void toggleMaximize() { applySnapMode(maximized_ ? SnapMode::None : SnapMode::Maximized); }

    void restoreFromSnap()
    {
        if (preSnapBounds_.getWidth() > 1) setBounds(preSnapBounds_);
        snapMode_  = SnapMode::None;
        maximized_ = false;
        if (border_) border_->setVisible(true);
        layoutContent();
        repaint();
    }

    void updateSnapPreview(juce::Point<int> screenPos)
    {
        SnapMode preview = SnapMode::None;
        if (auto* parent = getParentComponent())
        {
            auto  lp = parent->getLocalPoint(nullptr, screenPos);
            auto  pb = parent->getLocalBounds();
            bool  atL = lp.x < kSnapZone,  atR = lp.x > pb.getRight()  - kSnapZone;
            bool  atT = lp.y < kSnapZone;
            if      (atL && atT) preview = SnapMode::TopLeft;
            else if (atR && atT) preview = SnapMode::TopRight;
            else if (atL)        preview = SnapMode::Left;
            else if (atR)        preview = SnapMode::Right;
            else if (atT)        preview = SnapMode::Maximized;
        }
        if (preview != snapPreview_) { snapPreview_ = preview; repaint(); }
    }

    void showSnapMenu()
    {
        juce::PopupMenu m;
        m.addItem(1, "Snap Left");   m.addItem(2, "Snap Right");
        m.addItem(3, "Snap Top-Left"); m.addItem(4, "Snap Top-Right");
        m.addSeparator();
        m.addItem(5, "Maximize");
        if (snapMode_ != SnapMode::None || maximized_) m.addItem(6, "Restore");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this](int r)
            {
                switch (r)
                {
                    case 1: applySnapMode(SnapMode::Left);      break;
                    case 2: applySnapMode(SnapMode::Right);     break;
                    case 3: applySnapMode(SnapMode::TopLeft);   break;
                    case 4: applySnapMode(SnapMode::TopRight);  break;
                    case 5: applySnapMode(SnapMode::Maximized); break;
                    case 6: applySnapMode(SnapMode::None);      break;
                    default: break;
                }
            });
    }

    // Returns minimize/maximize shifted left to fill the close slot.
    Btns getShiftedBtnRects(juce::Rectangle<float> bar) const
    {
        Btns b{};
        float startX = kBtnPadL; // start where close would be
        float y = (bar.getHeight() - kBtnSize) * 0.5f;
        b.close    = {}; // not used
        b.minimize = { startX, y, (float)kBtnSize, (float)kBtnSize };
        startX += kBtnSize + kBtnGap;
        b.maximize = { startX, y, (float)kBtnSize, (float)kBtnSize };
        return b;
    }

    Btns getBtnRects(juce::Rectangle<float> bar) const
    {
        Btns b{};
        float startX = kBtnPadL;
        float y = (bar.getHeight() - kBtnSize) * 0.5f;

        b.close = { startX, y, (float)kBtnSize, (float)kBtnSize };
        startX += kBtnSize + kBtnGap;
        b.minimize = { startX, y, (float)kBtnSize, (float)kBtnSize };
        startX += kBtnSize + kBtnGap;
        b.maximize = { startX, y, (float)kBtnSize, (float)kBtnSize };
        return b;
    }

    BtnHit hitTestBtns(juce::Point<float> p) const
    {
        if (embedded_) return BtnHit::None;
        auto tBar = getLocalBounds().removeFromTop(kTitleH).toFloat();
        if (hideTrafficLights_)
        {
            auto btns = getShiftedBtnRects(tBar);
            if (btns.minimize.contains(p)) return BtnHit::Minimize;
            if (btns.maximize.contains(p)) return BtnHit::Maximize;
            return BtnHit::None;
        }
        auto btns = getBtnRects(tBar);
        if (btns.close.contains(p))    return BtnHit::Close;
        if (btns.minimize.contains(p)) return BtnHit::Minimize;
        if (btns.maximize.contains(p)) return BtnHit::Maximize;
        return BtnHit::None;
    }

    // Logic Pro iPad / Apple style traffic lights
    void drawMacButtons(juce::Graphics& g, juce::Rectangle<float> tBar)
    {
        auto btns = getBtnRects(tBar);
        auto mouse = getMouseXYRelative().toFloat();
        bool hoverBar = tBar.contains(mouse); 

        auto drawBtn = [&](juce::Rectangle<float> r, juce::Colour col, bool isHovered, juce::String symbol) {
            // Shadow behind button
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(r.translated(0, 1.0f));

            // Gradient body
            juce::ColourGradient grad(col.brighter(0.15f), 0, r.getY(), col.darker(0.15f), 0, r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillEllipse(r);

            // Bright rim light
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawEllipse(r.reduced(0.5f), 1.f);
            
            // Dark edge
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawEllipse(r, 0.8f);

            // Icon visible on hover
            if (hoverBar) {
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.setFont(juce::Font(10.f, juce::Font::bold));
                g.drawText(symbol, r.translated(0, -1), juce::Justification::centred);
            }
        };

        juce::Colour cClose = juce::Colour(0xffff5f56);
        juce::Colour cMin   = juce::Colour(0xffffbd2e);
        juce::Colour cMax   = juce::Colour(0xff27c93f);

        drawBtn(btns.close,    btns.close.contains(mouse) ? cClose.brighter(0.1f) : cClose, hoverBar, "x");
        drawBtn(btns.minimize, btns.minimize.contains(mouse) ? cMin.brighter(0.1f)   : cMin,   hoverBar, "-");
        drawBtn(btns.maximize, btns.maximize.contains(mouse) ? cMax.brighter(0.1f)   : cMax,   hoverBar, "+");
    }

    // Draws only minimize+maximize, shifted left into the close button's position.
    void drawMacButtonsNoClose(juce::Graphics& g, juce::Rectangle<float> tBar)
    {
        auto btns = getShiftedBtnRects(tBar);
        auto mouse = getMouseXYRelative().toFloat();
        bool hoverBar = tBar.contains(mouse);

        auto drawBtn = [&](juce::Rectangle<float> r, juce::Colour col, juce::String symbol) {
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(r.translated(0, 1.0f));
            juce::ColourGradient grad(col.brighter(0.15f), 0, r.getY(), col.darker(0.15f), 0, r.getBottom(), false);
            g.setGradientFill(grad);
            g.fillEllipse(r);
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawEllipse(r.reduced(0.5f), 1.f);
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawEllipse(r, 0.8f);
            if (hoverBar) {
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.setFont(juce::Font(10.f, juce::Font::bold));
                g.drawText(symbol, r.translated(0, -1), juce::Justification::centred);
            }
        };

        juce::Colour cMin = juce::Colour(0xffffbd2e);
        juce::Colour cMax = juce::Colour(0xff27c93f);
        drawBtn(btns.minimize, btns.minimize.contains(mouse) ? cMin.brighter(0.1f) : cMin, "-");
        drawBtn(btns.maximize, btns.maximize.contains(mouse) ? cMax.brighter(0.1f) : cMax, "+");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingWindowBase)
};

} // namespace DAW

#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

/**
 * FloatingPanelChrome
 *
 * Nucleus: reusable window chrome wrapper for DAW panels (side panel, browser, etc.).
 * Provides:
 *   - Title bar with X (close), _ (minimize), □ (maximize/restore) buttons
 *   - Drag-to-move by title bar
 *   - Maximize/restore toggle
 *   - State tracking: Open, Minimized, Maximized, Closed
 *   - Callbacks for all state transitions
 *   - Does NOT create an OS-level window — stays inside the DAW component tree
 */
class FloatingPanelChrome : public juce::Component
{
public:
    static constexpr int kChromeH    = 28;
    static constexpr int kBtnW       = 24;
    static constexpr int kBtnGap     = 2;
    static constexpr int kBtnPadR    = 6;

    enum class PanelState { Open, Minimized, Maximized, Closed };

    explicit FloatingPanelChrome(const juce::String& title, juce::Component* content)
        : title_(title), content_(content)
    {
        jassert(content_ != nullptr);
        addAndMakeVisible(content_);
        setOpaque(false);
    }

    ~FloatingPanelChrome() override = default;

    // ── Public API ────────────────────────────────────────────────────────

    PanelState getState() const noexcept { return state_; }
    bool isMaximized() const noexcept { return state_ == PanelState::Maximized; }
    bool isMinimized() const noexcept { return state_ == PanelState::Minimized; }

    void setTitle(const juce::String& t) { title_ = t; repaint(); }
    const juce::String& getTitle() const noexcept { return title_; }

    juce::Component* getContent() const noexcept { return content_; }

    /** Store the bounds to restore to after maximize. */
    void setPreMaxBounds(juce::Rectangle<int> b) { preMaxBounds_ = b; }
    juce::Rectangle<int> getPreMaxBounds() const noexcept { return preMaxBounds_; }

    /** Store floating position for restore after close/minimize. */
    void setFloatingBounds(juce::Rectangle<int> b) { floatingBounds_ = b; }
    juce::Rectangle<int> getFloatingBounds() const noexcept { return floatingBounds_; }

    /** External restore (called from bubble taskbar or quick access). */
    void restorePanel()
    {
        state_ = PanelState::Open;
        setVisible(true);
        if (floatingBounds_.getWidth() > 0)
            setBounds(floatingBounds_);
        toFront(true);
        DBG("[FloatingPanelChrome] Panel restored: \"" + title_ + "\" state=Open"
            + " bounds=" + getBounds().toString());
        if (onRestored) onRestored();
    }

    void maximizePanel(juce::Rectangle<int> parentBounds)
    {
        if (state_ != PanelState::Maximized)
        {
            preMaxBounds_ = getBounds();
            state_ = PanelState::Maximized;
            setBounds(parentBounds);
            DBG("[FloatingPanelChrome] Panel maximized: \"" + title_ + "\""
                + " bounds=" + getBounds().toString());
            if (onMaximized) onMaximized();
        }
        else
        {
            // Restore from maximized
            state_ = PanelState::Open;
            if (preMaxBounds_.getWidth() > 0)
                setBounds(preMaxBounds_);
            DBG("[FloatingPanelChrome] Panel restored from maximize: \"" + title_ + "\""
                + " bounds=" + getBounds().toString());
            if (onRestored) onRestored();
        }
        repaint();
    }

    void minimizePanel()
    {
        floatingBounds_ = getBounds();
        state_ = PanelState::Minimized;
        setVisible(false);
        DBG("[FloatingPanelChrome] Panel minimized: \"" + title_ + "\""
            + " savedBounds=" + floatingBounds_.toString());
        if (onMinimized) onMinimized();
    }

    void closePanel()
    {
        floatingBounds_ = getBounds();
        state_ = PanelState::Closed;
        setVisible(false);
        DBG("[FloatingPanelChrome] Panel closed: \"" + title_ + "\""
            + " savedBounds=" + floatingBounds_.toString());
        if (onClosed) onClosed();
    }

    // ── Callbacks ─────────────────────────────────────────────────────────

    std::function<void()> onClosed;
    std::function<void()> onMinimized;
    std::function<void()> onMaximized;
    std::function<void()> onRestored;
    std::function<void()> onMoved;

    // ── Component overrides ───────────────────────────────────────────────

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto bounds = getLocalBounds().toFloat();
        float corner = 6.f;

        // Panel body background
        g.setColour(t.colors.surface.withAlpha(0.96f));
        g.fillRoundedRectangle(bounds, corner);

        // Outer border
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, corner, 1.f);

        // Title bar
        auto titleBar = bounds.removeFromTop((float)kChromeH);
        juce::ColourGradient tGrad(t.colors.surface.brighter(0.06f), 0, titleBar.getY(),
                                   t.colors.surface.darker(0.04f), 0, titleBar.getBottom(), false);
        g.setGradientFill(tGrad);
        g.fillRoundedRectangle(titleBar.withHeight(titleBar.getHeight() + corner), corner);
        // Clip the bottom corners of the title bar fill
        g.setColour(t.colors.surface.withAlpha(0.96f));
        g.fillRect(titleBar.getX(), titleBar.getBottom() - corner, titleBar.getWidth(), corner);

        // Accent top edge
        g.setColour(t.colors.accent.withAlpha(0.5f));
        g.fillRoundedRectangle(titleBar.getX() + corner, titleBar.getY() + 0.5f,
                               titleBar.getWidth() - corner * 2.f, 1.5f, 0.75f);

        // Separator below title
        g.setColour(t.colors.border.withAlpha(0.5f));
        g.fillRect(titleBar.getX(), titleBar.getBottom() - 1.f, titleBar.getWidth(), 1.f);

        // Title text
        auto textArea = titleBar.reduced(10.f, 0.f);
        textArea.removeFromRight((float)(kBtnW * 3 + kBtnGap * 2 + kBtnPadR + 4));
        g.setFont(juce::Font(11.f, juce::Font::bold));
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawText(title_, textArea.translated(0, 1), juce::Justification::centredLeft, true);
        g.setColour(t.colors.text);
        g.drawText(title_, textArea, juce::Justification::centredLeft, true);

        // Window control buttons
        drawWindowButtons(g, titleBar);
    }

    void resized() override
    {
        if (content_)
            content_->setBounds(getLocalBounds().withTrimmedTop(kChromeH));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        toFront(true);

        // Hit-test window buttons
        auto titleBar = getLocalBounds().removeFromTop(kChromeH).toFloat();
        auto hit = hitTestButtons(e.position, titleBar);
        if (hit == BtnHit::Close)    { closePanel(); return; }
        if (hit == BtnHit::Minimize) { minimizePanel(); return; }
        if (hit == BtnHit::Maximize)
        {
            if (auto* parent = getParentComponent())
                maximizePanel(parent->getLocalBounds());
            return;
        }

        // Title bar drag
        if (e.y <= kChromeH && state_ != PanelState::Maximized)
        {
            isDragging_ = true;
            dragger_.startDraggingComponent(this, e);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isDragging_) return;
        dragger_.dragComponent(this, e, &constrainer_);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (isDragging_)
        {
            isDragging_ = false;
            floatingBounds_ = getBounds();
            DBG("[FloatingPanelChrome] Panel moved: \"" + title_ + "\""
                + " bounds=" + getBounds().toString());
            if (onMoved) onMoved();
        }
    }

    void mouseMove(const juce::MouseEvent&) override
    {
        // Repaint to show button hover states
        repaint(getLocalBounds().removeFromTop(kChromeH));
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.y <= kChromeH)
        {
            auto titleBar = getLocalBounds().removeFromTop(kChromeH).toFloat();
            if (hitTestButtons(e.position, titleBar) == BtnHit::None)
            {
                if (auto* parent = getParentComponent())
                    maximizePanel(parent->getLocalBounds());
            }
        }
    }

private:
    juce::String title_;
    juce::Component* content_ = nullptr;
    PanelState state_ = PanelState::Open;
    juce::Rectangle<int> preMaxBounds_;
    juce::Rectangle<int> floatingBounds_;
    bool isDragging_ = false;
    juce::ComponentDragger dragger_;
    juce::ComponentBoundsConstrainer constrainer_;

    enum class BtnHit { None, Close, Minimize, Maximize };

    struct BtnRects
    {
        juce::Rectangle<float> close, minimize, maximize;
    };

    BtnRects getButtonRects(juce::Rectangle<float> titleBar) const
    {
        BtnRects r;
        float x = titleBar.getRight() - kBtnPadR - kBtnW;
        float y = (titleBar.getHeight() - 16.f) * 0.5f + titleBar.getY();

        r.close    = { x, y, (float)kBtnW, 16.f };
        x -= kBtnW + kBtnGap;
        r.maximize = { x, y, (float)kBtnW, 16.f };
        x -= kBtnW + kBtnGap;
        r.minimize = { x, y, (float)kBtnW, 16.f };
        return r;
    }

    BtnHit hitTestButtons(juce::Point<float> p, juce::Rectangle<float> titleBar) const
    {
        auto btns = getButtonRects(titleBar);
        if (btns.close.contains(p))    return BtnHit::Close;
        if (btns.minimize.contains(p)) return BtnHit::Minimize;
        if (btns.maximize.contains(p)) return BtnHit::Maximize;
        return BtnHit::None;
    }

    void drawWindowButtons(juce::Graphics& g, juce::Rectangle<float> titleBar)
    {
        auto& t = Theme::getInstance();
        auto btns = getButtonRects(titleBar);
        auto mouse = getMouseXYRelative().toFloat();

        auto drawBtnBg = [&](juce::Rectangle<float> r, juce::Colour normalCol, juce::Colour hoverCol) -> bool
        {
            bool hovered = r.contains(mouse);
            g.setColour(hovered ? hoverCol : normalCol);
            g.fillRoundedRectangle(r, 3.f);
            return hovered;
        };

        // Close (×) — red on hover
        {
            bool hov = drawBtnBg(btns.close, t.colors.surface.brighter(0.05f), juce::Colour(0xFFEF4444));
            g.setColour(hov ? juce::Colours::white : t.colors.text.withAlpha(0.7f));
            auto c = btns.close.getCentre();
            float s = 4.5f;
            g.drawLine(c.x - s, c.y - s, c.x + s, c.y + s, 1.8f);
            g.drawLine(c.x + s, c.y - s, c.x - s, c.y + s, 1.8f);
        }

        // Maximize (□ / ⧉) — accent on hover
        {
            bool hov = drawBtnBg(btns.maximize, t.colors.surface.brighter(0.05f), t.colors.accent.withAlpha(0.7f));
            g.setColour(hov ? juce::Colours::white : t.colors.text.withAlpha(0.7f));
            auto c = btns.maximize.getCentre();
            float s = 4.5f;
            if (state_ == PanelState::Maximized)
            {
                // Restore icon: two overlapping rectangles
                g.drawRect(c.x - s + 2.f, c.y - s, s * 2.f - 2.f, s * 2.f - 2.f, 1.6f);
                g.drawRect(c.x - s, c.y - s + 2.f, s * 2.f - 2.f, s * 2.f - 2.f, 1.6f);
            }
            else
            {
                // Maximize icon: single square
                g.drawRect(c.x - s, c.y - s, s * 2.f, s * 2.f, 1.6f);
            }
        }

        // Minimize (—) — yellow on hover
        {
            bool hov = drawBtnBg(btns.minimize, t.colors.surface.brighter(0.05f), juce::Colour(0xFFFFBD2E));
            g.setColour(hov ? juce::Colours::white : t.colors.text.withAlpha(0.7f));
            auto c = btns.minimize.getCentre();
            float s = 5.f;
            g.fillRect(c.x - s, c.y - 0.9f, s * 2.f, 1.8f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingPanelChrome)
};

} // namespace DAW

#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

// ─── Icon drawing helpers (no font dependency) ────────────────────────────────
namespace Icons
{
    // ▶ Play triangle
    inline void drawPlay(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        b = b.reduced(b.getWidth() * 0.28f, b.getHeight() * 0.24f);
        juce::Path p;
        p.addTriangle(b.getX(), b.getY(),
                      b.getX(), b.getBottom(),
                      b.getRight(), b.getCentreY());
        g.setColour(c);
        g.fillPath(p);
    }

    // ■ Stop square
    inline void drawStop(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        g.fillRect(b.reduced(b.getWidth() * 0.28f, b.getHeight() * 0.28f));
    }

    // ● Record circle
    inline void drawRecord(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        g.fillEllipse(b.reduced(b.getWidth() * 0.26f, b.getHeight() * 0.26f));
    }

    // |◀ Skip-to-start
    inline void drawSkipBack(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        b = b.reduced(b.getWidth() * 0.26f, b.getHeight() * 0.24f);
        g.setColour(c);
        // Vertical bar
        g.fillRect(b.getX(), b.getY(), b.getWidth() * 0.18f, b.getHeight());
        // Triangle pointing left
        juce::Path p;
        float lx = b.getX() + b.getWidth() * 0.22f;
        p.addTriangle(b.getRight(), b.getY(),
                      b.getRight(), b.getBottom(),
                      lx,           b.getCentreY());
        g.fillPath(p);
    }

    // ↺ Loop (circular arrow approximated as arc + arrowhead)
    inline void drawLoop(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        b = b.reduced(b.getWidth() * 0.22f, b.getHeight() * 0.22f);
        g.setColour(c);
        juce::Path arc;
        float r = b.getWidth() * 0.5f;
        float cx = b.getCentreX(), cy = b.getCentreY();
        arc.addCentredArc(cx, cy, r, r, 0.f,
                          juce::MathConstants<float>::pi * 0.2f,
                          juce::MathConstants<float>::pi * 1.9f, true);
        juce::PathStrokeType st(b.getWidth() * 0.15f,
                                juce::PathStrokeType::curved,
                                juce::PathStrokeType::rounded);
        g.strokePath(arc, st);
        // Arrowhead at end of arc
        float endAngle = juce::MathConstants<float>::pi * 1.9f;
        float ax = cx + std::cos(endAngle) * r;
        float ay = cy + std::sin(endAngle) * r;
        float as = b.getWidth() * 0.22f;
        juce::Path arrow;
        arrow.addTriangle(ax - as * 0.5f, ay,
                          ax + as * 0.5f, ay,
                          ax,             ay + as);
        g.fillPath(arrow);
    }

    // ↩ Return to last position (curved arrow pointing left)
    inline void drawReturn(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        b = b.reduced(b.getWidth() * 0.24f, b.getHeight() * 0.26f);
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        float w = b.getWidth(), h = b.getHeight();

        // Curved arrow shaft
        juce::Path arc;
        arc.startNewSubPath(b.getRight(), cy - h * 0.25f);
        arc.cubicTo(b.getRight() - w * 0.3f, cy - h * 0.5f,
                    b.getX() + w * 0.3f, cy - h * 0.5f,
                    b.getX(), cy);

        juce::PathStrokeType st(b.getWidth() * 0.16f,
                                juce::PathStrokeType::curved,
                                juce::PathStrokeType::rounded);
        g.strokePath(arc, st);

        // Arrowhead pointing left
        float as = b.getWidth() * 0.35f;
        juce::Path arrow;
        arrow.addTriangle(b.getX() - as * 0.2f, cy,
                          b.getX() + as * 0.5f, cy - as * 0.6f,
                          b.getX() + as * 0.5f, cy + as * 0.6f);
        g.fillPath(arrow);
    }

    // Gamepad icon (drawn as path — no font needed)
    inline void drawGamepad(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        float w = b.getWidth() * 0.90f, h = b.getHeight() * 0.60f;
        float lx = cx - w * 0.5f, ty = cy - h * 0.5f;

        // Body (rounded rect)
        juce::Path body;
        body.addRoundedRectangle(lx, ty, w, h, h * 0.35f);
        g.strokePath(body, juce::PathStrokeType(b.getWidth() * 0.09f));

        // D-pad left side cross (horizontal bar)
        float dCX = lx + w * 0.28f, dCY = cy;
        float ds  = h * 0.22f;
        g.fillRect(dCX - ds * 1.6f, dCY - ds * 0.45f, ds * 3.2f, ds * 0.9f); // horiz
        g.fillRect(dCX - ds * 0.45f, dCY - ds * 1.6f, ds * 0.9f, ds * 3.2f); // vert

        // ABXY buttons (4 circles right side)
        float bCX = lx + w * 0.72f, bCY = cy;
        float br  = ds * 0.42f;
        g.fillEllipse(bCX,        bCY - ds, br * 2, br * 2); // top
        g.fillEllipse(bCX,        bCY + ds * 0.1f, br * 2, br * 2); // bottom (approx)
        g.fillEllipse(bCX - ds,   bCY - ds * 0.45f, br * 2, br * 2); // left
        g.fillEllipse(bCX + ds,   bCY - ds * 0.45f, br * 2, br * 2); // right
    }

    // Touch finger icon
    inline void drawTouchFinger(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        float fw = b.getWidth() * 0.22f, fh = b.getHeight() * 0.55f;

        // Palm base
        juce::Path palm;
        palm.addRoundedRectangle(cx - fw * 1.5f, cy - fh * 0.1f,
                                 fw * 3.f, fh * 0.7f, fw * 0.5f);
        g.fillPath(palm);

        // Three fingers
        for (int i = 0; i < 3; ++i)
        {
            float fx = cx + (i - 1) * fw * 1.15f;
            juce::Path finger;
            finger.addRoundedRectangle(fx - fw * 0.45f, cy - fh,
                                       fw * 0.9f, fh * 0.95f, fw * 0.45f);
            g.fillPath(finger);
        }
    }

    // Desktop/mouse icon
    inline void drawMouse(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        float cx = b.getCentreX(), cy = b.getCentreY();
        float mw = b.getWidth() * 0.40f, mh = b.getHeight() * 0.70f;

        // Mouse body
        juce::Path mouse;
        mouse.addRoundedRectangle(cx - mw * 0.5f, cy - mh * 0.4f, mw, mh, mw * 0.5f);
        g.strokePath(mouse, juce::PathStrokeType(b.getWidth() * 0.08f));

        // Centre divider line (left/right click separation)
        g.fillRect(cx - b.getWidth() * 0.01f, cy - mh * 0.4f,
                   b.getWidth() * 0.02f, mh * 0.45f);

        // Scroll wheel dot
        g.fillEllipse(cx - mw * 0.1f, cy - mh * 0.05f, mw * 0.2f, mh * 0.15f);
    }

    // Gear icon
    inline void drawGear(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        // Draw a simple gear-like shape using a star polygon
        float cx = b.getCentreX(), cy = b.getCentreY();
        float ro = b.getWidth() * 0.42f; // outer radius
        float ri = b.getWidth() * 0.25f; // inner radius
        int teeth = 8;
        juce::Path gear;
        for (int i = 0; i < teeth * 2; ++i)
        {
            float angle = juce::MathConstants<float>::twoPi * i / (teeth * 2)
                          - juce::MathConstants<float>::halfPi;
            float radius = (i % 2 == 0) ? ro : ri;
            float px = cx + std::cos(angle) * radius;
            float py = cy + std::sin(angle) * radius;
            if (i == 0) gear.startNewSubPath(px, py);
            else        gear.lineTo(px, py);
        }
        gear.closeSubPath();
        g.setColour(c);
        g.fillPath(gear);
        // Centre hole
        g.setColour(Theme::getInstance().colors.backgroundDark);
        g.fillEllipse(cx - ri * 0.55f, cy - ri * 0.55f, ri * 1.1f, ri * 1.1f);
    }

    // Headphones icon
    inline void drawHeadphones(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);

        auto r = b.reduced(b.getWidth() * 0.22f, b.getHeight() * 0.18f);
        float cx = r.getCentreX();
        float cy = r.getCentreY();
        float archR = juce::jmin(r.getWidth(), r.getHeight()) * 0.42f;

        juce::Path arch;
        arch.addCentredArc(cx, cy + archR * 0.15f, archR, archR, 0.0f,
                           juce::MathConstants<float>::pi,
                           juce::MathConstants<float>::twoPi, true);
        g.strokePath(arch, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        float cupW = archR * 0.42f;
        float cupH = archR * 0.60f;
        float cupY = cy + archR * 0.05f;
        g.fillRoundedRectangle(cx - archR - cupW * 0.15f, cupY, cupW, cupH, cupW * 0.25f);
        g.fillRoundedRectangle(cx + archR - cupW * 0.85f, cupY, cupW, cupH, cupW * 0.25f);

        g.drawLine(cx - archR * 0.62f, cupY + cupH * 0.15f, cx - archR * 0.62f, cupY - cupH * 0.18f, 1.4f);
        g.drawLine(cx + archR * 0.62f, cupY + cupH * 0.15f, cx + archR * 0.62f, cupY - cupH * 0.18f, 1.4f);
    }
} // namespace Icons

// ─── Transport icon type ──────────────────────────────────────────────────────
enum class TransportIcon { Play, Stop, Record, SkipBack, Loop, Return, None };

// ─── TransportButton ──────────────────────────────────────────────────────────
// Draws icon as a path — NO font/Unicode dependency
class TransportButton : public juce::Component
{
public:
    TransportButton(const juce::String& fallbackText, juce::Colour color,
                    TransportIcon icon = TransportIcon::None)
        : fallback_(fallbackText), color_(color), icon_(icon)
    {
        setSize(40, 40);
    }

    void setActive(bool active) { if (isActive_ != active) { isActive_ = active; repaint(); } }
    bool isActive() const { return isActive_; }

    void paint(juce::Graphics& g) override
    {
        auto& t  = Theme::getInstance();
        auto  b  = getLocalBounds().toFloat().reduced(1.f);
        const int cr = t.sizing.btnCornerR;

        // Background: pressed < active < hover < idle
        juce::Colour bg;
        if (isPressed_)       bg = color_.withAlpha(0.40f).darker(0.2f);
        else if (isActive_)   bg = color_.withAlpha(0.22f);
        else if (isHovered_)  bg = color_.withAlpha(0.14f);
        else                  bg = t.colors.surface;

        g.setColour(bg);
        g.fillRoundedRectangle(b, (float)cr);

        // Border: accent rim when active, focus ring when hovered, subtle otherwise
        juce::Colour borderCol;
        if (isActive_)        borderCol = color_.withAlpha(0.8f);
        else if (isHovered_)  borderCol = color_.withAlpha(0.45f);
        else                  borderCol = t.colors.border;

        g.setColour(borderCol);
        g.drawRoundedRectangle(b.reduced(0.5f), (float)cr, 1.f);

        // Top specular line (premium depth cue)
        if (!isPressed_)
        {
            g.setColour(juce::Colours::white.withAlpha(isActive_ ? 0.08f : 0.04f));
            g.fillRoundedRectangle(b.withHeight(1.5f), (float)cr);
        }

        // Icon
        const float iconPad = isPressed_ ? 1.f : 0.f;
        auto iconBounds = b.reduced(iconPad);
        juce::Colour ic = isActive_    ? color_.brighter(0.3f)
                        : isHovered_   ? juce::Colours::white.withAlpha(0.9f)
                                       : juce::Colours::white.withAlpha(0.65f);
        switch (icon_)
        {
            case TransportIcon::Play:     Icons::drawPlay(g, iconBounds, ic);     break;
            case TransportIcon::Stop:     Icons::drawStop(g, iconBounds, ic);     break;
            case TransportIcon::Record:   Icons::drawRecord(g, iconBounds, ic);   break;
            case TransportIcon::SkipBack: Icons::drawSkipBack(g, iconBounds, ic); break;
            case TransportIcon::Loop:     Icons::drawLoop(g, iconBounds, ic);     break;
            case TransportIcon::Return:   Icons::drawReturn(g, iconBounds, ic);   break;
            default:
                g.setColour(ic);
                g.setFont(t.fonts.bold);
                g.drawText(fallback_, iconBounds, juce::Justification::centred);
                break;
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { isHovered_ = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { isHovered_ = false; isPressed_ = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { isPressed_ = true;  repaint(); }
    void mouseUp   (const juce::MouseEvent&) override
    {
        isPressed_ = false;
        repaint();
        if (onClick) onClick();
    }

    std::function<void()> onClick;

private:
    juce::String    fallback_;
    juce::Colour    color_;
    TransportIcon   icon_;
    bool isActive_  = false;
    bool isHovered_ = false;
    bool isPressed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportButton)
};

// ─── ToggleButton (ON/OFF switch) ─────────────────────────────────────────────
class ToggleButton : public juce::Component
{
public:
    explicit ToggleButton(const juce::String& label = "") : label_(label)
    {
        setSize(160, 40);
    }

    void setToggleState(bool isOn)
    {
        if (isOn_ == isOn) return;
        isOn_ = isOn;
        repaint();
        if (onClick) onClick(isOn_);
    }

    bool getToggleState() const { return isOn_; }
    std::function<void(bool)> onClick;

    void paint(juce::Graphics& g) override
    {
        auto& t  = Theme::getInstance();
        auto  b  = getLocalBounds().toFloat();

        // Label on left
        if (label_.isNotEmpty())
        {
            g.setFont(t.fonts.regular);
            g.setColour(t.colors.text);
            g.drawText(label_, b.removeFromLeft(b.getWidth() - 54.f),
                       juce::Justification::centredLeft);
        }

        // Track
        auto track = b.removeFromRight(50.f).reduced(0.f, 10.f);
        g.setColour(isOn_ ? t.colors.accent : t.colors.surface);
        g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(track, track.getHeight() * 0.5f, 1.f);

        // Thumb
        float th = track.getHeight();
        float tx = isOn_ ? track.getRight() - th : track.getX();
        g.setColour(juce::Colours::white);
        g.fillEllipse(tx, track.getY(), th, th);
    }

    void mouseDown(const juce::MouseEvent&) override { setToggleState(!isOn_); }

private:
    juce::String label_;
    bool isOn_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleButton)
};

// ─── TextButton ───────────────────────────────────────────────────────────────
class TextButton : public juce::Component
{
public:
    explicit TextButton(const juce::String& text) : text_(text) { setSize(100, 32); }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto  b = getLocalBounds().toFloat();
        g.setColour(isHovered_ ? t.colors.surfaceHover : t.colors.surface);
        g.fillRoundedRectangle(b, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(b, 4.f, 1.f);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.regular);
        g.drawText(text_, b, juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override { isHovered_ = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { isHovered_ = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;

private:
    juce::String text_;
    bool isHovered_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextButton)
};

} // namespace DAW

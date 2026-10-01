#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>
#include <functional>

// NOTE: This header is deliberately platform-neutral — it never includes
// <windows.h> so that any translation unit can use the flow cursors without
// inheriting Windows' 16-bit-era macros (small/near/far/min/max). All Win32
// native-handle plumbing lives in Main.cpp.

namespace DAW {

/** APEX cursor theme — "flow" generation.
 *
 *  Cursors are rendered procedurally at runtime so they always match the
 *  APEX signal-core aesthetic: dark navy bodies, magenta/pink flow
 *  gradients, violet glows and cyan signal accents. Colours are hard-coded
 *  APEX palette constants so this header never touches Theme::getInstance()
 *  (keeps unit-test static initialization fully independent).
 *
 *  On Windows a global 30 Hz cursor override also swaps ANY operating-system
 *  standard cursor (scrollbars, text editors, popup menus, plugin windows)
 *  for its APEX twin, so the custom theme is applied everywhere — including
 *  surfaces that JUCE controls internally.
 *
 *  MESSAGE THREAD only — built once on first use and cached.
 */
class CursorThemeCore
{
public:
    enum class CursorKind
    {
        Arrow, Crosshair, Hand, DragHand, GrabHandB, IBeam,
        ResizeNS, ResizeWE, ResizeNWSE, ResizeNESW, SizeAll, Wait,
        NumKinds
    };

    // ── APEX flow cursors ─────────────────────────────────────────────────

    static juce::MouseCursor getDefaultArrow()  { static const auto c = buildArrow();   return c; }
    static juce::MouseCursor getCrosshair()     { static const auto c = buildCrosshair(); return c; }
    // Open hand — same magenta/pink identity as the arrow (no colour shifting
    // to whatever is under the cursor).
    static juce::MouseCursor getHand()          { static const auto c = buildHand(true);   return c; }
    // Closed grab hand — used during drags and while the button is held.
    static juce::MouseCursor getDraggingHand()  { static const auto c = buildHand(false);  return c; }
    /** Deeper grab pose — second frame of the squeeze pulse. */
    static juce::MouseCursor getGrabPulseB()    { static const auto c = buildGrabB();     return c; }

    /** Return the rendered artwork (image + hotspot) for a cursor kind, so a
     *  platform layer (Main.cpp) can build native handles without re-drawing.
     *  Forces the matching cursor to be built if it has not been yet. */
    static bool getCursorArtwork(CursorKind kind, juce::Image& outImage, int& outHotX, int& outHotY)
    {
        switch (kind)
        {
        case CursorKind::Arrow:      getDefaultArrow(); break;
        case CursorKind::Crosshair:  getCrosshair();    break;
        case CursorKind::Hand:       getHand();         break;
        case CursorKind::DragHand:   getDraggingHand(); break;
        case CursorKind::GrabHandB:  getGrabPulseB();   break;
        case CursorKind::IBeam:      getIBeam();        break;
        case CursorKind::ResizeNS:   getResizeNS();     break;
        case CursorKind::ResizeWE:   getResizeWE();     break;
        case CursorKind::ResizeNWSE: getResizeNWSE();   break;
        case CursorKind::ResizeNESW: getResizeNESW();   break;
        case CursorKind::SizeAll:    getSizeAll();      break;
        case CursorKind::Wait:       getWait();         break;
        default: return false;
        }
        auto& a = artworkFor(kind);
        if (! a.img.isValid())
            return false;
        outImage = a.img;
        outHotX  = a.hotX;
        outHotY  = a.hotY;
        return true;
    }
    static juce::MouseCursor getIBeam()         { static const auto c = buildIBeam();    return c; }
    static juce::MouseCursor getResizeNS()      { static const auto c = buildResizeAxis(0.0f,     CursorKind::ResizeNS);   return c; }
    static juce::MouseCursor getResizeWE()      { static const auto c = buildResizeAxis(1.570796f, CursorKind::ResizeWE);   return c; }
    // The base artwork is a vertical double-arrow. Rotation -45° moves the
    // upper head to the top-left (NW) and the lower head to the bottom-right
    // (SE) — the NW-SE diagonal used by the top-left / bottom-right corners.
    // Rotation +45° produces the NE-SW diagonal for top-right / bottom-left.
    static juce::MouseCursor getResizeNWSE()    { static const auto c = buildResizeAxis(-0.785398f, CursorKind::ResizeNWSE); return c; }
    static juce::MouseCursor getResizeNESW()    { static const auto c = buildResizeAxis(0.785398f, CursorKind::ResizeNESW); return c; }
    static juce::MouseCursor getSizeAll()       { static const auto c = buildSizeAll();    return c; }
    static juce::MouseCursor getWait()          { static const auto c = buildWait();       return c; }

    /** Map any standard cursor type to its APEX-flow twin. */
    static juce::MouseCursor getStandard(juce::MouseCursor::StandardCursorType type)
    {
        switch (type)
        {
        case juce::MouseCursor::NormalCursor:            return getDefaultArrow();
        case juce::MouseCursor::CrosshairCursor:         return getCrosshair();
        case juce::MouseCursor::PointingHandCursor:      return getHand();
        case juce::MouseCursor::DraggingHandCursor:      return getDraggingHand();
        case juce::MouseCursor::IBeamCursor:             return getIBeam();
        case juce::MouseCursor::WaitCursor:              return getWait();
        case juce::MouseCursor::LeftRightResizeCursor:   return getResizeWE();
        case juce::MouseCursor::UpDownResizeCursor:      return getResizeNS();
        case juce::MouseCursor::LeftEdgeResizeCursor:
        case juce::MouseCursor::RightEdgeResizeCursor:   return getResizeWE();
        case juce::MouseCursor::TopEdgeResizeCursor:
        case juce::MouseCursor::BottomEdgeResizeCursor:  return getResizeNS();
        case juce::MouseCursor::TopLeftCornerResizeCursor:
        case juce::MouseCursor::BottomRightCornerResizeCursor: return getResizeNWSE();
        case juce::MouseCursor::TopRightCornerResizeCursor:
        case juce::MouseCursor::BottomLeftCornerResizeCursor:  return getResizeNESW();
        case juce::MouseCursor::CopyingCursor:           return getDefaultArrow();
        default:                                         return juce::MouseCursor(type);
        }
    }

    /** Apply the APEX default arrow to a root component. */
    static void applyDefaultCursorTo(juce::Component& root)
    {
        root.setMouseCursor(getDefaultArrow());
    }

    /** Start the global Windows cursor override. Safe to call multiple times;
     *  idempotent. Covers JUCE-internal standard cursors (scrollbars, text
     *  editors, popups) and any OS-standard cursor anywhere, including plugin
     *  windows. Implementation lives in Main.cpp (Win32 only). */
    static void startGlobalOverride();

    /** Install the global APEX cursor theme.
     *  1) Makes ApexCursorLookAndFeel the default LookAndFeel so EVERY JUCE
     *     component resolves standard cursor types to their APEX-flow twins
     *     at the LookAndFeel level (deterministic — JUCE re-evaluates the
     *     cursor on every mouse move / component enter, so there is no
     *     polling race with WM_SETCURSOR). Components that never call
     *     setMouseCursor() (transport buttons, mixer chrome, plugin browser,
     *     side panels, …) would otherwise show the OS default arrow, because
     *     JUCE 8 only inherits a parent's cursor when the child's cursor is
     *     MouseCursor::ParentCursor.
     *  2) Starts the Win32 override that additionally swaps OS-standard
     *     cursors on native surfaces (plugin editor content, other threads).
     *  Message thread only. Idempotent. */
    static void installGlobalCursorTheme();

private:
    CursorThemeCore() = delete;

    // Hard-coded APEX palette (mirrors ThemeCore palette constants).
    static constexpr juce::uint32 kMagenta       = 0xFFFF1678;
    static constexpr juce::uint32 kMagentaBright = 0xFFFF2A91;
    static constexpr juce::uint32 kPink          = 0xFFFF3D9F;
    static constexpr juce::uint32 kViolet        = 0xFF813CFF;
    static constexpr juce::uint32 kVioletBright  = 0xFFA34CFF;
    static constexpr juce::uint32 kCyan          = 0xFF00C8FF;
    static constexpr juce::uint32 kDeepestB      = 0xFF070A10;
    static constexpr juce::uint32 kPanelA        = 0xFF0A0D15;
    static constexpr juce::uint32 kPanelC        = 0xFF111522;

    static constexpr int kSize = 32;

    static juce::Colour col(juce::uint32 argb) { return juce::Colour(argb); }

    static juce::Image makeCanvas()
    {
        juce::Image img(juce::Image::ARGB, kSize, kSize, true);
        img.clear(juce::Rectangle<int>(0, 0, kSize, kSize), juce::Colours::transparentBlack);
        return img;
    }

    struct CursorArtwork
    {
        juce::Image img;
        int hotX = 0;
        int hotY = 0;
    };

    static CursorArtwork& artworkFor(CursorKind kind)
    {
        static CursorArtwork arts[(int) CursorKind::NumKinds];
        return arts[(int) kind];
    }

    static juce::MouseCursor makeCursor(CursorKind kind,
                                        std::function<void(juce::Graphics&)> draw,
                                        int hotX, int hotY)
    {
        auto img = makeCanvas();
        juce::Graphics g(img);
        draw(g);
        auto cursor = juce::MouseCursor(img, juce::jlimit(0, kSize - 1, hotX),
                                             juce::jlimit(0, kSize - 1, hotY));
        auto& a = artworkFor(kind);
        a.img  = img;
        a.hotX = juce::jlimit(0, kSize - 1, hotX);
        a.hotY = juce::jlimit(0, kSize - 1, hotY);
        return cursor;
    }

    static void glowStroke(juce::Graphics& g, const juce::Path& p,
                           juce::Colour colour, float baseWidth)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour(colour.withAlpha(0.05f + 0.05f * (float) (3 - i)));
            g.strokePath(p, juce::PathStrokeType(
                baseWidth * (1.0f + (float) (3 - i) * 0.9f)));
        }
    }

    static void fillFlowBody(juce::Graphics& g, const juce::Path& p,
                             float x1, float y1, float x2, float y2)
    {
        juce::ColourGradient body(
            col(kPanelC).brighter(0.08f), x1, y1,
            col(kDeepestB), x2, y2, false);
        body.addColour(0.45f, col(kPanelA));
        g.setGradientFill(body);
        g.fillPath(p);
    }

    static void edgeFlow(juce::Graphics& g, const juce::Path& p,
                         juce::Colour edge, float width)
    {
        glowStroke(g, p, col(kViolet), width);
        g.setColour(edge);
        g.strokePath(p, juce::PathStrokeType(width));
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.strokePath(p, juce::PathStrokeType(juce::jmax(0.5f, width * 0.45f)));
    }

    // ── Builders ──────────────────────────────────────────────────────────

    static juce::MouseCursor buildArrow()
    {
        return makeCursor(CursorKind::Arrow, [](juce::Graphics& g)
        {
            juce::Path p;
            p.startNewSubPath(2.5f, 2.5f);
            p.cubicTo(9.0f, 7.0f, 15.0f, 11.0f, 21.0f, 14.0f);
            p.lineTo(16.5f, 18.5f);
            p.cubicTo(19.0f, 21.0f, 21.5f, 24.0f, 23.5f, 27.5f);
            p.cubicTo(20.5f, 25.5f, 17.5f, 22.5f, 14.5f, 20.0f);
            p.cubicTo(11.0f, 16.0f, 8.0f, 15.5f, 5.5f, 17.5f);
            p.cubicTo(4.0f, 12.5f, 3.0f, 7.5f, 2.5f, 2.5f);
            p.closeSubPath();

            fillFlowBody(g, p, 2.0f, 2.0f, 24.0f, 28.0f);
            edgeFlow(g, p, col(kMagentaBright), 1.3f);

            const float dx = 25.6f, dy = 29.6f, r = 1.7f;
            g.setColour(col(kCyan).withAlpha(0.25f));
            g.fillEllipse(dx - r * 2.2f, dy - r * 2.2f, r * 4.4f, r * 4.4f);
            g.setColour(col(kCyan).withAlpha(0.95f));
            g.fillEllipse(dx - r, dy - r, r * 2.0f, r * 2.0f);
        }, 3, 3);
    }

    static juce::MouseCursor buildCrosshair()
    {
        return makeCursor(CursorKind::Crosshair, [](juce::Graphics& g)
        {
            constexpr float cx = 16.0f, cy = 16.0f;

            juce::Path ring;
            ring.addEllipse(cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
            glowStroke(g, ring, col(kMagenta), 1.6f);
            g.setColour(col(kMagentaBright).withAlpha(0.95f));
            g.strokePath(ring, juce::PathStrokeType(1.5f));
            juce::Path core;
            core.addEllipse(cx - 6.2f, cy - 6.2f, 12.4f, 12.4f);
            g.setColour(col(kDeepestB).withAlpha(0.85f));
            g.fillPath(core);

            const float armOuter = 13.5f;
            for (int i = 0; i < 4; ++i)
            {
                const float ang = (float) i * 1.57079633f;
                const float dx = std::cos(ang), dy = std::sin(ang);
                juce::Path arm;
                arm.startNewSubPath(cx + dx * 9.0f, cy + dy * 9.0f);
                arm.lineTo(cx + dx * armOuter, cy + dy * armOuter);
                arm.lineTo(cx + dx * (armOuter - 2.2f) + dy * 1.6f,
                           cy + dy * (armOuter - 2.2f) - dx * 1.6f);
                arm.closeSubPath();
                g.setColour(col(kCyan).withAlpha(0.95f));
                g.fillPath(arm);
            }
            for (int i = 0; i < 4; ++i)
            {
                const float ang = (float) i * 1.57079633f + 0.78539816f;
                const float dx = std::cos(ang), dy = std::sin(ang);
                g.setColour(col(kCyan).withAlpha(0.55f));
                g.drawLine(cx + dx * 8.5f, cy + dy * 8.5f,
                           cx + dx * 10.5f, cy + dy * 10.5f, 1.2f);
            }
            g.setColour(col(kMagenta).withAlpha(0.30f));
            g.fillEllipse(cx - 3.4f, cy - 3.4f, 6.8f, 6.8f);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            g.fillEllipse(cx - 1.1f, cy - 1.1f, 2.2f, 2.2f);
        }, 16, 16);
    }

    // Open (rest) / closed (grab) hand silhouettes. The grab pose curls the
    // fingers toward the palm — shown while the button is held, like a real
    // grab-and-drag. Both use the same magenta/pink identity as the arrow.
    static juce::Path handPath(bool open)
    {
        juce::Path h;
        if (open)
        {
            h.addRoundedRectangle(5.5f,  4.0f, 8.0f, 13.0f, 4.0f);
            h.addRoundedRectangle(11.0f, 3.0f, 8.0f, 15.0f, 4.0f);
            h.addRoundedRectangle(16.5f, 4.0f, 8.0f, 13.0f, 4.0f);
            h.addRoundedRectangle(22.0f, 7.0f, 7.0f, 9.0f, 3.5f);
            h.addRoundedRectangle(5.0f, 14.0f, 20.0f, 14.0f, 6.0f);
            h.addRoundedRectangle(1.5f, 15.0f, 9.0f, 10.0f, 4.0f);
        }
        else
        {
            // Curled fingers (grab) — tips pulled down toward the palm.
            h.addRoundedRectangle(6.0f, 10.0f, 8.0f, 9.0f, 4.0f);
            h.addRoundedRectangle(11.0f, 9.0f, 8.0f, 10.0f, 4.0f);
            h.addRoundedRectangle(16.5f, 10.0f, 8.0f, 9.0f, 4.0f);
            h.addRoundedRectangle(21.5f, 12.0f, 7.0f, 7.0f, 3.5f);
            h.addRoundedRectangle(5.0f, 13.0f, 20.0f, 13.0f, 6.0f);
            h.addRoundedRectangle(2.5f, 14.0f, 9.0f, 9.0f, 4.0f);
        }
        return h;
    }

    // Second grab pose — slightly deeper curl for the squeeze pulse.
    static juce::Path handPathGrabDeep()
    {
        juce::Path h;
        h.addRoundedRectangle(6.5f, 11.5f, 7.5f, 8.0f, 4.0f);
        h.addRoundedRectangle(11.5f, 10.5f, 7.5f, 9.0f, 4.0f);
        h.addRoundedRectangle(16.5f, 11.5f, 7.5f, 8.0f, 4.0f);
        h.addRoundedRectangle(21.0f, 13.0f, 7.0f, 6.0f, 3.5f);
        h.addRoundedRectangle(5.0f, 13.5f, 20.0f, 12.5f, 6.0f);
        h.addRoundedRectangle(3.0f, 14.5f, 9.0f, 8.5f, 4.0f);
        return h;
    }

    static juce::MouseCursor buildHand(bool open)
    {
        const auto kind = open ? CursorKind::Hand : CursorKind::DragHand;
        return makeCursor(kind, [open](juce::Graphics& g)
        {
            juce::Path hand = handPath(open);
            // Smaller, more precise hand (~80%).
            hand.applyTransform(juce::AffineTransform::scale(0.80f, 0.80f, 16.0f, 16.0f));
            glowStroke(g, hand, col(kViolet), 1.6f);
            fillFlowBody(g, hand, 2.0f, 2.0f, 28.0f, 27.0f);
            g.setColour(col(kMagentaBright).withAlpha(0.95f));
            g.strokePath(hand, juce::PathStrokeType(1.3f));
            g.setColour(juce::Colours::white.withAlpha(0.22f));
            g.drawLine(6.0f, 6.0f, 26.0f, 9.0f, 0.8f);
        }, 7, 6);
    }

    // Deeper grab pose (squeeze pulse frame B).
    static juce::MouseCursor buildGrabB()
    {
        return makeCursor(CursorKind::GrabHandB, [](juce::Graphics& g)
        {
            juce::Path hand = handPathGrabDeep();
            hand.applyTransform(juce::AffineTransform::scale(0.78f, 0.78f, 16.0f, 16.0f));
            glowStroke(g, hand, col(kViolet), 1.6f);
            fillFlowBody(g, hand, 2.0f, 2.0f, 28.0f, 27.0f);
            g.setColour(col(kMagentaBright).withAlpha(0.95f));
            g.strokePath(hand, juce::PathStrokeType(1.3f));
        }, 7, 6);
    }

    static juce::MouseCursor buildIBeam()
    {
        return makeCursor(CursorKind::IBeam, [](juce::Graphics& g)
        {
            juce::Path beam;
            beam.addRoundedRectangle(14.8f, 4.0f, 2.4f, 24.0f, 1.2f);
            beam.addRoundedRectangle(11.0f, 3.0f, 10.0f, 4.0f, 2.0f);
            beam.addRoundedRectangle(11.0f, 25.0f, 10.0f, 4.0f, 2.0f);
            glowStroke(g, beam, col(kMagenta), 1.4f);
            g.setColour(col(kCyan).withAlpha(0.95f));
            g.fillPath(beam);
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.fillRect(14.8f, 6.0f, 1.0f, 20.0f);
        }, 16, 16);
    }

    static juce::MouseCursor buildResizeAxis(float rotationRadians, CursorKind kind)
    {
        return makeCursor(kind, [rotationRadians](juce::Graphics& g)
        {
            constexpr float cx = 16.0f, cy = 16.0f;
            const auto rot = juce::AffineTransform::rotation(rotationRadians, cx, cy);

            juce::Path arrows;
            arrows.startNewSubPath(cx, cy - 11.0f);
            arrows.lineTo(cx - 4.2f, cy - 2.5f);
            arrows.lineTo(cx + 4.2f, cy - 2.5f);
            arrows.closeSubPath();
            arrows.startNewSubPath(cx, cy + 11.0f);
            arrows.lineTo(cx - 4.2f, cy + 2.5f);
            arrows.lineTo(cx + 4.2f, cy + 2.5f);
            arrows.closeSubPath();
            arrows.addRoundedRectangle(cx - 1.1f, cy - 2.5f, 2.2f, 5.0f, 1.0f);

            juce::Path rotated = arrows;
            rotated.applyTransform(rot);

            glowStroke(g, rotated, col(kCyan), 1.5f);
            fillFlowBody(g, rotated, cx - 6.0f, cy - 12.0f, cx + 6.0f, cy + 12.0f);
            g.setColour(col(kMagentaBright).withAlpha(0.95f));
            g.strokePath(rotated, juce::PathStrokeType(1.2f));
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.fillEllipse(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f);
        }, 16, 16);
    }

    static juce::MouseCursor buildSizeAll()
    {
        return makeCursor(CursorKind::SizeAll, [](juce::Graphics& g)
        {
            constexpr float cx = 16.0f, cy = 16.0f;
            juce::Path arrows;
            arrows.startNewSubPath(cx, cy - 12.0f);
            arrows.lineTo(cx - 4.0f, cy - 4.0f);
            arrows.lineTo(cx + 4.0f, cy - 4.0f);
            arrows.closeSubPath();
            arrows.startNewSubPath(cx, cy + 12.0f);
            arrows.lineTo(cx - 4.0f, cy + 4.0f);
            arrows.lineTo(cx + 4.0f, cy + 4.0f);
            arrows.closeSubPath();
            arrows.startNewSubPath(cx - 12.0f, cy);
            arrows.lineTo(cx - 4.0f, cy - 4.0f);
            arrows.lineTo(cx - 4.0f, cy + 4.0f);
            arrows.closeSubPath();
            arrows.startNewSubPath(cx + 12.0f, cy);
            arrows.lineTo(cx + 4.0f, cy - 4.0f);
            arrows.lineTo(cx + 4.0f, cy + 4.0f);
            arrows.closeSubPath();

            glowStroke(g, arrows, col(kMagenta), 1.4f);
            fillFlowBody(g, arrows, cx - 12.0f, cy - 12.0f, cx + 12.0f, cy + 12.0f);
            g.setColour(col(kMagentaBright).withAlpha(0.95f));
            g.strokePath(arrows, juce::PathStrokeType(1.1f));
            g.setColour(col(kCyan).withAlpha(0.95f));
            g.fillEllipse(cx - 1.2f, cy - 1.2f, 2.4f, 2.4f);
        }, 16, 16);
    }

    static juce::MouseCursor buildWait()
    {
        return makeCursor(CursorKind::Wait, [](juce::Graphics& g)
        {
            constexpr float cx = 16.0f, cy = 16.0f;
            juce::Path ring;
            ring.addEllipse(cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
            glowStroke(g, ring, col(kViolet), 1.4f);
            g.setColour(col(kVioletBright).withAlpha(0.90f));
            g.strokePath(ring, juce::PathStrokeType(1.4f));

            for (int i = 0; i < 8; ++i)
            {
                const float ang = (float) i * 0.78539816f;
                const float px = cx + std::cos(ang) * 8.0f;
                const float py = cy + std::sin(ang) * 8.0f;
                const float dotR = (i % 2 == 0) ? 1.9f : 1.2f;
                g.setColour(col(i % 3 == 0 ? kCyan : kMagentaBright).withAlpha(0.95f));
                g.fillEllipse(px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
            }
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.fillEllipse(cx - 1.6f, cy - 1.6f, 3.2f, 3.2f);
        }, 16, 16);
    }

    // ── Global Windows override ────────────────────────────────────────────
    // Native Win32 handle plumbing lives in Main.cpp — this header stays
    // platform-neutral so including it never pulls in <windows.h> and header-
    // only TUs (unit tests) always link.
};

/** JUCE look-and-feel that themes EVERY cursor through CursorThemeCore.
 *
 *  JUCE 8 only inherits a parent's cursor when the child's cursor is
 *  MouseCursor::ParentCursor (LookAndFeel::getMouseCursorFor); a component
 *  that never calls setMouseCursor() therefore shows the OS standard arrow
 *  instead of the APEX arrow. This LAF maps every standard cursor type to
 *  its APEX-flow twin at the LookAndFeel level, so the custom theme is
 *  applied deterministically (JUCE re-evaluates the cursor on every mouse
 *  move) to every component, popup, menu, tooltip, scrollbar, text editor
 *  and list row — including surfaces that never set a cursor explicitly.
 *  Custom image cursors (e.g. CursorThemeCore's own) pass through unchanged.
 *
 *  Installed as the app default LookAndFeel; all APEX LookAndFeels derive
 *  from this class so their subtrees are themed too.
 */
class ApexCursorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::MouseCursor getMouseCursorFor(juce::Component& component) override
    {
        auto cursor = juce::LookAndFeel::getMouseCursorFor(component);

        if (cursor == juce::MouseCursor::NormalCursor)
            return CursorThemeCore::getDefaultArrow();

        for (int t = juce::MouseCursor::NoCursor;
             t < juce::MouseCursor::NumStandardCursorTypes; ++t)
            if (cursor == (juce::MouseCursor::StandardCursorType) t)
                return CursorThemeCore::getStandard((juce::MouseCursor::StandardCursorType) t);

        return cursor;
    }

    /** Process-wide singleton. Leaked on purpose: it is stored raw by
     *  Desktop as the default LAF and must outlive the message thread. */
    static ApexCursorLookAndFeel& instance()
    {
        static ApexCursorLookAndFeel* inst = new ApexCursorLookAndFeel();
        return *inst;
    }
};

inline void CursorThemeCore::installGlobalCursorTheme()
{
    // 1) JUCE-level: every standard cursor resolves to its APEX twin
    //    deterministically at cursor-evaluation time (no polling race).
    juce::LookAndFeel::setDefaultLookAndFeel(&ApexCursorLookAndFeel::instance());
    // 2) Win32-level safety net for native surfaces (plugin editor content,
    //    OS-standard cursors on other threads).
    startGlobalOverride();
}

} // namespace DAW

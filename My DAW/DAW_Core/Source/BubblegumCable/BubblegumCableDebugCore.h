#pragma once
#include "BubblegumCableTypes.h"

namespace DAW {

// =====================================================================
// BubblegumCableDebugCore — helper overlays ONLY.
//
// Rules:
//   * Default mode = Full, which paints NOTHING.
//   * Non-Full modes draw overlays ON TOP of the real cable — they
//     never replace it. This guarantees the production renderer is
//     always visible even when a debug mode is accidentally enabled.
//   * Toggle is opt-in; no external code sets this automatically.
// =====================================================================
class BubblegumCableDebugCore
{
public:
    void setMode(BubblegumCableDebugMode m) noexcept { mode_ = m; }
    BubblegumCableDebugMode getMode() const noexcept { return mode_; }

    void cycleMode() noexcept
    {
        int next = (int) mode_ + 1;
        if (next > (int) BubblegumCableDebugMode::FilledRibbon) next = 0;
        mode_ = (BubblegumCableDebugMode) next;
    }

    void paint(juce::Graphics& g, const BubblegumCableGeometry& geo) const
    {
        if (mode_ == BubblegumCableDebugMode::Full) return;

        constexpr int N = BubblegumCableGeometry::kSeg;
        const auto markCol = juce::Colour(0xFF00E5FF);

        switch (mode_)
        {
            case BubblegumCableDebugMode::Centerline:
            {
                juce::Path p;
                p.startNewSubPath(geo.points[0].x, geo.points[0].y);
                for (int i = 1; i <= N; ++i) p.lineTo(geo.points[i].x, geo.points[i].y);
                g.setColour(markCol.withAlpha(0.9f));
                g.strokePath(p, juce::PathStrokeType(1.2f));
                break;
            }
            case BubblegumCableDebugMode::TopContour:
            {
                juce::Path p;
                const auto& p0t = geo.points[0];
                p.startNewSubPath(p0t.x + p0t.nx * p0t.halfW, p0t.y + p0t.ny * p0t.halfW);
                for (int i = 1; i <= N; ++i)
                {
                    const auto& pt = geo.points[i];
                    p.lineTo(pt.x + pt.nx * pt.halfW, pt.y + pt.ny * pt.halfW);
                }
                g.setColour(markCol.withAlpha(0.9f));
                g.strokePath(p, juce::PathStrokeType(1.2f));
                break;
            }
            case BubblegumCableDebugMode::BottomContour:
            {
                juce::Path p;
                const auto& p0b = geo.points[0];
                p.startNewSubPath(p0b.x - p0b.nx * p0b.halfW, p0b.y - p0b.ny * p0b.halfW);
                for (int i = 1; i <= N; ++i)
                {
                    const auto& pt = geo.points[i];
                    p.lineTo(pt.x - pt.nx * pt.halfW, pt.y - pt.ny * pt.halfW);
                }
                g.setColour(markCol.withAlpha(0.9f));
                g.strokePath(p, juce::PathStrokeType(1.2f));
                break;
            }
            case BubblegumCableDebugMode::Outline:
                g.setColour(markCol.withAlpha(0.9f));
                g.strokePath(geo.ribbon, juce::PathStrokeType(1.0f));
                break;
            case BubblegumCableDebugMode::FilledRibbon:
                g.setColour(markCol.withAlpha(0.35f));
                g.fillPath(geo.ribbon);
                g.setColour(markCol.withAlpha(0.9f));
                g.strokePath(geo.ribbon, juce::PathStrokeType(1.0f));
                break;
            case BubblegumCableDebugMode::Full:
            default:
                break;
        }
    }

private:
    BubblegumCableDebugMode mode_ = BubblegumCableDebugMode::Full;
};

} // namespace DAW

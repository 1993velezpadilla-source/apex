#pragma once
#include <JuceHeader.h>
#include "CommandManager.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

/**
 * UndoHistoryPanel
 *
 * Floating glass panel showing the complete undo + redo history
 * with wall-clock timestamps formatted as "X minutes ago".
 *
 * Click any undo entry  → undo N steps to jump to that state.
 * Click any redo entry  → redo N steps to jump to that state.
 * Timestamps refresh every 15 seconds automatically.
 *
 * Module: CommandCore — no UI dependencies outside ThemeCore.
 */
class UndoHistoryPanel : public juce::Component,
                         private juce::Timer,
                         private CommandManager::Listener
{
public:
    static constexpr int kRowH    = 36;
    static constexpr int kHeaderH = 42;
    static constexpr int kPadH    = 12;
    static constexpr int kPadV    = 8;

    std::function<void()> onCloseClicked;

    UndoHistoryPanel()
    {
        CommandManager::getInstance().addListener(this);
        startTimer(15000); // Refresh time-ago labels every 15s
        setSize(320, 520);
        rebuild();
    }

    ~UndoHistoryPanel() override
    {
        stopTimer();
        CommandManager::getInstance().removeListener(this);
    }

    // ── paint ─────────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        auto& t   = Theme::getInstance();
        auto  b   = getLocalBounds().toFloat();
        float cr  = 10.f;

        // Glass body
        g.setColour(t.colors.surface.withAlpha(0.97f));
        g.fillRoundedRectangle(b, cr);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawRoundedRectangle(b.reduced(0.5f), cr, 1.f);

        // Header bar
        auto hdr = b.removeFromTop((float)kHeaderH);
        juce::ColourGradient hGrad(t.colors.titleBarTop.withAlpha(0.98f), 0, 0,
                                   t.colors.titleBarBot.withAlpha(0.98f), 0, hdr.getHeight(), false);
        g.setGradientFill(hGrad);
        g.fillRoundedRectangle(hdr.withHeight(hdr.getHeight() + cr), cr);

        // Accent line on top of header
        g.setColour(t.colors.accent.withAlpha(0.5f));
        g.fillRoundedRectangle(hdr.getX() + cr, hdr.getY() + 0.5f,
                               hdr.getWidth() - cr * 2.f, 1.5f, 0.75f);

        // Header title
        g.setFont(t.fonts.bold);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawText("History", hdr.translated(0, 1), juce::Justification::centred);
        g.setColour(t.colors.text);
        g.drawText("History", hdr, juce::Justification::centred);

        // Close button — styled circle
        {
            auto cb = closeBtnBounds_.toFloat();
            float sz = juce::jmin(cb.getWidth(), cb.getHeight());
            auto circle = juce::Rectangle<float>(cb.getCentreX() - sz / 2.f, cb.getCentreY() - sz / 2.f, sz, sz);
            if (closeHovered_)
            {
                g.setColour(juce::Colour(0xffff5f56));
                g.fillEllipse(circle);
                g.setColour(juce::Colours::white);
            }
            else
            {
                g.setColour(t.colors.textSecondary.withAlpha(0.25f));
                g.fillEllipse(circle);
                g.setColour(t.colors.textSecondary.withAlpha(0.7f));
            }
            float cx = circle.getCentreX(), cy = circle.getCentreY(), d = 4.f;
            g.drawLine(cx - d, cy - d, cx + d, cy + d, 1.6f);
            g.drawLine(cx + d, cy - d, cx - d, cy + d, 1.6f);
        }

        // Count badge
        {
            int nu = CommandManager::getInstance().getUndoCount();
            int nr = CommandManager::getInstance().getRedoCount();
            juce::String badge = juce::String(nu) + "u / " + juce::String(nr) + "r";
            g.setFont(juce::Font(9.5f, juce::Font::bold));
            g.setColour(t.colors.textSecondary.withAlpha(0.55f));
            g.drawText(badge, hdr.reduced(8, 0), juce::Justification::centredRight);
        }

        // Bevel below header
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(b.getX(), b.getY() - 1.f, b.getWidth(), 1.f);

        // ── Rows ────────────────────────────────────────────────────────────
        auto content = b.withTrimmedBottom(2.f);
        auto clip    = g.reduceClipRegion(content.toNearestInt());

        int y = (int)content.getY() - scrollOffset_;

        // ── Undo entries (most recent first) ──────────────────────────────
        if (!undoEntries_.empty())
        {
            drawSectionLabel(g, t, "UNDO", content.getX(), y, content.getWidth());
            y += kRowH;

            for (int i = 0; i < (int)undoEntries_.size(); ++i)
            {
                drawRow(g, t, undoEntries_[i], i, y, content.getWidth(),
                        hoveredRow_ == i && hoveredSection_ == 0,
                        false);
                y += kRowH;
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        if (!redoEntries_.empty())
        {
            y += 4;
            g.setColour(t.colors.border.withAlpha(0.35f));
            g.fillRect((float)content.getX() + kPadH, (float)y, content.getWidth() - kPadH * 2.f, 1.f);
            y += 6;

            drawSectionLabel(g, t, "REDO", content.getX(), y, content.getWidth());
            y += kRowH;

            for (int i = 0; i < (int)redoEntries_.size(); ++i)
            {
                drawRow(g, t, redoEntries_[i], i, y, content.getWidth(),
                        hoveredRow_ == i && hoveredSection_ == 1,
                        true);
                y += kRowH;
            }
        }

        if (undoEntries_.empty() && redoEntries_.empty())
        {
            g.setFont(t.fonts.regular);
            g.setColour(t.colors.textSecondary.withAlpha(0.4f));
            g.drawText("No history yet", content, juce::Justification::centred);
        }

        // Outer border
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), cr, 1.f);
    }

    void resized() override
    {
        closeBtnBounds_ = { getWidth() - 28, 4, 24, kHeaderH - 8 };
    }

    // ── mouse ─────────────────────────────────────────────────────────────────
    void mouseMove(const juce::MouseEvent& e) override
    {
        bool wasClose = closeHovered_;
        closeHovered_ = closeBtnBounds_.contains(e.getPosition());

        auto [sec, row] = rowAtY(e.y);
        bool changed = (hoveredRow_ != row || hoveredSection_ != sec || wasClose != closeHovered_);
        hoveredRow_     = row;
        hoveredSection_ = sec;
        if (changed) repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredRow_ = -1; hoveredSection_ = -1; closeHovered_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (closeBtnBounds_.contains(e.getPosition()))
        {
            if (onCloseClicked) onCloseClicked();
            return;
        }

        auto [sec, row] = rowAtY(e.y);
        if (sec == 0 && row >= 0 && row < (int)undoEntries_.size())
        {
            // Undo N steps = row+1 (row 0 = most recent = 1 undo step)
            CommandManager::getInstance().undoSteps(row + 1);
        }
        else if (sec == 1 && row >= 0 && row < (int)redoEntries_.size())
        {
            CommandManager::getInstance().redoSteps(row + 1);
        }
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        int totalH = calcTotalContentHeight();
        int visH   = getHeight() - kHeaderH;
        int maxScroll = juce::jmax(0, totalH - visH);
        scrollOffset_ = juce::jlimit(0, maxScroll,
                                     scrollOffset_ - (int)(wheel.deltaY * 60.f));
        repaint();
    }

private:
    // ── Data ──────────────────────────────────────────────────────────────────
    std::vector<CommandManager::HistorySummary> undoEntries_;
    std::vector<CommandManager::HistorySummary> redoEntries_;

    int  scrollOffset_   = 0;
    int  hoveredRow_     = -1;
    int  hoveredSection_ = -1;
    bool closeHovered_   = false;
    juce::Rectangle<int> closeBtnBounds_;

    // ── Category colour map ────────────────────────────────────────────────
    static juce::Colour categoryColour(const juce::String& cat)
    {
        if (cat == "Clip")    return juce::Colour(0xff4ea8de);
        if (cat == "Track")   return juce::Colour(0xff9d4edd);
        if (cat == "Routing") return juce::Colour(0xffef4444);
        if (cat == "Mix")     return juce::Colour(0xff22c55e);
        if (cat == "Marker")  return juce::Colour(0xfffbbf24);
        return juce::Colour(0xff64748b); // default grey-blue
    }

    // ── Time-ago formatter ────────────────────────────────────────────────
    static juce::String formatTimeAgo(juce::Time t)
    {
        auto   now  = juce::Time::getCurrentTime();
        double secs = (now - t).inSeconds();
        if (secs < 10.0)  return "just now";
        if (secs < 60.0)  return juce::String((int)secs) + "s ago";
        int m = (int)(secs / 60.0);
        int s = (int)secs % 60;
        if (m < 60) return juce::String(m) + "m " + juce::String(s) + "s ago";
        int h  = m / 60;
        int mm = m % 60;
        if (mm > 0) return juce::String(h) + "h " + juce::String(mm) + "m ago";
        return juce::String(h) + "h ago";
    }

    // ── Row drawing ───────────────────────────────────────────────────────
    void drawSectionLabel(juce::Graphics& g, const Theme& t,
                          const juce::String& label,
                          float x, int y, float w) const
    {
        auto b = juce::Rectangle<float>(x + kPadH, (float)y + 4.f, w - kPadH * 2.f, (float)kRowH - 8.f);
        g.setFont(juce::Font(9.f, juce::Font::bold));
        g.setColour(t.colors.textSecondary.withAlpha(0.45f));
        g.drawText(label, b, juce::Justification::centredLeft);
        g.fillRect(b.getX() + 30.f, b.getCentreY(), b.getWidth() - 30.f, 1.f);
    }

    void drawRow(juce::Graphics& g, const Theme& t,
                 const CommandManager::HistorySummary& entry,
                 int idx, int y, float w,
                 bool hovered, bool isRedo) const
    {
        auto b = juce::Rectangle<float>((float)kPadH, (float)y + 2.f,
                                        w - (float)kPadH * 2.f, (float)kRowH - 4.f);

        // Hover background
        if (hovered)
        {
            g.setColour(t.colors.accent.withAlpha(0.12f));
            g.fillRoundedRectangle(b, 5.f);
            g.setColour(t.colors.accent.withAlpha(0.25f));
            g.drawRoundedRectangle(b, 5.f, 1.f);
        }

        float alpha = isRedo ? 0.45f : 1.0f;

        // Category badge
        auto catCol = categoryColour(entry.category);
        auto badge  = b.removeFromLeft(6.f).reduced(0.f, 4.f);
        g.setColour(catCol.withAlpha(alpha));
        g.fillRoundedRectangle(badge, 3.f);
        b.removeFromLeft(6.f); // gap

        // Time-ago (right-aligned, secondary)
        auto timeStr = formatTimeAgo(entry.timestamp);
        g.setFont(juce::Font(9.5f));
        g.setColour(t.colors.textSecondary.withAlpha(alpha * 0.65f));
        float timeW = 72.f;
        auto timeBounds = b.removeFromRight(timeW);
        g.drawText(timeStr, timeBounds, juce::Justification::centredRight);

        // Index number
        g.setFont(juce::Font(9.f));
        g.setColour(t.colors.textSecondary.withAlpha(alpha * 0.35f));
        auto numBounds = b.removeFromLeft(22.f);
        g.drawText(juce::String(idx + 1), numBounds, juce::Justification::centredLeft);

        // Description
        g.setFont(t.fonts.regular);
        g.setColour(t.colors.text.withAlpha(alpha));
        g.drawText(entry.description, b.reduced(2.f, 0.f),
                   juce::Justification::centredLeft, true);
    }

    // ── Hit testing ───────────────────────────────────────────────────────
    std::pair<int, int> rowAtY(int mouseY) const
    {
        int contentTop = kHeaderH - scrollOffset_;
        int y = contentTop;

        auto hitTest = [&](const std::vector<CommandManager::HistorySummary>& entries,
                           int section) -> std::pair<int, int>
        {
            y += kRowH; // section label
            for (int i = 0; i < (int)entries.size(); ++i)
            {
                if (mouseY >= y && mouseY < y + kRowH)
                    return { section, i };
                y += kRowH;
            }
            return { -1, -1 };
        };

        if (!undoEntries_.empty())
        {
            auto r = hitTest(undoEntries_, 0);
            if (r.first >= 0) return r;
        }
        if (!redoEntries_.empty())
        {
            y += 11; // divider gap
            auto r = hitTest(redoEntries_, 1);
            if (r.first >= 0) return r;
        }
        return { -1, -1 };
    }

    int calcTotalContentHeight() const
    {
        int h = 0;
        if (!undoEntries_.empty())
            h += kRowH * (1 + (int)undoEntries_.size());
        if (!redoEntries_.empty())
            h += 11 + kRowH * (1 + (int)redoEntries_.size());
        return h;
    }

    // ── Helpers ───────────────────────────────────────────────────────────
    void rebuild()
    {
        undoEntries_ = CommandManager::getInstance().getUndoSummaries();
        redoEntries_ = CommandManager::getInstance().getRedoSummaries();
        repaint();
    }

    // CommandManager::Listener
    void undoHistoryChanged() override { rebuild(); }

    // Timer — refresh time-ago labels
    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoHistoryPanel)
};

} // namespace DAW

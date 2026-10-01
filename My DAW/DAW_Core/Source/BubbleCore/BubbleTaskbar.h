#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "BubbleOrchestrator.h"
#include <set>

namespace DAW {

class BubbleTaskbar : public juce::Component
{
public:
    static constexpr int kBubbleSize    = 56;
    static constexpr int kItemH         = 48;
    static constexpr int kGroupHeaderH  = 32;
    static constexpr int kSectionH      = 22;
    static constexpr int kHeaderH       = 28;
    static constexpr int kPanelW        = 280;
    static constexpr int kPanelMaxH     = 480;
    static constexpr int kDragThreshold = 2;

    BubbleTaskbar() {}

    std::function<void(const juce::String& targetId)> onMergeTriggered;
    std::function<void(const juce::String& targetId)> onMergePreviewStart;
    std::function<void()>                              onMergePreviewEnd;

    void setOrchestrator(BubbleOrchestrator* o, const juce::String& id)
    { orchestrator_ = o; bubbleId_ = id; }

    juce::Point<float> getBubbleCenterInParent() const noexcept
    { return { bubbleX_ + kBubbleSize / 2.f, bubbleY_ + kBubbleSize / 2.f }; }

    // ── Registration ──────────────────────────────────────────────────────

    /** Register a plugin editor entry (grouped by track). */
    void registerWindow(const juce::String& id, const juce::String& title,
                        const juce::String& category,
                        std::function<void()> restoreCallback,
                        std::function<void()> closeCallback = nullptr,
                        const juce::String& trackId = {},
                        const juce::String& pluginName = {},
                        const juce::String& subtitle = {})
    {
        windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
            [&](const Entry& e) { return e.id == id; }), windows_.end());
        windows_.push_back({ id, title, category, std::move(restoreCallback),
                             std::move(closeCallback), trackId, pluginName, subtitle, false });
        DBG("[BubbleTaskbar] Entry registered id=\"" + id + "\" title=\"" + title
            + "\" category=\"" + category + "\" trackId=\"" + trackId
            + "\" pluginName=\"" + pluginName + "\" subtitle=\"" + subtitle + "\"");
    }

    void registerWindow(const juce::String& id, const juce::String& title,
                        std::function<void()> restoreCallback)
    {
        registerWindow(id, title, "General", std::move(restoreCallback), nullptr);
    }

    void unregisterWindow(const juce::String& id)
    {
        windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
            [&](const Entry& e) { return e.id == id; }), windows_.end());
        // Clean up empty track groups from expanded state
        cleanEmptyExpanded();
        repaint();
    }

    void setWindowMinimized(const juce::String& id, bool isMinimized)
    {
        for (auto& w : windows_)
            if (w.id == id) { w.minimized = isMinimized; break; }
        if (!isMinimized && getMinimizedCount() <= 1) panelOpen_ = false;
        repaint();
    }

    /** Call after minimizing a plugin window: opens the panel and expands
     *  the track group that owns the entry so the user can immediately see
     *  where the window went and click to restore it. */
    void notifyWindowMinimized(const juce::String& id)
    {
        // Find the track this entry belongs to and auto-expand its group
        // (but do NOT auto-open the panel — user opens it by clicking the bubble).
        for (auto& w : windows_)
        {
            if (w.id == id && w.trackId.isNotEmpty())
            {
                expandedTracks_.insert(w.trackId);
                break;
            }
        }
        repaint();

        // Brief flash: close the panel after 3 seconds if the user hasn't
        // interacted with it, so it doesn't stay open forever.
        }

    int getMinimizedCount() const
    {
        int n = 0;
        for (auto& w : windows_) if (w.minimized) ++n;
        return n;
    }

    // ── Component overrides ───────────────────────────────────────────────

    void resized() override
    {
        if (bubbleX_ < 0.f) { bubbleX_ = 10.f; bubbleY_ = (float)getHeight() - kBubbleSize - 10.f; }
        clampBubble();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t  = Theme::getInstance();
        int   mc = getMinimizedCount();
        auto  bub = bubbleRect();

        // ── Bubble orb ──────────────────────────────────────────────────
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(bub.translated(0.f, 5.f).expanded(2.f));
        juce::ColourGradient bubbleGrad(t.colors.surface.brighter(0.08f), bub.getCentreX(), bub.getY(),
                                        t.colors.surface.darker(0.12f), bub.getCentreX(), bub.getBottom(), false);
        g.setGradientFill(bubbleGrad);
        g.fillEllipse(bub);
        g.setColour(t.colors.border.withAlpha(0.9f));
        g.drawEllipse(bub, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawEllipse(bub.reduced(1.0f), 1.0f);

        if (inMergeZone_)
        {
            g.setColour(t.colors.accent.withAlpha(0.4f));
            g.drawEllipse(bub.expanded(6.f), 2.5f);
            g.setColour(t.colors.accent.withAlpha(0.1f));
            g.fillEllipse(bub.expanded(6.f));
        }

        drawWindowsIcon(g, bub.reduced(14.f), mc > 0 ? t.colors.accent : t.colors.textSecondary.withAlpha(0.8f));

        if (mc > 0)
        {
            auto badge = juce::Rectangle<float>(bub.getRight() - 16.f, bub.getY() - 4.f, 20.f, 20.f);
            g.setColour(t.colors.transportRecord);
            g.fillEllipse(badge);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawEllipse(badge, 1.f);
            g.setColour(juce::Colours::white);
            g.setFont(t.fonts.small);
            g.drawText(juce::String(mc), badge, juce::Justification::centred);
        }

        if (!(panelOpen_ && mc > 0)) return;

        // ── Panel popup ─────────────────────────────────────────────────
        auto panel = panelRect();
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(panel.translated(0.f, 8.f).expanded(2.f), 14.f);
        juce::ColourGradient panelGrad(t.colors.surface.withAlpha(0.97f), panel.getCentreX(), panel.getY(),
                                       t.colors.backgroundLight.withAlpha(0.985f), panel.getCentreX(), panel.getBottom(), false);
        g.setGradientFill(panelGrad);
        g.fillRoundedRectangle(panel, 14.f);
        g.setColour(t.colors.border.withAlpha(0.9f));
        g.drawRoundedRectangle(panel, 14.f, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(panel.reduced(1.f), 13.f, 1.f);

        auto content = panel.reduced(10.f);
        auto header  = content.removeFromTop((float) kHeaderH);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.bold);
        g.drawText("MINIMIZED WINDOWS", header, juce::Justification::centredLeft);
        g.setColour(t.colors.textSecondary);
        g.setFont(t.fonts.small);
        g.drawText(juce::String(mc) + " item(s)", header, juce::Justification::centredRight);

        auto mouse = getMouseXYRelative().toFloat();

        // ── PANELS section ──────────────────────────────────────────────
        auto panelEntries = getMinimizedByCategory("Panels");
        if (!panelEntries.empty())
        {
            auto sec = content.removeFromTop((float) kSectionH);
            g.setColour(t.colors.textSecondary.withAlpha(0.95f));
            g.setFont(t.fonts.small);
            g.drawText("PANELS", sec.reduced(2.f, 0.f), juce::Justification::centredLeft);
            g.setColour(t.colors.border.withAlpha(0.55f));
            g.fillRect(sec.getX(), sec.getBottom() - 1.f, sec.getWidth(), 1.f);

            for (auto* entry : panelEntries)
            {
                auto row = content.removeFromTop((float) kItemH);
                drawEntryRow(g, row, entry, mouse);
            }
        }

        // ── PLUGINS section (grouped by track) ─────────────────────────
        auto trackGroups = buildTrackGroups();
        if (!trackGroups.empty())
        {
            auto sec = content.removeFromTop((float) kSectionH);
            g.setColour(t.colors.textSecondary.withAlpha(0.95f));
            g.setFont(t.fonts.small);
            g.drawText("PLUGINS", sec.reduced(2.f, 0.f), juce::Justification::centredLeft);
            g.setColour(t.colors.border.withAlpha(0.55f));
            g.fillRect(sec.getX(), sec.getBottom() - 1.f, sec.getWidth(), 1.f);

            for (auto& grp : trackGroups)
            {
                bool expanded = expandedTracks_.count(grp.trackId) > 0;

                // Track group header
                auto ghdr = content.removeFromTop((float) kGroupHeaderH);
                auto ghdrBox = ghdr.reduced(2.f, 2.f);
                bool ghdrHov = ghdrBox.contains(mouse);
                g.setColour(ghdrHov ? t.colors.surfaceHover.withAlpha(0.85f) : t.colors.surface.withAlpha(0.45f));
                g.fillRoundedRectangle(ghdrBox, 6.f);
                g.setColour(ghdrHov ? t.colors.accent.withAlpha(0.3f) : t.colors.border.withAlpha(0.35f));
                g.drawRoundedRectangle(ghdrBox, 6.f, 1.f);

                // Arrow
                auto arrowArea = ghdrBox.removeFromLeft(20.f);
                g.setColour(t.colors.textSecondary.withAlpha(0.8f));
                auto ac = arrowArea.getCentre();
                if (expanded)
                {
                    juce::Path tri;
                    tri.addTriangle(ac.x - 4.f, ac.y - 2.f, ac.x + 4.f, ac.y - 2.f, ac.x, ac.y + 3.f);
                    g.fillPath(tri);
                }
                else
                {
                    juce::Path tri;
                    tri.addTriangle(ac.x - 2.f, ac.y - 4.f, ac.x + 3.f, ac.y, ac.x - 2.f, ac.y + 4.f);
                    g.fillPath(tri);
                }

                // Track name + count
                g.setFont(juce::Font(10.5f, juce::Font::bold));
                g.setColour(t.colors.text);
                auto trackLabel = grp.trackName + " (" + juce::String((int)grp.entries.size()) + ")";
                g.drawText(trackLabel, ghdrBox.reduced(4.f, 0.f), juce::Justification::centredLeft, true);

                // Expanded children
                if (expanded)
                {
                    for (auto* entry : grp.entries)
                    {
                        auto row = content.removeFromTop((float) kItemH);
                        auto indentedRow = row.withTrimmedLeft(12.f);
                        drawEntryRow(g, indentedRow, entry, mouse);
                    }
                }
            }
        }

        // ── General / other categories ──────────────────────────────────
        auto otherEntries = getMinimizedExcluding({"Panels", "Plugins"});
        if (!otherEntries.empty())
        {
            auto sec = content.removeFromTop((float) kSectionH);
            g.setColour(t.colors.textSecondary.withAlpha(0.95f));
            g.setFont(t.fonts.small);
            g.drawText("OTHER", sec.reduced(2.f, 0.f), juce::Justification::centredLeft);
            g.setColour(t.colors.border.withAlpha(0.55f));
            g.fillRect(sec.getX(), sec.getBottom() - 1.f, sec.getWidth(), 1.f);

            for (auto* entry : otherEntries)
            {
                auto row = content.removeFromTop((float) kItemH);
                drawEntryRow(g, row, entry, mouse);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (bubbleRect().expanded(4.f).contains(e.position))
        {
            isDragging_ = false;
            dragStart_ = e.position;
            return;
        }

        if (!panelOpen_) return;

        auto panel = panelRect().reduced(10.f);
        panel.removeFromTop((float) kHeaderH);
        bool hitAny = false;

        // ── Panels section ──────────────────────────────────────────────
        auto panelEntries = getMinimizedByCategory("Panels");
        if (!panelEntries.empty())
        {
            panel.removeFromTop((float) kSectionH);
            for (auto* entry : panelEntries)
            {
                auto row = panel.removeFromTop((float) kItemH);
                if (!hitAny && row.contains(e.position))
                {
                    hitAny = handleEntryClick(e, row, entry);
                }
            }
        }

        // ── Plugins section ─────────────────────────────────────────────
        auto trackGroups = buildTrackGroups();
        if (!trackGroups.empty() && !hitAny)
        {
            panel.removeFromTop((float) kSectionH);
            for (auto& grp : trackGroups)
            {
                if (hitAny) break;
                bool expanded = expandedTracks_.count(grp.trackId) > 0;

                auto ghdr = panel.removeFromTop((float) kGroupHeaderH).reduced(2.f, 2.f);
                if (ghdr.contains(e.position))
                {
                    hitAny = true;
                    if (expanded)
                    {
                        expandedTracks_.erase(grp.trackId);
                        DBG("[BubbleTaskbar] Group collapsed: \"" + grp.trackName + "\" trackId=\"" + grp.trackId + "\"");
                    }
                    else
                    {
                        expandedTracks_.insert(grp.trackId);
                        DBG("[BubbleTaskbar] Group expanded: \"" + grp.trackName + "\" trackId=\"" + grp.trackId + "\"");
                    }
                    repaint();
                    break;
                }

                if (expanded)
                {
                    for (auto* entry : grp.entries)
                    {
                        if (hitAny) break;
                        auto row = panel.removeFromTop((float) kItemH).withTrimmedLeft(12.f);
                        if (row.contains(e.position))
                            hitAny = handleEntryClick(e, row, entry);
                    }
                }
            }
        }

        // ── Other section ───────────────────────────────────────────────
        auto otherEntries = getMinimizedExcluding({"Panels", "Plugins"});
        if (!otherEntries.empty() && !hitAny)
        {
            panel.removeFromTop((float) kSectionH);
            for (auto* entry : otherEntries)
            {
                if (hitAny) break;
                auto row = panel.removeFromTop((float) kItemH);
                if (row.contains(e.position))
                    hitAny = handleEntryClick(e, row, entry);
            }
        }

        if (!hitAny) { panelOpen_ = false; repaint(); }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragStart_.x < 0.f) return;
        auto delta = e.position - dragStart_;
        if (!isDragging_ && delta.getDistanceFromOrigin() > (float)kDragThreshold)
            isDragging_ = true;
        if (isDragging_)
        {
            bubbleX_ += delta.x;
            bubbleY_ += delta.y;
            clampBubble();
            dragStart_ = e.position;
            panelOpen_ = false;

            if (orchestrator_)
            {
                auto desired   = getBubbleCenterInParent();
                auto corrected = orchestrator_->resolveRepulsion(bubbleId_, desired, kBubbleSize / 2.f);
                bubbleX_ = corrected.x - kBubbleSize / 2.f;
                bubbleY_ = corrected.y - kBubbleSize / 2.f;
                clampBubble();

                bool          wasIn  = inMergeZone_;
                juce::String  target = orchestrator_->findDeepOverlapTarget(
                                           bubbleId_, getBubbleCenterInParent(), kBubbleSize / 2.f);
                inMergeZone_   = target.isNotEmpty();
                mergeTargetId_ = target;

                if (inMergeZone_ != wasIn)
                {
                    if (inMergeZone_  && onMergePreviewStart) onMergePreviewStart(mergeTargetId_);
                    if (!inMergeZone_ && onMergePreviewEnd)   onMergePreviewEnd();
                }
            }
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragStart_.x >= 0.f && !isDragging_)
        {
            if (bubbleRect().expanded(4.f).contains(e.position) && getMinimizedCount() > 0)
            { panelOpen_ = !panelOpen_; repaint(); }
        }
        if (isDragging_ && inMergeZone_ && onMergeTriggered)
            onMergeTriggered(mergeTargetId_);
        if (onMergePreviewEnd) onMergePreviewEnd();

        dragStart_   = { -1.f, -1.f };
        isDragging_  = false;
        inMergeZone_ = false;
        repaint();
    }

    void mouseMove(const juce::MouseEvent&) override { if (panelOpen_) repaint(); }
    void mouseExit(const juce::MouseEvent&) override { if (panelOpen_) repaint(); }

    bool hitTest(int x, int y) override
    {
        auto pt = juce::Point<float>((float)x, (float)y);
        if (bubbleRect().expanded(4.f).contains(pt)) return true;
        if (panelOpen_ && getMinimizedCount() > 0 && panelRect().contains(pt)) return true;
        return false;
    }

private:
    struct Entry
    {
        juce::String id, title, category;
        std::function<void()> restore;
        std::function<void()> close;
        juce::String trackId;      // non-empty for plugin entries
        juce::String pluginName;   // short plugin name for grouped display
        juce::String subtitle;
        bool minimized = false;
    };

    struct TrackGroup
    {
        juce::String trackId;
        juce::String trackName;
        std::vector<Entry*> entries;
    };

    std::vector<Entry> windows_;
    std::set<juce::String> expandedTracks_;
    bool  panelOpen_      = false;
    float bubbleX_        = -1.f;
    float bubbleY_        = -1.f;
    bool  isDragging_     = false;
    bool  inMergeZone_    = false;
    juce::String        mergeTargetId_;
    juce::Point<float> dragStart_ { -1.f, -1.f };
    BubbleOrchestrator* orchestrator_ = nullptr;
    juce::String        bubbleId_;

    // ── Helpers ───────────────────────────────────────────────────────────

    void clampBubble()
    {
        bubbleX_ = juce::jlimit(0.f, (float)juce::jmax(0, getWidth()  - kBubbleSize), bubbleX_);
        bubbleY_ = juce::jlimit(0.f, (float)juce::jmax(0, getHeight() - kBubbleSize), bubbleY_);
    }

    juce::Rectangle<float> bubbleRect() const
    { return juce::Rectangle<float>(bubbleX_, bubbleY_, (float)kBubbleSize, (float)kBubbleSize).reduced(4.f); }

    juce::Rectangle<float> panelRect() const
    {
        float ph = (float) juce::jmin(kPanelMaxH - 8, getPanelHeight());
        float px = bubbleX_ + kBubbleSize + 8.f;
        if (px + kPanelW > (float)getWidth()) px = bubbleX_ - kPanelW - 8.f;
        float py = bubbleY_ + kBubbleSize * 0.5f - ph * 0.5f;
        py = juce::jlimit(4.f, (float)juce::jmax(1, getHeight()) - ph - 4.f, py);
        return juce::Rectangle<float>(px, py, (float)kPanelW, ph);
    }

    int getPanelHeight() const
    {
        int h = kHeaderH + 20;
        auto panelEntries = getMinimizedByCategory("Panels");
        if (!panelEntries.empty())
            h += kSectionH + (int)panelEntries.size() * kItemH;

        auto trackGroups = buildTrackGroups();
        if (!trackGroups.empty())
        {
            h += kSectionH;
            for (auto& grp : trackGroups)
            {
                h += kGroupHeaderH;
                if (expandedTracks_.count(grp.trackId) > 0)
                    h += (int)grp.entries.size() * kItemH;
            }
        }

        auto other = getMinimizedExcluding({"Panels", "Plugins"});
        if (!other.empty())
            h += kSectionH + (int)other.size() * kItemH;

        return h;
    }

    /** Get all minimized entries in a specific category. */
    std::vector<Entry*> getMinimizedByCategory(const juce::String& cat) const
    {
        std::vector<Entry*> result;
        for (auto& w : windows_)
            if (w.minimized && w.category == cat) result.push_back(const_cast<Entry*>(&w));
        return result;
    }

    /** Get all minimized entries NOT in any of the listed categories. */
    std::vector<Entry*> getMinimizedExcluding(std::initializer_list<juce::String> cats) const
    {
        std::vector<Entry*> result;
        for (auto& w : windows_)
        {
            if (!w.minimized) continue;
            bool excluded = false;
            for (auto& c : cats) if (w.category == c) { excluded = true; break; }
            if (!excluded) result.push_back(const_cast<Entry*>(&w));
        }
        return result;
    }

    /** Build track groups from minimized plugin entries. */
    std::vector<TrackGroup> buildTrackGroups() const
    {
        std::vector<TrackGroup> groups;
        for (auto& w : windows_)
        {
            if (!w.minimized || w.category != "Plugins") continue;
            // Find or create group
            TrackGroup* grp = nullptr;
            for (auto& g : groups)
                if (g.trackId == w.trackId) { grp = &g; break; }
            if (!grp)
            {
                groups.push_back({ w.trackId, extractTrackName(w), {} });
                grp = &groups.back();
            }
            grp->entries.push_back(const_cast<Entry*>(&w));
        }
        return groups;
    }

    /** Extract track name from title ("TrackName - PluginName" format). */
    static juce::String extractTrackName(const Entry& e)
    {
        if (e.pluginName.isNotEmpty() && e.title.contains(e.pluginName))
        {
            // title = "TrackName - PluginName", extract before the em-dash
            auto dashIdx = e.title.indexOf(juce::String::charToString(0x2014));
            if (dashIdx > 0)
                return e.title.substring(0, dashIdx).trim();
        }
        // Fallback: use trackId or full title
        return e.trackId.isNotEmpty() ? e.trackId : e.title;
    }

    /** Remove expanded state for tracks with no remaining minimized plugins. */
    void cleanEmptyExpanded()
    {
        auto groups = buildTrackGroups();
        std::set<juce::String> activeTrackIds;
        for (auto& g : groups) activeTrackIds.insert(g.trackId);
        for (auto it = expandedTracks_.begin(); it != expandedTracks_.end(); )
        {
            if (activeTrackIds.count(*it) == 0)
            {
                DBG("[BubbleTaskbar] Group removed (empty): trackId=\"" + *it + "\"");
                it = expandedTracks_.erase(it);
            }
            else ++it;
        }
    }

    // ── Drawing helpers ───────────────────────────────────────────────────

    void drawEntryRow(juce::Graphics& g, juce::Rectangle<float> row, Entry* entry,
                      juce::Point<float> mouse) const
    {
        auto& t = Theme::getInstance();
        auto rowBox = row.reduced(2.f, 2.f);
        bool hov = rowBox.contains(mouse);
        g.setColour(hov ? t.colors.surfaceHover.withAlpha(0.95f) : t.colors.backgroundDark.withAlpha(0.65f));
        g.fillRoundedRectangle(rowBox, 7.f);
        g.setColour(hov ? t.colors.accent.withAlpha(0.35f) : t.colors.border.withAlpha(0.4f));
        g.drawRoundedRectangle(rowBox, 7.f, 1.f);

        // Close X button (right side)
        auto closeBtn = juce::Rectangle<float>(rowBox.getRight() - 22.f, rowBox.getCentreY() - 7.f, 14.f, 14.f);
        bool closeHov = closeBtn.contains(mouse);
        if (closeHov)
        {
            g.setColour(juce::Colour(0xFFEF4444).withAlpha(0.8f));
            g.fillRoundedRectangle(closeBtn, 3.f);
        }
        g.setColour(closeHov ? juce::Colours::white : t.colors.textSecondary.withAlpha(0.5f));
        auto cc = closeBtn.getCentre();
        float cs = 3.f;
        g.drawLine(cc.x - cs, cc.y - cs, cc.x + cs, cc.y + cs, 1.4f);
        g.drawLine(cc.x + cs, cc.y - cs, cc.x - cs, cc.y + cs, 1.4f);

        // Icon dot
        auto contentBox = rowBox.withTrimmedRight(26.f);
        auto icon = contentBox.removeFromLeft(22.f).reduced(5.f);
        g.setColour(entry->category == "Panels" ? t.colors.textSecondary.withAlpha(0.7f)
                                                 : t.colors.accent.withAlpha(0.9f));
        g.fillEllipse(icon);

        auto textBox = contentBox.reduced(6.f, 0.f);
        if (entry->subtitle.isNotEmpty())
        {
            auto titleBox = textBox.removeFromTop(14.f);
            textBox.removeFromTop(1.f);
            g.setColour(t.colors.text);
            g.setFont(juce::Font(9.8f, juce::Font::bold));
            g.drawFittedText(entry->title, titleBox.toNearestInt(), juce::Justification::centredLeft, 1);

            g.setColour(t.colors.textSecondary.withAlpha(0.9f));
            g.setFont(juce::Font(8.3f));
            g.drawFittedText(entry->subtitle, textBox.toNearestInt(), juce::Justification::centredLeft, 2);
        }
        else
        {
            // Display name: for grouped plugins show just pluginName; for panels show title
            juce::String displayName = entry->pluginName.isNotEmpty() ? entry->pluginName : entry->title;
            g.setColour(t.colors.text);
            g.setFont(t.fonts.regular);
            g.drawText(displayName, textBox, juce::Justification::centredLeft, true);
        }
    }

    /** Handle click on an entry row. Returns true if hit. */
    bool handleEntryClick(const juce::MouseEvent& e, juce::Rectangle<float> row, Entry* entry)
    {
        auto rowBox = row.reduced(2.f, 2.f);
        if (!rowBox.contains(e.position)) return false;

        // Check X close button
        auto closeBtn = juce::Rectangle<float>(rowBox.getRight() - 22.f, rowBox.getCentreY() - 7.f, 14.f, 14.f);
        if (closeBtn.contains(e.position))
        {
            DBG("[BubbleTaskbar] Close X pressed for \"" + entry->title + "\" id=\"" + entry->id + "\"");
            auto closeId = entry->id;
            auto closeFn = entry->close;
            if (closeFn) closeFn();
            unregisterWindow(closeId);
            if (getMinimizedCount() == 0) panelOpen_ = false;
            repaint();
            return true;
        }

        // Body click = restore
        DBG("[BubbleTaskbar] Restore clicked for \"" + entry->title + "\" id=\"" + entry->id + "\"");
        if (entry->restore) entry->restore();
        panelOpen_ = false;
        repaint();
        return true;
    }

    void drawWindowsIcon(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour c) const
    {
        float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
        g.setColour(c.withAlpha(0.45f));
        g.fillRoundedRectangle(x + 4.f, y, w - 4.f, h - 4.f, 2.f);
        g.setColour(c.darker(0.2f));
        g.drawRoundedRectangle(x + 4.f, y, w - 4.f, h - 4.f, 2.f, 1.f);
        g.setColour(c);
        g.fillRoundedRectangle(x, y + 4.f, w - 4.f, h - 4.f, 2.f);
        g.setColour(c.darker(0.3f));
        g.drawRoundedRectangle(x, y + 4.f, w - 4.f, h - 4.f, 2.f, 1.f);
        g.setColour(c.brighter(0.3f));
        g.fillRect(x + 2.f, y + 6.f, w - 8.f, 2.5f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubbleTaskbar)
};

} // namespace DAW

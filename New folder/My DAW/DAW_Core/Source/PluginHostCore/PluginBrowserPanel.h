#pragma once
#include <JuceHeader.h>
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../ThemeCore/Theme.h"
#include "PluginScannerCore.h"

namespace DAW {

/**
 * PluginBrowserPanel
 *
 * Nucleus: searchable, categorized FX explorer.
 * Groups plugins by:  Vendor → Name [Format]
 * Has a search bar at the top.
 * Double-click or drag a plugin to add to selected track.
 */
class PluginBrowserPanel : public juce::Component
{
public:
    static constexpr int kPreferredWidth = 240;
    static constexpr int kRowH = 22;

    /** Fired when user double-clicks a plugin to add it. */
    std::function<void(const juce::PluginDescription&)> onPluginSelected;

    /** Fired when user starts dragging a plugin (for drop onto a track). */
    std::function<void(const juce::PluginDescription&)> onPluginDragStarted;

    explicit PluginBrowserPanel(PluginScannerCore& scanner)
        : scanner_(scanner)
    {
        searchBox_.setTextToShowWhenEmpty("Search plugins...", juce::Colours::grey);
        searchBox_.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible(searchBox_);

        viewport_.setScrollBarsShown(true, false);
        viewport_.setViewedComponent(&listContent_, false);
        addAndMakeVisible(viewport_);

        rebuildList();
    }

    /** Refresh from scanner's known plugin list. */
    void refresh() { rebuildList(); }

    int getVisiblePluginItemCount() const
    {
        int count = 0;
        for (const auto& row : rows_)
            if (!row.isHeader)
                ++count;
        return count;
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.backgroundDark.darker(0.05f));

        // Header
        auto header = getLocalBounds().removeFromTop(28).toFloat();
        g.setColour(t.colors.surface.darker(0.1f));
        g.fillRect(header);
        g.setFont(juce::Font(10.f, juce::Font::bold));
        g.setColour(t.colors.accent);
        g.drawText("PLUGIN BROWSER", header.reduced(8.f, 0.f), juce::Justification::centredLeft);
        g.setColour(t.colors.textSecondary.withAlpha(0.5f));
        g.drawText(juce::String(scanner_.getKnownPlugins().getNumTypes()) + " plugins",
                   header.reduced(8.f, 0.f), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop(28); // header
        searchBox_.setBounds(b.removeFromTop(26).reduced(4, 2));
        viewport_.setBounds(b);
        layoutList();
    }

private:
    PluginScannerCore& scanner_;
    juce::TextEditor searchBox_;
    juce::Viewport   viewport_;
    juce::Component  listContent_;

    struct RowData
    {
        bool isHeader = false;
        juce::String text;
        juce::PluginDescription desc;
    };
    std::vector<RowData> rows_;
    juce::OwnedArray<juce::Component> rowWidgets_;
    int selectedRowIndex_ = -1;

    void rebuildList()
    {
        scanner_.ensureBrowserVisibleDataReady("PluginBrowserPanel::rebuildList");

        auto knownCount = scanner_.getKnownPlugins().getNumTypes();
        auto filter = searchBox_.getText().trim().toLowerCase();
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginBrowserPanel::rebuildList start"
                " knownPluginCount=" + juce::String(knownCount)
                + " filter=\"" + filter + "\"");

        rows_.clear();
        rowWidgets_.clear();
        listContent_.removeAllChildren();

        auto& known = scanner_.getKnownPlugins();
        const auto& list = known.getTypes();

        // Group by manufacturer
        juce::StringArray manufacturers;
        for (auto& d : list)
            manufacturers.addIfNotAlreadyThere(d.manufacturerName);
        manufacturers.sort(true);

        for (auto& mfr : manufacturers)
        {
            bool anyMatch = false;
            std::vector<RowData> mfrRows;

            for (auto& d : list)
            {
                if (d.manufacturerName != mfr) continue;
                if (filter.isNotEmpty() &&
                    !d.name.toLowerCase().contains(filter) &&
                    !d.manufacturerName.toLowerCase().contains(filter))
                    continue;

                mfrRows.push_back({ false,
                    d.name + "  [" + d.pluginFormatName + "]", d });
                anyMatch = true;
            }

            if (anyMatch)
            {
                rows_.push_back({ true, mfr, {} });
                for (auto& r : mfrRows)
                    rows_.push_back(std::move(r));
            }
        }

        // Create widgets
        for (int i = 0; i < (int)rows_.size(); ++i)
        {
            auto* w = new PluginRow(*this, i);
            rowWidgets_.add(w);
            listContent_.addAndMakeVisible(w);
        }

        layoutList();

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginBrowserPanel::rebuildList end"
                " knownPluginCount=" + juce::String(knownCount)
                + " browserItemCount=" + juce::String(getVisiblePluginItemCount())
                + " rowCount=" + juce::String((int) rows_.size()));
    }

    void activateRow(int rowIndex)
    {
        if (rowIndex < 0 || rowIndex >= (int) rows_.size())
            return;

        auto& row = rows_[(size_t) rowIndex];
        if (row.isHeader)
            return;

        selectedRowIndex_ = rowIndex;
        listContent_.repaint();

        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginBrowserPanel::activateRow"
                " rowIndex=" + juce::String(rowIndex)
                + " plugin=\"" + row.desc.name + "\""
                + " manufacturer=\"" + row.desc.manufacturerName + "\""
                + " format=\"" + row.desc.pluginFormatName + "\"");

        if (onPluginSelected)
            onPluginSelected(row.desc);
    }

    void layoutList()
    {
        int y = 0;
        int w = viewport_.getWidth() - (viewport_.isVerticalScrollBarShown() ? 10 : 0);
        for (auto* widget : rowWidgets_)
        {
            widget->setBounds(0, y, w, kRowH);
            y += kRowH;
        }
        listContent_.setSize(juce::jmax(1, w), juce::jmax(y, viewport_.getHeight()));
    }

    class PluginRow : public juce::Component
    {
    public:
        PluginRow(PluginBrowserPanel& owner, int index)
            : owner_(owner), index_(index) {}

        void paint(juce::Graphics& g) override
        {
            auto& t = Theme::getInstance();
            auto& rd = owner_.rows_[index_];
            auto b = getLocalBounds().toFloat();

            if (rd.isHeader)
            {
                g.setColour(t.colors.surface.darker(0.05f));
                g.fillRect(b);
                g.setFont(juce::Font(9.5f, juce::Font::bold));
                g.setColour(t.colors.accent.withAlpha(0.8f));
                g.drawText(rd.text, b.reduced(8.f, 0.f), juce::Justification::centredLeft);
            }
            else
            {
                if (index_ == owner_.selectedRowIndex_)
                {
                    g.setColour(t.colors.accent.withAlpha(0.18f));
                    g.fillRect(b);
                }
                if (hovered_)
                {
                    g.setColour(t.colors.accent.withAlpha(0.1f));
                    g.fillRect(b);
                }
                g.setFont(juce::Font(10.f));
                g.setColour(t.colors.text);
                g.drawText(rd.text, b.reduced(16.f, 0.f), juce::Justification::centredLeft, true);
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }

        void mouseUp(const juce::MouseEvent&) override
        {
            owner_.activateRow(index_);
        }

    private:
        PluginBrowserPanel& owner_;
        int  index_;
        bool hovered_ = false;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserPanel)
};

} // namespace DAW

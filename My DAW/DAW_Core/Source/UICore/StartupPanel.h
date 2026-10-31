#pragma once
#include <JuceHeader.h>
#include <array>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"

namespace DAW {

class StartupPanel : public juce::Component
{
public:
    std::function<void()> onNewProject;
    std::function<void()> onOpenProject;
    std::function<void()> onAudioSettings;
    std::function<void(const juce::File&)> onOpenRecent;
    std::function<void()> onRescanPlugins;
    std::function<void()> onUpgradeProject;
    std::function<void()> onDismissed;

    StartupPanel()
    {
        setOpaque(false);
        setAlwaysOnTop(true);
        loadRecentFiles();

        // Keep the existing custom-painted AAA surface, but expose the Open
        // action as a real button so keyboard/accessibility/UI Automation
        // clients can discover and invoke it without coordinate guessing.
        openProjectButton_.setButtonText("Open Project");
        openProjectButton_.setTitle("Open Project");
        openProjectButton_.setDescription("Browse for an existing project");
        openProjectButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        openProjectButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        openProjectButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        openProjectButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        openProjectButton_.onClick = [this]
        {
            if (onOpenProject)
                onOpenProject();
        };
        addAndMakeVisible(openProjectButton_);

        rescanPluginsButton_.setButtonText("Rescan Plugins");
        rescanPluginsButton_.setTitle("Rescan Plugins");
        rescanPluginsButton_.setDescription("Rescan installed plugins");
        rescanPluginsButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        rescanPluginsButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        rescanPluginsButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        rescanPluginsButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        rescanPluginsButton_.onClick = [this]
        {
            if (onRescanPlugins)
                onRescanPlugins();
        };
        addAndMakeVisible(rescanPluginsButton_);

        upgradeProjectButton_.setButtonText("Upgrade Project");
        upgradeProjectButton_.setTitle("Upgrade Project");
        upgradeProjectButton_.setDescription("Repair an old project file so it uses every feature of this version");
        upgradeProjectButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        upgradeProjectButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        upgradeProjectButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        upgradeProjectButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
        upgradeProjectButton_.onClick = [this]
        {
            if (onUpgradeProject)
                onUpgradeProject();
        };
        addAndMakeVisible(upgradeProjectButton_);
    }

    void setProjectName(const juce::String& name)
    {
        projectName_ = name;
        repaint();
    }

    void setPluginInfo(int loadedCount, int failureCount)
    {
        pluginCount_ = loadedCount;
        failureCount_ = failureCount;
        repaint();
    }

    void setRecentFiles(const std::vector<juce::File>& files)
    {
        recentFiles_ = files;
        repaint();
    }

    void addRecentFile(const juce::File& file)
    {
        if (file == juce::File() || !file.existsAsFile())
            return;
        // Remove duplicates
        recentFiles_.erase(
            std::remove_if(recentFiles_.begin(), recentFiles_.end(),
                [&](const juce::File& f) { return f == file; }),
            recentFiles_.end());
        recentFiles_.insert(recentFiles_.begin(), file);
        if (recentFiles_.size() > 10)
            recentFiles_.resize(10);
        saveRecentFiles();
        repaint();
    }

    void dismiss()
    {
        const bool wasVisible = isVisible();
        setVisible(false);
        if (wasVisible && onDismissed)
            onDismissed();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto& a = t.apex;
        g.fillAll(a.color.deepestA.withAlpha(0.90f));

       #if JUCE_ANDROID
        // The Windows 540x608 splash dialog extends below a phone's landscape
        // window. Use the SAME real actions, compacted without clipping.
        paintAndroidCompact(g);
        return;
       #endif

        auto box = getLocalBounds().withSizeKeepingCentre(540, 608).toFloat();
        ApexPrimitives::drawGlow(g, box.reduced(3.0f), true, a.color.violet);
        ApexPrimitives::drawPanel(g, box, ApexPrimitives::PanelDepth::floating, true, a.color.magenta);
        ApexPrimitives::drawSlashFlow(g, slashFlow_, a.color.magenta, a.color.cyan, 0.11f);

        auto inner = box.reduced(32.f);

        // X close button (top-right of box)
        auto closeArea = juce::Rectangle<int>(
            (int)box.getRight() - 32, (int)box.getY() + 4, 28, 28).toFloat();
        bool closeHovered = hoveredBtn_ == kCloseBtnIdx;
        g.setColour(a.color.panelC.withAlpha(closeHovered ? 0.95f : 0.60f));
        g.fillRoundedRectangle(closeArea, 4.f);
        g.setColour(closeHovered ? a.color.textPrimary : a.color.textSecondary);
        g.setFont(t.fonts.bold.withHeight(14.0f));
        g.drawText(juce::CharPointer_UTF8("\xC3\x97"), closeArea, juce::Justification::centred);
        btnAreas_[kCloseBtnIdx] = closeArea.toNearestInt();

        // Title
        g.setFont(t.fonts.bold.withHeight(24.0f));
        g.setColour(a.color.textPrimary);
        g.drawText(projectName_.isNotEmpty() ? projectName_ : "APEX",
                   inner.removeFromTop(34.f), juce::Justification::centred);

        auto signalLine = inner.removeFromTop(2.0f).reduced(130.0f, 0.0f);
        juce::ColourGradient signal(a.color.magenta, signalLine.getX(), signalLine.getCentreY(),
                                    a.color.cyan, signalLine.getRight(), signalLine.getCentreY(), false);
        g.setGradientFill(signal);
        g.fillRoundedRectangle(signalLine, 1.0f);

        inner.removeFromTop(4.f);
        g.setFont(t.fonts.regular.withHeight(10.5f));
        g.setColour(a.color.textMuted);
        g.drawText("Version " + juce::String(ProjectInfo::versionString),
                   inner.removeFromTop(14.f), juce::Justification::centred);

        inner.removeFromTop(18.f);

        // Action buttons
        auto btnArea = inner.removeFromTop(232.f);
        const int btnH = 40;
        const int btnGap = 8;

        auto drawBtn = [&](juce::Rectangle<float> area, const juce::String& label, const juce::String& sub, juce::Colour accent, bool hovered)
        {
            g.setColour(hovered ? a.color.panelC : a.color.panelB);
            g.fillRoundedRectangle(area, a.metric.radiusControl);
            g.setColour(accent.withAlpha(hovered ? 0.82f : 0.34f));
            g.drawRoundedRectangle(area.reduced(0.5f), a.metric.radiusControl, a.metric.strokeThin);
            g.setColour(accent.withAlpha(hovered ? 0.95f : 0.65f));
            g.fillRoundedRectangle(area.getX(), area.getY() + 6.0f, 3.0f, area.getHeight() - 12.0f, 1.5f);
            g.setFont(t.fonts.bold.withHeight(13.0f));
            g.setColour(a.color.textPrimary);
            g.drawText(label, area.reduced(14, 0).removeFromTop(23.0f), juce::Justification::centredLeft);
            g.setFont(t.fonts.regular.withHeight(10.5f));
            g.setColour(a.color.textSecondary);
            g.drawText(sub, area.reduced(14, 0).withTrimmedTop(20.0f), juce::Justification::centredLeft);
        };

        auto newBtn = btnArea.removeFromTop(btnH).toFloat();
        drawBtn(newBtn, "New Project", "Start a clean APEX session", a.color.magenta, hoveredBtn_ == 0);

        btnArea.removeFromTop(btnGap);
        auto openBtn = btnArea.removeFromTop(btnH).toFloat();
        drawBtn(openBtn, "Open Project", "Browse for an existing .dawproj", a.color.violet, hoveredBtn_ == 1);

        btnArea.removeFromTop(btnGap);
        auto audioBtn = btnArea.removeFromTop(btnH).toFloat();
        drawBtn(audioBtn, "Audio Settings", "Configure device, driver, and I/O", a.color.cyan, hoveredBtn_ == 2);

        btnArea.removeFromTop(btnGap);
        auto rescanBtn = btnArea.removeFromTop(btnH).toFloat();
        juce::String rescanSub = juce::String(pluginCount_) + " plugins loaded";
        if (failureCount_ > 0)
            rescanSub += "  |  " + juce::String(failureCount_) + " failures";
        drawBtn(rescanBtn, "Rescan Plugins", rescanSub, a.color.blue, hoveredBtn_ == 3);

        btnArea.removeFromTop(btnGap);
        auto upgradeBtn = btnArea.removeFromTop(btnH).toFloat();
        drawBtn(upgradeBtn, "Upgrade Project", "Repair an old project for this version", a.color.cyan, hoveredBtn_ == 4);

        btnAreas_[0] = newBtn.toNearestInt();
        btnAreas_[1] = openBtn.toNearestInt();
        btnAreas_[2] = audioBtn.toNearestInt();
        btnAreas_[3] = rescanBtn.toNearestInt();
        btnAreas_[4] = upgradeBtn.toNearestInt();

        inner.removeFromTop(16.f);

        // Recent projects
        if (!recentFiles_.empty())
        {
            g.setFont(t.fonts.bold.withHeight(12.0f));
            g.setColour(a.color.textSecondary);
            g.drawText("RECENT PROJECTS", inner.removeFromTop(18.f), juce::Justification::centredLeft);

            inner.removeFromTop(4.f);
            auto recentArea = inner.removeFromTop(juce::jmin((int)recentFiles_.size(), 5) * 34);
            int idx = 5;
            for (size_t i = 0; i < recentFiles_.size() && i < 5; ++i)
            {
                auto item = recentArea.removeFromTop(34);
                bool hovered = hoveredBtn_ == idx;
                g.setColour(hovered ? a.color.panelC : a.color.deepestB.withAlpha(0.55f));
                g.fillRoundedRectangle(item.toFloat(), 5.f);
                g.setColour(hovered ? a.color.violet : a.color.borderSoftA);
                g.drawRoundedRectangle(item.toFloat().reduced(0.5f), 5.f, 1.0f);
                g.setFont(t.fonts.bold.withHeight(12.5f));
                g.setColour(hovered ? a.color.textPrimary : a.color.textSecondary);
                g.drawText(recentFiles_[i].getFileNameWithoutExtension(),
                           item.reduced(10, 0).removeFromTop(20).toFloat(), juce::Justification::centredLeft);
                g.setFont(t.fonts.regular.withHeight(9.5f));
                g.setColour(a.color.textMuted);
                g.drawText(recentFiles_[i].getParentDirectory().getFullPathName(),
                           item.reduced(10, 0).withTrimmedTop(17).toFloat(), juce::Justification::centredLeft, true);
                btnAreas_[idx] = item.toNearestInt();
                ++idx;
            }
        }
        else
        {
            g.setFont(t.fonts.regular.withHeight(11.0f));
            g.setColour(a.color.textMuted);
            g.drawText("No recent projects", inner.removeFromTop(16.f), juce::Justification::centredLeft);
        }

        // Close hint
        auto remaining = inner;
        g.setFont(t.fonts.regular.withHeight(10.0f));
        g.setColour(a.color.textMuted);
        g.drawText("Choose a project or click outside the panel to return", remaining.removeFromBottom(20.f), juce::Justification::centred);
    }

    void resized() override
    {
       #if JUCE_ANDROID
        for (int i = 0; i < 5; ++i)
            btnAreas_[i] = androidButtonBounds(i);
        btnAreas_[kCloseBtnIdx] = androidCloseBounds();
        openProjectButton_.setBounds(btnAreas_[1]);
        rescanPluginsButton_.setBounds(btnAreas_[3]);
        upgradeProjectButton_.setBounds(btnAreas_[4]);
        return;
       #endif
        auto box = getLocalBounds().withSizeKeepingCentre(540, 608);
        auto inner = box.reduced(32);
        slashFlow_ = ApexPrimitives::buildSlashFlowPaths(box.toFloat().reduced(10.0f), 0xA9E12026u, 10);
        inner.removeFromTop(34);
        inner.removeFromTop(2);
        inner.removeFromTop(4);
        inner.removeFromTop(14);
        inner.removeFromTop(18);

        auto buttonArea = inner.removeFromTop(232);
        constexpr int buttonHeight = 40;
        constexpr int buttonGap = 8;
        buttonArea.removeFromTop(buttonHeight);
        buttonArea.removeFromTop(buttonGap);
        openProjectButton_.setBounds(buttonArea.removeFromTop(buttonHeight));
        buttonArea.removeFromTop(buttonGap);
        buttonArea.removeFromTop(buttonHeight); // Audio Settings
        buttonArea.removeFromTop(buttonGap);
        rescanPluginsButton_.setBounds(buttonArea.removeFromTop(buttonHeight));
        buttonArea.removeFromTop(buttonGap);
        upgradeProjectButton_.setBounds(buttonArea.removeFromTop(buttonHeight));
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int oldHover = hoveredBtn_;
        hoveredBtn_ = hitTestButton(e.position.toInt());
        if (oldHover != hoveredBtn_)
            repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoveredBtn_ != -1)
        {
            hoveredBtn_ = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        int btn = hitTestButton(e.position.toInt());
        if (btn == kCloseBtnIdx) // X close
        {
            dismiss();
        }
        else if (btn == 0) // New
        {
            if (onNewProject) onNewProject();
        }
        else if (btn == 1) // Open
        {
            if (onOpenProject) onOpenProject();
        }
        else if (btn == 2) // Audio settings
        {
            if (onAudioSettings) onAudioSettings();
        }
        else if (btn == 3) // Rescan plugins
        {
            if (onRescanPlugins) onRescanPlugins();
        }
        else if (btn == 4) // Upgrade Project
        {
            if (onUpgradeProject) onUpgradeProject();
        }
        else if (btn >= 5 && btn < 5 + (int)recentFiles_.size())
        {
            int idx = btn - 5;
            if (idx >= 0 && idx < (int)recentFiles_.size())
            {
                if (onOpenRecent) onOpenRecent(recentFiles_[idx]);
            }
        }
        else
        {
            dismiss();
        }
    }

    bool hitTest(int, int) override { return true; }

private:
   #if JUCE_ANDROID
    // A true responsive layout driven solely by the *current* JUCE content
    // bounds. Supports landscape phones, unfolded tablets, split screen,
    // resizable Android windows and DPI changes without fixed PC dimensions.
    // The exact same rectangles drive paint, hit testing and real buttons.
    struct AndroidLayout
    {
        juce::Rectangle<int> panel;
        juce::Rectangle<int> title;
        juce::Rectangle<int> close;
        std::array<juce::Rectangle<int>, 5> actions {};
        bool twoColumns = false;
    };

    AndroidLayout makeAndroidLayout() const
    {
        AndroidLayout layout;
        const int viewW = juce::jmax(1, getWidth());
        const int viewH = juce::jmax(1, getHeight());
        const int edge = juce::jlimit(4, 16, juce::jmin(viewW, viewH) / 30);
        auto available = getLocalBounds().reduced(edge);
        if (available.isEmpty())
            available = { 0, 0, viewW, viewH };

        // Landscape: two columns keep five actions fully visible above the
        // gesture/nav bar; compact portrait uses a vertical stack instead.
        layout.twoColumns = available.getWidth() >= 560
            && available.getWidth() > available.getHeight() * 13 / 10;
        const int targetW = juce::jmin(available.getWidth(),
                                      layout.twoColumns ? 880 : 540);
        const int targetH = juce::jmin(available.getHeight(),
                                      layout.twoColumns ? 400 : 550);
        layout.panel = available.withSizeKeepingCentre(juce::jmax(1, targetW),
                                                       juce::jmax(1, targetH));

        const int inset = juce::jlimit(5, 20, layout.panel.getWidth() / 35);
        const int titleH = juce::jlimit(36, 64, layout.panel.getHeight() / 6);
        const int gap = juce::jlimit(3, 10, layout.panel.getHeight() / 64);
        layout.title = {layout.panel.getX() + inset, layout.panel.getY() + 2,
                        juce::jmax(1, layout.panel.getWidth() - inset * 2),
                        titleH};
        layout.close = {layout.panel.getRight() - inset - 34,
                        layout.panel.getY() + 5, 32, 32};
        const int rows = layout.twoColumns ? 3 : 5;
        const int rowY = layout.panel.getY() + titleH + 5;
        const int usableH = juce::jmax(1, layout.panel.getBottom() - rowY - inset);
        const int rowH = juce::jmax(1, (usableH - (rows - 1) * gap) / rows);
        const int actionW = juce::jmax(1, layout.panel.getWidth() - 2 * inset);
        const int colGap = juce::jlimit(4, 12, actionW / 65);
        const int colW = (actionW - colGap) / 2;

        for (int i = 0; i < 5; ++i)
        {
            const int row = layout.twoColumns ? i / 2 : i;
            const int col = layout.twoColumns ? i % 2 : 0;
            // The final action spans the full last row rather than leaving
            // dead space or moving off the bottom of the screen.
            const bool fullRow = !layout.twoColumns || i == 4;
            layout.actions[(size_t) i] = {
                layout.panel.getX() + inset + (fullRow ? 0 : col * (colW + colGap)),
                rowY + row * (rowH + gap),
                fullRow ? actionW : colW,
                rowH
            };
        }
        return layout;
    }

    juce::Rectangle<int> androidButtonBounds(int index) const
    {
        if (index < 0 || index >= 5)
            return {};
        return makeAndroidLayout().actions[(size_t) index];
    }

    juce::Rectangle<int> androidCloseBounds() const
    {
        return makeAndroidLayout().close;
    }

    void paintAndroidCompact(juce::Graphics& g)
    {
        const auto layout = makeAndroidLayout();
        auto& a = Theme::getInstance().apex;
        ApexPrimitives::drawPanel(g, layout.panel.toFloat(),
                                  ApexPrimitives::PanelDepth::floating,
                                  true, a.color.magenta);

        g.setColour(a.color.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(
            juce::jlimit(17.0f, 27.0f,
                         (float) layout.title.getHeight() * 0.53f),
            juce::Font::bold)));
        g.drawText(projectName_.isNotEmpty() ? projectName_ : "APEX",
                   layout.title.reduced(40, 0),
                   juce::Justification::centred, true);

        const char* labels[] = { "New Project", "Open Project", "Audio Settings",
                                 "Rescan Plugins", "Upgrade Project" };
        const char* hints[]  = { "Start a clean studio session", "Browse .dawproj",
                                 "Select input / output", "Refresh available effects",
                                 "Repair an older project" };
        const juce::Colour accents[] = { a.color.magenta, a.color.violet,
                                         a.color.cyan, a.color.blue, a.color.cyan };

        for (int i = 0; i < 5; ++i)
        {
            const auto b = layout.actions[(size_t) i];
            btnAreas_[i] = b;
            const bool hovered = hoveredBtn_ == i;
            g.setColour(hovered ? a.color.panelC : a.color.panelB);
            g.fillRoundedRectangle(b.toFloat(), 5.0f);
            g.setColour(accents[i].withAlpha(hovered ? 0.96f : 0.72f));
            g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 5.0f, 1.0f);
            const int textInset = juce::jlimit(5, 12, b.getWidth() / 30);
            if (b.getHeight() >= 39)
            {
                auto labelRect = b.reduced(textInset, 1);
                labelRect.setHeight(juce::jmax(1, b.getHeight() / 2 + 2));
                g.setColour(a.color.textPrimary);
                g.setFont(juce::Font(juce::FontOptions(
                    juce::jlimit(11.0f, 16.0f, (float) b.getHeight() * 0.29f),
                    juce::Font::bold)));
                g.drawText(labels[i], labelRect,
                           juce::Justification::centredLeft, true);
                g.setColour(a.color.textSecondary);
                g.setFont(juce::Font(juce::FontOptions(
                    juce::jlimit(9.0f, 12.0f, (float) b.getHeight() * 0.2f))));
                g.drawText(hints[i], b.reduced(textInset, 1)
                                     .withTrimmedTop(b.getHeight() / 2),
                           juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour(a.color.textPrimary);
                g.setFont(juce::Font(juce::FontOptions(
                    juce::jlimit(10.0f, 15.0f, (float) b.getHeight() * 0.45f),
                    juce::Font::bold)));
                g.drawText(labels[i], b.reduced(textInset, 1),
                           juce::Justification::centredLeft, true);
            }
        }
        btnAreas_[kCloseBtnIdx] = layout.close;
        g.setColour(a.color.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(20.0f)));
        g.drawText("x", layout.close, juce::Justification::centred);
    }
   #endif

    juce::TextButton openProjectButton_;
    juce::TextButton rescanPluginsButton_;
    juce::TextButton upgradeProjectButton_;
    static constexpr int kCloseBtnIdx = 14;
    juce::String projectName_;
    int pluginCount_ = 0;
    int failureCount_ = 0;
    std::vector<juce::File> recentFiles_;
    std::vector<juce::Path> slashFlow_;
    juce::Rectangle<int> btnAreas_[15];
    int hoveredBtn_ = -1;

    int hitTestButton(juce::Point<int> pt) const
    {
        for (int i = 0; i < 15; ++i)
            if (btnAreas_[i].contains(pt))
                return i;
        return -1;
    }

    juce::File getSettingsFile() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core").getChildFile("recent_projects.txt");
    }

    void loadRecentFiles()
    {
        auto f = getSettingsFile();
        if (!f.existsAsFile()) return;
        auto lines = juce::StringArray::fromLines(f.loadFileAsString());
        for (auto& line : lines)
        {
            auto file = juce::File(line.trim());
            if (file.existsAsFile())
                recentFiles_.push_back(file);
        }
    }

    void saveRecentFiles()
    {
        auto f = getSettingsFile();
        f.getParentDirectory().createDirectory();
        juce::StringArray lines;
        for (auto& file : recentFiles_)
            lines.add(file.getFullPathName());
        f.replaceWithText(lines.joinIntoString("\n"));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StartupPanel)
};

} // namespace DAW

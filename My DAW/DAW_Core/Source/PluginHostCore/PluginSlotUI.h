#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../PluginHostCore/PluginInstanceCore.h"
#include "../PluginHostCore/PluginHostingProductPolicyCore.h"
#include "../PluginHostCore/PluginScannerCore.h"

namespace DAW {

/**
 * PluginSlotUI
 *
 * Nucleus: a single insert slot widget for the mixer strip.
 * - Empty slot: click → browse and load a plugin
 * - Loaded slot: click → open editor, right-click → bypass / remove
 *
 * Lives inside MixerStrip. One per chain slot (up to PluginChainCore::kMaxSlots).
 */
class PluginSlotUI : public juce::Component
{
public:
    static constexpr int kSlotH = 18;

    PluginSlotUI(int slotIndex, PluginChainCore& chain, PluginScannerCore& scanner)
        : slotIndex_(slotIndex), chain_(chain), scanner_(scanner) {}

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto  b = getLocalBounds().toFloat().reduced(1.f, 1.f);
        bool  hasPlugin = chain_.hasPlugin(slotIndex_);

        // Slot background
        g.setColour(hasPlugin
            ? (hovered_ ? t.colors.surface.brighter(0.15f) : t.colors.surface)
            : (hovered_ ? t.colors.backgroundDark.brighter(0.08f) : t.colors.backgroundDark));
        g.fillRoundedRectangle(b, 3.f);

        g.setColour(hasPlugin ? t.colors.accent.withAlpha(0.4f) : t.colors.border.withAlpha(0.25f));
        g.drawRoundedRectangle(b, 3.f, 1.f);

        if (hasPlugin)
        {
            auto* slot = chain_.getSlot(slotIndex_);
            bool  bypassed = slot && slot->isBypassed();

            // Bypass indicator stripe
            if (bypassed)
            {
                g.setColour(juce::Colour(0xFFFF9500).withAlpha(0.7f));
                g.fillRect(b.getX(), b.getY(), 3.f, b.getHeight());
            }
            else
            {
                g.setColour(t.colors.accent.withAlpha(0.8f));
                g.fillRect(b.getX(), b.getY(), 3.f, b.getHeight());
            }

            // Plugin name
            juce::String name = slot ? slot->getName() : "Plugin";
            g.setColour(bypassed ? t.colors.textDisabled : t.colors.text);
            g.setFont(juce::Font(9.5f));
            auto nameArea = b.withTrimmedLeft(6.f);
            const bool sandboxed = slot != nullptr && slot->isSandboxed();
            if (sandboxed)
            {
                auto badge = nameArea.removeFromRight(30.f).reduced(1.f, 3.f);
                g.setColour(t.colors.accent.withAlpha(0.25f));
                g.fillRoundedRectangle(badge, 3.f);
                g.setColour(t.colors.accent);
                g.setFont(juce::Font(7.5f, juce::Font::bold));
                g.drawText("SBX", badge, juce::Justification::centred);
                g.setFont(juce::Font(9.5f));
            }
            g.drawText(name, nameArea, juce::Justification::centredLeft, true);
        }
        else
        {
            // Empty slot: "+" hint
            g.setColour(t.colors.textSecondary.withAlpha(0.4f));
            g.setFont(juce::Font(9.f));
            g.drawText("+ insert", b.withTrimmedLeft(4.f), juce::Justification::centredLeft);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { hovered_ = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered_ = false; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && chain_.hasPlugin(slotIndex_))
        {
            showContextMenu();
            return;
        }
        if (chain_.hasPlugin(slotIndex_))
        {
            if (auto* slot = chain_.getSlot(slotIndex_))
            {
                slot->openEditor();
            }
        }
        else
        {
            showPluginBrowser();
        }
    }

private:
    int               slotIndex_;
    PluginChainCore&  chain_;
    PluginScannerCore& scanner_;
    bool              hovered_ = false;

    void showPluginBrowser(
        PluginExecutionMode requestedMode = PluginExecutionMode::InProcess,
        bool preserveCurrentMode = false)
    {
        if (requestedMode == PluginExecutionMode::Sandboxed
            && ! PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Plugin Hosting Unavailable",
                PluginHostingProductPolicyCore::kSandboxDisabledMessage);
            return;
        }

        auto finalVisibleCount = scanner_.ensureBrowserVisibleDataReady("PluginSlotUI::showPluginBrowser");
        auto& known = scanner_.getKnownPlugins();
        const auto& list = known.getTypes();
        if (list.isEmpty())
        {
            PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "PluginSlotUI::showPluginBrowser noPluginsPopup"
                    " scannerId=0x" + juce::String::toHexString((juce::int64) reinterpret_cast<uintptr_t>(&scanner_))
                    + " cacheCount=" + juce::String(scanner_.getCache().getCount())
                    + " knownCount=" + juce::String(scanner_.getKnownPlugins().getNumTypes())
                    + " browserFeedCount=" + juce::String(finalVisibleCount)
                    + " panelItemCount=unavailable"
                    + " scanRunning=" + juce::String(scanner_.isScanning() ? 1 : 0));
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "No Plugins Found",
                "No VST3 plugins have been scanned.\nGo to Settings -> Scan Plugins to find installed plugins.");
            return;
        }

        juce::PopupMenu menu;
        // Group by manufacturer, show format tag (VST3 / VST / LV2)
        juce::StringArray manufacturers;
        for (auto& d : list)
            manufacturers.addIfNotAlreadyThere(d.manufacturerName);
        manufacturers.sort(true);

        int itemId = 1;
        std::vector<juce::PluginDescription> orderedDescs;

        for (auto& mfr : manufacturers)
        {
            juce::PopupMenu sub;
            for (auto& d : list)
            {
                if (d.manufacturerName == mfr)
                {
                    juce::String label = d.name + "  [" + d.pluginFormatName + "]";
                    sub.addItem(itemId++, label);
                    orderedDescs.push_back(d);
                }
            }
            menu.addSubMenu(mfr, sub);
        }

        auto* chainPtr   = &chain_;
         auto* scannerPtr = &scanner_;
         int capturedSlot = slotIndex_;

        juce::Component::SafePointer<PluginSlotUI> safeThis(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
             [safeThis, chainPtr, scannerPtr, capturedSlot, orderedDescs,
              requestedMode, preserveCurrentMode](int result)
             {
                if (!safeThis) return;
                if (result < 1 || result > (int)orderedDescs.size()) return;
                 const auto& desc = orderedDescs[(size_t)result - 1];
                 juce::String err;
                 PluginInsertOptions insertOptions;
                 insertOptions.executionMode = requestedMode;
                  if (preserveCurrentMode)
                      if (auto* current = chainPtr->getSlot(capturedSlot))
                          insertOptions.executionMode = current->getExecutionMode();
                   if (insertOptions.executionMode == PluginExecutionMode::Sandboxed
                       && ! PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                   {
                       juce::AlertWindow::showMessageBoxAsync(
                           juce::MessageBoxIconType::WarningIcon,
                           "Plugin Hosting Unavailable",
                           PluginHostingProductPolicyCore::kSandboxDisabledMessage);
                       return;
                   }
                  const bool loaded = chainPtr->loadPlugin(
                     capturedSlot, desc, insertOptions,
                     scannerPtr->getFormatManager(), err);
                if (!loaded)
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Plugin Load Error", err);
                }
                else if (safeThis)
                {
                    if (auto* slot = chainPtr->getSlot(capturedSlot))
                        slot->openEditor();
                }
                if (safeThis) safeThis->repaint();
            });
    }

    void showContextMenu()
    {
        auto* slot = chain_.getSlot(slotIndex_);
        if (!slot) return;

        juce::PopupMenu menu;
        bool bypassed = slot->isBypassed();

        juce::Component::SafePointer<PluginSlotUI> safeThis(this);
        {
            juce::PopupMenu::Item b("Bypass");
            b.isTicked = bypassed;
            b.action = [safeThis, bypassed]
            {
                if (safeThis)
                {
                    safeThis->chain_.setSlotBypassed(safeThis->slotIndex_, !bypassed);
                    safeThis->repaint();
                }
            };
            menu.addItem(b);
        }
        if (slot->hasEditor())
        {
            menu.addItem(10, "Open Editor");
        }
        menu.addSeparator();
         const bool keepModeAllowed =
             PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled
             || slot->getExecutionMode() != PluginExecutionMode::Sandboxed;
        menu.addItem(30, "Replace (Keep Mode)", keepModeAllowed);
        menu.addItem(31, "Replace In Process");
        if (PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
            menu.addItem(32, "Replace Sandboxed");
        menu.addSeparator();
        menu.addItem(20, "Remove Plugin");

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safeThis](int result)
             {
                 if (!safeThis) return;
                 if (result == 30)
                 {
                     safeThis->showPluginBrowser(PluginExecutionMode::InProcess, true);
                     return;
                 }
                 if (result == 31)
                 {
                     safeThis->showPluginBrowser(PluginExecutionMode::InProcess, false);
                     return;
                 }
                  if (result == 32
                      && PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
                  {
                      safeThis->showPluginBrowser(PluginExecutionMode::Sandboxed, false);
                      return;
                  }
                 // Re-read slot at callback time — avoids dangling pointer if
                // rebuildInlineSlots() was called while the menu was open.
                auto* slot = safeThis->chain_.getSlot(safeThis->slotIndex_);
                if (result == 10) { if (slot) slot->openEditor(); }
                else if (result == 20) { safeThis->chain_.removePlugin(safeThis->slotIndex_); if (safeThis) safeThis->repaint(); }
            });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginSlotUI)
};

} // namespace DAW

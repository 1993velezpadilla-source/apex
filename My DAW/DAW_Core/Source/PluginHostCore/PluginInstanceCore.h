#pragma once
#include <JuceHeader.h>
#include "PluginWindowScaleStore.h"
#include "../KeyBindingCore/KeyBindingManager.h"
#include "../AutomationCore/PluginAutomationGestureCore.h"
#include "../PluginSafetyCore/PluginSehGuardCore.h"
#include "../PluginSafetyCore/PluginQuarantineCore.h"
#include "../AutomationCore/PluginAutomationRecorderCore.h"
#include "../AutomationCore/LastTouchedPluginParameterCore.h"
#include "../UICore/CursorThemeCore.h"
#include "../AutomationCore/AutomationSnapshotCore.h"
#include "../AutomationCore/AutomationSmootherCore.h"
#include "../Automation/AutomationParameterRegistryCore.h"
#include "HostedPluginIsolationCore.h"
#include "../PluginSandboxCore/PluginSandboxPreparationCore.h"
#include <unordered_map>
#include <atomic>

namespace DAW {
// Forward declarations: implemented in PluginEditorWindowWin32Patch.cpp
void patchPluginEditorWindowExStyle(juce::Component* window,
                                    juce::Component* ownerComponent = nullptr);
// Subclasses the HWND to intercept SC_MINIMIZE and route it to the JUCE
// component's minimiseButtonPressed() instead of letting Windows minimize
// the window into the taskbar.
void installMinimizeInterceptor(juce::Component* window, std::function<void()> onMinimize);
}

namespace DAW {

// Forward declaration only: sandboxed members are defined in
// PluginInstanceCore.cpp so windows.h stays out of UI translation units.
class SandboxedPluginProxyCore;

/** Runtime placement of one chain slot. The default remains InProcess; only
    explicitly requested slots execute through the sandbox worker. The sandbox
    branch never creates or loads a parent-side AudioPluginInstance. */
enum class PluginExecutionMode : std::uint8_t
{
    InProcess = 0,
    Sandboxed = 1
};

struct PluginProcessDiagSnapshot
{
    int prepareCount = 0;
    int resetCount = 0;
    int lastProcessSamples = 0;
    bool bypassed = false;
    juce::String pluginName;
};

/**
 * PluginInstanceCore
 *
 * Nucleus: wraps a single loaded AudioPluginInstance.
 * Owns the plugin's lifecycle: prepare → process → suspend → reset.
 *
 * Thread safety:
 *   - prepare / reset → message thread
 *   - processBlock    → audio thread (after prepare)
 *
 * The plugin editor window is managed separately (PluginEditorCore — future).
 */
class PluginInstanceCore
    : private juce::AudioProcessorParameter::Listener
{
public:
    /** Floating plugin editor window.
     *  Temporarily kept on a minimal safe native-title-bar path until the
     *  shared plugin-open crash is isolated and fixed. */
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        static constexpr int kSafeReaddPhase = 5;
        static constexpr int kTitleBarH  = 32;
        static constexpr int kHostHeaderH = 34;
        static constexpr int kMinWindowW = 220;
        static constexpr int kMinWindowH = 120;
        static constexpr int kMaxSafeW   = 3200;
        static constexpr int kMaxSafeH   = 1800;

        static constexpr int kNumPresets = 8;
        static constexpr float kPresets[kNumPresets] = { 0.50f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };
        static inline const char* kPresetLabels[kNumPresets] = { "50%", "75%", "100%", "125%", "150%", "175%", "200%", "250%" };

        std::function<void()> onCloseRequested;
        std::function<void()> onMinimiseRequested;
        std::function<float()> onGetSlotMix;
        std::function<void(float)> onSetSlotMix;
        std::function<bool()> onGetSlotBypass;
        std::function<void(bool)> onSetSlotBypass;
        std::function<AutomationWriteMode()> onGetAutomationMode;
        std::function<void(AutomationWriteMode)> onSetAutomationMode;
        std::function<juce::String()> onGetLastTouchedLabel;

        PluginEditorWindow(const juce::String& name, juce::Component* ownerComponent = nullptr)
            : juce::DocumentWindow(name,
                                   juce::Colour(0xFF202020),
                                   juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton,
                                   true),
              ownerComponent_(ownerComponent)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setOpaque(true);
            setResizable(true, false);
            setResizeLimits(kMinWindowW, kMinWindowH, kMaxSafeW, kMaxSafeH);
            // APEX flow cursor everywhere — including inside plugin editor
            // windows (they are separate top-level surfaces).
            setMouseCursor(DAW::CursorThemeCore::getDefaultArrow());
            juce::Logger::writeToLog("[PluginEditorWindow] ctor success windowValid=1 nativeTitleBar=1 phase="
                + juce::String(kSafeReaddPhase)
                + " buttons=minimize,maximize,close,settings");
        }

        ~PluginEditorWindow() override
        {
            // ResizableWindow normally deletes owned content from its base
            // destructor, after this class's callback members have died.
            // Destroy the SafeEditorHost (and native plugin view) now, while
            // all PluginEditorWindow callback state is still valid.
            host_ = nullptr;
            clearContentComponent();

            // C6-editor-lifetime: reached only when the window was destroyed
            // WITHOUT the two-phase closeEditor() flow (takeNativeEditor).
            // C++ destroys derived-class members BEFORE the base class, so a
            // member-held editor would still die before the peer/HWND
            // teardown. Defer the editor's destruction to the message loop
            // instead: plugin window procs dispatched by WM_DESTROY always
            // run against intact plugin UI state.
            if (deferredEditor_)
            {
                auto editor = std::shared_ptr<juce::Component>(std::move(deferredEditor_));
                juce::MessageManager::callAsync([editor]() mutable { editor.reset(); });
            }
        }

        int getDesktopWindowStyleFlags() const override
        {
            // Do NOT include windowAppearsOnTaskbar — plugin windows must NOT
            // appear in the Windows native taskbar. They live in the DAW's own
            // bubble taskbar instead. WS_EX_TOOLWINDOW is explicitly SET (not
            // stripped) in patchPluginEditorWindowExStyle to enforce this.
            // Strip windowIsTemporary so JUCE does not route clicks-elsewhere
            // through the modal-dismiss path for this window.
            return (DocumentWindow::getDesktopWindowStyleFlags()
                    | juce::ComponentPeer::windowIsResizable)
                & ~juce::ComponentPeer::windowAppearsOnTaskbar
                & ~juce::ComponentPeer::windowIsTemporary;
        }

        void addToDesktop(int styleFlags, void* nativeParent) override
        {
            DocumentWindow::addToDesktop(styleFlags, nativeParent);
            // Patch extended styles and ownership synchronously.
            DAW::patchPluginEditorWindowExStyle(this, ownerComponent_.getComponent());
            // Intercept SC_MINIMIZE at the Win32 level so clicking the native
            // title-bar minimize button routes to our minimiseButtonPressed()
            // instead of doing SW_MINIMIZE (which would send the window to the
            // Windows taskbar rather than our floating bubble taskbar).
            juce::Component::SafePointer<PluginEditorWindow> safeWin(this);
            DAW::installMinimizeInterceptor(this, [safeWin]
            {
                if (safeWin != nullptr)
                    safeWin->minimiseButtonPressed();
            });
        }

        void setOwnerComponent(juce::Component* ownerComponent)
        {
            ownerComponent_ = ownerComponent;
            if (isOnDesktop())
                DAW::patchPluginEditorWindowExStyle(this, ownerComponent_.getComponent());
        }

        void closeButtonPressed() override
        {
            closeRequestedByUser_ = true;
            juce::Logger::writeToLog("[PluginEditorWindow] closeButtonPressed");
            if (onCloseRequested) onCloseRequested();
        }

        void minimiseButtonPressed() override
        {
            minimiseRequestedByUser_ = true;
            juce::Logger::writeToLog("[PluginEditorWindow] minimiseButtonPressed");
            setVisible(false);
            if (onMinimiseRequested) onMinimiseRequested();
        }

        void visibilityChanged() override
        {
            DocumentWindow::visibilityChanged();

            if (isVisible())
            {
                // Window is being shown (initial open or restored from minimize).
                // Clear intent flags so the re-show guard works for the full
                // lifetime of the window, not just until the first minimize.
                closeRequestedByUser_    = false;
                minimiseRequestedByUser_ = false;
                // Re-apply patch every time the window is shown: JUCE's
                // setLayeredWindow() reads GWL_EXSTYLE and writes it back,
                // which can silently restore WS_EX_TOOLWINDOW. Stripping it
                // here (and re-setting the owner) keeps the window stable.
                DAW::patchPluginEditorWindowExStyle(this, ownerComponent_.getComponent());
            }
            else if (!closeRequestedByUser_ && !minimiseRequestedByUser_)
            {
                // Something other than the user hid the window — force it back,
                // but only while the DAW is the foreground process so we never
                // fight an OS-level minimize or alt-tab away from the app.
                juce::Component::SafePointer<PluginEditorWindow> safeWin(this);
                juce::MessageManager::callAsync([safeWin]
                {
                    if (safeWin != nullptr && !safeWin->isVisible()
                        && !safeWin->closeRequestedByUser_
                        && !safeWin->minimiseRequestedByUser_
                        && juce::Process::isForegroundProcess())
                    {
                        safeWin->setVisible(true);
                        safeWin->toFront(false);
                    }
                });
            }
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (DAW::KeyBindingManager::getInstance().handleKeyPress(key))
                return true;

            return juce::DocumentWindow::keyPressed(key);
        }

        void maximiseButtonPressed() override
        {
            if (!maximized_)
            {
                restoreBounds_ = getBounds();
                auto displays = juce::Desktop::getInstance().getDisplays();
                auto* display = displays.getDisplayForPoint(getBounds().getCentre());
                if (display == nullptr)
                    display = displays.getPrimaryDisplay();

                if (display != nullptr)
                    setBounds(display->userArea);

                maximized_ = true;
                juce::Logger::writeToLog("[PluginEditorWindow] maximiseButtonPressed action=maximize restoreBounds="
                    + restoreBounds_.toString() + " newBounds=" + getBounds().toString());
            }
            else
            {
                if (!restoreBounds_.isEmpty())
                    setBounds(restoreBounds_);

                maximized_ = false;
                juce::Logger::writeToLog("[PluginEditorWindow] maximiseButtonPressed action=restore restoredBounds="
                    + getBounds().toString());
            }

            clampToScreen();
        }

        void setPluginKey(const juce::String& key) { pluginKey_ = key; }
        const juce::String& getPluginKey() const { return pluginKey_; }

        void storeNativeEditorSize(int w, int h)
        {
            nativeEditorW_ = w;
            nativeEditorH_ = h;
            if (host_ != nullptr)
                host_->setNativeEditorSize(nativeEditorW_, nativeEditorH_);
            DBG("[PluginEditorWindow] Native editor size stored: " + juce::String(w) + "x" + juce::String(h));
        }

        void setEditorIsResizable(bool r)
        {
            editorIsResizable_ = r;
            setResizable(r, false);
            setResizeLimits(kMinWindowW, kMinWindowH, kMaxSafeW, kMaxSafeH);
            if (host_ != nullptr)
                host_->setEditorResizable(editorIsResizable_);
        }

        void setEditorContent(juce::Component* editor)
        {
            auto host = std::make_unique<SafeEditorHost>(*this, editor);
            host_ = host.get();
            setContentOwned(host.release(), true);
            host_->setEditorResizable(editorIsResizable_);
            host_->setNativeEditorSize(nativeEditorW_, nativeEditorH_);
            juce::Logger::writeToLog("[PluginEditorWindow] settings button wired yes");
        }

        /** C6-editor-lifetime: SafeEditorHost hands the native plugin editor
            here when the host is being destroyed without the two-phase
            closeEditor() flow. The editor is kept until after this shell's
            HWND teardown and then destroyed on the message loop. */
        void adoptEditorForDeferredDestruction(std::unique_ptr<juce::Component> editor)
        {
            jassert(deferredEditor_ == nullptr);
            if (editor && deferredEditor_ == nullptr)
                deferredEditor_ = std::move(editor);
        }

        /** C6-editor-lifetime two-phase teardown, phase 1: detach the native
            plugin editor from the content host so the shell window can be
            destroyed while the plugin UI state remains fully alive. The
            caller destroys the returned editor AFTER this window is gone. */
        std::unique_ptr<juce::Component> takeNativeEditor()
        {
            return host_ != nullptr ? host_->takeEditor() : nullptr;
        }

        void applyScalePreset(float scale)
        {
            if (nativeEditorW_ <= 0 || nativeEditorH_ <= 0)
                return;

            int newW = juce::roundToInt(nativeEditorW_ * scale);
            int newH = juce::roundToInt(nativeEditorH_ * scale) + (host_ != nullptr ? kHostHeaderH : 0);
            clampSizeToScreen(newW, newH);
            setSize(newW, newH);
            currentScale_ = scale;
            clampToScreen();
            if (host_ != nullptr)
                host_->updateScaleLabel(currentScale_);
            DBG("[PluginEditorWindow] Scale applied: " + juce::String((int)(scale * 100))
                + "% -> " + juce::String(newW) + "x" + juce::String(newH));
        }

        float getCurrentScale() const { return currentScale_; }

        void showSettingsMenu()
        {
            juce::Logger::writeToLog("[PluginEditorWindow] settingsButtonClicked pluginKey=\"" + pluginKey_ + "\"");

            auto& store = PluginWindowScaleStore::getInstance();
            bool hasOverride = pluginKey_.isNotEmpty() && store.hasPluginScale(pluginKey_);

            juce::PopupMenu menu;

            // ── Scale Mode submenu ──
            juce::PopupMenu modeMenu;
            modeMenu.addItem(10, "Universal (all plugins)", true, !hasOverride);
            modeMenu.addItem(11, "This Plugin Only", pluginKey_.isNotEmpty(), hasOverride);
            if (hasOverride)
            {
                modeMenu.addSeparator();
                modeMenu.addItem(12, "Clear Plugin Override");
            }
            menu.addSubMenu("Scale Mode", modeMenu);

            // ── Window Scale submenu ──
            juce::PopupMenu scaleMenu;
            for (int i = 0; i < kNumPresets; ++i)
            {
                bool isCurrent = std::abs(currentScale_ - kPresets[i]) < 0.01f;
                scaleMenu.addItem(100 + i, kPresetLabels[i], true, isCurrent);
            }
            scaleMenu.addSeparator();
            scaleMenu.addItem(200, "Reset to Default (100%)");
            scaleMenu.addItem(201, "Fit to Screen");
            menu.addSubMenu("Window Scale", scaleMenu);

            juce::Logger::writeToLog("[PluginEditorWindow] settingsMenuOpened yes hasOverride="
                + juce::String(hasOverride ? 1 : 0)
                + " currentScale=" + juce::String((int)(currentScale_ * 100)) + "%");

            juce::PopupMenu::Options opts;
            opts = opts.withTargetComponent(host_ != nullptr ? host_->getSettingsButton() : this);
            juce::Component::SafePointer<PluginEditorWindow> safeWin(this);
            menu.showMenuAsync(opts, [safeWin](int result)
            {
                if (!safeWin) return;
                juce::Logger::writeToLog("[PluginEditorWindow] settingsMenuResult=" + juce::String(result));
                auto& store = PluginWindowScaleStore::getInstance();

                if (result == 10)
                {
                    // Switch to universal: clear per-plugin override
                    if (safeWin->pluginKey_.isNotEmpty()) store.clearPluginScale(safeWin->pluginKey_);
                    safeWin->applyScalePreset(store.getUniversalScale());
                    juce::Logger::writeToLog("[PluginEditorWindow] scaleModeSelected=Universal applied=" + juce::String((int)(store.getUniversalScale() * 100)) + "%");
                }
                else if (result == 11)
                {
                    // Switch to per-plugin: save current scale as plugin override
                    if (safeWin->pluginKey_.isNotEmpty()) store.setPluginScale(safeWin->pluginKey_, safeWin->currentScale_);
                    juce::Logger::writeToLog("[PluginEditorWindow] scaleModeSelected=PerPlugin saved=" + juce::String((int)(safeWin->currentScale_ * 100)) + "%");
                }
                else if (result == 12)
                {
                    // Clear per-plugin override, revert to universal
                    if (safeWin->pluginKey_.isNotEmpty()) store.clearPluginScale(safeWin->pluginKey_);
                    safeWin->applyScalePreset(store.getUniversalScale());
                    juce::Logger::writeToLog("[PluginEditorWindow] scaleOverrideCleared revertedTo=" + juce::String((int)(store.getUniversalScale() * 100)) + "%");
                }
                else if (result >= 100 && result < 100 + kNumPresets)
                {
                    float scale = kPresets[result - 100];
                    safeWin->applyScalePreset(scale);
                    // Persist to whichever mode is active
                    if (safeWin->pluginKey_.isNotEmpty() && store.hasPluginScale(safeWin->pluginKey_))
                        store.setPluginScale(safeWin->pluginKey_, scale);
                    else
                        store.setUniversalScale(scale);
                    juce::Logger::writeToLog("[PluginEditorWindow] scalePresetSelected=" + juce::String((int)(scale * 100)) + "%");
                }
                else if (result == 200)
                {
                    safeWin->applyScalePreset(1.0f);
                    if (safeWin->pluginKey_.isNotEmpty() && store.hasPluginScale(safeWin->pluginKey_))
                        store.setPluginScale(safeWin->pluginKey_, 1.0f);
                    else
                        store.setUniversalScale(1.0f);
                    juce::Logger::writeToLog("[PluginEditorWindow] scalePresetSelected=ResetDefault 100%");
                }
                else if (result == 201)
                {
                    safeWin->fitToScreen();
                    juce::Logger::writeToLog("[PluginEditorWindow] scalePresetSelected=FitToScreen bounds=" + safeWin->getBounds().toString());
                }
            });
        }

        void fitToScreen()
        {
            auto displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForPoint(getBounds().getCentre());
            if (display == nullptr)
                return;

            auto area = display->userArea.reduced(40);
            setBounds(getBounds().withSizeKeepingCentre(juce::jmin(getWidth(), area.getWidth()),
                                                        juce::jmin(getHeight(), area.getHeight())));
            clampToScreen();
            DBG("[PluginEditorWindow] fitToScreen: bounds=" + getBounds().toString());
        }

        void clampToScreen()
        {
            auto displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getDisplayForPoint(getBounds().getCentre());
            if (display == nullptr)
                display = displays.getPrimaryDisplay();
            if (display == nullptr)
                return;

            auto area = display->userArea;
            auto b = getBounds();
            b.setWidth(juce::jlimit(kMinWindowW, area.getWidth() - 20, b.getWidth()));
            b.setHeight(juce::jlimit(kMinWindowH, area.getHeight() - 20, b.getHeight()));
            b.setX(juce::jlimit(area.getX(), area.getRight() - 80, b.getX()));
            b.setY(juce::jlimit(area.getY(), area.getBottom() - kTitleBarH, b.getY()));
            setBounds(b);
            DBG("[PluginEditorWindow] clampToScreen: bounds=" + b.toString() + " screen=" + area.toString());
        }

        void resized() override
        {
            DocumentWindow::resized();

            if (host_ != nullptr)
            {
                host_->setEditorResizable(editorIsResizable_);
                host_->setNativeEditorSize(nativeEditorW_, nativeEditorH_);
                return;
            }

            auto* content = getContentComponent();
            if (content == nullptr)
                return;

            auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(content);
            if (editor == nullptr && content->getNumChildComponents() > 0)
                editor = dynamic_cast<juce::AudioProcessorEditor*>(content->getChildComponent(0));

            if (editor != nullptr && !editorIsResizable_)
            {
                auto area = content->getLocalBounds();
                auto editorBounds = juce::Rectangle<int>(nativeEditorW_, nativeEditorH_);
                if (editorBounds.isEmpty())
                    editorBounds = area;
                editorBounds.setPosition(area.getCentreX() - editorBounds.getWidth() / 2,
                                         area.getCentreY() - editorBounds.getHeight() / 2);
                editor->setBounds(editorBounds);
            }
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xFF1A1A1C));
            // Subtle outer border
            g.setColour(juce::Colour(0x60000000));
            g.drawRect(getLocalBounds(), 1);
        }

        void clampSizeToScreen(int& w, int& h) const
        {
            auto displays = juce::Desktop::getInstance().getDisplays();
            auto* display = displays.getPrimaryDisplay();
            if (display == nullptr)
                return;
            auto area = display->userArea;
            w = juce::jlimit(kMinWindowW, juce::jmin(kMaxSafeW, area.getWidth() - 40), w);
            h = juce::jlimit(kMinWindowH, juce::jmin(kMaxSafeH, area.getHeight() - 40), h);
        }

        // ── Premium glass-style gear button ─────────────────────────────
        class GearButton : public juce::Component
        {
        public:
            std::function<void()> onClick;

            GearButton()
            {
                setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor));
                setName("GearButton");
            }

            void paint(juce::Graphics& g) override
            {
                auto bounds = getLocalBounds().toFloat().reduced(2.0f);
                bool hov = isMouseOver();

                // Pill background — glass tint
                g.setColour(juce::Colour(hov ? 0x28FFFFFF : 0x14FFFFFF));
                g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);

                // Subtle border
                g.setColour(juce::Colour(hov ? 0x30FFFFFF : 0x18FFFFFF));
                g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, 0.75f);

                auto c = bounds.getCentre();
                float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.28f;

                // Gear icon
                g.setColour(juce::Colour(hov ? 0xF0FFFFFF : 0xAAFFFFFF));
                g.drawEllipse(c.x - r * 0.55f, c.y - r * 0.55f, r * 1.1f, r * 1.1f, 1.2f);
                for (int i = 0; i < 8; ++i)
                {
                    float angle = (float)i * juce::MathConstants<float>::twoPi / 8.0f;
                    float x1 = c.x + std::cos(angle) * r * 0.5f;
                    float y1 = c.y + std::sin(angle) * r * 0.5f;
                    float x2 = c.x + std::cos(angle) * r * 1.05f;
                    float y2 = c.y + std::sin(angle) * r * 1.05f;
                    g.drawLine(x1, y1, x2, y2, 1.4f);
                }

                // Scale label right of gear
                auto textArea = bounds.withLeft(c.x + r * 1.3f).reduced(0, 1);
                if (textArea.getWidth() > 16)
                {
                    g.setFont(juce::Font(9.5f, juce::Font::bold));
                    g.setColour(juce::Colour(hov ? 0xCCFFFFFF : 0x80FFFFFF));
                    g.drawText(scaleLabel_, textArea, juce::Justification::centredLeft, false);
                }
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                if (getLocalBounds().toFloat().contains(e.position) && onClick)
                    onClick();
            }

            void mouseEnter(const juce::MouseEvent&) override { repaint(); }
            void mouseExit(const juce::MouseEvent&) override  { repaint(); }

            void setScaleLabel(const juce::String& label) { scaleLabel_ = label; repaint(); }

        private:
            juce::String scaleLabel_ { "100%" };
        };

        // ── Premium glass host header ────────────────────────────────────
        class SafeEditorHost : public juce::Component
            , private juce::Timer
        {
        public:
            SafeEditorHost(PluginEditorWindow& owner, juce::Component* editor)
                : owner_(owner), editor_(editor)
            {
                gearButton_.onClick = [this] { owner_.showSettingsMenu(); };
                    addAndMakeVisible(gearButton_);

                    minButton_.onClick = [this] { owner_.minimiseButtonPressed(); };
                    addAndMakeVisible(minButton_);

                    fsButton_.onClick = [this] { owner_.maximiseButtonPressed(); };
                    addAndMakeVisible(fsButton_);

                    closeButton_.onClick = [this] { owner_.closeButtonPressed(); };
                    addAndMakeVisible(closeButton_);

                    mixControl_.onValueChanged = [this](float v)
                    {
                        if (owner_.onSetSlotMix)
                            owner_.onSetSlotMix(v);
                    };
                    addAndMakeVisible(mixControl_);

                    bypassButton_.onClick = [this]
                    {
                        const bool current = owner_.onGetSlotBypass ? owner_.onGetSlotBypass() : false;
                        if (owner_.onSetSlotBypass)
                            owner_.onSetSlotBypass(!current);
                        bypassButton_.setToggleState(!current, juce::dontSendNotification);
                    };
                    bypassButton_.setText("Bypass");
                    addAndMakeVisible(bypassButton_);

                    setupModeButton(readButton_, AutomationWriteMode::Read, "Read");
                    setupModeButton(writeButton_, AutomationWriteMode::Write, "Write");
                    setupModeButton(touchButton_, AutomationWriteMode::Touch, "Touch");
                    setupModeButton(latchButton_, AutomationWriteMode::Latch, "Latch");

                    addAndMakeVisible(lastTouchedLabel_);
                    lastTouchedLabel_.setJustificationType(juce::Justification::centredLeft);
                    lastTouchedLabel_.setColour(juce::Label::textColourId, juce::Colour(0xCCFFFFFF));
                    lastTouchedLabel_.setFont(juce::Font(10.0f, juce::Font::plain));

                    addAndMakeVisible(editor_.get());
                    startTimerHz(60);   // 60 Hz — smooth host header UI polling
            }

            ~SafeEditorHost() override
            {
                // Timer's base destructor runs only after members are
                // destroyed. Stop it explicitly before native editor teardown
                // so nested message dispatch from IPlugView::removed() cannot
                // re-enter timerCallback() on a destructing host.
                stopTimer();

                // C6-editor-lifetime: do NOT destroy the native plugin editor
                // here. JUCE destroys the content component (this host) BEFORE
                // the window's peer, so destroying the editor now would free
                // the plugin's UI/OpenGL state while the plugin's subclassed
                // window proc still runs during the later WM_DESTROY dispatch.
                // Live crash evidence (undo-driven chain teardown, FabFilter
                // Pro-C 2): call [r10+118h] inside the plugin's window proc
                // during NtUserDestroyWindow against already-freed state.
                // Instead the editor is detached from the tree and handed to
                // the shell, whose members are destroyed only AFTER the base
                // DocumentWindow/peer (HWND) teardown completes.
                if (editor_)
                {
                    removeChildComponent(editor_.get());
                    owner_.adoptEditorForDeferredDestruction(std::move(editor_));
                }
            }

            /** C6-editor-lifetime two-phase teardown, phase 1: detach the
                native plugin editor from the host tree and return ownership
                so the shell window (HWND) can be destroyed while the plugin
                UI state is still fully alive. The caller then destroys the
                editor AFTER the window teardown completes. */
            std::unique_ptr<juce::Component> takeEditor()
            {
                stopTimer();
                if (editor_)
                    removeChildComponent(editor_.get());
                return std::move(editor_);
            }

            void mouseDown(const juce::MouseEvent& e) override
            {
                if (e.y < kHostHeaderH)
                    dragStart_ = owner_.getPosition() - e.getScreenPosition();
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                if (e.mouseWasDraggedSinceMouseDown() && dragStart_.has_value())
                    owner_.setTopLeftPosition(e.getScreenPosition() + *dragStart_);
            }

            void mouseUp(const juce::MouseEvent&) override
            {
                dragStart_.reset();
            }

            void mouseDoubleClick(const juce::MouseEvent& e) override
            {
                if (e.y < kHostHeaderH)
                    owner_.maximiseButtonPressed();
            }

            void setNativeEditorSize(int w, int h)
            {
                nativeEditorW_ = w;
                nativeEditorH_ = h;
                resized();
            }

            void setEditorResizable(bool isResizable)
            {
                editorResizable_ = isResizable;
                resized();
            }

            juce::Component* getSettingsButton() { return &gearButton_; }

            void updateScaleLabel(float scale)
            {
                gearButton_.setScaleLabel(juce::String((int)(scale * 100)) + "%");
            }

            void paint(juce::Graphics& g) override
            {
                auto full = getLocalBounds().toFloat();
                auto header = full.removeFromTop((float)kHostHeaderH);

                // ── Glass background gradient ──
                juce::ColourGradient glass(
                    juce::Colour(0xFF2C2C2E), 0, header.getY(),
                    juce::Colour(0xFF1C1C1E), 0, header.getBottom(), false);
                g.setGradientFill(glass);
                g.fillRect(header);

                // ── Top highlight — semi-transparent white edge ──
                g.setColour(juce::Colour(0x12FFFFFF));
                g.fillRect(header.getX(), header.getY(), header.getWidth(), 1.0f);

                // ── Subtle inner glow line ──
                g.setColour(juce::Colour(0x09FFFFFF));
                g.fillRect(header.getX(), header.getY() + 1.0f, header.getWidth(), 1.0f);

                // ── Bottom separator — crisp dark edge ──
                g.setColour(juce::Colour(0x50000000));
                g.fillRect(header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);

                // ── Plugin name — left side, light weight ──
                auto textArea = header.reduced(14.0f, 0);
                textArea.removeFromRight(194.0f); // reserve gear + minimize + fullscreen + close area
                g.setFont(juce::Font(10.5f, juce::Font::plain));

                // Shadow pass
                g.setColour(juce::Colour(0x30000000));
                g.drawText(owner_.getName(), textArea.translated(0, 0.75f),
                           juce::Justification::centredLeft, true);

                // Main text — soft white
                g.setColour(juce::Colour(0x99FFFFFF));
                g.drawText(owner_.getName(), textArea,
                           juce::Justification::centredLeft, true);

                // ── Content area fill ──
                g.setColour(juce::Colour(0xFF1A1A1C));
                g.fillRect(full);
            }

            void timerCallback() override
            {
                const float mix = owner_.onGetSlotMix ? owner_.onGetSlotMix() : 1.0f;
                mixControl_.setValue(mix);
                bypassButton_.setToggleState(owner_.onGetSlotBypass ? owner_.onGetSlotBypass() : false, juce::dontSendNotification);
                updateModeButtons(owner_.onGetAutomationMode ? owner_.onGetAutomationMode() : AutomationWriteMode::Read);
                lastTouchedLabel_.setText(owner_.onGetLastTouchedLabel ? owner_.onGetLastTouchedLabel() : "Last touched: --", juce::dontSendNotification);
            }

            void resized() override
            {
                auto area = getLocalBounds();
                auto header = area.removeFromTop(kHostHeaderH);

                auto editorArea = area;
                if (editor_->isOnDesktop())
                {
                    if (auto* peer = editor_->getPeer())
                    {
                        auto screenBounds = localAreaToGlobal(editorArea);
                        peer->setBounds(screenBounds, false);
                    }
                }

                // Close button — rightmost
                auto closeArea = header.removeFromRight(28).reduced(5, 5);
                closeButton_.setBounds(closeArea);

                // Fullscreen button — left of close
                auto fsArea = header.removeFromRight(34).reduced(6, 5);
                fsButton_.setBounds(fsArea);

                // Minimize button — left of fullscreen
                auto minArea = header.removeFromRight(34).reduced(6, 5);
                minButton_.setBounds(minArea);

                // Gear button — left of minimize
                auto gearArea = header.removeFromRight(90).reduced(6, 5);
                gearButton_.setBounds(gearArea);

                auto automationArea = header.removeFromRight(396).reduced(4, 5);
                mixControl_.setBounds(automationArea.removeFromLeft(66));
                bypassButton_.setBounds(automationArea.removeFromLeft(58).reduced(3, 0));
                readButton_.setBounds(automationArea.removeFromLeft(42).reduced(2, 0));
                writeButton_.setBounds(automationArea.removeFromLeft(48).reduced(2, 0));
                touchButton_.setBounds(automationArea.removeFromLeft(50).reduced(2, 0));
                latchButton_.setBounds(automationArea.removeFromLeft(50).reduced(2, 0));
                lastTouchedLabel_.setBounds(automationArea.reduced(4, 0));

                if (editorResizable_)
                {
                    editor_->setBounds(editorArea);
                    return;
                }

                auto editorBounds = juce::Rectangle<int>(nativeEditorW_, nativeEditorH_);
                if (editorBounds.isEmpty())
                    editorBounds = editorArea;
                editorBounds.setPosition(editorArea.getCentreX() - editorBounds.getWidth() / 2,
                                         editorArea.getCentreY() - editorBounds.getHeight() / 2);
                editor_->setBounds(editorBounds);
            }

        private:
            class HeaderPillButton : public juce::Component
            {
            public:
                std::function<void()> onClick;

                HeaderPillButton() { setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor)); }
                void setText(juce::String text) { text_ = std::move(text); repaint(); }
                void setToggleState(bool active, juce::NotificationType) { active_ = active; repaint(); }
                bool getToggleState() const noexcept { return active_; }

                void paint(juce::Graphics& g) override
                {
                    auto b = getLocalBounds().toFloat();
                    const bool hov = isMouseOver();
                    g.setColour(active_ ? juce::Colour(0xFF8F4DFF) : juce::Colour(hov ? 0x28FFFFFF : 0x14FFFFFF));
                    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
                    g.setColour(active_ ? juce::Colour(0x99FFFFFF) : juce::Colour(0x28FFFFFF));
                    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 0.8f);
                    g.setFont(juce::Font(9.0f, juce::Font::bold));
                    g.setColour(juce::Colours::white.withAlpha(active_ ? 0.98f : 0.72f));
                    g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
                }

                void mouseUp(const juce::MouseEvent& e) override
                {
                    if (getLocalBounds().toFloat().contains(e.position) && onClick)
                        onClick();
                }
                void mouseEnter(const juce::MouseEvent&) override { repaint(); }
                void mouseExit(const juce::MouseEvent&) override { repaint(); }

            private:
                juce::String text_;
                bool active_ = false;
            };

            class MixControl : public juce::Component
            {
            public:
                std::function<void(float)> onValueChanged;

                MixControl()
                {
                    setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor));
                }

                void setValue(float value)
                {
                    value_ = juce::jlimit(0.0f, 1.0f, value);
                    repaint();
                }

                void paint(juce::Graphics& g) override
                {
                    auto b = getLocalBounds().toFloat();
                    g.setColour(juce::Colour(0x18FFFFFF));
                    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
                    auto fill = b;
                    fill.setWidth(fill.getWidth() * value_);
                    g.setColour(juce::Colour(0xFF7B3CFF));
                    g.fillRoundedRectangle(fill, b.getHeight() * 0.5f);
                    g.setColour(juce::Colour(0x33FFFFFF));
                    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 0.8f);
                    g.setFont(juce::Font(9.0f, juce::Font::bold));
                    g.setColour(juce::Colours::white.withAlpha(0.9f));
                    g.drawText("Mix " + juce::String(juce::roundToInt(value_ * 100.0f)) + "%", getLocalBounds(), juce::Justification::centred, false);
                }

                void mouseDown(const juce::MouseEvent&) override { dragStartValue_ = value_; }
                void mouseDrag(const juce::MouseEvent& e) override
                {
                    setValue(dragStartValue_ - (float)e.getDistanceFromDragStartY() * 0.006f);
                    if (onValueChanged)
                        onValueChanged(value_);
                }

            private:
                float value_ = 1.0f;
                float dragStartValue_ = 1.0f;
            };

            void setupModeButton(HeaderPillButton& button, AutomationWriteMode mode, const char* text)
            {
                button.setText(text);
                button.onClick = [this, mode]
                {
                    if (owner_.onSetAutomationMode)
                        owner_.onSetAutomationMode(mode);
                    updateModeButtons(mode);
                };
                addAndMakeVisible(button);
            }

            void updateModeButtons(AutomationWriteMode mode)
            {
                readButton_.setToggleState(mode == AutomationWriteMode::Read, juce::dontSendNotification);
                writeButton_.setToggleState(mode == AutomationWriteMode::Write, juce::dontSendNotification);
                touchButton_.setToggleState(mode == AutomationWriteMode::Touch, juce::dontSendNotification);
                latchButton_.setToggleState(mode == AutomationWriteMode::Latch, juce::dontSendNotification);
            }

            // ── Minimize pill button ─────────────────────────────────────
            class MinimizeButton : public juce::Component
            {
            public:
                std::function<void()> onClick;

                MinimizeButton() { setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor)); }

                void paint(juce::Graphics& g) override
                {
                    auto b = getLocalBounds().toFloat();
                    bool hov = isMouseOver();
                    g.setColour(juce::Colour(hov ? 0x28FFFFFF : 0x14FFFFFF));
                    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
                    g.setColour(juce::Colour(hov ? 0x30FFFFFF : 0x18FFFFFF));
                    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 0.75f);
                    // Underscore / minus icon
                    float cx = b.getCentreX(), cy = b.getCentreY() + 2.f;
                    float hw = b.getWidth() * 0.28f;
                    g.setColour(juce::Colour(hov ? 0xF0FFFFFF : 0xAAFFFFFF));
                    g.fillRect(cx - hw, cy, hw * 2.f, 1.5f);
                }

                void mouseUp(const juce::MouseEvent& e) override
                { if (getLocalBounds().toFloat().contains(e.position) && onClick) onClick(); }
                void mouseEnter(const juce::MouseEvent&) override { repaint(); }
                void mouseExit (const juce::MouseEvent&) override { repaint(); }
            };

            // ── Close pill button ────────────────────────────────────
            class CloseButton : public juce::Component
            {
            public:
                std::function<void()> onClick;

                CloseButton() { setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor)); }

                void paint(juce::Graphics& g) override
                {
                    auto b = getLocalBounds().toFloat();
                    bool hov = isMouseOver();
                    g.setColour(juce::Colour(hov ? 0x60FF3B30 : 0x28FF3B30));
                    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
                    g.setColour(juce::Colour(hov ? 0x50FF3B30 : 0x18FF3B30));
                    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 0.75f);
                    float cx = b.getCentreX(), cy = b.getCentreY();
                    float s = juce::jmin(b.getWidth(), b.getHeight()) * 0.22f;
                    g.setColour(juce::Colour(hov ? 0xF0FFFFFF : 0xAAFFFFFF));
                    g.drawLine(cx - s, cy - s, cx + s, cy + s, 1.5f);
                    g.drawLine(cx + s, cy - s, cx - s, cy + s, 1.5f);
                }

                void mouseUp(const juce::MouseEvent& e) override
                { if (getLocalBounds().toFloat().contains(e.position) && onClick) onClick(); }
                void mouseEnter(const juce::MouseEvent&) override { repaint(); }
                void mouseExit (const juce::MouseEvent&) override { repaint(); }
            };

            // ── Fullscreen pill button ───────────────────────────────
            class FullscreenButton : public juce::Component
            {
            public:
                std::function<void()> onClick;

                FullscreenButton() { setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::PointingHandCursor)); }

                void paint(juce::Graphics& g) override
                {
                    auto b = getLocalBounds().toFloat();
                    bool hov = isMouseOver();
                    g.setColour(juce::Colour(hov ? 0x28FFFFFF : 0x14FFFFFF));
                    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
                    g.setColour(juce::Colour(hov ? 0x30FFFFFF : 0x18FFFFFF));
                    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 0.75f);
                    // Four-corner arrows icon
                    float cx = b.getCentreX(), cy = b.getCentreY();
                    float s = juce::jmin(b.getWidth(), b.getHeight()) * 0.22f;
                    g.setColour(juce::Colour(hov ? 0xF0FFFFFF : 0xAAFFFFFF));
                    juce::Path p;
                    // top-left arrow
                    p.startNewSubPath(cx - s * 1.7f, cy - s * 0.6f);
                    p.lineTo(cx - s * 1.7f, cy - s * 1.7f);
                    p.lineTo(cx - s * 0.6f, cy - s * 1.7f);
                    // top-right arrow
                    p.startNewSubPath(cx + s * 0.6f, cy - s * 1.7f);
                    p.lineTo(cx + s * 1.7f, cy - s * 1.7f);
                    p.lineTo(cx + s * 1.7f, cy - s * 0.6f);
                    // bottom-right arrow
                    p.startNewSubPath(cx + s * 1.7f, cy + s * 0.6f);
                    p.lineTo(cx + s * 1.7f, cy + s * 1.7f);
                    p.lineTo(cx + s * 0.6f, cy + s * 1.7f);
                    // bottom-left arrow
                    p.startNewSubPath(cx - s * 0.6f, cy + s * 1.7f);
                    p.lineTo(cx - s * 1.7f, cy + s * 1.7f);
                    p.lineTo(cx - s * 1.7f, cy + s * 0.6f);
                    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered,
                                                          juce::PathStrokeType::square));
                }

                void mouseUp(const juce::MouseEvent& e) override
                { if (getLocalBounds().toFloat().contains(e.position) && onClick) onClick(); }
                void mouseEnter(const juce::MouseEvent&) override { repaint(); }
                void mouseExit (const juce::MouseEvent&) override { repaint(); }
            };

            PluginEditorWindow& owner_;
            GearButton      gearButton_;
            MinimizeButton  minButton_;
            FullscreenButton fsButton_;
            CloseButton     closeButton_;
            MixControl      mixControl_;
            HeaderPillButton bypassButton_;
            HeaderPillButton readButton_;
            HeaderPillButton writeButton_;
            HeaderPillButton touchButton_;
            HeaderPillButton latchButton_;
            juce::Label     lastTouchedLabel_;
            std::unique_ptr<juce::Component> editor_;
            int nativeEditorW_ = 0;
            int nativeEditorH_ = 0;
            bool editorResizable_ = false;
            std::optional<juce::Point<int>> dragStart_;
        };

    private:
        juce::Component::SafePointer<juce::Component> ownerComponent_;
        int nativeEditorW_ = 0;
        int nativeEditorH_ = 0;
        bool editorIsResizable_ = false;
        bool maximized_ = false;
        bool closeRequestedByUser_ = false;
        bool minimiseRequestedByUser_ = false;
        float currentScale_ = 1.0f;
        juce::Rectangle<int> restoreBounds_;
        juce::String pluginKey_;
        SafeEditorHost* host_ = nullptr;

        // C6-editor-lifetime: fallback holder for the native plugin editor
        // when the shell is destroyed WITHOUT the two-phase closeEditor()
        // flow. C++ destroys derived members BEFORE base classes, so the
        // destructor defers this editor to the message loop instead of
        // destroying it as a member (which would still precede the peer/HWND
        // teardown and let WM_DESTROY run plugin window procs against freed
        // UI state).
        std::unique_ptr<juce::Component> deferredEditor_;
    };

    explicit PluginInstanceCore(std::unique_ptr<juce::AudioPluginInstance> plugin);

    /** Sandboxed runtime holder. Owns exactly one parent-side proxy; the real
        plugin module and instance live exclusively in the worker process.
        Defined out-of-line (PluginInstanceCore.cpp). */
    explicit PluginInstanceCore(std::unique_ptr<SandboxedPluginProxyCore> proxy);

    ~PluginInstanceCore();

    // ── Identity ─────────────────────────────────────────────────────────

    juce::String getName() const;
    juce::String getVendor() const;
    PluginExecutionMode getExecutionMode() const noexcept
    {
        return sandboxProxy_ != nullptr
            ? PluginExecutionMode::Sandboxed
            : PluginExecutionMode::InProcess;
    }
    bool isSandboxed() const noexcept { return sandboxProxy_ != nullptr; }
    const SandboxedPluginProxyCore* getSandboxProxy() const noexcept { return sandboxProxy_.get(); }
    SandboxedPluginProxyCore* getSandboxProxy() noexcept { return sandboxProxy_.get(); }
    bool         isBypassed()     const { return bypassed_.load(std::memory_order_relaxed); }
    int getPrepareCount() const noexcept { return prepareCount_.load(std::memory_order_relaxed); }
    int getResetCount() const noexcept { return resetCount_.load(std::memory_order_relaxed); }
    int getLastProcessSamples() const noexcept { return lastProcessSamples_.load(std::memory_order_relaxed); }
    PluginProcessDiagSnapshot getDiagSnapshot() const
    {
        PluginProcessDiagSnapshot snapshot;
        snapshot.prepareCount = getPrepareCount();
        snapshot.resetCount = getResetCount();
        snapshot.lastProcessSamples = getLastProcessSamples();
        snapshot.bypassed = isBypassed();
        snapshot.pluginName = getName();
        return snapshot;
    }
    void setBypassed(bool b);
    void         setPlayHead(juce::AudioPlayHead* playHead) { if (plugin_) plugin_->setPlayHead(playHead); }
    const juce::String& getPluginInstanceId() const noexcept { return pluginInstanceId_; }
    void setPluginInstanceId(const juce::String& pluginInstanceId) noexcept
    {
        if (pluginInstanceId.isNotEmpty())
            pluginInstanceId_ = pluginInstanceId;
    }
    void setSlotMixNormalized(float mix) noexcept { slotMixNormalized_.store(juce::jlimit(0.0f, 1.0f, mix), std::memory_order_relaxed); }
    float getSlotMixNormalized() const noexcept { return slotMixNormalized_.load(std::memory_order_relaxed); }
    void setSlotMixBypass(bool bypass) noexcept { slotMixBypass_.store(bypass, std::memory_order_relaxed); }
    bool getSlotMixBypass() const noexcept { return slotMixBypass_.load(std::memory_order_relaxed); }
    float getLastSlotMixAutomationValue() const noexcept
    {
        return lastSlotMixAutomationValue_.load(std::memory_order_relaxed);
    }
    void setLastSlotMixAutomationValue(float value) noexcept
    {
        lastSlotMixAutomationValue_.store(value, std::memory_order_relaxed);
    }

    juce::PluginDescription getDescription() const;

    // ── Lifecycle ────────────────────────────────────────────────────────

    void prepare(double sampleRate, int blockSize)
    {
        const juce::Array<int> disabledAuxInputBuses;
        prepare(sampleRate, blockSize, disabledAuxInputBuses);
    }

    void prepare(double sampleRate, int blockSize, const juce::Array<int>& enabledAuxInputBuses)
    {
        prepareCount_.fetch_add(1, std::memory_order_relaxed);

        if (!plugin_)
        {
            if (sandboxProxy_)
            {
                prepareSandboxed(sampleRate, blockSize, enabledAuxInputBuses);
                if (prepared_)
                {
                    sampleRate_ = sampleRate;
                    blockSize_  = blockSize;
                }
            }
            return;
        }

        sampleRate_ = sampleRate;
        blockSize_  = blockSize;

        // JUCE lifecycle contract: releaseResources() follows the end of one
        // playback/device lifetime before prepareToPlay() begins another. Keep
        // this wrapper defensive even if an upstream host lifecycle omits the
        // release; processors commonly retain buffers/pointers after processing.
        const bool wasPrepared = prepared_;
        const juce::AudioProcessor::BusesLayout previousLayout = effectiveLayout_;
        if (wasPrepared)
            releaseResources();

        plugin_->setRateAndBufferSizeDetails(sampleRate, blockSize);

        // C7-generic-negotiation: choose a MAIN layout from the plugin's own
        // declared capabilities instead of assuming stereo. Stereo is preferred
        // (the engine is stereo), but mono and asymmetric layouts are accepted
        // whenever the plugin declares them. The auxiliary-bus policy below is
        // unchanged (sidechain routing contract).
        juce::Array<int> inCandidates, outCandidates;
        buildMainLayoutCandidates (inCandidates, outCandidates);

        bool negotiated = false;
        const juce::AudioProcessor::BusesLayout current = plugin_->getBusesLayout();
        for (int inCh : inCandidates)
        {
            for (int outCh : outCandidates)
            {
                juce::AudioProcessor::BusesLayout trial = current;
                if (plugin_->getBusCount (true)  > 0) trial.getChannelSet (true,  0) = makeChannelSet (inCh);
                if (plugin_->getBusCount (false) > 0) trial.getChannelSet (false, 0) = makeChannelSet (outCh);
                applyAuxiliaryBusPolicy (trial, enabledAuxInputBuses);
                // JUCE's isBusesLayoutSupported() is protected; the public
                // setBusesLayout() performs the same capability check and only
                // mutates bus state when a candidate is ACCEPTED (unsupported
                // candidates return false without side effects), so it is the
                // correct host-facing probe.
                if (DAW::safelySetBusesLayout (plugin_.get(), trial).succeeded)
                {
                    negotiated = true;
                    break;
                }

                // VST3 spec (IAudioProcessor::setBusArrangements): a plug-in
                // may ADAPT its buses and still return kResultFalse — "does not
                // accept these arrangements, but can adapt its current
                // arrangements ... it should modify its buses arrangements and
                // return kResultFalse". JUCE's host-side wrapper treats that as
                // a hard failure and leaves its own bus state disabled, so an
                // auxiliary bus never reaches activateBus() and the sidechain
                // stays silent — the "EXT does not recognise the sidechain"
                // defect on restored projects. The plug-in has now adapted, so
                // an IDENTICAL re-request matches its current arrangement and
                // is accepted (kResultTrue) — exactly the re-query the spec
                // requires of the host. Bounded: one extra call, and only
                // while a sidechain bus is being negotiated.
                if (! enabledAuxInputBuses.isEmpty()
                    && DAW::safelySetBusesLayout (plugin_.get(), trial).succeeded)
                {
                    negotiated = true;
                    break;
                }
            }
            if (negotiated) break;
        }

        if (!negotiated)
        {
            // Fallback: keep the plugin's own current layout with the host's
            // auxiliary policy applied. If even that is unsupported, the slot
            // must not be published into the active processing graph.
            juce::AudioProcessor::BusesLayout trial = current;
            applyAuxiliaryBusPolicy (trial, enabledAuxInputBuses);
            negotiated = DAW::safelySetBusesLayout (plugin_.get(), trial).succeeded;

            // Same spec retry as in the candidate loop above: the failed
            // first request may already have made the plug-in adapt to
            // `trial`, in which case the identical re-request is accepted.
            if (! negotiated && ! enabledAuxInputBuses.isEmpty())
                negotiated = DAW::safelySetBusesLayout (plugin_.get(), trial).succeeded;
        }

        if (!negotiated)
        {
            // Transactional failure: restore the previous valid layout so a
            // rejected reconfiguration never destroys a working slot.
            prepared_ = false;
            if (wasPrepared && restorePreviousLayout (previousLayout, sampleRate, blockSize))
                return;
            juce::Logger::writeToLog ("[APEX-LAYOUT] plugin=\"" + getName()
                + "\" has no supported bus layout — slot left unprepared");
            return;
        }

        const bool layoutAccepted = negotiated;
        DBG("[APEX-DIAG-SETLAYOUT] plugin=" << plugin_->getName()
            << " requestedInput=" << (plugin_->getBusesLayout().inputBuses.size() > 0 ? plugin_->getBusesLayout().inputBuses[0].getDescription() : juce::String("none"))
            << " requestedOutput=" << (plugin_->getBusesLayout().outputBuses.size() > 0 ? plugin_->getBusesLayout().outputBuses[0].getDescription() : juce::String("none"))
            << " enabledAuxInputs=" << enabledAuxInputBuses.size()
            << " calledFrom=" << __FUNCTION__);

        // C6-external-sidechain ROOT FIX: JUCE's default
        // AudioProcessor::applyBusLayouts only mutates the JUCE-level bus
        // state. Hosted VST3 wrappers apply speaker arrangements, activate
        // component buses and rebuild their per-bus channel maps exclusively
        // inside prepareToPlay() — which early-returns while the component is
        // active with an unchanged rate/block size. The live Pro-C 2 EXT
        // failure was exactly this: the JUCE bus reported enabled (totalIn=4)
        // while the real VST3 component never received activateBus and its
        // channel map still delivered only the main bus, so the key never
        // reached the external sidechain. Deactivate first (the canonical
        // JUCE kIoChanged pattern — see juce_VST3PluginFormatImpl.h
        // restartComponentOnMessageThread) so prepareToPlay applies the
        // negotiated bus layout to the real component.
        if (plugin_->getBusCount(true) > 1 && !wasPrepared)
        {
            plugin_->releaseResources();
            prepared_ = false;
        }

        auto prepResult = DAW::safelyPrepareToPlay(plugin_.get(), sampleRate, blockSize);
        if (!prepResult.succeeded)
        {
            prepared_ = false;
            if (wasPrepared && restorePreviousLayout (previousLayout, sampleRate, blockSize))
                return;
            juce::Logger::writeToLog("[APEX-SEH] plugin=\"" + getName()
                + "\" failed in prepareToPlay() — plugin cannot be used");
            return;
        }
        prepared_ = true;
        effectiveLayout_ = plugin_->getBusesLayout();
        activeMainInputChannels_  = plugin_->getBusCount (true)  > 0 ? effectiveLayout_.getChannelSet (true,  0).size() : 0;
        activeMainOutputChannels_ = plugin_->getBusCount (false) > 0 ? effectiveLayout_.getChannelSet (false, 0).size() : 0;
        activeProcessWidth_ = juce::jmax (plugin_->getTotalNumInputChannels(),
                                          plugin_->getTotalNumOutputChannels());
        // One-shot restore hints are consumed by the first prepare.
        preferredMainInChannels_  = 0;
        preferredMainOutChannels_ = 0;
        registerParameterListeners();

        // C6-external-sidechain forensic (message thread — control plane only):
        // Release-visible bus-topology dump after negotiation so live sessions
        // prove the real VST3 auxiliary-bus state (the DBG-based layout logs
        // are compiled out of Release builds).
        {
            juce::String busDump;
            for (int b = 0; b < plugin_->getBusCount(true); ++b)
                if (auto* inputBus = plugin_->getBus(true, b))
                    busDump += " in" + juce::String(b) + ":\"" + inputBus->getName() + "\""
                        + (inputBus->isEnabled() ? "=on" : "=off")
                        + "/" + juce::String(inputBus->getNumberOfChannels()) + "ch";
            for (int b = 0; b < plugin_->getBusCount(false); ++b)
                if (auto* outputBus = plugin_->getBus(false, b))
                    busDump += " out" + juce::String(b) + ":\"" + outputBus->getName() + "\""
                        + (outputBus->isEnabled() ? "=on" : "=off")
                        + "/" + juce::String(outputBus->getNumberOfChannels()) + "ch";

            juce::String requestedAuxList;
            for (int b : enabledAuxInputBuses)
                requestedAuxList += juce::String(b) + ",";

            juce::Logger::writeToLog("[APEX-SC-BUS] plugin=\"" + getName()
                + "\" requestedAuxCount=" + juce::String(enabledAuxInputBuses.size())
                + " requestedAux=" + requestedAuxList
                + " layoutAccepted=" + juce::String(layoutAccepted ? 1 : 0)
                + " totalIn=" + juce::String(plugin_->getTotalNumInputChannels())
                + " totalOut=" + juce::String(plugin_->getTotalNumOutputChannels())
                + " inBusCount=" + juce::String(plugin_->getBusCount(true))
                + busDump);
        }

        // Pre-allocate the process scratch to the EXACT negotiated width on
        // this message thread so processBlockInternal never allocates on the
        // audio thread (C7 mono/asymmetric adapter contract).
        const int worstCaseBlock = juce::jmax (blockSize, 8192);
        if (processScratch_.getNumChannels() != juce::jmax (2, activeProcessWidth_)
            || processScratch_.getNumSamples() < worstCaseBlock)
            processScratch_.setSize (juce::jmax (2, activeProcessWidth_), worstCaseBlock, false, true, false);
    }

    void prepareForOffline(double sampleRate, int blockSize)
    {
        const juce::Array<int> disabledAuxInputBuses;
        prepareForOffline(sampleRate, blockSize, disabledAuxInputBuses);
    }

    void prepareForOffline(double sampleRate, int blockSize, const juce::Array<int>& enabledAuxInputBuses)
    {
        // Offline export is not yet supported for sandboxed slots (Phase C).
        // Keep the realtime-prepared proxy untouched and do not reprepare it
        // for an offline quantum the sandbox pipeline cannot serve.
        if (sandboxProxy_)
        {
            juce::ignoreUnused(sampleRate, blockSize, enabledAuxInputBuses);
            return;
        }

        const bool bypassWasEnabled = isBypassed();

        releaseResources();
        if (plugin_)
            plugin_->setNonRealtime(true);

        prepare(sampleRate, blockSize, enabledAuxInputBuses);
        setBypassed(bypassWasEnabled);
        offlinePrepared_ = true;
    }

    void restoreRealtimePrepare(double sampleRate, int blockSize)
    {
        const juce::Array<int> disabledAuxInputBuses;
        restoreRealtimePrepare(sampleRate, blockSize, disabledAuxInputBuses);
    }

    void restoreRealtimePrepare(double sampleRate, int blockSize, const juce::Array<int>& enabledAuxInputBuses)
    {
        if (sandboxProxy_)
        {
            juce::ignoreUnused(sampleRate, blockSize, enabledAuxInputBuses);
            return;
        }

        const bool bypassWasEnabled = isBypassed();

        releaseResources();
        if (plugin_)
            plugin_->setNonRealtime(false);

        prepare(sampleRate, blockSize, enabledAuxInputBuses);
        setBypassed(bypassWasEnabled);
        offlinePrepared_ = false;
    }

    void releaseResources()
    {
        if (sandboxProxy_)
            return;   // proxy owns its worker lifecycle; a chain re-prepare reprepares it
        if (plugin_ && prepared_)
        {
            plugin_->releaseResources();
            prepared_ = false;
        }
    }

    void reset();

    // ── Layout state (C7) ────────────────────────────────────────────────

    int getActiveMainInputChannels()  const noexcept { return activeMainInputChannels_; }
    int getActiveMainOutputChannels() const noexcept { return activeMainOutputChannels_; }
    int getActiveProcessWidth()       const noexcept { return activeProcessWidth_; }

    /** Persisted negotiated-layout record (project save). */
    juce::ValueTree getSavedLayoutTree() const
    {
        juce::ValueTree tree ("Layout");
        tree.setProperty ("mainInputChannels",  activeMainInputChannels_,  nullptr);
        tree.setProperty ("mainOutputChannels", activeMainOutputChannels_, nullptr);
        return tree;
    }

    /** One-shot restore hint: the next prepare() prefers this main layout
        before falling back to the generic candidate order. */
    void applyRestoredLayout (const juce::ValueTree& tree) noexcept
    {
        if (! tree.isValid() || ! tree.hasType ("Layout"))
            return;
        preferredMainInChannels_  = juce::jlimit (0, 64, (int) tree.getProperty ("mainInputChannels",  0));
        preferredMainOutChannels_ = juce::jlimit (0, 64, (int) tree.getProperty ("mainOutputChannels", 0));
    }

    /** Detectable state-restore failure (C7C): keep the instance alive so the
        slot record and identity survive for recovery, but never let it process. */
    void markStateRestoreFailed() noexcept
    {
        stateRestoreFailed_ = true;
        releaseResources();
        prepared_ = false;
    }

    // ── Audio thread ─────────────────────────────────────────────────────

    /** Process audio in-place. Buffer must be stereo. No allocation, no locks. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        processBlock(buffer, midi, buffer.getNumSamples());
    }

    /** Process exactly the active callback range of a capacity-sized host
        buffer. The plugin must never receive inactive storage beyond the
        frame count used to prepare and schedule this quantum. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples);

    void processBlockForced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        processBlockForced(buffer, midi, buffer.getNumSamples());
    }

    void processBlockForced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples);

    /** C7: forced-processing entry for chain-built combined buffers whose
        layout already includes the negotiated auxiliary buses (sidechain
        targets). Bypasses the main-layout mapping adapter. */
    void processBlockCombinedForced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numSamples);

    int getLatencySamples() const;

    double getPreparedSampleRate() const noexcept { return sampleRate_; }
    int getPreparedBlockSize() const noexcept { return blockSize_; }
    bool isPrepared() const noexcept { return prepared_; }
    bool isOfflinePrepared() const noexcept { return offlinePrepared_; }

    /** Returns the underlying AudioProcessor. Used by PluginChainCore to
     *  inspect the bus layout for sidechain routing. Audio thread safe —
     *  only reads bus count, does not mutate. */
    juce::AudioProcessor* getProcessor() const noexcept { return plugin_.get(); }

    void configureAutomationContext(const TrackID& trackId,
                                    int pluginSlotIndex,
                                    PluginAutomationGestureCore* gestureCore,
                                    LastTouchedPluginParameterCore* lastTouchedCore)
    {
        trackId_ = trackId;
        pluginSlotIndex_ = pluginSlotIndex;
        gestureCore_ = gestureCore;
        lastTouchedCore_ = lastTouchedCore;
        registerParameterListeners();
        // registerParameterListeners() may early-return when already
        // registered — rebuild bindings here too so a later-arriving
        // context always produces fresh cached keys.
        rebuildRtAutomationBindings();
        // Phase E2B: sandboxed slots build their ordinal table + live-value
        // smoother seed here (control plane). Callers must invoke this after
        // the worker is prepared and after any state/replay operation whose
        // live parameter values are intended to become authoritative.
        if (sandboxProxy_ != nullptr)
            rebuildSandboxAutomationBindings();
    }

    /** Complete a pending automatic sandbox recovery on the control plane.
        The instance owns the E2B mirrors, so it rebuilds them from the fresh
        worker before asking the proxy to reopen its realtime gates. The chain
        holds this PluginInstanceCore through the call; no raw owner callback is
        retained by the proxy. */
    bool completeSandboxRecovery();

   #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** E2B test-only inspection of the control-plane binding publication and
        its ordinal-indexed initial state. The production surface is omitted. */
    int sandboxAutomationBindingCountForTesting() const noexcept
    {
        return static_cast<int>(sandboxAutomationBindings_.size());
    }

    float sandboxLastAutomationValueForTesting(int ordinal) const noexcept
    {
        if (ordinal < 0
            || static_cast<std::size_t>(ordinal) >= sandboxLastAutomationValues_.size())
            return -1.0f;
        return sandboxLastAutomationValues_[static_cast<std::size_t>(ordinal)];
    }

     float sandboxLastDeliveredValueForTesting(int ordinal) const noexcept
     {
         if (ordinal < 0
             || static_cast<std::size_t>(ordinal) >= sandboxLastDeliveredValues_.size())
             return -1.0f;
         return sandboxLastDeliveredValues_[static_cast<std::size_t>(ordinal)];
     }

     /** E2B test-only observation of the preallocated boundary-event count.
         This is intentionally a read-only POD seam; it does not alter the
         producer or transport semantics. */
      std::uint32_t sandboxStagedEventCountForTesting() const noexcept
      {
          return sandboxStagedEventCount_;
      }

       std::uint64_t sandboxAutomationRebuildCountForTesting() const noexcept
       {
           return sandboxAutomationRebuildCount_;
       }

       /** E2B test-only seam: replay one captured delivery report through the
           same pending-record commit path used by sandboxProcessHandoff().
           This exists only in hook-enabled test builds so a stale report can
           be tested against a reused sequence without changing the realtime
        or production API surface. */
        bool sandboxConsumeAutomationDeliveryReportForTesting(
            std::uint64_t workerGeneration,
            std::uint64_t sequence,
           bool submitted,
           bool automationBatchPublished,
           bool automationBatchValid) noexcept;
      #endif

    /** ONE canonical lane-evaluation implementation for both execution
        modes. Source precedence is exactly: legacy lane → APEX lane-store →
        manual bridge; later sources override earlier ones. */
    template <typename BindingT>
    static float computeCanonicalTarget(
        const BindingT& binding,
        const apex::automation::AutomationLaneStore& laneStore,
        const AutomationSnapshot* automationSnap,
        const TrackID& trackId,
        int64_t samplePosition,
        double ppqPosition,
        float currentValue) noexcept
    {
        float newValue = currentValue;
        if (automationSnap != nullptr)
        {
            if (auto* lane = automationSnap->findLaneRT(trackId, binding.coreParamId))
                if (lane->enabled)
                    newValue = lane->getValueAtSample(samplePosition, currentValue);
        }
        if (binding.apexPid != apex::automation::kInvalidParameterID)
        {
            if (auto apexLane = laneStore.findLaneRT (binding.apexPid))
            {
                auto snap = apexLane->getSnapshot();
                if (snap != nullptr && ! snap->empty())
                    newValue = apex::automation::AutomationLane::evaluateAt (*snap, ppqPosition);
            }
        }
        if (binding.bridgePid != apex::automation::kInvalidParameterID)
        {
            if (auto bridgeLane = laneStore.findLaneRT (binding.bridgePid))
            {
                auto snap = bridgeLane->getSnapshot();
                if (snap != nullptr && ! snap->empty())
                    newValue = apex::automation::AutomationLane::evaluateAt (*snap, ppqPosition);
            }
        }
        return newValue;
    }

    void applyAutomationAtSample(const TrackID& trackId,
                                 int pluginSlotIndex,
                                 const AutomationSnapshot* automationSnap,
                                 int64_t samplePosition,
                                 double sampleRate = 44100.0,
                                 double bpm = 120.0,
                                 int numSamples = 512) noexcept
    {
        if (sandboxProxy_ != nullptr)
        {
            applySandboxedAutomationAtSample(trackId, automationSnap,
                                             samplePosition, sampleRate, bpm,
                                             numSamples);
            return;
        }
        if (plugin_ == nullptr)
            return;

        auto& params = plugin_->getParameters();
        auto& keyRegistry = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto& laneStore   = apex::automation::AutomationLaneStore::getInstance();
        auto& registry    = apex::automation::AutomationParameterRegistry::getInstance();
        const double ppqPosition = (sampleRate > 0.0 && bpm > 0.0)
            ? ((double)samplePosition / sampleRate) * (bpm / 60.0)
            : 0.0;

        // All binding strings were built on the message thread. If bindings
        // are absent/stale (context not configured yet), skip automation for
        // this block rather than allocate or lock on the audio thread.
        jassertquiet ((int) rtAutomationBindings_.size() == parameterInfos_.size()
                      && trackId == trackId_ && pluginSlotIndex == pluginSlotIndex_);
        if ((int) rtAutomationBindings_.size() < parameterInfos_.size()
            || (int) lastAutomationValues_.size() < parameterInfos_.size())
            return;

        const std::uint64_t keyGen = keyRegistry.getChangeGeneration();

        for (int i = 0; i < parameterInfos_.size(); ++i)
        {
            const auto& info = parameterInfos_.getReference(i);
            const int parameterIndex = info.parameterIndex;
            if (parameterIndex < 0 || parameterIndex >= params.size() || params[parameterIndex] == nullptr)
                continue;

            auto* pluginParam = params[parameterIndex];
            const float currentValue = pluginParam->getValue();
            auto& binding = rtAutomationBindings_[(size_t) i];

            // Re-resolve published IDs when the key registry moved (new key
            // created, project restore). Lock-free snapshot lookups only —
            // no string building, no CriticalSection on the audio thread.
            if (binding.keyGeneration != keyGen)
            {
                binding.apexPid   = keyRegistry.findIDRT (binding.apexKey);
                binding.bridgePid = keyRegistry.findIDRT (binding.bridgeKey);
                binding.keyGeneration = keyGen;
            }

            // Canonical source precedence (legacy lane → APEX lane-store →
            // manual bridge) lives in ONE shared evaluator used identically by
            // the InProcess and Sandboxed sinks — execution mode must never
            // change the automation math.
            const float newValue = computeCanonicalTarget(
                binding, laneStore, automationSnap, trackId,
                samplePosition, ppqPosition, currentValue);

            // De-zipper smoothing (10 ms one-pole, closed-form advancement).
            // The smoother must advance across the FULL audio block: the old
            // iteration loop was capped at 1024 samples, so host blocks above
            // 1024 (e.g. 2048) applied a stale mid-ramp value every block and
            // desynchronized parameter/automation movement. advance() is the
            // exact closed-form equivalent of iterating numSamples times and
            // is block-size invariant (256/512/1024/2048/non-power-of-two).
            float& lastVal = lastAutomationValues_[(size_t) i];
            float smoothedValue = newValue;
            if (std::abs (lastVal - newValue) > 0.0001f)
            {
                const float coeff = AutomationSmootherCore::makeCoeff (sampleRate, 0.010);
                smoothedValue = AutomationSmootherCore::advance (lastVal, newValue, coeff, numSamples);
            }
            lastVal = smoothedValue;

            if (binding.apexPid != apex::automation::kInvalidParameterID)
            {
                if (auto automationParam = registry.findRT (binding.apexPid))
                {
                    automationParam->setValueFromAutomation (smoothedValue);
                    continue;
                }
            }

            pluginParam->setValue (smoothedValue);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PHASE E2B — canonical automation producer → sandbox delivery.
    // The automation MATH (source precedence + 10 ms closed-form smoother)
    // is shared with the in-process path above; only the SINK differs:
    // InProcess → non-notifying setValue; Sandboxed → frozen E2A boundary
    // event at callback-local offset 0.
    // ═══════════════════════════════════════════════════════════════════════

    struct SandboxAutomationBinding
    {
        juce::String coreParamId;   // legacy snapshot lane key (control plane)
        juce::String apexKey;       // APEX lane-store key (control plane)
        juce::String bridgeKey;     // manual bridge key (control plane)
        apex::automation::ParameterID apexPid
            = apex::automation::kInvalidParameterID;
        apex::automation::ParameterID bridgePid
            = apex::automation::kInvalidParameterID;
        std::uint64_t keyGeneration = ~0ull;
        std::uint32_t ordinal = 0;  // E2A parameterOrdinal == worker index
        bool representable = false; // ordinal < 128 (frozen E2A domain)
    };

    /** Phase E2B (control plane): build sandbox RT automation bindings from
        E1 worker metadata and seed the smoother mirror + last-delivered state
        from the ACTUAL live worker parameter values. Strings/hash lookups are
        allowed here; the RT path reads only pre-resolved PODs. Defined in
        PluginInstanceCore.cpp (the proxy full type lives there). */
    bool rebuildSandboxAutomationBindings();

    /** Clear all E2B binding/state publication when the current worker cannot
        provide a complete authoritative metadata/live-value set. Control
        plane only; the event count is reset so no previously staged batch can
        be handed to the worker after disarming. */
    void clearSandboxAutomationBindings() noexcept;

    /** Control-plane reset of E2B delivery records without changing the
        authoritative binding/live-value seed. Used across a stream-generation
        transition so an old sequence can never commit into a new sidecar. */
    void clearSandboxAutomationPendingDeliveryRecords() noexcept;

    /** Phase E2B RT producer: ONE canonical smoother advance per host
        callback, staging boundary events (offset 0) into the preallocated
        scratch for the frozen E2A transport. No allocation, no locks, no
        strings. Defined in PluginInstanceCore.cpp. */
    void applySandboxedAutomationAtSample(
        const TrackID& trackId,
        const AutomationSnapshot* automationSnap,
        int64_t samplePosition,
        double sampleRate,
        double bpm,
        int numSamples) noexcept;

    bool sandboxAutomationDeliveryActive() const noexcept;

    /** Factor the sandbox E2B event handoff so all three sandbox process
        branches behave identically. Returns true when the call was consumed.
        Defined in PluginInstanceCore.cpp. */
    bool sandboxProcessHandoff(juce::AudioBuffer<float>& buffer, int numSamples);

    bool commitSandboxAutomationDeliveryReport(
        std::uint64_t workerGeneration,
        std::uint64_t sequence,
        bool submitted,
        bool automationBatchPublished,
        bool automationBatchValid) noexcept;

    // ── Editor ───────────────────────────────────────────────────────────

    bool hasEditor() const
    {
        // Do not call the vendor's VST3 hasEditor()/createView probe here.
        // It is not a cached query, and openEditor() performs the single real
        // createEditor() call inside the guarded control-plane boundary.
        return plugin_ != nullptr;
    }

    /** Callback: fired when editor is minimized (for BubbleTaskbar wiring). */
    std::function<void()> onEditorMinimized;
    /** Callback: fired when editor is closed (for BubbleTaskbar cleanup). */
    std::function<void()> onEditorClosed;

    void openEditor(juce::Component* parent = nullptr)
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        DBG("[CRASH TRACE] action=plugin_editor_open plugin=" << getName());
        auto logOpen = [&](const juce::String& stage, const juce::String& detail)
        {
            juce::Logger::writeToLog("[PluginInstanceCore][OpenTrace] plugin=\"" + getName()
                + "\" stage=" + stage
                + " detail=" + detail);
        };

        logOpen("entered",
                "parentValid=" + juce::String(parent != nullptr ? 1 : 0)
                + " pluginValid=" + juce::String(plugin_ != nullptr ? 1 : 0)
                + " hasEditorCheckedInsideGuard");

        // NOTE: hasEditor() is intentionally not called here — it is checked
        // inside DAW::safelyCreateEditor() which runs under /EHa SEH protection.
        // VST3 plugins (e.g. Antares Auto-Tune EFX+) can crash with access
        // violation inside hasEditor() when their COM interfaces are corrupted.
        if (sandboxProxy_)
        {
            openSandboxEditor(parent);
            return;
        }
        if (!plugin_)
        {
            logOpen("failed", "reason=noPlugin");
            return;
        }
        if (editorWindow_)
        {
            logOpen("reuseExistingWindow", "windowValid=1");
            if (parent != nullptr)
                editorWindow_->setOwnerComponent(parent->getTopLevelComponent());
            editorWindow_->setVisible(true);
            editorWindow_->toFront(true);
            editorWindow_->clampToScreen();
            logOpen("success", "existingWindowShown=1");
            return;
        }

        logOpen("createInstance_success", "pluginInstanceValid=1");
        logOpen("createEditor_start", "pluginInstanceValid=1");
        juce::AudioProcessorEditor* ed = nullptr;
        auto sehResult = DAW::safelyCreateEditor(plugin_.get(), ed);
        if (!sehResult.succeeded)
        {
            if (sehResult.exceptionCode != 0)
            {
                logOpen("failed", "reason=SEH_ACCESS_VIOLATION code=0x"
                    + juce::String::toHexString((int)sehResult.exceptionCode));
                juce::Logger::writeToLog("[PluginInstanceCore][OpenTrace] plugin=\"" + getName()
                    + "\" stage=createEditor SEH exception code=0x"
                    + juce::String::toHexString((int)sehResult.exceptionCode)
                    + " — plugin likely has a bug in its editor creation");
            }
            else
            {
                logOpen("failed", "reason=createEditorReturnedNull");
            }
            return;
        }
        logOpen("createEditor_success",
                "editorValid=1 width=" + juce::String(ed->getWidth())
                + " height=" + juce::String(ed->getHeight())
                + " resizable=" + juce::String(ed->isResizable() ? 1 : 0));

        bool pluginResizable = ed->isResizable();
        int edW = ed->getWidth();
        int edH = ed->getHeight();

        // Build plugin identity key for persistence
        auto desc = plugin_->getPluginDescription();
        auto pluginKey = PluginWindowScaleStore::makePluginKey(
            desc.pluginFormatName, desc.manufacturerName,
            desc.name, juce::String(desc.uniqueId));

        // Get persisted scale
        auto& store = PluginWindowScaleStore::getInstance();
        float savedScale = store.getEffectiveScale(pluginKey);
        logOpen("persistedState_loaded",
                "pluginKey=\"" + pluginKey + "\" savedScale=" + juce::String((int)(savedScale * 100)) + "%");

        auto* ownerComponent = parent != nullptr ? parent->getTopLevelComponent()
                                                 : juce::TopLevelWindow::getActiveTopLevelWindow();
        auto name = plugin_->getName() + " \u2014 " + desc.manufacturerName;
        logOpen("window_create_start", "safeFallback=1 name=\"" + name + "\"");
        auto window = std::make_unique<PluginEditorWindow>(name, ownerComponent);
        logOpen("window_create_success", "windowValid=" + juce::String(window != nullptr ? 1 : 0));
        std::weak_ptr<void> weak = lifetimeToken_;
        window->onGetSlotMix = [weak, this]() -> float
        {
            return weak.lock() ? getSlotMixNormalized() : 1.0f;
        };
        window->onSetSlotMix = [weak, this](float value)
        {
            if (weak.lock())
                setSlotMixNormalized(value);
        };
        window->onGetSlotBypass = [weak, this]() -> bool
        {
            return weak.lock() ? getSlotMixBypass() : false;
        };
        window->onSetSlotBypass = [weak, this](bool bypass)
        {
            if (weak.lock())
                setSlotMixBypass(bypass);
        };
        window->onGetAutomationMode = []() -> AutomationWriteMode
        {
            return PluginAutomationRecorderCore::getGlobalMode();
        };
        window->onSetAutomationMode = [](AutomationWriteMode mode)
        {
            PluginAutomationRecorderCore::setGlobalMode(mode);
        };
        window->onGetLastTouchedLabel = []() -> juce::String
        {
            const auto last = LastTouchedPluginParameterCore::getInstance().get();
            if (!last.isValid())
                return "Last touched: --";
            return "Last: " + last.pluginDisplayName + " / " + last.parameterName;
        };
        window->onCloseRequested = [weak, this]
        {
            if (weak.lock())
            {
                juce::MessageManager::callAsync([weak, this]
                {
                    if (weak.lock())
                        closeEditor();
                });
            }
        };
        window->onMinimiseRequested = [weak, this]
        {
            if (weak.lock())
                if (onEditorMinimized) onEditorMinimized();
        };
        logOpen("callbacks_wired", "close=1 minimize=1 maximize=1 settings=1 bubblePerOpenPath=external");

        window->setEditorContent(ed);
        logOpen("content_attach_success", "contentOwned=1 editorValid=1");
        window->storeNativeEditorSize(edW, edH);
        window->setEditorIsResizable(pluginResizable);
        window->setPluginKey(pluginKey);
        logOpen("window_configured",
                "pluginResizable=" + juce::String(pluginResizable ? 1 : 0)
                + " pluginKeyValid=" + juce::String(pluginKey.isNotEmpty() ? 1 : 0));

        constexpr float safeOpenScale = 1.0f;
        logOpen("persistedState_ignored", "savedScaleIgnored=1 openScale=100%");
        window->applyScalePreset(safeOpenScale);
        logOpen("scale_apply_success", "scale=100%");

        // Ensure window is on-screen BEFORE making visible
        logOpen("pre_show", "clampToScreen_start=1");
        window->clampToScreen();
        logOpen("pre_show", "clampToScreen_success=1");
        if (parent != nullptr)
        {
            window->centreAroundComponent(parent, window->getWidth(), window->getHeight());
            logOpen("pre_show", "centreAroundParent=1");
        }
        window->setVisible(true);
        window->toFront(true);
        logOpen("show_success", "visible=1 front=1");

        editorWindow_ = std::move(window);
        logOpen("success",
                "editorWindowCreated=1 finalBounds=" + editorWindow_->getBounds().toString());
        juce::ignoreUnused(parent);
    }

    /** Restore a previously minimized editor window. */
    void showEditor()
    {
        if (sandboxProxy_)
        {
            openEditor();
            return;
        }

        if (editorWindow_)
        {
            editorWindow_->setVisible(true);
            editorWindow_->toFront(true);
            editorWindow_->clampToScreen();
            DBG("[PluginInstanceCore] showEditor: restored for \"" + getName() + "\"");
        }
        else
        {
            DBG("[PluginInstanceCore] showEditor: no window, calling openEditor for \"" + getName() + "\"");
            openEditor();
        }
    }

    void closeEditor()
    {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        if (sandboxProxy_)
        {
            closeSandboxEditor();
            return;
        }
        if (! editorWindow_) return;
        DBG("[CRASH TRACE] action=plugin_editor_close plugin=" << getName());
        DBG("[PluginInstanceCore] closeEditor: destroying editor for \"" << getName()
            + "\" hadWindow=" + juce::String(1));

        // C6-editor-lifetime TWO-PHASE teardown:
        //   1. Detach the native plugin editor from the shell content tree
        //      while the plugin UI state is fully intact.
        //   2. Destroy the shell window — its HWND teardown dispatches
        //      WM_DESTROY into the plugin's window proc, which is safe while
        //      the editor (and the plugin instance) still exist.
        //   3. Destroy the plugin editor itself, with no window remaining.
        // Live crash evidence (FabFilter Pro-C 2, undo during playback):
        // destroying the editor before the HWND left the plugin's window
        // proc calling through freed UI state during NtUserDestroyWindow.
        std::unique_ptr<juce::Component> editor = editorWindow_->takeNativeEditor();
        editorWindow_.reset();
        editor.reset();

        if (onEditorClosed) onEditorClosed();
    }

    bool isEditorOpen() const
    {
        if (sandboxProxy_)
            return isSandboxEditorOpen();
        return editorWindow_ != nullptr && editorWindow_->isVisible();
    }
    bool isEditorMinimized() const { return editorWindow_ != nullptr && !editorWindow_->isVisible(); }
    bool isEditorCreated() const
    {
        if (sandboxProxy_)
            return isSandboxEditorOpen();
        return editorWindow_ != nullptr;
    }

    // ── State ────────────────────────────────────────────────────────────

    /** Ownership-neutral state checkpoint.  Control-plane only: the caller
        may be an in-process plugin or a worker-owned sandbox proxy. */
    bool capturePluginState(juce::MemoryBlock& state, juce::String& error) const;

    /** Ownership-neutral state restore.  Control-plane only.  A successful
        sandbox restore also rebuilds the existing E2B binding/seed table. */
    bool restorePluginState(const juce::MemoryBlock& state, juce::String& error);

    /** Compatibility read wrapper.  Durable project persistence uses
        capturePluginState() so a worker capture failure is observable. */
    juce::MemoryBlock getState() const;

    /** Compatibility restore wrapper.  Durable restore uses the result-bearing
        restorePluginState() overload. */
    bool setState(const juce::MemoryBlock& block);

private:
#if JUCE_WINDOWS
    static bool validateAndPresentSandboxEditor(
        const SandboxedPluginProxyCore& proxy,
        juce::String& error);
#endif

    void openSandboxEditor(juce::Component* parent);
    void closeSandboxEditor();
    bool isSandboxEditorOpen() const noexcept;

    struct ParameterInfo
    {
        int parameterIndex = -1;
        juce::String parameterId;
        juce::String name;
        int numSteps = 0;
        bool isDiscrete = false;
        bool isBoolean = false;
    };

    // ── C7 layout negotiation helpers (message thread only) ──────────────

    /** Explicit channel sets for the hosting cases APEX implements. 0 =
        disabled (no bus), 1 = mono, 2 = stereo. APEX intentionally does not
        fabricate arbitrary N-channel layouts it cannot host. */
    static juce::AudioChannelSet makeChannelSet (int channels) noexcept
    {
        switch (channels)
        {
            case 0:  return juce::AudioChannelSet::disabled();
            case 1:  return juce::AudioChannelSet::mono();
            default: return juce::AudioChannelSet::stereo();
        }
    }

    /** Deterministic MAIN layout candidate order:
          1. restored saved-layout hint (one-shot) when structurally valid
          2. stereo (the engine's native width)
          3. mono
        Asymmetric combinations are produced by the cross product of the two
        lists, so 1->2, 2->1, 0->1 and 0->2 are all representable. Every
        candidate must still pass plugin.isBusesLayoutSupported() in prepare().
        Duplicate values are tolerated (the support check deduplicates by
        simply accepting the first match). */
    void buildMainLayoutCandidates (juce::Array<int>& inCandidates,
                                    juce::Array<int>& outCandidates) const noexcept
    {
        inCandidates.clearQuick();
        outCandidates.clearQuick();

        const bool hasInput  = plugin_->getBusCount (true)  > 0;
        const bool hasOutput = plugin_->getBusCount (false) > 0;

        if (! hasInput)
        {
            inCandidates.add (0);                       // instrument: no main input
        }
        else
        {
            if (preferredMainInChannels_ > 0)
                inCandidates.add (preferredMainInChannels_);
            inCandidates.add (2);
            inCandidates.add (1);
        }

        if (! hasOutput)
        {
            outCandidates.add (0);
        }
        else
        {
            if (preferredMainOutChannels_ > 0)
                outCandidates.add (preferredMainOutChannels_);
            outCandidates.add (2);
            outCandidates.add (1);
        }
    }

    /** Auxiliary (sidechain) bus policy — independent of MAIN width. Enabled
        auxiliary inputs keep the current / last-enabled / default layout in
        that priority (stereo fallback, identical to the validated sidechain
        behavior); every other auxiliary input and ALL auxiliary outputs stay
        disabled. Sidechain channels are never reinterpreted as extra main
        channels. */
    void applyAuxiliaryBusPolicy (juce::AudioProcessor::BusesLayout& layout,
                                  const juce::Array<int>& enabledAuxInputBuses) const noexcept
    {
        for (int bus = 1; bus < plugin_->getBusCount (true); ++bus)
        {
            if (enabledAuxInputBuses.contains (bus))
            {
                juce::AudioChannelSet busLayout = juce::AudioChannelSet::stereo();
                if (auto* inputBus = plugin_->getBus (true, bus))
                {
                    if (inputBus->isEnabled() && ! inputBus->getCurrentLayout().isDisabled())
                        busLayout = inputBus->getCurrentLayout();
                    else if (! inputBus->getLastEnabledLayout().isDisabled())
                        busLayout = inputBus->getLastEnabledLayout();
                    else if (! inputBus->getDefaultLayout().isDisabled())
                        busLayout = inputBus->getDefaultLayout();
                }

                if (busLayout.isDisabled() || busLayout.size() <= 0)
                    busLayout = juce::AudioChannelSet::stereo();

                layout.getChannelSet (true, bus) = busLayout;
            }
            else
            {
                layout.getChannelSet (true, bus) = juce::AudioChannelSet::disabled();
            }
        }

        for (int bus = 1; bus < plugin_->getBusCount (false); ++bus)
            layout.getChannelSet (false, bus) = juce::AudioChannelSet::disabled();
    }

    /** Transactional rollback: re-apply the exact previously committed layout
        and bus-enable state, then re-prepare. Returns true only when the slot
        is fully prepared again — no half-applied layout may escape. */
    bool restorePreviousLayout (const juce::AudioProcessor::BusesLayout& previous,
                                double sampleRate,
                                int blockSize) noexcept
    {
        if (plugin_ == nullptr)
            return false;
        if (previous.inputBuses.isEmpty() && previous.outputBuses.isEmpty())
            return false;

        const auto layoutResult = DAW::safelySetBusesLayout (plugin_.get(), previous);
        if (! layoutResult.succeeded)
            return false;

        const auto prepResult = DAW::safelyPrepareToPlay (plugin_.get(), sampleRate, blockSize);
        if (! prepResult.succeeded)
            return false;

        prepared_        = true;
        effectiveLayout_ = plugin_->getBusesLayout();
        activeMainInputChannels_  = plugin_->getBusCount (true)  > 0
            ? effectiveLayout_.getChannelSet (true,  0).size() : 0;
        activeMainOutputChannels_ = plugin_->getBusCount (false) > 0
            ? effectiveLayout_.getChannelSet (false, 0).size() : 0;
        activeProcessWidth_ = juce::jmax (plugin_->getTotalNumInputChannels(),
                                          plugin_->getTotalNumOutputChannels());
        return true;
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    std::unique_ptr<SandboxedPluginProxyCore> sandboxProxy_;
    int sandboxQuantumSamples_ = 0;   // fixed worker quantum Q (independent of host block)
    std::unique_ptr<PluginEditorWindow>         editorWindow_;
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;

    // Per-parameter last automation value — used for smooth interpolation
    // between block-rate automation reads. Keyed by parameterIndex.
    // ── Cached RT automation bindings (built on the message thread) ──────
    struct RtAutomationBinding
    {
        juce::String coreParamId;   // AutomationManagerCore snapshot lane key
        juce::String apexKey;       // APEX lane-store key
        juce::String bridgeKey;     // manual-lane bridge key
        apex::automation::ParameterID apexPid   = apex::automation::kInvalidParameterID;
        apex::automation::ParameterID bridgePid = apex::automation::kInvalidParameterID;
        std::uint64_t keyGeneration = ~0ull;    // key-registry generation at last resolve
    };
    std::vector<RtAutomationBinding> rtAutomationBindings_;
    std::vector<float> lastAutomationValues_;

    // ── Phase E2B sandbox automation delivery state ───────────────────────
    // Control-plane bindings; RT path reads only pre-resolved PODs.
    struct SandboxEventScratch;   // defined in PluginInstanceCore.cpp (the
                                  // frozen E2A event type + windows.h live
                                  // there; this header must stay windows-free
                                  // for UI translation units).
    std::vector<SandboxAutomationBinding> sandboxAutomationBindings_;
    std::uint64_t sandboxKeyGeneration_ = ~0ull;
    // Ordinal-indexed (128 = frozen E2A worker target space).
    std::vector<float> sandboxLastAutomationValues_;
    std::vector<float> sandboxLastDeliveredValues_;
    // Preallocated boundary-event scratch (65 entries so event #65 reaches
    // the frozen E2A explicit whole-batch overflow path). Heap-allocated ONCE
    // on the control plane; the RT path only indexes into it.
     std::unique_ptr<SandboxEventScratch> sandboxEventScratch_;
     std::uint32_t sandboxStagedEventCount_ = 0;
    #if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
     std::uint64_t sandboxAutomationRebuildCount_ = 0;
    #endif
     bool   prepared_   = false;
    bool   offlinePrepared_ = false;
    std::atomic<bool> bypassed_{false};
    std::shared_ptr<void> lifetimeToken_;
    // C7 process adapter state — written on the message thread in prepare(),
    // read on the audio thread in processBlockInternal(). processScratch_ is
    // pre-allocated to the exact negotiated width; the audio thread never
    // resizes it.
    juce::AudioBuffer<float> processScratch_;
    juce::AudioProcessor::BusesLayout effectiveLayout_;
    int activeMainInputChannels_  = 2;
    int activeMainOutputChannels_ = 2;
    int activeProcessWidth_       = 2;
    int preferredMainInChannels_  = 0;   // one-shot restore hints (0 = unset)
    int preferredMainOutChannels_ = 0;
    bool stateRestoreFailed_ = false;
    juce::Array<ParameterInfo> parameterInfos_;
    juce::String pluginInstanceId_ { juce::Uuid().toString() };
    TrackID trackId_;
    int pluginSlotIndex_ = -1;
    PluginAutomationGestureCore* gestureCore_ = nullptr;
    LastTouchedPluginParameterCore* lastTouchedCore_ = nullptr;
    bool parameterListenersRegistered_ = false;
    std::atomic<float> slotMixNormalized_ { 1.0f };
    std::atomic<bool> slotMixBypass_ { false };
    // Per-instance slot-mix smoother state. Keeping this with the published
    // slot avoids a dynamically growing chain-side container on the RT path.
    std::atomic<float> lastSlotMixAutomationValue_ { 0.0f };

    /** Control-plane sandbox preparation: first call starts the worker and
        prepares the real VST3 inside it; later calls reprepare (full worker
        restart) so negotiated plugin latency is always re-validated.
        Defined out-of-line (PluginInstanceCore.cpp). */
    void prepareSandboxed(double sampleRate, int blockSize,
                          const juce::Array<int>& enabledAuxInputBuses);

    void registerParameterListeners()
    {
        if (plugin_ == nullptr || parameterListenersRegistered_)
            return;
        parameterInfos_.clearQuick();
        auto& params = plugin_->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            if (p == nullptr || !p->isAutomatable())
                continue;

            ParameterInfo info;
            info.parameterIndex = i;
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                info.parameterId = withId->paramID;
            if (info.parameterId.isEmpty())
                info.parameterId = "param_" + juce::String(p->getParameterIndex());
            info.name = p->getName(256);
            info.numSteps = p->getNumSteps();
            info.isDiscrete = p->isDiscrete();
            info.isBoolean = p->isBoolean();
            parameterInfos_.add(info);
            p->addListener(this);
        }
        parameterListenersRegistered_ = true;
        // Keys + cached RT bindings are built here (message thread) — never
        // per block on the audio thread.
        rebuildRtAutomationBindings();
    }

    /** Build the cached RT automation bindings (key strings + resolved
     *  parameter IDs) for every automatable parameter. MESSAGE THREAD only —
     *  allocates and talks to the locked key registry. The audio thread only
     *  ever re-resolves IDs via lock-free snapshot lookups when the key
     *  registry generation changes. */
    void rebuildRtAutomationBindings()
    {
        rtAutomationBindings_.clear();
        lastAutomationValues_.clear();

        if (plugin_ == nullptr || parameterInfos_.isEmpty()
            || trackId_.isEmpty() || pluginSlotIndex_ < 0)
            return;

        using KR = apex::automation::AutomationParameterKeyRegistry;
        auto& keyRegistry = KR::getInstance();
        auto& params = plugin_->getParameters();

        rtAutomationBindings_.resize ((size_t) parameterInfos_.size());
        lastAutomationValues_.assign ((size_t) parameterInfos_.size(), 0.0f);

        for (int i = 0; i < parameterInfos_.size(); ++i)
        {
            const auto& info = parameterInfos_.getReference(i);
            auto& binding = rtAutomationBindings_[(size_t) i];
            binding.coreParamId = "plugin." + juce::String (pluginSlotIndex_)
                + "." + pluginInstanceId_ + "." + info.parameterId;

            // C7D-identity: resolve through the identity-strong key first,
            // then the legacy name-only key (existing lanes) — mirroring
            // PluginChainCore::configureSlotAutomation.
            const int componentUid = plugin_->getPluginDescription().uniqueId;
            binding.apexKey   = KR::pluginParamKey (trackId_, pluginSlotIndex_, getName(), info.parameterId, componentUid);
            binding.bridgeKey = "plugin." + trackId_ + ".core." + binding.coreParamId;
            binding.apexPid   = keyRegistry.findID (binding.apexKey);
            if (binding.apexPid == apex::automation::kInvalidParameterID)
                binding.apexPid = keyRegistry.findID (
                    KR::pluginParamKey (trackId_, pluginSlotIndex_, getName(), info.parameterId, 0));
            if (binding.apexPid == apex::automation::kInvalidParameterID)
                binding.apexPid = keyRegistry.getOrCreateID (binding.apexKey);
            binding.bridgePid = keyRegistry.findID (binding.bridgeKey);
            binding.keyGeneration = keyRegistry.getChangeGeneration();

            // Start the de-zipper smoother at the parameter's CURRENT value
            // so binding never produces a value dip (the old std::map
            // defaulted to 0.0f on first sight of each parameter).
            if (info.parameterIndex >= 0 && info.parameterIndex < params.size()
                && params[info.parameterIndex] != nullptr)
                lastAutomationValues_[(size_t) i] = params[info.parameterIndex]->getValue();
        }
    }

    void unregisterParameterListeners()
    {
        if (plugin_ == nullptr || !parameterListenersRegistered_)
            return;

        auto& params = plugin_->getParameters();
        for (auto* p : params)
            if (p != nullptr)
                p->removeListener(this);
        parameterListenersRegistered_ = false;
    }

    const ParameterInfo* findParameterInfo(int parameterIndex) const noexcept
    {
        for (const auto& info : parameterInfos_)
            if (info.parameterIndex == parameterIndex)
                return &info;
        return nullptr;
    }

    LastTouchedPluginParameter makeTargetForParameter(int parameterIndex, float normalizedValue) const
    {
        LastTouchedPluginParameter target;
        if (const auto* info = findParameterInfo(parameterIndex))
        {
            target.trackId = trackId_;
            target.pluginSlotIndex = pluginSlotIndex_;
            target.pluginInstanceId = pluginInstanceId_;
            target.parameterId = info->parameterId;
            target.parameterName = info->name;
            target.pluginDisplayName = getName();
            target.normalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
            target.timestampMs = juce::Time::getMillisecondCounterHiRes();
        }
        return target;
    }

    void parameterValueChanged(int parameterIndex, float newValue) override
    {
        if (gestureCore_ == nullptr || lastTouchedCore_ == nullptr)
            return;

        auto target = makeTargetForParameter(parameterIndex, newValue);
        if (!target.isValid())
            return;

        gestureCore_->updateValue(target, newValue);
        lastTouchedCore_->set(target);
    }

    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override
    {
        if (gestureCore_ == nullptr || lastTouchedCore_ == nullptr || plugin_ == nullptr)
            return;

        auto& params = plugin_->getParameters();
        const float value = parameterIndex >= 0 && parameterIndex < params.size() && params[parameterIndex] != nullptr
            ? params[parameterIndex]->getValue()
            : 0.0f;
        auto target = makeTargetForParameter(parameterIndex, value);
        if (!target.isValid())
            return;

        if (gestureIsStarting)
        {
            gestureCore_->beginExplicit(target);
            lastTouchedCore_->set(target);
        }
        else
        {
            gestureCore_->endExplicit(target);
        }
    }

    /** C7 process adapter: presents the plugin with EXACTLY the negotiated
        process width (max of active input/output channels) and maps the engine
        stereo signal across the active MAIN layout:
          stereo -> mono input  : 0.5 * (L + R)
          mono   -> stereo out  : duplicated
        Never allocates — the scratch set was sized in prepare() to the
        negotiated width. */
    void processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const int supplied   = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        lastProcessSamples_.store(numSamples, std::memory_order_relaxed);

        const int need    = activeProcessWidth_;
        const int mainIn  = activeMainInputChannels_;
        const int mainOut = activeMainOutputChannels_;

        // Exact-match fast path: the caller already presents the negotiated
        // width with an identity main mapping (stereo engine for 2/2, or an
        // instrument's output-only view). Preserves the validated stereo path.
        if (supplied == need
            && (mainIn == 0 || mainIn == supplied)
            && (mainOut == 0 || mainOut == supplied))
        {
            logProcessBlockDiag(buffer);
            handleProcessResult(DAW::safelyProcessBlock(plugin_.get(), buffer, midi),
                                buffer, supplied, numSamples);
            return;
        }

        // Mapping path (mono/asymmetric main layouts or narrower callers).
        // The scratch set covers the exact negotiated width; a contract
        // violation degrades to silence — never an allocation or OOB access.
        if (processScratch_.getNumChannels() < need || processScratch_.getNumSamples() < numSamples)
        {
            jassertfalse;   // prepare() scratch contract violated
            for (int ch = 0; ch < supplied; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        for (int ch = 0; ch < need; ++ch)
            processScratch_.clear(ch, 0, numSamples);

        // Map the caller's MAIN section into the active main input bus.
        if (mainIn >= 1)
        {
            if (mainIn == 1 && supplied >= 2)
            {
                processScratch_.addFrom(0, 0, buffer, 0, 0, numSamples, 0.5f);
                processScratch_.addFrom(0, 0, buffer, 1, 0, numSamples, 0.5f);
            }
            else
            {
                for (int ch = 0; ch < juce::jmin(mainIn, supplied); ++ch)
                    processScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            }
        }

        // NOTE: no auxiliary passthrough here — plain callers present only the
        // engine's MAIN stereo section. Combined main+aux buffers are delivered
        // exclusively through processBlockCombinedForced() (chain-built).

        juce::AudioBuffer<float> processView(processScratch_.getArrayOfWritePointers(), need, numSamples);
        logProcessBlockDiag(processView);
        if (! handleProcessResult(DAW::safelyProcessBlock(plugin_.get(), processView, midi),
                                  buffer, supplied, numSamples))
            return;

        // Map the active MAIN output bus back to the caller.
        if (mainOut == 1)
        {
            for (int ch = 0; ch < supplied; ++ch)
                buffer.copyFrom(ch, 0, processScratch_, 0, 0, numSamples);
        }
        else if (mainOut >= 2)
        {
            const int copyBack = juce::jmin(supplied, mainOut);
            for (int ch = 0; ch < copyBack; ++ch)
                buffer.copyFrom(ch, 0, processScratch_, ch, 0, numSamples);
            for (int ch = copyBack; ch < supplied; ++ch)
                buffer.clear(ch, 0, numSamples);
        }
        // mainOut == 0: nothing routable back; the scratch silence stands.
    }

    /** Chain-built combined-buffer entry (sidechain targets): the caller
        already presents the exact negotiated width including auxiliary buses.
        No main mapping is applied here. */
    void processBlockCombinedInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const int supplied   = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        lastProcessSamples_.store(numSamples, std::memory_order_relaxed);
        logProcessBlockDiag(buffer);
        handleProcessResult(DAW::safelyProcessBlock(plugin_.get(), buffer, midi),
                            buffer, supplied, numSamples);
    }

    /** Returns true when the plugin call completed; on a native crash the
        plugin is auto-bypassed and the caller buffer is cleared. */
    bool handleProcessResult(const DAW::SehGuardResult& result,
                             juce::AudioBuffer<float>& buffer,
                             int supplied, int numSamples) noexcept
    {
        if (result.succeeded || result.exceptionCode == 0)
            return true;

        bypassed_.store(true, std::memory_order_relaxed);
        juce::Logger::writeToLog("[APEX-SEH] plugin=\"" + getName()
            + "\" crashed in processBlock() code=0x"
            + juce::String::toHexString((int)result.exceptionCode)
            + " — auto-bypassing");
        for (int ch = 0; ch < supplied; ++ch)
            buffer.clear(ch, 0, numSamples);
        return false;
    }

    void logProcessBlockDiag(const juce::AudioBuffer<float>& buffer) const
    {
#if APEX_AUDIO_DEBUG_LOGS
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        void* pluginKey = (void*)plugin_.get();
        const int currentChannels = buffer.getNumChannels();
        auto it = apexDiagLastChannelCount.find(pluginKey);
        if (it == apexDiagLastChannelCount.end())
        {
            apexDiagLastChannelCount.emplace(pluginKey, currentChannels);
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << plugin_->getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
        }
        else if (it->second != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << plugin_->getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            it->second = currentChannels;
        }
#else
        (void) buffer;
#endif
    }

    std::atomic<int> prepareCount_ { 0 };
    std::atomic<int> resetCount_ { 0 };
    std::atomic<int> lastProcessSamples_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginInstanceCore)
};

} // namespace DAW

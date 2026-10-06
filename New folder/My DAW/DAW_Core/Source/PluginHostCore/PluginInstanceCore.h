#pragma once
#include <JuceHeader.h>
#include "PluginWindowScaleStore.h"
#include "../KeyBindingCore/KeyBindingManager.h"
#include "../AutomationCore/PluginAutomationGestureCore.h"
#include "../AutomationCore/PluginAutomationRecorderCore.h"
#include "../AutomationCore/LastTouchedPluginParameterCore.h"
#include "../AutomationCore/AutomationSnapshotCore.h"
#include "../Automation/AutomationParameterRegistryCore.h"
#include <unordered_map>
#include <atomic>

namespace DAW {
// Forward declarations: implemented in PluginEditorWindowWin32Patch.cpp
void patchPluginEditorWindowExStyle(juce::Component* window);
// Subclasses the HWND to intercept SC_MINIMIZE and route it to the JUCE
// component's minimiseButtonPressed() instead of letting Windows minimize
// the window into the taskbar.
void installMinimizeInterceptor(juce::Component* window, std::function<void()> onMinimize);
}

namespace DAW {

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

        PluginEditorWindow(const juce::String& name)
            : juce::DocumentWindow(name,
                                   juce::Colour(0xFF202020),
                                   juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton,
                                   true)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setOpaque(true);
            setResizable(true, false);
            setResizeLimits(kMinWindowW, kMinWindowH, kMaxSafeW, kMaxSafeH);
            juce::Logger::writeToLog("[PluginEditorWindow] ctor success windowValid=1 nativeTitleBar=1 phase="
                + juce::String(kSafeReaddPhase)
                + " buttons=minimize,maximize,close,settings");
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
            DAW::patchPluginEditorWindowExStyle(this);
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
                DAW::patchPluginEditorWindowExStyle(this);
            }
            else if (!closeRequestedByUser_ && !minimiseRequestedByUser_)
            {
                // Something other than the user hid the window — force it back.
                juce::Component::SafePointer<PluginEditorWindow> safeWin(this);
                juce::MessageManager::callAsync([safeWin]
                {
                    if (safeWin != nullptr && !safeWin->isVisible()
                        && !safeWin->closeRequestedByUser_
                        && !safeWin->minimiseRequestedByUser_)
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
                setMouseCursor(juce::MouseCursor::PointingHandCursor);
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
                    startTimerHz(30);
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

                HeaderPillButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
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
                    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
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

                MinimizeButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

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

                CloseButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

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

                FullscreenButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

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
    };

    explicit PluginInstanceCore(std::unique_ptr<juce::AudioPluginInstance> plugin)
        : plugin_(std::move(plugin))
        , lifetimeToken_(std::make_shared<int>(0))
    {
        jassert(plugin_ != nullptr);
    }

    ~PluginInstanceCore()
    {
        unregisterParameterListeners();
        lifetimeToken_.reset();
        if (plugin_)
        {
            plugin_->releaseResources();
            closeEditor();
        }
    }

    // ── Identity ─────────────────────────────────────────────────────────

    juce::String getName()        const { return plugin_ ? plugin_->getName() : ""; }
    juce::String getVendor()      const { return plugin_ ? plugin_->getPluginDescription().manufacturerName : ""; }
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
    void         setBypassed(bool b)    { bypassed_.store(b, std::memory_order_relaxed); }
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

    juce::PluginDescription getDescription() const
    {
        return plugin_ ? plugin_->getPluginDescription() : juce::PluginDescription{};
    }

    // ── Lifecycle ────────────────────────────────────────────────────────

    void prepare(double sampleRate, int blockSize)
    {
        const juce::Array<int> disabledAuxInputBuses;
        prepare(sampleRate, blockSize, disabledAuxInputBuses);
    }

    void prepare(double sampleRate, int blockSize, const juce::Array<int>& enabledAuxInputBuses)
    {
        prepareCount_.fetch_add(1, std::memory_order_relaxed);
        sampleRate_ = sampleRate;
        blockSize_  = blockSize;

        if (!plugin_) return;
        plugin_->setRateAndBufferSizeDetails(sampleRate, blockSize);

        // Negotiate bus layout (stereo main in/out, auxiliary inputs enabled only
        // for explicitly sidechained plugin instances)
        auto layout = plugin_->getBusesLayout();
        if (plugin_->getBusCount(true)  > 0) layout.getChannelSet(true,  0) = juce::AudioChannelSet::stereo();
        if (plugin_->getBusCount(false) > 0) layout.getChannelSet(false, 0) = juce::AudioChannelSet::stereo();
        for (int bus = 1; bus < plugin_->getBusCount(true); ++bus)
        {
            if (enabledAuxInputBuses.contains(bus))
            {
                juce::AudioChannelSet busLayout = juce::AudioChannelSet::stereo();
                if (auto* inputBus = plugin_->getBus(true, bus))
                {
                    if (inputBus->isEnabled() && !inputBus->getCurrentLayout().isDisabled())
                        busLayout = inputBus->getCurrentLayout();
                    else if (!inputBus->getLastEnabledLayout().isDisabled())
                        busLayout = inputBus->getLastEnabledLayout();
                    else if (!inputBus->getDefaultLayout().isDisabled())
                        busLayout = inputBus->getDefaultLayout();
                }

                if (busLayout.isDisabled() || busLayout.size() <= 0)
                    busLayout = juce::AudioChannelSet::stereo();

                layout.getChannelSet(true, bus) = busLayout;
            }
            else
            {
                layout.getChannelSet(true, bus) = juce::AudioChannelSet::disabled();
            }
        }
        for (int bus = 1; bus < plugin_->getBusCount(false); ++bus)
            layout.getChannelSet(false, bus) = juce::AudioChannelSet::disabled();
        DBG("[APEX-DIAG-SETLAYOUT] plugin=" << plugin_->getName()
            << " requestedInput=" << (layout.inputBuses.size() > 0 ? layout.inputBuses[0].getDescription() : juce::String("none"))
            << " requestedOutput=" << (layout.outputBuses.size() > 0 ? layout.outputBuses[0].getDescription() : juce::String("none"))
            << " enabledAuxInputs=" << enabledAuxInputBuses.size()
            << " calledFrom=" << __FUNCTION__);
        const bool layoutResult = plugin_->setBusesLayout(layout);
        DBG("[APEX-DIAG-SETLAYOUT-RESULT] success=" << (layoutResult ? "YES" : "NO"));
        DBG("[APEX-DIAG-POSTLAYOUT] plugin=" << plugin_->getName()
            << " totalInputChannels=" << plugin_->getTotalNumInputChannels()
            << " totalOutputChannels=" << plugin_->getTotalNumOutputChannels()
            << " numInputBuses=" << plugin_->getBusCount(true)
            << " numOutputBuses=" << plugin_->getBusCount(false));
        if (! layoutResult)
            juce::Logger::writeToLog("[EXPORT CHAIN][WARN] unsupported stereo bus layout plugin=\"" + getName() + "\"");

        DBG("[APEX-DIAG-PREPARE] plugin=" << plugin_->getName()
            << " sampleRate=" << sampleRate
            << " blockSize=" << blockSize
            << " calledFrom=" << __FUNCTION__);
        plugin_->prepareToPlay(sampleRate, blockSize);
        prepared_ = true;
        registerParameterListeners();

        // Pre-allocate scratch buffer so processBlockInternal never allocates on the audio thread.
        const int requiredChans = juce::jmax(plugin_->getTotalNumInputChannels(),
                                             plugin_->getTotalNumOutputChannels());
        if (requiredChans > 2)
            wideScratch_.setSize(requiredChans, blockSize, false, true, false);
    }

    void prepareForOffline(double sampleRate, int blockSize)
    {
        const juce::Array<int> disabledAuxInputBuses;
        prepareForOffline(sampleRate, blockSize, disabledAuxInputBuses);
    }

    void prepareForOffline(double sampleRate, int blockSize, const juce::Array<int>& enabledAuxInputBuses)
    {
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
        if (plugin_ && prepared_)
        {
            plugin_->releaseResources();
            prepared_ = false;
        }
    }

    void reset()
    {
        if (plugin_ && prepared_)
        {
            resetCount_.fetch_add(1, std::memory_order_relaxed);
            plugin_->reset();
        }
    }

    // ── Audio thread ─────────────────────────────────────────────────────

    /** Process audio in-place. Buffer must be stereo. No allocation, no locks. */
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        if (!plugin_ || !prepared_ || bypassed_.load(std::memory_order_relaxed)) return;
        processBlockInternal(buffer, midi);
    }

    void processBlockForced(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        if (!plugin_ || !prepared_) return;
        processBlockInternal(buffer, midi);
    }

    int  getLatencySamples() const { return plugin_ ? plugin_->getLatencySamples() : 0; }

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
    }

    void applyAutomationAtSample(const TrackID& trackId,
                                 int pluginSlotIndex,
                                 const AutomationSnapshot* automationSnap,
                                 int64_t samplePosition,
                                 double sampleRate = 44100.0,
                                 double bpm = 120.0) noexcept
    {
        if (plugin_ == nullptr)
            return;

        if (PluginAutomationRecorderCore::getGlobalMode() == AutomationWriteMode::Off)
            return;

        auto& params = plugin_->getParameters();
        auto& registry = apex::automation::AutomationParameterRegistry::getInstance();
        const double ppqPosition = (sampleRate > 0.0 && bpm > 0.0)
            ? ((double)samplePosition / sampleRate) * (bpm / 60.0)
            : 0.0;

        for (int i = 0; i < parameterInfos_.size(); ++i)
        {
            const auto& info = parameterInfos_.getReference(i);
            const int parameterIndex = info.parameterIndex;
            if (parameterIndex < 0 || parameterIndex >= params.size() || params[parameterIndex] == nullptr)
                continue;

            auto* pluginParam = params[parameterIndex];
            const float currentValue = pluginParam->getValue();
            float newValue = currentValue;
            bool applied = false;

            // 1. AutomationManagerCore snapshot (legacy core key)
            if (automationSnap != nullptr)
            {
                const auto coreParamId = "plugin." + juce::String(pluginSlotIndex)
                    + "." + pluginInstanceId_ + "." + info.parameterId;
                if (auto* lane = automationSnap->findLane(trackId, coreParamId))
                    if (lane->enabled)
                    {
                        newValue = lane->getValueAtSample(samplePosition, currentValue);
                        applied = true;
                    }
            }

            // 2. APEX AutomationLaneStore (APEX key: plugin.<trackID>.slot<N>.<pluginName>.<paramID>)
            // This is what A*REC writes -- must be read back here on playback.
            if (!applied)
            {
                const auto apexKey = apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
                    trackId, pluginSlotIndex, getName(), info.parameterId);
                const auto apexPid = apex::automation::AutomationParameterKeyRegistry::getInstance()
                    .findID(apexKey);
                if (apexPid != apex::automation::kInvalidParameterID)
                {
                    auto apexLane = apex::automation::AutomationLaneStore::getInstance().findLane(apexPid);
                    if (apexLane != nullptr)
                    {
                        auto snap = apexLane->getSnapshot();
                        if (snap != nullptr && !snap->empty())
                        {
                            newValue = apex::automation::AutomationLane::evaluateAt(*snap, ppqPosition);
                            applied = true;
                        }
                    }
                }
            }

            // 3. Manual lane bridge key written by AutomationLaneComponent.
            // This mirrors core/legacy plugin automation into APEX without
            // requiring the lane UI to know the plugin display name.
            if (!applied)
            {
                const auto coreParamId = "plugin." + juce::String(pluginSlotIndex)
                    + "." + pluginInstanceId_ + "." + info.parameterId;
                const auto bridgeKey = "plugin." + trackId + ".core." + coreParamId;
                const auto bridgePid = apex::automation::AutomationParameterKeyRegistry::getInstance()
                    .findID(bridgeKey);
                if (bridgePid != apex::automation::kInvalidParameterID)
                {
                    auto bridgeLane = apex::automation::AutomationLaneStore::getInstance().findLane(bridgePid);
                    if (bridgeLane != nullptr)
                    {
                        auto snap = bridgeLane->getSnapshot();
                        if (snap != nullptr && !snap->empty())
                        {
                            newValue = apex::automation::AutomationLane::evaluateAt(*snap, ppqPosition);
                            applied = true;
                        }
                    }
                }
            }

            if (applied)
            {
                const auto apexKey = apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
                    trackId, pluginSlotIndex, getName(), info.parameterId);
                const auto apexPid = apex::automation::AutomationParameterKeyRegistry::getInstance()
                    .findID(apexKey);

                if (apexPid != apex::automation::kInvalidParameterID)
                {
                    if (auto* automationParam = registry.find(apexPid))
                    {
                        automationParam->setValueFromAutomation(newValue);
                        continue;
                    }
                }

                pluginParam->setValue(newValue);
            }
        }
    }

    // ── Editor ───────────────────────────────────────────────────────────

    bool hasEditor() const { return plugin_ && plugin_->hasEditor(); }

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
                + " hasEditor=" + juce::String(plugin_ != nullptr && plugin_->hasEditor() ? 1 : 0));

        if (!plugin_ || !plugin_->hasEditor())
        {
            logOpen("failed", "reason=noPluginOrNoEditor");
            return;
        }
        if (editorWindow_)
        {
            logOpen("reuseExistingWindow", "windowValid=1");
            editorWindow_->setVisible(true);
            editorWindow_->toFront(true);
            editorWindow_->clampToScreen();
            logOpen("success", "existingWindowShown=1");
            return;
        }

        logOpen("createInstance_success", "pluginInstanceValid=1");
        logOpen("createEditor_start", "pluginInstanceValid=1");
        auto* ed = plugin_->createEditor();
        if (!ed)
        {
            logOpen("failed", "reason=createEditorReturnedNull");
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

        auto name = plugin_->getName() + " \u2014 " + desc.manufacturerName;
        logOpen("window_create_start", "safeFallback=1 name=\"" + name + "\"");
        auto window = std::make_unique<PluginEditorWindow>(name);
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
        if (! editorWindow_) return;
        DBG("[CRASH TRACE] action=plugin_editor_close plugin=" << getName());
        DBG("[PluginInstanceCore] closeEditor: destroying editor for \"" + getName()
            + "\" hadWindow=" + juce::String(1));
        editorWindow_.reset();
        if (onEditorClosed) onEditorClosed();
    }

    bool isEditorOpen() const { return editorWindow_ != nullptr && editorWindow_->isVisible(); }
    bool isEditorMinimized() const { return editorWindow_ != nullptr && !editorWindow_->isVisible(); }
    bool isEditorCreated() const { return editorWindow_ != nullptr; }

    // ── State ────────────────────────────────────────────────────────────

    juce::MemoryBlock getState() const
    {
        juce::MemoryBlock block;
        if (plugin_) plugin_->getStateInformation(block);
        return block;
    }

    void setState(const juce::MemoryBlock& block)
    {
        if (plugin_ && block.getSize() > 0)
            plugin_->setStateInformation(block.getData(), (int)block.getSize());
    }

private:
    struct ParameterInfo
    {
        int parameterIndex = -1;
        juce::String parameterId;
        juce::String name;
        int numSteps = 0;
        bool isDiscrete = false;
        bool isBoolean = false;
    };

    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    std::unique_ptr<PluginEditorWindow>         editorWindow_;
    double sampleRate_ = 44100.0;
    int    blockSize_  = 512;
    bool   prepared_   = false;
    bool   offlinePrepared_ = false;
    std::atomic<bool> bypassed_{false};
    std::shared_ptr<void> lifetimeToken_;
    juce::AudioBuffer<float> wideScratch_; // pre-allocated; resized in prepare(), never on audio thread
    juce::Array<ParameterInfo> parameterInfos_;
    juce::String pluginInstanceId_ { juce::Uuid().toString() };
    TrackID trackId_;
    int pluginSlotIndex_ = -1;
    PluginAutomationGestureCore* gestureCore_ = nullptr;
    LastTouchedPluginParameterCore* lastTouchedCore_ = nullptr;
    bool parameterListenersRegistered_ = false;
    std::atomic<float> slotMixNormalized_ { 1.0f };
    std::atomic<bool> slotMixBypass_ { false };

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

            if (trackId_.isNotEmpty() && pluginSlotIndex_ >= 0)
            {
                apex::automation::AutomationParameterKeyRegistry::getInstance().getOrCreateID(
                    apex::automation::AutomationParameterKeyRegistry::pluginParamKey(
                        trackId_, pluginSlotIndex_, getName(), info.parameterId));
            }

            p->addListener(this);
        }
        parameterListenersRegistered_ = true;
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

    // Pads the buffer to the plugin's required input channel count, processes,
    // then discards the extra channels.  This prevents jassert failures when a
    // plugin has more input buses (e.g. sidechain) than the 2-channel track
    // buffer that the engine passes in.
    // wideScratch_ is a pre-allocated member — no heap allocation on the audio thread.
    void processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const int required   = plugin_->getTotalNumInputChannels();
        const int supplied   = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        lastProcessSamples_.store(numSamples, std::memory_order_relaxed);

        if (required <= supplied)
        {
            logProcessBlockDiag(buffer);
            plugin_->processBlock(buffer, midi);
            return;
        }

        // Grow scratch only if necessary (prepare() should have sized it already).
        if (wideScratch_.getNumChannels() < required || wideScratch_.getNumSamples() < numSamples)
            wideScratch_.setSize(required, juce::jmax(numSamples, blockSize_), false, true, true);

        // Copy supplied channels in, extra channels are already silent.
        for (int ch = 0; ch < supplied; ++ch)
            wideScratch_.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        for (int ch = supplied; ch < required; ++ch)
            wideScratch_.clear(ch, 0, numSamples);

        logProcessBlockDiag(wideScratch_);
        plugin_->processBlock(wideScratch_, midi);

        // Write processed output channels back to the caller's buffer.
        const int outChans = juce::jmin(supplied, wideScratch_.getNumChannels());
        for (int ch = 0; ch < outChans; ++ch)
            buffer.copyFrom(ch, 0, wideScratch_, ch, 0, numSamples);
    }

    void logProcessBlockDiag(const juce::AudioBuffer<float>& buffer) const
    {
        static std::unordered_map<void*, int> apexDiagLastChannelCount;
        void* pluginKey = (void*)plugin_.get();
        const int currentChannels = buffer.getNumChannels();
        if (apexDiagLastChannelCount[pluginKey] != currentChannels)
        {
            DBG("[APEX-DIAG-PROCESSBLOCK] plugin=" << plugin_->getName()
                << " ptr=0x" << juce::String::toHexString((juce::pointer_sized_int)pluginKey)
                << " bufferChannels=" << currentChannels
                << " bufferSamples=" << buffer.getNumSamples());
            apexDiagLastChannelCount[pluginKey] = currentChannels;
        }
    }

    std::atomic<int> prepareCount_ { 0 };
    std::atomic<int> resetCount_ { 0 };
    std::atomic<int> lastProcessSamples_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginInstanceCore)
};

} // namespace DAW

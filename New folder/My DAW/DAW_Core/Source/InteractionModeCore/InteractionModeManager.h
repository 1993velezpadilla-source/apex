#pragma once
#include <JuceHeader.h>

namespace DAW {

// Interaction modes inspired by your Android engine
// Keep Hybrid for compatibility with SettingsPanel / VirtualCursor.
enum class InteractionMode
{
    Touchscreen,
    Mouse,
    Hybrid
};

struct InteractionModeConfig
{
    InteractionMode mode = InteractionMode::Mouse;

    bool longPressEnabled = true;
    int longPressDelayMs = 400;
    bool twoFingerScrollEnabled = true;
    bool pinchZoomEnabled = true;

    bool rightClickEnabled = true;
    bool middleClickScrollEnabled = true;
    bool scrollWheelZoomEnabled = true;

    bool virtualCursorEnabled = true;
    bool trackpadVisible = true;
    float cursorSensitivity = 1.35f;

    bool windowDragFromTitlebar = true;
    bool windowResizeFromCorner = true;
    bool windowSnapEnabled = true;
    bool windowMinimizeEnabled = true;

    int dragStartThresholdPx = 8;
    int doubleTapMaxDelayMs = 300;
    int hoverDelayMs = 250;
};

class InteractionModeManager
{
public:
    InteractionModeManager();
    ~InteractionModeManager();

    void setMode(InteractionMode newMode);
    InteractionMode getMode() const { return config_.mode; }

    InteractionModeConfig& getConfig() { return config_; }
    const InteractionModeConfig& getConfig() const { return config_; }

    void detectAndSwitchMode();
    bool isTouchscreenAvailable() const;
    bool isMouseAvailable() const;

    bool shouldShowVirtualCursor() const;
    bool shouldShowTrackpad() const;
    bool shouldUseGestures() const;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void interactionModeChanged(InteractionMode newMode) {}
        virtual void configChanged() {}
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    InteractionModeConfig config_;
    juce::ListenerList<Listener> listeners_;

    void notifyModeChanged();
    void notifyConfigChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InteractionModeManager)
};

} // namespace DAW

#pragma once
#include <JuceHeader.h>

namespace DAW {

// Forward declare Windows types to avoid including Windows.h in header
#if JUCE_WINDOWS
  // XInput will be included in .cpp file
#endif

// Gamepad button mapping
enum class GamepadButton
{
    // Face buttons
    A, B, X, Y,
    
    // Bumpers
    LeftBumper, RightBumper,
    
    // D-pad
    DPadUp, DPadDown, DPadLeft, DPadRight,
    
    // System buttons
    Start, Back,
    
    // Stick buttons
    LeftStick, RightStick,
    
    // Trigger buttons (digital)
    LeftTrigger, RightTrigger,
    
    Count
};

// Gamepad axis mapping
enum class GamepadAxis
{
    LeftStickX,
    LeftStickY,
    RightStickX,
    RightStickY,
    LeftTrigger,
    RightTrigger,
    
    Count
};

// Gamepad state snapshot
struct GamepadState
{
    bool connected = false;
    int controllerId = -1;
    
    // Buttons (pressed this frame)
    bool buttons[(int)GamepadButton::Count] = {false};
    
    // Button states (for detecting press/release)
    bool buttonsPrev[(int)GamepadButton::Count] = {false};
    
    // Analog axes (-1.0 to 1.0, except triggers: 0.0 to 1.0)
    float axes[(int)GamepadAxis::Count] = {0.0f};
    
    // Vibration
    float leftMotor = 0.0f;
    float rightMotor = 0.0f;
    
    // Helpers
    bool isButtonPressed(GamepadButton btn) const 
    { 
        return buttons[(int)btn] && !buttonsPrev[(int)btn]; 
    }
    
    bool isButtonReleased(GamepadButton btn) const 
    { 
        return !buttons[(int)btn] && buttonsPrev[(int)btn]; 
    }
    
    bool isButtonDown(GamepadButton btn) const 
    { 
        return buttons[(int)btn]; 
    }
    
    float getAxis(GamepadAxis axis) const 
    { 
        return axes[(int)axis]; 
    }
};

// Gamepad manager - polls gamepads using XInput
class GamepadManager : public juce::Timer
{
public:
    GamepadManager();
    ~GamepadManager();
    
    // Enable/disable gamepad support
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const { return enabled_; }
    
    // Get gamepad states
    const GamepadState& getGamepad(int index) const;
    int getNumConnectedGamepads() const;
    
    // Vibration
    void setVibration(int controllerIndex, float leftMotor, float rightMotor);
    
    // Dead zone settings
    void setStickDeadZone(float deadZone) { stickDeadZone_ = deadZone; }
    void setTriggerDeadZone(float deadZone) { triggerDeadZone_ = deadZone; }
    
    // Polling rate
    void setPollingRate(int hz);
    
    // Listeners
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void gamepadConnected(int controllerIndex) {}
        virtual void gamepadDisconnected(int controllerIndex) {}
        virtual void gamepadButtonPressed(int controllerIndex, GamepadButton button) {}
        virtual void gamepadButtonReleased(int controllerIndex, GamepadButton button) {}
        virtual void gamepadAxisMoved(int controllerIndex, GamepadAxis axis, float value) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    
private:
    static constexpr int MAX_CONTROLLERS = 4; // XInput supports up to 4
    
    bool enabled_ = false;
    GamepadState gamepads_[MAX_CONTROLLERS];
    
    float stickDeadZone_ = 0.15f;
    float triggerDeadZone_ = 0.1f;
    
    juce::ListenerList<Listener> listeners_;
    
    // Timer callback - polls gamepads
    void timerCallback() override;
    
    void pollGamepad(int index);
    void updateGamepadState(int index);
    void notifyButtonEvents(int index);
    void notifyAxisEvents(int index);
    
    float applyDeadZone(float value, float deadZone);
    
#if JUCE_WINDOWS
    void pollXInput(int index);
#endif
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GamepadManager)
};

// Helper to get button name
inline juce::String getGamepadButtonName(GamepadButton btn)
{
    switch (btn)
    {
        case GamepadButton::A: return "A";
        case GamepadButton::B: return "B";
        case GamepadButton::X: return "X";
        case GamepadButton::Y: return "Y";
        case GamepadButton::LeftBumper: return "LB";
        case GamepadButton::RightBumper: return "RB";
        case GamepadButton::DPadUp: return "D-Up";
        case GamepadButton::DPadDown: return "D-Down";
        case GamepadButton::DPadLeft: return "D-Left";
        case GamepadButton::DPadRight: return "D-Right";
        case GamepadButton::Start: return "Start";
        case GamepadButton::Back: return "Back";
        case GamepadButton::LeftStick: return "L3";
        case GamepadButton::RightStick: return "R3";
        case GamepadButton::LeftTrigger: return "LT";
        case GamepadButton::RightTrigger: return "RT";
        default: return "Unknown";
    }
}

inline juce::String getGamepadAxisName(GamepadAxis axis)
{
    switch (axis)
    {
        case GamepadAxis::LeftStickX: return "Left Stick X";
        case GamepadAxis::LeftStickY: return "Left Stick Y";
        case GamepadAxis::RightStickX: return "Right Stick X";
        case GamepadAxis::RightStickY: return "Right Stick Y";
        case GamepadAxis::LeftTrigger: return "Left Trigger";
        case GamepadAxis::RightTrigger: return "Right Trigger";
        default: return "Unknown";
    }
}

} // namespace DAW

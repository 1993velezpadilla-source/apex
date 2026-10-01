#include "GamepadManager.h"

#if JUCE_WINDOWS
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <Xinput.h>
  #pragma comment(lib, "xinput.lib")
#endif

namespace DAW {

GamepadManager::GamepadManager()
{
    // Initialize all gamepads as disconnected
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
    {
        gamepads_[i].connected = false;
        gamepads_[i].controllerId = i;
    }
}

GamepadManager::~GamepadManager()
{
    stopTimer();
}

void GamepadManager::setEnabled(bool shouldBeEnabled)
{
    if (enabled_ == shouldBeEnabled)
        return;
    
    enabled_ = shouldBeEnabled;
    
    if (enabled_)
    {
        DBG("GamepadManager: Enabled - Starting polling at 60Hz");
        setPollingRate(60); // Default 60Hz
    }
    else
    {
        DBG("GamepadManager: Disabled - Stopping polling");
        stopTimer();
        
        // Disconnect all gamepads
        for (int i = 0; i < MAX_CONTROLLERS; ++i)
        {
            if (gamepads_[i].connected)
            {
                gamepads_[i].connected = false;
                listeners_.call([i](Listener& l) { l.gamepadDisconnected(i); });
            }
        }
    }
}

const GamepadState& GamepadManager::getGamepad(int index) const
{
    jassert(index >= 0 && index < MAX_CONTROLLERS);
    return gamepads_[index];
}

int GamepadManager::getNumConnectedGamepads() const
{
    int count = 0;
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
    {
        if (gamepads_[i].connected)
            count++;
    }
    return count;
}

void GamepadManager::setVibration(int controllerIndex, float leftMotor, float rightMotor)
{
    if (controllerIndex < 0 || controllerIndex >= MAX_CONTROLLERS)
        return;
    
    gamepads_[controllerIndex].leftMotor = juce::jlimit(0.0f, 1.0f, leftMotor);
    gamepads_[controllerIndex].rightMotor = juce::jlimit(0.0f, 1.0f, rightMotor);
    
#if JUCE_WINDOWS
    XINPUT_VIBRATION vibration;
    juce::zerostruct(vibration);
    vibration.wLeftMotorSpeed = (WORD)(gamepads_[controllerIndex].leftMotor * 65535.0f);
    vibration.wRightMotorSpeed = (WORD)(gamepads_[controllerIndex].rightMotor * 65535.0f);
    XInputSetState(controllerIndex, &vibration);
#endif
}

void GamepadManager::setPollingRate(int hz)
{
    if (hz <= 0)
        hz = 60;
    
    int intervalMs = 1000 / hz;
    startTimer(intervalMs);
}

void GamepadManager::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void GamepadManager::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void GamepadManager::timerCallback()
{
    if (!enabled_)
        return;
    
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
    {
        pollGamepad(i);
    }
}

void GamepadManager::pollGamepad(int index)
{
#if JUCE_WINDOWS
    pollXInput(index);
#else
    // On non-Windows platforms, gamepad support would need DirectInput or other API
    gamepads_[index].connected = false;
#endif
}

#if JUCE_WINDOWS
void GamepadManager::pollXInput(int index)
{
    XINPUT_STATE state;
    juce::zerostruct(state);
    
    DWORD result = XInputGetState(index, &state);
    bool wasConnected = gamepads_[index].connected;
    bool isConnected = (result == ERROR_SUCCESS);
    
    if (isConnected != wasConnected)
    {
        gamepads_[index].connected = isConnected;
        
        if (isConnected)
        {
            DBG("GamepadManager: Controller " + juce::String(index) + " connected");
            listeners_.call([index](Listener& l) { l.gamepadConnected(index); });
        }
        else
        {
            DBG("GamepadManager: Controller " + juce::String(index) + " disconnected");
            listeners_.call([index](Listener& l) { l.gamepadDisconnected(index); });
        }
    }
    
    if (!isConnected)
        return;
    
    updateGamepadState(index);
    
    // Map XInput buttons to our enum
    auto& gp = gamepads_[index];
    auto& pad = state.Gamepad;
    
    // Copy previous state
    for (int b = 0; b < (int)GamepadButton::Count; ++b)
        gp.buttonsPrev[b] = gp.buttons[b];
    
    // Read buttons
    gp.buttons[(int)GamepadButton::A] = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
    gp.buttons[(int)GamepadButton::B] = (pad.wButtons & XINPUT_GAMEPAD_B) != 0;
    gp.buttons[(int)GamepadButton::X] = (pad.wButtons & XINPUT_GAMEPAD_X) != 0;
    gp.buttons[(int)GamepadButton::Y] = (pad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    
    gp.buttons[(int)GamepadButton::LeftBumper] = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    gp.buttons[(int)GamepadButton::RightBumper] = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    
    gp.buttons[(int)GamepadButton::DPadUp] = (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    gp.buttons[(int)GamepadButton::DPadDown] = (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    gp.buttons[(int)GamepadButton::DPadLeft] = (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    gp.buttons[(int)GamepadButton::DPadRight] = (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    
    gp.buttons[(int)GamepadButton::Start] = (pad.wButtons & XINPUT_GAMEPAD_START) != 0;
    gp.buttons[(int)GamepadButton::Back] = (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    
    gp.buttons[(int)GamepadButton::LeftStick] = (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    gp.buttons[(int)GamepadButton::RightStick] = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    
    // Read analog sticks (normalize to -1.0 to 1.0)
    float lx = pad.sThumbLX / 32768.0f;
    float ly = pad.sThumbLY / 32768.0f;
    float rx = pad.sThumbRX / 32768.0f;
    float ry = pad.sThumbRY / 32768.0f;
    
    gp.axes[(int)GamepadAxis::LeftStickX] = applyDeadZone(lx, stickDeadZone_);
    gp.axes[(int)GamepadAxis::LeftStickY] = applyDeadZone(ly, stickDeadZone_);
    gp.axes[(int)GamepadAxis::RightStickX] = applyDeadZone(rx, stickDeadZone_);
    gp.axes[(int)GamepadAxis::RightStickY] = applyDeadZone(ry, stickDeadZone_);
    
    // Read triggers (normalize to 0.0 to 1.0)
    float lt = pad.bLeftTrigger / 255.0f;
    float rt = pad.bRightTrigger / 255.0f;
    
    gp.axes[(int)GamepadAxis::LeftTrigger] = applyDeadZone(lt, triggerDeadZone_);
    gp.axes[(int)GamepadAxis::RightTrigger] = applyDeadZone(rt, triggerDeadZone_);
    
    // Digital trigger buttons (pressed past threshold)
    gp.buttons[(int)GamepadButton::LeftTrigger] = (lt > 0.5f);
    gp.buttons[(int)GamepadButton::RightTrigger] = (rt > 0.5f);
    
    notifyButtonEvents(index);
    notifyAxisEvents(index);
}
#endif

void GamepadManager::updateGamepadState(int index)
{
    // State is updated in pollXInput
}

void GamepadManager::notifyButtonEvents(int index)
{
    auto& gp = gamepads_[index];
    
    for (int b = 0; b < (int)GamepadButton::Count; ++b)
    {
        GamepadButton btn = (GamepadButton)b;
        
        if (gp.isButtonPressed(btn))
        {
            listeners_.call([index, btn](Listener& l) 
            { 
                l.gamepadButtonPressed(index, btn); 
            });
        }
        
        if (gp.isButtonReleased(btn))
        {
            listeners_.call([index, btn](Listener& l) 
            { 
                l.gamepadButtonReleased(index, btn); 
            });
        }
    }
}

void GamepadManager::notifyAxisEvents(int index)
{
    auto& gp = gamepads_[index];
    
    for (int a = 0; a < (int)GamepadAxis::Count; ++a)
    {
        GamepadAxis axis = (GamepadAxis)a;
        float value = gp.getAxis(axis);
        
        // Only notify if axis moved significantly
        if (std::abs(value) > 0.01f)
        {
            listeners_.call([index, axis, value](Listener& l) 
            { 
                l.gamepadAxisMoved(index, axis, value); 
            });
        }
    }
}

float GamepadManager::applyDeadZone(float value, float deadZone)
{
    if (std::abs(value) < deadZone)
        return 0.0f;
    
    // Rescale to maintain smooth transition
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    float absValue = std::abs(value);
    float adjusted = (absValue - deadZone) / (1.0f - deadZone);
    
    return sign * juce::jlimit(0.0f, 1.0f, adjusted);
}

} // namespace DAW

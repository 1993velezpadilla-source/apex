#include "GamepadMapper.h"
#include "../ActionCore/ActionManager.h"

namespace DAW {

GamepadMapper::GamepadMapper(GamepadManager& gamepadManager)
    : gamepadManager_(gamepadManager)
{
    mapping_ = GamepadMapping::createDefault();
}

GamepadMapper::~GamepadMapper()
{
    setEnabled(false);
}

void GamepadMapper::setEnabled(bool shouldBeEnabled)
{
    if (enabled_ == shouldBeEnabled)
        return;

    enabled_ = shouldBeEnabled;

    if (enabled_)
    {
        DBG("GamepadMapper: Enabled - Listening to gamepad events");
        gamepadManager_.addListener(this);
    }
    else
    {
        DBG("GamepadMapper: Disabled - Stopped listening");
        gamepadManager_.removeListener(this);
    }
}

void GamepadMapper::setMapping(const GamepadMapping& mapping)
{
    mapping_ = mapping;
    DBG("GamepadMapper: Mapping updated");
}

void GamepadMapper::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void GamepadMapper::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void GamepadMapper::gamepadConnected(int controllerIndex)
{
    DBG("GamepadMapper: Controller " + juce::String(controllerIndex) + " connected");
}

void GamepadMapper::gamepadDisconnected(int controllerIndex)
{
    DBG("GamepadMapper: Controller " + juce::String(controllerIndex) + " disconnected");
}

void GamepadMapper::gamepadButtonPressed(int controllerIndex, GamepadButton button)
{
    if (!enabled_ || controllerIndex != 0)
        return;

    handleButtonAction(button);
}

void GamepadMapper::gamepadButtonReleased(int, GamepadButton)
{
}

void GamepadMapper::gamepadAxisMoved(int controllerIndex, GamepadAxis axis, float value)
{
    if (!enabled_ || controllerIndex != 0)
        return;

    handleAxisAction(axis, value);
}

void GamepadMapper::handleButtonAction(GamepadButton button)
{
    auto it = mapping_.buttonActions.find(button);
    if (it != mapping_.buttonActions.end())
    {
        DBG("GamepadMapper: Button " + getGamepadButtonName(button) + " -> dispatch");
        ActionManager::getInstance().dispatch(it->second, 1.0f);
    }
}

void GamepadMapper::handleAxisAction(GamepadAxis axis, float value)
{
    auto it = mapping_.axisActions.find(axis);
    if (it == mapping_.axisActions.end())
        return;

    ActionID action = it->second;
    float adjustedValue = value;

    // Apply sensitivity based on action type
    switch (action)
    {
        case ActionID::TrackVolumeUp:
        case ActionID::TrackVolumeDown:
            adjustedValue *= mapping_.volumeSensitivity;
            break;
        case ActionID::TrackPanLeft:
        case ActionID::TrackPanRight:
            adjustedValue *= mapping_.panSensitivity;
            break;
        case ActionID::ScrollUp:
        case ActionID::ScrollDown:
        case ActionID::ScrollLeft:
        case ActionID::ScrollRight:
            adjustedValue *= mapping_.scrollSensitivity;
            break;
        default:
            break;
    }

    ActionManager::getInstance().dispatch(action, adjustedValue);
}

} // namespace DAW

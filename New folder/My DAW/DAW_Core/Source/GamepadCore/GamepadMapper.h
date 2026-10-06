#pragma once
#include <JuceHeader.h>
#include "GamepadManager.h"
#include "../ActionCore/ActionID.h"

namespace DAW {

// Gamepad action mapping configuration — maps directly to canonical ActionIDs
struct GamepadMapping
{
    // Button mappings
    std::map<GamepadButton, ActionID> buttonActions;

    // Axis mappings
    std::map<GamepadAxis, ActionID> axisActions;

    // Sensitivity for analog controls
    float volumeSensitivity = 1.0f;
    float panSensitivity = 1.0f;
    float scrollSensitivity = 20.0f;
    float zoomSensitivity = 0.5f;

    // Default Legion Go / Xbox-style mapping
    static GamepadMapping createDefault()
    {
        GamepadMapping map;

        // Face buttons
        map.buttonActions[GamepadButton::A] = ActionID::TransportPlayStop;
        map.buttonActions[GamepadButton::B] = ActionID::TransportStop;
        map.buttonActions[GamepadButton::Y] = ActionID::TransportRecord;
        map.buttonActions[GamepadButton::X] = ActionID::TransportToggleLoop;

        // Bumpers
        map.buttonActions[GamepadButton::LeftBumper] = ActionID::TrackPrevious;
        map.buttonActions[GamepadButton::RightBumper] = ActionID::TrackNext;

        // D-pad
        map.buttonActions[GamepadButton::DPadUp] = ActionID::ScrollUp;
        map.buttonActions[GamepadButton::DPadDown] = ActionID::ScrollDown;
        map.buttonActions[GamepadButton::DPadLeft] = ActionID::ScrollLeft;
        map.buttonActions[GamepadButton::DPadRight] = ActionID::ScrollRight;

        // System buttons
        map.buttonActions[GamepadButton::Start] = ActionID::ViewToggleMixer;
        map.buttonActions[GamepadButton::Back] = ActionID::ViewToggleSettings;

        // Stick buttons
        map.buttonActions[GamepadButton::LeftStick] = ActionID::TrackMuteSelected;
        map.buttonActions[GamepadButton::RightStick] = ActionID::TrackSoloSelected;

        // Analog mappings
        map.axisActions[GamepadAxis::LeftTrigger] = ActionID::TrackVolumeUp;
        map.axisActions[GamepadAxis::RightTrigger] = ActionID::TrackVolumeDown;
        map.axisActions[GamepadAxis::RightStickX] = ActionID::TrackPanRight;
        map.axisActions[GamepadAxis::LeftStickY] = ActionID::ScrollUp;

        return map;
    }
};

// Gamepad mapper — connects gamepad to DAW actions via ActionManager
class GamepadMapper : public GamepadManager::Listener
{
public:
    GamepadMapper(GamepadManager& gamepadManager);
    ~GamepadMapper();

    // Enable/disable mapping
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const { return enabled_; }

    // Mapping configuration
    void setMapping(const GamepadMapping& mapping);
    const GamepadMapping& getMapping() const { return mapping_; }

    // Listeners for custom actions
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void customActionTriggered(ActionID action, float value) {}
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    GamepadManager& gamepadManager_;
    bool enabled_ = false;

    GamepadMapping mapping_;

    juce::ListenerList<Listener> listeners_;

    // GamepadManager::Listener implementation
    void gamepadConnected(int controllerIndex) override;
    void gamepadDisconnected(int controllerIndex) override;
    void gamepadButtonPressed(int controllerIndex, GamepadButton button) override;
    void gamepadButtonReleased(int controllerIndex, GamepadButton button) override;
    void gamepadAxisMoved(int controllerIndex, GamepadAxis axis, float value) override;

    void handleButtonAction(GamepadButton button);
    void handleAxisAction(GamepadAxis axis, float value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GamepadMapper)
};

} // namespace DAW

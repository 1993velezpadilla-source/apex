#include "InteractionModeManager.h"

namespace DAW {

InteractionModeManager::InteractionModeManager()
{
    detectAndSwitchMode();
}

InteractionModeManager::~InteractionModeManager()
{
}

void InteractionModeManager::setMode(InteractionMode newMode)
{
    if (config_.mode != newMode)
    {
        config_.mode = newMode;

        DBG("InteractionModeManager: Mode changed to "
            + juce::String((int) newMode)
            + " ("
            + (newMode == InteractionMode::Touchscreen ? "TOUCHSCREEN"
               : newMode == InteractionMode::Mouse ? "MOUSE"
                                                   : "HYBRID")
            + ")");

        notifyModeChanged();
    }
}

void InteractionModeManager::detectAndSwitchMode()
{
    auto& desktop = juce::Desktop::getInstance();
    bool hasTouch = desktop.getNumMouseSources() > 1;

   #if JUCE_WINDOWS || JUCE_MAC || JUCE_LINUX
    if (hasTouch)
        setMode(InteractionMode::Hybrid);
    else
        setMode(InteractionMode::Mouse);
   #elif JUCE_IOS || JUCE_ANDROID
    setMode(InteractionMode::Touchscreen);
   #else
    setMode(InteractionMode::Mouse);
   #endif
}

bool InteractionModeManager::isTouchscreenAvailable() const
{
    auto& desktop = juce::Desktop::getInstance();
    return desktop.getNumMouseSources() > 1;
}

bool InteractionModeManager::isMouseAvailable() const
{
    auto& desktop = juce::Desktop::getInstance();
    return desktop.getNumMouseSources() >= 1;
}

bool InteractionModeManager::shouldShowVirtualCursor() const
{
    return config_.mode == InteractionMode::Hybrid && config_.virtualCursorEnabled;
}

bool InteractionModeManager::shouldShowTrackpad() const
{
    return config_.mode == InteractionMode::Hybrid && config_.trackpadVisible;
}

bool InteractionModeManager::shouldUseGestures() const
{
    return config_.mode == InteractionMode::Touchscreen
        || config_.mode == InteractionMode::Hybrid;
}

void InteractionModeManager::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void InteractionModeManager::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void InteractionModeManager::notifyModeChanged()
{
    listeners_.call([this](Listener& l) { l.interactionModeChanged(config_.mode); });
    notifyConfigChanged();
}

void InteractionModeManager::notifyConfigChanged()
{
    listeners_.call([](Listener& l) { l.configChanged(); });
}

} // namespace DAW

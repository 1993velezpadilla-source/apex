#pragma once

#include <JuceHeader.h>

namespace DAW {

/**
 * Pure keyboard policy for the Mixer/Arrange boundary.
 *
 * Ctrl+arrow is reserved for the Arrange/Timeline zoom axes.  Plain arrows
 * retain their Mixer selection behavior, but only while the Mixer owns focus.
 * Keeping this decision separate from MainComponent makes the modifier and
 * focus contract directly unit-testable without constructing the application.
 */
enum class MixerKeyboardAction
{
    None,
    SelectPrevious,
    SelectNext,
    ZoomHorizontalOut,
    ZoomHorizontalIn,
    ZoomVerticalIn,
    ZoomVerticalOut
};

struct MixerKeyboardRoutingCore final
{
    static MixerKeyboardAction resolve(const juce::KeyPress& key,
                                       bool mixerOpen,
                                       bool mixerHasFocus) noexcept
    {
        const int code = key.getKeyCode();
        const bool isLeft  = code == juce::KeyPress::leftKey;
        const bool isRight = code == juce::KeyPress::rightKey;
        const bool isUp    = code == juce::KeyPress::upKey;
        const bool isDown  = code == juce::KeyPress::downKey;

        if (!isLeft && !isRight && !isUp && !isDown)
            return MixerKeyboardAction::None;

        // This branch intentionally precedes Mixer focus/navigation.  Ctrl+
        // arrows must never be consumed as Mixer selection or scrolling.
        if (key.getModifiers().isCtrlDown())
        {
            if (isLeft)  return MixerKeyboardAction::ZoomHorizontalOut;
            if (isRight) return MixerKeyboardAction::ZoomHorizontalIn;
            if (isUp)    return MixerKeyboardAction::ZoomVerticalIn;
            return MixerKeyboardAction::ZoomVerticalOut;
        }

        if (!mixerOpen || !mixerHasFocus)
            return MixerKeyboardAction::None;

        return (isLeft || isUp) ? MixerKeyboardAction::SelectPrevious
                                : MixerKeyboardAction::SelectNext;
    }
};

} // namespace DAW

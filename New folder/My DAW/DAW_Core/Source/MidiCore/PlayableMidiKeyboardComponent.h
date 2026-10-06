#pragma once

#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "VirtualMidiKeyboardCore.h"

namespace DAW {

namespace PlayableMidiKeyboardDetail
{
constexpr int pointerFallbackId = 1;

inline int getPointerIdFromEvent(const juce::MouseEvent& e)
{
    return e.source.isTouch() ? e.source.getIndex() : pointerFallbackId;
}
}

class PlayableMidiKeyboardComponent : public juce::Component,
                                      private juce::Timer
{
public:
    PlayableMidiKeyboardComponent() = default;

    void setKeyboardCore(VirtualMidiKeyboardCore* core) noexcept
    {
        keyboardCore_ = core;
        if (keyboardCore_ != nullptr)
            startTimerHz(30);
        else
            stopTimer();
        repaint();
    }

    void setLayout(VirtualMidiKeyboardCore::Layout layout)
    {
        if (keyboardCore_ != nullptr)
            keyboardCore_->setLayout(layout);
        repaint();
    }

    void setBaseOctave(int octave)
    {
        if (keyboardCore_ != nullptr)
            keyboardCore_->setBaseOctave(octave);
        repaint();
    }

    void setVelocityMode(VirtualMidiKeyboardCore::VelocityMode mode)
    {
        if (keyboardCore_ != nullptr)
            keyboardCore_->setVelocityMode(mode);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto bounds = getLocalBounds();

        g.setColour(theme.colors.graphite.darker(0.15f));
        g.fillRoundedRectangle(bounds.toFloat(), 8.0f);

        if (keyboardCore_ == nullptr)
        {
            g.setColour(theme.colors.textSecondary);
            g.setFont(theme.fonts.small);
            g.drawFittedText("No keyboard core", bounds, juce::Justification::centred, 1);
            return;
        }

        const auto& pads = keyboardCore_->getPads();
        for (int i = 0; i < pads.size(); ++i)
        {
            const auto& pad = pads.getReference(i);
            if (pad.isAccidental)
                continue;

            auto keyBounds = getPadBounds(pad);
            auto keyColour = isPadActive(i) ? theme.colors.accent.withAlpha(0.72f)
                                            : theme.colors.pearl.withAlpha(0.96f);
            g.setColour(keyColour);
            g.fillRoundedRectangle(keyBounds.reduced(0.5f, 1.0f), 4.0f);

            g.setColour(theme.colors.separatorHard.withAlpha(0.55f));
            g.drawRoundedRectangle(keyBounds.reduced(0.5f, 1.0f), 4.0f, 1.0f);

            if (pad.label.isNotEmpty() && !pad.isAccidental)
            {
                g.setColour(theme.colors.textSecondary);
                g.setFont(theme.fonts.small);
                g.drawText(pad.label + pad.secondaryLabel,
                           keyBounds.toNearestInt().removeFromBottom(18),
                           juce::Justification::centred,
                           false);
            }
        }

        for (int i = 0; i < pads.size(); ++i)
        {
            const auto& pad = pads.getReference(i);
            if (!pad.isAccidental)
                continue;

            auto keyBounds = getPadBounds(pad);
            auto keyColour = isPadActive(i) ? theme.colors.accentActive.withAlpha(0.95f)
                                            : theme.colors.obsidian.brighter(0.12f);
            g.setColour(keyColour);
            g.fillRoundedRectangle(keyBounds, 4.0f);

            g.setColour(theme.colors.separatorHard.withAlpha(0.8f));
            g.drawRoundedRectangle(keyBounds, 4.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override { updatePointerNote(e); }
    void mouseDrag(const juce::MouseEvent& e) override { updatePointerNote(e); }
    void mouseUp(const juce::MouseEvent& e) override { releasePointer(PlayableMidiKeyboardDetail::getPointerIdFromEvent(e)); }
    void mouseExit(const juce::MouseEvent& e) override
    {
        if (!e.mods.isAnyMouseButtonDown())
            releasePointer(PlayableMidiKeyboardDetail::getPointerIdFromEvent(e));
    }

private:
    juce::Rectangle<float> getPadBounds(const VirtualMidiKeyboardCore::Pad& pad) const
    {
        const auto area = getLocalBounds().toFloat();
        const auto layout = keyboardCore_ != nullptr ? keyboardCore_->getLayout() : VirtualMidiKeyboardCore::Layout::PianoStandard;

        if (layout != VirtualMidiKeyboardCore::Layout::PianoStandard)
        {
            const float cols = (float) juce::jmax(1, keyboardCore_->getColumnCount());
            const float rows = (float) juce::jmax(1, keyboardCore_->getRowCount());
            return {
                area.getX() + ((float) pad.column * area.getWidth() / cols),
                area.getY() + ((float) pad.row * area.getHeight() / rows),
                area.getWidth() / cols,
                area.getHeight() / rows
            };
        }

        const float whiteKeyWidth = area.getWidth() / (float) getWhiteKeyCount();
        const float x = (float) getWhiteKeyIndexForPad(pad) * whiteKeyWidth;
        if (!pad.isAccidental)
            return { x, 0.0f, whiteKeyWidth, area.getHeight() };

        const float blackKeyWidth = whiteKeyWidth * 0.64f;
        const float blackKeyHeight = area.getHeight() * 0.62f;
        return { x - (blackKeyWidth * 0.32f), 0.0f, blackKeyWidth, blackKeyHeight };
    }

    int getWhiteKeyCount() const noexcept
    {
        int count = 0;
        if (keyboardCore_ == nullptr)
            return 1;

        for (const auto& pad : keyboardCore_->getPads())
            if (!pad.isAccidental)
                ++count;

        return juce::jmax(1, count);
    }

    int getWhiteKeyIndexForPad(const VirtualMidiKeyboardCore::Pad& targetPad) const noexcept
    {
        int index = 0;
        if (keyboardCore_ == nullptr)
            return index;

        for (const auto& pad : keyboardCore_->getPads())
        {
            if (pad.midiNote == targetPad.midiNote)
                break;

            if (!pad.isAccidental)
                ++index;
        }

        return index;
    }

    int hitTestPadIndex(juce::Point<float> position) const
    {
        if (keyboardCore_ == nullptr)
            return -1;

        const auto& pads = keyboardCore_->getPads();

        for (int i = 0; i < pads.size(); ++i)
            if (pads.getReference(i).isAccidental && getPadBounds(pads.getReference(i)).contains(position))
                return i;

        for (int i = 0; i < pads.size(); ++i)
            if (!pads.getReference(i).isAccidental && getPadBounds(pads.getReference(i)).contains(position))
                return i;

        return -1;
    }

    bool isPadActive(int padIndex) const
    {
        if (keyboardCore_ == nullptr || !juce::isPositiveAndBelow(padIndex, keyboardCore_->getPads().size()))
            return false;

        return keyboardCore_->getState().isNoteOnForChannels(0xffff, keyboardCore_->getPads().getReference(padIndex).midiNote);
    }

    void updatePointerNote(const juce::MouseEvent& e)
    {
        if (keyboardCore_ == nullptr)
            return;

        const int pointerId = PlayableMidiKeyboardDetail::getPointerIdFromEvent(e);
        const int newPadIndex = hitTestPadIndex(e.position);
        const auto it = activePointerPads_.find(pointerId);
        const int previousPadIndex = it != activePointerPads_.end() ? it->second : -1;

        if (previousPadIndex == newPadIndex)
            return;

        if (previousPadIndex >= 0)
            keyboardCore_->releasePad(pointerId);

        if (newPadIndex >= 0)
        {
            const auto keyBounds = getPadBounds(keyboardCore_->getPads().getReference(newPadIndex));
            const float yWithinPad = keyBounds.getHeight() > 0.0f
                ? juce::jlimit(0.0f, 1.0f, (e.position.y - keyBounds.getY()) / keyBounds.getHeight())
                : 1.0f;
            keyboardCore_->touchPad(newPadIndex, yWithinPad, e.pressure, pointerId);
            activePointerPads_[pointerId] = newPadIndex;
        }
        else
        {
            activePointerPads_.erase(pointerId);
        }

        repaint();
    }

    void releasePointer(int pointerId)
    {
        if (keyboardCore_ == nullptr)
            return;

        const auto it = activePointerPads_.find(pointerId);
        if (it == activePointerPads_.end())
            return;

        keyboardCore_->releasePad(pointerId);
        activePointerPads_.erase(it);
        repaint();
    }

    void timerCallback() override { repaint(); }

    VirtualMidiKeyboardCore* keyboardCore_ = nullptr;
    std::unordered_map<int, int> activePointerPads_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayableMidiKeyboardComponent)
};

} // namespace DAW

#include "PlayableMidiKeyboardComponent.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

namespace
{
constexpr int kPointerFallbackId = 1;

int getPointerIdFromEvent(const juce::MouseEvent& e)
{
    return e.source.isTouch() ? e.source.getIndex() : kPointerFallbackId;
}
}

bool PlayableMidiKeyboardComponent::isBlackKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

void PlayableMidiKeyboardComponent::setKeyboardState(juce::MidiKeyboardState* state) noexcept
{
    keyboardState_ = state;
    if (keyboardState_ != nullptr)
        startTimerHz(60);   // 60 Hz for smooth key visual feedback
    else
        stopTimer();
    repaint();
}

void PlayableMidiKeyboardComponent::setAvailableRange(int lowMidiNote, int highMidiNote)
{
    lowMidiNote_ = juce::jlimit(0, 127, juce::jmin(lowMidiNote, highMidiNote));
    highMidiNote_ = juce::jlimit(lowMidiNote_, 127, juce::jmax(lowMidiNote, highMidiNote));
    repaint();
}

int PlayableMidiKeyboardComponent::getWhiteKeyCount() const noexcept
{
    int count = 0;
    for (int midiNote = lowMidiNote_; midiNote <= highMidiNote_; ++midiNote)
        if (!isBlackKey(midiNote))
            ++count;

    return juce::jmax(1, count);
}

int PlayableMidiKeyboardComponent::getWhiteKeyIndexForNote(int midiNote) const noexcept
{
    int index = 0;
    for (int note = lowMidiNote_; note < midiNote; ++note)
        if (!isBlackKey(note))
            ++index;

    return index;
}

juce::Rectangle<float> PlayableMidiKeyboardComponent::getWhiteKeyBounds(int midiNote) const
{
    const auto area = getLocalBounds().toFloat();
    const float whiteKeyWidth = area.getWidth() / (float) getWhiteKeyCount();
    const float x = (float) getWhiteKeyIndexForNote(midiNote) * whiteKeyWidth;
    return { x, 0.0f, whiteKeyWidth, area.getHeight() };
}

juce::Rectangle<float> PlayableMidiKeyboardComponent::getBlackKeyBounds(int midiNote) const
{
    const auto area = getLocalBounds().toFloat();
    const float whiteKeyWidth = area.getWidth() / (float) getWhiteKeyCount();
    const float blackKeyWidth = whiteKeyWidth * 0.64f;
    const float blackKeyHeight = area.getHeight() * 0.62f;

    int previousWhiteNote = midiNote - 1;
    while (previousWhiteNote >= lowMidiNote_ && isBlackKey(previousWhiteNote))
        --previousWhiteNote;

    const float previousWhiteX = (float) getWhiteKeyIndexForNote(previousWhiteNote) * whiteKeyWidth;
    const float x = previousWhiteX + whiteKeyWidth - (blackKeyWidth * 0.5f);
    return { x, 0.0f, blackKeyWidth, blackKeyHeight };
}

int PlayableMidiKeyboardComponent::hitTestMidiNote(juce::Point<float> position) const
{
    for (int midiNote = lowMidiNote_; midiNote <= highMidiNote_; ++midiNote)
        if (isBlackKey(midiNote) && getBlackKeyBounds(midiNote).contains(position))
            return midiNote;

    for (int midiNote = lowMidiNote_; midiNote <= highMidiNote_; ++midiNote)
        if (!isBlackKey(midiNote) && getWhiteKeyBounds(midiNote).contains(position))
            return midiNote;

    return -1;
}

bool PlayableMidiKeyboardComponent::isNoteOn(int midiNote) const
{
    return keyboardState_ != nullptr && keyboardState_->isNoteOnForChannels(0xffff, midiNote);
}

void PlayableMidiKeyboardComponent::paint(juce::Graphics& g)
{
    auto& theme = Theme::getInstance();
    auto bounds = getLocalBounds();

    g.setColour(theme.colors.graphite.darker(0.15f));
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);

    for (int midiNote = lowMidiNote_; midiNote <= highMidiNote_; ++midiNote)
    {
        if (isBlackKey(midiNote))
            continue;

        auto keyBounds = getWhiteKeyBounds(midiNote);
        auto keyColour = isNoteOn(midiNote) ? theme.colors.accent.withAlpha(0.72f)
                                            : theme.colors.pearl.withAlpha(0.96f);
        g.setColour(keyColour);
        g.fillRoundedRectangle(keyBounds.reduced(0.5f, 1.0f), 4.0f);

        g.setColour(theme.colors.separatorHard.withAlpha(0.55f));
        g.drawRoundedRectangle(keyBounds.reduced(0.5f, 1.0f), 4.0f, 1.0f);

        if (midiNote % 12 == 0)
        {
            g.setColour(theme.colors.textSecondary);
            g.setFont(theme.fonts.small);
            g.drawText("C" + juce::String(midiNote / 12 - 1),
                       keyBounds.toNearestInt().removeFromBottom(18),
                       juce::Justification::centred,
                       false);
        }
    }

    for (int midiNote = lowMidiNote_; midiNote <= highMidiNote_; ++midiNote)
    {
        if (!isBlackKey(midiNote))
            continue;

        auto keyBounds = getBlackKeyBounds(midiNote);
        auto keyColour = isNoteOn(midiNote) ? theme.colors.accentActive.withAlpha(0.95f)
                                            : theme.colors.obsidian.brighter(0.12f);
        g.setColour(keyColour);
        g.fillRoundedRectangle(keyBounds, 4.0f);

        g.setColour(theme.colors.separatorHard.withAlpha(0.8f));
        g.drawRoundedRectangle(keyBounds, 4.0f, 1.0f);
    }
}

void PlayableMidiKeyboardComponent::updatePointerNote(const juce::MouseEvent& e)
{
    if (keyboardState_ == nullptr)
        return;

    const int pointerId = getPointerIdFromEvent(e);
    const int newMidiNote = hitTestMidiNote(e.position);
    const auto it = activePointerNotes_.find(pointerId);
    const int previousMidiNote = it != activePointerNotes_.end() ? it->second : -1;

    if (previousMidiNote == newMidiNote)
        return;

    if (previousMidiNote >= 0)
        keyboardState_->noteOff(1, previousMidiNote, 0.0f);

    if (newMidiNote >= 0)
    {
        keyboardState_->noteOn(1, newMidiNote, 1.0f);
        activePointerNotes_[pointerId] = newMidiNote;
    }
    else
    {
        activePointerNotes_.erase(pointerId);
    }

    repaint();
}

void PlayableMidiKeyboardComponent::releasePointer(int pointerId)
{
    if (keyboardState_ == nullptr)
        return;

    const auto it = activePointerNotes_.find(pointerId);
    if (it == activePointerNotes_.end())
        return;

    keyboardState_->noteOff(1, it->second, 0.0f);
    activePointerNotes_.erase(it);
    repaint();
}

void PlayableMidiKeyboardComponent::mouseDown(const juce::MouseEvent& e)
{
    updatePointerNote(e);
}

void PlayableMidiKeyboardComponent::mouseDrag(const juce::MouseEvent& e)
{
    updatePointerNote(e);
}

void PlayableMidiKeyboardComponent::mouseUp(const juce::MouseEvent& e)
{
    releasePointer(getPointerIdFromEvent(e));
}

void PlayableMidiKeyboardComponent::mouseExit(const juce::MouseEvent& e)
{
    if (!e.mods.isAnyMouseButtonDown())
        releasePointer(getPointerIdFromEvent(e));
}

void PlayableMidiKeyboardComponent::timerCallback()
{
    repaint();
}

} // namespace DAW

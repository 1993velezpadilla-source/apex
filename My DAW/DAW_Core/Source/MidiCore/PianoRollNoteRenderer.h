#pragma once

#include <JuceHeader.h>
#include "PianoRollClipModel.h"

namespace DAW {

class PianoRollNoteRenderer
{
public:
    enum class ColorMode
    {
        Velocity,
        Pitch,
        Clip,
        Channel
    };

    enum class HitZone
    {
        None,
        Body,
        Velocity,
        LeftEdge,
        RightEdge
    };

    struct HitTestResult
    {
        juce::int64 noteId = 0;
        HitZone zone = HitZone::None;
    };

    void setPixelsPerTick(float pixels)          { pixelsPerTick_ = juce::jmax(0.0001f, pixels); }
    void setKeyRowHeight(float height)           { keyRowHeight_ = juce::jmax(8.0f, height); }
    void setScrollOffset(float x, int firstNote) { scrollX_ = x; firstVisibleNote_ = juce::jlimit(0, 127, firstNote); }
    void setColorMode(ColorMode mode)            { colorMode_ = mode; }

    void paint(juce::Graphics& g,
               juce::Rectangle<int> bounds,
               const PianoRollClipModel* clipModel) const;

    HitTestResult hitTest(juce::Point<float> localPoint,
                          juce::Rectangle<int> bounds,
                          const PianoRollClipModel* clipModel) const;

    juce::Array<juce::int64> getNotesIntersecting(juce::Rectangle<float> area,
                                                  juce::Rectangle<int> bounds,
                                                  const PianoRollClipModel* clipModel) const;

private:
    juce::Rectangle<float> getNoteBounds(const PianoRollClipModel::NoteEvent& note,
                                         juce::Rectangle<int> bounds) const;

    float pixelsPerTick_ = 0.1f;
    float keyRowHeight_ = 20.0f;
    float scrollX_ = 0.0f;
    int firstVisibleNote_ = 96;
    ColorMode colorMode_ = ColorMode::Velocity;
};

} // namespace DAW

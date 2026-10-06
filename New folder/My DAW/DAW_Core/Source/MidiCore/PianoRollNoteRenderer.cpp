#include "PianoRollNoteRenderer.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

namespace
{
    juce::Colour colourForPitchClass(int pitchClass)
    {
        return juce::Colour::fromHSV((float) ((pitchClass % 12) / 12.0), 0.72f, 0.95f, 0.90f);
    }

    juce::Colour colourForChannel(int channel)
    {
        return juce::Colour::fromHSV((float) (juce::jlimit(0, 15, channel) / 16.0), 0.58f, 0.92f, 0.90f);
    }
}

juce::Rectangle<float> PianoRollNoteRenderer::getNoteBounds(const PianoRollClipModel::NoteEvent& note,
                                                            juce::Rectangle<int> bounds) const
{
    const float x = (float) bounds.getX() + (float) note.startTick * pixelsPerTick_ - scrollX_;
    const float y = (float) bounds.getY() + (float) (firstVisibleNote_ - (int) note.pitch) * keyRowHeight_;
    const float w = juce::jmax(6.0f, (float) note.lengthTicks * pixelsPerTick_);
    const float h = juce::jmax(6.0f, keyRowHeight_ - 1.0f);
    return { x, y, w, h };
}

void PianoRollNoteRenderer::paint(juce::Graphics& g,
                                  juce::Rectangle<int> bounds,
                                  const PianoRollClipModel* clipModel) const
{
    if (clipModel == nullptr)
        return;

    auto& theme = Theme::getInstance();
    auto notes = clipModel->getAllNotes();

    for (auto& note : notes)
    {
        auto noteBounds = getNoteBounds(note, bounds);
        if (! noteBounds.intersects(bounds.toFloat()))
            continue;

        const float normVelocity = juce::jlimit(0.0f, 1.0f, note.velocity / 127.0f);
        juce::Colour fill;
        switch (colorMode_)
        {
            case ColorMode::Pitch:
                fill = colourForPitchClass((int) note.pitch);
                break;
            case ColorMode::Clip:
                fill = juce::Colour(0xFF8F6BFF);
                break;
            case ColorMode::Channel:
                fill = colourForChannel((int) note.channel);
                break;
            default:
                fill = juce::Colour::fromHSV(0.78f - (0.78f * normVelocity), 0.72f, 0.95f, 0.90f);
                break;
        }

        fill = note.muted ? fill.withAlpha(0.25f) : fill.withAlpha(0.90f);

        g.setColour(fill);
        g.fillRoundedRectangle(noteBounds.reduced(0.5f), 3.0f);

        g.setColour(theme.colors.pearl.withAlpha(note.selected ? 0.95f : 0.55f));
        g.drawRoundedRectangle(noteBounds.reduced(0.5f), 3.0f, note.selected ? 2.0f : 1.0f);

        const float velBarWidth = juce::jlimit(2.0f, noteBounds.getWidth(), noteBounds.getWidth() * normVelocity);
        g.setColour(theme.colors.pearl.withAlpha(0.35f));
        g.fillRect(noteBounds.getX() + 1.0f, noteBounds.getBottom() - 3.0f, velBarWidth - 2.0f, 2.0f);
    }
}

PianoRollNoteRenderer::HitTestResult PianoRollNoteRenderer::hitTest(juce::Point<float> localPoint,
                                                                    juce::Rectangle<int> bounds,
                                                                    const PianoRollClipModel* clipModel) const
{
    if (clipModel == nullptr)
        return {};

    auto notes = clipModel->getAllNotes();
    for (int i = notes.size(); --i >= 0;)
    {
        const auto noteBounds = getNoteBounds(notes.getReference(i), bounds);
        if (noteBounds.contains(localPoint))
        {
            const float edgeWidth = juce::jlimit(3.0f, 8.0f, noteBounds.getWidth() * 0.25f);
            if (localPoint.x <= noteBounds.getX() + edgeWidth)
                return { notes.getReference(i).id, HitZone::LeftEdge };
            if (localPoint.x >= noteBounds.getRight() - edgeWidth)
                return { notes.getReference(i).id, HitZone::RightEdge };
            if (localPoint.y >= noteBounds.getBottom() - 5.0f)
                return { notes.getReference(i).id, HitZone::Velocity };
            return { notes.getReference(i).id, HitZone::Body };
        }
    }

    return {};
}

juce::Array<juce::int64> PianoRollNoteRenderer::getNotesIntersecting(juce::Rectangle<float> area,
                                                                     juce::Rectangle<int> bounds,
                                                                     const PianoRollClipModel* clipModel) const
{
    juce::Array<juce::int64> result;

    if (clipModel == nullptr || area.isEmpty())
        return result;

    auto notes = clipModel->getAllNotes();
    for (auto& note : notes)
    {
        if (getNoteBounds(note, bounds).intersects(area))
            result.add(note.id);
    }

    return result;
}

} // namespace DAW

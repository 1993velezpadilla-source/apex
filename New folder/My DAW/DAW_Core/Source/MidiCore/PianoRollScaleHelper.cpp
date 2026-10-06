#include "PianoRollScaleHelper.h"

namespace DAW {

int PianoRollScaleHelper::getRootPitchClass(const juce::String& root)
{
    const auto upper = root.trim().toUpperCase();
    if (upper == "C")  return 0;
    if (upper == "C#" || upper == "DB") return 1;
    if (upper == "D")  return 2;
    if (upper == "D#" || upper == "EB") return 3;
    if (upper == "E")  return 4;
    if (upper == "F")  return 5;
    if (upper == "F#" || upper == "GB") return 6;
    if (upper == "G")  return 7;
    if (upper == "G#" || upper == "AB") return 8;
    if (upper == "A")  return 9;
    if (upper == "A#" || upper == "BB") return 10;
    if (upper == "B")  return 11;
    return 0;
}

juce::Array<int> PianoRollScaleHelper::getScaleIntervals(const juce::String& scaleName)
{
    const auto name = scaleName.trim().toLowerCase();
    if (name == "minor") return { 0, 2, 3, 5, 7, 8, 10 };
    if (name == "harmonic minor") return { 0, 2, 3, 5, 7, 8, 11 };
    if (name == "melodic minor") return { 0, 2, 3, 5, 7, 9, 11 };
    if (name == "dorian") return { 0, 2, 3, 5, 7, 9, 10 };
    if (name == "phrygian") return { 0, 1, 3, 5, 7, 8, 10 };
    if (name == "lydian") return { 0, 2, 4, 6, 7, 9, 11 };
    if (name == "mixolydian") return { 0, 2, 4, 5, 7, 9, 10 };
    if (name == "locrian") return { 0, 1, 3, 5, 6, 8, 10 };
    if (name == "blues") return { 0, 3, 5, 6, 7, 10 };
    if (name == "pentatonic") return { 0, 2, 4, 7, 9 };
    return { 0, 2, 4, 5, 7, 9, 11 };
}

bool PianoRollScaleHelper::prefersFlats(const juce::String& root)
{
    const auto upper = root.trim().toUpperCase();
    return upper.contains("B") && upper.length() > 1;
}

bool PianoRollScaleHelper::isNoteInScale(int midiNote, const juce::String& root, const juce::String& scaleName)
{
    const int rootPc = getRootPitchClass(root);
    const int pitchClass = ((midiNote % 12) + 12) % 12;
    const int relative = (pitchClass - rootPc + 12) % 12;
    return getScaleIntervals(scaleName).contains(relative);
}

int PianoRollScaleHelper::snapNoteToScale(int midiNote, const juce::String& root, const juce::String& scaleName)
{
    midiNote = juce::jlimit(0, 127, midiNote);
    if (isNoteInScale(midiNote, root, scaleName))
        return midiNote;

    for (int distance = 1; distance < 12; ++distance)
    {
        const int down = juce::jlimit(0, 127, midiNote - distance);
        if (isNoteInScale(down, root, scaleName))
            return down;

        const int up = juce::jlimit(0, 127, midiNote + distance);
        if (isNoteInScale(up, root, scaleName))
            return up;
    }

    return midiNote;
}

juce::String PianoRollScaleHelper::getDisplayNoteName(int midiNote, const juce::String& root)
{
    static const char* sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatNames[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const auto* names = prefersFlats(root) ? flatNames : sharpNames;
    return juce::String(names[((midiNote % 12) + 12) % 12]) + juce::String((midiNote / 12) - 1);
}

} // namespace DAW

#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * UndoActionCore — base for a single undoable action.
 *
 * Every user-visible change creates an UndoActionCore-derived object
 * that knows how to undo and redo itself.
 *
 * Actions are pushed onto the undo stack and can be grouped
 * for compound operations (e.g., drag = many internal changes = one undo step).
 */
class UndoActionCore
{
public:
    virtual ~UndoActionCore() = default;

    /** Undo this action — restore previous state. */
    virtual void undo() = 0;

    /** Redo this action — reapply the change. */
    virtual void redo() = 0;

    /** Human-readable description for undo history display. */
    virtual juce::String getDescription() const = 0;

    /** Returns the timestamp when this action was created. */
    juce::int64 getTimestamp() const noexcept { return timestamp_; }

protected:
    juce::int64 timestamp_ = juce::Time::currentTimeMillis();
};

} // namespace DAW

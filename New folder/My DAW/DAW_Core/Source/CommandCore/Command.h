#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * Command — base class for undoable actions.
 *
 * Every destructive or state-modifying operation must be wrapped in a Command
 * subclass. The CommandManager maintains a stack of executed commands and
 * supports undo/redo.
 *
 * Design rules:
 *  - execute() must be idempotent for redo
 *  - undo() must perfectly restore previous state
 *  - Commands own all data needed to reverse the operation
 *  - No UI dependencies — commands are pure data/logic
 */
class Command
{
public:
    virtual ~Command() = default;

    /** Human-readable description for the undo history UI. */
    virtual juce::String getDescription() const = 0;

    /** Execute the action. Called on first do and on redo. */
    virtual void execute() = 0;

    /** Reverse the action. Must perfectly restore previous state. */
    virtual void undo() = 0;

    /** Optional: merge with a subsequent command of the same type.
     *  Return true if merged (the other command will be discarded).
     *  Useful for coalescing rapid parameter changes (e.g., fader drags). */
    virtual bool mergeWith(const Command& /*subsequent*/) { return false; }

    /** Category for grouping in undo history (e.g., "Edit", "Track", "Routing"). */
    virtual juce::String getCategory() const { return "Edit"; }
};

} // namespace DAW

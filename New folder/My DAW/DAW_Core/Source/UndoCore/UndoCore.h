#pragma once
#include "UndoActionCore.h"
#include <vector>
#include <memory>

namespace DAW {

/**
 * UndoCore — undo/redo stack engine.
 *
 * Maintains undo and redo stacks of UndoActionCore objects.
 * Supports grouped actions (compound undo steps).
 *
 * Rules:
 *   - Undo reverts the last action and pushes it to redo stack
 *   - Redo reapplies the last undone action
 *   - A new action after undo clears the redo stack
 *   - Grouped actions appear as a single undo step
 */
class UndoCore
{
public:
    /** Push a new action onto the undo stack. Clears redo stack. */
    void pushAction(std::unique_ptr<UndoActionCore> action)
    {
        redoStack_.clear();
        undoStack_.push_back(std::move(action));

        // Limit stack size to prevent memory bloat
        while ((int)undoStack_.size() > maxStackSize_)
            undoStack_.erase(undoStack_.begin());
    }

    /** Undo the last action. Returns true if an action was undone. */
    bool undo()
    {
        if (undoStack_.empty()) return false;

        auto action = std::move(undoStack_.back());
        undoStack_.pop_back();
        action->undo();
        redoStack_.push_back(std::move(action));
        return true;
    }

    /** Redo the last undone action. Returns true if an action was redone. */
    bool redo()
    {
        if (redoStack_.empty()) return false;

        auto action = std::move(redoStack_.back());
        redoStack_.pop_back();
        action->redo();
        undoStack_.push_back(std::move(action));
        return true;
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    juce::String getUndoDescription() const
    {
        return undoStack_.empty() ? "" : undoStack_.back()->getDescription();
    }

    juce::String getRedoDescription() const
    {
        return redoStack_.empty() ? "" : redoStack_.back()->getDescription();
    }

    int getUndoStackSize() const { return (int)undoStack_.size(); }
    int getRedoStackSize() const { return (int)redoStack_.size(); }

    void setMaxStackSize(int maxSize) { maxStackSize_ = juce::jmax(1, maxSize); }

    void clearAll()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    std::vector<std::unique_ptr<UndoActionCore>> undoStack_;
    std::vector<std::unique_ptr<UndoActionCore>> redoStack_;
    int maxStackSize_ = 500;
};

} // namespace DAW

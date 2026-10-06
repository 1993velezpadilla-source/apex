#pragma once
#include <JuceHeader.h>
#include <functional>
#include "Command.h"
#include "../ActionCore/ActionManager.h"
#include "../ActionCore/ActionID.h"

namespace DAW {

/**
 * CommandManager — unlimited undo/redo stack with timestamped history.
 *
 * Thread safety: all calls must be on the message thread.
 * Self-wires to ActionManager for EditUndo / EditRedo dispatch.
 *
 * Usage:
 *   CommandManager::getInstance().execute(
 *       std::make_unique<RenameTrackCommand>(track, "New Name"));
 *
 *   Ctrl+Z / Ctrl+Y trigger undo/redo automatically via ActionManager.
 *   History panel calls undoSteps(n) / redoSteps(n) to jump directly.
 */
class CommandManager
{
public:
    // ── History entry — command + wall-clock timestamp ─────────────────────
    struct HistoryEntry
    {
        std::unique_ptr<Command> cmd;
        juce::Time timestamp = juce::Time::getCurrentTime();
    };

    // ── Summary — read-only snapshot for UI ────────────────────────────────
    struct HistorySummary
    {
        juce::String description;
        juce::String category;
        juce::Time   timestamp;
    };

    static CommandManager& getInstance()
    {
        static CommandManager instance;
        return instance;
    }

    /** Execute a command and push it onto the undo stack.
     *  Merges with top of stack when possible (e.g., fader drag coalescing).
     *  Merging is gesture-scoped: only commands arriving within
     *  kMergeWindowMs of the previous one coalesce, so a continuous drag
     *  collapses into ONE undo step while two separate gestures on the same
     *  object stay two distinct steps - exactly how Pro Tools / FL Studio /
     *  Ableton / Studio One granulate their histories.
     *  History is capped at kMaxHistory entries - oldest dropped when full. */
    void execute(std::unique_ptr<Command> cmd)
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (!cmd) return;

        // Try to merge with top of undo stack (coalesces rapid edits like drag).
        // Time-gated: an old top entry means the previous gesture ended - the
        // new command must become its own undo step.
        if (!undoStack_.empty())
        {
            auto& top = undoStack_.back();
            const auto sinceLast = juce::Time::getCurrentTime() - top.timestamp;

            if (sinceLast.inMilliseconds() <= kMergeWindowMs && top.cmd->mergeWith(*cmd))
            {
                // Keep the entry timestamp rolling so an in-progress drag can
                // exceed the window in total while still coalescing.
                top.timestamp = juce::Time::getCurrentTime();
                listeners_.call([](Listener& l) { l.undoHistoryChanged(); });
                return;
            }
        }

        cmd->execute();
        undoStack_.push_back({ std::move(cmd), juce::Time::getCurrentTime() });
        redoStack_.clear();

        // Cap to prevent unbounded memory growth. 10,000 steps = ~5-10 MB typical.
        // Oldest entries are dropped when the cap is reached.
        while ((int)undoStack_.size() > maxHistory_)
            undoStack_.erase(undoStack_.begin());

        listeners_.call([](Listener& l) { l.undoHistoryChanged(); });
    }

    /** Change the maximum undo depth. Default = 10,000. */
    void setMaxHistory(int n)
    {
        maxHistory_ = juce::jmax(1, n);
        while ((int)undoStack_.size() > maxHistory_)
            undoStack_.erase(undoStack_.begin());
    }
    int getMaxHistory() const { return maxHistory_; }

    /** Undo the most recent command. */
    bool undo()
    {
        if (undoRedoBlocked_ && undoRedoBlocked_()) return false;
        if (undoStack_.empty()) return false;
        auto entry = std::move(undoStack_.back());
        undoStack_.pop_back();
        entry.cmd->undo();
        redoStack_.push_back(std::move(entry));
        listeners_.call([](Listener& l) { l.undoHistoryChanged(); });
        return true;
    }

    /** Redo the most recently undone command. */
    bool redo()
    {
        if (undoRedoBlocked_ && undoRedoBlocked_()) return false;
        if (redoStack_.empty()) return false;
        auto entry = std::move(redoStack_.back());
        redoStack_.pop_back();
        entry.cmd->execute();
        undoStack_.push_back(std::move(entry));
        listeners_.call([](Listener& l) { l.undoHistoryChanged(); });
        return true;
    }

    /** Install a guard that blocks undo/redo while it returns true.
     *  Professional-DAW recording protection: undoing a structural edit
     *  (delete track / move clip) mid-take would corrupt the recording, so
     *  the host wires this to "is a take rolling?". Execute stays allowed -
     *  new edits during recording must still land on the history. */
    void setUndoRedoBlockedQuery(std::function<bool()> query)
    {
        undoRedoBlocked_ = std::move(query);
    }

    /** Undo exactly n steps. Used by history panel for direct jumps. */
    void undoSteps(int n)
    {
        for (int i = 0; i < n && !undoStack_.empty(); ++i)
            undo();
    }

    /** Redo exactly n steps. Used by history panel for direct jumps. */
    void redoSteps(int n)
    {
        for (int i = 0; i < n && !redoStack_.empty(); ++i)
            redo();
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    juce::String getUndoDescription() const
    {
        return undoStack_.empty() ? juce::String() : undoStack_.back().cmd->getDescription();
    }
    juce::String getRedoDescription() const
    {
        return redoStack_.empty() ? juce::String() : redoStack_.back().cmd->getDescription();
    }

    int getUndoCount() const { return (int)undoStack_.size(); }
    int getRedoCount() const { return (int)redoStack_.size(); }

    /** Returns undo history summaries — index 0 = most recent. */
    std::vector<HistorySummary> getUndoSummaries() const
    {
        std::vector<HistorySummary> out;
        out.reserve(undoStack_.size());
        for (int i = (int)undoStack_.size() - 1; i >= 0; --i)
            out.push_back({ undoStack_[i].cmd->getDescription(),
                            undoStack_[i].cmd->getCategory(),
                            undoStack_[i].timestamp });
        return out;
    }

    /** Returns redo history summaries — index 0 = most recent (next to redo). */
    std::vector<HistorySummary> getRedoSummaries() const
    {
        std::vector<HistorySummary> out;
        out.reserve(redoStack_.size());
        for (int i = (int)redoStack_.size() - 1; i >= 0; --i)
            out.push_back({ redoStack_[i].cmd->getDescription(),
                            redoStack_[i].cmd->getCategory(),
                            redoStack_[i].timestamp });
        return out;
    }

    /** Clear all undo/redo history (e.g., after project load). */
    void clearHistory()
    {
        undoStack_.clear();
        redoStack_.clear();
        listeners_.call([](Listener& l) { l.undoHistoryChanged(); });
    }

    // ── Listener ──────────────────────────────────────────────────────────────
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void undoHistoryChanged() {}
    };

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    CommandManager()
    {
        undoReg_ = ActionManager::getInstance().scoped(ActionID::EditUndo, [this] { undo(); });
        redoReg_ = ActionManager::getInstance().scoped(ActionID::EditRedo, [this] { redo(); });
    }

    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    int maxHistory_ = 10000; // configurable; 10k entries ≈ 5–10 MB typical

    // Merge window: drag/gesture events arrive well under this apart, while
    // two deliberate separate gestures on the same object land further apart.
    static constexpr juce::int64 kMergeWindowMs = 500;

    std::function<bool()> undoRedoBlocked_;

    juce::ListenerList<Listener> listeners_;
    ActionManager::ActionRegistration undoReg_;
    ActionManager::ActionRegistration redoReg_;

    JUCE_DECLARE_NON_COPYABLE(CommandManager)
};

} // namespace DAW


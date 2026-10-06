// ===========================================================================
// ArrangementUndoCore.h
// Command pattern undo/redo system for all arrangement operations.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include <JuceHeader.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../../Source/CommandCore/CommandManager.h"

namespace ArrangementEditor
{
    // -----------------------------------------------------------------------
    // Base command interface
    // -----------------------------------------------------------------------
    struct IArrangementCommand
    {
        virtual ~IArrangementCommand() = default;
        virtual void execute() = 0;
        virtual void undo() = 0;
        virtual std::string description() const = 0;
    };

    // -----------------------------------------------------------------------
    // LambdaArrangementCommand — convenient inline command built from two
    // std::function callbacks. Lets call sites record undo for clip
    // delete/cut/duplicate/move without authoring a dedicated class each time.
    // -----------------------------------------------------------------------
    struct LambdaArrangementCommand : public IArrangementCommand
    {
        std::function<void()> doFn;
        std::function<void()> undoFn;
        std::string desc;

        LambdaArrangementCommand(std::string description,
                                 std::function<void()> executeFn,
                                 std::function<void()> reverseFn,
                                 bool alreadyApplied = false)
            : doFn(std::move(executeFn)), undoFn(std::move(reverseFn)), desc(std::move(description)), alreadyApplied_(alreadyApplied) {}

        void execute() override
        {
            if (alreadyApplied_ && !hasExecuted_)
            {
                hasExecuted_ = true;
                return;
            }

            if (doFn) doFn();
            hasExecuted_ = true;
        }
        void undo() override { if (undoFn) undoFn(); }
        std::string description() const override { return desc; }

    private:
        bool alreadyApplied_ = false;
        bool hasExecuted_ = false;
    };

    // -----------------------------------------------------------------------
    // Undo manager
    //
    // UNIFIED HISTORY: every major DAW (Pro Tools, FL Studio, Ableton, Logic,
    // Reaper, Studio One) maintains ONE linear undo timeline shared across the
    // whole project. This class is now a thin façade over the global
    // DAW::CommandManager so arrangement edits land on the exact same stack as
    // track/mixer/plugin/automation edits. A single Ctrl+Z therefore walks the
    // entire project history regardless of which surface created each step.
    // -----------------------------------------------------------------------
    class ArrangementUndoCore
    {
    public:
        ArrangementUndoCore() = default;

        // Execute a new command — routed onto the global unified history.
        void executeCommand(std::unique_ptr<IArrangementCommand> cmd)
        {
            if (!cmd) return;
            DAW::CommandManager::getInstance().execute(
                std::make_unique<Adapter>(std::move(cmd)));
            if (onStackChanged) onStackChanged();
        }

        // -----------------------------------------------------------------------
        // perform() — bridge for juce::UndoableAction (TimePitchSetStateAction etc.)
        // Wraps the action in an IArrangementCommand adapter and executes it.
        // Takes ownership of the action pointer.
        // -----------------------------------------------------------------------
        void perform(juce::UndoableAction* action)
        {
            if (!action) return;

            struct JuceActionAdapter : public IArrangementCommand
            {
                std::unique_ptr<juce::UndoableAction> act;
                explicit JuceActionAdapter(juce::UndoableAction* a) : act(a) {}
                void execute()  override { act->perform(); }
                void undo()     override { act->undo(); }
                std::string description() const override { return "TimePitch"; }
            };

            executeCommand(std::make_unique<JuceActionAdapter>(action));
        }

        // Undo / redo — walk the global unified history.
        bool undo()
        {
            const bool ok = DAW::CommandManager::getInstance().undo();
            if (ok && onStackChanged) onStackChanged();
            return ok;
        }

        bool redo()
        {
            const bool ok = DAW::CommandManager::getInstance().redo();
            if (ok && onStackChanged) onStackChanged();
            return ok;
        }

        // Query — reflect the global unified history.
        bool canUndo() const { return DAW::CommandManager::getInstance().canUndo(); }
        bool canRedo() const { return DAW::CommandManager::getInstance().canRedo(); }

        std::string undoDescription() const
        {
            return DAW::CommandManager::getInstance().getUndoDescription().toStdString();
        }

        std::string redoDescription() const
        {
            return DAW::CommandManager::getInstance().getRedoDescription().toStdString();
        }

        void clear()
        {
            DAW::CommandManager::getInstance().clearHistory();
            if (onStackChanged) onStackChanged();
        }

        // Callbacks
        std::function<void()> onStackChanged;

    private:
        // Adapter — exposes an IArrangementCommand as a DAW::Command so it can
        // live on the global unified undo stack alongside every other edit type.
        struct Adapter : public DAW::Command
        {
            std::unique_ptr<IArrangementCommand> inner;
            explicit Adapter(std::unique_ptr<IArrangementCommand> c) : inner(std::move(c)) {}

            juce::String getDescription() const override { return juce::String(inner->description()); }
            juce::String getCategory() const override { return "Arrangement"; }
            void execute() override { inner->execute(); }
            void undo() override { inner->undo(); }
        };
    };

} // namespace ArrangementEditor

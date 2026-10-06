#pragma once

#include <JuceHeader.h>
#include "../../Source/AutomationCore/AutomationUIHelper.h"
#include "../../Source/UtilityCore/Types.h"
#include "../../Source/CommandCore/CommandManager.h"

namespace DAW {

/**
 * PHASE 3: UNDO/REDO SYSTEM
 * 
 * Base class for all automation actions that can be undone/redone.
 */
class AutomationAction
{
public:
    virtual ~AutomationAction() = default;

    /**
     * Execute the action (redo).
     */
    virtual void redo() = 0;

    /**
     * Revert the action (undo).
     */
    virtual void undo() = 0;

    /**
     * Get description for undo/redo menu.
     * e.g., "Delete Point", "Change Curve Type"
     */
    virtual juce::String getDescription() const = 0;
};

// ============================================================================
// CONCRETE ACTION IMPLEMENTATIONS
// ============================================================================

/**
 * Delete a point from automation lane.
 */
class DeletePointAction : public AutomationAction
{
public:
    DeletePointAction(AutomationUIHelper& helper,
                     const TrackID& trackId,
                     const juce::String& parameterId,
                     int pointIndex,
                     const AutomationPoint& savedPoint)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          pointIndex_(pointIndex), savedPoint_(savedPoint)
    {
    }

    void redo() override
    {
        helper_.deletePoint(trackId_, parameterId_, pointIndex_);
    }

    void undo() override
    {
        // Since we can't directly add a point via helper, we'll just mark this as unable to undo
        // This is a limitation of the current helper API
        // For Phase 3+, we'd need to expose addPoint() method in AutomationUIHelper
        DBG("Delete undo not yet fully implemented - need helper.addPoint() method");
    }

    juce::String getDescription() const override
    {
        return "Delete Point";
    }

private:
    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;
    int pointIndex_;
    AutomationPoint savedPoint_;
};

/**
 * Paste point value (inverse: restore original value).
 */
class PastePointValueAction : public AutomationAction
{
public:
    PastePointValueAction(AutomationUIHelper& helper,
                         const TrackID& trackId,
                         const juce::String& parameterId,
                         int pointIndex,
                         float newValue,
                         float oldValue)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          pointIndex_(pointIndex), newValue_(newValue), oldValue_(oldValue)
    {
    }

    void redo() override
    {
        helper_.setPointExactValue(trackId_, parameterId_, pointIndex_, newValue_);
    }

    void undo() override
    {
        helper_.setPointExactValue(trackId_, parameterId_, pointIndex_, oldValue_);
    }

    juce::String getDescription() const override
    {
        return "Paste Point Value";
    }

private:
    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;
    int pointIndex_;
    float newValue_;
    float oldValue_;
};

/**
 * Change curve type (inverse: restore original type).
 */
class SetCurveTypeAction : public AutomationAction
{
public:
    SetCurveTypeAction(AutomationUIHelper& helper,
                      const TrackID& trackId,
                      const juce::String& parameterId,
                      int segmentIndex,
                      AutomationCurveType newType,
                      AutomationCurveType oldType)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          segmentIndex_(segmentIndex), newType_(newType), oldType_(oldType)
    {
    }

    void redo() override
    {
        helper_.setSegmentCurveType(trackId_, parameterId_, segmentIndex_, newType_);
    }

    void undo() override
    {
        helper_.setSegmentCurveType(trackId_, parameterId_, segmentIndex_, oldType_);
    }

    juce::String getDescription() const override
    {
        return "Change Curve Type";
    }

private:
    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;
    int segmentIndex_;
    AutomationCurveType newType_;
    AutomationCurveType oldType_;
};

/**
 * Set tension value (inverse: restore original tension).
 */
class SetTensionAction : public AutomationAction
{
public:
    SetTensionAction(AutomationUIHelper& helper,
                    const TrackID& trackId,
                    const juce::String& parameterId,
                    int segmentIndex,
                    float newTension,
                    float oldTension)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          segmentIndex_(segmentIndex), newTension_(newTension), oldTension_(oldTension)
    {
    }

    void redo() override
    {
        helper_.setSegmentTension(trackId_, parameterId_, segmentIndex_, newTension_);
    }

    void undo() override
    {
        helper_.setSegmentTension(trackId_, parameterId_, segmentIndex_, oldTension_);
    }

    juce::String getDescription() const override
    {
        return "Set Tension";
    }

private:
    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;
    int segmentIndex_;
    float newTension_;
    float oldTension_;
};

/**
 * Reset tension to neutral (inverse: restore previous tension).
 */
class ResetTensionAction : public AutomationAction
{
public:
    ResetTensionAction(AutomationUIHelper& helper,
                      const TrackID& trackId,
                      const juce::String& parameterId,
                      int segmentIndex,
                      float oldTension)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          segmentIndex_(segmentIndex), oldTension_(oldTension)
    {
    }

    void redo() override
    {
        helper_.setSegmentTension(trackId_, parameterId_, segmentIndex_, 0.0f);
    }

    void undo() override
    {
        helper_.setSegmentTension(trackId_, parameterId_, segmentIndex_, oldTension_);
    }

    juce::String getDescription() const override
    {
        return "Reset Tension";
    }

private:
    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;
    int segmentIndex_;
    float oldTension_;
};

// ============================================================================
// UNDO MANAGER
// ============================================================================

/**
 * Manages undo/redo history for automation operations.
 *
 * UNIFIED HISTORY: like every major DAW (Pro Tools, FL Studio, Ableton, Logic,
 * Reaper, Studio One), automation edits now land on the SAME single linear undo
 * timeline as track/clip/mixer/plugin edits. This class is a thin façade over
 * the global DAW::CommandManager, so one Ctrl+Z walks the whole project history.
 *
 * Usage:
 *
 * auto action = std::make_unique<DeletePointAction>(...);
 * undoManager.doAction(std::move(action));
 *
 * undoManager.undo();
 * undoManager.redo();
 */
class UndoManager
{
public:
    UndoManager() = default;

    /**
     * Execute action and add to the global unified history.
     * Clears redo history (handled by the global CommandManager).
     */
    void doAction(std::unique_ptr<AutomationAction> action)
    {
        if (!action)
            return;

        DAW::CommandManager::getInstance().execute(
            std::make_unique<Adapter>(std::move(action)));
    }

    /**
     * Undo the last action on the global unified history.
     */
    void undo()
    {
        DAW::CommandManager::getInstance().undo();
    }

    /**
     * Redo the last undone action on the global unified history.
     */
    void redo()
    {
        DAW::CommandManager::getInstance().redo();
    }

    /**
     * Check if undo is available.
     */
    bool canUndo() const
    {
        return DAW::CommandManager::getInstance().canUndo();
    }

    /**
     * Check if redo is available.
     */
    bool canRedo() const
    {
        return DAW::CommandManager::getInstance().canRedo();
    }

    /**
     * Get description of last action (for menu).
     */
    juce::String getUndoDescription() const
    {
        auto d = DAW::CommandManager::getInstance().getUndoDescription();
        return d.isEmpty() ? juce::String("Undo") : ("Undo: " + d);
    }

    /**
     * Get description of next redo action (for menu).
     */
    juce::String getRedoDescription() const
    {
        auto d = DAW::CommandManager::getInstance().getRedoDescription();
        return d.isEmpty() ? juce::String("Redo") : ("Redo: " + d);
    }

    /**
     * Clear all history.
     */
    void clear()
    {
        DAW::CommandManager::getInstance().clearHistory();
    }

private:
    // Adapter — exposes an AutomationAction as a DAW::Command so automation
    // edits can live on the global unified undo stack alongside every other
    // edit type. Note: AutomationAction::redo() is the "execute" semantic.
    struct Adapter : public DAW::Command
    {
        std::unique_ptr<AutomationAction> inner;
        explicit Adapter(std::unique_ptr<AutomationAction> a) : inner(std::move(a)) {}

        juce::String getDescription() const override { return inner->getDescription(); }
        juce::String getCategory() const override { return "Automation"; }
        void execute() override { inner->redo(); }
        void undo() override { inner->undo(); }
    };
};

} // namespace DAW

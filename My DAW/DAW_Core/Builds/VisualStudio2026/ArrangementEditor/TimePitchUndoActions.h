// ===========================================================================
// TimePitchUndoActions.h
// Acciones undoable para cambios de pitch / time stretch / formant / mode.
//
// POR QUÉ EXISTE:
//   juce::UndoManager requiere juce::UndoableAction.
//   Cada cambio de parámetro de pitch/time es una acción atómica.
//   Agrupar múltiples cambios con UndoManager::beginNewTransaction().
//
// FLUJO:
//   1. User mueve pitch knob.
//   2. UI crea TimePitchSetStateAction(clip, oldState, newState).
//   3. undoManager->perform(action) → aplica newState, guarda oldState.
//   4. Ctrl+Z → undo() → aplica oldState.
//   5. Ctrl+Y → redo() → aplica newState.
//
// THREAD SAFETY:
//   Todas las acciones se ejecutan en el message thread.
//   El audio thread lee el estado vía snapshot atómico.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include "ArrangementClipModel.h"
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// TimePitchSetStateAction
// Undoable action for any TimePitchState change on a single clip.
// Covers: pitch, fine tune, stretch, formant, mode, preserveFormants,
//         transientPreserve, highQuality — all in one shot.
// ---------------------------------------------------------------------------
class TimePitchSetStateAction : public juce::UndoableAction
{
public:
    // onApply: callback that applies a TimePitchState to the clip.
    // Typically: [clipModel, audioClip, dspCore, viewRefresh] lambda.
    using ApplyFn = std::function<void(const TimePitchState&)>;

    TimePitchSetStateAction(const juce::Uuid& clipId,
                             const juce::String& clipName,
                             const TimePitchState& oldState,
                             const TimePitchState& newState,
                             ApplyFn applyFn)
        : clipId_(clipId)
        , clipName_(clipName)
        , oldState_(oldState)
        , newState_(newState)
        , applyFn_(std::move(applyFn))
    {}

    bool perform() override
    {
        if (applyFn_) applyFn_(newState_);
        return true;
    }

    bool undo() override
    {
        if (applyFn_) applyFn_(oldState_);
        return true;
    }

    // JUCE uses this to coalesce rapid knob movements into one undo step.
    // Two actions on the same clip can be merged if the old newState of the
    // earlier matches the oldState of the later.
    bool coalesceWith(const UndoableAction* other)
    {
        if (const auto* o = dynamic_cast<const TimePitchSetStateAction*>(other))
        {
            if (o->clipId_ == clipId_)
            {
                newState_ = o->newState_;
                return true;
            }
        }
        return false;
    }

    int getSizeInUnits() override { return sizeof(*this); }

    juce::String getActionName() const
    {
        return "Pitch/Time: " + clipName_;
    }

private:
    juce::Uuid       clipId_;
    juce::String     clipName_;
    TimePitchState   oldState_;
    TimePitchState   newState_;
    ApplyFn          applyFn_;
};

// ---------------------------------------------------------------------------
// TimePitchModeChangeAction
// Lighter action for mode-only changes (no continuous knob coalescing needed).
// ---------------------------------------------------------------------------
class TimePitchModeChangeAction : public juce::UndoableAction
{
public:
    using ApplyFn = std::function<void(TimePitchMode)>;

    TimePitchModeChangeAction(const juce::Uuid& clipId,
                               const juce::String& clipName,
                               TimePitchMode oldMode,
                               TimePitchMode newMode,
                               ApplyFn applyFn)
        : clipId_(clipId), clipName_(clipName)
        , oldMode_(oldMode), newMode_(newMode)
        , applyFn_(std::move(applyFn))
    {}

    bool perform() override { if (applyFn_) applyFn_(newMode_); return true; }
    bool undo()    override { if (applyFn_) applyFn_(oldMode_); return true; }

    int getSizeInUnits() override { return sizeof(*this); }

private:
    juce::Uuid     clipId_;
    juce::String   clipName_;
    TimePitchMode  oldMode_;
    TimePitchMode  newMode_;
    ApplyFn        applyFn_;
};

} // namespace ArrangementEditor

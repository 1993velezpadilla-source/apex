#pragma once
#include "Command.h"
#include "../ClipCore/Clip.h"

namespace DAW {

/**
 * ClipEditCommand — undoable clip move/resize.
 *
 * Stores old and new start position + length.
 * Uses ClipID for safe lookup (survives pointer invalidation).
 */
class ClipEditCommand : public Command
{
public:
    ClipEditCommand(ClipManager& clips, const ClipID& clipId,
                    SamplePosition oldStart, SamplePosition oldLen,
                    SamplePosition newStart, SamplePosition newLen)
        : clips_(clips), clipId_(clipId),
          oldStart_(oldStart), oldLen_(oldLen),
          newStart_(newStart), newLen_(newLen) {}

    juce::String getDescription() const override { return "Edit Clip"; }

    void execute() override
    {
        if (auto* c = clips_.getClip(clipId_))
        {
            c->setStartPosition(newStart_);
            c->setLength(newLen_);
        }
    }

    void undo() override
    {
        if (auto* c = clips_.getClip(clipId_))
        {
            c->setStartPosition(oldStart_);
            c->setLength(oldLen_);
        }
    }

    /** Merge consecutive edits on the same clip — keeps only the final position.
     *  This prevents drag operations from flooding the undo stack. */
    bool mergeWith(const Command& subsequent) override
    {
        if (auto* other = dynamic_cast<const ClipEditCommand*>(&subsequent))
        {
            if (other->clipId_ == clipId_)
            {
                newStart_ = other->newStart_;
                newLen_   = other->newLen_;
                return true;
            }
        }
        return false;
    }

    juce::String getCategory() const override { return "Clip"; }

private:
    ClipManager&   clips_;
    ClipID         clipId_;
    SamplePosition oldStart_, oldLen_, newStart_, newLen_;
};

} // namespace DAW

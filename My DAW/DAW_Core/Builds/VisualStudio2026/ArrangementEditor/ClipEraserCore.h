// ===========================================================================
// ClipEraserCore.h
// Logic for erasing clips. Supports undo.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "ArrangementClipStateCore.h"
#include <JuceHeader.h>

namespace ArrangementEditor
{
    class ClipEraserCore
    {
    public:
        // Erase a clip by ID
        static void eraseClip(ArrangementClipStateCore& state, const juce::Uuid& id)
        {
            state.removeClip(id);
        }

        // Erase multiple clips
        static void eraseClips(ArrangementClipStateCore& state,
                               const std::vector<juce::Uuid>& ids)
        {
            for (auto& id : ids)
                state.removeClip(id);
        }
    };

} // namespace ArrangementEditor

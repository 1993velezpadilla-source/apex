#pragma once
#include <JuceHeader.h>
#include <vector>
#include "Command.h"
#include "../ClipCore/Clip.h"
#include "../AudioEngineCore/AudioFileManager.h"

namespace DAW {

/**
 * RecordAudioTakeCommand — undoable recorded audio take(s).
 *
 * Pushed AFTER RecordingClipFinalizerCore has already created the clips
 * (alreadyApplied semantics: the first execute() is a no-op).
 *
 * Undo removes the take clips from the timeline but NEVER deletes the
 * recorded WAV from disk — the same non-destructive model as Pro Tools /
 * Logic / Live. Redo recreates the clips from their captured state and
 * reloads the audio (fast path: AudioFileManager caches decoded sources).
 *
 * recreateClipFromState() assigns fresh ClipIDs, so currentId is re-mapped
 * on every redo cycle to keep undo targeting the live clip.
 */
class RecordAudioTakeCommand : public Command
{
public:
	struct Take
	{
		juce::ValueTree state;      // full AudioClip state (post-recording)
		juce::File      sourceFile; // recorded WAV on disk
		ClipID          currentId;  // live clip id — updated across redo cycles
	};

	RecordAudioTakeCommand(ClipManager& clips, AudioFileManager* audioFiles,
						   std::vector<Take> takes)
		: clips_(clips), audioFiles_(audioFiles), takes_(std::move(takes)) {}

	juce::String getDescription() const override
	{
		return takes_.size() > 1 ? "Record Audio Takes" : "Record Audio Take";
	}
	juce::String getCategory() const override { return "Record"; }

	void execute() override
	{
		// Clips are already live when the command is pushed (recording just
		// finished) — only re-apply on redo.
		if (!hasExecuted_) { hasExecuted_ = true; return; }

		for (auto& t : takes_)
		{
			auto* clip = clips_.recreateClipFromState(t.state);
			if (clip == nullptr)
				continue;

			t.currentId = clip->getID();

			if (audioFiles_ != nullptr && t.sourceFile.existsAsFile())
				audioFiles_->loadForClip(clip->getID(), t.sourceFile);

			// Refresh UI mirrors now that audio is loaded (waveform etc.).
			clip->notifyStateRestored();
		}
	}

	void undo() override
	{
		// Non-destructive: timeline clips go away, WAV stays on disk.
		for (auto& t : takes_)
			clips_.deleteClip(t.currentId);
	}

private:
	ClipManager&      clips_;
	AudioFileManager* audioFiles_;
	std::vector<Take> takes_;
	bool              hasExecuted_ = false;
};

/**
 * RecordMidiTakeCommand — undoable recorded MIDI take(s).
 *
 * Two shapes, matching how major DAWs treat MIDI record passes:
 *  - isNewClip: the take created a fresh clip → undo deletes it,
 *    redo recreates it from the captured post-state.
 *  - !isNewClip: the take recorded INTO an existing clip → undo/redo swap
 *    the clip's full state (piano-roll notes, length, tempo context).
 */
class RecordMidiTakeCommand : public Command
{
public:
	struct Take
	{
		bool            isNewClip = false;
		juce::ValueTree preState;   // existing-clip takes only
		juce::ValueTree postState;  // full state after recording
		ClipID          currentId;  // live clip id — updated across redo cycles
	};

	RecordMidiTakeCommand(ClipManager& clips, std::vector<Take> takes)
		: clips_(clips), takes_(std::move(takes)) {}

	juce::String getDescription() const override { return "Record MIDI Take"; }
	juce::String getCategory() const override { return "Record"; }

	void execute() override
	{
		if (!hasExecuted_) { hasExecuted_ = true; return; }

		for (auto& t : takes_)
		{
			if (t.isNewClip)
			{
				if (auto* clip = clips_.recreateClipFromState(t.postState))
					t.currentId = clip->getID();
			}
			else if (auto* clip = clips_.getClip(t.currentId))
			{
				clip->restoreState(t.postState);
				clip->notifyStateRestored();
			}
		}
	}

	void undo() override
	{
		for (auto& t : takes_)
		{
			if (t.isNewClip)
			{
				clips_.deleteClip(t.currentId);
			}
			else if (auto* clip = clips_.getClip(t.currentId))
			{
				clip->restoreState(t.preState);
				clip->notifyStateRestored();
			}
		}
	}

private:
	ClipManager&      clips_;
	std::vector<Take> takes_;
	bool              hasExecuted_ = false;
};

} // namespace DAW

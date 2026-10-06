#pragma once

#include <JuceHeader.h>
#include <vector>
#include "VirtualMidiKeyboardCore.h"
#include "MidiClip.h"
#include "../StateCore/ApplicationState.h"
#include "../TransportCore/TransportController.h"
#include "../TrackCore/Track.h"
#include "../CommandCore/CommandManager.h"
#include "../CommandCore/RecordCommands.h"

namespace DAW {

class MidiInputCore : public juce::MidiInputCallback,
                      private TransportController::Listener,
                      private juce::Timer
{
public:
    MidiInputCore()
    {
        startTimerHz(60);
    }

    ~MidiInputCore() override
    {
        shutdown();
    }

    void setSubsystems(TrackManager* tracks,
                       ClipManager* clips,
                       ApplicationState* state,
                       TransportController* transport,
                       VirtualMidiKeyboardCore* keyboard)
    {
        tracks_ = tracks;
        clips_ = clips;
        state_ = state;
        keyboard_ = keyboard;

        if (transport_ != transport)
        {
            if (transport_ != nullptr)
                transport_->removeListener(this);
            transport_ = transport;
            if (transport_ != nullptr)
                transport_->addListener(this);
        }
    }

    void prepare(double sampleRate, int samplesPerBlock)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
        collector_.reset(sampleRate_);
        currentBlockSize_ = juce::jmax(1, samplesPerBlock);
    }

    void shutdown()
    {
        if (transport_ != nullptr)
        {
            transport_->removeListener(this);
            transport_ = nullptr;
        }

        for (auto& input : openInputs_)
            if (input != nullptr)
                input->stop();

        openInputs_.clear();
        enabledDeviceIds_.clear();
        queuedRecordEvents_.clear();
        pendingNotes_.clear();
        recordTargetClip_ = nullptr;
        recordTargetTrackId_.clear();
        activeMidiTakes_.clear();
    }

    juce::StringArray getAvailableInputDevices() const
    {
        juce::StringArray names;
        for (const auto& dev : juce::MidiInput::getAvailableDevices())
            names.add(dev.name);
        return names;
    }

    void setMidiInputEnabled(const juce::String& deviceIdentifier, bool enabled)
    {
        if (deviceIdentifier.isEmpty())
            return;

        if (enabled)
        {
            if (enabledDeviceIds_.contains(deviceIdentifier))
                return;

            auto device = juce::MidiInput::openDevice(deviceIdentifier, this);
            if (device != nullptr)
            {
                device->start();
                enabledDeviceIds_.add(deviceIdentifier);
                openInputs_.push_back(std::move(device));
            }
        }
        else
        {
            for (int i = (int) openInputs_.size() - 1; i >= 0; --i)
            {
                if (openInputs_[(size_t) i] != nullptr
                    && openInputs_[(size_t) i]->getIdentifier() == deviceIdentifier)
                {
                    openInputs_[(size_t) i]->stop();
                    openInputs_.erase(openInputs_.begin() + i);
                }
            }

            enabledDeviceIds_.removeString(deviceIdentifier);
        }
    }

    void setAllAvailableInputsEnabled(bool enabled)
    {
        for (const auto& dev : juce::MidiInput::getAvailableDevices())
            setMidiInputEnabled(dev.identifier, enabled);
    }

    void processBlock(const juce::String& trackId,
                      juce::MidiBuffer& midiOut,
                      int numSamples) noexcept
    {
        juce::ignoreUnused(trackId);
        collector_.removeNextBlockOfMessages(midiOut, numSamples);
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        if (keyboard_ == nullptr)
            return;

        const auto targetTrackId = getPreferredTargetTrackId();
        if (targetTrackId.isEmpty())
            return;

        keyboard_->setActiveTrackId(targetTrackId);
        collector_.addMessageToQueue(message);

        if (isRecording_.load(std::memory_order_acquire))
        {
            const juce::ScopedLock sl(recordLock_);
            queuedRecordEvents_.push_back({ message, transport_ != nullptr ? transport_->getPosition() : 0, targetTrackId });
        }
    }

private:
    struct PendingNote
    {
        juce::int64 startSamples = 0;
        juce::uint8 pitch = 60;
        juce::uint8 velocity = 100;
        juce::uint8 channel = 0;
        juce::String trackId;
    };

    struct QueuedRecordEvent
    {
        juce::MidiMessage message;
        juce::int64 samplePosition = 0;
        juce::String trackId;
    };

    juce::String getPreferredTargetTrackId() const
    {
        if (tracks_ == nullptr)
            return {};

        if (state_ != nullptr)
        {
            auto selected = state_->selectedTrackID.getValue().toString();
            if (auto* track = tracks_->getTrack(selected))
                if (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument)
                    return selected;
        }

        for (int i = 0; i < tracks_->getNumTracks(); ++i)
        {
            if (auto* track = tracks_->getTrack(i))
            {
                if (track->isArmed() && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
                    return track->getID();
            }
        }

        return {};
    }

    MidiClip* getSelectedMidiClipForTrack(const juce::String& trackId) const
    {
        if (clips_ == nullptr || state_ == nullptr)
            return nullptr;

        auto selectedClipId = state_->selectedClipID.getValue().toString();
        if (selectedClipId.isEmpty())
            return nullptr;

        auto* clip = dynamic_cast<MidiClip*>(clips_->getClip(selectedClipId));
        if (clip == nullptr || clip->getTrackID() != trackId)
            return nullptr;

        return clip;
    }

    MidiClip* ensureRecordTargetClip(const juce::String& trackId)
    {
        if (clips_ == nullptr || transport_ == nullptr || trackId.isEmpty())
            return nullptr;

        if (recordTargetClip_ != nullptr && recordTargetTrackId_ == trackId)
            return recordTargetClip_;

        if (auto* selectedClip = getSelectedMidiClipForTrack(trackId))
        {
            trackMidiTakeTarget(selectedClip, false);
            recordTargetClip_ = selectedClip;
            recordTargetTrackId_ = trackId;
            return recordTargetClip_;
        }

        auto* clip = clips_->createMIDIClip("MIDI Rec");
        if (clip == nullptr)
            return nullptr;

        clip->setTrackID(trackId);
        clip->setStartPosition(recordOriginSamples_);
        clip->setTempo(transport_->getTempo());
        clip->setSampleRate(sampleRate_);
        clip->getPianoRollModel().setLengthTicks(1);
        clip->syncLengthFromModel();

        if (tracks_ != nullptr)
            if (auto* track = tracks_->getTrack(trackId))
                clip->setColor(track->getColor().brighter(0.1f));

        if (state_ != nullptr)
            state_->selectedClipID.setValue(clip->getID());

        trackMidiTakeTarget(clip, true);
        recordTargetClip_ = clip;
        recordTargetTrackId_ = trackId;
        return recordTargetClip_;
    }

    /** Register a clip touched by this record pass (once per clip) so the
     *  whole pass can land on the unified undo history as one step. */
    void trackMidiTakeTarget(MidiClip* clip, bool isNewClip)
    {
        if (clip == nullptr)
            return;

        for (const auto& t : activeMidiTakes_)
            if (t.currentId == clip->getID())
                return;

        RecordMidiTakeCommand::Take take;
        take.isNewClip = isNewClip;
        take.currentId = clip->getID();
        if (!isNewClip)
            take.preState = clip->getState();
        activeMidiTakes_.push_back(std::move(take));
    }

    /** Record stop: snapshot post-states and push ONE undoable history step
     *  (pro-DAW model: Ctrl+Z removes/reverts the MIDI take, redo restores). */
    void commitMidiTakeHistory()
    {
        if (clips_ == nullptr || activeMidiTakes_.empty())
        {
            activeMidiTakes_.clear();
            return;
        }

        std::vector<RecordMidiTakeCommand::Take> takes;
        takes.reserve(activeMidiTakes_.size());

        for (auto& t : activeMidiTakes_)
        {
            auto* clip = clips_->getClip(t.currentId);
            if (clip == nullptr)
                continue;

            t.postState = clip->getState();

            // Skip no-op takes (armed pass that didn't change the clip).
            if (!t.isNewClip && t.preState.isEquivalentTo(t.postState))
                continue;

            takes.push_back(std::move(t));
        }

        activeMidiTakes_.clear();

        if (!takes.empty())
            CommandManager::getInstance().execute(
                std::make_unique<RecordMidiTakeCommand>(*clips_, std::move(takes)));
    }

    void flushPendingNotes(juce::int64 endSamplePosition)
    {
        if (recordTargetClip_ == nullptr)
        {
            pendingNotes_.clear();
            return;
        }

        for (const auto& pending : pendingNotes_)
            commitRecordedNote(pending, endSamplePosition, 64);

        pendingNotes_.clear();
    }

    void commitRecordedNote(const PendingNote& pending, juce::int64 endSamplePosition, int releaseVelocity)
    {
        if (recordTargetClip_ == nullptr)
            return;

        auto noteStart = juce::jmax<juce::int64>(0, pending.startSamples - recordTargetClip_->getStartPosition());
        auto noteEnd = juce::jmax<juce::int64>(pending.startSamples + 1, endSamplePosition - recordTargetClip_->getStartPosition());
        auto startTick = recordTargetClip_->samplesToTicks(noteStart);
        auto endTick = juce::jmax(startTick + 1, recordTargetClip_->samplesToTicks(noteEnd));

        PianoRollClipModel::NoteEvent note;
        note.startTick = startTick;
        note.lengthTicks = juce::jmax<juce::int64>(1, endTick - startTick);
        note.pitch = pending.pitch;
        note.velocity = pending.velocity;
        note.releaseVel = (juce::uint8) juce::jlimit(0, 127, releaseVelocity);
        note.channel = pending.channel;
        note.selected = false;

        auto& model = recordTargetClip_->getPianoRollModel();
        model.addNote(note);
        model.setLengthTicks(juce::jmax(model.getLengthTicks(), endTick + model.getPPQ()));
        recordTargetClip_->syncLengthFromModel();
    }

    void processQueuedRecordEvents()
    {
        std::vector<QueuedRecordEvent> events;
        {
            const juce::ScopedLock sl(recordLock_);
            if (queuedRecordEvents_.empty())
                return;
            events.swap(queuedRecordEvents_);
        }

        for (const auto& event : events)
        {
            auto* targetClip = ensureRecordTargetClip(event.trackId);
            if (targetClip == nullptr)
                continue;

            recordTargetClip_ = targetClip;
            recordTargetTrackId_ = event.trackId;

            if (event.message.isNoteOn())
            {
                PendingNote pending;
                pending.startSamples = event.samplePosition;
                pending.pitch = (juce::uint8) event.message.getNoteNumber();
                pending.velocity = (juce::uint8) juce::jlimit(1, 127, juce::roundToInt(event.message.getVelocity() * 127.0f));
                pending.channel = (juce::uint8) juce::jlimit(0, 15, event.message.getChannel() - 1);
                pending.trackId = event.trackId;

                auto existing = std::find_if(pendingNotes_.begin(), pendingNotes_.end(),
                    [&](const PendingNote& note)
                    {
                        return note.pitch == pending.pitch && note.channel == pending.channel && note.trackId == pending.trackId;
                    });
                if (existing != pendingNotes_.end())
                    *existing = pending;
                else
                    pendingNotes_.push_back(pending);
            }
            else if (event.message.isNoteOff())
            {
                auto existing = std::find_if(pendingNotes_.begin(), pendingNotes_.end(),
                    [&](const PendingNote& note)
                    {
                        return note.pitch == event.message.getNoteNumber()
                            && note.channel == juce::jlimit(0, 15, event.message.getChannel() - 1)
                            && note.trackId == event.trackId;
                    });

                if (existing != pendingNotes_.end())
                {
                    commitRecordedNote(*existing,
                                       event.samplePosition,
                                       juce::roundToInt(event.message.getVelocity() * 127.0f));
                    pendingNotes_.erase(existing);
                }
            }
        }
    }

    void transportStateChanged() override
    {
        if (transport_ == nullptr)
            return;

        const bool nowRecording = transport_->isRecording();
        if (nowRecording && !isRecording_.load(std::memory_order_relaxed))
        {
            recordOriginSamples_ = transport_->getPosition();
            recordTargetClip_ = nullptr;
            recordTargetTrackId_.clear();
            pendingNotes_.clear();
            activeMidiTakes_.clear();
            isRecording_.store(true, std::memory_order_release);
        }
        else if (!nowRecording && isRecording_.load(std::memory_order_relaxed))
        {
            processQueuedRecordEvents();
            flushPendingNotes(transport_->getPosition());
            commitMidiTakeHistory();
            isRecording_.store(false, std::memory_order_release);
            recordTargetClip_ = nullptr;
            recordTargetTrackId_.clear();
        }

        if (!transport_->isPlaying() && keyboard_ != nullptr)
            keyboard_->allNotesOff();
    }

    void timerCallback() override
    {
        if (isRecording_.load(std::memory_order_acquire))
            processQueuedRecordEvents();
    }

    TrackManager* tracks_ = nullptr;
    ClipManager* clips_ = nullptr;
    ApplicationState* state_ = nullptr;
    TransportController* transport_ = nullptr;
    VirtualMidiKeyboardCore* keyboard_ = nullptr;
    juce::MidiMessageCollector collector_;
    double sampleRate_ = 44100.0;
    int currentBlockSize_ = 512;
    juce::StringArray enabledDeviceIds_;
    std::vector<std::unique_ptr<juce::MidiInput>> openInputs_;
    std::atomic<bool> isRecording_{ false };
    juce::CriticalSection recordLock_;
    std::vector<QueuedRecordEvent> queuedRecordEvents_;
    std::vector<PendingNote> pendingNotes_;
    juce::int64 recordOriginSamples_ = 0;
    MidiClip* recordTargetClip_ = nullptr;
    juce::String recordTargetTrackId_;
    std::vector<RecordMidiTakeCommand::Take> activeMidiTakes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputCore)
};

} // namespace DAW

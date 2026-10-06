#pragma once
#include "Command.h"
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../MidiCore/MidiClip.h"
#include "../MidiCore/PianoRollClipModel.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../ProjectCore/ProjectManager.h"

namespace DAW {

class TrackPropertyChangeCommand : public Command
{
public:
    enum class Property { Name, Muted, Soloed, Armed, Monitoring, Role, Color, Volume, Pan };

    TrackPropertyChangeCommand(TrackManager& trackManager,
                               TrackID trackId,
                               Property property,
                               juce::var oldValue,
                               juce::var newValue,
                               juce::String description,
                               bool alreadyApplied = false)
        : trackManager_(trackManager),
          trackId_(std::move(trackId)),
          property_(property),
          oldValue_(std::move(oldValue)),
          newValue_(std::move(newValue)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Track"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        apply(newValue_);
        hasExecuted_ = true;
    }
    void undo() override { apply(oldValue_); }

private:
    void apply(const juce::var& value)
    {
        if (auto* track = trackManager_.getTrack(trackId_))
        {
            switch (property_)
            {
                case Property::Name:   track->setName(value.toString()); break;
                case Property::Muted:  track->setMuted((bool) value); break;
                case Property::Soloed: track->setSoloed((bool) value); break;
                case Property::Armed:  track->setArmed((bool) value); break;
                case Property::Monitoring: track->setMonitoring((bool) value); break;
                case Property::Role: track->setRole((TrackRole) (int) value); break;
                case Property::Color: track->setColor(juce::Colour((juce::uint32) (int) value)); break;
                case Property::Volume: track->setVolume((float) (double) value); break;
                case Property::Pan: track->setPan((float) (double) value); break;
            }
        }
    }

    TrackManager& trackManager_;
    TrackID trackId_;
    Property property_;
    juce::var oldValue_;
    juce::var newValue_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class PluginChainStateCommand : public Command
{
public:
    PluginChainStateCommand(PluginChainCore& chain,
                            juce::AudioPluginFormatManager& formatManager,
                            juce::ValueTree beforeState,
                            juce::ValueTree afterState,
                            juce::String description,
                            bool alreadyApplied = false)
        : chain_(chain),
          formatManager_(formatManager),
          beforeState_(std::move(beforeState)),
          afterState_(std::move(afterState)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Plugin"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        chain_.restoreState(afterState_, formatManager_);
        hasExecuted_ = true;
    }
    void undo() override { chain_.restoreState(beforeState_, formatManager_); }

private:
    PluginChainCore& chain_;
    juce::AudioPluginFormatManager& formatManager_;
    juce::ValueTree beforeState_;
    juce::ValueTree afterState_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class AddTrackCommand : public Command
{
public:
    AddTrackCommand(TrackManager& trackManager,
                    juce::String name,
                    TrackRole role = TrackRole::Audio)
        : trackManager_(trackManager), name_(std::move(name)), role_(role) {}

    juce::String getDescription() const override
    {
        return role_ == TrackRole::MIDI || role_ == TrackRole::Instrument ? "Add MIDI Track" : "Add Track";
    }

    juce::String getCategory() const override { return "Track"; }

    void execute() override
    {
        auto* track = (role_ == TrackRole::MIDI || role_ == TrackRole::Instrument)
            ? trackManager_.createMidiTrack(name_)
            : trackManager_.createTrack(name_);

        if (track != nullptr)
        {
            if (role_ != TrackRole::Audio && role_ != TrackRole::MIDI && role_ != TrackRole::Instrument)
                track->setRole(role_);
            trackId_ = track->getID();
        }
    }

    void undo() override
    {
        if (trackId_.isNotEmpty())
            trackManager_.deleteTrack(trackId_);
    }

    const TrackID& getTrackId() const noexcept { return trackId_; }

private:
    TrackManager& trackManager_;
    juce::String name_;
    TrackRole role_ = TrackRole::Audio;
    TrackID trackId_;
};

class DeleteTrackCommand : public Command
{
public:
    DeleteTrackCommand(TrackManager& trackManager, TrackID trackId)
        : trackManager_(trackManager), trackId_(std::move(trackId))
    {
        if (auto* track = trackManager_.getTrack(trackId_))
            trackState_ = track->getState();
    }

    juce::String getDescription() const override { return "Delete Track"; }
    juce::String getCategory() const override { return "Track"; }

    void execute() override
    {
        if (auto* track = trackManager_.getTrack(trackId_))
            trackState_ = track->getState();
        trackManager_.deleteTrack(trackId_);
    }

    void undo() override
    {
        auto* track = trackManager_.recreateTrackFromState(trackState_);
        if (track != nullptr)
            trackId_ = track->getID();
    }

private:
    TrackManager& trackManager_;
    TrackID trackId_;
    juce::ValueTree trackState_;
};

class TrackReorderCommand : public Command
{
public:
    TrackReorderCommand(TrackManager& trackManager, TrackID trackId, int oldIndex, int newIndex, bool alreadyApplied = false)
        : trackManager_(trackManager), trackId_(std::move(trackId)), oldIndex_(oldIndex), newIndex_(newIndex), alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return "Move Track"; }
    juce::String getCategory() const override { return "Track"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        trackManager_.moveTrack(trackId_, newIndex_);
        hasExecuted_ = true;
    }
    void undo() override { trackManager_.moveTrack(trackId_, oldIndex_); }

private:
    TrackManager& trackManager_;
    TrackID trackId_;
    int oldIndex_ = -1;
    int newIndex_ = -1;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class AddClipCommand : public Command
{
public:
    AddClipCommand(ClipManager& clipManager,
                   TrackID trackId,
                   juce::String name,
                   ClipType type,
                   SamplePosition startPosition,
                   SamplePosition length,
                   juce::Colour color,
                   juce::File audioFile = {})
        : clipManager_(clipManager),
          trackId_(std::move(trackId)),
          name_(std::move(name)),
          type_(type),
          startPosition_(startPosition),
          length_(length),
          color_(color),
          audioFile_(std::move(audioFile)) {}

    juce::String getDescription() const override { return "Add Clip"; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        Clip* clip = nullptr;
        if (type_ == ClipType::Audio)
            clip = clipManager_.createAudioClip(name_, audioFile_);
        else
        {
            auto* midiClip = clipManager_.createMIDIClip(name_);
            clip = midiClip;
        }

        if (clip == nullptr)
            return;

        clipId_ = clip->getID();
        clip->setTrackID(trackId_);
        clip->setStartPosition(startPosition_);
        clip->setLength(length_);
        clip->setColor(color_);
    }

    void undo() override
    {
        if (clipId_.isNotEmpty())
            clipManager_.deleteClip(clipId_);
    }

    const ClipID& getClipId() const noexcept { return clipId_; }

private:
    ClipManager& clipManager_;
    TrackID trackId_;
    juce::String name_;
    ClipType type_ = ClipType::Audio;
    SamplePosition startPosition_ = 0;
    SamplePosition length_ = 0;
    juce::Colour color_;
    juce::File audioFile_;
    ClipID clipId_;
};

class ClipPropertyChangeCommand : public Command
{
public:
    enum class Property { Muted };

    ClipPropertyChangeCommand(ClipManager& clipManager,
                              ClipID clipId,
                              Property property,
                              juce::var oldValue,
                              juce::var newValue,
                              juce::String description)
        : clipManager_(clipManager),
          clipId_(std::move(clipId)),
          property_(property),
          oldValue_(std::move(oldValue)),
          newValue_(std::move(newValue)),
          description_(std::move(description)) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override { apply(newValue_); }
    void undo() override { apply(oldValue_); }

private:
    void apply(const juce::var& value)
    {
        if (auto* clip = clipManager_.getClip(clipId_))
        {
            switch (property_)
            {
                case Property::Muted: clip->setMuted((bool) value); break;
            }
        }
    }

    ClipManager& clipManager_;
    ClipID clipId_;
    Property property_;
    juce::var oldValue_;
    juce::var newValue_;
    juce::String description_;
};

class DeleteClipCommand : public Command
{
public:
    DeleteClipCommand(ClipManager& clipManager, ClipID clipId)
        : clipManager_(clipManager), clipId_(std::move(clipId))
    {
        captureState();
    }

    juce::String getDescription() const override { return "Delete Clip"; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        captureState();
        clipManager_.deleteClip(clipId_);
    }

    void undo() override
    {
        auto* clip = clipManager_.recreateClipFromState(clipState_);
        if (clip != nullptr)
            clipId_ = clip->getID();
    }

private:
    void captureState()
    {
        if (auto* clip = clipManager_.getClip(clipId_))
            clipState_ = clip->getState();
    }

    ClipManager& clipManager_;
    ClipID clipId_;
    juce::ValueTree clipState_;
};

class DuplicateClipCommand : public Command
{
public:
    DuplicateClipCommand(ClipManager& clipManager, ClipID sourceClipId)
        : clipManager_(clipManager), sourceClipId_(std::move(sourceClipId)) {}

    juce::String getDescription() const override { return "Duplicate Clip"; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        auto* source = clipManager_.getClip(sourceClipId_);
        if (source == nullptr)
            return;

        const auto newStart = source->getStartPosition() + source->getLength();
        const auto trackId  = source->getTrackID();
        const auto colour   = source->getColor();

        if (auto* clip = clipManager_.duplicateClip(sourceClipId_))
        {
            duplicatedClipId_ = clip->getID();
            clip->setStartPosition(newStart);
            clip->setTrackID(trackId);
            clip->setColor(colour);
        }
    }

    void undo() override
    {
        if (duplicatedClipId_.isNotEmpty())
            clipManager_.deleteClip(duplicatedClipId_);
    }

private:
    ClipManager& clipManager_;
    ClipID sourceClipId_;
    ClipID duplicatedClipId_;
};

class ClipStateEditCommand : public Command
{
public:
    ClipStateEditCommand(ClipManager& clipManager,
                         ClipID clipId,
                         juce::ValueTree beforeState,
                         juce::ValueTree afterState,
                         juce::String description = "Edit Clip",
                         bool alreadyApplied = false)
        : clipManager_(clipManager),
          clipId_(std::move(clipId)),
          beforeState_(std::move(beforeState)),
          afterState_(std::move(afterState)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        if (auto* clip = clipManager_.getClip(clipId_))
            clip->restoreState(afterState_);
        hasExecuted_ = true;
    }

    void undo() override
    {
        if (auto* clip = clipManager_.getClip(clipId_))
            clip->restoreState(beforeState_);
    }

private:
    ClipManager& clipManager_;
    ClipID clipId_;
    juce::ValueTree beforeState_;
    juce::ValueTree afterState_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class MultiClipStateEditCommand : public Command
{
public:
    struct ClipStatePair
    {
        ClipID clipId;
        juce::ValueTree beforeState;
        juce::ValueTree afterState;
    };

    MultiClipStateEditCommand(ClipManager& clipManager,
                              std::vector<ClipStatePair> states,
                              juce::String description = "Edit Clips",
                              bool alreadyApplied = false)
        : clipManager_(clipManager),
          states_(std::move(states)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        applyAfter();
        hasExecuted_ = true;
    }

    void undo() override { applyBefore(); }

private:
    void applyBefore()
    {
        for (const auto& state : states_)
            if (auto* clip = clipManager_.getClip(state.clipId))
                clip->restoreState(state.beforeState);
    }

    void applyAfter()
    {
        for (const auto& state : states_)
            if (auto* clip = clipManager_.getClip(state.clipId))
                clip->restoreState(state.afterState);
    }

    ClipManager& clipManager_;
    std::vector<ClipStatePair> states_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class MultiDeleteClipCommand : public Command
{
public:
    MultiDeleteClipCommand(ClipManager& clipManager, std::vector<ClipID> clipIds)
        : clipManager_(clipManager), clipIds_(std::move(clipIds))
    {
        captureStates();
    }

    juce::String getDescription() const override { return "Delete Clips"; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        captureStates();
        for (const auto& clipId : clipIds_)
            clipManager_.deleteClip(clipId);
    }

    void undo() override
    {
        clipIds_.clear();
        for (const auto& state : clipStates_)
            if (auto* clip = clipManager_.recreateClipFromState(state))
                clipIds_.push_back(clip->getID());
    }

private:
    void captureStates()
    {
        clipStates_.clear();
        for (const auto& clipId : clipIds_)
            if (auto* clip = clipManager_.getClip(clipId))
                clipStates_.push_back(clip->getState());
    }

    ClipManager& clipManager_;
    std::vector<ClipID> clipIds_;
    std::vector<juce::ValueTree> clipStates_;
};

class PianoRollStateCommand : public Command
{
public:
    PianoRollStateCommand(PianoRollClipModel& model,
                          juce::ValueTree beforeState,
                          juce::ValueTree afterState,
                          juce::String description = "Edit MIDI",
                          bool alreadyApplied = false)
        : model_(model),
          beforeState_(std::move(beforeState)),
          afterState_(std::move(afterState)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "MIDI"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        model_.fromValueTree(afterState_);
        hasExecuted_ = true;
    }
    void undo() override { model_.fromValueTree(beforeState_); }

private:
    PianoRollClipModel& model_;
    juce::ValueTree beforeState_;
    juce::ValueTree afterState_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

class ImportAudioFilesCommand : public Command
{
public:
    ImportAudioFilesCommand(TrackManager& trackManager,
                            ClipManager& clipManager,
                            AudioFileManager& audioFileManager,
                            juce::Array<juce::File> files,
                            TrackID selectedTrackId,
                            SamplePosition insertPosition,
                            double engineSampleRate)
        : trackManager_(trackManager),
          clipManager_(clipManager),
          audioFileManager_(audioFileManager),
          files_(std::move(files)),
          selectedTrackId_(std::move(selectedTrackId)),
          insertPosition_(insertPosition),
          engineSampleRate_(engineSampleRate) {}

    juce::String getDescription() const override { return files_.size() > 1 ? "Import Audio Files" : "Import Audio File"; }
    juce::String getCategory() const override { return "Clip"; }

    void execute() override
    {
        if (!createdTrackStates_.isEmpty() || !createdClipStates_.isEmpty())
        {
            for (auto& state : createdTrackStates_)
                trackManager_.recreateTrackFromState(state);

            for (auto& state : createdClipStates_)
            {
                if (auto* clip = clipManager_.recreateClipFromState(state))
                {
                    auto sourceFile = juce::File(state.getProperty("sourceFile", {}).toString());
                    if (sourceFile.existsAsFile())
                        audioFileManager_.loadForClip(clip->getID(), sourceFile);
                }
            }
            return;
        }

        int baseTrackIdx = 0;
        for (int i = 0; i < trackManager_.getNumTracks(); ++i)
        {
            if (trackManager_.getTrack(i)->getID() == selectedTrackId_)
            {
                baseTrackIdx = i;
                break;
            }
        }

        for (int fi = 0; fi < files_.size(); ++fi)
        {
            auto file = files_[fi];
            if (!file.existsAsFile())
                continue;

            Track* targetTrack = nullptr;
            const int trackIdx = baseTrackIdx + fi;
            if (trackIdx < trackManager_.getNumTracks())
            {
                targetTrack = trackManager_.getTrack(trackIdx);
            }
            else
            {
                targetTrack = trackManager_.createTrack(file.getFileNameWithoutExtension());
                if (targetTrack != nullptr)
                    createdTrackStates_.add(targetTrack->getState());
            }

            if (targetTrack == nullptr)
                continue;

            auto* clip = clipManager_.createAudioClip(file.getFileNameWithoutExtension(), file);
            if (clip == nullptr)
                continue;

            clip->setTrackID(targetTrack->getID());
            clip->setStartPosition(insertPosition_);
            clip->setColor(targetTrack->getColor().brighter(0.1f));

            auto result = audioFileManager_.loadForClip(clip->getID(), file);
            if (result.success)
            {
                auto tl = (SamplePosition) std::round((double) result.numSamples * engineSampleRate_ / juce::jmax(1.0, result.sampleRate));
                clip->setLength(juce::jmax((SamplePosition) 1, tl));
            }

            createdClipStates_.add(clip->getState());
        }
    }

    void undo() override
    {
        for (auto& state : createdClipStates_)
        {
            auto clipId = state.getProperty("id", {}).toString();
            audioFileManager_.unloadClip(clipId);
            clipManager_.deleteClip(clipId);
        }

        for (int i = createdTrackStates_.size(); --i >= 0;)
        {
            auto trackId = createdTrackStates_.getReference(i).getProperty("id", {}).toString();
            trackManager_.deleteTrack(trackId);
        }
    }

private:
    TrackManager& trackManager_;
    ClipManager& clipManager_;
    AudioFileManager& audioFileManager_;
    juce::Array<juce::File> files_;
    TrackID selectedTrackId_;
    SamplePosition insertPosition_ = 0;
    double engineSampleRate_ = 44100.0;
    juce::Array<juce::ValueTree> createdTrackStates_;
    juce::Array<juce::ValueTree> createdClipStates_;
};

class ProjectTopologyStateCommand : public Command
{
public:
    ProjectTopologyStateCommand(ProjectManager& projectManager,
                                juce::ValueTree beforeState,
                                juce::ValueTree afterState,
                                juce::String description,
                                bool alreadyApplied = false)
        : projectManager_(projectManager),
          beforeState_(std::move(beforeState)),
          afterState_(std::move(afterState)),
          description_(std::move(description)),
          alreadyApplied_(alreadyApplied) {}

    juce::String getDescription() const override { return description_; }
    juce::String getCategory() const override { return "Routing"; }

    void execute() override
    {
        if (alreadyApplied_ && !hasExecuted_)
        {
            hasExecuted_ = true;
            return;
        }

        projectManager_.restoreFromState(afterState_);
        hasExecuted_ = true;
    }

    void undo() override
    {
        projectManager_.restoreFromState(beforeState_);
    }

private:
    ProjectManager& projectManager_;
    juce::ValueTree beforeState_;
    juce::ValueTree afterState_;
    juce::String description_;
    bool alreadyApplied_ = false;
    bool hasExecuted_ = false;
};

} // namespace DAW

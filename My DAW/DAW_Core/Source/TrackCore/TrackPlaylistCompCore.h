#pragma once

#include "Track.h"
#include "../ClipCore/Clip.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../CommandCore/Command.h"
#include <JuceHeader.h>
#include <vector>

namespace DAW
{
// Playlists share ONE track and its inserts/routing. All takes remain owned
// by the project ClipManager and reference original WAVs; no silent data loss.
struct TrackPlaylistCompCore final
{
    static void publishPlaybackVisibility(TrackManager& tracks, ClipManager& clips)
    {
        // Message thread only: audio callback reads only Clip's atomic flag.
        juce::ScopedLock lock(clips.getLock());
        for (auto* clip : clips.getAllClips())
        {
            if (!clip) continue;
            auto* track = tracks.getTrack(clip->getTrackID());
            clip->setPlaylistAudible(track == nullptr
                || clip->getPlaylistId() == track->getPlaybackPlaylistId());
        }
    }

    static juce::String createEmptyPlaylist(Track& track, TrackManager& tracks,
                                             ClipManager& clips)
    {
        const auto id = track.createPlaylist();
        publishPlaybackVisibility(tracks, clips);
        return id;
    }

    // A Selection is [start,end) in project samples, never a guessed pixel
    // interval or source-file region. Keep comp pieces in sample alignment.
    static juce::ValueTree sliceAudioClip(const juce::ValueTree& original,
                                         int64_t begin, int64_t end,
                                         double projectSampleRate,
                                         double sourceSampleRate)
    {
        if (!original.hasType("AudioClip") || end <= begin)
            return {};
        const int64_t base = (int64_t)original.getProperty("startPosition", (juce::int64)0);
        const int64_t length = (int64_t)original.getProperty("length", (juce::int64)0);
        if (begin < base || end > base + length)
            return {};
        auto result = original.createCopy();
        const int64_t delta = begin - base;
        const double ratio = sourceSampleRate > 0.0 && projectSampleRate > 0.0
            ? sourceSampleRate / projectSampleRate : 1.0;
        const int64_t sourceDelta = (int64_t)std::llround(delta * ratio);
        const int64_t sourceLength = (int64_t)std::llround((end - begin) * ratio);
        const int64_t sourceOffset = (int64_t)original.getProperty("sourceOffset", (juce::int64)0);
        const int64_t sourceStart = (int64_t)original.getProperty("sourceStartSample", (juce::int64)0);
        const int64_t sourceEnd = (int64_t)original.getProperty("sourceEndSample", (juce::int64)0);

        result.setProperty("startPosition", (juce::int64)begin, nullptr);
        result.setProperty("length", (juce::int64)(end - begin), nullptr);
        result.setProperty("sourceOffset", (juce::int64)(sourceOffset + sourceDelta), nullptr);
        if (sourceEnd > sourceStart)
        {
            const int64_t first = sourceStart + sourceDelta;
            result.setProperty("sourceStartSample", (juce::int64)first, nullptr);
            result.setProperty("sourceEndSample",
                               (juce::int64)juce::jmin(sourceEnd, first + sourceLength), nullptr);
        }
        result.setProperty("fadeInLength",
            (juce::int64)juce::jmin((int64_t)original.getProperty("fadeInLength", (juce::int64)0),
                                   (int64_t)(end - begin)), nullptr);
        result.setProperty("fadeOutLength",
            (juce::int64)juce::jmin((int64_t)original.getProperty("fadeOutLength", (juce::int64)0),
                                   (int64_t)(end - begin)), nullptr);
        return result;
    }
};

// One undo action for every range promotion. Source clips and WAV files are
// never moved or deleted. Reconstructs only the chosen TARGET playlist, not
// the rest of the session, using stable clip IDs for Undo/Redo.
class PromotePlaylistSelectionCommand final : public Command
{
public:
    PromotePlaylistSelectionCommand(TrackID trackId, juce::String sourceId,
                                    juce::String targetId, int64_t start, int64_t end,
                                    double projectRate, TrackManager& tracks,
                                    ClipManager& clips, AudioFileManager& audioFiles)
        : trackId_(std::move(trackId)), sourceId_(std::move(sourceId)),
          targetId_(std::move(targetId)), start_(start), end_(end),
          rate_(projectRate), tracks_(tracks), clips_(clips), audioFiles_(audioFiles)
    {
        if (start_ >= end_ || sourceId_ == targetId_ || rate_ <= 0.0)
            return;
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return;
        bool sourceExists = false, targetExists = false;
        for (auto& p : track->getPlaylists())
        {
            sourceExists |= p.id == sourceId_;
            targetExists |= p.id == targetId_;
        }
        if (!sourceExists || !targetExists) return;

        const auto all = clips_.getClipsOnTrack(trackId_);
        for (auto* clip : all)
        {
            if (!clip || clip->getType() != ClipType::Audio) continue;
            auto* audio = dynamic_cast<AudioClip*>(clip);
            if (!audio) continue;
            if (clip->getPlaylistId() == targetId_
                && clip->getStartPosition() < end_
                && clip->getEndPosition() > start_)
                before_.push_back(clip->getState());
        }
        after_ = before_;
        // Remove only selected target interval; preserve unselected parts.
        after_.clear();
        for (const auto& old : before_)
        {
            const auto a = (int64_t)old.getProperty("startPosition", (juce::int64)0);
            const auto b = a + (int64_t)old.getProperty("length", (juce::int64)0);
            if (b <= start_ || a >= end_)
            {
                after_.push_back(old.createCopy());
                continue;
            }
            const auto path = juce::File(old.getProperty("sourceFile").toString());
            const double sourceRate = readSourceRate(path);
            // Keep the original ID on at least one surviving side so clip
            // plugins and their editors do not lose their owner when comping.
            bool originalRetained = false;
            if (a < start_)
            {
                auto left = TrackPlaylistCompCore::sliceAudioClip(old, a, start_, rate_, sourceRate);
                if (left.isValid())
                {
                    after_.push_back(left); // preserve original stable clip ID
                    originalRetained = true;
                }
            }
            if (b > end_)
            {
                auto right = TrackPlaylistCompCore::sliceAudioClip(old, end_, b, rate_, sourceRate);
                if (right.isValid())
                {
                    if (originalRetained)
                        right.setProperty("id", "COMP_" + juce::Uuid().toString(), nullptr);
                    // Otherwise preserve the original ID on the right side.
                    after_.push_back(right);
                }
            }
        }
        // Copy all overlapping source pieces to their EXACT main time.
        for (auto* clip : all)
        {
            if (!clip || clip->getPlaylistId() != sourceId_) continue;
            auto* audio = dynamic_cast<AudioClip*>(clip);
            if (!audio) continue;
            const int64_t a = clip->getStartPosition(), b = clip->getEndPosition();
            const auto from = juce::jmax(a, start_), to = juce::jmin(b, end_);
            if (from >= to) continue;
            auto state = TrackPlaylistCompCore::sliceAudioClip(
                clip->getState(), from, to, rate_, readSourceRate(audio->getSourceFile()));
            if (!state.isValid()) continue;
            state.setProperty("id", "COMP_" + juce::Uuid().toString(), nullptr);
            state.setProperty("playlistId", targetId_, nullptr);
            after_.push_back(state);
            ++promoted_;
        }
        valid_ = promoted_ > 0;
    }

    bool isValid() const noexcept { return valid_; }
    int getPromotedCount() const noexcept { return promoted_; }
    juce::String getDescription() const override { return "Promote Playlist Selection"; }
    juce::String getCategory() const override { return "Comping"; }
    void execute() override { if (valid_) apply(after_); }
    void undo() override { if (valid_) apply(before_); }

private:
    double readSourceRate(const juce::File& file) const
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
        return reader != nullptr && reader->sampleRate > 0 ? reader->sampleRate : rate_;
    }
    void apply(const std::vector<juce::ValueTree>& replacement)
    {
        // Stable IDs are crucial: replacing even an UNCHANGED target clip
        // destroys its Vocal Tune and clip-FX state on clipRemoved(). Keep
        // original owners alive when only their bounds changed.
        juce::StringArray managedIds, nextIds;
        for (const auto& state : before_)
            managedIds.addIfNotAlreadyThere(state.getProperty("id").toString());
        for (const auto& state : after_)
            managedIds.addIfNotAlreadyThere(state.getProperty("id").toString());
        for (const auto& state : replacement)
            nextIds.addIfNotAlreadyThere(state.getProperty("id").toString());

        // Delete only actual outgoing pieces. The complete original WAV
        // survives on disk and the source take's playlist is never touched.
        for (const auto& id : managedIds)
        {
            if (nextIds.contains(id))
                continue;
            if (auto* clip = clips_.getClip(id))
                if (clip->getTrackID() == trackId_ && clip->getPlaylistId() == targetId_)
                    clips_.deleteClip(id);
        }

        for (const auto& state : replacement)
        {
            const auto id = state.getProperty("id").toString();
            if (auto* existing = clips_.getClip(id))
            {
                if (existing->getTrackID() == trackId_
                    && existing->getPlaylistId() == targetId_)
                {
                    existing->restoreState(state);
                    existing->notifyStateRestored();
                }
                continue;
            }
            auto* clip = clips_.recreateClipFromStateWithId(state, id);
            if (auto* audio = dynamic_cast<AudioClip*>(clip))
                if (audio->getSourceFile().existsAsFile())
                    audioFiles_.loadForClip(id, audio->getSourceFile(), false);
        }
        TrackPlaylistCompCore::publishPlaybackVisibility(tracks_, clips_);
    }

    TrackID trackId_;
    juce::String sourceId_, targetId_;
    int64_t start_, end_;
    double rate_;
    TrackManager& tracks_;
    ClipManager& clips_;
    AudioFileManager& audioFiles_;
    std::vector<juce::ValueTree> before_, after_;
    int promoted_ = 0;
    bool valid_ = false;
};
} // namespace DAW

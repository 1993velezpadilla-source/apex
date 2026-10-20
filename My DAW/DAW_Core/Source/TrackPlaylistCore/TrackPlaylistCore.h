#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>
#include "../ClipCore/Clip.h"
#include "../AudioEngineCore/AudioFileManager.h"

namespace DAW
{
// APEX Track Playlists. Inactive takes live ONLY in this project-scoped
// control-plane store, not the audio graph and not hidden muted tracks.
// All access is on the JUCE message thread. Recorded WAV files are NEVER
// removed when switching, comping, undoing or discarding a playlist.
class TrackPlaylistCore
{
public:
    struct Playlist
    {
        juce::String name;
        std::vector<juce::ValueTree> clips;
    };

    struct TrackState
    {
        std::vector<Playlist> alternates;
        std::vector<juce::ValueTree> main;  // held while auditioning an alternate
        int activeAlternate = -1;           // -1 = live Main
    };

    int getNumAlternates(const TrackID& id) const
    {
        auto it = tracks_.find(id);
        return it == tracks_.end() ? 0 : (int)it->second.alternates.size();
    }

    juce::String getName(const TrackID& id, int index) const
    {
        auto it = tracks_.find(id);
        if (it == tracks_.end() || index < 0 || index >= (int)it->second.alternates.size())
            return {};
        return it->second.alternates[(size_t)index].name;
    }

    int activeAlternate(const TrackID& id) const
    {
        auto it = tracks_.find(id);
        return it == tracks_.end() ? -1 : it->second.activeAlternate;
    }

    void clear() { tracks_.clear(); }

    void removeTrack(const TrackID& id) { tracks_.erase(id); }

    // Automatically create one alternate per previous overlapping recording.
    // This is invoked on the message thread AFTER WAV publication but BEFORE
    // the new take clip is inserted. Unaffected clips stay where they are.
    // Returns displaced snapshots for Undo, which reconstitutes the old pass.
    std::vector<juce::ValueTree> archiveForRecordedPass(
        const TrackID& trackId, const juce::String& trackName,
        SamplePosition start, SamplePosition length, ClipManager& clips)
    {
        std::vector<juce::ValueTree> displaced;
        if (trackId.isEmpty() || length <= 0)
            return displaced;

        // Never run this in the audio callback or while another playlist is
        // being auditioned; playback must first be returned to Main.
        auto& owner = tracks_[trackId];
        if (owner.activeAlternate >= 0)
            return displaced;

        const auto all = clips.getClipsOnTrack(trackId);
        for (auto* clip : all)
        {
            auto* audio = dynamic_cast<AudioClip*>(clip);
            if (audio == nullptr) continue;
            const auto end = start + length;
            if (audio->getStartPosition() < end && audio->getEndPosition() > start)
                displaced.push_back(audio->getState().createCopy());
        }

        if (displaced.empty())
            return displaced;

        Playlist previous;
        previous.name = nextName(owner, trackName);
        // A complete prior arrangement is available for instant A/B.
        previous.clips = captureTrack(trackId, clips);
        owner.alternates.push_back(std::move(previous));
        for (const auto& saved : displaced)
            clips.deleteClip(saved.getProperty("id").toString());

        return displaced;
    }

    // Manually save a full playlist of the current take arrangement.
    int duplicateCurrent(const TrackID& id, const juce::String& name,
                         const ClipManager& clips)
    {
        auto& owner = tracks_[id];
        Playlist playlist;
        playlist.name = nextName(owner, name);
        playlist.clips = captureTrack(id, clips);
        owner.alternates.push_back(std::move(playlist));
        return (int)owner.alternates.size() - 1;
    }

    // Safe manual new empty playlist; stores the previous Main first.
    // No user audio file is touched.
    bool newEmpty(const TrackID& id, const juce::String& name,
                  ClipManager& clips, AudioFileManager& audioFiles)
    {
        auto& owner = tracks_[id];
        if (owner.activeAlternate >= 0) return false;
        const auto snapshot = captureTrack(id, clips);
        if (!snapshot.empty())
        {
            Playlist previous;
            previous.name = nextName(owner, name);
            previous.clips = snapshot;
            owner.alternates.push_back(std::move(previous));
        }
        // A new empty main has no clips but the selected track, fader, FX,
        // sends and routing stay exactly as before.
        return replaceLive(id, {}, clips, audioFiles);
    }

    bool activate(const TrackID& id, int requestedAlternate,
                  ClipManager& clips, AudioFileManager& audioFiles)
    {
        auto it = tracks_.find(id);
        if (it == tracks_.end()) return requestedAlternate == -1;
        auto& owner = it->second;
        if (requestedAlternate < -1 || requestedAlternate >= (int)owner.alternates.size())
            return false;
        if (owner.activeAlternate == requestedAlternate)
            return true;

        const auto current = captureTrack(id, clips);
        std::vector<juce::ValueTree> selected;
        if (requestedAlternate < 0) selected = clone(owner.main);
        else selected = clone(owner.alternates[(size_t)requestedAlternate].clips);

        // Snapshot BEFORE destroying anything: on failure the current take
        // remains recoverable through the project/playlist record.
        if (owner.activeAlternate == -1) owner.main = current;
        else owner.alternates[(size_t)owner.activeAlternate].clips = current;

        if (!replaceLive(id, selected, clips, audioFiles))
            return false;
        owner.activeAlternate = requestedAlternate;
        return true;
    }

    // Copy part of an alternate playlist into Main, at the same sample time.
    // The original take is unchanged; trims adjust only the source offset and
    // output clip length (no DSP, resampling or file writes).
    bool compRange(const TrackID& id, int alternate, SamplePosition from,
                   SamplePosition to, ClipManager& clips,
                   AudioFileManager& audioFiles)
    {
        auto it = tracks_.find(id);
        if (it == tracks_.end() || alternate < 0 ||
            alternate >= (int)it->second.alternates.size() || from >= to)
            return false;
        auto& owner = it->second;

        auto source = clone(owner.alternates[(size_t)alternate].clips);
        auto main = owner.activeAlternate < 0
            ? captureTrack(id, clips) : clone(owner.main);

        std::vector<juce::ValueTree> comped;
        bool hasSource = false;
        for (const auto& original : main)
            appendOutsideRange(original, from, to, comped);
        for (const auto& original : source)
        {
            const auto a = (SamplePosition)(juce::int64)original.getProperty("startPosition", 0);
            const auto b = a + (SamplePosition)(juce::int64)original.getProperty("length", 0);
            const auto first = juce::jmax(a, from);
            const auto last = juce::jmin(b, to);
            if (first >= last) continue;
            comped.push_back(slice(original, first, last));
            hasSource = true;
        }
        if (!hasSource) return false;

        // If listening to an alternate, apply the comp to saved Main, then
        // bring Main back into view. Alternates remain immutable except
        // when deliberately auditioned/edited.
        if (owner.activeAlternate >= 0)
        {
            owner.main = std::move(comped);
            return activate(id, -1, clips, audioFiles);
        }
        return replaceLive(id, comped, clips, audioFiles);
    }

    juce::ValueTree getState() const
    {
        juce::ValueTree root("TrackPlaylists");
        root.setProperty("version", 1, nullptr);
        for (const auto& pair : tracks_)
        {
            juce::ValueTree track("PlaylistTrack");
            track.setProperty("trackId", pair.first, nullptr);
            track.setProperty("activeAlternate", pair.second.activeAlternate, nullptr);
            juce::ValueTree main("MainPlaylist");
            for (const auto& clip : pair.second.main)
                main.addChild(clip.createCopy(), -1, nullptr);
            track.addChild(main, -1, nullptr);
            for (const auto& playlist : pair.second.alternates)
            {
                juce::ValueTree lane("Playlist");
                lane.setProperty("name", playlist.name, nullptr);
                for (const auto& clip : playlist.clips)
                    lane.addChild(clip.createCopy(), -1, nullptr);
                track.addChild(lane, -1, nullptr);
            }
            root.addChild(track, -1, nullptr);
        }
        return root;
    }

    void restoreState(const juce::ValueTree& root)
    {
        tracks_.clear();
        if (!root.hasType("TrackPlaylists")) return;
        for (int i = 0; i < root.getNumChildren(); ++i)
        {
            auto node = root.getChild(i);
            if (!node.hasType("PlaylistTrack")) continue;
            const TrackID id = node.getProperty("trackId").toString();
            if (id.isEmpty()) continue;
            TrackState state;
            for (int j = 0; j < node.getNumChildren(); ++j)
            {
                auto part = node.getChild(j);
                if (part.hasType("MainPlaylist"))
                    for (int k = 0; k < part.getNumChildren(); ++k)
                        if (part.getChild(k).hasType("AudioClip"))
                            state.main.push_back(part.getChild(k).createCopy());
                if (part.hasType("Playlist"))
                {
                    Playlist lane;
                    lane.name = part.getProperty("name").toString();
                    for (int k = 0; k < part.getNumChildren(); ++k)
                        if (part.getChild(k).hasType("AudioClip"))
                            lane.clips.push_back(part.getChild(k).createCopy());
                    state.alternates.push_back(std::move(lane));
                }
            }
            const int active = (int)node.getProperty("activeAlternate", -1);
            state.activeAlternate = active >= 0 && active < (int)state.alternates.size()
                ? active : -1;
            tracks_[id] = std::move(state);
        }
    }

    static std::vector<juce::ValueTree> captureTrack(const TrackID& id, const ClipManager& clips)
    {
        std::vector<juce::ValueTree> result;
        for (auto* clip : clips.getClipsOnTrack(id))
            if (auto* audio = dynamic_cast<AudioClip*>(clip))
                result.push_back(audio->getState().createCopy());
        return result;
    }

private:
    std::map<TrackID, TrackState> tracks_;

    static juce::String nextName(const TrackState& owner, const juce::String& track)
    {
        return track + "." + juce::String((int)owner.alternates.size() + 1).paddedLeft('0', 2);
    }

    static std::vector<juce::ValueTree> clone(const std::vector<juce::ValueTree>& clips)
    {
        std::vector<juce::ValueTree> result;
        for (const auto& c : clips) result.push_back(c.createCopy());
        return result;
    }

    static juce::ValueTree slice(const juce::ValueTree& source,
                                 SamplePosition a, SamplePosition b)
    {
        auto result = source.createCopy();
        const auto start = (SamplePosition)(juce::int64)source.getProperty("startPosition", 0);
        const auto offset = (SamplePosition)(juce::int64)source.getProperty("sourceOffset", 0);
        result.setProperty("startPosition", (juce::int64)a, nullptr);
        result.setProperty("length", (juce::int64)(b - a), nullptr);
        result.setProperty("sourceOffset", (juce::int64)(offset + a - start), nullptr);
        // Fades in the source take are not transplanted onto a mid-phrase cut.
        result.setProperty("fadeInLength", (juce::int64)0, nullptr);
        result.setProperty("fadeOutLength", (juce::int64)0, nullptr);
        return result;
    }

    static void appendOutsideRange(const juce::ValueTree& clip, SamplePosition a,
                                   SamplePosition b, std::vector<juce::ValueTree>& out)
    {
        const auto start = (SamplePosition)(juce::int64)clip.getProperty("startPosition", 0);
        const auto end = start + (SamplePosition)(juce::int64)clip.getProperty("length", 0);
        if (end <= a || start >= b) { out.push_back(clip.createCopy()); return; }
        if (start < a) out.push_back(slice(clip, start, a));
        if (end > b) out.push_back(slice(clip, b, end));
    }

    static bool replaceLive(const TrackID& id,
                            const std::vector<juce::ValueTree>& snapshots,
                            ClipManager& clips, AudioFileManager& audioFiles)
    {
        // Validate every source prior to removing anything live. Old takes
        // are always still present in ProjectManager's persisted lanes.
        for (const auto& state : snapshots)
        {
            if (!state.hasType("AudioClip") ||
                state.getProperty("trackID").toString() != id ||
                !juce::File(state.getProperty("sourceFile").toString()).existsAsFile())
                return false;
        }
        auto current = clips.getClipsOnTrack(id);
        std::vector<ClipID> toRemove;
        for (auto* c : current)
            if (dynamic_cast<AudioClip*>(c) != nullptr)
                toRemove.push_back(c->getID());
        for (const auto& oldId : toRemove)
            clips.deleteClip(oldId);
        for (const auto& saved : snapshots)
        {
            // Fresh clip identity avoids collision with another live clip;
            // the underlying WAV file and edit state are unchanged.
            if (auto* clip = dynamic_cast<AudioClip*>(clips.recreateClipFromState(saved)))
                audioFiles.loadForClip(clip->getID(), clip->getSourceFile());
            else
                return false;
        }
        return true;
    }
};
} // namespace DAW

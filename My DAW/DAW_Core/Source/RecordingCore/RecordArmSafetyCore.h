#pragma once
#include <JuceHeader.h>
#include <vector>
#include "../UtilityCore/Types.h"
#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../TransportCore/TransportController.h"

namespace DAW {

class RecordArmSafetyCore
{
public:
    struct OverwriteWarning
    {
        TrackID      trackId;
        juce::String trackName;
        juce::String existingClipName;
    };

    static std::vector<OverwriteWarning> check(TrackManager* tracks,
                                                ClipManager* clips,
                                                SamplePosition playheadPos)
    {
        std::vector<OverwriteWarning> warnings;
        if (!tracks || !clips) return warnings;

        for (int i = 0; i < tracks->getNumTracks(); ++i)
        {
            auto* track = tracks->getTrack(i);
            if (!track || !track->isArmed()) continue;

            auto trackClips = clips->getClipsOnTrack(track->getID());
            for (auto* clip : trackClips)
            {
                if (!clip) continue;
                const auto start = clip->getStartPosition();
                const auto end = start + clip->getLength();
                if (playheadPos >= start && playheadPos < end)
                {
                    OverwriteWarning w;
                    w.trackId = track->getID();
                    w.trackName = track->getName();
                    w.existingClipName = clip->getName();
                    warnings.push_back(std::move(w));
                    break;
                }
            }
        }

        return warnings;
    }
};

} // namespace DAW

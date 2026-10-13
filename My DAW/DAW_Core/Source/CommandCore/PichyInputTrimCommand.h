#pragma once

#include "Command.h"
#include "../TrackCore/Track.h"

namespace DAW {

/**
 * An explicitly-authorized Pichy digital trim proposal, reversible through
 * the existing APEX CommandManager. Store identity rather than Track* so
 * Undo after deletion never dereferences an invalid track.
 *
 * IMPORTANT: this does not (and cannot) change hardware preamp gain.
 */
class PichyInputTrimCommand final : public Command
{
public:
    PichyInputTrimCommand(TrackManager& manager, TrackID id,
                          float beforeDb, float proposedDb)
        : manager_(manager), trackId_(std::move(id)),
          beforeDb_(beforeDb), proposedDb_(proposedDb) {}

    juce::String getDescription() const override
    {
        return "Pichy: adjust digital input trim";
    }

    juce::String getCategory() const override { return "Recording"; }

    void execute() override { apply(proposedDb_); }
    void undo() override { apply(beforeDb_); }

private:
    void apply(float db)
    {
        if (auto* track = manager_.getTrack(trackId_))
            track->setInputTrimDb(db);
    }

    TrackManager& manager_;
    TrackID trackId_;
    float beforeDb_ = 0.0f;
    float proposedDb_ = 0.0f;
};

} // namespace DAW

// APEX Project Settings > Reset Project
// Destructive "mix to zero" state planner. Mutates only an isolated copy of
// the complete project snapshot: the live project/audio graph is untouched
// until the caller has written a verified recovery backup.
#pragma once
#include <JuceHeader.h>
#include "../RoutingCore/RoutingConnection.h"
#include "../ClipCore/Clip.h"

namespace DAW
{
struct ProjectResetMixCore
{
    struct Counts
    {
        int tracks = 0;
        int audioClips = 0;
        int plugins = 0;
        int removedRoutes = 0;
    };

    // The original media source file, track membership, region timing, clip ID
    // and audio sample bounds MUST remain unchanged. Undoing manual trims is
    // not equivalent to resetting the mix and can destroy an arrangement.
    static bool prepare(const juce::ValueTree& original,
                        juce::ValueTree& reset, Counts& counts,
                        juce::String& error)
    {
        reset = {};
        counts = {};
        error.clear();
        if (!original.hasType("DAWProject")
            || !original.getChildWithName("Tracks").isValid()
            || !original.getChildWithName("Clips").isValid())
        {
            error = "Project snapshot is incomplete; nothing was reset.";
            return false;
        }

        reset = original.createCopy();
        auto tracks = reset.getChildWithName("Tracks");
        auto resetTrack = [&counts](juce::ValueTree track)
        {
            if (!track.hasType("Track")) return;
            ++counts.tracks;
            track.setProperty("volume", 1.0f, nullptr); // 0 dB unity, NOT silence
            track.setProperty("pan", 0.0f, nullptr);
            track.setProperty("muted", false, nullptr);
            track.setProperty("soloed", false, nullptr);
            track.setProperty("polarityInverted", false, nullptr);
            track.setProperty("inputTrimDb", 0.0f, nullptr);
            track.setProperty("automationVisible", false, nullptr);
            track.setProperty("automationMuted", false, nullptr);
            track.setProperty("automationActiveParameter", "track.volume", nullptr);
            track.setProperty("automationParameterOrder", "track.volume|track.pan", nullptr);
            track.setProperty("hasPlugins", false, nullptr);
            // Do NOT change arm/input assignment, track name, folder layout,
            // monitor source, user colors, or audio routing to Master.
        };
        for (int i = 0; i < tracks.getNumChildren(); ++i)
        {
            auto track = tracks.getChild(i);
            if (track.hasType("MasterTrack"))
            {
                for (int m = 0; m < track.getNumChildren(); ++m)
                    resetTrack(track.getChild(m));
            }
            else
                resetTrack(track);
        }

        auto clips = reset.getChildWithName("Clips");
        for (int i = 0; i < clips.getNumChildren(); ++i)
        {
            auto c = clips.getChild(i);
            c.setProperty("muted", false, nullptr);
            if (!c.hasType("AudioClip")) continue;
            ++counts.audioClips;
            c.setProperty("gain", 1.0f, nullptr);
            c.setProperty("clipPan", 0.0f, nullptr);
            c.setProperty("reversed", false, nullptr);
            c.setProperty("pitch", 0.0f, nullptr);
            c.setProperty("timeStretch", 1.0f, nullptr);
            c.setProperty("clipFineTune", 0.0f, nullptr);
            c.setProperty("fineTuneCents", 0.0f, nullptr);
            c.setProperty("formantSemitones", 0.0f, nullptr);
            c.setProperty("preserveFormants", false, nullptr);
            c.setProperty("timePitchMode", TimePitchModeIds::DefaultUserMode, nullptr);
            c.setProperty("transientLock", false, nullptr);
            c.setProperty("fadeInLength", (juce::int64)0, nullptr);
            c.setProperty("fadeOutLength", (juce::int64)0, nullptr);
            c.setProperty("fadeInCurve", 0, nullptr);
            c.setProperty("fadeOutCurve", 0, nullptr);
            // Vocal Tune is a clip-level processor, not part of PluginChains.
            // Remove only its known APEX-specific state subtrees; never media.
            for (int j = c.getNumChildren() - 1; j >= 0; --j)
            {
                auto child = c.getChild(j);
                if (child.getType().toString().containsIgnoreCase("ApexTune")
                    || child.getType().toString().containsIgnoreCase("VocalTune"))
                    c.removeChild(j, nullptr);
            }
        }

        // Empty containers with their original names. This covers the legacy
        // sample-time lanes, new PPQ lane store, plugin automations, tape-stop
        // curves, clip automation regions and all A/B automation memories.
        for (const auto* type : { "Automation", "APEXAutomation", "PluginChains",
                                   "ClipRegionPlugins" })
        {
            auto node = reset.getChildWithName(type);
            if (node.isValid())
            {
                if (juce::String(type) == "PluginChains")
                    counts.plugins = node.getNumChildren();
                node.removeAllChildren(nullptr);
                // APEXAutomation's key-registry must be regenerated from the
                // retained tracks at load, NOT restored from old plugin IDs.
                if (juce::String(type) == "APEXAutomation")
                    reset.removeChild(node, nullptr);
            }
        }

        // Remove auxiliary sends and sidechains but retain direct-to-master,
        // bus and folder connections so recorded vocals remain audible.
        if (auto routing = reset.getChildWithName("RoutingGraph"); routing.isValid())
        {
            auto connections = routing.getChildWithName("Connections");
            for (int i = connections.getNumChildren() - 1; i >= 0; --i)
            {
                auto connection = connections.getChild(i);
                const auto type = (int)connection.getProperty("type", 0);
                if (type == (int)ConnectionType::Send
                    || type == (int)ConnectionType::PreSend
                    || type == (int)ConnectionType::Sidechain)
                {
                    connections.removeChild(i, nullptr);
                    ++counts.removedRoutes;
                }
            }
        }

        // Mixer master ceiling and meter mode return to the usual APEX defaults.
        if (auto master = reset.getChildWithName("MasterPhaseD"); master.isValid())
        {
            master.setProperty("masterGain", 1.0f, nullptr);
            master.setProperty("ceilingMode", "SoftClip", nullptr);
            master.setProperty("ceilingDb", -0.1f, nullptr);
            master.setProperty("ditherMode", "Off", nullptr);
            master.setProperty("ditherBits", 24, nullptr);
            master.setProperty("meterDisplay", "PostFader", nullptr);
        }

        // Preserve song tempo, take position, source file paths, markers and
        // track hierarchy. Rewind the transport, not the recorded timeline.
        if (auto transport = reset.getChildWithName("Transport"); transport.isValid())
        {
            transport.setProperty("position", (juce::int64)0, nullptr);
            transport.setProperty("looping", false, nullptr);
        }
        return true;
    }
};
} // namespace DAW

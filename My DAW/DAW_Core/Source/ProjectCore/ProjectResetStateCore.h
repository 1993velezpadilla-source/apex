#pragma once
#include <JuceHeader.h>

namespace DAW
{
// Construct a project-scoped "raw recordings only" state WITHOUT changing
// original recordings, track identity, clip identity, edits or placement.
// Working on a deep copy permits validating the output before teardown.
struct ProjectResetStateCore
{
    static juce::ValueTree makeRawMix(const juce::ValueTree& original)
    {
        if (!original.hasType("DAWProject"))
            return {};

        auto project = original.createCopy();

        // Preserve all user recordings, file references, timeline positions,
        // clip boundaries, names, tempo and project identity. Strip the
        // processing properties only, not the musical arrangement.
        auto tracks = project.getChildWithName("Tracks");
        if (tracks.isValid())
        {
            for (int i = 0; i < tracks.getNumChildren(); ++i)
            {
                auto child = tracks.getChild(i);
                if (child.hasType("MasterTrack"))
                    child = child.getChild(0);
                if (!child.isValid() || !child.hasType("Track"))
                    continue;

                child.setProperty("volume", 1.0f, nullptr); // unity = 0 dB
                child.setProperty("pan", 0.0f, nullptr);
                child.setProperty("muted", false, nullptr);
                child.setProperty("soloed", false, nullptr);
                child.setProperty("armed", false, nullptr);
                child.setProperty("monitoring", false, nullptr);
                child.setProperty("recordMode", 0, nullptr);
                child.setProperty("polarityInverted", false, nullptr);
                child.setProperty("inputTrimDb", 0.0f, nullptr);
                child.setProperty("automationVisible", false, nullptr);
                child.setProperty("automationMuted", false, nullptr);
                child.setProperty("automationSnapToGrid", true, nullptr);
                child.setProperty("automationParameterOrder",
                                  "track.volume|track.pan|track.tape_stop", nullptr);
                child.setProperty("automationActiveParameter", "track.volume", nullptr);
                child.setProperty("hasPlugins", false, nullptr);
            }
        }

        auto clips = project.getChildWithName("Clips");
        if (clips.isValid())
        {
            for (int i = 0; i < clips.getNumChildren(); ++i)
            {
                auto clip = clips.getChild(i);
                // Clip positioning, full take bounds and file paths are
                // deliberately retained. Keep trimmed clips sounding in sync.
                clip.setProperty("muted", false, nullptr);
                clip.setProperty("selected", false, nullptr);

                if (clip.hasType("AudioClip"))
                {
                    clip.setProperty("gain", 1.0f, nullptr);
                    clip.setProperty("clipPan", 0.0f, nullptr);
                    clip.setProperty("reversed", false, nullptr);
                    clip.setProperty("pitch", 0.0f, nullptr);
                    clip.setProperty("timeStretch", 1.0f, nullptr);
                    clip.setProperty("fineTuneCents", 0.0f, nullptr);
                    clip.setProperty("formantSemitones", 0.0f, nullptr);
                    clip.setProperty("preserveFormants", false, nullptr);
                    clip.setProperty("transientLock", false, nullptr);
                    clip.setProperty("fadeInLength", (juce::int64)0, nullptr);
                    clip.setProperty("fadeOutLength", (juce::int64)0, nullptr);
                    clip.setProperty("fadeInCurve", 0, nullptr);
                    clip.setProperty("fadeOutCurve", 0, nullptr);
                    // The default user mode is derived from existing project
                    // defaults at runtime; 0 (Resample) with 0 pitch is unity.
                    clip.setProperty("timePitchMode", 0, nullptr);
                    clip.removeProperty("timePitchQuality", nullptr);

                    // Clip Vocal Tune plugin and preset state is not a raw take.
                    for (int j = clip.getNumChildren(); --j >= 0;)
                        clip.removeChild(j, nullptr);
                }
            }
        }

        // Reset every track FX chain, including master. The existing project
        // restore path handles actual processor/editor teardown under its
        // audio-callback-admission barrier; NEVER clear live plugin maps here.
        if (auto chains = project.getChildWithName("PluginChains"); chains.isValid())
            project.removeChild(chains, nullptr);
        project.addChild(juce::ValueTree("PluginChains"), -1, nullptr);

        if (auto clipFx = project.getChildWithName("ClipRegionPlugins"); clipFx.isValid())
            project.removeChild(clipFx, nullptr);
        project.addChild(juce::ValueTree("ClipRegionPlugins"), -1, nullptr);

        // Remove track/plugin/clip/send/tape-stop curves and their global
        // key/mode registries together. Retaining orphaned keys or Latch state
        // would reapply the old mix on the next transport play.
        if (auto automation = project.getChildWithName("Automation"); automation.isValid())
            project.removeChild(automation, nullptr);
        if (auto apex = project.getChildWithName("APEXAutomation"); apex.isValid())
            project.removeChild(apex, nullptr);
        project.addChild(juce::ValueTree("Automation"), -1, nullptr);

        // Remove effect sends and orphaned plugin sidechain cables without
        // disturbing direct bus/folder/master routes or track topology.
        if (auto routing = project.getChildWithName("RoutingGraph"); routing.isValid())
            if (auto connections = routing.getChildWithName("Connections"); connections.isValid())
                for (int i = connections.getNumChildren(); --i >= 0;)
                {
                    auto connection = connections.getChild(i);
                    const int type = (int)connection.getProperty("type", 0);
                    if (type == 1 || type == 2 || type == 3) // Send, PreSend, Sidechain
                        connections.removeChild(i, nullptr);
                }

        // Default master processing: no limiting/makeup adjustment.
        if (auto master = project.getChildWithName("MasterPhaseD"); master.isValid())
        {
            master.setProperty("masterGain", 1.0f, nullptr);
            master.setProperty("ceilingMode", "SoftClip", nullptr);
            master.setProperty("ceilingDb", -0.1f, nullptr);
            master.setProperty("ditherMode", "Off", nullptr);
        }

        // Leave playhead at the beginning, keep musical tempo and meter.
        if (auto transport = project.getChildWithName("Transport"); transport.isValid())
        {
            transport.setProperty("position", (juce::int64)0, nullptr);
            transport.setProperty("looping", false, nullptr);
        }

        // Preserve primary direct and folder routing, track hierarchy,
        // source paths, device configuration, markers and project key.
        // Only effect sends/sidechains are removed for a truly dry session.
        return project;
    }
};
} // namespace DAW

#pragma once

#include <JuceHeader.h>

namespace DAW
{
/**
    In-process VST3 startup diagnostics, not sandboxing and NOT a crash handler.

    The control thread writes an on-disk "plugin load in progress" marker
    BEFORE executing untrusted create/prepare code and clears it on every
    normal return/exception. If the entire DAW process terminates inside
    third-party code, the marker survives until the next launch.

    Important safety boundary: a stale marker identifies the plugin that
    was being LOADED, NOT a proven crash culprit. It must never automatically
    blacklist, delete or permanently bypass a plugin or its saved state.
    Native process crashes during playback/editor callbacks are outside
    the scope of this marker.
*/
class PluginLoadRecoveryCore final
{
public:
    struct Attempt
    {
        bool valid = false;
        juce::String name, format, identifier, stage, trackId;
        int uniqueId = 0;
        int slotIndex = -1;   // -1 when the producing chain had no slot identity
        juce::int64 startedUnixMs = 0;
    };

    static juce::File getMarkerFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("plugin-load-in-progress.xml");
    }

    static Attempt readPendingAttempt(const juce::File& file)
    {
        Attempt attempt;
        if (auto xml = juce::XmlDocument::parse(file))
        {
            if (!xml->hasTagName("APEXPluginLoadAttempt")
                || xml->getIntAttribute("version", 0) != 1)
                return attempt;

            attempt.name = xml->getStringAttribute("name");
            attempt.format = xml->getStringAttribute("format");
            attempt.identifier = xml->getStringAttribute("identifier");
            attempt.stage = xml->getStringAttribute("stage");
            attempt.trackId = xml->getStringAttribute("trackId");
            attempt.slotIndex = xml->getIntAttribute("slotIndex", -1);
            attempt.uniqueId = xml->getIntAttribute("uniqueId");
            attempt.startedUnixMs = xml->getStringAttribute("startedUnixMs").getLargeIntValue();
            attempt.valid = attempt.stage.isNotEmpty()
                && attempt.identifier.isNotEmpty()
                && attempt.format.isNotEmpty();
        }
        return attempt;
    }

    static bool writePendingAttempt(const juce::File& file, const Attempt& attempt)
    {
        if (!attempt.valid || attempt.stage.isEmpty()
            || attempt.identifier.isEmpty() || attempt.format.isEmpty())
            return false;

        if (file.getParentDirectory().createDirectory().failed())
            return false;

        juce::XmlElement xml("APEXPluginLoadAttempt");
        xml.setAttribute("version", 1);
        xml.setAttribute("name", attempt.name);
        xml.setAttribute("format", attempt.format);
        xml.setAttribute("identifier", attempt.identifier);
        xml.setAttribute("stage", attempt.stage);
        xml.setAttribute("trackId", attempt.trackId);
        xml.setAttribute("slotIndex", attempt.slotIndex);
        xml.setAttribute("uniqueId", attempt.uniqueId);
        xml.setAttribute("startedUnixMs", juce::String(attempt.startedUnixMs));

        // Replace atomically, so an interrupted disk write will not produce a
        // half-written marker. This is control-thread I/O, never audio-thread.
        juce::TemporaryFile temp(file);
        return temp.getFile().replaceWithText(xml.toString())
            && temp.overwriteTargetFileWithTemporary();
    }

    // Snapshot previous-launch evidence BEFORE the first plugin attempt can
    // overwrite the marker. UI/recovery code can inspect it later.
    static const Attempt& getStartupSuspect()
    {
        static const Attempt previousLaunch = readPendingAttempt(getMarkerFile());
        return previousLaunch;
    }

    static bool mayBeSamePlugin(const Attempt& attempt,
                                const juce::PluginDescription& description)
    {
        return attempt.valid
            && attempt.format.equalsIgnoreCase(description.pluginFormatName)
            && attempt.identifier.equalsIgnoreCase(description.fileOrIdentifier)
            && attempt.uniqueId == description.uniqueId;
    }

    /** If the crashing load had a stable track/slot identity, never suppress
        a different instance of the SAME VST3 component elsewhere in the
        project. Older markers lacking the optional fields retain the
        conservative component-level fallback (the user still opts in). */
    static bool mayBeSameSlot(const Attempt& attempt,
                              const juce::PluginDescription& description,
                              const juce::String& trackId, int slotIndex)
    {
        return mayBeSamePlugin(attempt, description)
            && (attempt.trackId.isEmpty() || attempt.trackId == trackId)
            && (attempt.slotIndex < 0 || attempt.slotIndex == slotIndex);
    }

    class ScopedAttempt final
    {
    public:
        ScopedAttempt(const juce::PluginDescription& desc,
                      const juce::String& stage,
                      const juce::String& trackId = {},
                      int slotIndex = -1)
        {
            // Reading the previous launch MUST precede our write.
            (void) PluginLoadRecoveryCore::getStartupSuspect();
            Attempt marker;
            marker.valid = true;
            marker.name = desc.name;
            marker.format = desc.pluginFormatName;
            marker.identifier = desc.fileOrIdentifier;
            marker.stage = stage;
            marker.trackId = trackId;
            marker.slotIndex = slotIndex;
            marker.uniqueId = desc.uniqueId;
            marker.startedUnixMs = juce::Time::currentTimeMillis();
            active = writePendingAttempt(getMarkerFile(), marker);
        }

        ~ScopedAttempt()
        {
            // Normal success, ordinary plugin error or C++ exception:
            // clear the marker. An OS process termination skips this scope.
            if (active)
                getMarkerFile().deleteFile();
        }

        ScopedAttempt(const ScopedAttempt&) = delete;
        ScopedAttempt& operator=(const ScopedAttempt&) = delete;

    private:
        bool active = false;
    };
};
} // namespace DAW

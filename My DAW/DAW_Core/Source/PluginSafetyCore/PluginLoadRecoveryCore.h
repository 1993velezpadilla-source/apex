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
        juce::String name, format, identifier, stage;
        int uniqueId = 0;
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

        if (!file.getParentDirectory().createDirectory())
            return false;

        juce::XmlElement xml("APEXPluginLoadAttempt");
        xml.setAttribute("version", 1);
        xml.setAttribute("name", attempt.name);
        xml.setAttribute("format", attempt.format);
        xml.setAttribute("identifier", attempt.identifier);
        xml.setAttribute("stage", attempt.stage);
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

    class ScopedAttempt final
    {
    public:
        ScopedAttempt(const juce::PluginDescription& desc,
                      const juce::String& stage)
        {
            // Reading the previous launch MUST precede our write.
            (void) PluginLoadRecoveryCore::getStartupSuspect();
            Attempt marker;
            marker.valid = true;
            marker.name = desc.name;
            marker.format = desc.pluginFormatName;
            marker.identifier = desc.fileOrIdentifier;
            marker.stage = stage;
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

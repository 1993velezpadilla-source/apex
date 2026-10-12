#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <vector>

namespace DAW
{
/**
    Crash-resilient, control-thread-only evidence for in-process plugin loads.

    A pending marker is written BEFORE invoking third-party creation/prepare/
    state-load code and removed once it returns. A marker found on a subsequent
    process launch is evidence of an INTERRUPTED operation, not proof that its
    plugin was responsible for an access violation: power loss, task-manager
    termination and machine shutdown look identical.

    This class never writes from the audio callback or from a Windows VEH.
    History is advisory and does not silently delete/bypass a user's plugin or
    change the project. The original project state remains the authority.
*/
class PluginLoadRecoveryCore final
{
public:
    struct Incident
    {
        juce::String identifier;
        juce::String format;
        juce::String name;
        juce::String lastStage;
        juce::String lastError;
        int uniqueId = 0;
        int interruptedCount = 0;
        int failedCount = 0;
        juce::int64 lastTimestampMs = 0;
    };

    explicit PluginLoadRecoveryCore(juce::File rootDirectory)
        : directory_(std::move(rootDirectory))
    {
    }

    static PluginLoadRecoveryCore& getInstance()
    {
        static PluginLoadRecoveryCore instance(
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("DAW_Core").getChildFile("plugin-recovery"));
        return instance;
    }

    // Call this once, before the first third-party in-process load. Safe for
    // repeated calls in the same process. No filesystem access on audio thread.
    void inspectPreviousLaunch()
    {
        if (inspected_) return;
        inspected_ = true;
        readHistory();

        const auto marker = pendingFile();
        if (!marker.existsAsFile()) return;

        if (auto xml = juce::XmlDocument::parse(marker))
        {
            if (xml->hasTagName("PendingPluginOperation"))
            {
                Incident incident;
                incident.identifier = xml->getStringAttribute("identifier");
                incident.format = xml->getStringAttribute("format");
                incident.name = xml->getStringAttribute("name");
                incident.uniqueId = xml->getIntAttribute("uniqueId");
                incident.lastStage = xml->getStringAttribute("stage");
                incident.lastTimestampMs = xml->getStringAttribute(
                    "timestampMs", "0").getLargeIntValue();
                if (incident.identifier.isNotEmpty() && incident.format.isNotEmpty())
                {
                    incident.lastError = "Previous in-process plugin operation did not complete. "
                                         "Cause not established.";
                    record(incident, true);
                    interruptedOnPreviousLaunch_ = true;
                    previousInterrupted_ = incident;
                    juce::Logger::writeToLog(
                        "[APEX RECOVERY] Interrupted in-process operation detected: "
                        + incident.name + " / " + incident.lastStage
                        + " (plugin culpability unverified)");
                }
            }
        }
        // Consume exactly once. A subsequent normal launch must not count
        // the same interruption a second time.
        marker.deleteFile();
    }

    bool interruptedOnPreviousLaunch() const noexcept
    {
        return interruptedOnPreviousLaunch_;
    }

    const Incident& previousInterruptedOperation() const noexcept
    {
        return previousInterrupted_;
    }

    bool beginOperation(const juce::PluginDescription& description,
                        const juce::String& stage)
    {
        inspectPreviousLaunch();
        directory_.createDirectory();
        juce::XmlElement xml("PendingPluginOperation");
        xml.setAttribute("identifier", description.fileOrIdentifier);
        xml.setAttribute("format", description.pluginFormatName);
        xml.setAttribute("name", description.name);
        xml.setAttribute("uniqueId", description.uniqueId);
        xml.setAttribute("stage", stage);
        xml.setAttribute("timestampMs", juce::String(
            juce::Time::currentTimeMillis()));
        return pendingFile().replaceWithText(xml.toString());
    }

    void finishOperation(const juce::PluginDescription& description,
                         const juce::String& stage,
                         bool success,
                         const juce::String& error = {})
    {
        inspectPreviousLaunch();
        // Clear BEFORE optional history persistence. If saving failure
        // information itself goes wrong, a clean return must not be counted
        // as a native crash on the next startup.
        pendingFile().deleteFile();
        if (!success)
        {
            Incident incident;
            incident.identifier = description.fileOrIdentifier;
            incident.format = description.pluginFormatName;
            incident.name = description.name;
            incident.uniqueId = description.uniqueId;
            incident.lastStage = stage;
            incident.lastError = error.isNotEmpty() ? error : "Plugin operation failed";
            incident.lastTimestampMs = juce::Time::currentTimeMillis();
            record(incident, false);
        }
    }

    const std::vector<Incident>& getIncidents()
    {
        inspectPreviousLaunch();
        return incidents_;
    }

    const Incident* findIncident(const juce::PluginDescription& description)
    {
        inspectPreviousLaunch();
        const auto it = std::find_if(incidents_.begin(), incidents_.end(),
            [&](const Incident& i) { return matches(i, description); });
        return it == incidents_.end() ? nullptr : &*it;
    }

    bool clearIncident(const juce::PluginDescription& description)
    {
        inspectPreviousLaunch();
        const auto before = incidents_.size();
        incidents_.erase(std::remove_if(incidents_.begin(), incidents_.end(),
            [&](const Incident& i) { return matches(i, description); }),
            incidents_.end());
        if (incidents_.size() == before) return false;
        saveHistory();
        return true;
    }

    juce::String warningFor(const juce::PluginDescription& description)
    {
        auto* incident = findIncident(description);
        if (incident == nullptr) return {};
        if (incident->interruptedCount > 0)
            return "APEX previously detected an unfinished in-process operation for '"
                + description.name + "'. This does not prove the plugin caused "
                "a crash. Keep a project backup and consider Safe Mode.";
        return "APEX previously recorded a failed load for '" + description.name
            + "'. Plugin state has not been removed.";
    }

    class ScopedOperation final
    {
    public:
        ScopedOperation(PluginLoadRecoveryCore& store,
                        const juce::PluginDescription& description,
                        juce::String stage)
            : store_(store), description_(description),
              stage_(std::move(stage))
        {
            store_.beginOperation(description_, stage_);
        }

        ~ScopedOperation()
        {
            if (!finished_)
                finish(false, "In-process operation exited without explicit completion");
        }

        void succeed() { finish(true, {}); }
        void fail(const juce::String& reason) { finish(false, reason); }

    private:
        void finish(bool succeeded, const juce::String& reason)
        {
            if (finished_) return;
            finished_ = true;
            store_.finishOperation(description_, stage_, succeeded, reason);
        }
        PluginLoadRecoveryCore& store_;
        juce::PluginDescription description_;
        juce::String stage_;
        bool finished_ = false;
        JUCE_DECLARE_NON_COPYABLE(ScopedOperation)
    };

private:
    static bool matches(const Incident& i, const juce::PluginDescription& d)
    {
        return i.uniqueId == d.uniqueId
            && i.identifier.equalsIgnoreCase(d.fileOrIdentifier)
            && i.format.equalsIgnoreCase(d.pluginFormatName);
    }

    juce::File pendingFile() const { return directory_.getChildFile("in_process_pending.xml"); }
    juce::File historyFile() const { return directory_.getChildFile("plugin_operation_history.xml"); }

    void record(const Incident& incoming, bool interrupted)
    {
        auto it = std::find_if(incidents_.begin(), incidents_.end(),
            [&](const Incident& i)
            {
                return i.uniqueId == incoming.uniqueId
                    && i.identifier.equalsIgnoreCase(incoming.identifier)
                    && i.format.equalsIgnoreCase(incoming.format);
            });
        if (it == incidents_.end())
        {
            incidents_.push_back(incoming);
            it = incidents_.end() - 1;
            it->interruptedCount = 0;
            it->failedCount = 0;
        }
        it->name = incoming.name;
        it->lastStage = incoming.lastStage;
        it->lastError = incoming.lastError;
        it->lastTimestampMs = incoming.lastTimestampMs;
        if (interrupted) ++it->interruptedCount;
        else ++it->failedCount;
        // Bound history growth from pathological plugin directories.
        if (incidents_.size() > 128)
            incidents_.erase(incidents_.begin());
        saveHistory();
    }

    void readHistory()
    {
        incidents_.clear();
        if (auto xml = juce::XmlDocument::parse(historyFile()))
        {
            if (!xml->hasTagName("PluginOperationHistory")) return;
            for (auto* item : xml->getChildIterator())
            {
                if (!item->hasTagName("Incident") || incidents_.size() >= 128)
                    continue;
                Incident i;
                i.identifier = item->getStringAttribute("identifier");
                i.format = item->getStringAttribute("format");
                if (i.identifier.isEmpty() || i.format.isEmpty()) continue;
                i.name = item->getStringAttribute("name");
                i.lastStage = item->getStringAttribute("stage");
                i.lastError = item->getStringAttribute("error");
                i.uniqueId = item->getIntAttribute("uid");
                i.interruptedCount = juce::jlimit(0, 1000000,
                    item->getIntAttribute("interrupted"));
                i.failedCount = juce::jlimit(0, 1000000,
                    item->getIntAttribute("failed"));
                i.lastTimestampMs = item->getStringAttribute(
                    "timestampMs", "0").getLargeIntValue();
                incidents_.push_back(std::move(i));
            }
        }
    }

    void saveHistory()
    {
        directory_.createDirectory();
        juce::XmlElement xml("PluginOperationHistory");
        for (const auto& i : incidents_)
        {
            auto* child = xml.createNewChildElement("Incident");
            child->setAttribute("identifier", i.identifier);
            child->setAttribute("format", i.format);
            child->setAttribute("name", i.name);
            child->setAttribute("uid", i.uniqueId);
            child->setAttribute("stage", i.lastStage);
            child->setAttribute("error", i.lastError);
            child->setAttribute("interrupted", i.interruptedCount);
            child->setAttribute("failed", i.failedCount);
            child->setAttribute("timestampMs", juce::String(i.lastTimestampMs));
        }
        historyFile().replaceWithText(xml.toString());
    }

    juce::File directory_;
    std::vector<Incident> incidents_;
    Incident previousInterrupted_;
    bool interruptedOnPreviousLaunch_ = false;
    bool inspected_ = false;
};
} // namespace DAW

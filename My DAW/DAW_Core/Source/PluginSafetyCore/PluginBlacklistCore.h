#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PluginBlacklistCore
 *
 * Nucleus: persistent blacklist of plugins that crashed, timed out,
 * or were manually disabled by the user.
 *
 * Stored as a simple XML file alongside the plugin cache.
 * Each entry records the plugin path, reason, and timestamp.
 */
class PluginBlacklistCore
{
public:
    enum class Reason
    {
        CrashedDuringScan,
        TimedOutDuringScan,
        InvalidBinary,
        Unsupported,
        UserDisabled,
        PendingRetry,
        PolicyBlockedDependency,
        BlockedCopyProtectionRuntime,
        TlsInitFailed,
        BadImageDependency,
        UnsignedHostContextFailure,
        UnknownSecurityFailure
    };

    struct Entry
    {
        juce::String pluginPath;
        Reason       reason    = Reason::CrashedDuringScan;
        juce::String notes;
        juce::int64  timestamp = 0;  // ms since epoch
    };

    PluginBlacklistCore() = default;

    // ── Query ────────────────────────────────────────────────────────────

    bool isBlacklisted(const juce::String& pluginPath) const
    {
        for (auto& e : entries_)
            if (e.pluginPath.equalsIgnoreCase(pluginPath)
                && e.reason != Reason::PendingRetry)
                return true;
        return false;
    }

    const std::vector<Entry>& getEntries() const { return entries_; }

    int getCount() const { return (int)entries_.size(); }

    // ── Mutate ───────────────────────────────────────────────────────────

    void add(const juce::String& pluginPath, Reason reason,
             const juce::String& notes = {})
    {
        // Update existing entry if present
        for (auto& e : entries_)
        {
            if (e.pluginPath.equalsIgnoreCase(pluginPath))
            {
                e.reason    = reason;
                e.notes     = notes;
                e.timestamp = juce::Time::currentTimeMillis();
                return;
            }
        }
        entries_.push_back({ pluginPath, reason, notes,
                             juce::Time::currentTimeMillis() });
    }

    void markForRetry(const juce::String& pluginPath)
    {
        for (auto& e : entries_)
            if (e.pluginPath.equalsIgnoreCase(pluginPath))
            { e.reason = Reason::PendingRetry; return; }
    }

    void remove(const juce::String& pluginPath)
    {
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                [&](const Entry& e) { return e.pluginPath.equalsIgnoreCase(pluginPath); }),
            entries_.end());
    }

    void clear() { entries_.clear(); }

    // ── Persistence ──────────────────────────────────────────────────────

    void saveToFile(const juce::File& f) const
    {
        auto root = std::make_unique<juce::XmlElement>("PluginBlacklist");
        for (auto& e : entries_)
        {
            auto* node = root->createNewChildElement("Entry");
            node->setAttribute("path",      e.pluginPath);
            node->setAttribute("reason",    (int)e.reason);
            node->setAttribute("notes",     e.notes);
            node->setAttribute("timestamp", juce::String(e.timestamp));
        }
        root->writeTo(f);
    }

    void loadFromFile(const juce::File& f)
    {
        entries_.clear();
        if (auto xml = juce::XmlDocument::parse(f))
        {
            for (auto* node : xml->getChildIterator())
            {
                if (node->getTagName() != "Entry") continue;
                Entry e;
                e.pluginPath = node->getStringAttribute("path");
                e.reason     = (Reason)node->getIntAttribute("reason", 0);
                e.notes      = node->getStringAttribute("notes");
                e.timestamp  = node->getStringAttribute("timestamp", "0").getLargeIntValue();
                entries_.push_back(std::move(e));
            }
        }
    }

    static juce::File getDefaultFile()
    {
        return juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
                .getChildFile("DAW_Core")
                .getChildFile("plugin_blacklist.xml");
    }

    static juce::String reasonToString(Reason r)
    {
        switch (r)
        {
            case Reason::CrashedDuringScan:  return "Crashed during scan";
            case Reason::TimedOutDuringScan: return "Timed out during scan";
            case Reason::InvalidBinary:      return "Invalid binary";
            case Reason::Unsupported:        return "Unsupported format";
            case Reason::UserDisabled:       return "Disabled by user";
            case Reason::PendingRetry:       return "Pending retry";
            case Reason::PolicyBlockedDependency: return "Policy blocked dependency";
            case Reason::BlockedCopyProtectionRuntime: return "Blocked copy-protection runtime";
            case Reason::TlsInitFailed: return "TLS init failed";
            case Reason::BadImageDependency: return "Bad image dependency";
            case Reason::UnsignedHostContextFailure: return "Unsigned host context failure";
            case Reason::UnknownSecurityFailure: return "Unknown security failure";
        }
        return "Unknown";
    }

private:
    std::vector<Entry> entries_;
};

} // namespace DAW

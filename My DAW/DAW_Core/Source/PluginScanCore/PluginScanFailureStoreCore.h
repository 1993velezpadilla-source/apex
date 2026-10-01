#pragma once
#include <JuceHeader.h>
#include "PluginScanResultCore.h"

namespace DAW {

class PluginScanFailureStoreCore
{
public:
    struct Entry
    {
        int jobId = 0;
        juce::String candidatePath;
        juce::String format;
        juce::String resultType;
        juce::String failureReason;
        juce::String errorText;
        juce::int64 timestamp = 0;
        int attempts = 0;
    };

    void loadFromFile(const juce::File& file)
    {
        entries_.clear();
        if (auto xml = juce::XmlDocument::parse(file))
        {
            for (auto* node : xml->getChildIterator())
            {
                if (node->getTagName() != "Failure")
                    continue;

                Entry entry;
                entry.jobId = node->getIntAttribute("jobId");
                entry.candidatePath = node->getStringAttribute("candidatePath");
                entry.format = node->getStringAttribute("format");
                entry.resultType = node->getStringAttribute("resultType");
                entry.failureReason = node->getStringAttribute("failureReason");
                entry.errorText = node->getStringAttribute("errorText");
                entry.timestamp = node->getStringAttribute("timestamp", "0").getLargeIntValue();
                entry.attempts = node->getIntAttribute("attempts", 0);
                entries_.push_back(std::move(entry));
            }
        }
    }

    void saveToFile(const juce::File& file) const
    {
        auto root = std::make_unique<juce::XmlElement>("PluginScanFailures");
        for (const auto& entry : entries_)
        {
            auto* node = root->createNewChildElement("Failure");
            node->setAttribute("jobId", entry.jobId);
            node->setAttribute("candidatePath", entry.candidatePath);
            node->setAttribute("format", entry.format);
            node->setAttribute("resultType", entry.resultType);
            node->setAttribute("failureReason", entry.failureReason);
            node->setAttribute("errorText", entry.errorText);
            node->setAttribute("timestamp", juce::String(entry.timestamp));
            node->setAttribute("attempts", entry.attempts);
        }
        root->writeTo(file);
    }

    int storeResult(const PluginScanResult& result)
    {
        if (result.resultType == PluginScanState::Success || result.resultType == PluginScanState::Skipped)
            return 0;

        for (auto& entry : entries_)
        {
            if (entry.candidatePath.equalsIgnoreCase(result.candidatePath))
            {
                entry.jobId = result.jobId;
                entry.format = result.format;
                entry.resultType = PluginScanStateCore::toString(result.resultType);
                entry.failureReason = result.failureReason;
                entry.errorText = result.errorText;
                entry.timestamp = result.timestamp;
                ++entry.attempts;
                return 1;
            }
        }

        Entry entry;
        entry.jobId = result.jobId;
        entry.candidatePath = result.candidatePath;
        entry.format = result.format;
        entry.resultType = PluginScanStateCore::toString(result.resultType);
        entry.failureReason = result.failureReason;
        entry.errorText = result.errorText;
        entry.timestamp = result.timestamp;
        entry.attempts = 1;
        entries_.push_back(std::move(entry));
        return 1;
    }

    void remove(const juce::String& candidatePath)
    {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
            [&](const Entry& entry) { return entry.candidatePath.equalsIgnoreCase(candidatePath); }),
            entries_.end());
    }

    void clear() { entries_.clear(); }
    int getCount() const { return static_cast<int>(entries_.size()); }
    const std::vector<Entry>& getEntries() const { return entries_; }

    bool shouldRetry(const juce::String&) const
    {
        return true;
    }

    static juce::File getDefaultFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core")
            .getChildFile("plugin_scan_failures.xml");
    }

private:
    std::vector<Entry> entries_;
};

} // namespace DAW

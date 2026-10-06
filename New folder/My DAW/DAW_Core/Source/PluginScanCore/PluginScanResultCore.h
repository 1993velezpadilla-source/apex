#pragma once
#include <JuceHeader.h>
#include "PluginScanStateCore.h"

namespace DAW {

struct PluginScannedDescriptionRecord
{
    juce::String path;
    juce::String format;
    juce::String name;
    juce::String manufacturer;
    juce::String uniqueId;
    juce::String category;
    juce::String version;
    bool isInstrument = false;
    int numInputs = 0;
    int numOutputs = 0;
    bool hasMidi = false;
};

struct PluginScanResult
{
    int jobId = 0;
    juce::String candidatePath;
    juce::String format;
    juce::String pluginName;
    juce::String manufacturer;
    juce::String uniqueId;
    juce::String category;
    juce::String version;
    PluginScanState resultType = PluginScanState::Pending;
    juce::String failureReason;
    juce::String errorText;
    juce::int64 timestamp = 0;
    bool producedMultiplePluginDescriptions = false;
    std::vector<PluginScannedDescriptionRecord> producedDescriptions;
};

class PluginScanResultCore
{
public:
    static PluginScanResult makeBasicResult(int jobId,
                                            const juce::String& candidatePath,
                                            const juce::String& format,
                                            PluginScanState resultType)
    {
        PluginScanResult result;
        result.jobId = jobId;
        result.candidatePath = candidatePath;
        result.format = format;
        result.resultType = resultType;
        result.timestamp = juce::Time::currentTimeMillis();
        return result;
    }

    static juce::var toVar(const PluginScannedDescriptionRecord& description)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("path", description.path);
        obj->setProperty("format", description.format);
        obj->setProperty("name", description.name);
        obj->setProperty("manufacturer", description.manufacturer);
        obj->setProperty("uniqueId", description.uniqueId);
        obj->setProperty("category", description.category);
        obj->setProperty("version", description.version);
        obj->setProperty("isInstrument", description.isInstrument);
        obj->setProperty("numInputs", description.numInputs);
        obj->setProperty("numOutputs", description.numOutputs);
        obj->setProperty("hasMidi", description.hasMidi);
        return juce::var(obj.release());
    }

    static PluginScannedDescriptionRecord descriptionFromVar(const juce::var& value)
    {
        PluginScannedDescriptionRecord description;
        if (auto* obj = value.getDynamicObject())
        {
            description.path = obj->getProperty("path").toString();
            description.format = obj->getProperty("format").toString();
            description.name = obj->getProperty("name").toString();
            description.manufacturer = obj->getProperty("manufacturer").toString();
            description.uniqueId = obj->getProperty("uniqueId").toString();
            description.category = obj->getProperty("category").toString();
            description.version = obj->getProperty("version").toString();
            description.isInstrument = static_cast<bool>(obj->getProperty("isInstrument"));
            description.numInputs = static_cast<int>(obj->getProperty("numInputs"));
            description.numOutputs = static_cast<int>(obj->getProperty("numOutputs"));
            description.hasMidi = static_cast<bool>(obj->getProperty("hasMidi"));
        }
        return description;
    }

    static juce::var toVar(const PluginScanResult& result)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("jobId", result.jobId);
        obj->setProperty("candidatePath", result.candidatePath);
        obj->setProperty("format", result.format);
        obj->setProperty("pluginName", result.pluginName);
        obj->setProperty("manufacturer", result.manufacturer);
        obj->setProperty("uniqueId", result.uniqueId);
        obj->setProperty("category", result.category);
        obj->setProperty("version", result.version);
        obj->setProperty("resultType", PluginScanStateCore::toString(result.resultType));
        obj->setProperty("failureReason", result.failureReason);
        obj->setProperty("errorText", result.errorText);
        obj->setProperty("timestamp", result.timestamp);
        obj->setProperty("producedMultiplePluginDescriptions", result.producedMultiplePluginDescriptions);

        juce::Array<juce::var> descriptions;
        for (const auto& description : result.producedDescriptions)
            descriptions.add(toVar(description));
        obj->setProperty("producedDescriptions", juce::var(descriptions));
        return juce::var(obj.release());
    }

    static PluginScanResult fromVar(const juce::var& value)
    {
        PluginScanResult result;
        if (auto* obj = value.getDynamicObject())
        {
            result.jobId = static_cast<int>(obj->getProperty("jobId"));
            result.candidatePath = obj->getProperty("candidatePath").toString();
            result.format = obj->getProperty("format").toString();
            result.pluginName = obj->getProperty("pluginName").toString();
            result.manufacturer = obj->getProperty("manufacturer").toString();
            result.uniqueId = obj->getProperty("uniqueId").toString();
            result.category = obj->getProperty("category").toString();
            result.version = obj->getProperty("version").toString();
            result.resultType = PluginScanStateCore::fromString(obj->getProperty("resultType").toString());
            result.failureReason = obj->getProperty("failureReason").toString();
            result.errorText = obj->getProperty("errorText").toString();
            result.timestamp = static_cast<juce::int64>(obj->getProperty("timestamp"));
            result.producedMultiplePluginDescriptions = static_cast<bool>(obj->getProperty("producedMultiplePluginDescriptions"));

            auto descriptionsValue = obj->getProperty("producedDescriptions");
            if (descriptionsValue.isArray())
                for (const auto& item : *descriptionsValue.getArray())
                    result.producedDescriptions.push_back(descriptionFromVar(item));
        }
        return result;
    }
};

} // namespace DAW

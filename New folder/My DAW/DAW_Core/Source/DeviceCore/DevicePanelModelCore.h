#pragma once

#include <JuceHeader.h>
#include <map>
#include <set>

namespace DAW {

class DevicePanelModelCore
{
public:
    struct OutputChannelChoice
    {
        juce::String label;
        juce::BigInteger channels;
        bool useDefault = true;
    };

    static const juce::String& noDeviceSentinel()
    {
        static const juce::String value = juce::String::fromUTF8 (u8"— No Device —");
        return value;
    }

    static const juce::String& defaultOutputChannelsSentinel()
    {
        static const juce::String value = juce::String::fromUTF8 (u8"— Default Output Pair —");
        return value;
    }

    explicit DevicePanelModelCore(juce::AudioDeviceManager& dm)
        : dm_(dm)
    {
    }

    /** Drops all cached device lists / channel probes.
        Call when the panel opens so each session gets exactly ONE scan
        instead of a rescan on every control interaction. */
    void invalidateDeviceCache()
    {
        outputListCache_.clear();
        inputListCache_.clear();
        channelChoicesCache_.clear();
        scannedTypes_.clear();
    }

    juce::StringArray getTypeNames() const
    {
        juce::StringArray names;
        auto& types = dm_.getAvailableDeviceTypes();
        for (auto* type : types)
            if (type != nullptr)
                names.add(type->getTypeName());
        return names;
    }

    juce::StringArray getOutputDevices(const juce::String& typeName) const
    {
        if (auto it = outputListCache_.find(typeName); it != outputListCache_.end())
            return it->second;

        juce::StringArray names;

        if (auto* type = findType(typeName))
        {
            ensureScanned(*type);
            names = type->getDeviceNames(false);

            if (isAsioType(typeName))
                names.insert(0, noDeviceSentinel());

            outputListCache_[typeName] = names;
        }

        return names;
    }

    juce::StringArray getInputDevices(const juce::String& typeName) const
    {
        if (auto it = inputListCache_.find(typeName); it != inputListCache_.end())
            return it->second;

        if (auto* type = findType(typeName))
        {
            ensureScanned(*type);
            auto names = type->getDeviceNames(true);
            inputListCache_[typeName] = names;
            return names;
        }

        return {};
    }

    juce::StringArray getCommonSampleRates() const
    {
        return { "44100", "48000", "88200", "96000" };
    }

    juce::StringArray getCommonBufferSizes() const
    {
        return { "64", "128", "256", "512", "1024", "2048" };
    }

    /** Ensures the given backend was scanned this session (cached).
        Cheap when already scanned — avoids a redundant, slow
        scanForDevices() on the Configure path. */
    void ensureTypeScanned(const juce::String& typeName) const
    {
        if (auto* type = findType(typeName))
            ensureScanned(*type);
    }

    juce::Array<OutputChannelChoice> getOutputChannelChoices(const juce::String& typeName,
                                                             const juce::String& outputDeviceName) const
    {
        juce::Array<OutputChannelChoice> choices;

        if (!isAsioType(typeName))
            return choices;

        auto* type = findType(typeName);
        if (type == nullptr)
            return choices;

        auto outputName = outputDeviceName.trim();
        if (outputName.isEmpty() || outputName == noDeviceSentinel())
            return choices;

        // Creating a temp ASIO device to probe channel names is VERY expensive
        // (ASIO4ALL can block for seconds). Cache per type+device for the session.
        const auto cacheKey = typeName + "||" + outputName;
        if (auto it = channelChoicesCache_.find(cacheKey); it != channelChoicesCache_.end())
            return it->second;

        // FAST PATH: if the requested device is already the open device, read
        // channel names straight from it — no temp ASIO device, no blocking.
        if (auto* current = dm_.getCurrentAudioDevice())
        {
            if (dm_.getCurrentAudioDeviceType() == typeName
                && current->getName() == outputName)
            {
                choices = buildChoicesFromChannelNames(current->getOutputChannelNames());
                channelChoicesCache_[cacheKey] = choices;
                return choices;
            }
        }

        // Scan at most once per panel-open (JUCE asserts if createDevice is
        // called on a never-scanned type); no rescan per interaction.
        ensureScanned(*type);
        std::unique_ptr<juce::AudioIODevice> device(type->createDevice(outputName, {}));
        if (device == nullptr)
            return choices;

        choices = buildChoicesFromChannelNames(device->getOutputChannelNames());
        channelChoicesCache_[cacheKey] = choices;
        return choices;
    }

private:
    static juce::Array<OutputChannelChoice> buildChoicesFromChannelNames(const juce::StringArray& outputNames)
    {
        juce::Array<OutputChannelChoice> choices;
        choices.add({ defaultOutputChannelsSentinel(), {}, true });

        const auto addChoice = [&choices](const juce::String& label, int firstChannel, int secondChannel = -1)
        {
            juce::BigInteger channels;
            channels.setBit(firstChannel);
            if (secondChannel >= 0)
                channels.setBit(secondChannel);
            choices.add({ label, channels, false });
        };

        for (int i = 0; i < outputNames.size(); i += 2)
        {
            const int first = i;
            const int second = (i + 1 < outputNames.size()) ? (i + 1) : -1;
            juce::String label = outputNames[i];
            if (second >= 0)
                label << " + " << outputNames[second];
            addChoice(label, first, second);
        }

        if (outputNames.size() == 1)
            addChoice(outputNames[0], 0);

        return choices;
    }

    juce::AudioIODeviceType* findType(const juce::String& typeName) const
    {
        auto& types = dm_.getAvailableDeviceTypes();
        for (auto* type : types)
            if (type != nullptr && type->getTypeName() == typeName)
                return type;
        return nullptr;
    }

    void ensureScanned(juce::AudioIODeviceType& type) const
    {
        if (scannedTypes_.count(type.getTypeName()) > 0)
            return;

        type.scanForDevices();
        scannedTypes_.insert(type.getTypeName());
    }

    static bool isAsioType(const juce::String& typeName)
    {
        return typeName.containsIgnoreCase("asio");
    }

    juce::AudioDeviceManager& dm_;

    // Session caches — invalidated when the panel opens (invalidateDeviceCache).
    mutable std::map<juce::String, juce::StringArray> outputListCache_;
    mutable std::map<juce::String, juce::StringArray> inputListCache_;
    mutable std::map<juce::String, juce::Array<OutputChannelChoice>> channelChoicesCache_;
    mutable std::set<juce::String> scannedTypes_;
};

} // namespace DAW

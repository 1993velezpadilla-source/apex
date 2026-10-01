#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginScanResultTransportCore
{
public:
    static juce::StringPairArray parseKeyValueOutput(const juce::String& output)
    {
        juce::StringPairArray kv;
        auto lines = juce::StringArray::fromLines(output);
        for (auto& line : lines)
        {
            auto eq = line.indexOfChar('=');
            if (eq > 0)
                kv.set(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
        }
        return kv;
    }
};

} // namespace DAW

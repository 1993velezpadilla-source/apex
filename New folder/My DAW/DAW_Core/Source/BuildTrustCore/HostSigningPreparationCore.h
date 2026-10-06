#pragma once
#include <JuceHeader.h>

namespace DAW {

class HostSigningPreparationCore
{
public:
    static juce::StringArray getRequiredArtifacts()
    {
        return { "DAW_Core.exe", "scanner/helper process executable", "future plugin bridge executable" };
    }
};

} // namespace DAW

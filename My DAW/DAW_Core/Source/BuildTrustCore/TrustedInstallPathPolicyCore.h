#pragma once
#include <JuceHeader.h>

namespace DAW {

class TrustedInstallPathPolicyCore
{
public:
    static bool isTrustedInstallPath(const juce::File& exe)
    {
        auto path = exe.getFullPathName().toLowerCase();
        return path.contains("\\program files\\");
    }

    static juce::String getRecommendation()
    {
        return "Run the signed release build from Program Files instead of Downloads/debug paths.";
    }
};

} // namespace DAW

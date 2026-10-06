#pragma once
#include <JuceHeader.h>

namespace DAW {

class HelperProcessSigningPolicyCore
{
public:
    static juce::String getPolicy()
    {
        return "All scanner/helper/bridge executables must be signed and shipped as first-class host binaries.";
    }
};

} // namespace DAW

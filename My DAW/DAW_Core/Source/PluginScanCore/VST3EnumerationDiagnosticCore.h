#pragma once

#include <JuceHeader.h>

namespace DAW
{

// APEX-owned transport flag for optional VST3 enumeration diagnostics.
//
// This used to come from a locally modified JUCE header that was not part of
// the pinned JUCE 8.0.12 dependency. Keeping the flag in APEX makes clean
// checkouts and CI reproducible and avoids coupling scanner/sandbox control
// flow to an unversioned SDK file.
struct VST3EnumerationDiagnosticCore final
{
    static constexpr const char* kCommandLineFlag =
        "--apex-vst3-enumeration-diagnostic";

    static bool isEnabledForCurrentProcess()
    {
        return juce::JUCEApplicationBase::getCommandLineParameters()
            .contains (kCommandLineFlag);
    }
};

} // namespace DAW

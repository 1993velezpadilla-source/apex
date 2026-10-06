#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <objbase.h>
 #include <windows.h>
#endif

namespace DAW
{

// APEX-owned transport flag and platform diagnostics for VST3 enumeration.
//
// These facilities used to come from a locally modified JUCE header that was
// not part of the pinned JUCE 8.0.12 dependency. Keeping them in APEX makes
// clean checkouts/CI reproducible and avoids coupling scanner/sandbox control
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

struct VST3EnumerationDiagnosticPlatform final
{
    static juce::String currentProcessId()
    {
       #if JUCE_WINDOWS
        return juce::String (static_cast<juce::int64> (::GetCurrentProcessId()));
       #else
        return {};
       #endif
    }

    static juce::String currentThreadId()
    {
       #if JUCE_WINDOWS
        return juce::String (static_cast<juce::int64> (::GetCurrentThreadId()));
       #else
        return {};
       #endif
    }

    static juce::String comApartment()
    {
       #if JUCE_WINDOWS
        APTTYPE type {};
        APTTYPEQUALIFIER qualifier {};
        const auto hr = ::CoGetApartmentType (&type, &qualifier);
        if (FAILED (hr))
            return "Uninitialized";

        switch (type)
        {
            case APTTYPE_STA:  return "STA";
            case APTTYPE_MTA:  return "MTA";
            case APTTYPE_NA:   return "NA";
            case APTTYPE_MAINSTA: return "MainSTA";
            default:           return "Unknown";
        }
       #else
        return "N/A";
       #endif
    }
};

} // namespace DAW

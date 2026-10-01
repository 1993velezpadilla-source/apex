#pragma once
#include <JuceHeader.h>

namespace DAW {

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int uMode);
static constexpr unsigned int kSemFailCriticalErrors = 0x0001;
static constexpr unsigned int kSemNoGpFaultErrorBox  = 0x0002;
static constexpr unsigned int kSemNoOpenFileErrorBox = 0x8000;
#endif

class PluginProtectionRuntimeGuardCore
{
public:
    PluginProtectionRuntimeGuardCore()
    {
#if JUCE_WINDOWS
        previousMode_ = SetErrorMode(kSemFailCriticalErrors | kSemNoGpFaultErrorBox | kSemNoOpenFileErrorBox);
#endif
    }

    ~PluginProtectionRuntimeGuardCore()
    {
#if JUCE_WINDOWS
        SetErrorMode(previousMode_);
#endif
    }

private:
#if JUCE_WINDOWS
    unsigned int previousMode_ = 0;
#endif
};

} // namespace DAW

#pragma once

#if JUCE_ANDROID
 #include <android/trace.h>
#endif

namespace DAW
{
    // Message-thread presentation scopes for Android System Trace/Perfetto.
    // Inactive tracing performs no logging, allocation or file I/O. Do not
    // use this in the audio callback; it describes UI work, not audio load.
    class ScopedAndroidUiTrace final
    {
    public:
        explicit ScopedAndroidUiTrace(const char* name) noexcept
        {
           #if JUCE_ANDROID
            enabled_ = ATrace_isEnabled();
            if (enabled_)
                ATrace_beginSection(name);
           #else
            (void) name;
           #endif
        }

        ~ScopedAndroidUiTrace() noexcept
        {
           #if JUCE_ANDROID
            if (enabled_)
                ATrace_endSection();
           #endif
        }

        ScopedAndroidUiTrace(const ScopedAndroidUiTrace&) = delete;
        ScopedAndroidUiTrace& operator=(const ScopedAndroidUiTrace&) = delete;

    private:
       #if JUCE_ANDROID
        bool enabled_ = false;
       #endif
    };
}

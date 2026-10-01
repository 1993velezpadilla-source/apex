#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace DAW {

/**
 * PluginCrashGuardCore - OS-level crash protection for misbehaving plugins.
 *
 * Installs a Vectored Exception Handler (VEH) that catches Windows access
 * violations (0xC0000005) from third-party plugin DLLs BEFORE stack unwinding
 * occurs.  When the plugin corrupts its own COM interfaces and crashes, the
 * VEH handler:
 *   1. Logs the crash with the faulting address and exception code
 *   2. Saves the project to a recovery file so the user doesn't lose work
 *   3. Allows the process to terminate gracefully (vs. abrupt OS kill)
 *
 * This is necessary because:
 *   - /EHsc cannot catch access violations (only C++ exceptions)
 *   - /EHa breaks JUCE's String/StringHolder memory management
 *   - Corrupted call stacks prevent catch(...) from reaching the handler
 *   - VEH runs BEFORE SEH stack unwinding, so it catches even corrupted-stack crashes
 *
 * Usage: call install() once at application startup.
 */
class PluginCrashGuardCore
{
public:
    /** Install the VEH handler.  Call once from ApplicationCore or MainComponent. */
    static void install();

    /** Check if a crash was caught (for UI notification). */
    static bool wasCrashDetected() noexcept;

    /** Get the crash log path for the most recent crash. */
    static juce::File getCrashLogPath() noexcept;

private:
    /** Vectored Exception Handler callback.  Private static member so it
        can access s_crashDetected and s_crashLogPath. */
    static LONG CALLBACK apexPluginCrashVEH(PEXCEPTION_POINTERS exInfo);

    static inline bool s_installed = false;
    static inline bool s_crashDetected = false;
    static inline juce::File s_crashLogPath;
};

} // namespace DAW

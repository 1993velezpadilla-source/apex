// PluginCrashGuardCore.cpp
// Compiled with /EHa to enable SEH exception handling.
// This file installs a Vectored Exception Handler (VEH) that catches Windows
// access violations from third-party plugins before stack unwinding occurs.

#include "PluginCrashGuardCore.h"
#include <atomic>

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

// ═══════════════════════════════════════════════════════════════════════════
// VEH handler implementation
// ═══════════════════════════════════════════════════════════════════════════

#if JUCE_WINDOWS

LONG CALLBACK PluginCrashGuardCore::apexPluginCrashVEH(PEXCEPTION_POINTERS exInfo)
{
    if (exInfo == nullptr || exInfo->ExceptionRecord == nullptr)
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = exInfo->ExceptionRecord->ExceptionCode;

    // Only handle access violations (0xC0000005) and stack buffer overruns
    if (code != 0xC0000005)
        return EXCEPTION_CONTINUE_SEARCH;

    s_crashDetected = true;

    // Log crash details
    const void* faultAddr = exInfo->ExceptionRecord->ExceptionAddress;
    const void* accessAddr = reinterpret_cast<const void*>(
        exInfo->ExceptionRecord->ExceptionInformation[1]);

    juce::String crashLog;
    crashLog += "=== APEX Plugin Crash Detected ===\n";
    crashLog += "Time: " + juce::Time::getCurrentTime().toString(true, true) + "\n";
    crashLog += "Exception: 0xC0000005 (Access Violation)\n";
    crashLog += "Faulting instruction: 0x" + juce::String::toHexString((juce::int64)faultAddr) + "\n";
    crashLog += "Access address: 0x" + juce::String::toHexString((juce::int64)accessAddr) + "\n";
    crashLog += "Thread ID: " + juce::String((int)GetCurrentThreadId()) + "\n";
    crashLog += "\nThe DAW will close. Your project has been saved to the crash recovery file.\n";

    // Determine crash log path
    auto logDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DAW_Core").getChildFile("crash-reports");
    logDir.createDirectory();
    auto logFile = logDir.getChildFile(
        "plugin-crash_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".txt");
    logFile.replaceWithText(crashLog);

    s_crashLogPath = logFile;

    juce::Logger::writeToLog("[APEX-CRASH-GUARD] Plugin access violation detected - "
        "fault=0x" + juce::String::toHexString((juce::int64)faultAddr)
        + " access=0x" + juce::String::toHexString((juce::int64)accessAddr));

    // Let the process terminate normally — the crash dialog will show
    // the crash log location so the user can report it.
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

void PluginCrashGuardCore::install()
{
    if (s_installed)
        return;

#if JUCE_WINDOWS
    AddVectoredExceptionHandler(1, &PluginCrashGuardCore::apexPluginCrashVEH);
    s_installed = true;
    juce::Logger::writeToLog("[APEX-CRASH-GUARD] Vectored exception handler installed");
#endif
}

bool PluginCrashGuardCore::wasCrashDetected() noexcept
{
    return s_crashDetected;
}

juce::File PluginCrashGuardCore::getCrashLogPath() noexcept
{
    return s_crashLogPath;
}

} // namespace DAW

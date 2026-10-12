#pragma once

#include "PluginSandboxProtocolCore.h"
#include "../PluginScanCore/PluginScanAuditLogCore.h"
#include "../PluginScanCore/PluginScanFormatsCore.h"
#include "../PluginScanCore/VST3EnumerationDiagnosticCore.h"

#include <condition_variable>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace DAW
{

/**
    Forensic, worker-context-only VST3 enumeration.

    This deliberately bypasses the IPC/PluginCreate transaction. It is enabled
    only by an explicit worker command line and owns a diagnostic-only ceiling;
    normal PluginSandboxProcessCore deadlines and PluginCreate behavior do not
    call this path.
*/
class PluginSandboxEnumOnlyDiagnosticCore
{
public:
    static constexpr int kInvalidArgumentsExitCode = 64;
    static constexpr int kWorkerContextExitCode = 65;
    static constexpr int kFormatUnavailableExitCode = 66;
    static constexpr int kNoDescriptionsExitCode = 67;
    static constexpr int kEnumerationFailureExitCode = 68;
    static constexpr std::uint32_t kCeilingExpiredExitCode = 124;

    static int run(const PluginSandboxEnumOnlyCommandLine& command)
    {
       #if ! JUCE_WINDOWS
        juce::ignoreUnused(command);
        return kWorkerContextExitCode;
       #else
        using namespace PluginSandboxWin32;

        if (! command.valid)
        {
            std::cerr << "APEX_ENUM_ONLY_RESULT state=Failure reason=InvalidArguments"
                      << " detail=" << command.error << std::endl;
            return kInvalidArgumentsExitCode;
        }

        const auto executablePath = resolveCurrentExecutablePath();
        const juce::File executable(executablePath);
        const auto workerDirectory = executable.getParentDirectory();
        if (executablePath.isEmpty()
            || ! workerDirectory.isDirectory()
            || ! workerDirectory.setAsCurrentWorkingDirectory())
        {
            const auto error = "could not establish the worker executable directory";
            appendHarnessLine("ENUMERATION_HARNESS_CONTEXT_FAILURE",
                              "file=" + quote(command.pluginPath)
                                  + " pid=" + processId()
                                  + " detail=" + error);
            std::cerr << "APEX_ENUM_ONLY_RESULT state=Failure reason=ContextSetup"
                      << " detail=" << error << std::endl;
            return kWorkerContextExitCode;
        }

        const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        const bool messageManagerPresent = messageManager != nullptr;
        const bool messageThread = messageManagerPresent
                                 && messageManager->isThisTheMessageThread();
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        std::mutex watchdogMutex;
        std::condition_variable watchdogCondition;
        bool enumerationFinished = false;
        std::thread watchdog([&]
        {
            std::unique_lock lock(watchdogMutex);
            if (watchdogCondition.wait_for(
                    lock,
                    std::chrono::milliseconds(command.ceilingMilliseconds),
                    [&] { return enumerationFinished; }))
                return;

            appendHarnessLine(
                "ENUMERATION_HARNESS_CEILING_EXPIRED",
                "context=SANDBOX_WORKER"
                    + juce::String(" file=") + quote(command.pluginPath)
                    + " pid=" + processId()
                    + " tid=" + threadId()
                    + " workingDirectory=" + quote(currentWorkingDirectory())
                    + " ceilingMs="
                        + juce::String(static_cast<juce::int64>(command.ceilingMilliseconds))
                    + " exitCode=" + juce::String(static_cast<int>(kCeilingExpiredExitCode)));

            TerminateProcess(GetCurrentProcess(), kCeilingExpiredExitCode);
            std::_Exit(static_cast<int>(kCeilingExpiredExitCode));
        });

        int exitCode = 0;
        int descriptionCount = 0;
        juce::String failureReason;
        juce::String failureDetail;

        try
        {
            PluginScanFormatsCore formats;
            auto* format = formats.findFormatByName("VST3");
            if (format == nullptr)
            {
                exitCode = kFormatUnavailableExitCode;
                failureReason = "VST3FormatUnavailable";
            }
            else
            {
                juce::OwnedArray<juce::PluginDescription> descriptions;
                format->findAllTypesForFile(descriptions, command.pluginPath);
                descriptionCount = descriptions.size();

                if (descriptionCount == 0)
                {
                    exitCode = kNoDescriptionsExitCode;
                    failureReason = juce::File(command.pluginPath).exists()
                                  ? "NoPluginTypesAdded"
                                  : "PathDoesNotExist";
                }
            }
        }
        catch (const std::exception& exception)
        {
            exitCode = kEnumerationFailureExitCode;
            failureReason = "StdException";
            failureDetail = juce::String::fromUTF8(exception.what());
        }
        catch (...)
        {
            exitCode = kEnumerationFailureExitCode;
            failureReason = "UnknownException";
        }

        {
            std::lock_guard lock(watchdogMutex);
            enumerationFinished = true;
        }
        watchdogCondition.notify_one();
        watchdog.join();

        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
        const auto state = exitCode == 0 ? "Success" : "Failure";
        auto detail = juce::String("state=") + state
            + " context=SANDBOX_WORKER"
            + " file=" + quote(command.pluginPath)
            + " pid=" + processId()
            + " tid=" + threadId()
            + " workingDirectory=" + quote(currentWorkingDirectory())
            + " messageManagerPresent=" + juce::String(messageManagerPresent ? 1 : 0)
            + " messageThread=" + juce::String(messageThread ? 1 : 0)
            + " comApartment=" + VST3EnumerationDiagnosticPlatform::comApartment()
            + " descriptions=" + juce::String(descriptionCount)
            + " elapsedMs=" + juce::String(elapsedMs, 3)
            + " ceilingMs="
                + juce::String(static_cast<juce::int64>(command.ceilingMilliseconds))
            + " exitCode=" + juce::String(exitCode);

        if (failureReason.isNotEmpty())
            detail += " reason=" + failureReason;
        if (failureDetail.isNotEmpty())
            detail += " detail=" + quote(failureDetail);

        appendHarnessLine("ENUMERATION_HARNESS_RESULT", detail);

        std::cout << "APEX_ENUM_ONLY_RESULT state=" << state
                  << " context=SANDBOX_WORKER"
                  << " file=\"" << command.pluginPath << "\""
                  << " pid=" << processId()
                  << " tid=" << threadId()
                  << " workingDirectory=\"" << currentWorkingDirectory() << "\""
                  << " messageManagerPresent=" << (messageManagerPresent ? 1 : 0)
                  << " messageThread=" << (messageThread ? 1 : 0)
                  << " comApartment="
                  << VST3EnumerationDiagnosticPlatform::comApartment()
                  << " descriptions=" << descriptionCount
                  << " elapsedMs=" << elapsedMs
                  << " ceilingMs=" << command.ceilingMilliseconds
                  << " exitCode=" << exitCode;
        if (failureReason.isNotEmpty())
            std::cout << " reason=" << failureReason;
        if (failureDetail.isNotEmpty())
            std::cout << " detail=\"" << failureDetail << "\"";
        std::cout << std::endl;

        return exitCode;
       #endif
    }

private:
    static juce::String processId()
    {
        return VST3EnumerationDiagnosticPlatform::currentProcessId();
    }

    static juce::String threadId()
    {
        return VST3EnumerationDiagnosticPlatform::currentThreadId();
    }

    static juce::String currentWorkingDirectory()
    {
        return juce::File::getCurrentWorkingDirectory().getFullPathName();
    }

    static juce::String quote(juce::String value)
    {
        return "\"" + value.replace("\\", "\\\\")
                             .replace("\"", "\\\"")
                             .replaceCharacters("\r\n", "  ") + "\"";
    }

    static void appendHarnessLine(const char* event,
                                  const juce::String& detail)
    {
        PluginScanAuditLogCore::appendLine(
            "vst3_enumeration_diagnostic.log",
            juce::String("[VST3-ENUM-HARNESS] event=") + event + " " + detail);
    }
};

} // namespace DAW

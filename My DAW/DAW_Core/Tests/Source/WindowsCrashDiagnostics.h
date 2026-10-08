#pragma once

// Test-runner diagnostics only. Never installed in the audio application or
// plugin worker: a native crash remains a failing, incomplete test execution.
#if JUCE_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

namespace ApexTestDiagnostics
{
inline LONG WINAPI reportNativeCrash(EXCEPTION_POINTERS* exception)
{
    std::fprintf(stderr, "APEX_NATIVE_CRASH code=0x%08lx address=%p\n",
                 exception->ExceptionRecord->ExceptionCode,
                 exception->ExceptionRecord->ExceptionAddress);
    const auto library = LoadLibraryW(L"dbghelp.dll");
    if (library != nullptr)
    {
        const auto process = GetCurrentProcess();
        const auto initialise = reinterpret_cast<decltype(&SymInitialize)>(GetProcAddress(library, "SymInitialize"));
        const auto setOptions = reinterpret_cast<decltype(&SymSetOptions)>(GetProcAddress(library, "SymSetOptions"));
        const auto walk = reinterpret_cast<decltype(&StackWalk64)>(GetProcAddress(library, "StackWalk64"));
        const auto functionTable = reinterpret_cast<PFUNCTION_TABLE_ACCESS_ROUTINE64>(GetProcAddress(library, "SymFunctionTableAccess64"));
        const auto moduleBase = reinterpret_cast<PGET_MODULE_BASE_ROUTINE64>(GetProcAddress(library, "SymGetModuleBase64"));
        const auto fromAddress = reinterpret_cast<decltype(&SymFromAddr)>(GetProcAddress(library, "SymFromAddr"));
        const auto fromLine = reinterpret_cast<decltype(&SymGetLineFromAddr64)>(GetProcAddress(library, "SymGetLineFromAddr64"));
        if (setOptions != nullptr)
            setOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (initialise != nullptr && walk != nullptr && functionTable != nullptr
            && moduleBase != nullptr && fromAddress != nullptr && initialise(process, nullptr, TRUE))
        {
            auto context = *exception->ContextRecord;
            STACKFRAME64 frame {};
            frame.AddrPC = { context.Rip, 0, AddrModeFlat };
            frame.AddrStack = { context.Rsp, 0, AddrModeFlat };
            frame.AddrFrame = { context.Rbp, 0, AddrModeFlat };
            for (int index = 0; index < 48 && frame.AddrPC.Offset != 0; ++index)
            {
                alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] {};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = MAX_SYM_NAME;
                DWORD64 displacement = 0;
                const bool resolved = fromAddress(process, frame.AddrPC.Offset, &displacement, symbol) != FALSE;
                IMAGEHLP_LINE64 line {};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;
                const bool lineResolved = fromLine != nullptr
                    && fromLine(process, frame.AddrPC.Offset, &lineDisplacement, &line) != FALSE;
                std::fprintf(stderr, "#%d 0x%llx %s %s:%lu\n", index,
                             static_cast<unsigned long long>(frame.AddrPC.Offset),
                             resolved ? symbol->Name : "<unresolved>",
                             lineResolved ? line.FileName : "<no source>",
                             lineResolved ? line.LineNumber : 0);
                const auto previousPC = frame.AddrPC.Offset;
                if (!walk(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                          &context, nullptr, functionTable, moduleBase, nullptr)
                    || frame.AddrPC.Offset == previousPC)
                    break;
            }
        }
        // The publisher puts stdout/stderr in a unique run directory. Put the
        // dump beside them, so its lifetime and source identity match the log.
        const auto dumpPath = juce::SystemStats::getEnvironmentVariable("APEX_TEST_CRASH_DUMP", {});
        const auto writeDump = reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(library, "MiniDumpWriteDump"));
        if (writeDump != nullptr && dumpPath.isNotEmpty())
        {
            const auto file = CreateFileW(dumpPath.toWideCharPointer(), GENERIC_WRITE, 0,
                                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION info { GetCurrentThreadId(), exception, FALSE };
                writeDump(process, GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
                CloseHandle(file);
            }
        }
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void install()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(reportNativeCrash);
}
}
#endif

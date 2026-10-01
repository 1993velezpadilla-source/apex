// Minimal test to isolate startup crash
#include <windows.h>
#include <iostream>
#include <fstream>

void writeLog(const char* msg)
{
    std::ofstream log("C:\\temp\\daw_crash_test.log", std::ios::app);
    log << msg << std::endl;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    writeLog("Test EXE started");

    // Try to load the actual DAW exe and see what happens
    HMODULE hMod = LoadLibraryA("DAW_Core.exe");
    if (!hMod)
    {
        DWORD err = GetLastError();
        writeLog(("Failed to load DAW_Core.exe, error: " + std::to_string(err)).c_str());
    }
    else
    {
        writeLog("DAW_Core.exe loaded successfully");
        FreeLibrary(hMod);
    }

    writeLog("Test EXE finished");
    return 0;
}

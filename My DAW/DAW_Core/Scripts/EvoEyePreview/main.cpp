// Captures the ACTUAL APEX C++ SplashScreen renderer, never generated art.
// No production code is duplicated. JUCE shim contains only math utilities.
#include "../../Source/SplashScreen.h"
#include <filesystem>
#include <iostream>
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    std::filesystem::path dir(argv[1]);
    std::filesystem::create_directories(dir);
    const auto open = (dir / L"open.png").wstring();
    const auto closed = (dir / L"closed.png").wstring();
    const bool a = DAWSplashScreen::renderDiagnosticFrame(open.c_str(), 0.0f);
    const bool b = DAWSplashScreen::renderDiagnosticFrame(closed.c_str(), 0.69f);
    std::wcout << L"APEX procedural eye open=" << a << L" closed=" << b << L"\n";
    return (a && b) ? 0 : 1;
}
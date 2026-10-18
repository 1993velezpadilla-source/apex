// Captures the ACTUAL APEX C++ SplashScreen renderer, never generated art.
// No production code is duplicated. JUCE shim contains only math utilities.
#include "../../Source/SplashScreen.h"
#include <filesystem>
#include <iostream>
#include <cmath>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;

    // A real splash must blink AGAIN AND AGAIN until StartupPanel is shown.
    // Sample seven complete cycles, including beyond the old 12-second limit.
    // Verify the exact renderer's phase function; don't use a separate fake
    // animation model or prerecorded sprite sequence.
    constexpr double blinkPeriod = 3.8;
    for (int cycle = 0; cycle < 7; ++cycle)
    {
        const double whenOpen = blinkPeriod * cycle;
        const double whenClosed = whenOpen + 0.69;
        const float opening = DAWSplashScreen::diagnosticBlinkAmount(whenOpen);
        const float closing = DAWSplashScreen::diagnosticBlinkAmount(whenClosed);
        std::wcout << L"APEX blink cycle " << cycle + 1
                   << L" open=" << opening << L" closed=" << closing
                   << L" t=" << whenClosed << L"s\n";
        if (std::fabs(opening) > 0.001f || closing < 0.95f)
            return 3;
    }

    std::filesystem::path dir(argv[1]);
    std::filesystem::create_directories(dir);
    const auto open = (dir / L"open.png").wstring();
    const auto closed = (dir / L"closed.png").wstring();
    const auto repeated = (dir / L"blink_after_5_cycles.png").wstring();
    const bool a = DAWSplashScreen::renderDiagnosticFrame(open.c_str(), 0.0f);
    const bool b = DAWSplashScreen::renderDiagnosticFrame(closed.c_str(), 0.69f);
    const bool c = DAWSplashScreen::renderDiagnosticFrame(
        repeated.c_str(), static_cast<float>(0.69 + 5.0 * blinkPeriod));
    std::wcout << L"APEX procedural eye open=" << a
               << L" closed=" << b << L" repeated-after-5-cycles=" << c << L"\n";
    return (a && b && c) ? 0 : 1;
}

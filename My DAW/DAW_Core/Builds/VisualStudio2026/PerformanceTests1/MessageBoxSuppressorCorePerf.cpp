#include "pch.h"
#include "CppUnitTest.h"
#include <chrono>
#include <string>
#include <windows.h>
#include "../../../Source/PluginSafetyCore/MessageBoxSuppressorCore.h"
// Minimal shims so the suppressor header compiles without full JUCE linkage.
// DBG and OutputDebugStringW are already available; juce::String is shimmed in pch.h.


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MessageBoxSuppressorCorePerf
{
    // -----------------------------------------------------------------------
    // Isolate the cost of one EnumWindows call — the exact work done by
    // timerCallback() each tick — so we can measure before/after overhead.
    // -----------------------------------------------------------------------
    static DWORD runEnumWindowsScan()
    {
        DWORD hits = 0;
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL
        {
            wchar_t className[256];
            GetClassNameW(hwnd, className, 256);
            if (wcscmp(className, L"#32770") == 0)
            {
                wchar_t title[512];
                wchar_t text[1024];
                GetWindowTextW(hwnd, title, 512);
                HWND textCtrl = FindWindowExW(hwnd, nullptr, L"Static", nullptr);
                if (textCtrl) GetWindowTextW(textCtrl, text, 1024);
                else          text[0] = 0;

                // Same string matching logic as the production timerCallback
                std::wstring t(title), b(text);
                auto ci = [](const std::wstring& hay, const wchar_t* needle) {
                    std::wstring h(hay), n(needle);
                    std::transform(h.begin(), h.end(), h.begin(), ::towlower);
                    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
                    return h.find(n) != std::wstring::npos;
                };
                bool suspicious =
                    ci(t, L"tls initialization") || ci(t, L"failed") ||
                    ci(t, L"error")               || ci(b, L"tls initialization") ||
                    ci(b, L"application control policy") ||
                    ci(b, L"blocked this file")   || ci(b, L"failed to load") ||
                    ci(b, L"tls library")         || ci(b, L"wibu");

                if (suspicious)
                    ++(*reinterpret_cast<DWORD*>(lp));
            }
            return TRUE;
        }, (LPARAM)&hits);
        return hits;
    }

    TEST_CLASS(MessageBoxSuppressorCorePerformanceTest)
    {
    public:
        // ------------------------------------------------------------------
        // Baseline: cost of EnumWindows scan at the current 1 ms fire rate.
        // 1000 iterations mirrors exactly 1 second of real DAW runtime.
        // ------------------------------------------------------------------
        TEST_METHOD(Baseline_EnumWindows_1000Iters)
        {
            constexpr int kIters = 1000;

            // warm-up
            for (int i = 0; i < 5; ++i)
                runEnumWindowsScan();

            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < kIters; ++i)
                runEnumWindowsScan();
            auto t1 = std::chrono::high_resolution_clock::now();

            const double totalMs   = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double perCallUs = (totalMs / kIters) * 1000.0;

            std::wstring msg =
                L"[MessageBoxSuppressor Baseline — EnumWindows @ 1ms rate]\n"
                L"  Iterations      : " + std::to_wstring(kIters) + L"\n"
                L"  Total time      : " + std::to_wstring((long long)totalMs) + L" ms\n"
                L"  Per-call (avg)  : " + std::to_wstring((long long)perCallUs) + L" µs\n"
                L"  1-second budget : 1 000 000 µs\n"
                L"  Scan overhead   : " + std::to_wstring((long long)(totalMs / 1000.0 * 100.0)) + L"% of wall time\n";
            Logger::WriteMessage(msg.c_str());

            Assert::IsTrue(std::isfinite(totalMs), L"totalMs is non-finite");
        }
    };
}

#pragma once
#include <JuceHeader.h>

/**
 * MessageBoxSuppressorCore
 *
 * Nucleus: suppresses ALL MessageBox dialogs process-wide by
 * automatically closing them with a background timer.
 *
 * Why this is necessary:
 *   Copy-protection DLLs (Wibu CodeMeter, etc.) call MessageBoxA/W
 *   to show "TLS initialization failed" dialogs. These freeze the scan.
 *   SetErrorMode does NOT suppress user-mode message boxes.
 *
 * How it works:
 *   Polls for MessageBox windows every 50ms and closes them with WM_CLOSE.
 *   Lightweight, no IAT patching needed, works even if DLLs load late.
 */

#if JUCE_WINDOWS
#include <windows.h>

namespace DAW {

class MessageBoxSuppressorCore : private juce::Timer
{
public:
    MessageBoxSuppressorCore()
    {
        // Timer starts stopped; call setScanActive(true) when a plugin scan begins.
        // This keeps EnumWindows cost at zero during normal DAW operation.
    }

    ~MessageBoxSuppressorCore()
    {
        stopTimer();
    }

    /** Call with true when a plugin scan starts, false when it ends.
     *  While active the suppressor polls at 500 ms — fast enough to catch
     *  any dialog before the user sees it, cheap enough to be negligible. */
    void setScanActive(bool active)
    {
        if (active)
            startTimer(kPollIntervalMs);
        else
            stopTimer();
    }

private:
    // 500 ms: catches dialogs well within human reaction time (~200 ms to notice)
    // while reducing EnumWindows call rate 500x vs the previous 1 ms interval.
    static constexpr int kPollIntervalMs = 500;

    void timerCallback() override
    {
        EnumWindows([](HWND hwnd, LPARAM) -> BOOL
        {
            wchar_t className[256];
            GetClassNameW(hwnd, className, 256);

            // MessageBox windows use class name "#32770" (dialog class)
            if (wcscmp(className, L"#32770") == 0)
            {
                wchar_t title[512];
                wchar_t text[1024];
                GetWindowTextW(hwnd, title, 512);

                HWND textCtrl = FindWindowExW(hwnd, nullptr, L"Static", nullptr);
                if (textCtrl)
                    GetWindowTextW(textCtrl, text, 1024);
                else
                    text[0] = 0;

                juce::String titleStr(title);
                juce::String textStr(text);

                bool isSuspiciousDialog =
                    titleStr.containsIgnoreCase("TLS initialization") ||
                    titleStr.containsIgnoreCase("failed") ||
                    titleStr.containsIgnoreCase("error") ||
                    textStr.containsIgnoreCase("TLS initialization") ||
                    textStr.containsIgnoreCase("Application Control policy") ||
                    textStr.containsIgnoreCase("blocked this file") ||
                    textStr.containsIgnoreCase("failed to load") ||
                    textStr.containsIgnoreCase("tls library") ||
                    textStr.containsIgnoreCase("wibu");

                if (isSuspiciousDialog)
                {
                    juce::String msg = "[DAW] Auto-closed MessageBox: \""
                        + titleStr + "\" - \"" + textStr + "\"";
                    DBG(msg);
                    OutputDebugStringW(msg.toWideCharPointer());
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            }
            return TRUE;
        }, 0);
    }
};

} // namespace DAW

#else // Non-Windows stub

namespace DAW {
class MessageBoxSuppressorCore
{
public:
    MessageBoxSuppressorCore() = default;
    ~MessageBoxSuppressorCore() = default;
};
} // namespace DAW

#endif

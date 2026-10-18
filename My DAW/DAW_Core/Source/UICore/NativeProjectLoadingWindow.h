#pragma once
#include <JuceHeader.h>

// Project restore intentionally executes on the JUCE control/message thread:
// native/plugin state cannot safely be moved onto an arbitrary worker. A JUCE
// Component::repaint() cannot paint while that thread is restoring plugins or
// decoding audio, so the user sees either NO bar or only the final frame.
// This tiny independent Win32 UI thread renders completed-work checkpoints.
// It never touches DAW, plugin, or audio objects.
#if JUCE_WINDOWS
#include <windows.h>
#include <gdiplus.h>
#include <atomic>
#include <mutex>
#include <string>
#pragma comment(lib, "gdiplus.lib")

namespace DAW {
class NativeProjectLoadingWindow final
{
public:
    NativeProjectLoadingWindow() = default;
    ~NativeProjectLoadingWindow() { dismiss(); }

    void show(const juce::String& name)
    {
        dismiss();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            project_ = name.toWideCharPointer();
            stage_ = L"Preparing project";
            item_.clear();
            percent_ = 0;
            failed_ = false;
        }
        stopped_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopped_ || !ready_)
        {
            dismiss();
            return;
        }
        thread_ = CreateThread(nullptr, 0, &NativeProjectLoadingWindow::threadEntry,
                               this, 0, nullptr);
        if (thread_)
            WaitForSingleObject(ready_, 1200);
    }

    void update(const juce::String& stage, const juce::String& item, double fraction)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stage_ = stage.toWideCharPointer();
        item_ = item.toWideCharPointer();
        // Fractions are measured by ProjectManager's completed restore steps.
        // Never rewind the fill or increment it with elapsed wall-clock time.
        const int completed = juce::jlimit(0, 100, juce::roundToInt(fraction * 100.0));
        if (!failed_ && completed > percent_)
            percent_ = completed;
    }

    void finish(bool success, const juce::String& error = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = !success;
        if (success)
        {
            stage_ = L"Project ready";
            item_.clear();
            percent_ = 100; // Called only AFTER loadFromFile returned true.
        }
        else
        {
            stage_ = L"Project load failed";
            item_ = error.toWideCharPointer();
            // Error is NOT a completed load. Never paint a 100% failure bar.
            percent_ = juce::jmin(percent_, 99);
        }
    }

    void dismiss()
    {
        if (stopped_) SetEvent(stopped_);
        if (thread_)
        {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
        if (stopped_) { CloseHandle(stopped_); stopped_ = nullptr; }
    }

private:
    static DWORD WINAPI threadEntry(void* context)
    {
        return static_cast<NativeProjectLoadingWindow*>(context)->run();
    }

    DWORD run()
    {
        using namespace Gdiplus;
        GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (GdiplusStartup(&token, &input, nullptr) != Ok)
        {
            SetEvent(ready_);
            return 1;
        }

        const wchar_t* name = L"APEX_ProjectLoading_Progress";
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = &NativeProjectLoadingWindow::windowProc;
        wc.lpszClassName = name;
        wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
        RegisterClassExW(&wc); // ERROR_CLASS_ALREADY_EXISTS is harmless.
        constexpr int width = 520, height = 194;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            name, L"APEX Project Loading", WS_POPUP,
            x, y, width, height, nullptr, nullptr, wc.hInstance, this);
        if (!hwnd)
        {
            SetEvent(ready_);
            GdiplusShutdown(token);
            return 2;
        }
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        SetEvent(ready_);

        // Own message loop remains responsive even while JUCE is blocked on
        // synchronous third-party plugin restore or worker-pool audio decode.
        while (WaitForSingleObject(stopped_, 40) == WAIT_TIMEOUT)
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }
        DestroyWindow(hwnd);
        GdiplusShutdown(token);
        return 0;
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        auto* self = reinterpret_cast<NativeProjectLoadingWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(hwnd, &ps);
            if (self && dc) self->paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void paint(HDC dc)
    {
        using namespace Gdiplus;
        std::wstring name, stage, item;
        int percent;
        bool failed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            name = project_; stage = stage_; item = item_;
            percent = percent_; failed = failed_;
        }
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        SolidBrush backdrop(Color(255, 12, 9, 24));
        g.FillRectangle(&backdrop, 0, 0, 520, 194);
        Pen border(Color(255, 139, 50, 144), 1.5f);
        g.DrawRectangle(&border, 1, 1, 517, 191);
        Pen accent(Color(255, 238, 34, 119), 3.0f);
        g.DrawLine(&accent, 28, 19, 116, 19);

        FontFamily family(L"Segoe UI");
        Font titleFont(&family, 18, FontStyleBold, UnitPixel);
        Font bodyFont(&family, 13, FontStyleRegular, UnitPixel);
        Font percentFont(&family, 12, FontStyleBold, UnitPixel);
        SolidBrush primary(Color(255, 238, 232, 251));
        SolidBrush secondary(Color(255, 170, 164, 190));
        SolidBrush error(Color(255, 255, 88, 104));
        g.DrawString(L"APEX  /  LOADING PROJECT", -1, &titleFont,
                     PointF(28, 26), &primary);
        g.DrawString(name.c_str(), -1, &bodyFont,
                     RectF(28, 60, 456, 22), nullptr, &secondary);
        g.DrawString(stage.c_str(), -1, &bodyFont,
                     RectF(28, 92, 360, 21), nullptr, failed ? &error : &primary);
        if (!item.empty())
            g.DrawString(item.c_str(), -1, &bodyFont,
                         RectF(28, 113, 450, 17), nullptr, &secondary);
        const auto value = std::to_wstring(percent) + L"%";
        g.DrawString(value.c_str(), -1, &percentFont,
                     PointF(460, 94), failed ? &error : &primary);

        constexpr float bx = 28.0f, by = 151.0f, bw = 464.0f, bh = 13.0f;
        SolidBrush track(Color(255, 49, 35, 64));
        g.FillRectangle(&track, bx, by, bw, bh);
        if (percent > 0)
        {
            LinearGradientBrush fill(PointF(bx, by), PointF(bx + bw, by),
                                     Color(255, 241, 27, 116),
                                     Color(255, 67, 210, 239));
            g.FillRectangle(&fill, bx, by, bw * percent / 100.0f, bh);
        }
        Pen rim(Color(255, 100, 69, 127), 1.0f);
        g.DrawRectangle(&rim, bx, by, bw, bh);
    }

    HANDLE stopped_ = nullptr;
    HANDLE ready_ = nullptr;
    HANDLE thread_ = nullptr;
    std::mutex mutex_;
    std::wstring project_, stage_, item_;
    int percent_ = 0;
    bool failed_ = false;
};
} // namespace DAW
#else
namespace DAW {
class NativeProjectLoadingWindow final
{
public:
    void show(const juce::String&) {}
    void update(const juce::String&, const juce::String&, double) {}
    void finish(bool, const juce::String& = {}) {}
    void dismiss() {}
};
} // namespace DAW
#endif

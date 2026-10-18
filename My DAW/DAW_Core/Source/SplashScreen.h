#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// The splash has its OWN Win32 message/render thread. A JUCE Timer cannot
// animate during MainComponent construction (the JUCE message thread is busy).
// No audio/DSP thread ever calls into the splash.
class DAWSplashScreen final
{
public:
    // Embedded Win32 RCDATA only: the installed EXE never opens a logo file.
    static DAWSplashScreen* launch()
    {
        std::unique_ptr<DAWSplashScreen> splash(new DAWSplashScreen());

        splash->ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        splash->stop_  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (splash->ready_ == nullptr || splash->stop_ == nullptr)
            return nullptr;

        splash->thread_ = CreateThread(nullptr, 0, &DAWSplashScreen::runThread,
                                       splash.get(), 0, nullptr);
        if (splash->thread_ == nullptr)
            return nullptr;

        // Give Win32 enough time to create and paint the first splash frame.
        // If startup fails, the destructor closes and joins the worker.
        const auto wait = WaitForSingleObject(splash->ready_, 4000);
        if (wait != WAIT_OBJECT_0 || ! splash->valid_.load())
            return nullptr;

        return splash.release();
    }

    // Called once APEX's real startup panel is ready behind the splash.
    // The worker performs a 220 ms fade and owns destruction of its HWND.
    void finish()
    {
        delete this;
    }

    ~DAWSplashScreen()
    {
        if (stop_ != nullptr)
            SetEvent(stop_);
        if (thread_ != nullptr)
        {
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (ready_ != nullptr) CloseHandle(ready_);
        if (stop_  != nullptr) CloseHandle(stop_);
    }

private:
    DAWSplashScreen() = default;

    static constexpr int kEvoEyeResourceId = 27182;

    static DWORD WINAPI runThread(void* context)
    {
        return static_cast<DAWSplashScreen*>(context)->run();
    }

    // Smooth close-and-open at ~3.8 second intervals, not a repetitive
    // mechanical flashing pattern. Starts fully open on the first frame.
    static float blinkAmount(double seconds)
    {
        const double phase = std::fmod(seconds, 3.8);
        if (phase < 2.76 || phase > 3.08)
            return 0.0f;
        if (phase < 2.88)
        {
            const float t = static_cast<float>((phase - 2.76) / 0.12);
            return t * t * (3.0f - 2.0f * t);
        }
        const float t = static_cast<float>((phase - 2.88) / 0.20);
        const float smooth = t * t * (3.0f - 2.0f * t);
        return 1.0f - smooth;
    }

    static void drawBlink(Gdiplus::Graphics& g, int w, int h, float closure)
    {
        if (closure <= 0.001f)
            return;

        // The photo's external spiked armor remains fixed. Two soft curved,
        // obsidian lids mask only the luminous eye interior.
        using namespace Gdiplus;
        const float x0 = w * 0.257f, x1 = w * 0.743f;
        const float centre = h * 0.405f;
        const float top = h * 0.277f;
        const float bottom = h * 0.532f;
        const float lidTop = top + (centre - top) * closure;
        const float lidBottom = bottom - (bottom - centre) * closure;
        const float arc = h * 0.022f;

        SolidBrush obsidian(Color(248, 6, 4, 12));
        Pen edge(Color(235, 243, 35, 102), juce::jmax(1.0f, w * 0.0015f));

        GraphicsPath upper;
        upper.AddLine(x0, top, x1, top);
        upper.AddBezier(x1, top, x1 - w * 0.10f, lidTop - arc,
                        x0 + w * 0.10f, lidTop - arc, x0, lidTop);
        upper.CloseFigure();
        g.FillPath(&obsidian, &upper);
        g.DrawBezier(&edge, x1, lidTop, x1 - w * 0.10f, lidTop - arc,
                     x0 + w * 0.10f, lidTop - arc, x0, lidTop);

        GraphicsPath lower;
        lower.AddLine(x0, bottom, x1, bottom);
        lower.AddBezier(x1, bottom, x1 - w * 0.10f, lidBottom + arc,
                        x0 + w * 0.10f, lidBottom + arc, x0, lidBottom);
        lower.CloseFigure();
        g.FillPath(&obsidian, &lower);
        g.DrawBezier(&edge, x1, lidBottom, x1 - w * 0.10f, lidBottom + arc,
                     x0 + w * 0.10f, lidBottom + arc, x0, lidBottom);
    }

    static void drawSubtitle(Gdiplus::Graphics& g, int w, int h)
    {
        using namespace Gdiplus;
        // Replace the baked-in artwork caption; never ship the old wording.
        // This affects just the bottom subtitle, not the original Evo Eye art.
        SolidBrush wipe(Color(244, 7, 5, 12));
        g.FillRectangle(&wipe, RectF(w * 0.300f, h * 0.795f,
                                    w * 0.400f, h * 0.036f));

        FontFamily family(L"Segoe UI");
        Font font(&family, juce::jmax(10.0f, w * 0.0115f),
                  FontStyleRegular, UnitPixel);
        SolidBrush text(Color(238, 218, 204, 217));
        StringFormat align;
        align.SetAlignment(StringAlignmentCenter);
        align.SetLineAlignment(StringAlignmentCenter);
        const wchar_t* caption = L"AUDIO PRODUCTION ENGINEERING";
        g.DrawString(caption, -1, &font,
                     RectF(w * 0.24f, h * 0.795f, w * 0.52f, h * 0.036f),
                     &align, &text);
    }

    DWORD run()
    {
        using namespace Gdiplus;
        GdiplusStartupInput input;
        // Keep GDI+ alive until all bitmaps and the IStream have been freed.
        struct GdiSession
        {
            ULONG_PTR token = 0;
            ~GdiSession() { if (token != 0) GdiplusShutdown(token); }
        } gdip;
        if (GdiplusStartup(&gdip.token, &input, nullptr) != Ok)
        {
            SetEvent(ready_);
            return 1;
        }

        // Win32 resource 27182 is linked INTO DAW_Core.exe at build time.
        // SHCreateMemStream exposes it without touching the filesystem.
        const HMODULE module = GetModuleHandleW(nullptr);
        const HRSRC resource = FindResourceW(
            module, MAKEINTRESOURCEW(kEvoEyeResourceId), RT_RCDATA);
        if (resource == nullptr)
        {
            SetEvent(ready_);
            return 2;
        }
        const DWORD size = SizeofResource(module, resource);
        const HGLOBAL loaded = LoadResource(module, resource);
        const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (bytes == nullptr || size == 0)
        {
            SetEvent(ready_);
            return 3;
        }
        struct StreamRelease
        {
            void operator()(IStream* stream) const noexcept
            {
                if (stream != nullptr) stream->Release();
            }
        };
        std::unique_ptr<IStream, StreamRelease> stream(
            SHCreateMemStream(static_cast<const BYTE*>(bytes), size));
        if (! stream)
        {
            SetEvent(ready_);
            return 4;
        }
        std::unique_ptr<Bitmap> photo(Bitmap::FromStream(stream.get(), FALSE));
        if (! photo || photo->GetLastStatus() != Ok ||
            photo->GetWidth() == 0 || photo->GetHeight() == 0)
        {
            SetEvent(ready_);
            return 5;
        }

        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const float scale = juce::jmin(1100.0f / photo->GetWidth(),
                               juce::jmin(640.0f / photo->GetHeight(),
                               juce::jmin(screenW * 0.84f / photo->GetWidth(),
                                          screenH * 0.78f / photo->GetHeight())));
        const int w = juce::jmax(1, juce::roundToInt(photo->GetWidth() * scale));
        const int h = juce::jmax(1, juce::roundToInt(photo->GetHeight() * scale));
        const int x = (screenW - w) / 2, y = (screenH - h) / 2;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"APEX_EvoEye_Splash";
        RegisterClassExW(&wc);

        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW
                                      | WS_EX_NOACTIVATE,
                                    wc.lpszClassName, L"", WS_POPUP,
                                    x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
        if (hwnd == nullptr)
        {
            SetEvent(ready_);
            return 6;
        }

        HDC desktop = GetDC(nullptr);
        HDC memoryDC = CreateCompatibleDC(desktop);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP dib = memoryDC != nullptr
            ? CreateDIBSection(memoryDC, &info, DIB_RGB_COLORS, &pixels, nullptr, 0)
            : nullptr;
        if (dib == nullptr)
        {
            if (memoryDC) DeleteDC(memoryDC);
            if (desktop) ReleaseDC(nullptr, desktop);
            DestroyWindow(hwnd);
            SetEvent(ready_);
            return 7;
        }

        HGDIOBJ old = SelectObject(memoryDC, dib);
        const ULONGLONG startedAt = GetTickCount64();
        ULONGLONG finishAt = 0;
        bool publishedReady = false;

        for (;;)
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            const auto now = GetTickCount64();
            if (finishAt == 0 && WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
                finishAt = now;
            const float fade = finishAt == 0 ? 1.0f
                : juce::jlimit(0.0f, 1.0f, 1.0f - (now - finishAt) / 220.0f);
            if (fade <= 0.0f)
                break;

            {
                Graphics g(memoryDC);
                g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.Clear(Color(255, 0, 0, 0));
                g.DrawImage(photo.get(), Rect(0, 0, w, h));
                const double elapsed = (now - startedAt) / 1000.0;
                drawBlink(g, w, h, blinkAmount(elapsed));
                drawSubtitle(g, w, h);

                // A soft alive/energy pulse along the image's existing
                // loading line. Not a fake percentage of plugin scanning.
                const float power = static_cast<float>(
                    0.55 + 0.45 * std::sin(elapsed * 3.0));
                Pen pulse(Color(static_cast<BYTE>(90 + 110 * power), 255, 35, 112),
                          juce::jmax(1.0f, h * 0.003f));
                g.DrawLine(&pulse, w * 0.40f, h * 0.854f,
                                  w * (0.49f + 0.06f * power), h * 0.854f);
            }

            POINT pos = { x, y }, origin = { 0, 0 };
            SIZE dimensions = { w, h };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0,
                                    static_cast<BYTE>(255.0f * fade), AC_SRC_ALPHA };
            UpdateLayeredWindow(hwnd, desktop, &pos, &dimensions, memoryDC,
                                &origin, 0, &blend, ULW_ALPHA);
            if (! publishedReady)
            {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                valid_.store(true);
                SetEvent(ready_);
                publishedReady = true;
            }
            Sleep(33); // 30 fps on a lightweight splash-only Win32 worker
        }

        if (! publishedReady)
            SetEvent(ready_);
        SelectObject(memoryDC, old);
        DeleteObject(dib);
        DeleteDC(memoryDC);
        ReleaseDC(nullptr, desktop);
        DestroyWindow(hwnd);
        return 0;
    }

     HANDLE ready_ = nullptr;
    HANDLE stop_ = nullptr;
    HANDLE thread_ = nullptr;
    std::atomic<bool> valid_ { false };
};

#else
class DAWSplashScreen
{
public:
    static DAWSplashScreen* launch() { return nullptr; }
    void finish() { delete this; }
};
#endif

#pragma once
#include <JuceHeader.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

/**
 * True OS-level splash screen using a Win32 WS_EX_LAYERED popup window
 * drawn via GDI+ and UpdateLayeredWindow. This paints synchronously at
 * the OS level — completely independent of JUCE's message loop, so the
 * logo appears instantly even while MainComponent blocks the thread.
 */
class DAWSplashScreen
{
public:
    static DAWSplashScreen* launch(const juce::File& imageFile)
    {
        auto* s = new DAWSplashScreen(imageFile);
        if (!s->valid_) { delete s; return nullptr; }
        return s;
    }

    // Called once the DAW is ready — destroy the splash immediately.
    void finish()
    {
        destroyWindow();
        delete this;
    }

    ~DAWSplashScreen() { destroyWindow(); }

private:
    HWND       hwnd_         = nullptr;
    ULONG_PTR  gdiplusToken_ = 0;
    bool       valid_        = false;

    explicit DAWSplashScreen(const juce::File& imageFile)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr);

        // Load PNG via GDI+ (preserves alpha channel)
        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(imageFile.getFullPathName().toWideCharPointer());
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return; }

        // Scale to fit 55 % of the screen, max 700 x 500
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int maxW = juce::jmin(700, (int)(screenW * 0.55f));
        const int maxH = juce::jmin(500, (int)(screenH * 0.55f));
        const float scale = juce::jmin((float)maxW / (float)juce::jmax(1u, bmp->GetWidth()),
                                        (float)maxH / (float)juce::jmax(1u, bmp->GetHeight()));
        const int w = juce::roundToInt((float)bmp->GetWidth()  * scale);
        const int h = juce::roundToInt((float)bmp->GetHeight() * scale);
        const int x = (screenW - w) / 2;
        const int y = (screenH - h) / 2;

        // Register minimal window class
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DAW_SplashWnd";
        RegisterClassExW(&wc);

        // Popup + layered + topmost = borderless with per-pixel alpha
        hwnd_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"DAW_SplashWnd", L"",
            WS_POPUP,
            x, y, w, h,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (!hwnd_) { delete bmp; return; }

        // Create a 32-bit memory DC and draw the logo into it with GDI+
        HDC screenDC = GetDC(nullptr);
        HDC memDC    = CreateCompatibleDC(screenDC);

        BITMAPINFOHEADER bmh = {};
        bmh.biSize        = sizeof(bmh);
        bmh.biWidth       = w;
        bmh.biHeight      = -h;   // top-down
        bmh.biPlanes      = 1;
        bmh.biBitCount    = 32;
        bmh.biCompression = BI_RGB;
        BITMAPINFO bmi    = {};
        bmi.bmiHeader     = bmh;

        void*   bits    = nullptr;
        HBITMAP hBmp    = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(memDC, hBmp);

        {
            Gdiplus::Graphics gfx(memDC);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            gfx.DrawImage(bmp, 0, 0, w, h);
        }
        delete bmp;

        // Push the pixels to the layered window — true per-pixel alpha
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        POINT ptDst = { x, y };
        SIZE  sz    = { w, h };
        POINT ptSrc = { 0, 0 };
        UpdateLayeredWindow(hwnd_, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(memDC, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);

        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        valid_ = true;
    }

    void fadeAndDestroy()
    {
        // Quick alpha fade: 255 → 0 in ~300 ms using a simple loop
        for (int alpha = 255; alpha >= 0; alpha -= 15)
        {
            SetLayeredWindowAttributes(hwnd_, 0, (BYTE)juce::jmax(0, alpha), LWA_ALPHA);
            Sleep(12);
        }
        destroyWindow();
        delete this; // self-managed
    }

    void destroyWindow()
    {
        if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
        if (gdiplusToken_) { Gdiplus::GdiplusShutdown(gdiplusToken_); gdiplusToken_ = 0; }
    }
};

#else
// Non-Windows stub
class DAWSplashScreen
{
public:
    static DAWSplashScreen* launch(const juce::File&) { return nullptr; }
    void finish() { delete this; }
};
#endif


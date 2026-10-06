#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace DAW {

void patchPluginEditorWindowExStyle(juce::Component* window)
{
#if JUCE_WINDOWS
    if (window == nullptr) return;
    auto* peer = window->getPeer();
    if (peer == nullptr) return;
    HWND hwnd = static_cast<HWND>(peer->getNativeHandle());
    if (hwnd == nullptr) return;

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    // WS_EX_TOOLWINDOW hides the window from the Windows native taskbar so it
    // does not appear as a separate taskbar entry. Plugin windows live in the
    // DAW's own bubble taskbar instead. WS_EX_NOACTIVATE prevents a taskbar
    // flash on open. Keep both set at all times.
    exStyle |= static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    exStyle |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
#else
    juce::ignoreUnused(window);
#endif
}

// ?? Per-HWND SC_MINIMIZE interceptor ?????????????????????????????????????
// Subclasses the Win32 WndProc to catch WM_SYSCOMMAND SC_MINIMIZE before
// JUCE processes it (which calls setMinimised(true) -> SW_MINIMIZE ->
// Windows taskbar / owned-strip). We swallow the message and fire the
// DAW minimize callback instead, which routes into the bubble taskbar.

#if JUCE_WINDOWS
struct MinimizeEntry
{
    WNDPROC               origProc = nullptr;
    std::function<void()> callback;
};

static std::unordered_map<HWND, MinimizeEntry*> g_minEntries;

static LRESULT CALLBACK MinimizeSubclassProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam)
{
    auto it = g_minEntries.find(hwnd);
    MinimizeEntry* e = (it != g_minEntries.end()) ? it->second : nullptr;

    if (e)
    {
        if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MINIMIZE)
        {
            // Swallow SC_MINIMIZE entirely and route to DAW bubble taskbar.
            if (e->callback)
                juce::MessageManager::callAsync(e->callback);
            return 0;
        }
        if (msg == WM_DESTROY)
        {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                             reinterpret_cast<LONG_PTR>(e->origProc));
            g_minEntries.erase(it);
            WNDPROC orig = e->origProc;
            delete e;
            return CallWindowProc(orig, hwnd, msg, wParam, lParam);
        }
    }

    return e ? CallWindowProc(e->origProc, hwnd, msg, wParam, lParam)
             : DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

void installMinimizeInterceptor(juce::Component* window,
                                 std::function<void()> onMinimize)
{
#if JUCE_WINDOWS
    if (!window) return;
    auto* peer = window->getPeer();
    if (!peer) return;
    HWND hwnd = static_cast<HWND>(peer->getNativeHandle());
    if (!hwnd) return;

    auto it = g_minEntries.find(hwnd);
    if (it != g_minEntries.end())
    {
        it->second->callback = std::move(onMinimize);
        return;
    }

    auto* e = new MinimizeEntry();
    e->callback = std::move(onMinimize);
    e->origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(MinimizeSubclassProc)));
    g_minEntries[hwnd] = e;
#else
    juce::ignoreUnused(window, onMinimize);
#endif
}

} // namespace DAW
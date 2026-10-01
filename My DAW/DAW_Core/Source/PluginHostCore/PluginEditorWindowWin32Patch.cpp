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

void patchPluginEditorWindowExStyle(juce::Component* window,
                                    juce::Component* ownerComponent)
{
#if JUCE_WINDOWS
    if (window == nullptr) return;
    auto* peer = window->getPeer();
    if (peer == nullptr) return;
    HWND hwnd = static_cast<HWND>(peer->getNativeHandle());
    if (hwnd == nullptr) return;

    // Some plugin windows (e.g. Antares Auto-Tune EFX) use non-standard native
    // windows that reject style modification or return garbage from
    // GetWindowLongPtr.  Validate the HWND and the read result before writing.
    if (!::IsWindow(hwnd)) return;
    ::SetLastError(ERROR_SUCCESS);
    LONG_PTR exStyle = ::GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle == 0 && ::GetLastError() != 0) return;

    exStyle |= static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);
    // TOOLWINDOW suppresses taskbar/Alt-Tab by itself. NOACTIVATE is invalid
    // for an interactive editor that needs keyboard, IME and accessibility.
    exStyle &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

    // Establish a modeless top-level owner. The editor then stays above its
    // APEX window when another DAW panel is clicked without becoming globally
    // topmost over unrelated applications.
    if (ownerComponent != nullptr)
    {
        auto* ownerTop = ownerComponent->getTopLevelComponent();
        auto* ownerPeer = ownerTop != nullptr ? ownerTop->getPeer() : nullptr;
        HWND ownerHwnd = ownerPeer != nullptr
            ? static_cast<HWND>(ownerPeer->getNativeHandle()) : nullptr;
        if (ownerHwnd != nullptr && ::IsWindow(ownerHwnd))
        {
            ownerHwnd = ::GetAncestor(ownerHwnd, GA_ROOT);
            if (ownerHwnd != nullptr && ownerHwnd != hwnd)
                ::SetWindowLongPtr(hwnd, GWLP_HWNDPARENT,
                                   reinterpret_cast<LONG_PTR>(ownerHwnd));
        }
    }

    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
#else
    juce::ignoreUnused(window, ownerComponent);
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

    auto origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(MinimizeSubclassProc)));

    // If WndProc replacement failed (non-standard plugin windows such as
    // Antares Auto-Tune EFX), do NOT install the subclass — calling
    // CallWindowProc with a NULL origProc would crash the host.
    if (!origProc)
        return;

    auto* e = new MinimizeEntry();
    e->callback = std::move(onMinimize);
    e->origProc = origProc;
    g_minEntries[hwnd] = e;
#else
    juce::ignoreUnused(window, onMinimize);
#endif
}

} // namespace DAW

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

#include "GlobalTransportSpaceHook.h"
#include "KeyBindingManager.h"

namespace DAW {

#if JUCE_WINDOWS

static HHOOK g_spaceHook = nullptr;
static std::atomic<bool> g_spaceHookInstalled { false };

static LRESULT CALLBACK globalTransportSpaceHookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == PM_REMOVE)
    {
        auto& msg = *reinterpret_cast<MSG*>(lParam);

        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
        {
            if (msg.wParam == VK_SPACE)
            {
                // Auto-repeat (held key): bit 30 of lParam indicates the key
                // was already down. Never re-trigger the toggle.
                if ((msg.lParam & 0x40000000) != 0)
                    return CallNextHookEx(g_spaceHook, nCode, wParam, lParam);

                // Modified space (Ctrl/Alt/Shift+Space) passes through so
                // plugins and future bindings keep those combos.
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
                    || (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0
                    || (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0)
                    return CallNextHookEx(g_spaceHook, nCode, wParam, lParam);

                // Text-input exception: if the focused JUCE component is a
                // text editor, let JUCE type the space instead of toggling.
                if (auto* fc = juce::Component::getCurrentlyFocusedComponent())
                    if (auto* peer = fc->getPeer())
                        if (peer->findCurrentTextInputTarget() != nullptr)
                            return CallNextHookEx(g_spaceHook, nCode, wParam, lParam);

                // Canonical path: profile lookup -> ActionManager dispatch.
                // This hook runs on the JUCE message thread (inside
                // GetMessage), satisfying ActionManager::dispatch's
                // message-thread contract.
                const juce::KeyPress spaceKey(juce::KeyPress::spaceKey);
                if (KeyBindingManager::getInstance().handleKeyPress(spaceKey))
                {
                    // Consume the key: replace with a harmless WM_USER so the
                    // target window (native plugin editor, floating window,
                    // main window) never sees the space, and no JUCE-side
                    // handler can double-fire.
                    msg = {};
                    msg.message = WM_USER;
                    return 0;
                }
            }
        }
    }

    return CallNextHookEx(g_spaceHook, nCode, wParam, lParam);
}

#endif // JUCE_WINDOWS

void GlobalTransportSpaceHook::install()
{
#if JUCE_WINDOWS
    if (g_spaceHookInstalled.load(std::memory_order_relaxed))
        return;

    HHOOK h = SetWindowsHookEx(WH_GETMESSAGE, &globalTransportSpaceHookCallback,
                               (HINSTANCE) juce::Process::getCurrentModuleInstanceHandle(),
                               GetCurrentThreadId());
    if (h != nullptr)
    {
        g_spaceHook = h;
        g_spaceHookInstalled.store(true, std::memory_order_relaxed);
    }
#else
    juce::ignoreUnused();
#endif
}

void GlobalTransportSpaceHook::shutdown()
{
#if JUCE_WINDOWS
    if (g_spaceHook != nullptr)
    {
        UnhookWindowsHookEx(g_spaceHook);
        g_spaceHook = nullptr;
    }
    g_spaceHookInstalled.store(false, std::memory_order_relaxed);
#else
    juce::ignoreUnused();
#endif
}

} // namespace DAW

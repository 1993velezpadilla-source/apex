/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"
#include "SplashScreen.h"
#include "PluginScanCore/PluginScanWorkerProcessCore.h"
#include "PluginSecurityCore/HostBuildTrustModeCore.h"
#include "PluginSafetyCore/MessageBoxSuppressorCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
 #pragma comment(lib, "comctl32.lib")
#endif

//==============================================================================
class DAW_CoreApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    DAW_CoreApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {
#if JUCE_WINDOWS
        // ── Suppress Windows error dialogs for DLL load failures ─────────
        // Plugins using Wibu CodeMeter, iLok, etc. try to load DLLs that
        // Windows Application Control may block. Without this, modal error
        // dialogs ("TLS initialization failed", "Bad Image") freeze the app.
        // This must happen BEFORE any plugin code runs — in both the main
        // DAW process and the scanner subprocess.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif

        DAW::PluginScanAuditLogCore::appendStartupTrace(
            "DAW_CoreApplication::initialise",
            commandLine.contains(DAW::PluginScanIpcProtocolCore::kWorkerFlag) ? "worker-process" : "main-process",
            -1,
            -1,
            "commandLine=\"" + commandLine + "\"");

        DBG("Host trust mode: "
            + DAW::HostBuildTrustModeCore::toString(DAW::HostBuildTrustModeCore::detectCurrentMode()));

        // ── Suppress user-mode MessageBox dialogs from protection DLLs ───
        // Wibu CodeMeter calls MessageBoxA/W directly to show
        // "TLS initialization failed" — SetErrorMode does NOT block these.
        // Must be created AFTER JUCE MessageManager is ready (i.e., here).
        if (!msgBoxSuppressor_)
            msgBoxSuppressor_ = std::make_unique<DAW::MessageBoxSuppressorCore>();

        if (commandLine.contains(DAW::PluginScanIpcProtocolCore::kWorkerFlag))
        {
            auto jobId = extractArg(commandLine, "--scan-job-id").getIntValue();
            auto pluginPath = extractQuotedArg(commandLine, "--scan-path");
            auto format = extractQuotedArg(commandLine, "--scan-format");
            auto fileTimestamp = extractArg(commandLine, "--scan-mod-time").getLargeIntValue();
            auto fileSize = extractArg(commandLine, "--scan-file-size").getLargeIntValue();

            DAW::PluginScanAuditLogCore::appendStartupTrace(
                "Main.cpp worker subprocess entry",
                "worker",
                -1,
                -1,
                "jobId=" + juce::String(jobId)
                    + " path=\"" + pluginPath + "\" format=" + format);

            if (pluginPath.isNotEmpty() && format.isNotEmpty())
            {
                int result = DAW::PluginScanWorkerProcessCore::run(jobId, pluginPath, format, fileTimestamp, fileSize);
                setApplicationReturnValue(result);
            }
            else
            {
                std::cerr << "Usage: --plugin-scan-worker --scan-job-id <id> --scan-path \"<path>\" --scan-format \"<format>\" --scan-mod-time <ms> --scan-file-size <bytes>" << std::endl;
                setApplicationReturnValue(1);
            }
            quit();
            return;
        }

        // ── Normal DAW mode ──────────────────────────────────────────────
        // Persistent session log — makes every [APEX-DIAG-*]/[AUTO-TRANSPORT]/
        // recording diagnostic visible in Release builds. Without this, all
        // Logger::writeToLog output is silently discarded outside a debugger.
        {
            auto logDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("DAW_Core_Projects").getChildFile("Logs");
            logDir.createDirectory();
            fileLogger_.reset(new juce::FileLogger(
                logDir.getChildFile("DAW_Core_Session_"
                    + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".log"),
                "DAW_Core session log - " + juce::Time::getCurrentTime().toString(true, true), 0));
            juce::Logger::setCurrentLogger(fileLogger_.get());
            juce::Logger::writeToLog("[SESSION] DAW_Core " + juce::String(ProjectInfo::versionString)
                + " started. Log file: " + fileLogger_->getLogFile().getFullPathName());

            // Keep only the 20 newest session logs.
            juce::Array<juce::File> oldLogs = logDir.findChildFiles(juce::File::findFiles, false, "DAW_Core_Session_*.log");
            struct OldestFirst
            {
                static int compareElements(const juce::File& a, const juce::File& b)
                {
                    const auto ta = a.getLastModificationTime().toMilliseconds();
                    const auto tb = b.getLastModificationTime().toMilliseconds();
                    return ta < tb ? -1 : (ta > tb ? 1 : 0);
                }
            } oldestFirst;
            oldLogs.sort(oldestFirst);
            for (int i = 0; i < oldLogs.size() - 20; ++i)
                oldLogs.getReference(i).deleteFile();
        }

        // Win32 layered window splash — paints at OS level, independent of
        // JUCE message loop, so it shows instantly even while MainComponent
        // blocks the thread during heavy initialization.
        {
            auto logoFile = juce::File("C:\\Users\\1993v\\Downloads\\My DAW\\DAW_Core\\logo\\1000166327(2).png");
            splashScreen_ = DAWSplashScreen::launch(logoFile);
        }

        mainWindow.reset (new MainWindow (getApplicationName()));

        // Keep the splash visible until the JUCE window has had a chance to
        // create its peer and paint at least once. This avoids the brief white
        // frame between destroying the splash and the DAW being ready.
        if (splashScreen_ != nullptr)
        {
           #if JUCE_WINDOWS
            if (auto* peer = mainWindow->getPeer())
            {
                HWND hwnd = (HWND) peer->getNativeHandle();
                ShowWindow(hwnd, SW_HIDE);
            }
           #endif

            juce::MessageManager::callAsync([this]
            {
                if (mainWindow == nullptr || splashScreen_ == nullptr)
                    return;

               #if JUCE_WINDOWS
                if (auto* peer = mainWindow->getPeer())
                {
                    HWND hwnd = (HWND) peer->getNativeHandle();
                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    UpdateWindow(hwnd);
                    SetForegroundWindow(hwnd);
                }
               #else
                mainWindow->setVisible(true);
               #endif

                splashScreen_->finish();
                splashScreen_ = nullptr;
            });
        }

        // Wire the MessageBox suppressor to activate only during plugin scans.
        // This cuts EnumWindows overhead from 11% continuous CPU to near-zero.
        if (auto* mc = mainWindow->getMainComponent())
        {
            DAW::MessageBoxSuppressorCore* suppressor = msgBoxSuppressor_.get();
            mc->setScanActiveCallback([suppressor](bool active)
            {
                if (suppressor)
                    suppressor->setScanActive(active);
            });
        }
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        juce::Logger::writeToLog("[SESSION] DAW_Core clean shutdown");
        mainWindow = nullptr; // (deletes our window)

        // Detach the logger LAST so all teardown diagnostics are captured.
        juce::Logger::setCurrentLogger(nullptr);
        fileLogger_ = nullptr;
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Colours::black,
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);

            // Clamp initial size to the available display area so the window
            // doesn't overflow on smaller / high-DPI screens and falsely
            // appear maximised (which breaks the maximize-button toggle).
            auto workArea = juce::Desktop::getInstance().getDisplays()
                                .getMainDisplay().userArea;
            int w = juce::jmin(getWidth(),  workArea.getWidth());
            int h = juce::jmin(getHeight(), workArea.getHeight());
            centreWithSize (w, h);
           #endif

            setVisible (true);

           #if JUCE_WINDOWS
            installSubclass();
           #endif

            // Register exclusive full-screen toggle with the ActionManager
            // so any input source (F11, gamepad, menu) can trigger it.
            regFullScreen_ = DAW::ActionManager::getInstance().scoped(
                DAW::ActionID::ViewToggleFullScreen,
                [this] { toggleExclusiveFullScreen(); });
        }

        void closeButtonPressed() override
        {
            if (auto* mc = getMainComponent())
            {
                if (mc->requestQuitFromWindowClose())
                    JUCEApplication::getInstance()->systemRequestedQuit();
                return;
            }
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* getMainComponent() const noexcept
        {
            return dynamic_cast<MainComponent*>(getContentComponent());
        }

        /** Use the native Win32 maximize / restore so the OS handles DPI,
         *  invisible frame borders and taskbar avoidance correctly. */
        void maximiseButtonPressed() override
        {
           #if JUCE_WINDOWS
            if (isExclusiveFullScreen_)
            {
                toggleExclusiveFullScreen();
                return;
            }
            if (auto* peer = getPeer())
            {
                auto hwnd = static_cast<HWND>(peer->getNativeHandle());
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            }
           #else
            setFullScreen(!isFullScreen());
           #endif
        }

        /** Exclusive borderless full-screen that covers the entire monitor.
         *  Windows will not show the auto-hide taskbar while this is active,
         *  so nothing pops up while working at the bottom of the DAW. */
        void toggleExclusiveFullScreen()
        {
           #if JUCE_WINDOWS
            if (auto* peer = getPeer())
            {
                auto hwnd = static_cast<HWND>(peer->getNativeHandle());

                if (isExclusiveFullScreen_)
                {
                    // Restore the original window style and position
                    SetWindowLongPtr(hwnd, GWL_STYLE, static_cast<LONG_PTR>(savedStyle_));
                    SetWindowPos(hwnd, HWND_NOTOPMOST,
                        savedRect_.left, savedRect_.top,
                        savedRect_.right  - savedRect_.left,
                        savedRect_.bottom - savedRect_.top,
                        SWP_FRAMECHANGED);
                    isExclusiveFullScreen_ = false;
                }
                else
                {
                    // Save current style and window rect
                    savedStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE));
                    GetWindowRect(hwnd, &savedRect_);

                    // Remove all window chrome → borderless popup
                    SetWindowLongPtr(hwnd, GWL_STYLE,
                        (savedStyle_ & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP);

                    // Cover the entire monitor (rcMonitor, not rcWork)
                    MONITORINFO mi = { sizeof(mi) };
                    GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);

                    SetWindowPos(hwnd, HWND_TOPMOST,
                        mi.rcMonitor.left, mi.rcMonitor.top,
                        mi.rcMonitor.right  - mi.rcMonitor.left,
                        mi.rcMonitor.bottom - mi.rcMonitor.top,
                        SWP_FRAMECHANGED | SWP_NOACTIVATE);

                    isExclusiveFullScreen_ = true;
                }
            }
           #else
            setFullScreen(!isFullScreen());
           #endif
        }

        /** Show a popup menu for entering / exiting exclusive full-screen. */
        void showFullScreenMenu()
        {
            juce::PopupMenu menu;
            if (isExclusiveFullScreen_)
                menu.addItem(1, "Exit Full Screen  (F11)");
            else
                menu.addItem(1, "Enter Full Screen  (F11)");

            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this](int result) {
                    if (result == 1)
                        toggleExclusiveFullScreen();
                });
        }

       #if JUCE_WINDOWS
        /** Win32 subclass proc — intercepts right-click on the maximize button. */
        static LRESULT CALLBACK subclassProc(
            HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
            UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
        {
            if (msg == WM_NCRBUTTONUP && wParam == HTMAXBUTTON)
            {
                auto* self = reinterpret_cast<MainWindow*>(dwRefData);
                juce::Component::SafePointer<MainWindow> safe(self);
                juce::MessageManager::callAsync([safe]() {
                    if (auto* w = safe.getComponent())
                        w->showFullScreenMenu();
                });
                return 0;
            }
            if (msg == WM_NCDESTROY)
                RemoveWindowSubclass(hWnd, subclassProc, uIdSubclass);
            return DefSubclassProc(hWnd, msg, wParam, lParam);
        }

        void installSubclass()
        {
            if (auto* peer = getPeer())
            {
                auto hwnd = static_cast<HWND>(peer->getNativeHandle());
                SetWindowSubclass(hwnd, subclassProc, 1,
                                  reinterpret_cast<DWORD_PTR>(this));
            }
        }
       #endif

    private:
        bool isExclusiveFullScreen_ = false;
       #if JUCE_WINDOWS
        DWORD savedStyle_ = 0;
        RECT  savedRect_  = {};
       #endif
        DAW::ActionManager::ActionRegistration regFullScreen_;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    DAWSplashScreen* splashScreen_ = nullptr;
    std::unique_ptr<DAW::MessageBoxSuppressorCore> msgBoxSuppressor_;
    std::unique_ptr<juce::FileLogger> fileLogger_;

    // ── Command-line argument helpers for subprocess scanner mode ─────
    static juce::String extractQuotedArg(const juce::String& cmdLine, const juce::String& flag)
    {
        auto idx = cmdLine.indexOf(flag);
        if (idx < 0) return {};
        auto rest = cmdLine.substring(idx + flag.length()).trimStart();
        if (rest.startsWithChar('"'))
        {
            auto end = rest.indexOf(1, "\"");
            return end > 0 ? rest.substring(1, end) : rest.substring(1);
        }
        return rest.upToFirstOccurrenceOf(" ", false, false);
    }

    static juce::String extractArg(const juce::String& cmdLine, const juce::String& flag)
    {
        auto idx = cmdLine.indexOf(flag);
        if (idx < 0) return {};
        auto rest = cmdLine.substring(idx + flag.length()).trimStart();
        return rest.upToFirstOccurrenceOf(" ", false, false);
    }
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (DAW_CoreApplication)

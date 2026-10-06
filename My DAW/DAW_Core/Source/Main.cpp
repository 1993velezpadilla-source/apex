/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
 #include <dbghelp.h>
 #include <gdiplus.h>
 #pragma comment(lib, "comctl32.lib")
 #pragma comment(lib, "dbghelp.lib")
 #pragma comment(lib, "gdiplus.lib")
#endif

#include "SplashScreen.h"
#include "MainComponent.h"
#include "UICore/ApexPresentationClock.h"
#include "PluginScanCore/PluginScanWorkerProcessCore.h"
#include "PluginSecurityCore/HostBuildTrustModeCore.h"
#include "PluginSafetyCore/MessageBoxSuppressorCore.h"
#include "PluginHostCore/PluginChainCore.h"
#include "PluginHostCore/HostedPluginIsolationCore.h"
#include "PluginSandboxCore/PluginSandboxProtocolCore.h"
#include "PluginSandboxCore/PluginSandboxProcessCore.h"
#include "PluginSandboxCore/PluginSandboxSelfTestCore.h"
#include "PluginSandboxCore/PluginWorkerMainCore.h"
#include "Benchmark/TimelineBenchmarkController.h"
#include "ThemeCore/ApexPrimitives.h"
#include "UICore/CursorThemeCore.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <utility>

#if JUCE_WINDOWS

//==============================================================================
// Minidump writer — catches any unhandled crash and writes a .dmp file
// so we can get the exact stack trace with CDB/WinDbg.
static LONG WINAPI ApexCrashHandler(EXCEPTION_POINTERS* ep)
{
    auto dumpDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getChildFile("../../../../crash-reports");
    dumpDir.createDirectory();
    auto ts = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    auto dumpPath = dumpDir.getChildFile("APEX_CRASH_" + ts + ".dmp");

    auto hFile = CreateFileW(dumpPath.getFullPathName().toUTF16(),
                             GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    // Also write a human-readable crash summary
    auto summaryPath = dumpDir.getChildFile("CRASH_SUMMARY_" + ts + ".txt");
    juce::String summary;
    summary << "===== APEX CRASH SUMMARY =====\n";
    summary << "Time: " << juce::Time::getCurrentTime().toString(true, true) << "\n";
    summary << "ExceptionCode: 0x" << juce::String::toHexString((int)ep->ExceptionRecord->ExceptionCode) << "\n";
    summary << "FaultAddress:  0x" << juce::String::toHexString((juce::int64)ep->ExceptionRecord->ExceptionAddress) << "\n";
    summary << "DumpFile:      " << dumpPath.getFullPathName() << "\n";
    summary << "================================\n";
    summaryPath.replaceWithText(summary.toStdString());

    // C6-crash-teardown: DO NOT show a modal MessageBox here. A modal loop
    // (IsDialogMessageA) pumps the faulting thread's window messages, which
    // dispatches pending window-destroy callbacks (e.g. plugin editor
    // teardown) WHILE the process is mid-crash — turning a single fault into
    // a cascade of secondary access violations inside plugin window procs
    // and corrupting the evidence stream (observed live: FabFilter Pro-C 2
    // wndproc crashed inside ApexCrashHandler's MessageBoxW modal loop).
    // The dump + summary files are the notification; WER presents the OS
    // dialog on its own thread outside the faulting stack.
    OutputDebugStringW(L"APEX: a crash dump has been saved: ");
    OutputDebugStringW(dumpPath.getFullPathName().toWideCharPointer());
    OutputDebugStringW(L"\n");

    return EXCEPTION_CONTINUE_SEARCH;
}

//==============================================================================
// APEX flow-cursor native plumbing — the Win32 half of DAW::CursorThemeCore.
// The header is platform-neutral; this translation unit already includes
// <windows.h> (and <commctrl.h>) so the 16-bit-era macros never leak into the
// JUCE headers of other TUs.
namespace
{
HCURSOR* nativeCursorHandles()
{
    static HCURSOR handles[(int) DAW::CursorThemeCore::CursorKind::NumKinds] = {};
    return handles;
}

// Convert a rendered APEX cursor image into a native colour cursor (HCURSOR).
// 32-bit alpha colour cursor: XOR = premultiplied BGRA, AND mask = 0.
HCURSOR makeNativeCursorHandle(const juce::Image& img, int hotX, int hotY)
{
    const int w = img.getWidth(), h = img.getHeight();
    if (w <= 0 || h <= 0)
        return nullptr;

    juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);

    std::vector<std::uint8_t> xorData((size_t) w * 4u * (size_t) h, 0);
    for (int y = 0; y < h; ++y)
    {
        const int dstY = h - 1 - y;
        for (int x = 0; x < w; ++x)
        {
            const auto c = data.getPixelColour(x, y);
            const float a = c.getFloatAlpha();
            const size_t o = ((size_t) dstY * (size_t) w + (size_t) x) * 4u;
            xorData[o + 0] = (std::uint8_t) juce::jlimit(0, 255, (int) std::lround(c.getBlue()  * 255.0f * a));
            xorData[o + 1] = (std::uint8_t) juce::jlimit(0, 255, (int) std::lround(c.getGreen() * 255.0f * a));
            xorData[o + 2] = (std::uint8_t) juce::jlimit(0, 255, (int) std::lround(c.getRed()   * 255.0f * a));
            xorData[o + 3] = (std::uint8_t) juce::jlimit(0, 255, (int) std::lround(a * 255.0f));
        }
    }
    const int andRowBytes = ((w + 31) / 32) * 4;
    std::vector<std::uint8_t> andData((size_t) andRowBytes * (size_t) h, 0);

    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = w;
    bi.bV5Height      = h * 2;   // colour + mask
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(hdc, (const BITMAPINFO*) &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (color == nullptr || bits == nullptr)
    {
        if (color != nullptr) DeleteObject(color);
        return nullptr;
    }
    std::memcpy(bits, xorData.data(), xorData.size());

    HBITMAP mask = CreateBitmap(w, h, 1, 1, andData.data());
    if (mask == nullptr)
    {
        DeleteObject(color);
        return nullptr;
    }

    ICONINFO ii = {};
    ii.fIcon    = FALSE;
    ii.xHotspot = (DWORD) juce::jlimit(0, w - 1, hotX);
    ii.yHotspot = (DWORD) juce::jlimit(0, h - 1, hotY);
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    HCURSOR cur = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    return cur;
}

class GlobalCursorOverride final : public juce::Timer
{
public:
    void initialise()
    {
        if (ready_)
        {
            if (! isTimerRunning())
                startTimerHz(30);
            return;
        }
        ready_ = true;

        // Build every custom cursor and register its native handle. The header
        // exposes the artwork (image + hotspot); we convert to HCURSOR here so
        // header-only TUs (unit tests) never need this Win32 code.
        auto* handles = nativeCursorHandles();
        for (int k = 0; k < (int) DAW::CursorThemeCore::CursorKind::NumKinds; ++k)
        {
            juce::Image img;
            int hx = 0, hy = 0;
            if (DAW::CursorThemeCore::getCursorArtwork(
                    (DAW::CursorThemeCore::CursorKind) k, img, hx, hy))
                handles[k] = makeNativeCursorHandle(img, hx, hy);
        }

        auto add = [this](LPCTSTR id, DAW::CursorThemeCore::CursorKind kind)
        {
            HCURSOR sys    = LoadCursor(nullptr, id);
            HCURSOR custom = nativeCursorHandles()[(int) kind];
            if (sys != nullptr && custom != nullptr)
                map_.emplace_back(sys, custom);
        };
        add(IDC_ARROW,       DAW::CursorThemeCore::CursorKind::Arrow);
        add(IDC_CROSS,       DAW::CursorThemeCore::CursorKind::Crosshair);
        add(IDC_HAND,        DAW::CursorThemeCore::CursorKind::Hand);
        add(IDC_IBEAM,       DAW::CursorThemeCore::CursorKind::IBeam);
        add(IDC_SIZENS,      DAW::CursorThemeCore::CursorKind::ResizeNS);
        add(IDC_SIZEWE,      DAW::CursorThemeCore::CursorKind::ResizeWE);
        add(IDC_SIZENWSE,    DAW::CursorThemeCore::CursorKind::ResizeNWSE);
        add(IDC_SIZENESW,    DAW::CursorThemeCore::CursorKind::ResizeNESW);
        add(IDC_SIZEALL,     DAW::CursorThemeCore::CursorKind::SizeAll);
        add(IDC_WAIT,        DAW::CursorThemeCore::CursorKind::Wait);
        add(IDC_APPSTARTING, DAW::CursorThemeCore::CursorKind::Wait);

        stdHand_ = LoadCursor(nullptr, IDC_HAND);

        startTimerHz(60);
    }

private:
    void timerCallback() override
    {
        HCURSOR current = GetCursor();
        if (current == nullptr)
            return;

        auto* handles = nativeCursorHandles();
        const HCURSOR openHand = handles[(int) DAW::CursorThemeCore::CursorKind::Hand];
        const HCURSOR closedA  = handles[(int) DAW::CursorThemeCore::CursorKind::DragHand];
        const HCURSOR closedB  = handles[(int) DAW::CursorThemeCore::CursorKind::GrabHandB];
        const bool isHand = (current == openHand || current == closedA
                          || current == closedB || current == stdHand_);

        // Grab animation: while the left button is held the hand closes and
        // gives a small squeeze pulse; on release it opens again.
        const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (isHand)
        {
            if (leftDown)
            {
                ++grabPhase_;
                const bool pulseFrame = ((grabPhase_ / 3) % 2) == 0;
                SetCursor(pulseFrame ? closedA : closedB);
            }
            else
            {
                SetCursor(openHand);
            }
            return;
        }

        for (const auto& pair : map_)
        {
            if (pair.first == current && pair.second != nullptr)
            {
                SetCursor(pair.second);
                break;
            }
        }
    }

    bool ready_ = false;
    int grabPhase_ = 0;
    HCURSOR stdHand_ = nullptr;
    std::vector<std::pair<HCURSOR, HCURSOR>> map_;
};
} // namespace

namespace DAW {

void CursorThemeCore::startGlobalOverride()
{
    static GlobalCursorOverride override;
    override.initialise();
}

} // namespace DAW
#endif

namespace
{
class PluginForensicHarness final : private juce::Timer,
                                    private juce::Thread
{
public:
    PluginForensicHarness(juce::File pluginFile,
                          juce::String requestedName,
                          std::function<void(bool)> completion,
                          juce::File coexistBFile = {},
                          juce::String coexistBName = {},
                          bool reverseOrder = false,
                          int aUid = -1, int aDeprecatedUid = -1,
                          int bUid = -1, int bDeprecatedUid = -1,
                          juce::AudioPluginFormatManager* productionManager = nullptr)
        : pluginFile_(std::move(pluginFile)),
          requestedName_(std::move(requestedName)),
          coexistBFile_(std::move(coexistBFile)),
          coexistBName_(std::move(coexistBName)),
          reverseOrder_(reverseOrder),
          coexistMode_(coexistBFile_ != juce::File() && coexistBName_.isNotEmpty()),
          aUid_(aUid), aDeprecatedUid_(aDeprecatedUid),
          bUid_(bUid), bDeprecatedUid_(bDeprecatedUid),
          externalManager_(productionManager),
          settleDelayMs_(productionManager != nullptr ? 15000 : 50),
          stageTimeoutMs_(coexistMode_ || aUid >= 0 ? 240000 : 30000),
          completion_(std::move(completion)),
          juce::Thread("APEX plugin forensic watchdog")
    {
        touchProgress();
        if (settleDelayMs_ > 0)
        {
            forensicTrace("settle_wait_begin ms=" + juce::String(settleDelayMs_));
            startThread();
            startTimer(settleDelayMs_);
        }
        else
        {
            startThread();
            startTimer(50);
        }
    }

    ~PluginForensicHarness() override
    {
        signalThreadShouldExit();
        stopThread(2000);
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (wait(250))
                break;

            const auto idleMs = juce::Time::currentTimeMillis()
                              - lastProgressMs_.load(std::memory_order_acquire);
            if (idleMs <= stageTimeoutMs_)
                continue;

           #if JUCE_WINDOWS
            const auto message = L"[PLUGIN FORENSIC] WATCHDOG_TIMEOUT stage="
                + std::to_wstring(activeStage_.load(std::memory_order_acquire)) + L"\n";
            OutputDebugStringW(message.c_str());
            TerminateProcess(GetCurrentProcess(), 124);
           #else
            juce::JUCEApplicationBase::quit();
           #endif
            return;
        }
    }

    void touchProgress() noexcept
    {
        lastProgressMs_.store(juce::Time::currentTimeMillis(), std::memory_order_release);
    }

    void timerCallback() override
    {
        stopTimer();
        activeStage_.store(stage_, std::memory_order_release);
        touchProgress();
        juce::Logger::writeToLog("[PLUGIN FORENSIC] checkpoint=stage_enter stage="
            + juce::String(stage_));
        try
        {
            if (coexistMode_)
            {
                runCoexistStage(stage_++);
                startTimer(150);
            }
            else if (aUid_ >= 0)
            {
                runSingleCachedStage(stage_++);
                startTimer(150);
            }
            else
            {
                switch (stage_++)
                {
                    case 0: scanAndInstantiate(); startTimer(100); break;
                    case 1: processAndSnapshot(); startTimer(100); break;
                    case 2: openEditor(); startTimer(1500); break;
                    case 3: processWithEditorOpen(); startTimer(100); break;
                    case 4: closeAndRemove(); startTimer(1000); break;
                    case 5: restoreAndProcess(); startTimer(250); break;
                    case 6: validateRestoredEditor(); startTimer(1000); break;
                    case 7: closeRestoredAndRemove(); startTimer(1000); break;
                    case 8: repeatedLoadUnloadStep(); break;
                    default: finish(false); break;
                }
            }
            touchProgress();
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[PLUGIN FORENSIC] failed stage=" + juce::String(stage_ - 1)
                + " exception=\"" + e.what() + "\"");
            finish(false);
        }
        catch (...)
        {
            juce::Logger::writeToLog("[PLUGIN FORENSIC] failed stage=" + juce::String(stage_ - 1)
                + " exception=unknown");
            finish(false);
        }
    }

    [[noreturn]] static void fail(const juce::String& message)
    {
        throw std::runtime_error(message.toStdString());
    }

    void scanAndInstantiate()
    {
        if (! pluginFile_.existsAsFile())
            fail("plugin file does not exist: " + pluginFile_.getFullPathName());

        formatManager_.addFormat(std::make_unique<juce::VST3PluginFormat>());
        auto* format = formatManager_.getFormat(0);
        if (format == nullptr)
            fail("VST3 format unavailable");

        juce::OwnedArray<juce::PluginDescription> scanned;
        format->findAllTypesForFile(scanned, pluginFile_.getFullPathName());
        for (auto* candidate : scanned)
            if (candidate != nullptr && candidate->name.containsIgnoreCase(requestedName_))
            {
                description_ = *candidate;
                break;
            }
        if (description_.name.isEmpty())
            fail("requested plugin class was not found");

        knownPlugins_.addType(description_);
        juce::Logger::writeToLog("[PLUGIN FORENSIC] scanner_result name=\"" + description_.name
            + "\" format=" + description_.pluginFormatName
            + " knownCount=" + juce::String(knownPlugins_.getNumTypes()));

        chain_ = std::make_unique<DAW::PluginChainCore>();
        chain_->setDebugName("plugin-forensic-harness");
        chain_->prepare(sampleRate_, blockSize_);
        juce::String error;
        if (chain_->appendPlugin(description_, formatManager_, error) != 0)
            fail("create/prepare failed: " + error);
        if (chain_->getNumActiveSlots() != 1 || chain_->getSlot(0) == nullptr)
            fail("created chain does not contain one active slot");

        juce::Logger::writeToLog("[PLUGIN FORENSIC] instantiate_prepare=success preparedBlock="
            + juce::String(blockSize_));
    }

    // ── C7E coexistence instrumentation (test-only, off the realtime thread) ─
    // Drives the REAL production VST3 creation sequence for two components
    // (typically two WaveShells) in both orders, tracing every host boundary
    // to plugin_forensic_coexistence.log so a native crash is attributable to
    // the exact operation that failed.

    static void forensicTrace(const juce::String& msg)
    {
        juce::Logger::writeToLog("[PLUGIN FORENSIC] " + msg);
        DAW::PluginScanAuditLogCore::appendLine("plugin_forensic_coexistence.log",
                                                "[PLUGIN FORENSIC] " + msg);
    }

    juce::AudioPluginFormatManager& manager()
    {
        return externalManager_ != nullptr ? *externalManager_ : formatManager_;
    }

    juce::PluginDescription scanInto(const juce::File& file,
                                     const juce::String& requestedName)
    {
        if (! file.existsAsFile())
            fail("plugin file does not exist: " + file.getFullPathName());

        juce::OwnedArray<juce::PluginDescription> scanned;
        auto& fm = manager();
        if (fm.getNumFormats() == 0)
            fm.addFormat(std::make_unique<juce::VST3PluginFormat>());
        auto* format = fm.getFormat(0);
        if (format == nullptr)
            fail("VST3 format unavailable");

        format->findAllTypesForFile(scanned, file.getFullPathName());
        juce::PluginDescription found;
        for (auto* candidate : scanned)
            if (candidate != nullptr && candidate->name.containsIgnoreCase(requestedName))
            {
                found = *candidate;
                break;
            }
        if (found.name.isEmpty())
            fail("requested plugin class was not found: " + requestedName);
        return found;
    }

    /** Single-plugin cached-identity parity sequence (production-faithful):
        description -> create -> prepare -> process -> remove -> PASS. */
    void runSingleCachedStage(int stage)
    {
        switch (stage)
        {
            case 0:
                if (chain_ == nullptr)
                {
                    chain_ = std::make_unique<DAW::PluginChainCore>();
                    chain_->setDebugName("plugin-forensic-harness");
                    chain_->prepare(sampleRate_, blockSize_);
                }
                forensicTrace("single_desc name=\"" + requestedName_
                    + "\" uid=" + juce::String(aUid_)
                    + " file=\"" + pluginFile_.getFullPathName() + "\"");
                description_ = makeCachedDescription(pluginFile_, requestedName_, aUid_, aDeprecatedUid_);
                forensicTrace("single_create_begin");
                {
                    juce::String err;
                    if (chain_->appendPlugin(description_, manager(), err) != 0)
                        fail("single create/prepare failed: " + err);
                }
                forensicTrace("single_create_end prepared="
                    + juce::String(chain_->getSlot(0) != nullptr && chain_->getSlot(0)->isPrepared() ? 1 : 0));
                break;
            case 1:
                processBlocks(8);
                forensicTrace("single_process_success blocks=8");
                break;
            case 2:
                forensicTrace("single_remove_begin");
                chain_->removePlugin(0);
                DAW::PluginChainCore::drainRetiredPlugins();
                forensicTrace("single_remove_end RESULT=PASS");
                finish(true);
                break;
            default:
                finish(false);
                break;
        }
    }

    /** Production-representative description: the insertion path consumes the
        scanner-cache identity (path + UID + name), NOT an in-process scan.
        In-process findAllTypesForFile on a WaveShell is slow/hangs (licensing
        UI) and is not what production does. */
    static juce::PluginDescription makeCachedDescription(const juce::File& file,
                                                         const juce::String& name,
                                                         int uid, int deprecatedUid)
    {
        juce::PluginDescription d;
        d.fileOrIdentifier = file.getFullPathName();
        d.pluginFormatName = "VST3";
        d.name             = name;
        d.manufacturerName = "Waves";
        d.uniqueId         = uid;
        d.deprecatedUid    = deprecatedUid;
        d.numInputChannels  = 2;
        d.numOutputChannels = 2;
        return d;
    }

    void runCoexistStage(int stage)
    {
        // A = primary target (pluginFile_/requestedName_), B = secondary shell.
        const bool firstIsB = reverseOrder_;
        const juce::File   firstFile  = firstIsB ? coexistBFile_ : pluginFile_;
        const juce::String firstName = firstIsB ? coexistBName_ : requestedName_;
        const juce::File   secondFile = firstIsB ? pluginFile_ : coexistBFile_;
        const juce::String secondName = firstIsB ? requestedName_ : coexistBName_;
        const int firstUid  = firstIsB ? bUid_ : aUid_;
        const int firstDep  = firstIsB ? bDeprecatedUid_ : aDeprecatedUid_;
        const int secondUid = firstIsB ? aUid_ : bUid_;
        const int secondDep = firstIsB ? aDeprecatedUid_ : bDeprecatedUid_;

        switch (stage)
        {
            case 0:
                if (chain_ == nullptr)
                {
                    chain_ = std::make_unique<DAW::PluginChainCore>();
                    chain_->setDebugName("plugin-forensic-harness");
                    chain_->prepare(sampleRate_, blockSize_);
                }
                forensicTrace("coexist_first_desc role=" + juce::String(firstIsB ? "B" : "A")
                    + " file=\"" + firstFile.getFullPathName() + "\" name=\"" + firstName
                    + "\" uid=" + juce::String(firstUid));
                firstDesc_ = firstUid >= 0
                    ? makeCachedDescription(firstFile, firstName, firstUid, firstDep)
                    : scanInto(firstFile, firstName);
                forensicTrace("coexist_first_create_begin");
                {
                    juce::String err;
                    if (chain_->appendPlugin(firstDesc_, manager(), err) != 0)
                        fail("first create/prepare failed: " + err);
                }
                forensicTrace("coexist_first_create_end prepared="
                    + juce::String(chain_->getSlot(0) != nullptr && chain_->getSlot(0)->isPrepared() ? 1 : 0));
                break;
            case 1:
                processBlocks(8);
                forensicTrace("coexist_first_process_success blocks=8");
                break;
            case 2:
                forensicTrace("coexist_second_desc role=" + juce::String(firstIsB ? "A" : "B")
                    + " file=\"" + secondFile.getFullPathName() + "\" name=\"" + secondName
                    + "\" uid=" + juce::String(secondUid));
                secondDesc_ = secondUid >= 0
                    ? makeCachedDescription(secondFile, secondName, secondUid, secondDep)
                    : scanInto(secondFile, secondName);
                forensicTrace("coexist_second_create_begin");
                {
                    juce::String err;
                    if (chain_->appendPlugin(secondDesc_, manager(), err) != 1)
                        fail("second create/prepare failed: " + err);
                }
                forensicTrace("coexist_second_create_end prepared="
                    + juce::String(chain_->getSlot(1) != nullptr && chain_->getSlot(1)->isPrepared() ? 1 : 0));
                break;
            case 3:
                processBlocks(8);
                forensicTrace("coexist_second_process_success blocks=8");
                break;
            case 4:
                forensicTrace("coexist_remove_second_begin");
                chain_->removePlugin(1);
                DAW::PluginChainCore::drainRetiredPlugins();
                forensicTrace("coexist_remove_second_end");
                break;
            case 5:
                forensicTrace("coexist_reinsert_second_begin");
                {
                    juce::String err;
                    if (chain_->appendPlugin(secondDesc_, manager(), err) != 1)
                        fail("reinsert failed: " + err);
                }
                processBlocks(8);
                forensicTrace("coexist_reinsert_second_end");
                break;
            case 6:
                forensicTrace("coexist_remove_first_begin");
                chain_->removePlugin(0);
                DAW::PluginChainCore::drainRetiredPlugins();
                forensicTrace("coexist_remove_first_end RESULT=PASS");
                finish(true);
                break;
            default:
                finish(false);
                break;
        }
    }

    void processBlocks(int count)
    {
        juce::AudioBuffer<float> storage(2, 8192);
        juce::int64 phase = 0;
        for (int block = 0; block < count; ++block)
        {
            for (int ch = 0; ch < storage.getNumChannels(); ++ch)
            {
                auto* samples = storage.getWritePointer(ch);
                for (int i = 0; i < blockSize_; ++i)
                    samples[i] = 0.05f * std::sin((float) (phase + i) * 0.017f);
                for (int i = blockSize_; i < storage.getNumSamples(); ++i)
                    samples[i] = (block & 1) == 0 ? 0.75f : -0.75f;
            }

            chain_->processBlock(storage, blockSize_);
            for (int ch = 0; ch < storage.getNumChannels(); ++ch)
                for (int i = 0; i < blockSize_; ++i)
                    if (! std::isfinite(storage.getSample(ch, i)))
                        fail("non-finite sample after processBlock");
            phase += blockSize_;
        }

        const auto diag = chain_->getSlot(0)->getDiagSnapshot();
        if (diag.lastProcessSamples != blockSize_)
            fail("plugin received " + juce::String(diag.lastProcessSamples)
                + " samples for a " + juce::String(blockSize_) + "-sample quantum");
    }

    void processAndSnapshot()
    {
        processBlocks(64);
        savedState_ = chain_->getState();
        if (! savedState_.isValid() || savedState_.getNumChildren() != 1)
            fail("plugin chain state snapshot failed");
        juce::Logger::writeToLog("[PLUGIN FORENSIC] processing_state=success blocks=64");
    }

    void openEditor()
    {
        auto* slot = chain_->getSlot(0);
        slot->openEditor();
        if (! slot->isEditorCreated() || ! slot->isEditorOpen())
            fail("native editor was not created and shown");
        juce::Logger::writeToLog("[PLUGIN FORENSIC] editor_open=success");
    }

    void processWithEditorOpen()
    {
        if (! chain_->getSlot(0)->isEditorOpen())
            fail("native editor closed unexpectedly");
        processBlocks(32);
        juce::Logger::writeToLog("[PLUGIN FORENSIC] editor_open_processing=success blocks=32");
    }

    void closeAndRemove()
    {
        chain_->getSlot(0)->closeEditor();
        if (chain_->getSlot(0)->isEditorCreated())
            fail("native editor survived closeEditor");
        chain_->removePlugin(0);
        if (chain_->getNumActiveSlots() != 0)
            fail("plugin survived removePlugin");
        DAW::PluginChainCore::drainRetiredPlugins();
        juce::Logger::writeToLog("[PLUGIN FORENSIC] close_remove=success awaiting_message_loop_settle");
    }

    void restoreAndProcess()
    {
        chain_->restoreState(savedState_, formatManager_);
        if (chain_->getNumActiveSlots() != 1 || chain_->getSlot(0) == nullptr)
            fail("saved plugin chain did not restore");
        processBlocks(16);
        juce::Logger::writeToLog("[PLUGIN FORENSIC] remove_restore_processing=success");
    }

    void validateRestoredEditor()
    {
        chain_->getSlot(0)->openEditor();
        if (! chain_->getSlot(0)->isEditorOpen())
            fail("restored plugin editor did not open");
        juce::Logger::writeToLog("[PLUGIN FORENSIC] restored_editor_open=success");
    }

    void closeRestoredAndRemove()
    {
        chain_->getSlot(0)->closeEditor();
        chain_->removePlugin(0);
        DAW::PluginChainCore::drainRetiredPlugins();
        juce::Logger::writeToLog("[PLUGIN FORENSIC] restored_close_remove=success");
    }

    void repeatedLoadUnloadStep()
    {
        if (loadUnloadCycle_ < 8)
        {
            chain_->restoreState(savedState_, formatManager_);
            if (chain_->getNumActiveSlots() != 1)
                fail("repeated restore failed cycle=" + juce::String(loadUnloadCycle_));
            processBlocks(8);
            chain_->removePlugin(0);
            if (chain_->getNumActiveSlots() != 0)
                fail("repeated remove failed cycle=" + juce::String(loadUnloadCycle_));
            DAW::PluginChainCore::drainRetiredPlugins();
            juce::Logger::writeToLog("[PLUGIN FORENSIC] load_unload_cycle=success cycle="
                + juce::String(loadUnloadCycle_));
            ++loadUnloadCycle_;
            --stage_; // Repeat stage 8 after yielding to the message loop.
            startTimer(1000);
            return;
        }

        chain_.reset();
        DAW::PluginChainCore::drainRetiredPlugins();
        juce::Logger::writeToLog("[PLUGIN FORENSIC] repeated_load_unload=success cycles=8");
        finish(true);
    }

    void finish(bool succeeded)
    {
        stopTimer();
        if (finished_)
            return;
        finished_ = true;
        juce::Logger::writeToLog(succeeded
            ? "[PLUGIN FORENSIC] RESULT=PASS"
            : "[PLUGIN FORENSIC] RESULT=FAIL");
        completion_(succeeded);
    }

    juce::File pluginFile_;
    juce::String requestedName_;
    juce::File coexistBFile_;
    juce::String coexistBName_;
    bool reverseOrder_ = false;
    bool coexistMode_ = false;
    int aUid_ = -1;
    int aDeprecatedUid_ = -1;
    int bUid_ = -1;
    int bDeprecatedUid_ = -1;
    juce::PluginDescription firstDesc_;
    juce::PluginDescription secondDesc_;
    std::function<void(bool)> completion_;
    juce::AudioPluginFormatManager formatManager_;
    juce::AudioPluginFormatManager* externalManager_ = nullptr;   // production scanner manager when running in-app
    int settleDelayMs_ = 0;                                       // production-faithful settle before first stage
    juce::KnownPluginList knownPlugins_;
    juce::PluginDescription description_;
    std::unique_ptr<DAW::PluginChainCore> chain_;
    juce::ValueTree savedState_;
    int stage_ = 0;
    int loadUnloadCycle_ = 0;
    bool finished_ = false;
    std::atomic<juce::int64> lastProgressMs_ { 0 };
    std::atomic<int> activeStage_ { -1 };
    const double sampleRate_ = 48000.0;
    const int blockSize_ = 480;
    juce::int64 stageTimeoutMs_ = 30000;
    static constexpr juce::int64 forensicStageTimeoutMs_ = 30000;
};
}

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
        const auto processArguments = getCommandLineParameterArray();
        processMode_ = DAW::PluginSandboxCommandLineCore::resolveProcessMode(processArguments);
        DAW::ApexProcessModeStateCore::setProcessMode(processMode_);

#if JUCE_WINDOWS
        // ── Suppress Windows error dialogs for DLL load failures ─────────
        // Plugins using Wibu CodeMeter, iLok, etc. try to load DLLs that
        // Windows Application Control may block. Without this, modal error
        // dialogs ("TLS initialization failed", "Bad Image") freeze the app.
        // This must happen BEFORE any plugin code runs — in both the main
        // DAW process and the scanner subprocess.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        SetUnhandledExceptionFilter(ApexCrashHandler);
#endif

        // #7F Phase A: resolve the process role before any normal APEX logger,
        // MainWindow, MainComponent, project, plugin browser, audio device, or
        // presentation infrastructure is created. Worker mode cannot recurse:
        // its entrypoint owns no process launcher and returns directly here.
        if (processMode_ == DAW::ApexProcessMode::PluginWorker)
        {
            setApplicationReturnValue(DAW::PluginWorkerMainCore::run(processArguments));
            quit();
            return;
        }

        if (processMode_ == DAW::ApexProcessMode::PluginSandboxSelfTest)
        {
            setApplicationReturnValue(DAW::PluginSandboxSelfTestCore::run(processArguments));
            quit();
            return;
        }

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

        // ── Production-faithful plugin forensic mode ──────────────────────
        // The scenario is scheduled AFTER full application initialization
        // (MainWindow/MainComponent/ApplicationCore/plugin scanner format
        // manager), on the message thread, in the exact environment real user
        // sessions run. See the branch near the end of initialise(). The old
        // bare early-return harness was not production-faithful and is gone.

        // Diagnostic session policy: resolve an exact token before the main
        // window, project, plugins, or audio device are created. This is not a
        // project setting and cannot be toggled while playback is running.
        DAW::HostedPluginIsolationCore::configureForSession(
            DAW::HostedPluginIsolationCore::commandLineRequestsIsolation(
                getCommandLineParameterArray()));

        // Win32 layered window splash — paints at OS level, independent of
        // JUCE message loop, so it shows instantly even while MainComponent
        // blocks the thread during heavy initialization.
        //
        // Packaging note: installation must place logo/1000166327(2).png
        // beside the executable for the installed-logo path to resolve.
        {
            const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            const auto installedLogo = executable.getSiblingFile("logo").getChildFile("1000166327(2).png");
            const auto repositoryLogo = executable.getParentDirectory()
                                              .getParentDirectory()
                                              .getParentDirectory()
                                              .getParentDirectory()
                                              .getParentDirectory()
                                              .getParentDirectory()
                                              .getChildFile("logo")
                                              .getChildFile("1000166327(2).png");
            const auto logoFile = installedLogo.existsAsFile() ? installedLogo : repositoryLogo;
            splashScreen_ = logoFile.existsAsFile() ? DAWSplashScreen::launch(logoFile) : nullptr;
        }

        mainWindow.reset (new MainWindow (getApplicationName()));
        DAW::ApexProcessModeStateCore::markNormalGuiCreated();

        // Register the main window with the presentation clock so the
        // adaptive display-aware rate follows the correct monitor.
        DAW::ApexPresentationClock::instance().setMainWindow (mainWindow.get());

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

        // ── Timeline benchmark mode ──────────────────────────────────────
        // Activated by --timeline-benchmark flag. Runs inside the real app
        // with normal JUCE repaint scheduling and message-thread dispatch.
        if (commandLine.contains("--timeline-benchmark"))
        {
            // ── Extract benchmark arguments from JUCE's parameter array ──
            // Using the parameter array is reliable because Windows
            // command-line parsing correctly handles quoted arguments
            // with spaces. Reparsing the raw commandLine string can
            // fail for paths with spaces.
            auto params = juce::JUCEApplication::getInstance()->getCommandLineParameterArray();

            juce::String projectPath, label;
            int totalRuns = 1, runIndex = 0;
            bool autoStart = false;

            for (int i = 0; i < params.size(); ++i)
            {
                const auto& p = params[i];
                if (p.startsWith("--benchmark-project"))
                {
                    // --benchmark-project=<value> (with =)
                    auto eqIdx = p.indexOfChar('=');
                    if (eqIdx > 0)
                    {
                        projectPath = p.substring(eqIdx + 1);
                    }
                    // --benchmark-project <value> (value is next param)
                    else if (i + 1 < params.size())
                    {
                        projectPath = params[i + 1];
                    }
                }
                else if (p.startsWith("--benchmark-label"))
                {
                    auto eqIdx = p.indexOfChar('=');
                    if (eqIdx > 0)
                        label = p.substring(eqIdx + 1);
                    else if (i + 1 < params.size())
                        label = params[i + 1];
                }
                else if (p.startsWith("--benchmark-total-runs"))
                {
                    auto eqIdx = p.indexOfChar('=');
                    if (eqIdx > 0)
                        totalRuns = p.substring(eqIdx + 1).getIntValue();
                    else if (i + 1 < params.size())
                        totalRuns = params[i + 1].getIntValue();
                }
                else if (p.startsWith("--benchmark-run-index"))
                {
                    auto eqIdx = p.indexOfChar('=');
                    if (eqIdx > 0)
                        runIndex = p.substring(eqIdx + 1).getIntValue();
                    else if (i + 1 < params.size())
                        runIndex = params[i + 1].getIntValue();
                }
                else if (p == "--benchmark-auto-start")
                {
                    autoStart = true;
                }
            }

            if (label.isEmpty()) label = "default";

            // Log ALL parsed args for audit
            DBG("[BENCHMARK] ===== PARSED ARGV =====");
            for (int ai = 0; ai < params.size(); ++ai)
                DBG("[BENCHMARK]   argv[" << ai << "] = \"" << params[ai] << "\"");

            DBG("[BENCHMARK] projectPath=\"" << projectPath
                << "\" label=\"" << label
                << "\" totalRuns=" << totalRuns
                << " runIndex=" << runIndex
                << " autoStart=" << (autoStart ? "yes" : "no")
                << " autoStartFlagPresent=" << (commandLine.contains("--benchmark-auto-start") ? "yes" : "no"));

            // Schedule benchmark to start after message loop is running
            juce::MessageManager::callAsync([this, projectPath, label, totalRuns, runIndex, autoStart]
            {
                if (mainWindow != nullptr)
                {
                    benchmarkController_ = std::make_unique<TimelineBenchmarkController>(
                        *mainWindow, projectPath, label, totalRuns, runIndex, autoStart);
                }
                else
                {
                    juce::Logger::writeToLog("[BENCHMARK] ERROR: mainWindow is null, cannot start");
                    if (auto* app = JUCEApplication::getInstance())
                        app->systemRequestedQuit();
                }
            });
        }

        // ── Production-faithful plugin forensic mode ──────────────────────
        // Runs AFTER the real app is fully initialized: MainWindow +
        // MainComponent + ApplicationCore + the production plugin scanner
        // format manager are all live, the message loop is pumping, and the
        // audio device has been prepared — the same environment the user's
        // real Clarity sessions run in. Flags:
        //   --plugin-forensic-vst3 <shellPath>
        //   --plugin-forensic-name <name>
        //   [--forensic-a-uid <n> --forensic-a-deprecated-uid <n>]
        //   [--forensic-coexist-b-file <shellPath2> --forensic-coexist-b-name <name2>
        //    --forensic-b-uid <n> --forensic-b-deprecated-uid <n>
        //    --forensic-coexist-reverse]
        if (commandLine.contains("--plugin-forensic-vst3"))
        {
            auto params = juce::JUCEApplication::getInstance()->getCommandLineParameterArray();
            auto getArg = [&](const juce::String& key) -> juce::String
            {
                for (int i = 0; i < params.size(); ++i)
                {
                    const auto& p = params[i];
                    if (p == key && i + 1 < params.size())
                        return params[i + 1];
                    if (p.startsWith(key + "="))
                        return p.substring(key.length() + 1);
                }
                return {};
            };
            const juce::String pluginPath = getArg("--plugin-forensic-vst3");
            const juce::String pluginName = getArg("--plugin-forensic-name");
            const juce::String bPath = getArg("--forensic-coexist-b-file");
            const juce::String bName = getArg("--forensic-coexist-b-name");
            const bool reverse = commandLine.contains("--forensic-coexist-reverse");
            const juce::String uidStr  = getArg("--forensic-a-uid");
            const juce::String depStr  = getArg("--forensic-a-deprecated-uid");
            const juce::String buidStr = getArg("--forensic-b-uid");
            const juce::String bdepStr = getArg("--forensic-b-deprecated-uid");
            const int aUid = uidStr.isNotEmpty()  ? uidStr.getIntValue()  : -1;
            const int aDep = depStr.isNotEmpty()  ? depStr.getIntValue()  : -1;
            const int bUid = buidStr.isNotEmpty() ? buidStr.getIntValue() : -1;
            const int bDep = bdepStr.isNotEmpty() ? bdepStr.getIntValue() : -1;

            juce::MessageManager::callAsync([this, pluginPath, pluginName, bPath, bName,
                                             reverse, aUid, aDep, bUid, bDep]
            {
                if (mainWindow == nullptr || mainWindow->getMainComponent() == nullptr)
                {
                    juce::Logger::writeToLog("[PLUGIN FORENSIC] ERROR: main window unavailable");
                    setApplicationReturnValue(1);
                    quit();
                    return;
                }

                auto* productionManager =
                    &mainWindow->getMainComponent()->getAppCore().getPluginScanner().getFormatManager();

                forensicHarness_ = std::make_unique<PluginForensicHarness>(
                    juce::File(pluginPath), pluginName,
                    [this](bool succeeded)
                    {
                        juce::MessageManager::callAsync([this, succeeded]
                        {
                            forensicHarness_.reset();
                            setApplicationReturnValue(succeeded ? 0 : 1);
                            quit();
                        });
                    },
                    juce::File(bPath), bName, reverse,
                    aUid, aDep, bUid, bDep,
                    productionManager);
            });
        }
    }

    void shutdown() override
    {
        if (processMode_ != DAW::ApexProcessMode::NormalApex)
            return;

        // Add your application's shutdown code here..

        juce::Logger::writeToLog("[SESSION] DAW_Core clean shutdown");

        // Unregister main window from presentation clock before destruction.
        // This is the explicit lifecycle cleanup; componentBeingDeleted
        // serves as a safety net if this path is somehow missed.
        DAW::ApexPresentationClock::instance().setMainWindow (nullptr);

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
            // ── Integrated custom title bar ──────────────────────────────────
            // The APEX header (logo + menus, inside MainComponent) IS the title
            // bar. Disable the OS-native white bar and the internal JUCE title
            // bar so the window shell begins directly with the APEX header.
            // This must happen during initialization (before the peer is used
            // interactively); the flag change recreates the ComponentPeer here
            // safely, once — never during normal operation.
            setUsingNativeTitleBar (false);
            setTitleBarHeight (0);
            setContentOwned (new MainComponent(), true);

            // Premium cursor theme: the custom arrow applies to every
            // default-cursor area in the whole DAW (floating windows are
            // children of MainComponent and inherit it). Components that set
            // their own cursors (resize handles, hands, crosshairs) keep
            // their behaviour.
            DAW::CursorThemeCore::applyDefaultCursorTo(*this);
            // Global cursor theme: the JUCE-level LookAndFeel mapping resolves
            // EVERY standard cursor (including components that never call
            // setMouseCursor) to its APEX twin deterministically at cursor
            // evaluation time — no polling race — plus the Win32 override as
            // a safety net for native surfaces (plugin editor content, other
            // threads).
            DAW::CursorThemeCore::installGlobalCursorTheme();

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            // Resizable with native borderless non-client resize: JUCE 8 on
            // Windows keeps WS_THICKFRAME and emulates the resize border inside
            // the client area (WM_NCHITTEST), so left/right/top/bottom edges
            // and all four corners stay resizable with no visible OS frame.
            // useBottomRightCornerResizer=false avoids the JUCE-drawn corner
            // triangle so the APEX shell stays fully custom.
            setResizable (true, false);

            // The DAW layout (menu bar + transport + docked mixer + a usable
            // arrangement area) requires a minimum canvas. Without a floor the
            // window can shrink until content overflows the frame. Keep the
            // initial size at the full display work area so the frame hugs the
            // edge of the DAW (never leaves a large margin on larger monitors),
            // clamped to the intended default only when the screen is smaller.
            const int kMinWindowW = 1024;
            const int kMinWindowH = 640;
            setResizeLimits (kMinWindowW, kMinWindowH, 3840, 2160);

            // Size to the entire display work area (the frame then sits at the
            // edge of the DAW on any monitor). On small screens this falls back
            // to the layout minimum; on large screens it fills the display
            // instead of leaving the DAW floating with an inset frame.
            auto workArea = juce::Desktop::getInstance().getDisplays()
                                .getMainDisplay().userArea;
            int w = juce::jmax (kMinWindowW, juce::jmin (workArea.getWidth(),  3840));
            int h = juce::jmax (kMinWindowH, juce::jmin (workArea.getHeight(), 2160));
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

        /** The APEX content lives inside the shell frame, not under it. The
         *  frame (drawOuterWindowFrame) ends ~5.75 px inside the client area;
         *  insetting the content by the same shell margin guarantees that no
         *  child — including the timeline's vertical scrollbar at the far
         *  right — is ever overlapped, clipped or covered by the frame. */
        juce::BorderSize<int> getContentComponentBorder() const override
        {
            return juce::BorderSize<int>(kFrameInset);
        }

        /** Routes the OS non-client hit-test so the APEX shell behaves like a
         *  native window without a native title bar (JUCE 8 borderless mode):
         *   - perimeter strip  → native edge/corner resizing,
         *   - empty header     → HTCAPTION (drag, double-click maximize, Aero
         *     Snap, multi-monitor and DPI handling all native),
         *   - window controls  → native minimize/maximize/close (routed to the
         *     APEX overrides below),
         *   - menus/undo/redo  → client (normal component clicks).
         *  Called by the peer on the message thread; must stay cheap and never
         *  call Component::contains (would recurse into the peer). */
        juce::Component::WindowControlKind findControlAtPoint(juce::Point<float> pt) const override
        {
            using Kind = juce::Component::WindowControlKind;

            const auto b = getLocalBounds();
            const int x = (int) std::floor(pt.x);
            const int y = (int) std::floor(pt.y);

            // Resize border, in logical px. Matches JUCE's borderless resize
            // emulation (SM_CXFRAME + SM_CXPADDEDBORDER scales with DPI and
            // converts to the same 12 logical px at every scale factor).
            constexpr int zone = 12;
            if (b.getWidth() > zone * 2 && b.getHeight() > zone * 2)
            {
                const bool atL = x < zone;
                const bool atR = x >= b.getWidth() - zone;
                const bool atT = y < zone;
                const bool atB = y >= b.getHeight() - zone;

                if (atT || atB || atL || atR)
                {
                    if (atT && atL) return Kind::sizeTopLeft;
                    if (atT && atR) return Kind::sizeTopRight;
                    if (atB && atL) return Kind::sizeBottomLeft;
                    if (atB && atR) return Kind::sizeBottomRight;
                    if (atT) return Kind::sizeTop;
                    if (atB) return Kind::sizeBottom;
                    if (atL) return Kind::sizeLeft;
                    if (atR) return Kind::sizeRight;
                }
            }

            // Custom title bar: the APEX header occupies the top of the shell.
            if (auto* mc = getMainComponent())
                if (auto* header = mc->getMenuBar())
                    if (header->isVisible())
                    {
                        const auto hp = header->getLocalPoint(this, pt.roundToInt());
                        if (header->getLocalBounds().contains(hp))
                        {
                            switch (header->findWindowControlAt(hp))
                            {
                                case DAW::DAWMenuBar::WindowControlId::Minimize: return Kind::minimise;
                                case DAW::DAWMenuBar::WindowControlId::Maximize: return Kind::maximise;
                                case DAW::DAWMenuBar::WindowControlId::Close:    return Kind::close;
                                default: break;
                            }
                            if (header->isInteractiveAt(hp))
                                return Kind::client;

                            // Floating windows (mixer, plugin browsers, panels)
                            // may overlap the header region. If a visible child
                            // of the main component contains this point, the
                            // click belongs to that child — never let the shell
                            // caption steal it (which would drag the whole DAW
                            // instead of the floating window).
                            const auto mcPt = mc->getLocalPoint(this, pt.roundToInt());
                            for (auto* child : mc->getChildren())
                            {
                                if (child == header || !child->isVisible())
                                    continue;
                                if (child->getBounds().contains(mcPt))
                                    return Kind::client;
                            }
                            return Kind::caption;   // empty header → drag region
                        }
                    }

            return Kind::client;
        }

        void resized() override
        {
            DocumentWindow::resized();
            updateWindowControlsState();
        }

        /** Keep the header's maximize/restore glyph in sync with the real
         *  window state (maximized, restored, or exclusive full-screen). */
        void updateWindowControlsState()
        {
            bool maximized = isExclusiveFullScreen_;
           #if JUCE_WINDOWS
            if (auto* peer = getPeer())
                if (IsZoomed(static_cast<HWND>(peer->getNativeHandle())))
                    maximized = true;
           #endif
            if (auto* mc = getMainComponent())
                if (auto* header = mc->getMenuBar())
                    header->setWindowMaximized(maximized);
        }

        void paintOverChildren(juce::Graphics& g) override
        {
            // One continuous shell frame around the COMPLETE top-level client
            // area. getLocalBounds() is the only visual authority: the frame is
            // redrawn automatically on every resize / DPI change / repaint, the
            // primitive keeps every stroke inside the clip, and the content is
            // inset by kFrameInset so nothing is ever covered by it.
            // The border uses the dedicated electric steel-blue shell token so
            // the window boundary reads instantly as "outer window border",
            // distinct from the near-black body and the violet UI accents.
            DAW::ApexPrimitives::drawOuterWindowFrame(
                g, getLocalBounds().toFloat(),
                DAW::Theme::getInstance().apex.color.shellBorder);
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

                // Keep the window corners SQUARE. With the native title bar
                // gone, Windows 11 would otherwise round the whole window
                // shape and clip the corners of the violet APEX frame.
                // Best-effort: missing dwmapi or an older OS simply leaves
                // the OS default corner rounding in place. The DWMWA_* values
                // are defined inline so the build does not depend on a
                // specific Windows SDK version.
                using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
                if (auto dwmSetWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
                        GetProcAddress(GetModuleHandleW(L"dwmapi.dll"), "DwmSetWindowAttribute")))
                {
                    const DWORD cornerPref = 1; // DWMWCP_DONOTROUND
                    const DWORD attrId = 33;    // DWMWA_WINDOW_CORNER_PREFERENCE
                    dwmSetWindowAttribute(hwnd, attrId, &cornerPref, sizeof(cornerPref));
                }
            }
        }
       #endif

    private:
        // Shell frame thickness + margin: the outermost frame strokes end
        // ~5.75 px inside the client area; the content is inset by this amount
        // so no UI is ever overlapped by the border.
        static constexpr int kFrameInset = 6;
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
    std::unique_ptr<PluginForensicHarness> forensicHarness_;
    DAW::ApexProcessMode processMode_ = DAW::ApexProcessMode::NormalApex;

    // ── Timeline benchmark controller (--timeline-benchmark flag) ──────
    std::unique_ptr<TimelineBenchmarkController> benchmarkController_;

    // ── Command-line argument helpers for subprocess scanner mode ─────
    static juce::String extractQuotedArg(const juce::String& cmdLine, const juce::String& flag)
    {
        auto idx = cmdLine.indexOf(flag);
        if (idx < 0) return {};
        auto rest = cmdLine.substring(idx + flag.length()).trimStart();

        // Handle --flag=<value> syntax
        if (rest.startsWithChar('='))
            rest = rest.substring(1).trimStart();

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

        // Handle --flag=<value> syntax
        if (rest.startsWithChar('='))
            rest = rest.substring(1).trimStart();

        // Handle --flag "quoted value" syntax
        if (rest.startsWithChar('"'))
        {
            auto end = rest.indexOf(1, "\"");
            return end > 0 ? rest.substring(1, end) : rest.substring(1);
        }

        return rest.upToFirstOccurrenceOf(" ", false, false);
    }
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (DAW_CoreApplication)

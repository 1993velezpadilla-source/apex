#include <JuceHeader.h>

#include "../../../Source/PluginHostCore/PluginInstanceCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"

#include <cstdint>
#include <chrono>
#include <memory>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
juce::File environmentFile(const char* name)
{
    return juce::File(juce::SystemStats::getEnvironmentVariable(name, {}));
}

juce::File workerExecutable()
{
    const auto configured = environmentFile("APEX_TEST_PLUGIN_WORKER_PATH");
    if (configured.existsAsFile())
        return configured;
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
}

juce::PluginDescription editorFixtureDescription()
{
    const auto bundle = environmentFile("APEX_TEST_VST3_PATH");
    juce::PluginDescription description;
    description.name = "APEX Test VST3";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = bundle.getFullPathName();
    description.uniqueId = 2021071337;
    description.deprecatedUid = 1612553221;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    return description;
}

bool prepareProxy(juce::UnitTest& test,
                  DAW::SandboxedPluginProxyCore& proxy)
{
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
    preparation.startupTimeoutMs = 5000;
    preparation.pluginCreateTimeoutMs = 60000;
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = workerExecutable().getFullPathName();
#endif

    const auto result = proxy.prepare(preparation);
    test.expect(result == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
                "sandbox fixture preparation failed: "
                    + proxy.processDiagnostics().error);
    return result == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared;
}

std::unique_ptr<DAW::PluginInstanceCore> makeProductSlot(juce::UnitTest& test)
{
    const auto description = editorFixtureDescription();
    test.expect(juce::File(description.fileOrIdentifier).isDirectory(),
                "editor fixture bundle is missing");
    if (! juce::File(description.fileOrIdentifier).isDirectory())
        return {};

#if JUCE_WINDOWS
    test.expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
                "fixture module was loaded in parent before product-path open");
#endif

    auto proxy = std::make_unique<DAW::SandboxedPluginProxyCore>(description);
    if (! prepareProxy(test, *proxy))
        return {};

    auto slot = std::make_unique<DAW::PluginInstanceCore>(std::move(proxy));
    test.expect(slot->isSandboxed(), "product path constructed an in-process slot");
    test.expect(slot->getExecutionMode() == DAW::PluginExecutionMode::Sandboxed,
                "product path execution mode is not sandboxed");
    return slot;
}

bool waitForProductEditorReady(juce::UnitTest& test,
                               DAW::PluginInstanceCore& slot,
                               std::uint32_t timeoutMs = 5000)
{
    juce::ignoreUnused(test);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (slot.isEditorOpen())
            return true;

        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            mm != nullptr && mm->isThisTheMessageThread())
            mm->runDispatchLoopUntil(5);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return slot.isEditorOpen();
}

#if JUCE_WINDOWS
bool isVisibleWorkerWindow(const DAW::SandboxedPluginProxyCore::EditorInfo& info,
                           const DAW::SandboxedPluginProxyCore& proxy)
{
    if (info.nativeWindowHandle == 0)
        return false;

    const auto hwnd = reinterpret_cast<HWND>(
        static_cast<std::uintptr_t>(info.nativeWindowHandle));
    DWORD ownerPid = 0;
    return hwnd != nullptr
        && IsWindow(hwnd) != FALSE
        && IsWindowVisible(hwnd) != FALSE
        && GetWindowThreadProcessId(hwnd, &ownerPid) != 0
        && ownerPid == proxy.currentWorkerPid()
        && info.workerProcessId == proxy.currentWorkerPid()
        && info.workerGeneration != 0
        && info.workerGeneration == proxy.currentWorkerGeneration();
}

bool processHasExited(DWORD pid, DWORD timeoutMs)
{
    if (pid == 0)
        return false;

    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, pid);
    if (process == nullptr)
        return true;

    const bool exited = WaitForSingleObject(process, timeoutMs) == WAIT_OBJECT_0;
    CloseHandle(process);
    return exited;
}
#endif

DAW::PluginSandboxProcessCore::Options editorProcessOptions()
{
    DAW::PluginSandboxProcessCore::Options options;
    options.enableAudioTransport = true;
    options.createVst3Plugin = true;
    options.pluginDescription = editorFixtureDescription();
    options.audioConfiguration.maxInputChannels = 2;
    options.audioConfiguration.maxOutputChannels = 2;
    options.audioConfiguration.maxSamples = 512;
    options.audioConfiguration.nominalBlockSamples = 512;
    options.audioConfiguration.sampleRate = 48000.0;
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    options.workerExecutablePathForTest = workerExecutable().getFullPathName();
#endif
    return options;
}

bool startEditorProcess(juce::UnitTest& test,
                        DAW::PluginSandboxProcessCore& process,
                        const DAW::PluginSandboxProcessCore::Options& options)
{
#if JUCE_WINDOWS
    const auto fixture = juce::File(
        juce::SystemStats::getEnvironmentVariable("APEX_TEST_VST3_PATH", {}));
    test.expect(fixture.isDirectory(),
                "editor deadline fixture bundle is missing");
    if (! fixture.isDirectory())
        return false;

    const auto result = process.start(options);
    test.expect(result == DAW::PluginSandboxProcessCore::StartResult::Started,
                "editor deadline worker start failed: "
                    + process.diagnostics().error);
    test.expect(process.diagnostics().hostedPluginCreated,
                "editor deadline fixture was not created in the worker");
    return result == DAW::PluginSandboxProcessCore::StartResult::Started;
#else
    juce::ignoreUnused(test, process, options);
    return false;
#endif
}

class PluginSandboxF2OpenProductPathTest final : public juce::UnitTest
{
public:
    PluginSandboxF2OpenProductPathTest()
        : juce::UnitTest("plugin.sandbox.editor.open-product-path.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("product open path presents a worker-owned floating editor");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        expect(proxy != nullptr, "product path lost its sandbox proxy");
        expect(slot->isSandboxed(), "product path left sandbox execution mode");
        expect(slot->isEditorOpen(), "product path did not report editor open");
        if (proxy != nullptr && slot->isEditorOpen())
        {
            expect(isVisibleWorkerWindow(proxy->editorInfo(), *proxy),
                   "presented editor HWND is not visible and worker-owned");
            expect(proxy->isActive(), "plugin became inactive during editor open");
            expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
                   "supported product path loaded the fixture module in the parent");
        }
        slot->closeEditor();
#else
        beginTest("Windows-only Phase F2 product path");
        expect(false, "Phase F2 requires Windows");
#endif
    }
};

class PluginSandboxF2ReopenFocusTest final : public juce::UnitTest
{
public:
    PluginSandboxF2ReopenFocusTest()
        : juce::UnitTest("plugin.sandbox.editor.reopen-focus.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("reopen reuses and presents the current worker editor");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr || ! slot->isEditorOpen())
        {
            expect(false, "initial product editor open failed");
            return;
        }

        const auto first = proxy->editorInfo();
        slot->openEditor();
        const auto second = proxy->editorInfo();
        expect(slot->isEditorOpen(), "reopen closed the current editor");
        expect(second.nativeWindowHandle == first.nativeWindowHandle,
               "reopen created a duplicate native editor instead of reusing it");
        expect(second.requestSequence == first.requestSequence,
               "reopen changed the singular current editor request identity");
        expect(isVisibleWorkerWindow(second, *proxy),
               "reopen did not present the current worker-owned HWND");
        expect(proxy->isActive(), "plugin became inactive during editor reopen");
        slot->closeEditor();
#endif
    }
};

class PluginSandboxF2CloseReopenTest final : public juce::UnitTest
{
public:
    PluginSandboxF2CloseReopenTest()
        : juce::UnitTest("plugin.sandbox.editor.close-reopen.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("close clears identity and reopen creates a current editor");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr || ! slot->isEditorOpen())
        {
            expect(false, "initial product editor open failed");
            return;
        }

        const auto first = proxy->editorInfo();
        const auto firstHwnd = reinterpret_cast<HWND>(
            static_cast<std::uintptr_t>(first.nativeWindowHandle));
        slot->closeEditor();
        expect(! slot->isEditorOpen(), "close retained the parent editor identity");
        expect(! proxy->isEditorOpen(), "proxy retained a stale editor identity");
        expect(proxy->isActive(), "plugin became inactive after editor close");
        expect(proxy->remoteAvailable(), "remote processing gate closed after editor close");
        expect(firstHwnd == nullptr || IsWindow(firstHwnd) == FALSE,
               "closed worker editor HWND remained alive");

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        expect(slot->isEditorOpen(), "reopen failed after a clean close");
        expect(proxy->editorInfo().requestSequence > first.requestSequence,
               "reopen did not create a new editor request identity");
        expect(isVisibleWorkerWindow(proxy->editorInfo(), *proxy),
               "reopened editor is not a current visible worker window");
        expect(proxy->isActive(), "plugin became inactive after close/reopen");
        slot->closeEditor();
#endif
    }
};

class PluginSandboxF2WorkerDeathProductPathTest final : public juce::UnitTest
{
public:
    PluginSandboxF2WorkerDeathProductPathTest()
        : juce::UnitTest("plugin.sandbox.editor.worker-death-product-path.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("worker death leaves no stale product editor identity");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr || ! slot->isEditorOpen())
        {
            expect(false, "initial product editor open failed");
            return;
        }

        const auto info = proxy->editorInfo();
        const auto hwnd = reinterpret_cast<HWND>(
            static_cast<std::uintptr_t>(info.nativeWindowHandle));
        expect(proxy->terminateWorkerForEditorTest(3000),
               "bounded worker retirement failed");
        expect(slot->isSandboxed(), "worker death changed product execution mode");
        expect(! slot->isEditorOpen(), "worker death retained parent editor identity");
        expect(! proxy->isEditorOpen(), "worker death retained proxy editor identity");
        expect(! proxy->remoteAvailable(), "worker death left realtime gate open");
        expect(proxy->processDiagnostics().workerUnavailableDetected,
               "worker death was not recorded by the existing recovery path");
        expect(hwnd == nullptr || IsWindow(hwnd) == FALSE,
               "dead worker editor HWND remained valid");
#endif
    }
};

class PluginSandboxF2RemoveProductPathTest final : public juce::UnitTest
{
public:
    PluginSandboxF2RemoveProductPathTest()
        : juce::UnitTest("plugin.sandbox.editor.remove-product-path.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("product removal closes editor before worker teardown");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr || ! slot->isEditorOpen())
        {
            expect(false, "initial product editor open failed");
            return;
        }

        const auto pid = proxy->currentWorkerPid();
        slot.reset();
        expect(slot == nullptr, "product slot was not removed");
        expect(pid != 0, "product editor worker PID was missing");
        expect(processHasExited(pid, 5000),
               "worker remained alive after product slot removal");
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "product removal loaded the fixture module in the parent");
#endif
    }
};

class PluginSandboxF2EditorDeadlineTest final : public juce::UnitTest
{
public:
    PluginSandboxF2EditorDeadlineTest()
        : juce::UnitTest("plugin.sandbox.editor.deadline-policy.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("EditorCreate has an independent sixty-second heavy deadline");
        expectEquals(static_cast<juce::int64>(
                          DAW::PluginSandboxProcessCore::Options {}.startupTimeoutMs),
                      static_cast<juce::int64>(5000));
        expectEquals(static_cast<juce::int64>(
                          DAW::PluginSandboxProcessCore::Options {}.pluginCreateTimeoutMs),
                      static_cast<juce::int64>(60000));
        expectEquals(static_cast<juce::int64>(
                          DAW::PluginSandboxProcessCore::Options {}.editorCreateTimeoutMs),
                      static_cast<juce::int64>(60000));

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("normal fixture EditorCreate duration is measured");
        {
            auto options = editorProcessOptions();
            options.editorCreateTimeoutMs = 3000;
            DAW::PluginSandboxProcessCore process;
            if (startEditorProcess(*this, process, options))
            {
                DAW::PluginSandboxProcessCore::EditorInfo info;
                juce::String error;
                const auto startedAt = juce::Time::getMillisecondCounterHiRes();
                const auto created = process.createEditor("f2-normal-create", 1, info,
                                                          options.editorCreateTimeoutMs,
                                                          error);
                const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
                juce::Logger::writeToLog(
                    "[F2] normal fixture EditorCreate elapsedMs="
                    + juce::String(elapsedMs, 1)
                    + " created=" + juce::String(created ? 1 : 0)
                    + " error=" + error);
                expect(created, "normal fixture EditorCreate failed: " + error);
                expect(info.nativeWindowHandle != 0,
                       "normal fixture EditorCreate returned no HWND");
                expect(process.shutdown(5000), process.diagnostics().error);
            }
        }

        beginTest("slow finite EditorCreate beyond the old short budget succeeds");
        {
            auto options = editorProcessOptions();
            options.editorCreateTimeoutMs = 1500;
            options.editorCreateResponseDelayMilliseconds = 300;
            DAW::PluginSandboxProcessCore process;
            if (startEditorProcess(*this, process, options))
            {
                DAW::PluginSandboxProcessCore::EditorInfo info;
                juce::String error;
                const auto startedAt = juce::Time::getMillisecondCounterHiRes();
                const auto created = process.createEditor("f2-slow-create", 1, info,
                                                          options.editorCreateTimeoutMs,
                                                          error);
                const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
                juce::Logger::writeToLog(
                    "[F2] slow finite EditorCreate elapsedMs="
                    + juce::String(elapsedMs, 1)
                    + " created=" + juce::String(created ? 1 : 0)
                    + " error=" + error);
                expect(created,
                       "slow finite EditorCreate failed: " + error);
                expect(info.nativeWindowHandle != 0,
                       "slow finite EditorCreate returned no HWND");
                expect(process.shutdown(5000), process.diagnostics().error);
            }
        }

        beginTest("EditorCreate beyond its heavy deadline fails closed");
        {
            auto options = editorProcessOptions();
            options.editorCreateTimeoutMs = 100;
            options.editorCreateResponseDelayMilliseconds = 250;
            DAW::PluginSandboxProcessCore process;
            if (startEditorProcess(*this, process, options))
            {
                DAW::PluginSandboxProcessCore::EditorInfo info;
                juce::String error;
                expect(! process.createEditor("f2-heavy-timeout", 1, info,
                                               options.editorCreateTimeoutMs, error),
                       "over-budget EditorCreate unexpectedly succeeded");
                expect(info.nativeWindowHandle == 0,
                       "failed EditorCreate returned a stale HWND");
                expect(error.containsIgnoreCase("timed out"),
                       "failed EditorCreate did not report a timeout: " + error);
                expect(process.retireWorkerAfterFailure(3000),
                       "over-budget EditorCreate worker retirement failed");
            }
        }

        beginTest("EditorResize retains a short bounded deadline");
        {
            auto options = editorProcessOptions();
            options.editorResizeResponseDelayMilliseconds = 100;
            DAW::PluginSandboxProcessCore process;
            if (startEditorProcess(*this, process, options))
            {
                DAW::PluginSandboxProcessCore::EditorInfo info;
                juce::String error;
                expect(process.createEditor("f2-resize-timeout", 1, info, 1000, error),
                       "resize deadline setup EditorCreate failed: " + error);
                error.clear();
                expect(! process.resizeEditor(info, 320, 240, 25, error),
                       "delayed EditorResize unexpectedly succeeded within injected short deadline");
                expect(error.containsIgnoreCase("timed out"),
                       "EditorResize did not report its short deadline: " + error);
                expect(process.retireWorkerAfterFailure(3000),
                       "EditorResize timeout worker retirement failed");
            }
        }

        beginTest("EditorClose retains a short bounded deadline");
        {
            auto options = editorProcessOptions();
            options.editorCloseResponseDelayMilliseconds = 100;
            DAW::PluginSandboxProcessCore process;
            if (startEditorProcess(*this, process, options))
            {
                DAW::PluginSandboxProcessCore::EditorInfo info;
                juce::String error;
                expect(process.createEditor("f2-close-timeout", 1, info, 1000, error),
                       "close deadline setup EditorCreate failed: " + error);
                error.clear();
                expect(! process.closeEditor(info, 25, error),
                       "delayed EditorClose unexpectedly succeeded within injected short deadline");
                expect(error.containsIgnoreCase("timed out"),
                       "EditorClose did not report its short deadline: " + error);
                expect(process.retireWorkerAfterFailure(3000),
                       "EditorClose timeout worker retirement failed");
            }
        }
#else
        beginTest("Windows-only EditorCreate deadline seams");
        expect(true);
#endif
#else
        beginTest("Windows-only Phase F2 editor deadline policy");
        expect(false, "Phase F2 requires Windows");
#endif
    }
};

class PluginSandboxF2WindowTitleTest final : public juce::UnitTest
{
public:
    PluginSandboxF2WindowTitleTest()
        : juce::UnitTest("plugin.sandbox.editor.window-title.v1",
                         "PluginSandboxPhaseF2") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("worker-owned editor HWND has the canonical plugin title");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this);
        if (slot == nullptr)
            return;

        slot->openEditor();
        waitForProductEditorReady(*this, *slot);
        auto* proxy = slot->getSandboxProxy();
        expect(proxy != nullptr, "title test lost its sandbox proxy");
        expect(slot->isEditorOpen(), "title test could not create the editor");
        if (proxy != nullptr && slot->isEditorOpen())
        {
            const auto& info = proxy->editorInfo();
            expect(isVisibleWorkerWindow(info, *proxy),
                   "title test editor HWND failed worker identity validation");

            auto hwnd = reinterpret_cast<HWND>(
                static_cast<std::uintptr_t>(info.nativeWindowHandle));
            wchar_t title[512] = {};
            const auto length = hwnd != nullptr
                ? GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)))
                : 0;
            const juce::String nativeTitle(title);
            expect(length > 0, "worker editor native title is empty");
            expect(nativeTitle.contains(editorFixtureDescription().name),
                   "worker editor native title does not contain the canonical fixture name: "
                       + nativeTitle);
            expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
                   "title path loaded the fixture module in the parent");
        }
        slot->closeEditor();
#else
        beginTest("Windows-only Phase F2 window title");
        expect(false, "Phase F2 requires Windows");
#endif
    }
};

PluginSandboxF2OpenProductPathTest pluginSandboxF2OpenProductPathTest;
PluginSandboxF2ReopenFocusTest pluginSandboxF2ReopenFocusTest;
PluginSandboxF2CloseReopenTest pluginSandboxF2CloseReopenTest;
PluginSandboxF2WorkerDeathProductPathTest pluginSandboxF2WorkerDeathProductPathTest;
PluginSandboxF2RemoveProductPathTest pluginSandboxF2RemoveProductPathTest;
PluginSandboxF2EditorDeadlineTest pluginSandboxF2EditorDeadlineTest;
PluginSandboxF2WindowTitleTest pluginSandboxF2WindowTitleTest;
} // namespace

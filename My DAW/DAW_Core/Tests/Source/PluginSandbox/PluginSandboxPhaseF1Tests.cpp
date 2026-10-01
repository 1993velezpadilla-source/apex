#include <JuceHeader.h>

#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
constexpr int kEditorInitialWidth = 320;
constexpr int kEditorInitialHeight = 180;
constexpr int kEditorResizeWidth = 480;
constexpr int kEditorResizeHeight = 240;

juce::File environmentFile(const char* name)
{
    return juce::File(juce::SystemStats::getEnvironmentVariable(name, {}));
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

juce::PluginDescription noEditorFixtureDescription()
{
    const auto bundle = environmentFile("APEX_TEST_VST3_MONO_PATH");
    juce::PluginDescription description;
    description.name = "APEX Test VST3 Mono";
    description.descriptiveName = description.name;
    description.manufacturerName = "APEX";
    description.version = "1.0.0";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = bundle.getFullPathName();
    // The mono fixture has one deterministic component. UID zero lets the
    // worker resolve that component without a parent-side module load.
    description.uniqueId = 0;
    description.deprecatedUid = 0;
    description.numInputChannels = 1;
    description.numOutputChannels = 1;
    return description;
}

juce::File workerExecutable()
{
    const auto configured = environmentFile("APEX_TEST_PLUGIN_WORKER_PATH");
    if (configured.existsAsFile())
        return configured;
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
}

bool prepareProxy(juce::UnitTest& test,
                  DAW::SandboxedPluginProxyCore& proxy,
                  std::uint32_t channels)
{
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.mainInputChannels = channels;
    preparation.mainOutputChannels = channels;
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

bool isWorkerWindow(const DAW::SandboxedPluginProxyCore::EditorInfo& info,
                    DWORD expectedPid)
{
#if JUCE_WINDOWS
    const auto hwnd = reinterpret_cast<HWND>(
        static_cast<std::uintptr_t>(info.nativeWindowHandle));
    DWORD ownerPid = 0;
    return hwnd != nullptr && IsWindow(hwnd) != FALSE
        && GetWindowThreadProcessId(hwnd, &ownerPid) != 0
        && ownerPid == expectedPid;
#else
    juce::ignoreUnused(info, expectedPid);
    return false;
#endif
}

class PluginSandboxF1WorkerOwnedTest final : public juce::UnitTest
{
public:
    PluginSandboxF1WorkerOwnedTest()
        : juce::UnitTest("plugin.sandbox.editor.worker-owned.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("worker owns deterministic editor and native HWND");
        juce::MessageManager::getInstance();
        const auto description = editorFixtureDescription();
        expect(juce::File(description.fileOrIdentifier).isDirectory(),
               "editor fixture bundle is missing");
        if (! juce::File(description.fileOrIdentifier).isDirectory())
            return;

        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "fixture module was loaded in parent before editor create");
        DAW::SandboxedPluginProxyCore proxy(description);
        if (! prepareProxy(*this, proxy, 2))
            return;

        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        if (! proxy.isEditorOpen())
            return;
        const auto info = proxy.editorInfo();
        expect(info.nativeWindowHandle != 0, "worker returned a null HWND");
        expect(isWorkerWindow(info, proxy.currentWorkerPid()),
               "returned HWND is not owned by the current worker PID");
        expectEquals(info.workerGeneration, proxy.currentWorkerGeneration());
        expectEquals(info.width, static_cast<std::uint32_t>(kEditorInitialWidth));
        expectEquals(info.height, static_cast<std::uint32_t>(kEditorInitialHeight));
        expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
               "parent loaded the vendor fixture module");
        expect(proxy.isActive(), "plugin was not active while editor was open");

        expect(proxy.closeEditor(5000, error), error);
        expect(! proxy.isEditorOpen(), "parent retained a stale editor identity");
        expect(proxy.release(5000), "worker release failed after editor close");
#else
        beginTest("Windows-only Phase F1 test");
        expect(false, "Phase F1 requires Windows");
#endif
    }
};

class PluginSandboxF1LifecycleTest final : public juce::UnitTest
{
public:
    PluginSandboxF1LifecycleTest()
        : juce::UnitTest("plugin.sandbox.editor.lifecycle.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("create, resize, close while DSP remains active");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(editorFixtureDescription());
        if (! prepareProxy(*this, proxy, 2))
            return;
        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        expectEquals(proxy.editorInfo().width,
                     static_cast<std::uint32_t>(kEditorInitialWidth));
        expectEquals(proxy.editorInfo().height,
                     static_cast<std::uint32_t>(kEditorInitialHeight));
        expect(proxy.resizeEditor(kEditorResizeWidth, kEditorResizeHeight,
                                  5000, error), error);
        expectEquals(proxy.editorInfo().width,
                     static_cast<std::uint32_t>(kEditorResizeWidth));
        expectEquals(proxy.editorInfo().height,
                     static_cast<std::uint32_t>(kEditorResizeHeight));
        expect(proxy.closeEditor(5000, error), error);
        expect(proxy.isActive(), "plugin became inactive after editor close");
        expect(proxy.remoteAvailable(), "realtime worker gate closed after editor close");
        expect(proxy.release(5000), "worker release failed");
#endif
    }
};

class PluginSandboxF1NoEditorTest final : public juce::UnitTest
{
public:
    PluginSandboxF1NoEditorTest()
        : juce::UnitTest("plugin.sandbox.editor.no-editor.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("no-editor fixture fails closed without fallback");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(noEditorFixtureDescription());
        if (! prepareProxy(*this, proxy, 1))
            return;
        juce::String error;
        expect(! proxy.createEditor(5000, error),
               "no-editor fixture unexpectedly created an editor");
        expect(error.isNotEmpty(), "no-editor failure was not descriptive");
        expect(proxy.isActive(), "no-editor failure deactivated the plugin");
        expect(! proxy.isEditorOpen(), "no-editor failure left editor identity active");
        expect(GetModuleHandleW(L"APEXTestVST3Mono.vst3") == nullptr,
               "parent loaded the no-editor fixture module");
        expect(proxy.release(5000), "no-editor worker release failed");
#endif
    }
};

class PluginSandboxF1IdentityTest final : public juce::UnitTest
{
public:
    PluginSandboxF1IdentityTest()
        : juce::UnitTest("plugin.sandbox.editor.foreign-stale-identity.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("foreign HWND is rejected");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(editorFixtureDescription());
        if (! prepareProxy(*this, proxy, 2))
            return;
        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        auto foreign = proxy.editorInfo();
        foreign.nativeWindowHandle = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(GetDesktopWindow()));
        expect(! proxy.resizeEditorIdentityForTest(foreign, 400, 200,
                                                   1000, error),
               "foreign HWND was accepted");
        expect(proxy.isEditorOpen(), "foreign HWND rejection destroyed valid editor");

        beginTest("stale generation is rejected after worker replacement");
        const auto stale = proxy.editorInfo();
        const auto oldGeneration = stale.workerGeneration;
        DAW::SandboxedPluginProxyCore::Preparation preparation;
        preparation.mainInputChannels = 2;
        preparation.mainOutputChannels = 2;
        preparation.startupTimeoutMs = 5000;
        preparation.pluginCreateTimeoutMs = 60000;
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        preparation.workerExecutablePathForTest = workerExecutable().getFullPathName();
#endif
        expect(proxy.reprepare(preparation)
                   == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "worker replacement failed");
        expect(proxy.currentWorkerGeneration() != oldGeneration,
               "worker generation did not change");
        error.clear();
        expect(! proxy.resizeEditorIdentityForTest(stale, 400, 200, 1000, error),
               "stale editor identity was accepted");
        expect(error.isNotEmpty(), "stale identity rejection lacked an error");
        expect(proxy.release(5000), "replacement worker release failed");
#endif
    }
};

class PluginSandboxF1WorkerDeathTest final : public juce::UnitTest
{
public:
    PluginSandboxF1WorkerDeathTest()
        : juce::UnitTest("plugin.sandbox.editor.worker-death.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("worker death invalidates foreign editor identity");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(editorFixtureDescription());
        if (! prepareProxy(*this, proxy, 2))
            return;
        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        expect(proxy.terminateWorkerForEditorTest(3000),
               "bounded worker retirement failed");
        expect(! proxy.isEditorOpen(), "worker death retained stale editor identity");
        expect(! proxy.remoteAvailable(), "worker death left realtime gate open");
        expect(proxy.processDiagnostics().workerUnavailableDetected,
               "worker death was not recorded");
        expect(proxy.release(1000), "post-death proxy release failed");
#endif
    }
};

class PluginSandboxF1ShutdownTest final : public juce::UnitTest
{
public:
    PluginSandboxF1ShutdownTest()
        : juce::UnitTest("plugin.sandbox.editor.shutdown.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("editor-open shutdown is bounded and reaps worker");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(editorFixtureDescription());
        if (! prepareProxy(*this, proxy, 2))
            return;
        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        expect(proxy.release(5000), "shutdown with editor open failed");
        expect(! proxy.isEditorOpen(), "shutdown retained editor identity");
        expect(proxy.processDiagnostics().gracefulShutdown,
               "worker did not report graceful shutdown");
        expect(proxy.processDiagnostics().reaped, "worker was not reaped");
#endif
    }
};

class PluginSandboxF1PluginRemoveTest final : public juce::UnitTest
{
public:
    PluginSandboxF1PluginRemoveTest()
        : juce::UnitTest("plugin.sandbox.editor.plugin-remove.v1",
                         "PluginSandboxPhaseF1") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("plugin removal tears down editor before worker plugin");
        juce::MessageManager::getInstance();
        DAW::SandboxedPluginProxyCore proxy(editorFixtureDescription());
        if (! prepareProxy(*this, proxy, 2))
            return;
        juce::String error;
        expect(proxy.createEditor(5000, error), error);
        const auto pid = proxy.currentWorkerPid();
        expect(proxy.release(5000), "plugin removal release failed");
        expect(! proxy.isEditorOpen(), "plugin removal retained editor identity");
        expect(proxy.processDiagnostics().reaped, "plugin removal left worker unreaped");
        expect(pid != 0, "plugin removal had no worker PID");
#endif
    }
};

PluginSandboxF1WorkerOwnedTest pluginSandboxF1WorkerOwnedTest;
PluginSandboxF1LifecycleTest pluginSandboxF1LifecycleTest;
PluginSandboxF1NoEditorTest pluginSandboxF1NoEditorTest;
PluginSandboxF1IdentityTest pluginSandboxF1IdentityTest;
PluginSandboxF1WorkerDeathTest pluginSandboxF1WorkerDeathTest;
PluginSandboxF1ShutdownTest pluginSandboxF1ShutdownTest;
PluginSandboxF1PluginRemoveTest pluginSandboxF1PluginRemoveTest;
} // namespace

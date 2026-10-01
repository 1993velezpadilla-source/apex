#include <JuceHeader.h>

#include "../../../Source/PluginHostCore/PluginInstanceCore.h"
#include "../../../Source/PluginSandboxCore/SandboxedPluginProxyCore.h"

#include <chrono>
#include <cstdint>
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

DAW::SandboxedPluginProxyCore::Preparation editorPreparation(
    std::uint32_t editorDelayMs,
    std::uint32_t editorTimeoutMs = 60000)
{
    DAW::SandboxedPluginProxyCore::Preparation preparation;
    preparation.mainInputChannels = 2;
    preparation.mainOutputChannels = 2;
    preparation.startupTimeoutMs = 5000;
    preparation.pluginCreateTimeoutMs = 60000;
    preparation.editorCreateTimeoutMs = editorTimeoutMs;
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    preparation.workerExecutablePathForTest = workerExecutable().getFullPathName();
    preparation.editorCreateResponseDelayMilliseconds = editorDelayMs;
#else
    juce::ignoreUnused(editorDelayMs);
#endif
    return preparation;
}

std::unique_ptr<DAW::PluginInstanceCore> makeProductSlot(
    juce::UnitTest& test,
    std::uint32_t editorDelayMs,
    std::uint32_t editorTimeoutMs = 60000)
{
    const auto description = editorFixtureDescription();
    test.expect(juce::File(description.fileOrIdentifier).isDirectory(),
                "F3 editor fixture bundle is missing");
    if (! juce::File(description.fileOrIdentifier).isDirectory())
        return {};

#if JUCE_WINDOWS
    test.expect(GetModuleHandleW(L"APEXTestVST3.vst3") == nullptr,
                "F3 fixture module was loaded in parent before product open");
#endif

    auto proxy = std::make_unique<DAW::SandboxedPluginProxyCore>(description);
    const auto preparation = editorPreparation(editorDelayMs, editorTimeoutMs);
    const auto result = proxy->prepare(preparation);
    test.expect(result == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
                "F3 sandbox preparation failed: " + proxy->processDiagnostics().error);
    if (result != DAW::SandboxedPluginProxyCore::PrepareResult::Prepared)
        return {};

    auto slot = std::make_unique<DAW::PluginInstanceCore>(std::move(proxy));
    test.expect(slot->isSandboxed(), "F3 product path constructed an in-process slot");
    return slot;
}

bool lifecycleIsPending(DAW::SandboxedPluginProxyCore::EditorLifecycleState state)
{
    using State = DAW::SandboxedPluginProxyCore::EditorLifecycleState;
    return state == State::Requesting
        || state == State::Creating
        || state == State::Slow
        || state == State::CancelPending;
}

bool waitForState(juce::UnitTest& test,
                  DAW::SandboxedPluginProxyCore& proxy,
                  DAW::SandboxedPluginProxyCore::EditorLifecycleState wanted,
                  std::uint32_t timeoutMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (proxy.editorLifecycle().state == wanted)
            return true;

        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        {
            if (mm->isThisTheMessageThread())
                mm->runDispatchLoopUntil(5);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    test.expect(false, "timed out waiting for requested editor lifecycle state");
    return false;
}

#if JUCE_WINDOWS
bool isWorkerWindow(const DAW::SandboxedPluginProxyCore::EditorLifecycleSnapshot& snapshot,
                    const DAW::SandboxedPluginProxyCore& proxy)
{
    const auto& info = snapshot.info;
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
        && info.workerGeneration == proxy.currentWorkerGeneration();
}
#endif

class PluginSandboxF3PromptOpenTest final : public juce::UnitTest
{
public:
    PluginSandboxF3PromptOpenTest()
        : juce::UnitTest("plugin.sandbox.f3.editor-open-returns-promptly.v1",
                         "PluginSandboxPhaseF3") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("product open returns before delayed worker GUI creation completes");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this, 1200);
        if (slot == nullptr)
            return;

        auto* proxy = slot->getSandboxProxy();
        expect(proxy != nullptr, "F3 prompt test lost its sandbox proxy");
        if (proxy == nullptr)
            return;

        const auto startedAt = juce::Time::getMillisecondCounterHiRes();
        slot->openEditor();
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
        expect(elapsedMs < 500.0,
               "product open waited for delayed vendor GUI: elapsedMs="
                   + juce::String(elapsedMs, 1));

        const auto pending = proxy->editorLifecycle();
        expect(lifecycleIsPending(pending.state) || pending.state
                   == DAW::SandboxedPluginProxyCore::EditorLifecycleState::Ready,
               "product open did not enter an async pending/ready lifecycle state");
        expect(waitForState(*this, *proxy,
                            DAW::SandboxedPluginProxyCore::EditorLifecycleState::Ready),
               "delayed product editor did not become ready");

        const auto ready = proxy->editorLifecycle();
        expect(ready.info.nativeWindowHandle != 0,
               "async product open published no native editor HWND");
        expect(ready.identity.workerGeneration == proxy->currentWorkerGeneration(),
               "async ready result used the wrong worker generation");
#if JUCE_WINDOWS
        expect(isWorkerWindow(ready, *proxy),
               "async ready HWND is not visible and owned by the current worker");
#endif
        slot->closeEditor();
#else
        beginTest("Windows-only F3 prompt open");
        expect(false, "PluginSandboxPhaseF3 requires Windows");
#endif
    }
};

class PluginSandboxF3SlowFiniteTest final : public juce::UnitTest
{
public:
    PluginSandboxF3SlowFiniteTest()
        : juce::UnitTest("plugin.sandbox.f3.slow-finite-editor.v1",
                         "PluginSandboxPhaseF3") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("slow finite creation is not a total EditorCreate timeout failure");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this, 1500, 100);
        if (slot == nullptr)
            return;

        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr)
        {
            expect(false, "F3 slow test lost its sandbox proxy");
            return;
        }

        const auto startedAt = juce::Time::getMillisecondCounterHiRes();
        slot->openEditor();
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
        expect(elapsedMs < 500.0,
               "slow finite product open was synchronously deadline-bound: elapsedMs="
                   + juce::String(elapsedMs, 1));

        const auto pending = proxy->editorLifecycle();
        expect(pending.state != DAW::SandboxedPluginProxyCore::EditorLifecycleState::Failed,
               "slow finite editor was marked failed before worker completion");
        expect(waitForState(*this, *proxy,
                            DAW::SandboxedPluginProxyCore::EditorLifecycleState::Ready),
               "slow finite editor did not eventually become ready");
        expect(! proxy->editorLifecycle().error.containsIgnoreCase("timed out"),
               "slow finite editor reported a false timeout failure");
        slot->closeEditor();
#else
        beginTest("Windows-only F3 slow finite editor");
        expect(false, "PluginSandboxPhaseF3 requires Windows");
#endif
    }
};

class PluginSandboxF3DuplicateOpenTest final : public juce::UnitTest
{
public:
    PluginSandboxF3DuplicateOpenTest()
        : juce::UnitTest("plugin.sandbox.f3.duplicate-open-coalesces.v1",
                         "PluginSandboxPhaseF3") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("duplicate opens while creating reuse one request identity");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this, 1200);
        if (slot == nullptr)
            return;

        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr)
        {
            expect(false, "F3 duplicate test lost its sandbox proxy");
            return;
        }

        slot->openEditor();
        const auto first = proxy->editorLifecycle();
        slot->openEditor();
        slot->openEditor();
        const auto coalesced = proxy->editorLifecycle();
        expect(first.identity.workerGeneration != 0,
               "duplicate test did not receive a worker generation");
        expect(first.identity.editorRequestId != 0,
               "duplicate test did not receive an editor request ID");
        expect(coalesced.identity.workerGeneration == first.identity.workerGeneration,
               "duplicate open changed worker generation");
        expect(coalesced.identity.editorRequestId == first.identity.editorRequestId,
               "duplicate open created a second logical editor request");
        expect(waitForState(*this, *proxy,
                            DAW::SandboxedPluginProxyCore::EditorLifecycleState::Ready),
               "coalesced editor request did not become ready");
        slot->closeEditor();
#else
        beginTest("Windows-only F3 duplicate open");
        expect(false, "PluginSandboxPhaseF3 requires Windows");
#endif
    }
};

class PluginSandboxF3CloseWhileCreatingTest final : public juce::UnitTest
{
public:
    PluginSandboxF3CloseWhileCreatingTest()
        : juce::UnitTest("plugin.sandbox.f3.close-while-creating.v1",
                         "PluginSandboxPhaseF3") {}

    void runTest() override
    {
#if JUCE_WINDOWS
        beginTest("close while creating sets intent without waiting for vendor GUI");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this, 1500);
        if (slot == nullptr)
            return;

        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr)
        {
            expect(false, "F3 close test lost its sandbox proxy");
            return;
        }

        slot->openEditor();
        const auto opened = proxy->editorLifecycle();
        expect(lifecycleIsPending(opened.state),
               "close test did not observe a pending editor lifecycle");

        const auto startedAt = juce::Time::getMillisecondCounterHiRes();
        slot->closeEditor();
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startedAt;
        expect(elapsedMs < 500.0,
               "close while creating waited for vendor GUI: elapsedMs="
                   + juce::String(elapsedMs, 1));
        expect(! proxy->editorLifecycle().desiredOpen,
               "close while creating did not clear desiredOpen");

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(5000);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto state = proxy->editorLifecycle().state;
            if (state == DAW::SandboxedPluginProxyCore::EditorLifecycleState::Closed
                || state == DAW::SandboxedPluginProxyCore::EditorLifecycleState::Cancelled)
                break;
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
                mm != nullptr && mm->isThisTheMessageThread())
                mm->runDispatchLoopUntil(5);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const auto closed = proxy->editorLifecycle();
        expect(! closed.desiredOpen, "late editor result restored desiredOpen");
        expect(closed.state == DAW::SandboxedPluginProxyCore::EditorLifecycleState::Closed
                   || closed.state == DAW::SandboxedPluginProxyCore::EditorLifecycleState::Cancelled,
               "close while creating did not settle into a closed/cancelled state");
        expect(! slot->isEditorOpen(), "late READY result exposed a closed editor");
#else
        beginTest("Windows-only F3 close while creating");
        expect(false, "PluginSandboxPhaseF3 requires Windows");
#endif
    }
};

class PluginSandboxF3StaleGenerationTest final : public juce::UnitTest
{
public:
    PluginSandboxF3StaleGenerationTest()
        : juce::UnitTest("plugin.sandbox.f3.stale-generation-result.v1",
                         "PluginSandboxPhaseF3") {}

    void runTest() override
    {
#if JUCE_WINDOWS && defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        beginTest("late old-generation result cannot mutate the current editor lifecycle");
        juce::MessageManager::getInstance();
        auto slot = makeProductSlot(*this, 1200);
        if (slot == nullptr)
            return;

        auto* proxy = slot->getSandboxProxy();
        if (proxy == nullptr)
        {
            expect(false, "F3 stale test lost its sandbox proxy");
            return;
        }

        slot->openEditor();
        const auto oldIdentity = proxy->editorLifecycle();
        expect(oldIdentity.identity.workerGeneration != 0,
               "stale test did not capture generation A");
        expect(oldIdentity.identity.editorRequestId != 0,
               "stale test did not capture request A");

        expect(proxy->terminateWorkerForEditorTest(3000),
               "stale test could not retire generation A worker");
        const auto preparation = editorPreparation(0);
        expect(proxy->prepare(preparation)
                   == DAW::SandboxedPluginProxyCore::PrepareResult::Prepared,
               "stale test could not prepare generation B worker");

        slot->openEditor();
        const auto current = proxy->editorLifecycle();
        expect(current.identity.workerGeneration != oldIdentity.identity.workerGeneration,
               "generation B did not replace generation A");

        auto staleReady = oldIdentity;
        staleReady.state = DAW::SandboxedPluginProxyCore::EditorLifecycleState::Ready;
        staleReady.desiredOpen = true;
        staleReady.info.nativeWindowHandle = 1;
        expect(! proxy->acceptEditorStatusForTest(staleReady),
               "stale generation result was accepted");

        const auto afterStale = proxy->editorLifecycle();
        expect(afterStale.identity.workerGeneration == current.identity.workerGeneration,
               "stale generation changed the current worker identity");
        expect(afterStale.identity.editorRequestId == current.identity.editorRequestId,
               "stale generation changed the current request identity");
        expect(afterStale.info.nativeWindowHandle != 1,
               "stale generation published an invalid HWND");
        slot->closeEditor();
#else
        beginTest("Windows/test-hook F3 stale generation");
        expect(false, "PluginSandboxPhaseF3 stale generation requires Windows test hooks");
#endif
    }
};

PluginSandboxF3PromptOpenTest pluginSandboxF3PromptOpenTest;
PluginSandboxF3SlowFiniteTest pluginSandboxF3SlowFiniteTest;
PluginSandboxF3DuplicateOpenTest pluginSandboxF3DuplicateOpenTest;
PluginSandboxF3CloseWhileCreatingTest pluginSandboxF3CloseWhileCreatingTest;
PluginSandboxF3StaleGenerationTest pluginSandboxF3StaleGenerationTest;
} // namespace

#include <JuceHeader.h>
#include "../../../Source/PluginHostCore/PluginChainCore.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
struct EditorLifecycleProbe
{
    std::atomic<int> timerCallbacks { 0 };
    std::atomic<int> timerCallbacksDuringTeardown { 0 };
    std::atomic<int> editorsCreated { 0 };
    std::atomic<int> editorsDestroyed { 0 };
    std::atomic<int> processCalls { 0 };
    std::atomic<int> processorsDestroyed { 0 };
    std::atomic<bool> teardownStarted { false };
    std::atomic<bool> nestedDispatchRan { false };
    std::atomic<bool> processingTimedOut { false };
    juce::WaitableEvent processEntered;
    juce::WaitableEvent allowProcessReturn;
    std::atomic<bool> gateNextProcess { false };
};

class NestedDispatchEditor final : public juce::Component
{
public:
    explicit NestedDispatchEditor(std::shared_ptr<EditorLifecycleProbe> probe)
        : probe_(std::move(probe))
    {
        setSize(320, 180);
    }

    ~NestedDispatchEditor() override
    {
        probe_->teardownStarted.store(true, std::memory_order_release);
        juce::MessageManager::callAsync([probe = probe_]
        {
            probe->nestedDispatchRan.store(true, std::memory_order_release);
        });

        // Model a native view that pumps a nested message loop from removed().
        // This is bounded test-only synchronization, never a production delay.
        if (auto* messages = juce::MessageManager::getInstanceWithoutCreating())
            messages->runDispatchLoopUntil(120);

        probe_->editorsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<EditorLifecycleProbe> probe_;
};

struct ContentLifetimeProbe
{
    std::atomic<int> editorsDestroyed { 0 };
    std::atomic<bool> callbackStateAliveDuringEditorDestruction { false };
};

class ContentLifetimeEditor final : public juce::Component
{
public:
    ContentLifetimeEditor(std::weak_ptr<int> callbackState,
                          std::shared_ptr<ContentLifetimeProbe> probe)
        : callbackState_(std::move(callbackState)), probe_(std::move(probe))
    {
        setSize(320, 180);
    }

    ~ContentLifetimeEditor() override
    {
        probe_->callbackStateAliveDuringEditorDestruction.store(! callbackState_.expired(),
                                                                 std::memory_order_release);
        probe_->editorsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::weak_ptr<int> callbackState_;
    std::shared_ptr<ContentLifetimeProbe> probe_;
};

class ProbeAudioEditor final : public juce::AudioProcessorEditor
{
public:
    ProbeAudioEditor(juce::AudioProcessor& processor,
                     std::shared_ptr<EditorLifecycleProbe> probe)
        : juce::AudioProcessorEditor(processor), probe_(std::move(probe))
    {
        probe_->editorsCreated.fetch_add(1, std::memory_order_relaxed);
        setSize(320, 180);
    }

    ~ProbeAudioEditor() override
    {
        probe_->editorsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<EditorLifecycleProbe> probe_;
};

class EditorLifecyclePlugin final : public juce::AudioPluginInstance
{
public:
    explicit EditorLifecyclePlugin(std::shared_ptr<EditorLifecycleProbe> probe)
        : juce::AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          probe_(std::move(probe))
    {
    }

    ~EditorLifecyclePlugin() override
    {
        probe_->processorsDestroyed.fetch_add(1, std::memory_order_relaxed);
    }

    const juce::String getName() const override { return "APEX Editor Lifecycle Probe"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        if (probe_->gateNextProcess.exchange(false, std::memory_order_acq_rel))
        {
            probe_->processEntered.signal();
            if (! probe_->allowProcessReturn.wait(5000))
                probe_->processingTimedOut.store(true, std::memory_order_release);
        }

        probe_->processCalls.fetch_add(1, std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override
    {
        return new ProbeAudioEditor(*this, probe_);
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destinationData) override { destinationData.reset(); }
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& description) const override
    {
        description.name = getName();
        description.descriptiveName = getName();
        description.pluginFormatName = "APEX Test";
        description.manufacturerName = "APEX";
        description.version = "1";
        description.uniqueId = 0x454C5052;
        description.deprecatedUid = description.uniqueId;
        description.numInputChannels = 2;
        description.numOutputChannels = 2;
    }

private:
    std::shared_ptr<EditorLifecycleProbe> probe_;
};

class PluginEditorLifecycleTests final : public juce::UnitTest
{
public:
    PluginEditorLifecycleTests()
        : juce::UnitTest("plugin.editor-lifecycle.v1", "PluginHost")
    {
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

       #if JUCE_WINDOWS
        beginTest("interactive editor has a real APEX owner and remains visible after owner activation");
        {
            juce::DocumentWindow owner("APEX owner", juce::Colours::black, 0, true);
            owner.setBounds(50, 50, 480, 320);
            owner.setVisible(true);

            DAW::PluginInstanceCore::PluginEditorWindow editor("Owned plugin", &owner);
            editor.setBounds(90, 90, 320, 180);
            editor.setVisible(true);

            auto* ownerPeer = owner.getPeer();
            auto* editorPeer = editor.getPeer();
            expect(ownerPeer != nullptr && editorPeer != nullptr);
            if (ownerPeer != nullptr && editorPeer != nullptr)
            {
                auto ownerHwnd = static_cast<HWND>(ownerPeer->getNativeHandle());
                auto editorHwnd = static_cast<HWND>(editorPeer->getNativeHandle());
                expect(::GetWindow(editorHwnd, GW_OWNER) == ownerHwnd,
                       "plugin HWND must be owned by the APEX top-level HWND");

                const auto exStyle = ::GetWindowLongPtr(editorHwnd, GWL_EXSTYLE);
                expect((exStyle & WS_EX_TOOLWINDOW) != 0);
                expect((exStyle & WS_EX_NOACTIVATE) == 0,
                       "interactive editors must accept keyboard/IME focus");

                owner.toFront(true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
                expect(editor.isVisible());
                expect(::IsWindowVisible(editorHwnd) != FALSE);
                expect(::GetWindow(editorHwnd, GW_OWNER) == ownerHwnd);
            }
        }
       #endif

        beginTest("SafeEditorHost defers native editor destruction until after window teardown");
        {
            auto probe = std::make_shared<EditorLifecycleProbe>();
            std::unique_ptr<DAW::PluginInstanceCore::PluginEditorWindow::SafeEditorHost> host;

            {
                DAW::PluginInstanceCore::PluginEditorWindow owner("Timer teardown probe");
                owner.onGetSlotMix = [probe]
                {
                    probe->timerCallbacks.fetch_add(1, std::memory_order_relaxed);
                    if (probe->teardownStarted.load(std::memory_order_acquire))
                        probe->timerCallbacksDuringTeardown.fetch_add(1, std::memory_order_relaxed);
                    return 1.0f;
                };

                host = std::make_unique<DAW::PluginInstanceCore::PluginEditorWindow::SafeEditorHost>(
                    owner, new NestedDispatchEditor(probe));

                juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
                expect(probe->timerCallbacks.load(std::memory_order_acquire) > 0,
                       "the timer must fire before teardown so the regression cannot pass vacuously");

                host.reset();

                // C6-editor-lifetime contract: the host detaches the native
                // editor instead of destroying it, so the plugin's UI state
                // survives the shell window's HWND teardown (WM_DESTROY
                // dispatch runs plugin window procs). The editor is destroyed
                // only after the shell window itself dies.
                expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 0,
                             "the editor must be detached, not destroyed, while the shell window still exists");
            }   // <- shell window destroyed here: HWND teardown first, then the deferred editor

            // The deferred destruction runs on the next message-loop dispatch.
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);

            expect(probe->nestedDispatchRan.load(std::memory_order_acquire),
                   "the editor destructor must exercise nested message dispatch");
            expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 1,
                         "the deferred editor must be destroyed exactly once after window teardown");
            expectEquals(probe->timerCallbacksDuringTeardown.load(std::memory_order_acquire), 0,
                         "no host timer callback may execute once editor teardown starts");
        }

        beginTest("PluginEditorWindow defers native editor destruction past window teardown");
        {
            auto probe = std::make_shared<ContentLifetimeProbe>();
            auto callbackState = std::make_shared<int>(42);
            std::weak_ptr<int> callbackStateWeak = callbackState;

            auto window = std::make_unique<DAW::PluginInstanceCore::PluginEditorWindow>(
                "Content lifetime probe");
            window->onGetSlotMix = [callbackState] { return *callbackState == 42 ? 1.0f : 0.0f; };
            window->setEditorContent(new ContentLifetimeEditor(callbackStateWeak, probe));
            callbackState.reset();

            window.reset();

            // C6-editor-lifetime: the native editor is destroyed on the next
            // message-loop dispatch, AFTER the shell window's HWND teardown
            // (and therefore after the window's callback members die).
            juce::MessageManager::getInstance()->runDispatchLoopUntil(120);

            expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 1);
            expect(! probe->callbackStateAliveDuringEditorDestruction.load(std::memory_order_acquire),
                   "window callback members are released before the deferred editor destruction");
            expect(callbackStateWeak.expired(),
                   "callback-owned state must be released after the window finishes destruction");
        }

        beginTest("editor close remains independent while audio processing is in flight");
        {
            auto probe = std::make_shared<EditorLifecycleProbe>();
            auto plugin = std::make_unique<EditorLifecyclePlugin>(probe);
            DAW::PluginInstanceCore instance(std::move(plugin));
            instance.prepare(48000.0, 128);
            instance.openEditor();
            expect(instance.isEditorCreated());

            juce::AudioBuffer<float> buffer(2, 128);
            buffer.clear();
            juce::MidiBuffer midi;
            probe->gateNextProcess.store(true, std::memory_order_release);

            std::thread audioWorker([&]
            {
                instance.processBlock(buffer, midi, 128);
            });

            const bool processingEntered = probe->processEntered.wait(5000);
            expect(processingEntered, "synthetic audio processing must enter before editor close");
            if (processingEntered)
            {
                instance.closeEditor();
                expect(! instance.isEditorCreated());
                expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 1);
                expectEquals(probe->processorsDestroyed.load(std::memory_order_acquire), 0,
                             "closing the editor must not destroy the processor");
            }

            probe->allowProcessReturn.signal();
            audioWorker.join();

            expect(! probe->processingTimedOut.load(std::memory_order_acquire));
            instance.processBlock(buffer, midi, 128);
            expectEquals(probe->processCalls.load(std::memory_order_acquire), 2,
                         "DSP must continue after the editor closes");
        }

        beginTest("repeated create/open/close/remove/recreate preserves exact ownership");
        {
            constexpr int cycles = 8;
            std::vector<std::shared_ptr<EditorLifecycleProbe>> probes;
            probes.reserve(cycles);

            {
                DAW::PluginChainCore chain;
                chain.prepare(48000.0, 128);
                juce::AudioBuffer<float> buffer(2, 128);

                for (int cycle = 0; cycle < cycles; ++cycle)
                {
                    auto probe = std::make_shared<EditorLifecycleProbe>();
                    probes.push_back(probe);
                    expectEquals(chain.appendPluginInstanceForTesting(
                                     std::make_unique<EditorLifecyclePlugin>(probe)), 0);

                    auto* slot = chain.getSlot(0);
                    expect(slot != nullptr);
                    if (slot == nullptr)
                        break;

                    slot->openEditor();
                    expect(slot->isEditorCreated());
                    buffer.clear();
                    chain.processBlock(buffer, 128);

                    slot->closeEditor();
                    expect(! slot->isEditorCreated());
                    buffer.clear();
                    chain.processBlock(buffer, 128);

                    chain.removePlugin(0);
                    expectEquals(chain.getNumActiveSlots(), 0);
                    expectEquals(probe->editorsCreated.load(std::memory_order_acquire), 1);
                    expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 1);
                    expectEquals(probe->processCalls.load(std::memory_order_acquire), 2);
                }
            }

            for (const auto& probe : probes)
            {
                expectEquals(probe->editorsCreated.load(std::memory_order_acquire), 1);
                expectEquals(probe->editorsDestroyed.load(std::memory_order_acquire), 1);
                expectEquals(probe->processorsDestroyed.load(std::memory_order_acquire), 1,
                             "retired processor must be reclaimed off the audio thread");
            }
        }
    }
};

PluginEditorLifecycleTests pluginEditorLifecycleTests;
}

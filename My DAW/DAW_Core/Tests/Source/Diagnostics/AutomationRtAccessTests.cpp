#include <JuceHeader.h>

#include "../../../Source/Automation/AutomationParameterRegistryCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationModeStateCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Source/Automation/AutomationEvaluatorCore.h"
#include "../../../Source/Automation/AutomationGestureQueueCore.h"
#include "../../../Source/Automation/AutomationClockCore.h"
#include "../../../Source/Automation/AutomationRecorderCore.h"
#include "../../../Source/Automation/AutomationGestureBridgeCore.h"
#include "../../../Source/AutomationCore/PluginAutomationRecorderCore.h"
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace apex::automation;

/**
    Regression tests for the low-buffer hardening of the apex::automation
    realtime access path.

    Background: every rolling audio block previously took a global
    CriticalSection for the ENTIRE parameter-registry walk
    (AutomationEvaluator via registry.forEach), plus per-parameter
    CriticalSections in AutomationModeState::getMode,
    AutomationLaneStore::findLane and AutomationParameterKeyRegistry::findID,
    and PluginInstanceCore additionally built heap juce::String keys per
    parameter per block. Any message-thread automation edit (lane write,
    mode change, parameter registration) could block the audio callback —
    a priority-inversion window that scales with parameter count and is
    fatal at 64/128-sample buffers.

    The hardened design publishes immutable copy-on-write snapshots on
    mutation (message thread) and gives the audio thread lock-free,
    allocation-free RT accessors. These tests pin snapshot semantics,
    retire semantics, change generations, allocation-freedom, and
    end-to-end evaluator behavior.
*/
class AutomationRtAccessTests final : public juce::UnitTest
{
public:
    AutomationRtAccessTests() : juce::UnitTest ("automation.rt-access.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("key registry RT snapshot");
        {
            AutomationParameterKeyRegistry reg;
            const ParameterID idA = reg.getOrCreateID ("plugin.TRK_1.slot0.Foo.cutoff");
            const ParameterID idB = reg.getOrCreateID ("track.TRK_1.volume");
            expect (idA != kInvalidParameterID && idB != kInvalidParameterID && idA != idB);
            expectEquals ((int) reg.findIDRT ("plugin.TRK_1.slot0.Foo.cutoff"), (int) idA);
            expectEquals ((int) reg.findIDRT ("track.TRK_1.volume"), (int) idB);
            expectEquals ((int) reg.findIDRT ("nonexistent.key"), (int) kInvalidParameterID);

            const auto gen0 = reg.getChangeGeneration();
            reg.getOrCreateID ("new.key");
            expect (reg.getChangeGeneration() != gen0);

            // getOrCreateID on an existing key neither republishes nor changes the ID.
            const auto gen1 = reg.getChangeGeneration();
            expectEquals ((int) reg.getOrCreateID ("track.TRK_1.volume"), (int) idB);
            expectEquals (reg.getChangeGeneration(), gen1);

            // findIDRT must be allocation-free (audio-thread contract).
            // NOTE: the juce::String key is hoisted out of the checked scope —
            // constructing a String from a literal legitimately allocates,
            // which is exactly why the production RT path caches its Strings.
            const juce::String cachedKey ("track.TRK_1.volume");
            {
                juce::UnitTestAllocationChecker checker (*this);
                std::uint64_t acc = 0;
                for (int i = 0; i < 1000; ++i)
                    acc += reg.findIDRT (cachedKey);
                expect (acc != 0);
            }
        }

        beginTest ("lane store RT snapshot and lane lifetime");
        {
            AutomationLaneStore store;
            expect (store.findLaneRT (42) == nullptr);

            auto& lane = store.getOrCreateLane (42);
            auto found = store.findLaneRT (42);
            expect (found != nullptr);
            expect (found.get() == &lane);
            expect (store.findLaneRT (7) == nullptr);

            // A lane held by the audio thread survives removeLane (RCU).
            auto& lane99 = store.getOrCreateLane (99);
            (void) lane99;
            auto held = store.findLaneRT (99);
            store.removeLane (99);
            expect (store.findLaneRT (99) == nullptr);
            expect (held != nullptr);
            expectEquals ((int) held->getParameterID(), 99);

            const auto gen0 = store.getChangeGeneration();
            store.getOrCreateLane (100);
            expect (store.getChangeGeneration() != gen0);

            // findLaneRT must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                bool ok = true;
                for (int i = 0; i < 1000; ++i)
                    ok = ok && (store.findLaneRT (100) != nullptr)
                            && (store.findLaneRT (12345) == nullptr);
                expect (ok);
            }
        }

        beginTest ("mode state RT reads");
        {
            AutomationModeState modes;
            expect (modes.getModeRT (11) == AutomationMode::Read); // global default
            modes.setMode (11, AutomationMode::Touch);
            expect (modes.getModeRT (11) == AutomationMode::Touch);
            expect (modes.isReadingRT (11));

            modes.setGlobalDefaultMode (AutomationMode::Trim);
            expect (modes.getModeRT (22) == AutomationMode::Trim);

            modes.setLatchHeld (33, true);
            expect (modes.latchHeldRT (33));
            expect (! modes.latchHeldRT (44));
            modes.clearAllLatches();
            expect (! modes.latchHeldRT (33));

            modes.clearOverride (11);
            expect (modes.getModeRT (11) == AutomationMode::Trim);

            // RT reads must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                int acc = 0;
                for (int i = 0; i < 1000; ++i)
                {
                    if (modes.isReadingRT (11)) ++acc;
                    if (modes.latchHeldRT (33)) ++acc;
                }
                expectEquals (acc, 1000);
            }
        }

        beginTest ("registry RT snapshot and retire semantics");
        {
            AutomationParameterRegistry reg;
            ParameterRange range;
            range.defaultValue = 0.5f;

            auto* p1 = reg.createParameter (101, "cutoff", range);
            auto* p2 = reg.createParameter (102, "resonance", range);
            expect (p1 != nullptr && p2 != nullptr);
            expect (reg.findRT (101).get() == p1);
            expect (reg.findRT (103) == nullptr);

            int count = 0;
            reg.forEachRT ([&] (AutomationParameter&) { ++count; });
            expectEquals (count, 2);

            // Retire: unregistered parameters stay alive for in-flight RT
            // readers holding a shared_ptr (never destroyed on the audio thread).
            auto held = reg.findRT (101);
            expect (reg.unregisterParameter (101));
            expect (reg.findRT (101) == nullptr);
            expect (held != nullptr);
            expectEquals ((int) held->getID(), 101);

            // findRT + forEachRT must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                int n = 0;
                for (int i = 0; i < 100; ++i)
                {
                    if (reg.findRT (102) != nullptr) ++n;
                    reg.forEachRT ([] (AutomationParameter&) {});
                }
                expectEquals (n, 100);
            }
        }

        beginTest ("evaluator drives parameters from published snapshots");
        {
            AutomationParameterRegistry reg;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationEvaluator eval (reg, lanes, modes);

            ParameterRange range; // 0..1, default 0
            auto* p = reg.createParameter (7, "wet", range);
            expect (p != nullptr);

            {
                AutomationLane::PointVector pts;
                pts.push_back ({ 0.0, 0.25f, CurveType::Linear, 0.0f });
                pts.push_back ({ 4.0, 0.75f, CurveType::Linear, 0.0f });
                lanes.getOrCreateLane (7).replacePoints (std::move (pts));
            }

            eval.evaluateBlock (2.0, true); // linear midpoint between the two points
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);

            // Stopped transport -> no evaluation, value holds.
            eval.evaluateBlock (4.0, false);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);

            // Mode Off -> parameter skipped.
            modes.setMode (7, AutomationMode::Off);
            eval.evaluateBlock (4.0, true);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);
            modes.setMode (7, AutomationMode::Read);
            eval.evaluateBlock (4.0, true);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.75f, 1.0e-5f);

            // Steady-state evaluation must be allocation-free (this is the
            // exact call the audio engine makes every rolling block).
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 500; ++i)
                    eval.evaluateBlock (0.001 * (double) i, true);
            }
        }
    }
};

static AutomationRtAccessTests automationRtAccessTests;

/**
    The recorder has ONE message-thread consumer, but native controls and
    hosted VST3 parameter callbacks can push from MANY producer threads.
    This test prevents a reservation-before-publication race in which the
    consumer previously read unwritten/mismatched events.
*/
class AutomationGestureQueueMpscTests final : public juce::UnitTest
{
public:
    AutomationGestureQueueMpscTests()
        : juce::UnitTest("automation.gesture-mpsc-publication.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using Queue = AutomationGestureQueue;
        using Event = Queue::Event;

        beginTest("FIFO and exact full-capacity boundary with wraparound");
        {
            // The queue is intentionally large; keep it off the test thread's
            // relatively small Windows stack.
            auto q = std::make_unique<Queue>();
            for (std::size_t i = 0; i < Queue::kCapacity; ++i)
            {
                Event e;
                e.paramID = static_cast<ParameterID>(i + 1);
                e.normalizedValue = static_cast<float>(i % 100) / 100.0f;
                if (!q->push(e))
                {
                    expect(false, "queue became full before its published capacity");
                    return;
                }
            }
            Event extra;
            extra.paramID = 99999;
            expect(!q->push(extra), "a full queue must refuse to overwrite pending gestures");
            expectEquals(static_cast<int>(q->getOverflowCount()), 1);

            bool valid = true;
            for (std::size_t i = 0; i < Queue::kCapacity / 2; ++i)
            {
                Event out;
                if (!q->pop(out) || out.paramID != i + 1)
                    valid = false;
            }
            for (std::size_t i = 0; i < Queue::kCapacity / 2; ++i)
            {
                Event e;
                e.paramID = static_cast<ParameterID>(Queue::kCapacity + i + 1);
                if (!q->push(e))
                    valid = false;
            }
            for (std::size_t i = Queue::kCapacity / 2; i < Queue::kCapacity * 3 / 2; ++i)
            {
                Event out;
                if (!q->pop(out) || out.paramID != i + 1)
                    valid = false;
            }
            Event out;
            expect(valid, "payloads must remain FIFO and unique across wraparound");
            expect(!q->pop(out), "a fully drained queue must be empty");
        }

        beginTest("four concurrent plugin/UI producers never expose unpublished events");
        {
            constexpr int kProducers = 4;
            constexpr int kPerProducer = 3072;
            auto q = std::make_unique<Queue>();
            std::atomic<bool> begin { false };
            std::atomic<bool> abort { false };
            std::atomic<int> finished { 0 };
            std::vector<std::thread> producers;
            producers.reserve(kProducers);

            for (int producer = 0; producer < kProducers; ++producer)
            {
                producers.emplace_back([&, producer]
                {
                    while (!begin.load(std::memory_order_acquire))
                        std::this_thread::yield();

                    for (int n = 0; n < kPerProducer; ++n)
                    {
                        Event e;
                        e.paramID = static_cast<ParameterID>(producer * kPerProducer + n + 1);
                        e.kind = n % 3 == 0 ? Queue::EventKind::GestureBegin
                               : n % 3 == 1 ? Queue::EventKind::ValueChange
                                            : Queue::EventKind::GestureEnd;
                        e.source = ChangeSource::Plugin;
                        e.normalizedValue = static_cast<float>(n) / kPerProducer;
                        e.ppqAtCapture = producer * 100000.0 + n;
                        while (!q->push(e))
                        {
                            if (abort.load(std::memory_order_acquire))
                                return;
                            std::this_thread::yield();
                        }
                    }
                    finished.fetch_add(1, std::memory_order_release);
                });
            }

            std::array<int, kProducers> receivedPerProducer {};
            int received = 0;
            bool integrityOk = true;
            begin.store(true, std::memory_order_release);
            const double deadlineMs = juce::Time::getMillisecondCounterHiRes() + 20000.0;

            while (received < kProducers * kPerProducer
                   && juce::Time::getMillisecondCounterHiRes() < deadlineMs)
            {
                Event out;
                if (!q->pop(out))
                {
                    std::this_thread::yield();
                    continue;
                }

                const auto index = static_cast<int>(out.paramID) - 1;
                if (index < 0 || index >= kProducers * kPerProducer)
                {
                    integrityOk = false;
                    continue;
                }

                const int producer = index / kPerProducer;
                const int n = index % kPerProducer;
                if (receivedPerProducer[producer] != n
                    || out.source != ChangeSource::Plugin
                    || out.kind != (n % 3 == 0 ? Queue::EventKind::GestureBegin
                                  : n % 3 == 1 ? Queue::EventKind::ValueChange
                                               : Queue::EventKind::GestureEnd)
                    || out.ppqAtCapture != producer * 100000.0 + n
                    || std::abs(out.normalizedValue - (static_cast<float>(n) / kPerProducer)) > 1.0e-6f)
                    integrityOk = false;

                ++receivedPerProducer[producer];
                ++received;
            }

            // All writers eventually finish because this is a bounded test.
            // Any timeout is a real regression: drain remaining slots first
            // so writers can finish, then report the timeout as a failure.
            if (received < kProducers * kPerProducer)
            {
                const auto rescueDeadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
                while (finished.load(std::memory_order_acquire) < kProducers
                       && juce::Time::getMillisecondCounterHiRes() < rescueDeadline)
                {
                    Event out;
                    if (!q->pop(out))
                        std::this_thread::yield();
                }
            }
            abort.store(true, std::memory_order_release);
            for (auto& t : producers)
                t.join();

            expectEquals(received, kProducers * kPerProducer,
                         "all gesture events must be consumed exactly once");
            expect(integrityOk, "consumer observed a missing, reordered or partially written gesture");
            Event out;
            expect(!q->pop(out), "queue must be empty after all concurrent producers finish");
        }
    }
};

static AutomationGestureQueueMpscTests automationGestureQueueMpscTests;

/**
    A single recorder tick must be able to observe a Play or Stop edge.
    Previously consumeTransportStoppedEdge() mutated the observation cursor
    before consumeTransportStartedEdge() checked it, swallowing every Start.
*/
class AutomationTransportEdgeTests final : public juce::UnitTest
{
public:
    AutomationTransportEdgeTests()
        : juce::UnitTest("automation.transport-edges-play-stop.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        AutomationClock clock;

        beginTest("initially stopped and no spurious transition");
        auto e = clock.consumeTransportEdges();
        expect(!e.started && !e.stopped);

        beginTest("Play is reported once, not swallowed by Stop observation");
        clock.publishFromAudioThread(4.0, 0.0001, true);
        e = clock.consumeTransportEdges();
        expect(e.started && !e.stopped);
        e = clock.consumeTransportEdges();
        expect(!e.started && !e.stopped);

        beginTest("Stop is reported once");
        clock.publishFromAudioThread(5.0, 0.0001, false);
        e = clock.consumeTransportEdges();
        expect(!e.started && e.stopped);
        e = clock.consumeTransportEdges();
        expect(!e.started && !e.stopped);

        beginTest("restart creates a fresh Play edge");
        clock.publishFromAudioThread(0.0, 0.0002, true);
        e = clock.consumeTransportEdges();
        expect(e.started && !e.stopped);
        clock.publishFromAudioThread(0.5, 0.0002, true);
        e = clock.consumeTransportEdges();
        expect(!e.started && !e.stopped);

        beginTest("second Stop produces another edge");
        clock.publishFromAudioThread(0.5, 0.0002, false);
        e = clock.consumeTransportEdges();
        expect(!e.started && e.stopped);
    }
};

static AutomationTransportEdgeTests automationTransportEdgeTests;

/**
    A plugin recorder must use the actual audio-engine PPQ, not 44.1 kHz/120
    BPM regardless of the user's project. Its throttle must be expressed in
    SAMPLES using the active device sample rate.
*/
class PluginAutomationRealTimebaseTests final : public juce::UnitTest
{
public:
    PluginAutomationRealTimebaseTests()
        : juce::UnitTest("automation.plugin-timebase-and-write-spacing.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        DAW::LastTouchedPluginParameter target;
        target.trackId = "automation_timebase_probe_48k";
        target.pluginSlotIndex = 0;
        target.pluginInstanceId = "unit_test_plugin";
        target.pluginDisplayName = "Unit Test VST3";
        target.parameterId = "gain";
        target.parameterName = "Gain";
        target.normalizedValue = 0.33f;

        beginTest("15ms write spacing uses actual 48kHz device rate");
        DAW::PluginAutomationRecorderCore recorder;
        recorder.markWritten(target, 48000);
        expect(!recorder.shouldWritePoint(target, 48600, 48000.0),
               "600 samples is only 12.5ms at 48k, so an unchanged value is throttled");
        expect(recorder.shouldWritePoint(target, 48720, 48000.0),
               "720 samples at 48k is exactly 15ms");
        auto changed = target;
        changed.normalizedValue = 0.8f;
        expect(recorder.shouldWritePoint(changed, 48600, 48000.0),
               "a meaningful value change must not be discarded");

        beginTest("plugin lane mirrors audio engine PPQ, never fixed 44.1kHz/120BPM");
        auto& clock = AutomationClock::getInstance();
        const auto oldClock = clock.snapshot();
        auto& arms = AutomationTransportState::getInstance();
        const bool oldArmed = arms.isRecordArmed();
        const auto oldMode = DAW::PluginAutomationRecorderCore::getGlobalMode();

        auto& keys = AutomationParameterKeyRegistry::getInstance();
        const auto key = AutomationParameterKeyRegistry::pluginParamKey(
            target.trackId, target.pluginSlotIndex, target.pluginDisplayName, target.parameterId);
        const auto id = keys.getOrCreateID(key);
        auto& store = AutomationLaneStore::getInstance();
        store.removeLane(id);

        DAW::AutomationManagerCore manager;
        DAW::PluginAutomationGestureCore gestures;
        recorder.setSubsystems(&manager, &gestures, [] { return true; }, [] { return int64_t(48000); });
        recorder.setSampleRateProvider([] { return 48000.0; });
        arms.setRecordArmed(true);
        DAW::PluginAutomationRecorderCore::setGlobalMode(DAW::AutomationWriteMode::Write);
        clock.publishFromAudioThread(7.25, 1.0 / 48000.0, true);
        recorder.writeFinalPoint(target, true);

        auto lane = store.findLane(id);
        expect(lane != nullptr, "plugin Write must create a mirrored PPQ lane");
        if (lane)
        {
            auto points = lane->getSnapshot();
            expect(points != nullptr && points->size() == 1);
            if (points != nullptr && points->size() == 1)
            {
                expectWithinAbsoluteError((*points)[0].timePPQ, 7.25, 1.0e-9,
                    "the engine's 7.25 PPQ position must be used unchanged");
                expectWithinAbsoluteError((*points)[0].normalizedValue, 0.33f, 1.0e-6f);
            }
        }

        store.removeLane(id);
        clock.publishFromAudioThread(oldClock.blockStartPPQ, oldClock.ppqPerSample,
                                     oldClock.transportRolling);
        arms.setRecordArmed(oldArmed);
        DAW::PluginAutomationRecorderCore::setGlobalMode(oldMode);
    }
};

static PluginAutomationRealTimebaseTests pluginAutomationRealTimebaseTests;

/**
    Regression: Latch and Write must hold the user value on release and
    commit it through the transport Stop playhead. Previously the evaluator
    overwrote the held value from the stale lane, and the recorder committed
    only through the final knob movement (leaving a gap until Stop).
*/
class AutomationLatchWriteStopTests final : public juce::UnitTest
{
public:
    AutomationLatchWriteStopTests()
        : juce::UnitTest("automation.latch-write-stop-hold.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76001;

        for (auto mode : { AutomationMode::Latch, AutomationMode::Write })
        {
            const juce::String modeName = mode == AutomationMode::Latch ? "Latch" : "Write";
            beginTest(modeName + ": hold after release and through transport Stop");

            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arms;
            auto queue = std::make_unique<AutomationGestureQueue>();

            auto* param = registry.createParameter(id, "Automation hold probe", ParameterRange{});
            expect(param != nullptr, "test parameter registered");
            if (param == nullptr)
                continue;

            // The old lane changes during the held interval: without the
            // guard, its evaluator would override the released knob value.
            lanes.getOrCreateLane(id).replacePoints({
                {0.0, 0.2f, CurveType::Linear, 0.0f},
                {3.0, 0.1f, CurveType::Linear, 0.0f},
                {6.0, 0.4f, CurveType::Linear, 0.0f}
            });
            modes.setMode(id, mode);
            arms.setRecordArmed(true);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
            AutomationEvaluator evaluator(registry, lanes, modes);

            clock.publishFromAudioThread(0.0, 0.001, true);
            recorder.drainForTests();  // Consume Play before first gesture.

            auto addEvent = [&](AutomationGestureQueue::EventKind kind,
                                double ppq, float value)
            {
                AutomationGestureQueue::Event event;
                event.paramID = id;
                event.kind = kind;
                event.source = ChangeSource::User;
                event.ppqAtCapture = ppq;
                event.normalizedValue = value;
                return queue->push(event);
            };
            expect(addEvent(AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
            expect(addEvent(AutomationGestureQueue::EventKind::ValueChange, 1.25, 0.8f));
            expect(addEvent(AutomationGestureQueue::EventKind::GestureEnd, 1.5, 0.8f));
            recorder.drainForTests();

            expect(modes.latchHeldRT(id), "release holds Latch/Write until Stop");
            param->writeValue(0.8f, ChangeSource::User);
            evaluator.evaluateBlock(2.0, true);
            expectWithinAbsoluteError(param->getNormalizedValue(), 0.8f, 1.0e-5f,
                "the old curve cannot overwrite the released user value");

            // Less than the prior 30-PPQ sustain interval: Stop must still
            // write the final held breakpoint at the true stop playhead.
            clock.publishFromAudioThread(4.0, 0.001, false);
            recorder.drainForTests();
            expect(!modes.latchHeldRT(id), "Stop releases held mode");

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "recorded lane must remain available");
            if (lane == nullptr)
                continue;
            auto points = lane->getSnapshot();
            expect(points != nullptr && !points->empty(), "recording must contain points");
            if (points == nullptr || points->empty())
                continue;

            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 3.5),
                                      0.8f, 1.0e-5f,
                "held value must persist between final gesture and Stop");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 4.0),
                                      0.8f, 1.0e-5f,
                "Stop must close the held range at the actual playhead");
            evaluator.evaluateBlock(5.0, true);
            expectWithinAbsoluteError(param->getNormalizedValue(),
                                      AutomationLane::evaluateAt(*points, 5.0),
                                      1.0e-5f,
                "after Stop, playback must again follow the committed lane");
        }
    }
};

static AutomationLatchWriteStopTests automationLatchWriteStopTests;

/**
    Regression: Touch returns to the untouched source curve at gesture end.
    Live commits must not change the baseline used for the return value.
    Release must be included in the overwrite interval so intermediate old
    breakpoints cannot leak through the newly recorded gesture.
*/
class AutomationTouchReturnCurveTests final : public juce::UnitTest
{
public:
    AutomationTouchReturnCurveTests()
        : juce::UnitTest("automation.touch-original-curve-return.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76002;

        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Touch return probe", ParameterRange{});
        expect(param != nullptr, "parameter must be registered");
        if (param == nullptr)
            return;

        beginTest("Touch return is evaluated from original curve at release PPQ");
        // Original lane: 1.0 -> .2, 2.0 -> .3, 3.0 -> .5, 4.0 -> .7.
        // Returning to touch-start .2 instead of release .5 is a regression.
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Touch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);

        clock.publishFromAudioThread(0.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.25, 0.8f));
        recorder.drainForTests(); // Live-commit touched points before release.

        auto touched = lanes.findLane(id);
        expect(touched != nullptr, "Touch live-commits while held");
        if (touched == nullptr)
            return;
        auto intermediate = touched->getSnapshot();
        expect(intermediate != nullptr && !intermediate->empty());
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 3.0, 0.8f));
        recorder.drainForTests();

        auto snap = touched->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "Touch ends with a recorded lane");
        if (snap == nullptr || snap->empty())
            return;

        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 0.5),
                                  0.15f, 1.0e-5f,
                                  "automation before the touch remains unchanged");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.75),
                                  0.8f, 1.0e-5f,
                                  "the last user value holds until release");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                  0.5f, 1.0e-5f,
                                  "release rejoins source curve at PPQ 3.0, not touch start");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.5),
                                  0.6f, 1.0e-5f,
                                  "future portion rejoins original automation curve");

        AutomationEvaluator evaluator(registry, lanes, modes);
        param->writeValue(0.8f, ChangeSource::User);
        evaluator.evaluateBlock(3.5, true);
        expectWithinAbsoluteError(param->getNormalizedValue(), 0.6f, 1.0e-5f,
                                  "Touch release resumes live playback");
        expect(!modes.latchHeldRT(id), "Touch does not accidentally latch");
    }
};

static AutomationTouchReturnCurveTests automationTouchReturnCurveTests;

/**
    Trim must apply knob movement as a delta over the original curve,
    preserving ramps and their knots rather than writing absolute knob values.
*/
class AutomationTrimRelativeTests final : public juce::UnitTest
{
public:
    AutomationTrimRelativeTests()
        : juce::UnitTest("automation.trim-relative-preserve-curve.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76003;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Trim delta probe", ParameterRange{});
        expect(param != nullptr, "parameter registered");
        if (param == nullptr) return;

        // Reference curve: PPQ 1=.2, 2=.3, 3=.5, 4=.7, 5=.8.
        // Moving the parameter from .2 to .4 should produce +.2 relative
        // to source at every PPQ, not a flat .4 automation section.
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        param->writeValue(0.2f, ChangeSource::Automation);
        modes.setMode(id, AutomationMode::Trim);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(0.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        beginTest("Trim adds a signed offset to the original moving curve");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.25, 0.4f));
        recorder.drainForTests(); // Live commit must not destroy source knots.
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 5.0, 0.4f));
        recorder.drainForTests();

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "Trim lane available");
        if (lane == nullptr) return;
        const auto points = lane->getSnapshot();
        expect(points != nullptr && !points->empty(), "Trim points written");
        if (points == nullptr || points->empty()) return;

        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 0.5),
                                  0.15f, 1.0e-5f,
                                  "before Trim source curve is unmodified");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 2.0),
                                  0.5f, 1.0e-5f,
                                  "Trim at first original knot is source .3 + delta .2");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 3.0),
                                  0.7f, 1.0e-5f,
                                  "Trim follows the original ramp instead of writing flat");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 4.0),
                                  0.9f, 1.0e-5f,
                                  "Trim preserves second original knot");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 5.0),
                                  0.8f, 1.0e-5f,
                                  "Trim release rejoins unmodified source");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 5.5),
                                  0.85f, 1.0e-5f,
                                  "future original automation remains intact");
        expect(!modes.latchHeldRT(id), "Trim is not a Latch hold");
    }
};

static AutomationTrimRelativeTests automationTrimRelativeTests;

/**
    Backward Playhead movement while transport stays rolling is a loop/seek
    boundary, not a valid monotonic automation interval. One recorded session
    must never bridge two musical positions from different loop passes.
*/
class AutomationLoopRewindTests final : public juce::UnitTest
{
public:
    AutomationLoopRewindTests()
        : juce::UnitTest("automation.loop-rewind-session-boundary.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76004;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Loop fence probe", ParameterRange{});
        expect(param != nullptr, "parameter must register");
        if (param == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {6.0, 0.7f, CurveType::Linear, 0.0f},
            {8.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Latch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(4.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind, double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        beginTest("a Latch release is held within its own loop pass");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 5.0, 0.6f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 5.25, 0.8f));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 5.5, 0.8f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "Latch held in first pass");

        clock.publishFromAudioThread(6.0, 0.001, true);
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "normal forward playback keeps Latch");

        // Event captured before the wrap, but still pending when the message
        // thread sees the new lap. Without loop identity it is ambiguous.
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 6.5, 0.95f));
        beginTest("backward rolling seek fences old take, flushes ambiguous queue");
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();
        expect(!modes.latchHeldRT(id),
               "Latch release must not leak into a different loop pass");

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "recorded lane exists");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "first pass was committed");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 5.25),
                                  0.8f, 1.0e-5f,
                                  "first pass's recorded value remains");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.0),
                                  0.2f, 1.0e-5f,
                                  "a loop jump does not overwrite the earlier position");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 6.5),
                                  0.75f, 1.0e-5f,
                                  "ambiguous queued value is not written to another lap");

        beginTest("fresh gesture after wrap records independently");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.25, 0.2f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.5, 0.35f));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 1.75, 0.35f));
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "second-pass lane persisted");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.5),
                                  0.35f, 1.0e-5f,
                                  "new loop-pass touch is recorded");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 5.25),
                                  0.8f, 1.0e-5f,
                                  "new pass does not corrupt previous captured range");
    }
};

static AutomationLoopRewindTests automationLoopRewindTests;

/**
    A Punch-Out is a record-arm falling edge while transport keeps rolling.
    It must finalize the currently recorded value at the punch boundary,
    clear Latch, and exclude gesture events pending from the old arm epoch.
    Punch-In must start an independent new session without restarting Play.
*/
class AutomationPunchArmBoundaryTests final : public juce::UnitTest
{
public:
    AutomationPunchArmBoundaryTests()
        : juce::UnitTest("automation.punch-record-arm-boundary.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76005;

        for (const auto mode : { AutomationMode::Latch,
                                 AutomationMode::Write,
                                 AutomationMode::Touch })
        {
            const juce::String modeName =
                mode == AutomationMode::Latch ? "Latch"
                : mode == AutomationMode::Write ? "Write" : "Touch";
            beginTest(modeName + ": Punch-Out finalizes old take and drops stale events");

            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arms;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* param = registry.createParameter(id, "Punch boundary probe", ParameterRange{});
            expect(param != nullptr, "fixture parameter registered");
            if (param == nullptr) return;

            lanes.getOrCreateLane(id).replacePoints({
                {0.0, 0.1f, CurveType::Linear, 0.0f},
                {2.0, 0.3f, CurveType::Linear, 0.0f},
                {4.0, 0.7f, CurveType::Linear, 0.0f},
                {6.0, 0.9f, CurveType::Linear, 0.0f}
            });
            modes.setMode(id, mode);
            arms.setRecordArmed(true);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();

            auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                               double ppq, float value)
            {
                AutomationGestureQueue::Event e;
                e.paramID = id;
                e.kind = kind;
                e.source = ChangeSource::User;
                e.ppqAtCapture = ppq;
                e.normalizedValue = value;
                return queue->push(e);
            };
            if (mode != AutomationMode::Write)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.5, 0.2f));
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.0, 0.8f));
            if (mode == AutomationMode::Latch || mode == AutomationMode::Write)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 2.25, 0.8f));
            recorder.drainForTests();

            if (mode != AutomationMode::Touch)
                expect(modes.latchHeldRT(id), "Latch or Write holds after release");

            // Play continues. The queued .95 belongs to the previous arm
            // interval; a falling arm edge must not record it.
            clock.publishFromAudioThread(3.0, 0.001, true);
            arms.setRecordArmed(false);
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.8, 0.95f));
            recorder.drainForTests();
            expect(!modes.latchHeldRT(id), "Punch-Out clears held value");
            expect(!arms.isRecordArmed(), "transport remains disarmed");

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "committed automation lane exists");
            if (lane == nullptr) return;
            auto snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "Punch-Out saved points");
            if (snap == nullptr || snap->empty()) return;

            const float punchValue = mode == AutomationMode::Touch ? 0.5f : 0.8f;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                      punchValue, 1.0e-5f,
                                      "Punch-Out commits correct boundary without stale .95");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                      0.7f, 1.0e-5f,
                                      "original automation following Punch-Out preserved");

            beginTest(modeName + ": Punch-In starts independently without stopping transport");
            arms.setRecordArmed(true);
            recorder.drainForTests();
            if (mode != AutomationMode::Write)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 4.5, 0.7f));
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 4.75, 0.25f));
            if (mode == AutomationMode::Touch)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 5.0, 0.25f));
            recorder.drainForTests();

            snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "second take recorded");
            if (snap == nullptr || snap->empty()) return;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.75),
                                      0.25f, 1.0e-5f,
                                      "second armed interval writes to a fresh session");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                      0.8f, 1.0e-5f,
                                      "second take cannot corrupt earlier recorded interval");
        }
    }
};

static AutomationPunchArmBoundaryTests automationPunchArmBoundaryTests;

/**
    A capture-order regression can happen within one recorder timer period,
    so the rolling clock itself never observes the backward hop. The gesture
    event boundary must fence the session independently of timer snapshots.
*/
class AutomationGesturePpqOrderTests final : public juce::UnitTest
{
public:
    AutomationGesturePpqOrderTests()
        : juce::UnitTest("automation.gesture-ppq-order-guard.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76006;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Order probe", ParameterRange{});
        expect(param != nullptr, "fixture parameter");
        if (param == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {6.0, 0.7f, CurveType::Linear, 0.0f},
            {8.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Latch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(4.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float normalized)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = normalized;
            return queue->push(e);
        };

        beginTest("Latch cannot cross a backward timestamp within one timer tick");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 5.0, 0.6f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 5.25, 0.8f));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 5.4, 0.8f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "Latch held before regressive event");

        // No backwards movement in clock snapshots; only the queued capture
        // timestamp regresses. This used to shrink s.lastPPQ to 1.0 and
        // overwrite with a reversed 5.0->1.0 session range.
        clock.publishFromAudioThread(6.0, 0.001, true);
        recorder.drainForTests();
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.0, 0.95f));
        recorder.drainForTests();
        expect(!modes.latchHeldRT(id),
               "regressive timestamp closes and releases previous session");

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "lane must survive rejected event");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "curve points retained");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.0),
                                  0.2f, 1.0e-5f,
                                  "an old loop position cannot be overwritten");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 5.25),
                                  0.8f, 1.0e-5f,
                                  "the valid previous pass remains committed");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 7.0),
                                  0.8f, 1.0e-5f,
                                  "future original automation survives");

        beginTest("non-finite capture timestamp is ignored");
        const auto beforeSize = snap->size();
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin,
                       std::numeric_limits<double>::quiet_NaN(), 0.2f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                       std::numeric_limits<double>::infinity(), 0.9f));
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && snap->size() == beforeSize,
               "invalid PPQ cannot introduce NaN/Inf into the lane");
    }
};

static AutomationGesturePpqOrderTests automationGesturePpqOrderTests;

/**
    A real Stop can happen while the user is still touching a control.
    Touch and Trim must rejoin the original curve at Stop even if JUCE never
    emitted GestureEnd first.
*/
class AutomationStopImplicitReleaseTests final : public juce::UnitTest
{
public:
    AutomationStopImplicitReleaseTests()
        : juce::UnitTest("automation.stop-implicit-gesture-release.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        for (const auto mode : { AutomationMode::Touch, AutomationMode::Trim })
        {
            beginTest(mode == AutomationMode::Touch
                          ? "Touch Stop restores underlying curve"
                          : "Trim Stop restores underlying curve");
            constexpr ParameterID id = 76007;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arms;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* param = registry.createParameter(id, "Stop release probe", ParameterRange{});
            expect(param != nullptr, "registered parameter");
            if (param == nullptr) return;

            lanes.getOrCreateLane(id).replacePoints({
                {0.0, 0.1f, CurveType::Linear, 0.0f},
                {2.0, 0.3f, CurveType::Linear, 0.0f},
                {4.0, 0.7f, CurveType::Linear, 0.0f},
                {6.0, 0.9f, CurveType::Linear, 0.0f}
            });
            param->writeValue(0.2f, ChangeSource::Automation);
            modes.setMode(id, mode);
            arms.setRecordArmed(true);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();

            auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                               double ppq, float normalized)
            {
                AutomationGestureQueue::Event e;
                e.paramID = id;
                e.kind = kind;
                e.source = ChangeSource::User;
                e.ppqAtCapture = ppq;
                e.normalizedValue = normalized;
                return queue->push(e);
            };

            expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.25,
                           mode == AutomationMode::Trim ? 0.4f : 0.8f));
            recorder.drainForTests();
            // No GestureEnd: the user presses Stop while touching the knob.
            clock.publishFromAudioThread(3.0, 0.001, false);
            recorder.drainForTests();

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "recorded lane available");
            if (lane == nullptr) return;
            const auto snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "curve survives stop");
            if (snap == nullptr || snap->empty()) return;

            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 0.5),
                                      0.15f, 1.0e-5f,
                                      "pre-touch automation is unmodified");
            if (mode == AutomationMode::Touch)
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                          0.8f, 1.0e-5f,
                                          "Touch holds the written value up to Stop");
            else
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                          0.5f, 1.0e-5f,
                                          "Trim preserves base curve plus delta up to Stop");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                      0.5f, 1.0e-5f,
                                      "Stop restores original value at exact Stop PPQ");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.5),
                                      0.6f, 1.0e-5f,
                                      "following curve is preserved, not stuck on user value");
        }
    }
};

static AutomationStopImplicitReleaseTests automationStopImplicitReleaseTests;

/**
    MPSC producers may overflow the bounded gesture queue. If GestureEnd is
    lost, continuing a Latch session would keep a stale user value forever.
    Fence only the valid captured portion and discard all ambiguous events.
*/
class AutomationQueueOverflowFenceTests final : public juce::UnitTest
{
public:
    AutomationQueueOverflowFenceTests()
        : juce::UnitTest("automation.queue-overflow-session-fence.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76008;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Overflow fence probe", ParameterRange{});
        expect(param != nullptr, "fixture registered");
        if (param == nullptr) return;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Latch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float value, ChangeSource source)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = source;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        beginTest("a valid Latch session exists before queue overflow");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin,
                       2.0, 0.3f, ChangeSource::User));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                       2.25, 0.8f, ChangeSource::User));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd,
                       2.30, 0.8f, ChangeSource::User));
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "Latch is held before overflow");

        beginTest("overflow drops an End, then recorder releases stale Latch");
        for (std::size_t i = 0; i < AutomationGestureQueue::kCapacity; ++i)
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                           2.5, 0.95f, ChangeSource::Programmatic));
        expect(! enqueue(AutomationGestureQueue::EventKind::GestureEnd,
                         2.6, 0.95f, ChangeSource::User),
               "full queue drops incoming GestureEnd");
        expect(queue->getOverflowCount() > 0, "overflow is observable");

        recorder.drainForTests();
        expect(!modes.latchHeldRT(id), "queue loss clears stale Latch");
        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "lane still exists after overflow");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "recorded curve survives");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "last valid captured value is committed");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                  0.7f, 1.0e-5f,
                                  "overflow cannot overwrite future original curve");

        beginTest("new gestures can record after queue has drained");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin,
                       3.0, 0.5f, ChangeSource::User));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                       3.25, 0.6f, ChangeSource::User));
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "new session records");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.25),
                                  0.6f, 1.0e-5f,
                                  "recording resumes after overflow boundary");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "new session cannot corrupt previous take");
    }
};

static AutomationQueueOverflowFenceTests automationQueueOverflowFenceTests;

/**
    Regression: UI notifications for native parameters are delayed. A user can
    begin a Trim gesture at .2, move to .4, and *then* the dispatcher delivers
    GestureBegin plus ValueChange in one tick. Trim must use the begin-time
    value .2, not the already-updated live parameter .4.
*/
class AutomationTrimCapturedGestureTests final : public juce::UnitTest
{
public:
    AutomationTrimCapturedGestureTests()
        : juce::UnitTest("automation.trim-begin-value-capture.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76009;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Trim delayed UI probe", ParameterRange{});
        expect(param != nullptr, "parameter must register");
        if (param == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Trim);
        arms.setRecordArmed(true);
        param->writeValue(0.2f, ChangeSource::Automation);

        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*param);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        beginTest("gesture begin captures old value before deferred UI dispatch");
        param->beginGesture();
        expectWithinAbsoluteError(param->getValueAtGestureBegin(), 0.2f,
                                  1.0e-5f, "pre-drag normalized value captured");
        // A newer native PPQ-capture path records the source time, not the
        // callback time. Advance the source clock before the actual move.
        clock.publishFromAudioThread(1.25, 0.001, true);
        param->setValueFromUser(0.4f);
        expectWithinAbsoluteError(param->getNormalizedValue(), 0.4f,
                                  1.0e-5f, "user change already visible");

        // Both callbacks arrive later, at PPQ 2.0, after the actual
        // GestureBegin at 1.0 and ValueChange at 1.25.
        clock.publishFromAudioThread(2.0, 0.001, true);
        param->dispatchPendingNotifications(0.4f, ChangeSource::User);
        recorder.drainForTests();

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "Trim lane exists");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "Trim event recorded");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.25),
                                  0.425f, 1.0e-5f,
                                  "source .225 plus captured gesture delta +.2");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 0.5),
                                  0.15f, 1.0e-5f,
                                  "source automation before gesture preserved");

        beginTest("the delayed gesture can still end and rejoin original curve");
        clock.publishFromAudioThread(3.0, 0.001, true);
        param->endGesture();
        clock.publishFromAudioThread(4.0, 0.001, true);
        param->dispatchPendingNotifications(0.4f, ChangeSource::User);
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "lane retained after release");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                  0.5f, 1.0e-5f,
                                  "original source knot shifted by actual delta");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                  0.5f, 1.0e-5f,
                                  "Trim release returns to source curve");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.5),
                                  0.6f, 1.0e-5f,
                                  "automation after release unchanged");
    }
};

static AutomationTrimCapturedGestureTests automationTrimCapturedGestureTests;

/**
    Native parameter listeners receive UI dispatcher notifications later.
    The timestamp must be captured by the original control operation, not
    rounded up to dispatcher time, for begin, value and gesture end.
*/
class AutomationNativeCapturePPQTests final : public juce::UnitTest
{
public:
    AutomationNativeCapturePPQTests()
        : juce::UnitTest("automation.native-gesture-timestamp-capture.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76010;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Delayed capture clock probe", ParameterRange{});
        expect(param != nullptr, "native parameter registered");
        if (param == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        param->writeValue(0.2f, ChangeSource::Automation);
        modes.setMode(id, AutomationMode::Trim);
        arms.setRecordArmed(true);

        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*param);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        beginTest("native begin and value timestamps survive 60 Hz notification delay");
        param->beginGesture(); // Actual PPQ 1.0, normalized .2
        clock.publishFromAudioThread(1.25, 0.001, true);
        param->setValueFromUser(0.4f); // Actual PPQ 1.25
        // Deliberately defer both notifications to a later PPQ.
        clock.publishFromAudioThread(3.0, 0.001, true);
        param->dispatchPendingNotifications(0.4f, ChangeSource::User);
        expectWithinAbsoluteError(param->getCapturedGestureBeginPPQ(), 1.0,
                                  1.0e-8, "begin PPQ is not notification PPQ");
        expectWithinAbsoluteError(param->getCapturedValueChangePPQ(), 1.25,
                                  1.0e-8, "value PPQ is not notification PPQ");
        recorder.drainForTests();

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "Trim recording created the lane");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "captured points published");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.25),
                                  0.425f, 1.0e-5f,
                                  "source at true movement time +.2");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 0.5),
                                  0.15f, 1.0e-5f,
                                  "original section before touch survives");

        beginTest("native GestureEnd PPQ does not shift to delayed notification time");
        clock.publishFromAudioThread(4.0, 0.001, true);
        param->endGesture(); // Actual PPQ 4.0
        clock.publishFromAudioThread(5.0, 0.001, true);
        param->dispatchPendingNotifications(0.4f, ChangeSource::User);
        expectWithinAbsoluteError(param->getCapturedGestureEndPPQ(), 4.0,
                                  1.0e-8, "release PPQ is not notification PPQ");
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "release points committed");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                  0.5f, 1.0e-5f,
                                  "underlying knot remains offset until real release");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                  0.7f, 1.0e-5f,
                                  "rejoins source curve at real end PPQ 4");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.5),
                                  0.75f, 1.0e-5f,
                                  "automation after actual release is untouched");

        bridge.detachFromParameter(id);
    }
};

static AutomationNativeCapturePPQTests automationNativeCapturePPQTests;

/**
    A Punch-Out immediately followed by Punch-In can happen entirely between
    two 90 Hz message-thread recorder ticks. Merely comparing recordArmed's
    before/after boolean misses this ABA transition and merges two takes.
*/
class AutomationArmEpochABATests final : public juce::UnitTest
{
public:
    AutomationArmEpochABATests()
        : juce::UnitTest("automation.punch-arm-epoch-aba.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76011;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Arm epoch probe", ParameterRange{});
        expect(param != nullptr, "fixture registered");
        if (param == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Latch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        beginTest("first pass holds Latch before record-arm ABA");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 2.0, 0.3f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.25, 0.8f));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 2.3, 0.8f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "first Latch must be held");

        beginTest("On-Off-On between timer callbacks closes first session");
        const auto epoch = arms.getArmTransitionCount();
        arms.setRecordArmed(false);
        arms.setRecordArmed(true);
        expect(arms.isRecordArmed(), "final state is armed");
        expectEquals(arms.getArmTransitionCount(), epoch + std::uint64_t{2},
                     "both transitions must be visible despite final On");

        // A late value from the first arm epoch cannot be distinguished
        // from a newly armed value solely by its captured PPQ.
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.75, 0.95f));
        clock.publishFromAudioThread(3.0, 0.001, true);
        recorder.drainForTests();
        expect(!modes.latchHeldRT(id),
               "old held Latch must release even when final armed state is On");
        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "lane retained");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "first take committed");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "first pass value retained");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                  0.8f, 1.0e-5f,
                                  "first pass extended to the observed punch boundary");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                  0.7f, 1.0e-5f,
                                  "queued stale .95 never damages future source curve");

        beginTest("a valid fresh Latch in same rolling Play interval works");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 4.5, 0.7f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 4.75, 0.25f));
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "new session published");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.75),
                                  0.25f, 1.0e-5f,
                                  "new interval recorded after ABA boundary");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "earlier take not overwritten");

        beginTest("redundant arm writes do not reset a valid session");
        const auto after = arms.getArmTransitionCount();
        arms.setRecordArmed(true);
        expectEquals(arms.getArmTransitionCount(), after,
                     "setting the same armed value is not a transition");
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "lane remains intact");
        if (snap != nullptr && !snap->empty())
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.75),
                                      0.25f, 1.0e-5f,
                                      "idempotent arm state does not erase current take");
    }
};

static AutomationArmEpochABATests automationArmEpochABATests;

/**
    The audio clock publishes every transport edge. Stop followed by Play
    inside one recorder timer interval must not look like continuous Play,
    even if the new playhead position is strictly increasing.
*/
class AutomationHiddenTransportTurnaroundTests final : public juce::UnitTest
{
public:
    AutomationHiddenTransportTurnaroundTests()
        : juce::UnitTest("automation.hidden-stop-start-boundary.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76012;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* param = registry.createParameter(id, "Hidden Stop-Play probe", ParameterRange{});
        expect(param != nullptr, "registered fixture");
        if (param == nullptr) return;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Latch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                           double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            return queue->push(e);
        };

        beginTest("the first take is held before transport turnaround");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 2.0, 0.3f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.25, 0.8f));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 2.3, 0.8f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(id), "Latch held in first transport interval");
        const auto before = clock.snapshot().transportTransitions;

        beginTest("Stop then Play entirely between ticks cannot continue old take");
        // The final playhead advances, unlike a loop rewind. The old
        // bool-only detector would see Play both times and no PPQ reversal.
        clock.publishFromAudioThread(2.5, 0.001, false);
        clock.publishFromAudioThread(3.0, 0.001, true);
        expectEquals(clock.snapshot().transportTransitions,
                     before + std::uint64_t{2},
                     "clock must record both edges");
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 2.8, 0.95f));
        recorder.drainForTests();
        expect(!modes.latchHeldRT(id),
               "old Latch released despite final transport remaining rolling");

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "lane preserved");
        if (lane == nullptr) return;
        auto snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "first take committed");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "valid recorded movement retained");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                  0.7f, 1.0e-5f,
                                  "late value from prior Play interval dropped");

        beginTest("new Play interval can independently record new gesture");
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 4.5, 0.7f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 4.75, 0.25f));
        recorder.drainForTests();
        snap = lane->getSnapshot();
        expect(snap != nullptr && !snap->empty(), "second take exists");
        if (snap == nullptr || snap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.75),
                                  0.25f, 1.0e-5f,
                                  "fresh play interval records correctly");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.25),
                                  0.8f, 1.0e-5f,
                                  "fresh play does not overwrite first take");
    }
};

static AutomationHiddenTransportTurnaroundTests automationHiddenTransportTurnaroundTests;

/**
    Hosted AudioProcessor parameter notifications can arrive on an audio
    thread while the message thread attaches, detaches or remaps a plugin.
    A mutable unordered_map lookup in that callback is a C++ data race and
    can invalidate the map while a reader is inside find().
*/
class AutomationPluginBridgeSnapshotTests final : public juce::UnitTest
{
public:
    AutomationPluginBridgeSnapshotTests()
        : juce::UnitTest("automation.hosted-plugin-bridge-snapshot.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID firstID = 76013, secondID = 76014;
        AutomationParameterRegistry registry;
        AutomationClock clock;
        auto queue = std::make_unique<AutomationGestureQueue>();
        juce::AudioProcessorGraph processor;
        AutomationGestureBridge bridge(registry, *queue, clock);
        clock.publishFromAudioThread(1.0, 0.001, true);

        beginTest("hosted plugin mapping publishes before callback registration");
        bridge.attachToPlugin(processor, {{3, firstID}});
        bridge.audioProcessorParameterChanged(&processor, 3, 0.3f);
        AutomationGestureQueue::Event event;
        expect(queue->pop(event), "registered plugin maps its callback");
        expectEquals(static_cast<int>(event.paramID),
                     static_cast<int>(firstID), "initial parameter identity");
        bridge.audioProcessorParameterChanged(&processor, 4, 0.3f);
        expect(!queue->pop(event), "unknown index does not create an event");

        beginTest("concurrent callbacks see only complete immutable bindings");
        std::atomic<bool> start { false };
        std::atomic<int> callbackCount { 0 };
        std::thread callbackThread([&]
        {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < 10000; ++i)
            {
                bridge.audioProcessorParameterChanged(&processor, 3, 0.5f);
                callbackCount.fetch_add(1, std::memory_order_relaxed);
            }
        });

        start.store(true, std::memory_order_release);
        for (int i = 0; i < 192; ++i)
        {
            if ((i % 7) == 0)
                bridge.detachFromPlugin(processor);
            else
                bridge.attachToPlugin(processor,
                    {{3, (i % 2) == 0 ? firstID : secondID}});
        }

        callbackThread.join();
        expect(callbackCount.load(std::memory_order_relaxed) == 10000,
               "every producer callback returned without data races");
        int consumed = 0;
        bool badID = false;
        while (queue->pop(event))
        {
            ++consumed;
            if (event.paramID != firstID && event.paramID != secondID)
                badID = true;
        }
        expect(!badID, "callback sees only complete published mappings");
        expect(consumed > 0, "at least one mapping produced events");

        beginTest("fully detached plugin callback is safely ignored");
        bridge.detachFromPlugin(processor);
        bridge.audioProcessorParameterChanged(&processor, 3, 0.7f);
        expect(!queue->pop(event), "detached plugin has no published mapping");
        bridge.attachToPlugin(processor, {{3, secondID}});
        bridge.audioProcessorParameterChanged(&processor, 3, 0.8f);
        expect(queue->pop(event), "reattached plugin can record again");
        expectEquals(static_cast<int>(event.paramID),
                     static_cast<int>(secondID), "remapped identity is current");
        bridge.detachFromAllPlugins();
        bridge.audioProcessorParameterChanged(&processor, 3, 0.9f);
        expect(!queue->pop(event), "detachAll withdraws all mappings");
    }
};

static AutomationPluginBridgeSnapshotTests automationPluginBridgeSnapshotTests;

/**
    Changing a parameter from Touch/Trim/Latch into Read while Play remains
    active must terminate the previous recording session, including when no
    further GestureEnd / ValueChange arrives. Queued events from an unknown
    mode epoch must never cause a stale session to be reopened accidentally.
*/
class AutomationModeSwitchFenceTests final : public juce::UnitTest
{
public:
    AutomationModeSwitchFenceTests()
        : juce::UnitTest("automation.mode-switch-session-fence.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        for (const auto mode : { AutomationMode::Touch,
                                 AutomationMode::Trim,
                                 AutomationMode::Latch,
                                 AutomationMode::Write })
        {
            const juce::String modeName =
                mode == AutomationMode::Touch ? "Touch" :
                mode == AutomationMode::Trim ? "Trim" :
                mode == AutomationMode::Latch ? "Latch" : "Write";
            beginTest(modeName + " -> Read during rolling Play");

            constexpr ParameterID id = 76015;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arms;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* param = registry.createParameter(id, "Mode switch probe", ParameterRange{});
            expect(param != nullptr, "fixture parameter exists");
            if (param == nullptr) return;
            lanes.getOrCreateLane(id).replacePoints({
                {0.0, 0.1f, CurveType::Linear, 0.0f},
                {2.0, 0.3f, CurveType::Linear, 0.0f},
                {4.0, 0.7f, CurveType::Linear, 0.0f},
                {6.0, 0.9f, CurveType::Linear, 0.0f}
            });

            param->writeValue(0.2f, ChangeSource::Automation);
            modes.setMode(id, mode);
            arms.setRecordArmed(true);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();

            auto enqueue = [&](AutomationGestureQueue::EventKind kind,
                               double ppq, float value)
            {
                AutomationGestureQueue::Event e;
                e.paramID = id;
                e.kind = kind;
                e.source = ChangeSource::User;
                e.ppqAtCapture = ppq;
                e.normalizedValue = value;
                if (kind == AutomationGestureQueue::EventKind::GestureBegin)
                    e.hasStartValue = true;
                return queue->push(e);
            };

            if (mode != AutomationMode::Write)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin,
                               1.0, 0.2f));
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                           1.25, mode == AutomationMode::Trim ? 0.4f : 0.8f));
            if (mode == AutomationMode::Latch || mode == AutomationMode::Write)
                expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd,
                               1.5, 0.8f));
            recorder.drainForTests();

            if (mode == AutomationMode::Latch || mode == AutomationMode::Write)
                expect(modes.latchHeldRT(id), "writing mode retains last value");

            clock.publishFromAudioThread(3.0, 0.001, true);
            modes.setMode(id, AutomationMode::Read);
            // These events cannot be attributed to either mode without a
            // producer-side mode epoch. Discard them at the mode boundary.
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                           2.75, 0.95f));
            recorder.drainForTests();
            expect(!modes.latchHeldRT(id), "switch to Read clears retained value");

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "lane exists");
            if (lane == nullptr) return;
            auto snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "lane committed");
            if (snap == nullptr || snap->empty()) return;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 0.5),
                                      0.15f, 1.0e-5f,
                                      "pre-gesture source curve preserved");
            const float writtenAtTwo = mode == AutomationMode::Trim ? 0.5f : 0.8f;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                      writtenAtTwo, 1.0e-5f,
                                      "active gesture preserved through mode boundary");
            const float writtenAtBoundary =
                (mode == AutomationMode::Touch || mode == AutomationMode::Trim)
                    ? 0.5f : 0.8f;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                      writtenAtBoundary, 1.0e-5f,
                                      "old session ended at observed mode switch PPQ");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.0),
                                      0.7f, 1.0e-5f,
                                      "future source automation remains untouched");

            beginTest(modeName + " -> Read: stale events cannot write further");
            expect(enqueue(AutomationGestureQueue::EventKind::ValueChange,
                           4.5, 0.99f));
            recorder.drainForTests();
            snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "Read keeps existing lane");
            if (snap == nullptr || snap->empty()) return;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 4.5),
                                      0.75f, 1.0e-5f,
                                      "Read mode does not append stale automation");
        }
    }
};

static AutomationModeSwitchFenceTests automationModeSwitchFenceTests;

/**
    When two parameters are automated simultaneously, switching mode on one
    parameter must not erase pending gestures for the other parameter.
    Global queue drain on any mode change silently loses unrelated takes.
*/
class AutomationModeSwitchIsolationTests final : public juce::UnitTest
{
public:
    AutomationModeSwitchIsolationTests()
        : juce::UnitTest("automation.mode-switch-per-param-isolation.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID latchID = 76016, touchID = 76017;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* latchParam = registry.createParameter(
            latchID, "Mode switch Latch", ParameterRange{});
        auto* touchParam = registry.createParameter(
            touchID, "Unrelated Touch", ParameterRange{});
        expect(latchParam != nullptr && touchParam != nullptr,
               "two independent parameters registered");
        if (latchParam == nullptr || touchParam == nullptr) return;

        const auto original = std::vector<Breakpoint>{
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        };
        lanes.getOrCreateLane(latchID).replacePoints(original);
        lanes.getOrCreateLane(touchID).replacePoints(original);
        modes.setMode(latchID, AutomationMode::Latch);
        modes.setMode(touchID, AutomationMode::Touch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        auto push = [&](ParameterID id, AutomationGestureQueue::EventKind kind,
                        double ppq, float value)
        {
            AutomationGestureQueue::Event event;
            event.paramID = id;
            event.kind = kind;
            event.source = ChangeSource::User;
            event.ppqAtCapture = ppq;
            event.normalizedValue = value;
            return queue->push(event);
        };

        beginTest("two live sessions share the recorder");
        expect(push(latchID, AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
        expect(push(latchID, AutomationGestureQueue::EventKind::ValueChange, 1.25, 0.8f));
        expect(push(latchID, AutomationGestureQueue::EventKind::GestureEnd, 1.5, 0.8f));
        expect(push(touchID, AutomationGestureQueue::EventKind::GestureBegin, 1.5, 0.2f));
        expect(push(touchID, AutomationGestureQueue::EventKind::ValueChange, 2.0, 0.9f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(latchID), "Latch session held");

        beginTest("Latch -> Read must not discard Touch events queued this tick");
        clock.publishFromAudioThread(3.0, 0.001, true);
        modes.setMode(latchID, AutomationMode::Read);
        expect(push(latchID, AutomationGestureQueue::EventKind::ValueChange,
                    2.75, 0.95f)); // Stale event from switched mode.
        expect(push(touchID, AutomationGestureQueue::EventKind::ValueChange,
                    2.75, 0.45f)); // Must still record.
        recorder.drainForTests();

        expect(!modes.latchHeldRT(latchID), "Latch released by mode switch");
        auto left = lanes.findLane(latchID);
        auto right = lanes.findLane(touchID);
        expect(left != nullptr && right != nullptr, "both lanes persist");
        if (left == nullptr || right == nullptr) return;
        auto leftSnap = left->getSnapshot(), rightSnap = right->getSnapshot();
        expect(leftSnap != nullptr && rightSnap != nullptr, "both curves published");
        if (leftSnap == nullptr || rightSnap == nullptr) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*leftSnap, 4.0),
                                  0.7f, 1.0e-5f,
                                  "stale Latch event cannot overwrite future source");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*rightSnap, 2.75),
                                  0.45f, 1.0e-5f,
                                  "unrelated Touch value survives the mode boundary");

        beginTest("unrelated Touch session can finish normally afterwards");
        expect(push(touchID, AutomationGestureQueue::EventKind::GestureEnd,
                    3.5, 0.45f));
        recorder.drainForTests();
        rightSnap = right->getSnapshot();
        expect(rightSnap != nullptr && !rightSnap->empty(), "Touch curve committed");
        if (rightSnap == nullptr || rightSnap->empty()) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*rightSnap, 2.75),
                                  0.45f, 1.0e-5f,
                                  "Touch point is not erased by later release");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*rightSnap, 3.5),
                                  0.6f, 1.0e-5f,
                                  "Touch returns to its own original curve");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*rightSnap, 4.5),
                                  0.75f, 1.0e-5f,
                                  "Touch release preserves the future curve");
    }
};

static AutomationModeSwitchIsolationTests automationModeSwitchIsolationTests;

/**
    Mode enums alone have the same ABA bug as record-arm and transport.
    Latch->Read->Latch inside one 90Hz recorder period must fence the old
    session, and the new fence must not discard another parameter's events.
*/
class AutomationModeEpochABATests final : public juce::UnitTest
{
public:
    AutomationModeEpochABATests()
        : juce::UnitTest("automation.mode-epoch-aba-and-isolation.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID latchID = 76018, touchID = 76019;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* latchParam = registry.createParameter(
            latchID, "ABA Latch", ParameterRange{});
        auto* touchParam = registry.createParameter(
            touchID, "ABA independent Touch", ParameterRange{});
        expect(latchParam != nullptr && touchParam != nullptr, "registered parameters");
        if (latchParam == nullptr || touchParam == nullptr) return;

        const auto original = std::vector<Breakpoint>{
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        };
        lanes.getOrCreateLane(latchID).replacePoints(original);
        lanes.getOrCreateLane(touchID).replacePoints(original);
        modes.setMode(latchID, AutomationMode::Latch);
        modes.setMode(touchID, AutomationMode::Touch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();

        const auto enqueue = [&] (ParameterID id,
                                  AutomationGestureQueue::EventKind kind,
                                  double ppq, float value)
        {
            AutomationGestureQueue::Event event;
            event.paramID = id;
            event.kind = kind;
            event.source = ChangeSource::User;
            event.ppqAtCapture = ppq;
            event.normalizedValue = value;
            return queue->push(event);
        };

        beginTest("Latch and Touch can have independent open sessions");
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.2f));
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::ValueChange, 1.25, 0.8f));
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::GestureEnd, 1.5, 0.8f));
        expect(enqueue(touchID, AutomationGestureQueue::EventKind::GestureBegin, 1.5, 0.2f));
        expect(enqueue(touchID, AutomationGestureQueue::EventKind::ValueChange, 2.0, 0.9f));
        recorder.drainForTests();
        expect(modes.latchHeldRT(latchID), "Latch held before ABA toggle");

        beginTest("Latch -> Read -> Latch between timer polls fences old take");
        const auto initialEpoch = modes.getModeEpoch(latchID);
        const auto touchEpoch = modes.getModeEpoch(touchID);
        modes.setMode(latchID, AutomationMode::Read);
        modes.setMode(latchID, AutomationMode::Latch);
        expectEquals(static_cast<int>(modes.getMode(latchID)),
                     static_cast<int>(AutomationMode::Latch), "final enum unchanged");
        expect(modes.getModeEpoch(latchID) != initialEpoch,
               "per-parameter transition epoch detects both edges");
        expect(!(modes.getModeEpoch(touchID) != touchEpoch),
               "unrelated Touch mode epoch unchanged");
        const auto epochAfter = modes.getModeEpoch(latchID);
        modes.setMode(latchID, AutomationMode::Latch);
        expect(!(modes.getModeEpoch(latchID) != epochAfter),
               "idempotent mode set is not a new transition");

        clock.publishFromAudioThread(3.0, 0.001, true);
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::ValueChange, 2.75, 0.95f));
        expect(enqueue(touchID, AutomationGestureQueue::EventKind::ValueChange, 2.75, 0.45f));
        recorder.drainForTests();
        expect(!modes.latchHeldRT(latchID),
               "old held Latch released even when final mode is unchanged");

        auto first = lanes.findLane(latchID);
        auto second = lanes.findLane(touchID);
        expect(first != nullptr && second != nullptr, "both lanes survive ABA");
        if (first == nullptr || second == nullptr) return;
        auto firstSnap = first->getSnapshot(), secondSnap = second->getSnapshot();
        expect(firstSnap != nullptr && secondSnap != nullptr, "snapshots published");
        if (firstSnap == nullptr || secondSnap == nullptr) return;
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*firstSnap, 2.25),
                                  0.8f, 1.0e-5f, "old take was preserved");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*firstSnap, 4.0),
                                  0.7f, 1.0e-5f, "stale Latch value not recorded");
        expectWithinAbsoluteError(AutomationLane::evaluateAt(*secondSnap, 2.75),
                                  0.45f, 1.0e-5f, "unrelated Touch event was recorded");

        beginTest("new Latch gesture works after ABA boundary");
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::GestureBegin, 4.5, 0.7f));
        expect(enqueue(latchID, AutomationGestureQueue::EventKind::ValueChange, 4.75, 0.25f));
        recorder.drainForTests();
        firstSnap = first->getSnapshot();
        expect(firstSnap != nullptr && !firstSnap->empty(), "fresh take committed");
        if (firstSnap != nullptr && !firstSnap->empty())
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*firstSnap, 4.75),
                                      0.25f, 1.0e-5f, "new take is independent");

        beginTest("override and global-default ABA are also observable");
        const auto beforeOverride = modes.getModeEpoch(latchID);
        modes.clearOverride(latchID);
        modes.setMode(latchID, AutomationMode::Latch);
        expect(modes.getModeEpoch(latchID) != beforeOverride,
               "removing and re-adding override advances parameter epoch");
        constexpr ParameterID inheritedID = 76020;
        const auto beforeDefault = modes.getModeEpoch(inheritedID);
        modes.setGlobalDefaultMode(AutomationMode::Off);
        modes.setGlobalDefaultMode(AutomationMode::Read);
        expect(modes.getModeEpoch(inheritedID) != beforeDefault,
               "inherited mode records hidden default-mode toggle");
        expect(modes.getModeEpoch(touchID).globalDefault == 0,
               "explicit Touch override ignores global-default toggles");
    }
};

static AutomationModeEpochABATests automationModeEpochABATests;

/**
    Regression for torn snapshots under concurrent audio publications.
    Both PPQ and per-sample increment encode the same block sequence. The
    message-thread reader must never receive values from different blocks,
    even after colliding with the writer on multiple consecutive retries.
*/
class AutomationClockConsistentSnapshotTests final : public juce::UnitTest
{
public:
    AutomationClockConsistentSnapshotTests()
        : juce::UnitTest("automation.clock-seqlock-consistency.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest("one reader, concurrent audio writer, no mixed block fields");
        AutomationClock clock;
        constexpr int iterations = 150000;
        std::atomic<bool> begin { false };
        std::atomic<bool> done { false };
        std::thread writer([&]
        {
            while (!begin.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (int block = 1; block <= iterations; ++block)
                clock.publishFromAudioThread(static_cast<double>(block),
                                             static_cast<double>(block) * 2.0,
                                             true);
            done.store(true, std::memory_order_release);
        });

        std::uint64_t observations = 0;
        bool sawTornSnapshot = false;
        begin.store(true, std::memory_order_release);
        while (!done.load(std::memory_order_acquire) || observations < iterations)
        {
            const auto observed = clock.snapshot();
            const auto expectedIncrement = observed.blockStartPPQ * 2.0;
            if (observed.ppqPerSample != expectedIncrement
                || observed.transportTransitions > 1)
                sawTornSnapshot = true;
            ++observations;
            if (observations >= static_cast<std::uint64_t>(iterations * 4))
                break;
        }

        writer.join();
        expect(!sawTornSnapshot, "snapshot must never mix fields from different blocks");
        expect(observations >= 1, "at least one snapshot captured");
        const auto finalState = clock.snapshot();
        expectWithinAbsoluteError(finalState.blockStartPPQ,
                                  static_cast<double>(iterations),
                                  1.0e-8, "final PPQ is last published block");
        expectWithinAbsoluteError(finalState.ppqPerSample,
                                  static_cast<double>(iterations) * 2.0,
                                  1.0e-8, "final increment matches the same block");
        expect(finalState.transportRolling, "transport remained rolling");
        expectEquals(finalState.transportTransitions, std::uint64_t{1},
                     "only initial Stop->Play transition happened");
    }
};

static AutomationClockConsistentSnapshotTests automationClockConsistentSnapshotTests;

/**
    Hosted plugin bugs or corrupt project edits can report NaN/Infinity.
    Those values must never reach atomic parameter state, the gesture queue,
    a Trim envelope, or an audio-thread-interpolated automation lane.
*/
class AutomationNonFiniteInputTests final : public juce::UnitTest
{
public:
    AutomationNonFiniteInputTests()
        : juce::UnitTest("automation.reject-nonfinite-input.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float posInf = std::numeric_limits<float>::infinity();
        const double timeNaN = std::numeric_limits<double>::quiet_NaN();
        const double timeInf = std::numeric_limits<double>::infinity();
        constexpr ParameterID id = 76021;

        AutomationParameterRegistry registry;
        auto* parameter = registry.createParameter(
            id, "Nonfinite input", ParameterRange{});
        expect(parameter != nullptr, "parameter registered");
        if (parameter == nullptr) return;
        parameter->writeValue(0.3f, ChangeSource::Automation);
        const auto version = parameter->getVersion();

        beginTest("native input paths never store or forward NaN/Inf");
        parameter->setValueFromUser(nan);
        parameter->setValueFromPlugin(posInf);
        parameter->setValueFromAutomation(nan);
        parameter->setValueProgrammatic(-posInf);
        parameter->writeValue(nan, ChangeSource::Automation);
        expectWithinAbsoluteError(parameter->getNormalizedValue(),
                                  0.3f, 1.0e-6f, "invalid values rejected");
        expectEquals(parameter->getVersion(), version,
                     "invalid values do not publish fake parameter changes");

        beginTest("hosted plugin callbacks reject nonfinite payloads");
        auto queue = std::make_unique<AutomationGestureQueue>();
        AutomationClock clock;
        clock.publishFromAudioThread(1.0, 0.001, true);
        juce::AudioProcessorGraph graph;
        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToPlugin(graph, {{3, id}});
        bridge.audioProcessorParameterChanged(&graph, 3, nan);
        bridge.audioProcessorParameterChanged(&graph, 3, posInf);
        AutomationGestureQueue::Event event;
        expect(!queue->pop(event), "invalid plugin payloads never enter queue");
        bridge.audioProcessorParameterChanged(&graph, 3, 0.65f);
        expect(queue->pop(event), "subsequent finite plugin event is recorded");
        expectWithinAbsoluteError(event.normalizedValue,
                                  0.65f, 1.0e-6f, "valid value unchanged");
        bridge.detachFromAllPlugins();

        beginTest("lane persistence sanitizes invalid knots before sorting");
        AutomationLane lane(id);
        lane.replacePoints({
            {0.0, 0.2f, CurveType::Linear, 0.0f},
            {timeNaN, 0.9f, CurveType::Linear, 0.0f},
            {timeInf, 0.9f, CurveType::Linear, 0.0f},
            {1.0, nan, CurveType::Linear, 0.0f},
            {2.0, posInf, CurveType::Linear, 0.0f},
            {4.0, 1.25f, CurveType::Smooth, nan},
            {6.0, 0.6f, CurveType::Linear, 0.0f}
        });
        auto saved = lane.getSnapshot();
        expect(saved != nullptr && saved->size() == 3,
               "nonfinite timestamp/value knots removed");
        if (saved != nullptr && saved->size() == 3)
        {
            expectWithinAbsoluteError((*saved)[1].normalizedValue,
                                      1.0f, 1.0e-6f,
                                      "out-of-range normalized value clamped");
            expectWithinAbsoluteError((*saved)[1].curveTension,
                                      0.0f, 1.0e-6f,
                                      "nonfinite curve tension reset");
            const auto middle = AutomationLane::evaluateAt(*saved, 3.0);
            expect(std::isfinite(middle), "audio curve interpolation stays finite");
            const auto invalidTime = AutomationLane::evaluateAt(*saved, timeNaN);
            expect(std::isfinite(invalidTime), "nonfinite playhead never returns NaN");
        }

        beginTest("recorder drops malformed gesture changes but preserves valid takes");
        AutomationLaneStore lanes;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        AutomationModeState modes;
        modes.setMode(id, AutomationMode::Touch);
        AutomationTransportState arms;
        arms.setRecordArmed(true);
        auto recordingQueue = std::make_unique<AutomationGestureQueue>();
        AutomationRecorder recorder(registry, lanes, modes, *recordingQueue,
                                    clock, arms);
        recorder.drainForTests();

        auto enqueue = [&] (AutomationGestureQueue::EventKind kind,
                            double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            e.hasStartValue = kind == AutomationGestureQueue::EventKind::GestureBegin;
            return recordingQueue->push(e);
        };
        expect(enqueue(AutomationGestureQueue::EventKind::GestureBegin, 1.0, 0.3f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.5, 0.8f));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.75, nan));
        expect(enqueue(AutomationGestureQueue::EventKind::ValueChange, 1.8, posInf));
        expect(enqueue(AutomationGestureQueue::EventKind::GestureEnd, 2.0, 0.8f));
        recorder.drainForTests();

        auto committed = lanes.findLane(id);
        expect(committed != nullptr, "recording lane exists");
        if (committed == nullptr) return;
        const auto points = committed->getSnapshot();
        expect(points != nullptr && !points->empty(), "valid recorded points retained");
        if (points != nullptr && !points->empty())
        {
            for (const auto& p : *points)
                expect(std::isfinite(p.timePPQ)
                    && std::isfinite(p.normalizedValue), "recorded lane remains finite");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 1.5),
                                      0.8f, 1.0e-5f, "finite gesture recorded");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 3.0),
                                      0.5f, 1.0e-5f, "future source curve unaffected");
        }
    }
};

static AutomationNonFiniteInputTests automationNonFiniteInputTests;

/**
    A UI dispatcher wake-up from GestureBegin/GestureEnd is not a value
    change. Forwarding it as such creates phantom automation points at the
    old ValueChange PPQ, sometimes rewinding an otherwise valid touch.
*/
class AutomationGestureOnlyDispatchTests final : public juce::UnitTest
{
public:
    AutomationGestureOnlyDispatchTests()
        : juce::UnitTest("automation.native-gesture-only-no-phantom-value.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76022;
        AutomationParameterRegistry registry;
        auto* parameter = registry.createParameter(
            id, "Gesture-only probe", ParameterRange{});
        expect(parameter != nullptr, "registered native parameter");
        if (parameter == nullptr) return;

        AutomationClock clock;
        auto queue = std::make_unique<AutomationGestureQueue>();
        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*parameter);
        AutomationGestureQueue::Event event;

        beginTest("pure GestureBegin dispatches one begin, zero value changes");
        clock.publishFromAudioThread(1.0, 0.001, true);
        parameter->beginGesture();
        clock.publishFromAudioThread(1.5, 0.001, true);
        parameter->dispatchPendingNotifications(
            parameter->getNormalizedValue(), parameter->getLastSource());
        expect(queue->pop(event), "gesture begin emitted");
        expectEquals(static_cast<int>(event.kind),
                     static_cast<int>(AutomationGestureQueue::EventKind::GestureBegin),
                     "first event is GestureBegin");
        expectWithinAbsoluteError(event.ppqAtCapture, 1.0, 1.0e-8,
                                  "captured begin timestamp preserved");
        expect(!queue->pop(event), "no synthetic ValueChange on begin-only tick");

        beginTest("real user write dispatches precisely one value at input PPQ");
        clock.publishFromAudioThread(2.0, 0.001, true);
        parameter->setValueFromUser(0.6f);
        clock.publishFromAudioThread(3.0, 0.001, true);
        parameter->dispatchPendingNotifications(
            parameter->getNormalizedValue(), parameter->getLastSource());
        expect(queue->pop(event), "real value change emitted");
        expectEquals(static_cast<int>(event.kind),
                     static_cast<int>(AutomationGestureQueue::EventKind::ValueChange),
                     "value event kind is correct");
        expectWithinAbsoluteError(event.normalizedValue, 0.6f, 1.0e-6f,
                                  "actual moved value preserved");
        expectWithinAbsoluteError(event.ppqAtCapture, 2.0, 1.0e-8,
                                  "value uses original input timestamp");
        expect(!queue->pop(event), "only one value event");

        beginTest("pure GestureEnd dispatches one end, zero value changes");
        clock.publishFromAudioThread(4.0, 0.001, true);
        parameter->endGesture();
        clock.publishFromAudioThread(5.0, 0.001, true);
        parameter->dispatchPendingNotifications(
            parameter->getNormalizedValue(), parameter->getLastSource());
        expect(queue->pop(event), "gesture end emitted");
        expectEquals(static_cast<int>(event.kind),
                     static_cast<int>(AutomationGestureQueue::EventKind::GestureEnd),
                     "event is GestureEnd, not a repeated ValueChange");
        expectWithinAbsoluteError(event.ppqAtCapture, 4.0, 1.0e-8,
                                  "captured end PPQ preserved");
        expect(!queue->pop(event), "end-only tick does not replay stale value");

        beginTest("programmatic and automation writes still notify UI");
        parameter->setValueProgrammatic(0.25f);
        parameter->dispatchPendingNotifications(
            parameter->getNormalizedValue(), parameter->getLastSource());
        // Programmatic changes still invoke ordinary value listeners; the
        // bridge intentionally excludes them from recorder events.
        expect(!queue->pop(event), "programmatic value is not user automation");
        parameter->setValueFromAutomation(0.35f);
        parameter->dispatchPendingNotifications(
            parameter->getNormalizedValue(), parameter->getLastSource());
        expect(!queue->pop(event), "playback automation is not user recording");

        bridge.detachFromParameter(id);
    }
};

static AutomationGestureOnlyDispatchTests automationGestureOnlyDispatchTests;

/**
    UI dispatcher notifications run at ~60Hz. Multiple user knob movements
    between ticks MUST be recorded individually, and a delayed dispatcher
    cannot re-enqueue any of those events or create a phantom movement.
*/
class AutomationNativeDirectProducerTests final : public juce::UnitTest
{
public:
    AutomationNativeDirectProducerTests()
        : juce::UnitTest("automation.native-producer-capture-every-move.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76023;
        AutomationParameterRegistry registry;
        auto* param = registry.createParameter(
            id, "Rapid Native Fader", ParameterRange{});
        expect(param != nullptr, "native parameter exists");
        if (param == nullptr) return;
        param->writeValue(0.2f, ChangeSource::Automation);

        AutomationClock clock;
        auto queue = std::make_unique<AutomationGestureQueue>();
        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*param);

        beginTest("all five events are captured before UI dispatcher wakes");
        clock.publishFromAudioThread(1.0, 0.001, true);
        param->beginGesture();
        clock.publishFromAudioThread(1.25, 0.001, true);
        param->setValueFromUser(0.8f);
        clock.publishFromAudioThread(1.5, 0.001, true);
        param->setValueFromUser(0.2f);
        clock.publishFromAudioThread(1.75, 0.001, true);
        param->setValueFromUser(0.9f);
        clock.publishFromAudioThread(2.0, 0.001, true);
        param->endGesture();

        // The 60Hz dispatcher did not run during any input above.
        const std::array<AutomationGestureQueue::EventKind, 5> kinds = {
            AutomationGestureQueue::EventKind::GestureBegin,
            AutomationGestureQueue::EventKind::ValueChange,
            AutomationGestureQueue::EventKind::ValueChange,
            AutomationGestureQueue::EventKind::ValueChange,
            AutomationGestureQueue::EventKind::GestureEnd
        };
        const std::array<double, 5> times = {1.0, 1.25, 1.5, 1.75, 2.0};
        const std::array<float, 5> values = {0.2f, 0.8f, 0.2f, 0.9f, 0.9f};
        AutomationGestureQueue::Event e;
        for (std::size_t i = 0; i < kinds.size(); ++i)
        {
            expect(queue->pop(e), "native producer enqueued its own event");
            expectEquals(static_cast<int>(e.kind), static_cast<int>(kinds[i]),
                         "kind matches producer ordering");
            expectWithinAbsoluteError(e.ppqAtCapture, times[i], 1.0e-8,
                                      "each event carries its own capture PPQ");
            expectWithinAbsoluteError(e.normalizedValue, values[i], 1.0e-5f,
                                      "each intermediate value survived");
        }
        expect(!queue->pop(e), "exactly five events recorded");

        beginTest("later coalescing widget dispatch never duplicates recording");
        clock.publishFromAudioThread(3.0, 0.001, true);
        param->dispatchPendingNotifications(
            param->getNormalizedValue(), param->getLastSource());
        expect(!queue->pop(e), "no delayed duplicates or phantom events");

        beginTest("a fresh rapid gesture preserves non-linear intermediate knots");
        AutomationLaneStore lanes;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        AutomationModeState modes;
        modes.setMode(id, AutomationMode::Touch);
        AutomationTransportState arms;
        arms.setRecordArmed(true);
        auto recordingQueue = std::make_unique<AutomationGestureQueue>();
        bridge.detachFromParameter(id);
        AutomationGestureBridge recordingBridge(registry, *recordingQueue, clock);
        recordingBridge.attachToParameter(*param);
        AutomationRecorder recorder(registry, lanes, modes, *recordingQueue,
                                    clock, arms);

        param->writeValue(0.2f, ChangeSource::Automation);
        clock.publishFromAudioThread(1.0, 0.001, true);
        recorder.drainForTests();
        param->beginGesture();
        clock.publishFromAudioThread(1.25, 0.001, true);
        param->setValueFromUser(0.8f);
        clock.publishFromAudioThread(1.5, 0.001, true);
        param->setValueFromUser(0.2f);
        clock.publishFromAudioThread(1.75, 0.001, true);
        param->setValueFromUser(0.9f);
        clock.publishFromAudioThread(2.0, 0.001, true);
        param->endGesture();
        // No 60 Hz dispatch; recorder consumes the direct producer stream.
        recorder.drainForTests();

        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "recording lane still exists");
        if (lane != nullptr)
        {
            auto snap = lane->getSnapshot();
            expect(snap != nullptr && !snap->empty(), "rapid moves committed");
            if (snap != nullptr && !snap->empty())
            {
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.25),
                                          0.8f, 1.0e-5f, "first fast movement retained");
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.5),
                                          0.2f, 1.0e-5f, "second fast movement retained");
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 1.75),
                                          0.9f, 1.0e-5f, "third fast movement retained");
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 2.0),
                                          0.3f, 1.0e-5f, "Touch release rejoins source");
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snap, 3.0),
                                          0.5f, 1.0e-5f, "future source unaffected");
            }
        }
        recordingBridge.detachFromParameter(id);
    }
};

static AutomationNativeDirectProducerTests automationNativeDirectProducerTests;

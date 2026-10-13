#include <JuceHeader.h>

#include "../../../Source/Automation/AutomationParameterRegistryCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationModeStateCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Source/Automation/AutomationEvaluatorCore.h"
#include "../../../Source/Automation/AutomationUIDispatcherCore.h"
#include "../../../Source/Automation/AutomationGestureQueueCore.h"
#include "../../../Source/Automation/AutomationClockCore.h"
#include "../../../Source/Automation/AutomationRecorderCore.h"
#include "../../../Source/Automation/AutomationGestureBridgeCore.h"
#include "../../../Source/Automation/AutomationSystemCore.h"
#include "../../../Source/Automation/AutomationTrackVolumePlaybackCore.h"
#include "../../../Source/Automation/AutomationTrackPanPlaybackCore.h"
#include "../../../Source/Automation/AutomationTrackMutePlaybackCore.h"
#include "../../../Source/Automation/AutomationTrackSoloPlaybackCore.h"
#include "../../../Source/AutomationCore/PluginAutomationRecorderCore.h"
#include "../../../Source/AutomationCore/AutomationLaneCurveClipboardCore.h"
#include "../../../Source/AutomationCore/AutomationMusicalLfoCore.h"
#include "../../../Source/AutomationCore/AutomationFreehandStrokeCore.h"
#include "../../../Source/AutomationCore/AutomationCurveTransformCore.h"
#include "../../../Source/AutomationCore/AutomationCurveMorphCore.h"
#include "../../../Source/AutomationCore/AutomationGroupedCurveCore.h"
#include "../../../Source/AutomationCore/AutomationMultiParameterSnapshotCore.h"
#include "../../../Source/AutomationCore/AutomationMacroMappingCore.h"
#include "../../../Source/AutomationCore/AutomationMacroVst3TargetCore.h"
#include "../../../Source/AutomationCore/AutomationLaneStateValidationCore.h"
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

        beginTest ("New Project drops stale keys but preserves Master identity");
        {
            AutomationParameterKeyRegistry reg;
            const auto masterVolume = reg.getOrCreateID(
                AutomationParameterKeyRegistry::trackVolumeKey("master"));
            const auto masterSolo = reg.getOrCreateID(
                AutomationParameterKeyRegistry::trackSoloKey("master"));
            const auto oldTrack = reg.getOrCreateID(
                AutomationParameterKeyRegistry::trackMuteKey("TRK_OLDER"));
            const auto oldPlugin = reg.getOrCreateID(
                "plugin.TRK_OLDER.slot0.Example.cutoff");
            const auto oldSend = reg.getOrCreateID(
                AutomationParameterKeyRegistry::trackSendLevelKey("TRK_OLDER", "route"));
            const juce::String oldTrackKey =
                AutomationParameterKeyRegistry::trackMuteKey("TRK_OLDER");
            const auto previous = reg.getKeySnapshotRT();
            const auto generation = reg.getChangeGeneration();

            reg.resetForNewProject();
            expect(reg.getChangeGeneration() > generation);
            expectEquals(reg.findIDRT(
                AutomationParameterKeyRegistry::trackVolumeKey("master")), masterVolume);
            expectEquals(reg.findIDRT(
                AutomationParameterKeyRegistry::trackSoloKey("master")), masterSolo);
            expectEquals(reg.findIDRT(oldTrackKey), kInvalidParameterID);
            expect(reg.findKey(oldPlugin).isEmpty());
            expect(reg.findKey(oldSend).isEmpty());
            expect(previous != nullptr && previous->count(oldTrackKey) == 1);
            const auto newTrack = reg.getOrCreateID(
                AutomationParameterKeyRegistry::trackMuteKey("TRK_NEW"));
            expect(newTrack > oldSend);
            expect(newTrack != oldTrack);
            // Saving Untitled must not serialize aliases from the old song.
            const auto state = reg.getState();
            expectEquals(state.getNumChildren(), 3); // Master volume + solo + new track
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

/**
    Touching a Smooth automation segment cannot be spliced by inserting only
    one knot just before the gesture. Smoothstep interpolation renormalizes
    its time against the new, shortened segment length and bends the entire
    preceding curve even though the producer never edited that earlier time.
*/
class AutomationSmoothPreTouchPreservationTests final : public juce::UnitTest
{
public:
    AutomationSmoothPreTouchPreservationTests()
        : juce::UnitTest("automation.smooth-pre-touch-preservation.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        for (float tension : {0.0f, 0.6f, -0.6f})
        {
            beginTest("Smooth source segment remains intact before touch, tension "
                      + juce::String(tension));
            constexpr ParameterID id = 76024;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arms;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* p = registry.createParameter(id, "Smooth pre-touch", ParameterRange{});
            expect(p != nullptr, "test parameter registered");
            if (p == nullptr) return;
            AutomationLane::PointVector original = {
                {0.0, 0.1f, CurveType::Smooth, tension},
                {2.0, 0.9f, CurveType::Linear, 0.0f},
                {4.0, 0.3f, CurveType::Linear, 0.0f}
            };
            lanes.getOrCreateLane(id).replacePoints(original);
            modes.setMode(id, AutomationMode::Touch);
            arms.setRecordArmed(true);
            p->writeValue(0.2f, ChangeSource::Automation);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);
            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();

            auto push = [&] (AutomationGestureQueue::EventKind kind,
                             double ppq, float value)
            {
                AutomationGestureQueue::Event event;
                event.paramID = id;
                event.kind = kind;
                event.source = ChangeSource::User;
                event.normalizedValue = value;
                event.ppqAtCapture = ppq;
                event.hasStartValue =
                    kind == AutomationGestureQueue::EventKind::GestureBegin;
                return queue->push(event);
            };

            expect(push(AutomationGestureQueue::EventKind::GestureBegin, 1.5, 0.2f));
            expect(push(AutomationGestureQueue::EventKind::ValueChange, 1.5, 0.95f));
            recorder.drainForTests();

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "recorded lane exists");
            if (lane == nullptr) return;
            auto checkEarlier = [&](const char* stage)
            {
                const auto snapshot = lane->getSnapshot();
                expect(snapshot != nullptr, "snapshot exists");
                if (snapshot == nullptr) return;
                for (double at : {0.125, 0.25, 0.5, 0.75, 1.0, 1.125, 1.25, 1.375})
                {
                    const float expected = AutomationLane::evaluateAt(original, at);
                    const float actual = AutomationLane::evaluateAt(*snapshot, at);
                    expectWithinAbsoluteError(actual, expected, 4.0e-5f,
                        juce::String(stage) + ": unedited Smooth automation at PPQ "
                            + juce::String(at));
                }
            };

            checkEarlier("first same-timestamp touch");
            auto snapshot = lane->getSnapshot();
            expect(snapshot != nullptr && !snapshot->empty(), "first change committed");
            if (snapshot != nullptr && !snapshot->empty())
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snapshot, 1.5),
                                          0.95f, 1.0e-5f,
                                          "touched value wins at start PPQ");

            expect(push(AutomationGestureQueue::EventKind::ValueChange, 1.75, 0.5f));
            recorder.drainForTests();
            checkEarlier("second live commit");
            snapshot = lane->getSnapshot();
            expect(snapshot != nullptr && !snapshot->empty(), "second change committed");
            if (snapshot != nullptr && !snapshot->empty())
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snapshot, 1.75),
                                          0.5f, 1.0e-5f,
                                          "subsequent automation is still recorded");

            expect(push(AutomationGestureQueue::EventKind::GestureEnd, 2.5, 0.5f));
            recorder.drainForTests();
            checkEarlier("after releasing control");
            snapshot = lane->getSnapshot();
            expect(snapshot != nullptr && !snapshot->empty(), "post-release source persists");
            if (snapshot != nullptr && !snapshot->empty())
                expectWithinAbsoluteError(AutomationLane::evaluateAt(*snapshot, 3.0),
                                          AutomationLane::evaluateAt(original, 3.0),
                                          1.0e-5f, "unchanged later linear source persists");
        }
    }
};

static AutomationSmoothPreTouchPreservationTests
    automationSmoothPreTouchPreservationTests;

/**
    A Play edge must quarantine pending gestures from the stopped epoch.
    The bridge stamps PPQ on events, but not the transport epoch; if the
    recorder drains after Play resumes, previously queued stopped-time
    gestures must NOT start a new take. A real post-boundary gesture still
    records normally on the next recorder tick.
*/
class AutomationPlayEdgeQueueFenceTests final : public juce::UnitTest
{
public:
    AutomationPlayEdgeQueueFenceTests()
        : juce::UnitTest("automation.play-edge-queue-fence.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 76025;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState arms;
        auto queue = std::make_unique<AutomationGestureQueue>();
        auto* p = registry.createParameter(id, "Play boundary", ParameterRange{});
        expect(p != nullptr, "parameter registered");
        if (p == nullptr) return;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        });
        modes.setMode(id, AutomationMode::Touch);
        arms.setRecordArmed(true);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arms);

        auto push = [&] (AutomationGestureQueue::EventKind kind,
                         double ppq, float value)
        {
            AutomationGestureQueue::Event e;
            e.paramID = id;
            e.kind = kind;
            e.source = ChangeSource::User;
            e.ppqAtCapture = ppq;
            e.normalizedValue = value;
            e.hasStartValue = kind == AutomationGestureQueue::EventKind::GestureBegin;
            return queue->push(e);
        };
        auto lane = lanes.findLane(id);
        expect(lane != nullptr, "lane exists");
        if (lane == nullptr) return;

        beginTest("stopped-epoch backlog rejected on first Play edge");
        clock.publishFromAudioThread(0.5, 0.001, false);
        recorder.drainForTests();
        expect(push(AutomationGestureQueue::EventKind::GestureBegin, 0.75, 0.2f));
        expect(push(AutomationGestureQueue::EventKind::ValueChange, 1.0, 0.95f));
        expect(push(AutomationGestureQueue::EventKind::GestureEnd, 1.0, 0.95f));
        clock.publishFromAudioThread(1.25, 0.001, true);
        recorder.drainForTests();
        auto snapshot = lane->getSnapshot();
        expect(snapshot != nullptr && snapshot->size() == 4,
               "pre-Play knob movements never create recorded breakpoints");
        if (snapshot != nullptr)
            expectWithinAbsoluteError(
                AutomationLane::evaluateAt(*snapshot, 1.0), 0.2f, 1.0e-5f,
                "stale movements did not overwrite source curve");

        beginTest("fresh gesture recorded after first Play boundary");
        expect(push(AutomationGestureQueue::EventKind::GestureBegin, 1.5, 0.2f));
        expect(push(AutomationGestureQueue::EventKind::ValueChange, 1.75, 0.85f));
        expect(push(AutomationGestureQueue::EventKind::GestureEnd, 2.0, 0.85f));
        recorder.drainForTests();
        snapshot = lane->getSnapshot();
        expect(snapshot != nullptr && !snapshot->empty(), "fresh take persisted");
        if (snapshot != nullptr && !snapshot->empty())
        {
            expectWithinAbsoluteError(
                AutomationLane::evaluateAt(*snapshot, 1.75), 0.85f, 1.0e-5f,
                "new gesture recorded at correct timestamp");
            expectWithinAbsoluteError(
                AutomationLane::evaluateAt(*snapshot, 3.0), 0.5f, 1.0e-5f,
                "future source curve preserved after release");
        }

        beginTest("Stop then Play after a poll also rejects stopped-time events");
        clock.publishFromAudioThread(3.0, 0.001, false);
        recorder.drainForTests();
        expect(push(AutomationGestureQueue::EventKind::GestureBegin, 3.25, 0.5f));
        expect(push(AutomationGestureQueue::EventKind::ValueChange, 3.5, 0.98f));
        clock.publishFromAudioThread(4.0, 0.001, true);
        recorder.drainForTests();
        snapshot = lane->getSnapshot();
        expect(snapshot != nullptr && !snapshot->empty(), "recorded lane retained");
        if (snapshot != nullptr && !snapshot->empty())
        {
            expectWithinAbsoluteError(
                AutomationLane::evaluateAt(*snapshot, 3.5), 0.6f, 1.0e-5f,
                "stale stopped-time event could not overwrite PPQ 3.5");
            expectWithinAbsoluteError(
                AutomationLane::evaluateAt(*snapshot, 4.5), 0.75f, 1.0e-5f,
                "future source remains unchanged");
        }
    }
};
static AutomationPlayEdgeQueueFenceTests automationPlayEdgeQueueFenceTests;

/**
    Cross-component integration gate: producer-side native gestures ->
    lock-free MPSC -> 90Hz recorder -> immutable lane snapshot ->
    audio-thread AutomationEvaluator -> normalized parameter state.
    A family of individually passing unit tests cannot prove the full path
    is connected or that Stop releases Latch before the next playback pass.
*/
class AutomationFullStackRoundTripTests final : public juce::UnitTest
{
public:
    AutomationFullStackRoundTripTests()
        : juce::UnitTest("automation.full-stack-record-playback-roundtrip.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        const AutomationLane::PointVector original = {
            {0.0, 0.1f, CurveType::Linear, 0.0f},
            {2.0, 0.3f, CurveType::Linear, 0.0f},
            {4.0, 0.7f, CurveType::Linear, 0.0f},
            {6.0, 0.9f, CurveType::Linear, 0.0f}
        };

        beginTest("native Touch: multi-move record -> live lane -> audio evaluator");
        {
            constexpr ParameterID id = 76026;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arm;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* parameter = registry.createParameter(
                id, "Roundtrip Touch", ParameterRange{});
            expect(parameter != nullptr, "parameter registered");
            if (parameter == nullptr) return;
            parameter->writeValue(0.2f, ChangeSource::Automation);
            lanes.getOrCreateLane(id).replacePoints(original);
            modes.setMode(id, AutomationMode::Touch);
            arm.setRecordArmed(true);
            AutomationGestureBridge bridge(registry, *queue, clock);
            bridge.attachToParameter(*parameter);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arm);
            AutomationEvaluator evaluator(registry, lanes, modes);

            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests(); // Observe arm/Play before capturing.
            parameter->beginGesture();
            clock.publishFromAudioThread(1.25, 0.001, true);
            parameter->setValueFromUser(0.8f);
            clock.publishFromAudioThread(1.5, 0.001, true);
            parameter->setValueFromUser(0.2f);
            clock.publishFromAudioThread(1.75, 0.001, true);
            parameter->setValueFromUser(0.9f);
            clock.publishFromAudioThread(2.0, 0.001, true);
            parameter->endGesture();

            // No 60Hz widget notifications. Every movement was captured at
            // the producer; the 90Hz recorder must receive all of them.
            recorder.drainForTests();
            expect(!parameter->isGestureActive(), "Touch released");
            const auto lane = lanes.findLane(id);
            expect(lane != nullptr, "lane persisted");
            if (lane == nullptr) return;
            const auto recorded = lane->getSnapshot();
            expect(recorded != nullptr && !recorded->empty(), "recording created knots");
            if (recorded == nullptr || recorded->empty()) return;

            const std::array<double, 7> times = {
                0.5, 1.25, 1.5, 1.75, 2.0, 3.0, 4.5
            };
            const std::array<float, 7> expected = {
                0.15f, 0.8f, 0.2f, 0.9f, 0.3f, 0.5f, 0.75f
            };
            for (std::size_t i = 0; i < times.size(); ++i)
            {
                const auto fromLane = AutomationLane::evaluateAt(
                    *recorded, times[i]);
                expectWithinAbsoluteError(fromLane, expected[i], 1.0e-5f,
                    "lane point at PPQ " + juce::String(times[i]));

                evaluator.evaluateBlock(times[i], true);
                expectWithinAbsoluteError(parameter->getNormalizedValue(),
                    expected[i], 1.0e-5f,
                    "audio-thread evaluator roundtrip at PPQ "
                        + juce::String(times[i]));
            }
            bridge.detachFromParameter(id);
        }

        beginTest("Latch: audio evaluator respects held gesture, Stop commits sustain");
        {
            constexpr ParameterID id = 76027;
            AutomationParameterRegistry registry;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationClock clock;
            AutomationTransportState arm;
            auto queue = std::make_unique<AutomationGestureQueue>();
            auto* parameter = registry.createParameter(
                id, "Roundtrip Latch", ParameterRange{});
            expect(parameter != nullptr, "parameter registered");
            if (parameter == nullptr) return;
            parameter->writeValue(0.2f, ChangeSource::Automation);
            lanes.getOrCreateLane(id).replacePoints(original);
            modes.setMode(id, AutomationMode::Latch);
            arm.setRecordArmed(true);
            AutomationGestureBridge bridge(registry, *queue, clock);
            bridge.attachToParameter(*parameter);
            AutomationRecorder recorder(registry, lanes, modes, *queue, clock, arm);
            AutomationEvaluator evaluator(registry, lanes, modes);

            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();
            parameter->beginGesture();
            clock.publishFromAudioThread(1.25, 0.001, true);
            parameter->setValueFromUser(0.8f);
            clock.publishFromAudioThread(1.5, 0.001, true);
            parameter->endGesture();
            recorder.drainForTests();
            expect(modes.latchHeldRT(id), "Latch held after released gesture");

            evaluator.evaluateBlock(2.5, true);
            expectWithinAbsoluteError(parameter->getNormalizedValue(), 0.8f,
                1.0e-5f, "playback cannot fight the held user value");

            clock.publishFromAudioThread(3.0, 0.001, false);
            recorder.drainForTests();
            expect(!modes.latchHeldRT(id), "Stop released held Latch");
            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "Latch lane exists");
            if (lane == nullptr) return;
            auto saved = lane->getSnapshot();
            expect(saved != nullptr && !saved->empty(), "Stop committed Latch");
            if (saved == nullptr || saved->empty()) return;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*saved, 2.75),
                0.8f, 1.0e-5f, "last Latch value retained through Stop");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*saved, 3.0),
                0.8f, 1.0e-5f, "Stop boundary is final sustained value");

            clock.publishFromAudioThread(3.25, 0.001, true);
            recorder.drainForTests();
            evaluator.evaluateBlock(4.0, true);
            expectWithinAbsoluteError(parameter->getNormalizedValue(), 0.7f,
                1.0e-5f, "new playback pass reads committed lane, not old hold");
            bridge.detachFromParameter(id);
        }
    }
};

static AutomationFullStackRoundTripTests automationFullStackRoundTripTests;

/**
    AudioProcessorListener callbacks and native producer operations can run
    on realtime threads. They need PPQ only, and must never enter the full
    clock snapshot's unbounded seqlock retry if the audio writer stalls in
    its odd publication phase. The single-atomic capture path remains valid
    and monotonic even while another thread publishes blocks.
*/
class AutomationClockRealtimeCaptureTests final : public juce::UnitTest
{
public:
    AutomationClockRealtimeCaptureTests()
        : juce::UnitTest("automation.rt-clock-single-atomic-capture.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID nativeID = 76028;
        constexpr ParameterID pluginID = 76029;

        AutomationParameterRegistry registry;
        auto* native = registry.createParameter(
            nativeID, "Realtime capture", ParameterRange{});
        expect(native != nullptr, "native control exists");
        if (native == nullptr) return;
        AutomationClock clock;
        clock.publishFromAudioThread(1.0, 0.001, true);
        auto queue = std::make_unique<AutomationGestureQueue>();
        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*native);
        juce::AudioProcessorGraph plugin;
        bridge.attachToPlugin(plugin, {{7, pluginID}});

        beginTest("concurrent clock publication never stalls PPQ-only producers");
        std::atomic<bool> ready { false };
        std::thread writer ([&]
        {
            while (!ready.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int block = 2; block <= 40000; ++block)
                clock.publishFromAudioThread(double(block), 0.001, true);
        });
        ready.store(true, std::memory_order_release);

        double previousPPQ = 1.0;
        bool monotonic = true;
        bool finite = true;
        for (int n = 0; n < 30000; ++n)
        {
            const double now = clock.captureInputPPQ();
            finite = finite && std::isfinite(now);
            monotonic = monotonic && now >= previousPPQ;
            previousPPQ = now;
            if (n == 500 || n == 1000 || n == 1500)
            {
                clock.captureInputPPQ();
                bridge.audioProcessorParameterChanged(
                    &plugin, 7, 0.1f * float(n / 500));
            }
        }
        writer.join();
        expect(finite, "single-field PPQ capture remains finite");
        expect(monotonic, "monotonic publisher never goes backwards");
        expectWithinAbsoluteError(clock.captureInputPPQ(), 40000.0, 1.0e-8,
                                  "latest published audio block is readable");

        beginTest("plugin callbacks retain PPQ in bounded queue");
        AutomationGestureQueue::Event event;
        for (int i = 0; i < 3; ++i)
        {
            expect(queue->pop(event), "plugin callback reached queue");
            expectEquals(event.paramID, pluginID, "plugin mapping preserved");
            expectEquals(static_cast<int>(event.kind),
                static_cast<int>(AutomationGestureQueue::EventKind::ValueChange),
                "plugin value event kind");
            expect(std::isfinite(event.ppqAtCapture)
                && event.ppqAtCapture >= 1.0
                && event.ppqAtCapture <= 40000.0,
                "plugin callback timestamp is a published PPQ");
        }
        expect(!queue->pop(event), "no duplicate plugin messages");

        beginTest("native control operations use same producer fast-path");
        clock.publishFromAudioThread(50000.0, 0.001, true);
        native->beginGesture();
        clock.publishFromAudioThread(50000.25, 0.001, true);
        native->setValueFromUser(0.6f);
        clock.publishFromAudioThread(50000.5, 0.001, true);
        native->endGesture();
        for (double ppq : {50000.0, 50000.25, 50000.5})
        {
            expect(queue->pop(event), "native producer captured gesture event");
            expectEquals(event.paramID, nativeID, "native ID preserved");
            expectWithinAbsoluteError(event.ppqAtCapture, ppq, 1.0e-8,
                                      "native PPQ captured at input operation");
        }
        expect(!queue->pop(event), "no additional native events");
        bridge.detachFromAllParameters();
        bridge.detachFromAllPlugins();
    }
};
static AutomationClockRealtimeCaptureTests automationClockRealtimeCaptureTests;

/**
    The 60Hz dispatcher invokes listeners that may remove or replace native
    controls during a project change. Iterating the registry's mutable map
    through forEach() while holding its lock invalidates the current iterator
    when a callback unregisters itself. A pinned immutable snapshot must
    finish its tick safely, and a new parameter reusing an ID/version must
    still get its first value notification.
*/
class AutomationDispatcherReentrantRegistryTests final : public juce::UnitTest
{
public:
    AutomationDispatcherReentrantRegistryTests()
        : juce::UnitTest("automation.ui-dispatcher-reentrant-registry.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        constexpr ParameterID firstID = 76032;
        constexpr ParameterID secondID = 76033;

        struct Listener final : AutomationParameter::Listener
        {
            AutomationParameterRegistry* registry = nullptr;
            ParameterID removeID = kInvalidParameterID;
            int notifications = 0;
            float lastValue = -1.0f;

            void parameterValueChanged (AutomationParameter&,
                                        float value, ChangeSource) override
            {
                ++notifications;
                lastValue = value;
                if (registry != nullptr && removeID != kInvalidParameterID)
                {
                    // Unregister the parameter whose listener is CURRENTLY
                    // being invoked. The old mutable-map iteration could
                    // increment an invalidated unordered_map iterator.
                    auto* r = registry;
                    registry = nullptr;
                    r->unregisterParameter (removeID);
                }
            }
        };

        AutomationParameterRegistry registry;
        auto* first = registry.createParameter(
            firstID, "Self-removing fader", ParameterRange{});
        auto* second = registry.createParameter(
            secondID, "Neighboring fader", ParameterRange{});
        expect(first != nullptr && second != nullptr, "parameters registered");
        if (first == nullptr || second == nullptr) return;

        Listener removing;
        removing.registry = &registry;
        removing.removeID = firstID;
        Listener neighbor;
        first->addListener (&removing);
        second->addListener (&neighbor);
        AutomationUIDispatcher dispatcher(registry);
        dispatcher.pause(); // deterministic manual tick, not an async timer

        beginTest("listener can unregister itself mid-dispatch without invalidation");
        first->setValueFromUser (0.4f);
        second->setValueFromUser (0.65f);
        dispatcher.dispatchOnceForTests();
        expectEquals(removing.notifications, 1,
                     "removal callback delivered exactly once");
        expectEquals(neighbor.notifications, 1,
                     "neighboring parameter delivered in same pinned snapshot");
        expectWithinAbsoluteError(neighbor.lastValue, 0.65f, 1.0e-6f,
                                  "neighbor retained valid value");
        expect(registry.find(firstID) == nullptr,
               "first parameter was unregistered by callback");

        beginTest("reusing same ParameterID and version still sends first event");
        auto* replacement = registry.createParameter(
            firstID, "Replacement fader", ParameterRange{});
        expect(replacement != nullptr && replacement != first,
               "replacement parameter allocated without destroying retired source");
        if (replacement == nullptr) return;
        Listener replacementListener;
        replacement->addListener (&replacementListener);
        // The original and replacement both advance to version 1.
        replacement->setValueFromUser(0.9f);
        expectEquals(replacement->getVersion(), first->getVersion(),
                     "test reproduces same-ID same-version collision");
        dispatcher.dispatchOnceForTests();
        expectEquals(replacementListener.notifications, 1,
                     "replaced parameter receives its initial notification");
        expectWithinAbsoluteError(replacementListener.lastValue, 0.9f,
                                  1.0e-6f, "replacement controls have fresh values");
        expectEquals(removing.notifications, 1,
                     "retired parameter does not receive more callbacks");

        beginTest("later stable ticks do not duplicate value notifications");
        dispatcher.dispatchOnceForTests();
        expectEquals(replacementListener.notifications, 1);
        expectEquals(neighbor.notifications, 1);

        first->removeListener(&removing);
        second->removeListener(&neighbor);
        replacement->removeListener(&replacementListener);
    }
};
static AutomationDispatcherReentrantRegistryTests
    automationDispatcherReentrantRegistryTests;

/**
    MixerPanel::rebuildStrips() destroys and recreates every MixerStrip (e.g.
    reorder, folder collapse, or presentation change). The former
    AutomationSystem::removeNativeParameter erased the PROJECT lane and mode
    whenever the last GUI registration detached. The next strip appeared
    without its recorded fader envelope: a user-visible data-loss bug.

    This pins the same register -> detach -> register lifecycle used by real
    MixerStrip, without requiring a synthetic track delete.
*/
class AutomationMixerStripRebindPersistenceTests final : public juce::UnitTest
{
public:
    AutomationMixerStripRebindPersistenceTests()
        : juce::UnitTest("automation.mixer-strip-rebind-preserves-lanes.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        auto& system = AutomationSystem::getInstance();
        auto& keys = AutomationParameterKeyRegistry::getInstance();
        auto& lanes = system.getLaneStore();
        auto& modes = system.getModeState();

        const auto id = keys.getOrCreateID(
            keys.trackVolumeKey("APEX_CI_STRIP_REBIND"));
        beginTest("attach production native mixer parameter");
        auto* first = system.createNativeParameter(
            id, "CI Mixer Volume", ParameterRange{});
        expect(first != nullptr, "native parameter exists");
        if (first == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.25, 0.12f, CurveType::Linear, 0.0f},
            {1.00, 0.70f, CurveType::Smooth, 0.35f},
            {2.50, 0.40f, CurveType::Hold, 0.0f}
        });
        modes.setMode(id, AutomationMode::Touch);

        beginTest("closing/rebuilding mixer strip never erases project data");
        system.removeNativeParameter(id);
        expect(system.getRegistry().find(id) == nullptr,
               "the UI-facing native parameter actually detached");
        auto savedLane = lanes.findLane(id);
        expect(savedLane != nullptr,
               "persistent project lane survives mixer strip destruction");
        expectEquals(static_cast<int>(modes.getMode(id)),
                     static_cast<int>(AutomationMode::Touch),
                     "Touch mode survives parameter detachment");
        if (savedLane == nullptr) return;

        auto savedPoints = savedLane->getSnapshot();
        expect(savedPoints != nullptr && savedPoints->size() == 3,
               "all breakpoints survive UI rebuild");
        if (savedPoints == nullptr || savedPoints->size() != 3) return;
        expectWithinAbsoluteError((*savedPoints)[1].normalizedValue,
                                  0.70f, 1.0e-6f);
        expectEquals(static_cast<int>((*savedPoints)[1].curveType),
                     static_cast<int>(CurveType::Smooth));

        beginTest("rebuilt strip rebinds to the original lane and mode");
        auto* rebound = system.createNativeParameter(
            id, "CI Mixer Volume Rebound", ParameterRange{});
        expect(rebound != nullptr,
               "recreated native mixer parameter registered");
        expect(lanes.findLane(id) == savedLane,
               "lane identity and recorded points are retained across rebind");
        expectEquals(static_cast<int>(modes.getMode(id)),
                     static_cast<int>(AutomationMode::Touch));

        // Explicit project/track teardown, unlike widget destruction, owns
        // lane/mode deletion. Release test-owned global state deterministically.
        system.removeNativeParameter(id);
        lanes.removeLane(id);
        modes.clearOverride(id);
        expect(lanes.findLane(id) == nullptr,
               "explicit removal deletes lane when requested");
        expectEquals(static_cast<int>(modes.getMode(id)),
                     static_cast<int>(modes.getGlobalDefaultMode()),
                     "explicit mode cleanup returns to the global default");
    }
};
static AutomationMixerStripRebindPersistenceTests
    automationMixerStripRebindPersistenceTests;

/**
    A visible mixer automation button must cycle through every mode and not
    omit Trim. The button now uses the same pure policy as this test.
*/
class AutomationMixerModeCycleTests final : public juce::UnitTest
{
public:
    AutomationMixerModeCycleTests()
        : juce::UnitTest("automation.mixer-mode-cycle-includes-trim.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest("mode button reaches all six unique modes and wraps to Off");
        constexpr std::array<AutomationMode, 6> expected = {
            AutomationMode::Read, AutomationMode::Touch,
            AutomationMode::Latch, AutomationMode::Write,
            AutomationMode::Trim, AutomationMode::Off
        };
        auto current = AutomationMode::Off;
        for (const auto next : expected)
        {
            current = nextMixerAutomationMode(current);
            expectEquals(static_cast<int>(current), static_cast<int>(next),
                         "mixer mode cycle follows documented order");
        }
        expectEquals(static_cast<int>(current),
                     static_cast<int>(AutomationMode::Off),
                     "full cycle returns to the initial OFF state");

        beginTest("restored Touch and Trim modes advance from actual state");
        expectEquals(static_cast<int>(
                         nextMixerAutomationMode(AutomationMode::Touch)),
                     static_cast<int>(AutomationMode::Latch));
        expectEquals(static_cast<int>(
                         nextMixerAutomationMode(AutomationMode::Trim)),
                     static_cast<int>(AutomationMode::Off));
    }
};
static AutomationMixerModeCycleTests automationMixerModeCycleTests;

/**
    The DAW DSP ramps MUST evaluate persisted native fader lanes directly.
    A mixer UI dispatcher updates at ~60 Hz, and offline export may not run
    any UI timer: an AudioEngine that reads only Track::getVolume() misses
    the PPQ automation even while AutomationEvaluator updates its atomics.
*/
class AutomationNativeFaderAudioPlaybackTests final : public juce::UnitTest
{
public:
    AutomationNativeFaderAudioPlaybackTests()
        : juce::UnitTest("automation.native-fader-direct-audio-playback.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        DAW::FaderRangeCore taper;
        constexpr ParameterID id = 77031;
        constexpr float fallbackGain = 0.64f;

        auto* fader = registry.createParameter(id, "Production Fader", {});
        expect(fader != nullptr, "registered native fader");
        if (fader == nullptr) return;
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.20f, CurveType::Linear, 0.0f},
            {2.0, 0.80f, CurveType::Linear, 0.0f}
        });
        const auto play = [&] (double ppq, bool rolling)
        {
            return getNativeTrackVolumeGainRT(id, fallbackGain, ppq,
                                              rolling, false, registry, lanes,
                                              modes, taper);
        };

        beginTest("Read lane feeds audio DSP directly, without UI dispatcher");
        modes.setMode(id, AutomationMode::Read);
        expectWithinAbsoluteError(play(0.0, true),
                                  taper.normToGain(0.20f), 1.0e-5f);
        expectWithinAbsoluteError(play(1.0, true),
                                  taper.normToGain(0.50f), 1.0e-5f);
        expectWithinAbsoluteError(play(2.0, true),
                                  taper.normToGain(0.80f), 1.0e-5f);
        expectWithinAbsoluteError(play(1.0, false), fallbackGain, 1.0e-5f,
                                  "stopped transport does not read automation");

        beginTest("track automation mute bypasses the lane without muting audio");
        expectWithinAbsoluteError(getNativeTrackVolumeGainRT(
            id, fallbackGain, 1.0, true, true,
            registry, lanes, modes, taper), fallbackGain, 1.0e-5f,
            "automation mute returns the regular track volume");
        expectWithinAbsoluteError(play(1.0, true),
                                  taper.normToGain(0.50f), 1.0e-5f,
                                  "clearing automation mute resumes Read immediately");

        beginTest("Off bypasses lane completely");
        modes.setMode(id, AutomationMode::Off);
        expectWithinAbsoluteError(play(1.0, true), fallbackGain, 1.0e-5f);

        beginTest("Touch yields to live gesture value, then resumes envelope");
        modes.setMode(id, AutomationMode::Touch);
        fader->beginGesture();
        fader->setValueFromUser(0.91f);
        expectWithinAbsoluteError(play(1.0, true),
                                  taper.normToGain(0.91f), 1.0e-5f,
                                  "audio uses producer atomic before 60Hz UI tick");
        fader->endGesture();
        expectWithinAbsoluteError(play(1.0, true),
                                  taper.normToGain(0.50f), 1.0e-5f,
                                  "Touch release resumes curve");

        beginTest("Latch sustains touched gain until release at Stop");
        modes.setMode(id, AutomationMode::Latch);
        modes.setLatchHeld(id, true);
        expectWithinAbsoluteError(play(1.5, true),
                                  taper.normToGain(0.91f), 1.0e-5f,
                                  "latch suppresses stale breakpoint");
        modes.setLatchHeld(id, false);
        expectWithinAbsoluteError(play(1.5, true),
                                  taper.normToGain(0.65f), 1.0e-5f,
                                  "released latch resumes curve");

        beginTest("missing/invalid key and malformed clock remain neutral");
        expectWithinAbsoluteError(getNativeTrackVolumeGainRT(
            kInvalidParameterID, fallbackGain, 1.0, true, false,
            registry, lanes, modes, taper), fallbackGain, 1.0e-5f);
        expectWithinAbsoluteError(play(std::numeric_limits<double>::quiet_NaN(),
                                       true), fallbackGain, 1.0e-5f);
    }
};
static AutomationNativeFaderAudioPlaybackTests automationNativeFaderAudioPlaybackTests;


/**
    Pan must follow saved PPQ curves in AudioEngine even without an open mixer.
    A valid curve takes precedence over the legacy sample-domain pan lane.
*/
class AutomationNativePanAudioPlaybackTests final : public juce::UnitTest
{
public:
    AutomationNativePanAudioPlaybackTests()
        : juce::UnitTest("automation.native-pan-direct-audio-playback.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        constexpr ParameterID id = 77032;
        constexpr float manualPan = -0.30f;
        auto* pan = registry.createParameter(id, "Production Pan", {});
        expect(pan != nullptr);
        if (pan == nullptr) return;

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.0f, CurveType::Linear, 0.0f},
            {2.0, 1.0f, CurveType::Linear, 0.0f}
        });
        const auto play = [&] (double ppq, bool rolling)
        {
            return getNativeTrackPanRT(id, manualPan, ppq, rolling, false,
                                       registry, lanes, modes);
        };

        beginTest("Read drives L/C/R audio without a GUI listener");
        modes.setMode(id, AutomationMode::Read);
        expectWithinAbsoluteError(play(0.0, true), -1.0f, 1.0e-5f);
        expectWithinAbsoluteError(play(1.0, true), 0.0f, 1.0e-5f);
        expectWithinAbsoluteError(play(2.0, true), 1.0f, 1.0e-5f);

        beginTest("stopped transport, Off and automation mute preserve manual pan");
        expectWithinAbsoluteError(play(1.0, false), manualPan, 1.0e-5f);
        expectWithinAbsoluteError(getNativeTrackPanRT(
            id, manualPan, 1.0, true, true, registry, lanes, modes),
            manualPan, 1.0e-5f);
        modes.setMode(id, AutomationMode::Off);
        expectWithinAbsoluteError(play(1.0, true), manualPan, 1.0e-5f);

        beginTest("Touch follows live gesture and returns to saved envelope");
        modes.setMode(id, AutomationMode::Touch);
        pan->beginGesture();
        pan->setValueFromUser(0.9f);
        expectWithinAbsoluteError(play(1.0, true), 0.8f, 1.0e-5f);
        pan->endGesture();
        expectWithinAbsoluteError(play(1.0, true), 0.0f, 1.0e-5f);

        beginTest("Latch sustains the live pan, then returns to lane");
        modes.setMode(id, AutomationMode::Latch);
        modes.setLatchHeld(id, true);
        expectWithinAbsoluteError(play(0.5, true), 0.8f, 1.0e-5f);
        modes.setLatchHeld(id, false);
        expectWithinAbsoluteError(play(0.5, true), -0.5f, 1.0e-5f);

        beginTest("invalid key and clock use neutral fallback");
        expectWithinAbsoluteError(getNativeTrackPanRT(
            kInvalidParameterID, manualPan, 1.0, true, false,
            registry, lanes, modes), manualPan, 1.0e-5f);
        expectWithinAbsoluteError(play(std::numeric_limits<double>::quiet_NaN(),
                                       true), manualPan, 1.0e-5f);

        beginTest("RT pan playback makes no heap allocations");
        modes.setMode(id, AutomationMode::Read);
        float sum = 0.0f;
        {
            juce::UnitTestAllocationChecker checker(*this);
            for (int i = 0; i < 300; ++i)
                sum += play(1.0, true);
        }
        // Assertions may allocate test-report Strings: never run them under
        // the realtime allocation guard itself.
        expectWithinAbsoluteError(sum, 0.0f, 1.0e-5f);
    }
};
static AutomationNativePanAudioPlaybackTests automationNativePanAudioPlaybackTests;


/** Native mute is discrete and must silence actual DSP independently of UI. */
class AutomationNativeMuteAudioPlaybackTests final : public juce::UnitTest
{
public:
    AutomationNativeMuteAudioPlaybackTests()
        : juce::UnitTest("automation.native-mute-stepped-audio-playback.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        using namespace apex::automation;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        constexpr ParameterID id = 77033;
        auto* mute = registry.createParameter(id, "Production Mute", {});
        expect(mute != nullptr);
        if (mute == nullptr) return;

        const auto play = [&] (double ppq, bool rolling, bool fallback = false,
                              bool automationMuted = false)
        {
            return getNativeTrackMuteRT(id, fallback, ppq, rolling,
                                        automationMuted, registry, lanes, modes);
        };

        beginTest("no lane or invalid ID preserves manual mute");
        modes.setMode(id, AutomationMode::Read);
        expect(!play(1.0, true));
        expect(play(1.0, true, true));
        expect(getNativeTrackMuteRT(kInvalidParameterID, true, 1.0, true,
                                   false, registry, lanes, modes));

        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.0f, CurveType::Linear, 0.0f},
            {2.0, 1.0f, CurveType::Smooth, 0.0f},
            {4.0, 0.0f, CurveType::Linear, 0.0f}
        });

        beginTest("mute only changes at knots; never half-mutes before a knot");
        expect(!play(0.0, true));
        expect(!play(1.999, true));
        expect(play(2.0, true));
        expect(play(3.99, true));
        expect(!play(4.0, true));
        expect(!play(4.0, true, true)); // lane explicitly unmutes manual mute

        beginTest("Off, stopped, disabled automation and invalid clock");
        modes.setMode(id, AutomationMode::Off);
        expect(play(0.0, true, true));
        modes.setMode(id, AutomationMode::Read);
        expect(play(2.0, false, true));
        expect(play(2.0, true, true, true));
        expect(play(std::numeric_limits<double>::quiet_NaN(), true, true));

        beginTest("Touch overrides a lane while held then returns");
        modes.setMode(id, AutomationMode::Touch);
        mute->beginGesture();
        mute->setValueFromUser(1.0f);
        expect(play(0.0, true));
        mute->endGesture();
        expect(!play(0.0, true));

        beginTest("Latch and Write hold gesture value until latch cleared");
        for (auto mode : {AutomationMode::Latch, AutomationMode::Write})
        {
            modes.setMode(id, mode);
            modes.setLatchHeld(id, true);
            expect(play(0.0, true));
            modes.setLatchHeld(id, false);
            expect(!play(0.0, true));
        }

        beginTest("RT mute evaluation performs no heap allocations");
        modes.setMode(id, AutomationMode::Read);
        int mutedBlocks = 0;
        {
            juce::UnitTestAllocationChecker allocationGuard(*this);
            for (int i = 0; i < 300; ++i)
                mutedBlocks += play(2.0, true) ? 1 : 0;
        }
        expectEquals(mutedBlocks, 300);
    }
};
static AutomationNativeMuteAudioPlaybackTests automationNativeMuteAudioPlaybackTests;

/** Native Solo must gate routed tracks and preserve ancestor FolderBus audio. */
class AutomationNativeSoloAudioPlaybackTests final : public juce::UnitTest
{
public:
    AutomationNativeSoloAudioPlaybackTests()
        : juce::UnitTest("automation.native-solo-folder-routing.v1",
                         "APEX.Diagnostics") {}
    void runTest() override
    {
        using namespace apex::automation;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        constexpr ParameterID leaf = 77041, bus = 77042;
        auto* p = registry.createParameter(leaf, "Solo leaf", {});
        expect(p != nullptr);
        if (p == nullptr) return;
        registry.createParameter(bus, "Solo bus", {});
        modes.setMode(leaf, AutomationMode::Read);
        modes.setMode(bus, AutomationMode::Read);
        DAW::FolderBusSnapshot snap;
        snap.soloSources = { {leaf, false, false}, {bus, false, false} };
        snap.soloAudibleSourceIndices["LEAF_A"] = {0, 1};
        snap.soloAudibleSourceIndices["LEAF_B"] = {1};
        snap.soloAudibleSourceIndices["BUS"] = {0, 1};
        lanes.getOrCreateLane(leaf).replacePoints({
            {0.0, 0.0f, CurveType::Linear, 0.0f},
            {2.0, 1.0f, CurveType::Smooth, 0.0f},
            {4.0, 0.0f, CurveType::Linear, 0.0f}
        });
        const auto read = [&] (double ppq, bool rolling)
        {
            return evaluateNativeSoloBlockRT(&snap, ppq, rolling,
                                              registry, lanes, modes);
        };
        const auto audible = [&] (const juce::String& id, const NativeSoloBlockState& state)
        {
            return isNativeSoloAudibleRT(id, &snap, state, registry, lanes, modes);
        };
        beginTest("without solo, every source passes");
        auto state = read(0.0, true);
        expect(!state.anyActive);
        expect(audible("LEAF_A", state));
        expect(audible("LEAF_B", state));
        beginTest("stepped leaf solo admits only that track and its ancestor");
        expect(!read(1.999, true).anyActive);
        state = read(2.0, true);
        expect(state.anyActive);
        expect(audible("LEAF_A", state));
        expect(audible("BUS", state));
        expect(!audible("LEAF_B", state));
        expect(!audible("OUTSIDE", state));
        beginTest("manually soloed folder admits descendants");
        snap.soloSources[1].manualSoloed = true;
        state = read(0.0, true);
        expect(audible("LEAF_B", state));
        snap.soloSources[1].manualSoloed = false;
        beginTest("stopped and Off bypass lane");
        expect(!read(2.0, false).anyActive);
        modes.setMode(leaf, AutomationMode::Off);
        expect(!read(2.0, true).anyActive);
        beginTest("Touch takes priority; Latch holds current value");
        modes.setMode(leaf, AutomationMode::Touch);
        p->beginGesture();
        p->setValueFromUser(1.0f);
        expect(read(0.0, true).anyActive);
        p->endGesture();
        expect(!read(0.0, true).anyActive);
        modes.setMode(leaf, AutomationMode::Latch);
        modes.setLatchHeld(leaf, true);
        expect(read(0.0, true).anyActive);
        modes.setLatchHeld(leaf, false);
        expect(!read(0.0, true).anyActive);
        beginTest("no heap allocations in Solo callback");
        modes.setMode(leaf, AutomationMode::Read);
        int count = 0;
        {
            juce::UnitTestAllocationChecker checker(*this);
            for (int i = 0; i < 200; ++i)
                count += read(2.0, true).anyActive ? 1 : 0;
        }
        expectEquals(count, 200);
    }
};
static AutomationNativeSoloAudioPlaybackTests automationNativeSoloAudioPlaybackTests;

// Flat routing has no FolderBusSnapshot; a PPQ Solo lane must still gate
// audible tracks, including the manual/Off/stop/gesture fallbacks.
class AutomationNativeSoloFlatFallbackTests final : public juce::UnitTest
{
public:
    AutomationNativeSoloFlatFallbackTests()
        : juce::UnitTest("automation.native-solo-flat-fallback.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        using namespace apex::automation;
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        constexpr ParameterID firstID = 79011, secondID = 79012;
        registry.createParameter(firstID, "Flat Solo A", {});
        auto* second = registry.createParameter(secondID, "Flat Solo B", {});
        expect(second != nullptr);
        if (second == nullptr) return;
        modes.setMode(firstID, AutomationMode::Read);
        modes.setMode(secondID, AutomationMode::Read);
        lanes.getOrCreateLane(secondID).replacePoints({
            {0.0, 0.0f, CurveType::Linear, 0.0f},
            {2.0, 1.0f, CurveType::Linear, 0.0f}
        });
        std::array<NativeFlatSoloSource, 2> sources {{
            {firstID, false, false}, {secondID, false, false}
        }};
        const auto any = [&] (double ppq, bool rolling) noexcept
        {
            return hasNativeFlatSoloRT(
                (int)sources.size(),
                [&] (int i) noexcept { return sources[(std::size_t)i]; },
                ppq, rolling, registry, lanes, modes);
        };
        const auto audible = [&] (int i, bool active, double ppq, bool rolling)
        {
            return isNativeFlatSoloAudibleRT(
                sources[(std::size_t)i], active, ppq, rolling,
                registry, lanes, modes);
        };

        beginTest("flat PPQ Solo narrows only the active track");
        expect(!any(0.0, true));
        expect(audible(0, false, 0.0, true));
        expect(audible(1, false, 0.0, true));
        expect(any(2.0, true));
        expect(!audible(0, true, 2.0, true));
        expect(audible(1, true, 2.0, true));

        beginTest("manual Solo, stopped transport and Off mode");
        sources[0].manualSoloed = true;
        expect(any(0.0, true));
        expect(audible(0, true, 0.0, true));
        expect(!audible(1, true, 0.0, true));
        sources[0].manualSoloed = false;
        expect(!any(2.0, false));
        modes.setMode(secondID, AutomationMode::Off);
        expect(!any(2.0, true));
        modes.setMode(secondID, AutomationMode::Read);
        sources[1].automationMuted = true;
        expect(!any(2.0, true));
        sources[1].automationMuted = false;

        beginTest("Touch gesture takes precedence over an inactive lane");
        modes.setMode(secondID, AutomationMode::Touch);
        second->beginGesture();
        second->setValueFromUser(1.0f);
        expect(any(0.0, true));
        expect(audible(1, true, 0.0, true));
        second->endGesture();
        expect(!any(0.0, true));

        beginTest("flat Solo callback does not allocate");
        modes.setMode(secondID, AutomationMode::Read);
        int count = 0;
        {
            juce::UnitTestAllocationChecker checker(*this);
            for (int i = 0; i < 200; ++i)
                if (any(2.0, true) && audible(1, true, 2.0, true))
                    ++count;
        }
        expectEquals(count, 200);
    }
};
static AutomationNativeSoloFlatFallbackTests automationNativeSoloFlatFallbackTests;

// A restore must publish one coherent map, release its batch suppression,
// and leave previous RT snapshots alive until their readers finish.
class AutomationLaneRestoreBatchTests final : public juce::UnitTest
{
public:
    AutomationLaneRestoreBatchTests()
        : juce::UnitTest("automation.lane-restore-batch-publication.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        AutomationLaneStore lanes;
        constexpr ParameterID restoredID = 78121;
        constexpr ParameterID subsequentID = 78122;
        lanes.getOrCreateLane(restoredID).replacePoints({
            {0.0, 0.2f, CurveType::Linear, 0.0f},
            {1.0, 0.8f, CurveType::Linear, 0.0f}
        });
        const auto saved = lanes.getState();
        const auto oldRTSnapshot = lanes.getLaneSnapshotRT();
        lanes.clear();

        beginTest("restore publishes once and preserves old RT snapshot lifetime");
        const auto generationBefore = lanes.getChangeGeneration();
        lanes.restoreState(saved);
        expectEquals((juce::int64) lanes.getChangeGeneration(),
                     (juce::int64) (generationBefore + 1));
        const auto restored = lanes.findLaneRT(restoredID);
        expect(restored != nullptr);
        if (restored != nullptr)
        {
            const auto points = restored->getSnapshot();
            expect(points != nullptr && points->size() == 2);
        }
        expect(oldRTSnapshot != nullptr &&
               oldRTSnapshot->find(restoredID) != oldRTSnapshot->end());

        beginTest("invalid legacy tree clears and subsequent mutations publish");
        const auto beforeEmpty = lanes.getChangeGeneration();
        lanes.restoreState(juce::ValueTree{});
        expectEquals((juce::int64) lanes.getChangeGeneration(),
                     (juce::int64) (beforeEmpty + 1));
        expect(lanes.findLaneRT(restoredID) == nullptr);
        const auto beforeNext = lanes.getChangeGeneration();
        lanes.getOrCreateLane(subsequentID);
        expectEquals((juce::int64) lanes.getChangeGeneration(),
                     (juce::int64) (beforeNext + 1));
        expect(lanes.findLaneRT(subsequentID) != nullptr);
    }
};
static AutomationLaneRestoreBatchTests automationLaneRestoreBatchTests;

// Legacy projects without ModeState must not inherit Write/Latch from the
// previously opened song. All structural mode edits remain RT-published.
class AutomationLegacyModeRestoreTests final : public juce::UnitTest
{
public:
    AutomationLegacyModeRestoreTests()
        : juce::UnitTest("automation.legacy-mode-restore-isolation.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        using namespace apex::automation;
        auto& keys = AutomationParameterKeyRegistry::getInstance();
        const juce::String key("test.legacy-mode-restore.parameter");
        const auto id = keys.getOrCreateID(key);
        AutomationModeState modes;
        modes.setMode(id, AutomationMode::Write);
        modes.setGlobalDefaultMode(AutomationMode::Off);

        beginTest("missing ModeState discards old Write override and Off default");
        const auto oldRT = modes.getStateSnapshotRT();
        modes.restoreState(juce::ValueTree{});
        expectEquals((int) modes.getGlobalDefaultMode(), (int) AutomationMode::Read);
        expectEquals((int) modes.getModeRT(id), (int) AutomationMode::Read);
        expect(oldRT != nullptr);
        expect(modes.getStateSnapshotRT() != oldRT);

        beginTest("saved Latch override and default round-trip by key");
        juce::ValueTree saved("ModeState");
        saved.setProperty("globalDefault", (int) AutomationMode::Off, nullptr);
        juce::ValueTree entry("Mode");
        entry.setProperty("key", key, nullptr);
        entry.setProperty("mode", (int) AutomationMode::Latch, nullptr);
        saved.addChild(entry, -1, nullptr);
        modes.restoreState(saved);
        expectEquals((int) modes.getGlobalDefaultMode(), (int) AutomationMode::Off);
        expectEquals((int) modes.getModeRT(id), (int) AutomationMode::Latch);

        beginTest("valid but empty legacy mode tree resets previous overrides");
        modes.restoreState(juce::ValueTree("ModeState"));
        expectEquals((int) modes.getGlobalDefaultMode(), (int) AutomationMode::Read);
        expectEquals((int) modes.getModeRT(id), (int) AutomationMode::Read);
        modes.setMode(id, AutomationMode::Touch);
        expectEquals((int) modes.getModeRT(id), (int) AutomationMode::Touch);
    }
};
static AutomationLegacyModeRestoreTests automationLegacyModeRestoreTests;

/** A discrete parameter must NOT interpolate between automation points:
 *  the atomic seen by mixer/plugin UI must transition at the breakpoint,
 *  matching the native stepped Mute/Solo audio path.
 */
class AutomationSteppedEvaluatorTests final : public juce::UnitTest
{
public:
    AutomationSteppedEvaluatorTests()
        : juce::UnitTest("automation.evaluator-stepped-hold.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        constexpr ParameterID steppedID = 79181, continuousID = 79182;
        ParameterRange steppedRange;
        steppedRange.isStepped = true;
        steppedRange.numSteps = 2;
        auto* switchParam = registry.createParameter(
            steppedID, "Discrete bypass", steppedRange);
        auto* gainParam = registry.createParameter(
            continuousID, "Continuous gain", {});
        expect(switchParam != nullptr && gainParam != nullptr);
        if (switchParam == nullptr || gainParam == nullptr) return;
        modes.setMode(steppedID, AutomationMode::Read);
        modes.setMode(continuousID, AutomationMode::Read);
        const AutomationLane::PointVector points {
            {0.0, 0.0f, CurveType::Smooth, 0.0f},
            {4.0, 1.0f, CurveType::Linear, 0.0f}
        };
        lanes.getOrCreateLane(steppedID).replacePoints(points);
        lanes.getOrCreateLane(continuousID).replacePoints(points);
        AutomationEvaluator evaluator(registry, lanes, modes);

        beginTest("stepped holds through a malformed Smooth segment");
        evaluator.evaluateBlock(2.0, true);
        expectWithinAbsoluteError(switchParam->getNormalizedValue(), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError(gainParam->getNormalizedValue(), 0.5f, 1.0e-6f);
        evaluator.evaluateBlock(3.999, true);
        expectWithinAbsoluteError(switchParam->getNormalizedValue(), 0.0f, 1.0e-6f);
        evaluator.evaluateBlock(4.0, true);
        expectWithinAbsoluteError(switchParam->getNormalizedValue(), 1.0f, 1.0e-6f);

        beginTest("stopped transport and Off mode do not override user value");
        switchParam->writeValue(0.0f, ChangeSource::User);
        evaluator.evaluateBlock(4.0, false);
        expectWithinAbsoluteError(switchParam->getNormalizedValue(), 0.0f, 1.0e-6f);
        modes.setMode(steppedID, AutomationMode::Off);
        evaluator.evaluateBlock(4.0, true);
        expectWithinAbsoluteError(switchParam->getNormalizedValue(), 0.0f, 1.0e-6f);

        beginTest("stepped evaluator stays allocation-free on audio callback");
        modes.setMode(steppedID, AutomationMode::Read);
        int heldReads = 0;
        {
            juce::UnitTestAllocationChecker checker(*this);
            for (int i = 0; i < 256; ++i)
            {
                evaluator.evaluateBlock(2.0, true);
                if (switchParam->getNormalizedValue() == 0.0f)
                    ++heldReads;
            }
        }
        expectEquals(heldReads, 256);
    }
};
static AutomationSteppedEvaluatorTests automationSteppedEvaluatorTests;


/**
    Final UI-to-DSP wiring gate (not a mock button policy):
    a real JUCE Slider + production SliderAutomationAttachment produces
    user changes, the 90Hz recorder writes PPQ knots, the audio evaluator
    replays them in Read, and the 60Hz dispatcher changes the visible slider
    WITHOUT creating a second user gesture/phantom Write.
    Uses deterministic clock/dispatcher ticks, not wall-clock sleeps.
*/
class AutomationGuiSliderRecordReplayTests final : public juce::UnitTest
{
public:
    AutomationGuiSliderRecordReplayTests()
        : juce::UnitTest("automation.gui-slider-write-replay-dispatch.v1",
                         "APEX.Diagnostics") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        constexpr ParameterID id = 79963;

        AutomationParameterRegistry registry;
        AutomationLaneStore lanes;
        AutomationModeState modes;
        AutomationClock clock;
        AutomationTransportState armed;
        auto queue = std::make_unique<AutomationGestureQueue>();

        auto* parameter = registry.createParameter(
            id, "Real JUCE mixer fader", ParameterRange{});
        expect(parameter != nullptr);
        if (parameter == nullptr) return;

        modes.setMode(id, AutomationMode::Write);
        armed.setRecordArmed(true);
        lanes.getOrCreateLane(id).replacePoints({
            {0.0, 0.15f, CurveType::Linear, 0.0f},
            {8.0, 0.15f, CurveType::Linear, 0.0f}
        });

        AutomationGestureBridge bridge(registry, *queue, clock);
        bridge.attachToParameter(*parameter);
        AutomationRecorder recorder(registry, lanes, modes, *queue, clock, armed);
        AutomationEvaluator evaluator(registry, lanes, modes);
        AutomationUIDispatcher dispatcher(registry);
        dispatcher.pause();

        juce::Slider fader;
        fader.setSliderStyle(juce::Slider::LinearHorizontal);
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

        {
            SliderAutomationAttachment attachment(*parameter, fader);

            beginTest("visible JUCE control records PPQ Write movements");
            clock.publishFromAudioThread(1.0, 0.001, true);
            recorder.drainForTests();

            // Model the GUI drag begin/end boundaries explicitly. Values
            // themselves still flow through the REAL JUCE Slider attachment;
            // gesture notifications go through the production native bridge.
            parameter->beginGesture();
            clock.publishFromAudioThread(1.25, 0.001, true);
            fader.setValue(0.70, juce::sendNotificationSync);
            expectWithinAbsoluteError(parameter->getNormalizedValue(), 0.70f,
                                      1.0e-5f, "slider event reaches parameter immediately");

            clock.publishFromAudioThread(1.50, 0.001, true);
            fader.setValue(0.35, juce::sendNotificationSync);
            expectWithinAbsoluteError(parameter->getNormalizedValue(), 0.35f,
                                      1.0e-5f, "second movement is not coalesced");

            clock.publishFromAudioThread(1.75, 0.001, true);
            parameter->endGesture();
            recorder.drainForTests();
            expect(modes.latchHeldRT(id), "Write keeps the last user value until Stop");
            clock.publishFromAudioThread(2.0, 0.001, false);
            recorder.drainForTests();
            expect(!modes.latchHeldRT(id), "Stop ends Write hold");

            auto lane = lanes.findLane(id);
            expect(lane != nullptr, "GUI movements must create a committed lane");
            if (lane == nullptr) return;
            auto points = lane->getSnapshot();
            expect(points != nullptr && !points->empty(), "recorded lane has breakpoints");
            if (points == nullptr || points->empty()) return;
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 1.25),
                                      0.70f, 0.02f, "first GUI move recorded at producer PPQ");
            expectWithinAbsoluteError(AutomationLane::evaluateAt(*points, 1.50),
                                      0.35f, 0.02f, "second GUI move recorded at producer PPQ");

            beginTest("Read automation moves the actual slider without feedback Write");
            modes.setMode(id, AutomationMode::Read);
            parameter->setValueProgrammatic(0.02f);
            dispatcher.dispatchOnceForTests();
            expectWithinAbsoluteError(static_cast<float>(fader.getValue()),
                                      0.02f, 1.0e-5f, "dispatcher initializes slider");

            evaluator.evaluateBlock(1.25, true);
            dispatcher.dispatchOnceForTests();
            expectWithinAbsoluteError(static_cast<float>(fader.getValue()),
                                      parameter->getNormalizedValue(), 1.0e-5f,
                                      "DSP Read update propagated to visible JUCE slider");
            expectWithinAbsoluteError(static_cast<float>(fader.getValue()),
                                      AutomationLane::evaluateAt(*points, 1.25),
                                      0.02f, "displayed value matches the persisted lane");

            AutomationGestureQueue::Event unwanted;
            expect(!queue->pop(unwanted),
                   "automated UI redraw cannot generate user/plugin Write events");
            dispatcher.dispatchOnceForTests();
            expect(!queue->pop(unwanted),
                   "no duplicate events on second unchanged 60Hz dispatcher tick");
        }

        bridge.detachFromParameter(id);
    }
};
static AutomationGuiSliderRecordReplayTests automationGuiSliderRecordReplayTests;


/**
 * V2 arrangement curve clipboard is musician-facing (not a hypothetical
 * standalone DSP feature): the real Arrangement AutomationLaneComponent
 * exposes Copy Entire Curve / Replace Entire Curve in its context menu.
 * These tests pin PPQ timing, parameter safety, curve shape and undo state.
 */
class AutomationCurveClipboardTests final : public juce::UnitTest
{
public:
    AutomationCurveClipboardTests()
        : juce::UnitTest("automation.v2-whole-curve-clipboard.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using DAW::AutomationLaneCurveClipboardCore;

        AutomationLaneCurveClipboardCore clipboard;
        AutomationLaneCore source("V2-TRACK-A", AutomationLaneCore::trackVolumeParameterId);
        source.addPoint(AutomationPoint{0, 0.20f, AutomationCurveType::Hold, 0.0f});
        source.addPoint(AutomationPoint{24000, 0.55f, AutomationCurveType::Smooth, -0.5f});
        source.addPoint(AutomationPoint{48000, 0.80f, AutomationCurveType::Linear, 0.25f});

        beginTest("48 kHz 120 BPM curve copied in PPQ, pasted 44.1 kHz 100 BPM");
        expect(clipboard.copyFrom(source, 48000.0, 120.0));
        expectEquals((int)clipboard.pointCount(), 3);
        expect(clipboard.hasCurveFor(AutomationLaneCore::trackVolumeParameterId));

        std::vector<AutomationPoint> pasted;
        expect(clipboard.makePastedPoints(AutomationLaneCore::trackVolumeParameterId,
                                          44100.0, 100.0, pasted));
        expectEquals((int)pasted.size(), 3);
        if (pasted.size() == 3)
        {
            expectEquals((juce::int64)pasted[0].timeSamples, (juce::int64)0);
            expectEquals((juce::int64)pasted[1].timeSamples, (juce::int64)26460);
            expectEquals((juce::int64)pasted[2].timeSamples, (juce::int64)52920);
            expect(pasted[0].curveToNext == AutomationCurveType::Hold);
            expect(pasted[1].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(pasted[1].tensionToNext, -0.5f, 0.00001f);
            expectWithinAbsoluteError(pasted[2].value, 0.80f, 0.00001f);
        }

        beginTest("never reinterpret volume curve as pan, pitch or unrelated VST3");
        std::vector<AutomationPoint> sentinel{{1024, 0.75f, AutomationCurveType::Hold, 0.0f}};
        expect(!clipboard.makePastedPoints(AutomationLaneCore::trackPanParameterId,
                                           48000.0, 120.0, sentinel));
        expectEquals((int)sentinel.size(), 1);
        expectEquals((juce::int64)sentinel.front().timeSamples, (juce::int64)1024);
        expect(!clipboard.makePastedPoints("plugin.0.foreign.cutoff",
                                           48000.0, 120.0, sentinel));

        beginTest("reject invalid tempo/sample rates without destroying clipboard");
        expect(!clipboard.copyFrom(source, 0.0, 120.0));
        expect(!clipboard.copyFrom(source, 48000.0,
                                  std::numeric_limits<double>::quiet_NaN()));
        expect(clipboard.hasCurveFor(AutomationLaneCore::trackVolumeParameterId));
        expect(!clipboard.makePastedPoints(AutomationLaneCore::trackVolumeParameterId,
                                           44100.0, 0.0, sentinel));
        expectEquals((int)sentinel.size(), 1);

        beginTest("complete curve state persists correct parameter key after roundtrip");
        DAW::AutomationLaneCore target("V2-TRACK-B",
                                       AutomationLaneCore::trackVolumeParameterId);
        target.points = pasted;
        target.sortAndResolveDuplicates();
        auto state = target.getState();
        expectEquals(state.getProperty("parameterId").toString(),
                     juce::String(AutomationLaneCore::trackVolumeParameterId));
        DAW::AutomationLaneCore reopened("V2-TRACK-B",
                                         AutomationLaneCore::trackVolumeParameterId);
        reopened.restoreState(state);
        expectEquals(reopened.getParameterId(),
                     juce::String(AutomationLaneCore::trackVolumeParameterId));
        expectEquals((int)reopened.points.size(), 3);
        if (reopened.points.size() == 3)
        {
            expect(reopened.points[0].curveToNext == AutomationCurveType::Hold);
            expect(reopened.points[1].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(reopened.points[1].tensionToNext, -0.5f, 0.00001f);
        }

        beginTest("clear removes clipboard authority");
        clipboard.clear();
        expect(!clipboard.hasCurveFor(AutomationLaneCore::trackVolumeParameterId));
    }
};
static AutomationCurveClipboardTests automationCurveClipboardTests;


class AutomationV2MusicalLfoTests final : public juce::UnitTest
{
public:
    AutomationV2MusicalLfoTests()
        : juce::UnitTest("automation.v2-musical-lfo-real-editor.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationMusicalLfoCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationCurveType;
        using DAW::AutomationPoint;

        beginTest("All four one-bar generators deterministic at 48kHz/120BPM");
        std::vector<AutomationPoint> values;
        for (const auto shape : {
             AutomationMusicalLfoCore::Shape::Sine,
             AutomationMusicalLfoCore::Shape::Triangle,
             AutomationMusicalLfoCore::Shape::Square,
             AutomationMusicalLfoCore::Shape::Saw })
        {
            expect(AutomationMusicalLfoCore::generate(
                AutomationLaneCore::trackVolumeParameterId, shape,
                48000.0, 120.0, values));
            expectEquals((int)values.size(), 65);
            if (values.size() != 65) continue;
            expectEquals((juce::int64)values.front().timeSamples, (juce::int64)0);
            expectEquals((juce::int64)values.back().timeSamples, (juce::int64)96000);
            for (size_t i = 0; i < values.size(); ++i)
            {
                expect(values[i].value >= 0.0f && values[i].value <= 1.0f);
                expect(std::isfinite(values[i].value));
                if (i > 0)
                    expect(values[i].timeSamples > values[i - 1].timeSamples);
            }

            if (shape == AutomationMusicalLfoCore::Shape::Square)
            {
                expect(values[0].curveToNext == AutomationCurveType::Hold);
                expectWithinAbsoluteError(values[0].value, 1.0f, 0.00001f);
                expectWithinAbsoluteError(values[32].value, 0.0f, 0.00001f);
                expectWithinAbsoluteError(values[64].value, 1.0f, 0.00001f);
            }
            if (shape == AutomationMusicalLfoCore::Shape::Triangle)
            {
                expectWithinAbsoluteError(values[0].value, 0.0f, 0.00001f);
                expectWithinAbsoluteError(values[32].value, 1.0f, 0.00001f);
                expectWithinAbsoluteError(values[64].value, 0.0f, 0.00001f);
            }
        }

        beginTest("Pan generator reaches full bipolar -1 to +1");
        expect(AutomationMusicalLfoCore::generate(AutomationLaneCore::trackPanParameterId,
                AutomationMusicalLfoCore::Shape::Triangle, 48000.0, 120.0, values));
        if (values.size() == 65)
        {
            expectWithinAbsoluteError(values[0].value, -1.0f, 0.00001f);
            expectWithinAbsoluteError(values[32].value, 1.0f, 0.00001f);
        }

        beginTest("No generator for mute/solo or pitch, or invalid numeric inputs");
        expect(!AutomationMusicalLfoCore::supports("track.mute"));
        expect(!AutomationMusicalLfoCore::supports("track.solo"));
        expect(!AutomationMusicalLfoCore::supports("clip.test.pitch_shift"));
        const auto priorSize = values.size();
        expect(!AutomationMusicalLfoCore::generate("track.mute",
                 AutomationMusicalLfoCore::Shape::Square, 48000.0, 120.0, values));
        expect(!AutomationMusicalLfoCore::generate(AutomationLaneCore::trackVolumeParameterId,
                 AutomationMusicalLfoCore::Shape::Sine, 48000.0,
                 std::numeric_limits<double>::quiet_NaN(), values));
        expect(!AutomationMusicalLfoCore::generate(AutomationLaneCore::trackVolumeParameterId,
                 AutomationMusicalLfoCore::Shape::Sine, 0.0, 120.0, values));
        expectEquals((int)values.size(), (int)priorSize);

        beginTest("LFO points survive core lane serialization, shapes preserve steps");
        DAW::AutomationLaneCore replay("LFO-TEST", AutomationLaneCore::trackVolumeParameterId);
        expect(AutomationMusicalLfoCore::generate(AutomationLaneCore::trackVolumeParameterId,
                 AutomationMusicalLfoCore::Shape::Square, 48000.0, 120.0, values));
        replay.points = values;
        replay.sortAndResolveDuplicates();
        auto state = replay.getState();
        DAW::AutomationLaneCore restored("LFO-TEST", AutomationLaneCore::trackVolumeParameterId);
        restored.restoreState(state);
        expectEquals((int)restored.points.size(), 65);
        if (restored.points.size() == 65)
        {
            expect(restored.points[31].curveToNext == AutomationCurveType::Hold);
            expect(restored.points[32].curveToNext == AutomationCurveType::Hold);
        }
    }
};
static AutomationV2MusicalLfoTests automationV2MusicalLfoTests;


class AutomationV2FreehandStrokeTests final : public juce::UnitTest
{
public:
    AutomationV2FreehandStrokeTests()
      : juce::UnitTest("automation.v2-freehand-pencil-core.v1", "APEX.Diagnostics") {}
    void runTest() override
    {
        using DAW::AutomationLaneCore;
        using DAW::AutomationFreehandStrokeCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        beginTest("Stroke replaces selected time region only");
        AutomationLaneCore lane("v2stroke", AutomationLaneCore::trackVolumeParameterId);
        lane.addPoint(AutomationPoint{100, 0.1f, AutomationCurveType::Hold, -0.3f});
        lane.addPoint(AutomationPoint{200, 0.2f, AutomationCurveType::Smooth, 0.3f});
        lane.addPoint(AutomationPoint{300, 0.3f, AutomationCurveType::Linear, 0.0f});
        lane.addPoint(AutomationPoint{400, 0.4f, AutomationCurveType::Hold, 0.4f});
        expect(AutomationFreehandStrokeCore::commit(lane, {
            AutomationPoint{350, 0.85f}, AutomationPoint{250, 0.55f},
            AutomationPoint{250, 0.65f}
        }));
        expectEquals((int)lane.points.size(), 5);
        if (lane.points.size() == 5)
        {
            expectEquals((juce::int64)lane.points[0].timeSamples, (juce::int64)100);
            expect(lane.points[0].curveToNext == AutomationCurveType::Hold);
            expectEquals((juce::int64)lane.points[1].timeSamples, (juce::int64)200);
            expectEquals((juce::int64)lane.points[2].timeSamples, (juce::int64)250);
            expectWithinAbsoluteError(lane.points[2].value, 0.65f, 0.00001f);
            expectEquals((juce::int64)lane.points[3].timeSamples, (juce::int64)350);
            expectEquals((juce::int64)lane.points[4].timeSamples, (juce::int64)400);
        }

        beginTest("Invalid stroke rejected without changing lane");
        auto before = lane.getState().toXmlString();
        expect(!AutomationFreehandStrokeCore::commit(lane, {
             AutomationPoint{-1, 0.7f}}));
        expect(!AutomationFreehandStrokeCore::commit(lane, {
             AutomationPoint{125, std::numeric_limits<float>::quiet_NaN()}}));
        expectEquals(lane.getState().toXmlString(), before);

        beginTest("Pencil respects explicit lane enabled state");
        lane.setEnabled(false);
        expect(!AutomationFreehandStrokeCore::commit(lane, {
            AutomationPoint{100, 0.9f}}));
        lane.setEnabled(true);
        expect(AutomationFreehandStrokeCore::commit(lane, {
            AutomationPoint{250, 0.4f}, AutomationPoint{300, 0.95f}}));
        expect(lane.getNumPoints() >= 2);

        beginTest("Pencil roundtrip retains curve points and timestamps");
        const auto state = lane.getState();
        AutomationLaneCore restored("v2stroke", AutomationLaneCore::trackVolumeParameterId);
        restored.restoreState(state);
        expectEquals(restored.getNumPoints(), lane.getNumPoints());
        expectEquals(restored.getParameterId(),
                     juce::String(AutomationLaneCore::trackVolumeParameterId));
    }
};
static AutomationV2FreehandStrokeTests automationV2FreehandStrokeTests;


class AutomationV2CurveTransformTests final : public juce::UnitTest
{
public:
    AutomationV2CurveTransformTests()
      : juce::UnitTest("automation.v2-curve-transform.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationCurveTransformCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        const std::vector<AutomationPoint> source {
            {0, 0.20f, AutomationCurveType::Hold, -0.75f},
            {24000, 0.75f, AutomationCurveType::Smooth, 0.40f},
            {48000, 0.95f, AutomationCurveType::Linear, 0.0f}
        };
        std::vector<AutomationPoint> result;

        beginTest("Volume vertical inversion preserves segment shapes and timestamps");
        expect(AutomationCurveTransformCore::transform(
            AutomationLaneCore::trackVolumeParameterId,
            AutomationCurveTransformCore::Operation::InvertValues,
            source, 48000.0, 120.0, result));
        expectEquals((int)result.size(), 3);
        if (result.size() == 3)
        {
            expectWithinAbsoluteError(result[0].value, 1.80f, 0.00001f);
            expectWithinAbsoluteError(result[1].value, 1.25f, 0.00001f);
            expect(result[0].curveToNext == AutomationCurveType::Hold);
            expect(result[1].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(result[0].tensionToNext, -0.75f, 0.00001f);
            expectEquals((juce::int64)result[1].timeSamples, (juce::int64)24000);
        }

        beginTest("Half and double scaling are clamped to correct parameter domain");
        expect(AutomationCurveTransformCore::transform(
            AutomationLaneCore::trackPanParameterId,
            AutomationCurveTransformCore::Operation::CompressHalf,
            source, 48000.0, 120.0, result));
        if (result.size() == 3)
            expectWithinAbsoluteError(result[0].value, 0.10f, 0.00001f);
        expect(AutomationCurveTransformCore::transform(
            AutomationLaneCore::trackPanParameterId,
            AutomationCurveTransformCore::Operation::ExpandDouble,
            source, 48000.0, 120.0, result));
        if (result.size() == 3)
            expectWithinAbsoluteError(result[2].value, 1.0f, 0.00001f);

        beginTest("Shift by exact tempo-aware beat, never by pixels");
        expect(AutomationCurveTransformCore::transform(
            AutomationLaneCore::trackVolumeParameterId,
            AutomationCurveTransformCore::Operation::ShiftRightBeat,
            source, 48000.0, 120.0, result));
        if (result.size() == 3)
        {
            expectEquals((juce::int64)result[0].timeSamples, (juce::int64)24000);
            expectEquals((juce::int64)result[1].timeSamples, (juce::int64)48000);
            expectEquals((juce::int64)result[2].timeSamples, (juce::int64)72000);
        }

        beginTest("Reject unsafe operation inputs without clobbering destination");
        const auto originalSize = result.size();
        expect(!AutomationCurveTransformCore::transform(
            "track.mute", AutomationCurveTransformCore::Operation::InvertValues,
            source, 48000.0, 120.0, result));
        expect(!AutomationCurveTransformCore::transform(
            AutomationLaneCore::trackVolumeParameterId,
            AutomationCurveTransformCore::Operation::ShiftRightBeat,
            source, 48000.0, std::numeric_limits<double>::quiet_NaN(), result));
        expectEquals((int)result.size(), (int)originalSize);

        beginTest("Transformed curves retain exact state across save/reopen");
        AutomationLaneCore lane("curve-test", AutomationLaneCore::trackVolumeParameterId);
        lane.points = result;
        lane.sortAndResolveDuplicates();
        const auto saved = lane.getState();
        AutomationLaneCore restored("curve-test", AutomationLaneCore::trackVolumeParameterId);
        restored.restoreState(saved);
        expectEquals(restored.getNumPoints(), lane.getNumPoints());
    }
};
static AutomationV2CurveTransformTests automationV2CurveTransformTests;


class AutomationV2CurveMorphTests final : public juce::UnitTest
{
public:
    AutomationV2CurveMorphTests()
      : juce::UnitTest("automation.v2-curve-snapshot-morph.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationCurveMorphCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using DAW::AutomationLaneCore;
        std::vector<AutomationPoint> left{
            {0, 0.0f, AutomationCurveType::Hold, -0.5f},
            {48000, 0.0f, AutomationCurveType::Smooth, 0.6f}};
        std::vector<AutomationPoint> right{
            {0, 1.0f, AutomationCurveType::Linear, 0.25f},
            {24000, 1.0f, AutomationCurveType::Linear, 0.25f},
            {48000, 1.0f, AutomationCurveType::Linear, 0.25f}};
        std::vector<AutomationPoint> result;

        beginTest("Blend 0 and 1 preserve exact source curves");
        expect(AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, left, right, 0.0f, result));
        expectEquals((int)result.size(), 2);
        if (result.size() == 2)
        {
            expect(result[0].curveToNext == AutomationCurveType::Hold);
            expectWithinAbsoluteError(result[1].tensionToNext, 0.6f, 0.00001f);
        }
        expect(AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, left, right, 1.0f, result));
        expectEquals((int)result.size(), 3);
        if (result.size() == 3)
            expect(result[0].curveToNext == AutomationCurveType::Linear);

        beginTest("Midpoint blend is bounded and samples distinct curve timestamps");
        expect(AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, left, right, 0.5f, result));
        expect(result.size() > 3);
        expect(result.size() <= AutomationCurveMorphCore::maxOutputKnots);
        for (size_t i = 0; i < result.size(); ++i)
        {
            expect(std::isfinite(result[i].value));
            expectWithinAbsoluteError(result[i].value, 0.5f, 0.00001f);
            if (i > 0)
                expect(result[i].timeSamples > result[i - 1].timeSamples);
        }

        beginTest("Negative, NaN, unordered and empty curves never overwrite output");
        const auto savedSize = result.size();
        expect(!AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, left, right, -0.1f, result));
        expect(!AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, left, right,
            std::numeric_limits<float>::quiet_NaN(), result));
        expect(!AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, {}, right, 0.5f, result));
        auto unordered = left;
        unordered.front().timeSamples = 55000;
        expect(!AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackVolumeParameterId, unordered, right, 0.5f, result));
        expectEquals((int)result.size(), (int)savedSize);

        beginTest("Pan morph remains in bipolar range and reopens with correct points");
        std::vector<AutomationPoint> panA{{0, -1.0f}, {48000, -1.0f}};
        std::vector<AutomationPoint> panB{{0, 1.0f}, {48000, 1.0f}};
        expect(AutomationCurveMorphCore::blend(
            AutomationLaneCore::trackPanParameterId, panA, panB, 0.5f, result));
        for (const auto& pt : result)
            expectWithinAbsoluteError(pt.value, 0.0f, 0.00001f);

        AutomationLaneCore playback("morph-track", AutomationLaneCore::trackPanParameterId);
        playback.points = result;
        playback.sortAndResolveDuplicates();
        AutomationLaneCore opened("morph-track", AutomationLaneCore::trackPanParameterId);
        opened.restoreState(playback.getState());
        expectEquals(opened.getNumPoints(), playback.getNumPoints());
    }
};
static AutomationV2CurveMorphTests automationV2CurveMorphTests;


class AutomationV2LaneEnabledPersistenceTests final : public juce::UnitTest
{
public:
    AutomationV2LaneEnabledPersistenceTests()
      : juce::UnitTest("automation.v2-lane-enabled-project-state.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest("Per-lane bypass survives save/load and command state restoration");
        DAW::AutomationLaneCore original("track-01", DAW::AutomationLaneCore::trackVolumeParameterId);
        original.addPoint(juce::int64(0), 0.8f);
        original.addPoint(juce::int64(48000), 0.5f);
        original.setEnabled(false);

        const auto state = original.getState();
        expect(!(bool)state.getProperty("enabled", true));
        DAW::AutomationLaneCore restored("track-01", DAW::AutomationLaneCore::trackVolumeParameterId);
        restored.restoreState(state);
        expect(!restored.isEnabled());
        expectWithinAbsoluteError(restored.getValueAtSample(24000, 0.9f), 0.9f, 0.00001f);
        expectEquals(restored.getNumPoints(), original.getNumPoints());

        beginTest("Enabled lanes persist TRUE, older project files default safely to TRUE");
        original.setEnabled(true);
        restored.restoreState(original.getState());
        expect(restored.isEnabled());
        auto legacy = original.getState().createCopy();
        legacy.removeProperty("enabled", nullptr);
        restored.setEnabled(false);
        restored.restoreState(legacy);
        expect(restored.isEnabled());

        beginTest("Global manager state also roundtrips lane enabled flags");
        DAW::AutomationManagerCore manager;
        auto& activeLane = manager.getOrCreateLane("v2-track-enabled", DAW::AutomationLaneCore::trackVolumeParameterId);
        activeLane.addPoint(0, 0.25f);
        activeLane.setEnabled(false);
        manager.publishSnapshot();
        const auto managerState = manager.getState();
        DAW::AutomationManagerCore newManager;
        newManager.restoreState(managerState);
        const auto* openedLane = newManager.findLane("v2-track-enabled", DAW::AutomationLaneCore::trackVolumeParameterId);
        expect(openedLane != nullptr);
        if (openedLane)
            expect(!openedLane->isEnabled());
    }
};
static AutomationV2LaneEnabledPersistenceTests automationV2LaneEnabledPersistenceTests;


/** Regression against destructive clip paste and parameter-domain corruption.
 *  Exercises the real AutomationManagerCore + persisted clip regions, not a
 *  standalone mock clipboard.
 */
class AutomationV2ClipClipboardSafetyTests final : public juce::UnitTest
{
public:
    AutomationV2ClipClipboardSafetyTests()
        : juce::UnitTest("automation.v2-clip-clipboard-safety.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationClipRegionCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using DAW::AutomationLaneCore;

        beginTest("Same-parameter reusable pan clip preserves negative values, curves and tension");
        AutomationManagerCore manager;
        AutomationClipRegionCore panSrc;
        panSrc.trackId = "PAN-SOURCE";
        panSrc.parameterId = AutomationLaneCore::trackPanParameterId;
        panSrc.startSample = 0;
        panSrc.lengthSamples = 48000;
        panSrc.localPoints = {
            AutomationPoint{0, -0.80f, AutomationCurveType::Hold, -0.30f},
            AutomationPoint{24000, 0.10f, AutomationCurveType::Smooth, 0.40f},
            AutomationPoint{48000, 0.65f, AutomationCurveType::Linear, 0.0f}
        };
        const auto sourceId = panSrc.regionId;
        manager.addClipRegion(panSrc);
        expect(manager.copyClipRegionState(sourceId));

        AutomationClipRegionCore panDest;
        panDest.trackId = "PAN-DEST";
        panDest.parameterId = AutomationLaneCore::trackPanParameterId;
        panDest.startSample = 96000;
        panDest.lengthSamples = 96000;
        panDest.localPoints = {{0, -0.1f}, {96000, -0.1f}};
        const auto destId = panDest.regionId;
        manager.addClipRegion(panDest);
        expect(manager.pasteClipRegionState(destId));
        const auto* pasted = manager.findClipRegion(destId);
        expect(pasted != nullptr);
        if (pasted != nullptr)
        {
            expectEquals((int)pasted->localPoints.size(), 3);
            if (pasted->localPoints.size() == 3)
            {
                expectEquals((juce::int64)pasted->localPoints[0].timeSamples, (juce::int64)0);
                expectEquals((juce::int64)pasted->localPoints[1].timeSamples, (juce::int64)48000);
                expectEquals((juce::int64)pasted->localPoints[2].timeSamples, (juce::int64)96000);
                expectWithinAbsoluteError(pasted->localPoints[0].value, -0.80f, 0.00001f);
                expectWithinAbsoluteError(pasted->localPoints[1].value, 0.10f, 0.00001f);
                expect(pasted->localPoints[0].curveToNext == AutomationCurveType::Hold);
                expect(pasted->localPoints[1].curveToNext == AutomationCurveType::Smooth);
                expectWithinAbsoluteError(pasted->localPoints[1].tensionToNext, 0.40f, 0.00001f);
            }
        }

        const auto* playback = manager.findLane("PAN-DEST", AutomationLaneCore::trackPanParameterId);
        expect(playback != nullptr);
        if (playback != nullptr)
            expectWithinAbsoluteError(playback->getValueAtSample(96000, 0.0f), -0.80f, 0.00001f);

        beginTest("Empty or malformed copy fails instead of claiming stale clipboard content");
        AutomationClipRegionCore noPoints;
        noPoints.trackId = "PAN-EMPTY";
        noPoints.parameterId = AutomationLaneCore::trackPanParameterId;
        noPoints.startSample = 0;
        noPoints.lengthSamples = 48000;
        const auto emptySourceId = noPoints.regionId;
        manager.addClipRegion(noPoints);
        expect(!manager.copyClipRegionState(emptySourceId));
        // Previously this call returned TRUE even though the old clipboard
        // still held the valid PAN-SOURCE curve. A user could paste the wrong
        // pattern after believing an empty source was copied.
        expect(manager.getClipClipboard().hasClipState());
        const auto* previous = manager.getClipClipboard().getClipState()
            ? &*manager.getClipClipboard().getClipState() : nullptr;
        expect(previous != nullptr);
        if (previous != nullptr)
            expectWithinAbsoluteError(previous->normalisedPoints[0].value,
                                     -0.80f, 0.00001f);

        AutomationClipRegionCore badSource = panSrc;
        badSource.startSample = -1;
        expect(!manager.getClipClipboard().copyRegionState(badSource));
        badSource.startSample = std::numeric_limits<int64_t>::max() - 4;
        badSource.lengthSamples = 48000;
        expect(!manager.getClipClipboard().copyRegionState(badSource));
        badSource = panSrc;
        badSource.localPoints[0].value = std::numeric_limits<float>::quiet_NaN();
        expect(!manager.getClipClipboard().copyRegionState(badSource));
        expect(manager.getClipClipboard().hasClipState());

        beginTest("Orphan target and unrepresentable sample duration reject before mutation");
        AutomationClipRegionCore orphanDestination;
        orphanDestination.parameterId = AutomationLaneCore::trackPanParameterId;
        orphanDestination.lengthSamples = 48000;
        orphanDestination.localPoints = { AutomationPoint{0, 0.5f} };
        const auto originalOrphan = orphanDestination.getState().toXmlString();
        expect(!manager.getClipClipboard().pasteStateToRegion(orphanDestination));
        expectEquals(orphanDestination.getState().toXmlString(), originalOrphan);
        orphanDestination.trackId = "PAN-EXTREMELY-LONG";
        orphanDestination.lengthSamples = DAW::AutomationClipClipboardCore::maxExactSampleLength + 1;
        expect(!manager.getClipClipboard().pasteStateToRegion(orphanDestination));
        orphanDestination = panSrc;
        orphanDestination.lengthSamples = 96000;
        expect(manager.getClipClipboard().pasteStateToRegion(orphanDestination));

        beginTest("Wrong parameter clip paste does not erase either destination or live points");
        AutomationClipRegionCore volDest;
        volDest.trackId = "VOL-DEST";
        volDest.parameterId = AutomationLaneCore::trackVolumeParameterId;
        volDest.startSample = 0;
        volDest.lengthSamples = 48000;
        volDest.localPoints = {
            AutomationPoint{0, 1.60f, AutomationCurveType::Smooth, -0.20f},
            AutomationPoint{48000, 1.20f, AutomationCurveType::Hold, 0.30f}
        };
        const auto volDestId = volDest.regionId;
        manager.addClipRegion(volDest);
        const auto snapshotBefore = manager.getState().toXmlString();
        expect(!manager.pasteClipRegionState(volDestId));
        expectEquals(manager.getState().toXmlString(), snapshotBefore);
        const auto* unchanged = manager.findClipRegion(volDestId);
        expect(unchanged != nullptr);
        if (unchanged != nullptr && unchanged->localPoints.size() == 2)
        {
            expectWithinAbsoluteError(unchanged->localPoints[0].value, 1.60f, 0.00001f);
            expect(unchanged->localPoints[0].curveToNext == AutomationCurveType::Smooth);
        }

        beginTest("Empty clipboard cannot wipe an existing clip on manager replay");
        AutomationManagerCore empty;
        empty.restoreState(manager.getState());
        const auto beforeEmptyPaste = empty.getState().toXmlString();
        expect(!empty.pasteClipRegionState(volDestId));
        expectEquals(empty.getState().toXmlString(), beforeEmptyPaste);

        beginTest("Long clips retain individual sample positions and >1.0 volume values");
        DAW::AutomationClipClipboardCore highResolutionClipboard;
        AutomationClipRegionCore longSource;
        longSource.trackId = "LONG-VOLUME";
        longSource.parameterId = AutomationLaneCore::trackVolumeParameterId;
        longSource.lengthSamples = 100000000;
        longSource.localPoints = {
            AutomationPoint{12345, 1.80f, AutomationCurveType::Smooth, -0.25f},
            AutomationPoint{98765432, 1.30f, AutomationCurveType::Hold, 0.50f}
        };
        highResolutionClipboard.copyRegionState(longSource);
        expect(highResolutionClipboard.hasClipState());

        AutomationClipRegionCore stretched;
        stretched.trackId = "LONG-VOLUME-B";
        stretched.parameterId = AutomationLaneCore::trackVolumeParameterId;
        stretched.lengthSamples = 200000000;
        expect(highResolutionClipboard.pasteStateToRegion(stretched));
        expectEquals((int)stretched.localPoints.size(), 2);
        if (stretched.localPoints.size() == 2)
        {
            expectEquals((juce::int64)stretched.localPoints[0].timeSamples,
                         (juce::int64)24690);
            expectEquals((juce::int64)stretched.localPoints[1].timeSamples,
                         (juce::int64)197530864);
            expectWithinAbsoluteError(stretched.localPoints[0].value, 1.80f, 0.00001f);
            expect(stretched.localPoints[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(stretched.localPoints[0].tensionToNext, -0.25f, 0.00001f);
        }

        beginTest("Actual project session save/reopen retains pasted pan range and original shape");
        AutomationManagerCore reopened;
        reopened.restoreState(manager.getState());
        const auto* savedPan = reopened.findClipRegion(destId);
        expect(savedPan != nullptr);
        if (savedPan != nullptr && savedPan->localPoints.size() == 3)
        {
            expectWithinAbsoluteError(savedPan->localPoints[0].value, -0.80f, 0.00001f);
            expect(savedPan->localPoints[1].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(savedPan->localPoints[1].tensionToNext, 0.40f, 0.00001f);
        }
    }
};
static AutomationV2ClipClipboardSafetyTests automationV2ClipClipboardSafetyTests;


class AutomationV2ClipDomainTransformTests final : public juce::UnitTest
{
public:
    AutomationV2ClipDomainTransformTests()
        : juce::UnitTest("automation.v2-clip-native-domain-transform.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationClipRegionCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationArticulatorToolsCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        beginTest("Pan flip/scale/normalize retain signed range and shape");
        AutomationClipRegionCore pan;
        pan.trackId = "pan-track";
        pan.parameterId = AutomationLaneCore::trackPanParameterId;
        pan.lengthSamples = 48000;
        pan.localPoints = {
            AutomationPoint{0, -0.8f, AutomationCurveType::Smooth, -0.4f},
            AutomationPoint{48000, 0.6f, AutomationCurveType::Hold, 0.3f}
        };
        AutomationArticulatorToolsCore::flipVertically(pan);
        expectWithinAbsoluteError(pan.localPoints[0].value, 0.8f, 0.00001f);
        expectWithinAbsoluteError(pan.localPoints[1].value, -0.6f, 0.00001f);
        AutomationArticulatorToolsCore::scaleLevels(pan, 0.5f);
        expectWithinAbsoluteError(pan.localPoints[0].value, 0.4f, 0.00001f);
        expectWithinAbsoluteError(pan.localPoints[1].value, -0.3f, 0.00001f);
        AutomationArticulatorToolsCore::normalizeLevels(pan);
        expectWithinAbsoluteError(pan.localPoints[0].value, 1.0f, 0.00001f);
        expectWithinAbsoluteError(pan.localPoints[1].value, -1.0f, 0.00001f);
        expect(pan.localPoints[0].curveToNext == AutomationCurveType::Smooth);
        expectWithinAbsoluteError(pan.localPoints[0].tensionToNext, -0.4f, 0.00001f);

        beginTest("Volume and clip gain transformations preserve levels above 1.0");
        AutomationClipRegionCore volume;
        volume.parameterId = AutomationLaneCore::trackVolumeParameterId;
        volume.lengthSamples = 1000;
        volume.localPoints = { AutomationPoint{0, 0.25f}, AutomationPoint{1000, 1.75f} };
        AutomationArticulatorToolsCore::flipVertically(volume);
        expectWithinAbsoluteError(volume.localPoints[0].value, 1.75f, 0.00001f);
        expectWithinAbsoluteError(volume.localPoints[1].value, 0.25f, 0.00001f);
        AutomationArticulatorToolsCore::resetLevels(volume, 1.5f);
        expectWithinAbsoluteError(volume.localPoints[0].value, 1.5f, 0.00001f);
        expectWithinAbsoluteError(volume.localPoints[1].value, 1.5f, 0.00001f);

        AutomationClipRegionCore clipGain;
        clipGain.parameterId = AutomationLaneCore::makeClipGainParameterId("gain-id");
        clipGain.lengthSamples = 1000;
        clipGain.localPoints = { AutomationPoint{0, 3.5f}, AutomationPoint{1000, 0.5f} };
        AutomationArticulatorToolsCore::flipVertically(clipGain);
        expectWithinAbsoluteError(clipGain.localPoints[0].value, 0.5f, 0.00001f);
        expectWithinAbsoluteError(clipGain.localPoints[1].value, 3.5f, 0.00001f);

        beginTest("Other native continuous parameters preserve host [0,2] domain");
        DAW::AutomationClipRegionCore send;
        send.parameterId = "send.group-b.level";
        send.lengthSamples = 1000;
        send.localPoints = { AutomationPoint{0, 1.7f},
                             AutomationPoint{1000, 0.2f} };
        AutomationArticulatorToolsCore::flipVertically(send);
        expectWithinAbsoluteError(send.localPoints[0].value, 0.3f, 0.00001f);
        expectWithinAbsoluteError(send.localPoints[1].value, 1.8f, 0.00001f);

        beginTest("Time stretch fit retains signed pan and exact curve tension");
        AutomationClipRegionCore panStretched;
        panStretched.parameterId = AutomationLaneCore::trackPanParameterId;
        panStretched.lengthSamples = 96000;
        panStretched.fitStateToLength(pan.localPoints, 48000);
        expectEquals((int)panStretched.localPoints.size(), 2);
        if (panStretched.localPoints.size() == 2)
        {
            expectEquals((juce::int64)panStretched.localPoints[1].timeSamples,
                         (juce::int64)96000);
            expectWithinAbsoluteError(panStretched.localPoints[1].value, -1.0f, 0.00001f);
            expect(panStretched.localPoints[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(panStretched.localPoints[0].tensionToNext, -0.4f, 0.00001f);
        }

        beginTest("Non-finite scale does not partially corrupt a valid pattern");
        const auto before = pan.getState().toXmlString();
        AutomationArticulatorToolsCore::scaleLevels(
            pan, std::numeric_limits<float>::quiet_NaN());
        expectEquals(pan.getState().toXmlString(), before);

        beginTest("Pan native range survives AutomationClipRegion save/reopen");
        const auto restored = AutomationClipRegionCore::fromState(pan.getState());
        expect(restored.isValid());
        expectEquals((int)restored.localPoints.size(), 2);
        if (restored.localPoints.size() == 2)
            expectWithinAbsoluteError(restored.localPoints[1].value, -1.0f, 0.00001f);
    }
};
static AutomationV2ClipDomainTransformTests automationV2ClipDomainTransformTests;

class AutomationV2EmptyClipMustNotEraseTests final : public juce::UnitTest
{
public:
    AutomationV2EmptyClipMustNotEraseTests()
        : juce::UnitTest("automation.v2-empty-clip-no-erasure.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationClipRegionCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        AutomationManagerCore manager;
        auto& lane = manager.getOrCreateLane("VOICE-ONE", AutomationLaneCore::trackVolumeParameterId);
        lane.addPoint(AutomationPoint{0, 0.7f, AutomationCurveType::Smooth, -0.4f});
        lane.addPoint(AutomationPoint{24000, 1.5f, AutomationCurveType::Hold, 0.2f});
        lane.addPoint(AutomationPoint{48000, 0.4f, AutomationCurveType::Linear, 0.0f});
        manager.publishSnapshot();

        auto assertUnaffected = [this](const AutomationManagerCore& state)
        {
            const auto* loaded = state.findLane(
                "VOICE-ONE", AutomationLaneCore::trackVolumeParameterId);
            expect(loaded != nullptr);
            if (loaded != nullptr)
            {
                expectEquals((int)loaded->points.size(), 3);
                if (loaded->points.size() == 3)
                {
                    expectEquals((juce::int64)loaded->points[1].timeSamples, (juce::int64)24000);
                    expectWithinAbsoluteError(loaded->points[0].value, 0.7f, 0.00001f);
                    expectWithinAbsoluteError(loaded->points[1].value, 1.5f, 0.00001f);
                    expectWithinAbsoluteError(loaded->points[2].value, 0.4f, 0.00001f);
                    expect(loaded->points[0].curveToNext == AutomationCurveType::Smooth);
                    expectWithinAbsoluteError(loaded->points[0].tensionToNext, -0.4f, 0.00001f);
                }
            }
        };

        beginTest("Creating an empty clip cannot erase three existing envelope points");
        AutomationClipRegionCore empty;
        empty.trackId = "VOICE-ONE";
        empty.parameterId = AutomationLaneCore::trackVolumeParameterId;
        empty.startSample = 0;
        empty.lengthSamples = 48000;
        const auto clipId = empty.regionId;
        manager.addClipRegion(empty);
        assertUnaffected(manager);

        beginTest("Empty region move/resize/mute/re-publish leave lane unchanged");
        expect(manager.moveClipRegion(clipId, 12000));
        assertUnaffected(manager);
        expect(manager.resizeClipRegion(clipId, 96000));
        assertUnaffected(manager);
        expect(manager.setClipRegionMuted(clipId, true));
        assertUnaffected(manager);
        manager.publishSnapshot();
        assertUnaffected(manager);

        beginTest("Project save/reopen does not reinterpret empty clip as erase mask");
        AutomationManagerCore reopened;
        reopened.restoreState(manager.getState());
        assertUnaffected(reopened);

        beginTest("Deleting empty placeholder does not remove non-owned points");
        expect(manager.deleteClipRegion(clipId));
        assertUnaffected(manager);
    }
};
static AutomationV2EmptyClipMustNotEraseTests automationV2EmptyClipMustNotEraseTests;

class AutomationV2BulkEditorClipOwnershipTests final : public juce::UnitTest
{
public:
    AutomationV2BulkEditorClipOwnershipTests()
        : juce::UnitTest("automation.v2-bulk-editor-clip-ownership.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationClipRegionCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        beginTest("Clip regions own only exact track+parameter identity");
        AutomationManagerCore manager;
        const auto vol = juce::String(AutomationLaneCore::trackVolumeParameterId);
        const auto pan = juce::String(AutomationLaneCore::trackPanParameterId);
        expect(!manager.hasClipRegionsForParameter("vocals", vol));

        AutomationClipRegionCore empty;
        empty.trackId = "vocals";
        empty.parameterId = vol;
        empty.lengthSamples = 48000;
        const auto emptyId = empty.regionId;
        manager.addClipRegion(empty);
        expect(!manager.hasClipRegionsForParameter("vocals", vol));

        AutomationClipRegionCore occupied;
        occupied.trackId = "vocals";
        occupied.parameterId = vol;
        occupied.startSample = 48000;
        occupied.lengthSamples = 48000;
        occupied.localPoints = {
            AutomationPoint{0, 0.8f, AutomationCurveType::Hold, 0.0f},
            AutomationPoint{48000, 1.5f, AutomationCurveType::Smooth, 0.2f}
        };
        const auto occupiedId = occupied.regionId;
        manager.addClipRegion(occupied);
        expect(manager.hasClipRegionsForParameter("vocals", vol));
        expect(!manager.hasClipRegionsForParameter("vocals", pan));
        expect(!manager.hasClipRegionsForParameter("different-vocals", vol));

        beginTest("Even muted clip still owns range and blocks destructive whole-lane operations");
        expect(manager.setClipRegionMuted(occupiedId, true));
        expect(manager.hasClipRegionsForParameter("vocals", vol));
        expect(manager.setClipRegionMuted(occupiedId, false));

        beginTest("Ownership guard survives session save/load and clears on region deletion");
        AutomationManagerCore loaded;
        loaded.restoreState(manager.getState());
        expect(loaded.hasClipRegionsForParameter("vocals", vol));
        expect(!loaded.hasClipRegionsForParameter("vocals", pan));
        expect(manager.deleteClipRegion(occupiedId));
        expect(!manager.hasClipRegionsForParameter("vocals", vol));
        expect(manager.deleteClipRegion(emptyId));
        expect(!manager.hasClipRegionsForParameter("vocals", vol));

        beginTest("Roundtrip preserves exact clip-owned values and curve shape");
        const auto* region = loaded.findClipRegion(occupiedId);
        expect(region != nullptr);
        if (region != nullptr)
        {
            expectEquals((int)region->localPoints.size(), 2);
            if (region->localPoints.size() == 2)
            {
                expectWithinAbsoluteError(region->localPoints[1].value, 1.5f, 0.00001f);
                expect(region->localPoints[1].curveToNext == AutomationCurveType::Smooth);
                expectWithinAbsoluteError(region->localPoints[1].tensionToNext, 0.2f, 0.00001f);
            }
        }
    }
};
static AutomationV2BulkEditorClipOwnershipTests automationV2BulkEditorClipOwnershipTests;

class AutomationV2GroupedCurveTests final : public juce::UnitTest
{
public:
    AutomationV2GroupedCurveTests()
        : juce::UnitTest("automation.v2-grouped-curve-atomic.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationGroupedCurveCore;
        using DAW::AutomationManagerCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        AutomationManagerCore manager;
        auto& a = manager.getOrCreateLane("singer-a", AutomationLaneCore::trackVolumeParameterId);
        a.addPoint(AutomationPoint{0, 0.25f, AutomationCurveType::Hold, 0.0f});
        a.addPoint(AutomationPoint{48000, 1.20f, AutomationCurveType::Smooth, 0.4f});
        auto& b = manager.getOrCreateLane("singer-b", AutomationLaneCore::trackVolumeParameterId);
        b.addPoint(AutomationPoint{0, 0.50f, AutomationCurveType::Hold, 0.0f});
        auto& unrelated = manager.getOrCreateLane("drumbus", AutomationLaneCore::trackPanParameterId);
        unrelated.addPoint(AutomationPoint{0, -0.75f, AutomationCurveType::Hold, 0.0f});
        manager.publishSnapshot();

        const std::vector<AutomationPoint> desired{
            {0, 0.10f, AutomationCurveType::Hold, 0.0f},
            {24000, 0.80f, AutomationCurveType::Smooth, -0.6f},
            {48000, 1.40f, AutomationCurveType::Linear, 0.0f}
        };

        beginTest("Prepare exact selection without edits or phantom lanes");
        AutomationGroupedCurveCore::Batch batch;
        expect(AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, desired, batch));
        expectEquals((int)batch.lanes.size(), 2);
        expectEquals((int)manager.findLane("singer-b", AutomationLaneCore::trackVolumeParameterId)->points.size(), 1);
        expect(manager.findLane("missing", AutomationLaneCore::trackVolumeParameterId) == nullptr);

        beginTest("Apply exactly two selected lanes, one published revision");
        const auto ver0 = manager.getSnapshotVersion();
        expect(AutomationGroupedCurveCore::apply(manager, batch, true));
        const auto ver1 = manager.getSnapshotVersion();
        expectEquals((juce::int64)ver1, (juce::int64)ver0 + 1);
        for (const auto& track : {juce::String("singer-a"), juce::String("singer-b")})
        {
            const auto* lane = manager.findLane(track, AutomationLaneCore::trackVolumeParameterId);
            expect(lane != nullptr);
            if (lane != nullptr && lane->points.size() == 3)
            {
                expectWithinAbsoluteError(lane->points[2].value, 1.40f, 0.00001f);
                expect(lane->points[0].curveToNext == AutomationCurveType::Hold);
                expect(lane->points[1].curveToNext == AutomationCurveType::Smooth);
                expectWithinAbsoluteError(lane->points[1].tensionToNext, -0.6f, 0.00001f);
            }
        }
        auto* drum = manager.findLane("drumbus", AutomationLaneCore::trackPanParameterId);
        expect(drum != nullptr);
        if (drum != nullptr)
            expectWithinAbsoluteError(drum->points[0].value, -0.75f, 0.00001f);

        beginTest("Undo only selected lane snapshots, without overwriting unrelated edits");
        manager.addPoint("drum", AutomationLaneCore::trackVolumeParameterId, 0, 0.75f);
        expect(AutomationGroupedCurveCore::apply(manager, batch, false));
        const auto* formerA = manager.findLane("singer-a", AutomationLaneCore::trackVolumeParameterId);
        const auto* formerB = manager.findLane("singer-b", AutomationLaneCore::trackVolumeParameterId);
        expect(formerA != nullptr && formerB != nullptr);
        if (formerA != nullptr && formerB != nullptr)
        {
            expectEquals((int)formerA->points.size(), 2);
            expectEquals((int)formerB->points.size(), 1);
            expectWithinAbsoluteError(formerA->points[1].value, 1.20f, 0.00001f);
            expectWithinAbsoluteError(formerB->points[0].value, 0.50f, 0.00001f);
        }
        expect(manager.findLane("drum", AutomationLaneCore::trackVolumeParameterId) != nullptr);

        beginTest("Stale redo must not overwrite one selected track edited after Undo");
        manager.addPoint("singer-b", AutomationLaneCore::trackVolumeParameterId,
                         12000, 1.45f);
        const auto beforeStaleRedo = manager.getState().toXmlString();
        const auto staleVersion = manager.getSnapshotVersion();
        expect(!AutomationGroupedCurveCore::apply(manager, batch, true));
        expectEquals(manager.getState().toXmlString(), beforeStaleRedo);
        expect(manager.getSnapshotVersion() == staleVersion);
        // Revert only singer-b to the originally captured state, preserving
        // unrelated changes to drum and other tracks.
        manager.getOrCreateLane("singer-b",
            AutomationLaneCore::trackVolumeParameterId)
            .restoreState(batch.lanes.back().before);
        manager.publishSnapshot();

        beginTest("Rollback guarantee: disabled/missing/double targets fail with zero changes");
        const auto beforeFailed = manager.getState().toXmlString();
        AutomationGroupedCurveCore::Batch invalid;
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "not-present"}, desired, invalid));
        expect(!invalid.isValid());
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-a"}, desired, invalid));
        expect(!AutomationGroupedCurveCore::prepare(
            manager, "plugin.1.something", {"singer-a", "singer-b"}, desired, invalid));
        auto badBatch = batch;
        badBatch.lanes.back().trackId = "not-present";
        expect(!AutomationGroupedCurveCore::apply(manager, badBatch, true));
        expectEquals(manager.getState().toXmlString(), beforeFailed);

        beginTest("A rejected re-prepare clears a previously valid staged group");
        auto previouslyValid = batch;
        expect(previouslyValid.isValid());
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "not-present"}, desired, previouslyValid));
        expect(!previouslyValid.isValid());

        beginTest("Disabled target cannot be replayed as grouped automation");
        manager.setLaneEnabled("singer-b", AutomationLaneCore::trackVolumeParameterId, false);
        const auto bypassedBefore = manager.getState().toXmlString();
        expect(!AutomationGroupedCurveCore::apply(manager, batch, true));
        expectEquals(manager.getState().toXmlString(), bypassedBefore);
        manager.setLaneEnabled("singer-b", AutomationLaneCore::trackVolumeParameterId, true);

        beginTest("Reject NaN and unordered curves before touching any track");
        auto malformed = desired;
        malformed[1].value = std::numeric_limits<float>::quiet_NaN();
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, malformed, invalid));
        malformed = desired;
        malformed[1].value = 2.5f; // native volume max 2.0, no silent clamp
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, malformed, invalid));
        malformed = desired;
        malformed[1].tensionToNext = 2.5f;
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, malformed, invalid));
        malformed = desired;
        malformed[1].timeSamples = -4;
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, malformed, invalid));
        expectEquals(manager.getState().toXmlString(), beforeFailed);

        beginTest("Clip-owned section blocks grouped writes");
        DAW::AutomationClipRegionCore managedRegion;
        managedRegion.trackId = "singer-b";
        managedRegion.parameterId = AutomationLaneCore::trackVolumeParameterId;
        managedRegion.startSample = 0;
        managedRegion.lengthSamples = 48000;
        managedRegion.localPoints = {
            AutomationPoint{0, 0.5f, AutomationCurveType::Hold, 0.0f},
            AutomationPoint{48000, 0.7f, AutomationCurveType::Linear, 0.0f}
        };
        manager.addClipRegion(managedRegion);
        const auto beforeReject = manager.getState().toXmlString();
        expect(!AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, desired, invalid));
        expect(!AutomationGroupedCurveCore::apply(manager, batch, true));
        expectEquals(manager.getState().toXmlString(), beforeReject);
        expect(manager.deleteClipRegion(managedRegion.regionId));

        beginTest("Clip region teardown invalidates stale group plan; reprepare explicitly");
        expect(!AutomationGroupedCurveCore::apply(manager, batch, true));
        AutomationGroupedCurveCore::Batch refreshed;
        expect(AutomationGroupedCurveCore::prepare(
            manager, AutomationLaneCore::trackVolumeParameterId,
            {"singer-a", "singer-b"}, desired, refreshed));
        expect(refreshed.isValid());

        beginTest("Empty automation clip placeholder must not block or erase grouped edits");
        DAW::AutomationClipRegionCore emptyPlaceholder;
        emptyPlaceholder.trackId = "singer-b";
        emptyPlaceholder.parameterId = AutomationLaneCore::trackVolumeParameterId;
        emptyPlaceholder.startSample = 0;
        emptyPlaceholder.lengthSamples = 48000;
        manager.addClipRegion(emptyPlaceholder);
        expect(!manager.hasClipRegionsForParameter(
            "singer-b", AutomationLaneCore::trackVolumeParameterId));

        beginTest("Group edit survives real manager project serialization");
        expect(AutomationGroupedCurveCore::apply(manager, refreshed, true));
        const auto* editedWithPlaceholder = manager.findLane(
            "singer-b", AutomationLaneCore::trackVolumeParameterId);
        expect(editedWithPlaceholder != nullptr);
        if (editedWithPlaceholder != nullptr)
            expectEquals((int)editedWithPlaceholder->points.size(), 3);
        expect(manager.deleteClipRegion(emptyPlaceholder.regionId));
        AutomationManagerCore reopened;
        reopened.restoreState(manager.getState());
        const auto* loaded = reopened.findLane("singer-b", AutomationLaneCore::trackVolumeParameterId);
        expect(loaded != nullptr);
        if (loaded != nullptr && loaded->points.size() == 3)
        {
            expectWithinAbsoluteError(loaded->points[2].value, 1.40f, 0.00001f);
            expect(loaded->points[1].curveToNext == AutomationCurveType::Smooth);
        }
    }
};
static AutomationV2GroupedCurveTests automationV2GroupedCurveTests;

class AutomationV2MultiParameterSnapshotTests final : public juce::UnitTest
{
public:
    AutomationV2MultiParameterSnapshotTests()
        : juce::UnitTest("automation.v2-multiparam-exact-instance-morph.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationMultiParameterSnapshotCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using Snap = AutomationMultiParameterSnapshotCore;

        const juce::String vol = AutomationLaneCore::trackVolumeParameterId;
        const juce::String pan = AutomationLaneCore::trackPanParameterId;
        const juce::String plug = "plugin.2.inst-a001.freq";
        const juce::String otherPlug = "plugin.2.inst-b002.freq";
        const std::vector<Snap::Target> targets = {
            {"V1", vol}, {"V2", pan}, {"V1", plug}};

        beginTest("Require explicit target identity, including plugin instance ID");
        expect(Snap::supported(vol));
        expect(Snap::supported(pan));
        expect(Snap::supported(plug));
        expect(!Snap::supported("plugin.2.freq"));
        expect(!Snap::supported("plugin.x.inst-a001.freq"));
        expect(!Snap::supported("plugin.2.inst-a001."));
        expect(!Snap::supported("instrument.volume"));
        expect(!Snap::supported("plugin.2.inst-a001"));
        AutomationManagerCore manager;
        auto& a = manager.getOrCreateLane("V1", vol);
        a.addPoint(AutomationPoint{0, 0.2f, AutomationCurveType::Smooth, -0.5f});
        a.addPoint(AutomationPoint{48000, 1.8f, AutomationCurveType::Hold, 0.3f});
        auto& b = manager.getOrCreateLane("V2", pan);
        b.addPoint(AutomationPoint{0, -0.8f, AutomationCurveType::Linear, 0.0f});
        b.addPoint(AutomationPoint{48000, 0.8f, AutomationCurveType::Smooth, 0.5f});
        auto& c = manager.getOrCreateLane("V1", plug);
        c.addPoint(AutomationPoint{0, 0.1f, AutomationCurveType::Smooth, 0.0f});
        c.addPoint(AutomationPoint{48000, 0.5f, AutomationCurveType::Linear, 0.0f});
        manager.getOrCreateLane("V1", otherPlug).addPoint(
            AutomationPoint{0, 0.75f, AutomationCurveType::Hold, 0.0f});
        manager.publishSnapshot();

        Snap::Snapshot left;
        expect(Snap::capture(manager, targets, left));
        expectEquals((int)left.lanes.size(), 3);
        const auto initialState = manager.getState();

        beginTest("Store snapshot A as project ValueTree with exact VST3 IDs and curve families");
        const auto savedA = Snap::serializeSnapshot(left);
        expect(savedA.hasType("AutomationMultiSnapshot"));
        expectEquals(savedA.getNumChildren(), 3);
        Snap::Snapshot reloadedA;
        expect(Snap::deserializeSnapshot(savedA, reloadedA));
        expectEquals((int)reloadedA.lanes.size(), 3);

        beginTest("Project Automation subtree persists exact A/B snapshots across reopen");
        juce::ValueTree memory("AutomationAdvancedMemories");
        auto aState = savedA.createCopy();
        aState.setProperty("slot", "A", nullptr);
        memory.addChild(aState, -1, nullptr);
        manager.setAdvancedMemories(memory);
        const auto withMemory = manager.getState();
        expect(withMemory.getChildWithName("AutomationAdvancedMemories").isValid());
        AutomationManagerCore openedWithMemory;
        openedWithMemory.restoreState(withMemory);
        auto storedMemory = openedWithMemory.getAdvancedMemories();
        expect(storedMemory.isValid());
        expectEquals(storedMemory.getNumChildren(), 1);
        Snap::Snapshot loadedAFromProject;
        expect(Snap::deserializeSnapshot(
            storedMemory.getChild(0), loadedAFromProject));
        expectEquals((int)loadedAFromProject.lanes.size(), 3);
        if (loadedAFromProject.lanes.size() == 3)
            expectEquals(loadedAFromProject.lanes[2].target.parameterId, plug);

        beginTest("Project reset erases session A/B and invalid slot replay is rejected");
        auto duplicate = memory.createCopy();
        duplicate.addChild(aState.createCopy(), -1, nullptr);
        openedWithMemory.setAdvancedMemories(duplicate);
        expectEquals(openedWithMemory.getAdvancedMemories().getNumChildren(), 1);
        openedWithMemory.clearAll();
        expect(!openedWithMemory.getAdvancedMemories().isValid());
        openedWithMemory.restoreState(withMemory);
        expect(openedWithMemory.getAdvancedMemories().isValid());
        openedWithMemory.restoreState(initialState);
        expect(!openedWithMemory.getAdvancedMemories().isValid());

        beginTest("Project loader rejects out-of-domain plugin values before accepting memories");
        auto malformedMemory = memory.createCopy();
        malformedMemory.getChild(0).getChild(2).getChild(0)
            .setProperty("value", 1.5f, nullptr); // VST3 domain [0, 1]
        openedWithMemory.setAdvancedMemories(malformedMemory);
        expect(!openedWithMemory.getAdvancedMemories().isValid());

        beginTest("Existing memories remain unchanged when an invalid project payload arrives");
        openedWithMemory.setAdvancedMemories(memory);
        const auto untouchedMemory = openedWithMemory.getAdvancedMemories().toXmlString();
        openedWithMemory.setAdvancedMemories(malformedMemory);
        expectEquals(openedWithMemory.getAdvancedMemories().toXmlString(),
                     untouchedMemory);

        beginTest("Project loader rejects corrupt curve, duplicate target and unordered times");
        malformedMemory = memory.createCopy();
        malformedMemory.getChild(0).getChild(0).getChild(0)
            .setProperty("curveToNextName", "INVALID_CURVE", nullptr);
        openedWithMemory.setAdvancedMemories(malformedMemory);
        expectEquals(openedWithMemory.getAdvancedMemories().toXmlString(),
                     untouchedMemory);

        malformedMemory = memory.createCopy();
        auto duplicatedTarget = malformedMemory.getChild(0).getChild(0).createCopy();
        malformedMemory.getChild(0).addChild(duplicatedTarget, -1, nullptr);
        openedWithMemory.setAdvancedMemories(malformedMemory);
        expectEquals(openedWithMemory.getAdvancedMemories().toXmlString(),
                     untouchedMemory);

        malformedMemory = memory.createCopy();
        malformedMemory.getChild(0).getChild(0).getChild(1)
            .setProperty("timeSamples", (juce::int64)0, nullptr);
        openedWithMemory.setAdvancedMemories(malformedMemory);
        expectEquals(openedWithMemory.getAdvancedMemories().toXmlString(),
                     untouchedMemory);

        beginTest("Restoring a corrupted project strips unsafe A/B data");
        auto damagedProject = withMemory.createCopy();
        auto attached = damagedProject.getChildWithName("AutomationAdvancedMemories");
        attached.getChild(0).getChild(0).getChild(0)
            .setProperty("value", std::numeric_limits<float>::quiet_NaN(), nullptr);
        AutomationManagerCore rejectedProject;
        rejectedProject.restoreState(damagedProject);
        expect(!rejectedProject.getAdvancedMemories().isValid());

        if (reloadedA.lanes.size() == 3)
        {
            expectEquals(reloadedA.lanes[2].target.parameterId, plug);
            expectWithinAbsoluteError(reloadedA.lanes[0].points[1].value, 1.8f, 0.00001f);
            expect(reloadedA.lanes[0].points[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(reloadedA.lanes[0].points[0].tensionToNext, -0.5f, 0.00001f);
        }

        beginTest("Project sample-rate change rescales A/B and live lane together");
        AutomationManagerCore rateChanged;
        rateChanged.restoreState(withMemory);
        rateChanged.scaleSamplePositions(2.0); // 48k sample => 96k
        const auto scaledA = rateChanged.getAdvancedMemories();
        expect(scaledA.isValid());
        expectEquals(scaledA.getNumChildren(), 1);
        if (scaledA.isValid() && scaledA.getNumChildren() == 1)
        {
            const auto savedVolume = scaledA.getChild(0).getChild(0);
            expectEquals((juce::int64)savedVolume.getChild(1).getProperty(
                             "timeSamples", (juce::int64)-1), (juce::int64)96000);
            const auto* liveVolume = rateChanged.findLane("V1", vol);
            expect(liveVolume != nullptr);
            if (liveVolume != nullptr && liveVolume->points.size() > 1)
                expectEquals((juce::int64)liveVolume->points[1].timeSamples,
                             (juce::int64)96000);
        }
        const auto scaledProject = rateChanged.getState();
        AutomationManagerCore reopenedAtNewRate;
        reopenedAtNewRate.restoreState(scaledProject);
        const auto rememberedNewRate = reopenedAtNewRate.getAdvancedMemories();
        expect(rememberedNewRate.isValid());
        if (rememberedNewRate.isValid() && rememberedNewRate.getNumChildren() == 1)
            expectEquals((juce::int64)rememberedNewRate.getChild(0).getChild(0)
                .getChild(1).getProperty("timeSamples", (juce::int64)-1),
                (juce::int64)96000);

        beginTest("Downsampling A/B knots uses deterministic last-point wins");
        auto closeKnots = memory.createCopy();
        closeKnots.getChild(0).getChild(0).getChild(1)
            .setProperty("timeSamples", (juce::int64)1, nullptr);
        juce::ValueTree collapsed;
        expect(DAW::AutomationAdvancedMemoryValidationCore::rescaleMemories(
            closeKnots, 0.1, collapsed));
        if (collapsed.isValid())
        {
            const auto target = collapsed.getChild(0).getChild(0);
            expectEquals(target.getNumChildren(), 1);
            if (target.getNumChildren() == 1)
                expectWithinAbsoluteError(
                    (float)target.getChild(0).getProperty("value", 0.0f),
                    1.8f, 0.00001f);
        }

        beginTest("Overflow must not write corrupt A/B sample positions");
        juce::ValueTree overflowed;
        expect(!DAW::AutomationAdvancedMemoryValidationCore::rescaleMemories(
            memory, 1.0e20, overflowed));
        expect(!overflowed.isValid());

        beginTest("Corrupted snapshot cannot partially restore or retain older memory");
        auto corrupted = savedA.createCopy();
        corrupted.getChild(2).getChild(0)
            .setProperty("value", 1.5f, nullptr); // VST3 normalized > 1
        expect(!Snap::deserializeSnapshot(corrupted, reloadedA));
        expect(!reloadedA.isValid());
        corrupted = savedA.createCopy();
        corrupted.getChild(0).getChild(0)
            .setProperty("curveToNextName", "INVALID_CURVE", nullptr);
        expect(!Snap::deserializeSnapshot(corrupted, reloadedA));
        expect(!reloadedA.isValid());
        corrupted = savedA.createCopy();
        corrupted.getChild(2)
            .setProperty("parameterId", plug + ".missing", nullptr);
        // A different fully qualified instance parameter identity still has
        // valid syntax; consumers must validate live host instance at apply.
        expect(Snap::deserializeSnapshot(corrupted, reloadedA));
        expectEquals(reloadedA.lanes[2].target.parameterId, plug + ".missing");

        beginTest("Capture B with changed volume, PAN and exact plugin instance");
        auto& secondVolume = manager.getOrCreateLane("V1", vol);
        secondVolume.points = {
            AutomationPoint{0, 1.8f, AutomationCurveType::Hold, -0.1f},
            AutomationPoint{48000, 0.2f, AutomationCurveType::Linear, 0.0f}
        };
        secondVolume.sortAndResolveDuplicates();
        auto& secondPan = manager.getOrCreateLane("V2", pan);
        secondPan.points = {
            AutomationPoint{0, 0.8f, AutomationCurveType::Hold, 0.0f},
            AutomationPoint{48000, -0.8f, AutomationCurveType::Linear, -0.3f}
        };
        secondPan.sortAndResolveDuplicates();
        auto& secondPlugin = manager.getOrCreateLane("V1", plug);
        secondPlugin.points = {
            AutomationPoint{0, 0.9f, AutomationCurveType::Hold, -0.4f},
            AutomationPoint{48000, 0.7f, AutomationCurveType::Smooth, 0.4f}
        };
        secondPlugin.sortAndResolveDuplicates();
        manager.publishSnapshot();

        Snap::Snapshot right;
        expect(Snap::capture(manager, targets, right));
        manager.restoreState(initialState);
        Snap::Plan half;
        const auto verifiedContinuous = [&](const Snap::Target& target)
        {
            return target.trackId == "V1" && target.parameterId == plug;
        };
        beginTest("VST3 A/B refuses unknown/discrete host targets without metadata");
        Snap::Plan noHostEvidence;
        expect(!Snap::planMorph(manager, left, right, 0.5f, noHostEvidence));
        expect(!noHostEvidence.isValid());
        expect(!Snap::planMorph(manager, left, right, 0.5f,
                                noHostEvidence, [](const Snap::Target&) { return false; }));
        expect(!noHostEvidence.isValid());

        beginTest("Verified exact continuous VST3 instance joins native A/B morph");
        expect(Snap::planMorph(manager, left, right, 0.5f, half, verifiedContinuous));
        expectEquals((int)half.changes.size(), 3);

        beginTest("Apply one transactional three-parameter morph");
        const auto beforeVersion = manager.getSnapshotVersion();
        expect(Snap::apply(manager, half, true));
        expectEquals((juce::int64)manager.getSnapshotVersion(),
                     (juce::int64)beforeVersion + 1);
        const auto* v = manager.findLane("V1", vol);
        const auto* p = manager.findLane("V2", pan);
        const auto* plugA = manager.findLane("V1", plug);
        const auto* plugB = manager.findLane("V1", otherPlug);
        expect(v != nullptr && p != nullptr && plugA != nullptr && plugB != nullptr);
        if (v != nullptr && p != nullptr && plugA != nullptr && plugB != nullptr)
        {
            expectWithinAbsoluteError(v->getValueAtSample(0, -1.0f), 1.0f, 0.0001f);
            expectWithinAbsoluteError(p->getValueAtSample(0, -1.0f), 0.0f, 0.0001f);
            expectWithinAbsoluteError(plugA->getValueAtSample(0, -1.0f), 0.5f, 0.0001f);
            expectWithinAbsoluteError(plugB->getValueAtSample(0, -1.0f), 0.75f, 0.0001f);
        }

        beginTest("Undo exact three parameters, unrelated plugin never affected");
        expect(Snap::apply(manager, half, false));
        const auto* orig = manager.findLane("V1", vol);
        expect(orig != nullptr);
        if (orig != nullptr && orig->points.size() == 2)
        {
            expectWithinAbsoluteError(orig->points[1].value, 1.8f, 0.0001f);
            expect(orig->points[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(orig->points[0].tensionToNext, -0.5f, 0.0001f);
        }

        beginTest("B source must match every exact track+plugin instance");
        auto mismatched = right;
        mismatched.lanes[2].target.parameterId = otherPlug;
        Snap::Plan rejected = half;
        expect(!Snap::planMorph(manager, left, mismatched, 0.5f, rejected, verifiedContinuous));
        expect(!rejected.isValid());
        expect(!Snap::planMorph(manager, left, right,
                               std::numeric_limits<float>::quiet_NaN(), rejected,
                               verifiedContinuous));
        expect(!rejected.isValid());

        beginTest("Out-of-domain VST3/PAN snapshot data cannot silently clamp");
        auto wrongDomain = right;
        wrongDomain.lanes[2].points[0].value = 1.5f; // VST3 normalized 0..1
        expect(!Snap::planMorph(manager, left, wrongDomain, 0.5f, rejected, verifiedContinuous));
        expect(!rejected.isValid());
        wrongDomain = right;
        wrongDomain.lanes[1].points[0].value = -1.5f; // native pan -1..1
        expect(!Snap::planMorph(manager, left, wrongDomain, 0.5f, rejected, verifiedContinuous));
        expect(!rejected.isValid());

        beginTest("Tampered after-state is rejected before any parameter mutation");
        auto brokenPlan = half;
        brokenPlan.changes[2].after = half.changes[2].after.createCopy();
        brokenPlan.changes[2].after.getChild(0).setProperty("value", 1.5f, nullptr);
        const auto noMutationState = manager.getState().toXmlString();
        const auto noMutationVersion = manager.getSnapshotVersion();
        expect(!Snap::apply(manager, brokenPlan, true));
        expectEquals(manager.getState().toXmlString(), noMutationState);
        expect(manager.getSnapshotVersion() == noMutationVersion);

        beginTest("Conflicting user edit blocks entire morph/Undo");
        auto& changed = manager.getOrCreateLane("V2", pan);
        changed.addPoint(24000, 0.25f);
        manager.publishSnapshot();
        const auto stateBeforeConflict = manager.getState().toXmlString();
        expect(!Snap::apply(manager, half, true));
        expectEquals(manager.getState().toXmlString(), stateBeforeConflict);

        beginTest("Clip-owned parameter cannot join a multiparameter plan");
        manager.restoreState(initialState);
        DAW::AutomationClipRegionCore ownership;
        ownership.trackId = "V2";
        ownership.parameterId = pan;
        ownership.startSample = 0;
        ownership.lengthSamples = 48000;
        ownership.localPoints = {AutomationPoint{0, 0.1f}};
        manager.addClipRegion(ownership);
        Snap::Snapshot protectedSnap;
        expect(!Snap::capture(manager, targets, protectedSnap));
        expect(!protectedSnap.isValid());
        expect(!Snap::planMorph(manager, left, right, 0.5f, rejected, verifiedContinuous));
        expect(!rejected.isValid());

        beginTest("Endpoint B preserves native shape and values through project serialization");
        manager.restoreState(initialState);
        Snap::Plan endpoint;
        expect(Snap::planMorph(manager, left, right, 1.0f, endpoint, verifiedContinuous));
        expect(Snap::apply(manager, endpoint, true));
        AutomationManagerCore reopened;
        reopened.restoreState(manager.getState());
        const auto* finalPan = reopened.findLane("V2", pan);
        const auto* finalPlug = reopened.findLane("V1", plug);
        expect(finalPan != nullptr && finalPlug != nullptr);
        if (finalPan != nullptr && finalPlug != nullptr)
        {
            expectWithinAbsoluteError(finalPan->points[1].value, -0.8f, 0.0001f);
            expect(finalPan->points[0].curveToNext == AutomationCurveType::Hold);
            expectWithinAbsoluteError(finalPlug->points[0].value, 0.9f, 0.0001f);
            expect(finalPlug->points[1].curveToNext == AutomationCurveType::Smooth);
        }
    }
};
static AutomationV2MultiParameterSnapshotTests automationV2MultiParameterSnapshotTests;

/** Controlled VST3 metadata is explicit here, never assumed by a parameter name.
 *  Host-side integration must query stepCount for the exact plugin instance.
 */
class AutomationV2MacroMappingTests final : public juce::UnitTest
{
public:
    AutomationV2MacroMappingTests()
        : juce::UnitTest("automation.v2-macro-multiparam-transaction.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationMacroMappingCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using M = AutomationMacroMappingCore;

        AutomationManagerCore manager;
        const juce::String vol = AutomationLaneCore::trackVolumeParameterId;
        const juce::String pan = AutomationLaneCore::trackPanParameterId;
        const juce::String vst = "plugin.1.instance-6cf.cutoff";
        const juce::String otherVst = "plugin.1.instance-999.cutoff";
        manager.getOrCreateLane("voice", vol).addPoint(0, 0.80f);
        manager.getOrCreateLane("voice", pan).addPoint(0, 0.0f);
        manager.getOrCreateLane("voice", vst).addPoint(0, 0.15f);
        manager.getOrCreateLane("voice", otherVst).addPoint(0, 0.70f);
        manager.publishSnapshot();

        std::vector<AutomationPoint> macro {
            {0, 0.0f, AutomationCurveType::Smooth, -0.4f},
            {12000, 0.5f, AutomationCurveType::Hold, 0.7f},
            {48000, 1.0f, AutomationCurveType::Linear, 0.0f}
        };
        std::vector<M::Binding> bindings {
            {{"voice", vol}, 0.50f, 1.50f, false, false},
            {{"voice", pan}, -1.0f, 1.0f, true, false},
            {{"voice", vst}, 0.20f, 0.80f, false, true}
        };

        beginTest("Compile native + individually verified continuous VST3 macro offline");
        M::Plan plan;
        expect(M::compile(manager, macro, bindings, plan));
        expectEquals((int)plan.changes.size(), 3);
        expectWithinAbsoluteError(
            manager.findLane("voice", vol)->points[0].value, 0.80f, 0.00001f);

        beginTest("All targets apply at one snapshot revision; another VST3 untouched");
        const auto revision = manager.getSnapshotVersion();
        expect(M::apply(manager, plan, true));
        expectEquals((juce::int64)manager.getSnapshotVersion(),
                     (juce::int64)revision + 1);
        const auto* volume = manager.findLane("voice", vol);
        const auto* panning = manager.findLane("voice", pan);
        const auto* cutoff = manager.findLane("voice", vst);
        const auto* untouched = manager.findLane("voice", otherVst);
        expect(volume != nullptr && panning != nullptr
               && cutoff != nullptr && untouched != nullptr);
        if (volume != nullptr && panning != nullptr
            && cutoff != nullptr && untouched != nullptr)
        {
            expectEquals((int)volume->points.size(), 3);
            expectWithinAbsoluteError(volume->points[0].value, 0.5f, 0.00001f);
            expectWithinAbsoluteError(volume->points[1].value, 1.0f, 0.00001f);
            expectWithinAbsoluteError(volume->points[2].value, 1.5f, 0.00001f);
            expectWithinAbsoluteError(panning->points[0].value, 1.0f, 0.00001f);
            expectWithinAbsoluteError(panning->points[2].value, -1.0f, 0.00001f);
            expectWithinAbsoluteError(cutoff->points[0].value, 0.2f, 0.00001f);
            expectWithinAbsoluteError(cutoff->points[2].value, 0.8f, 0.00001f);
            expectWithinAbsoluteError(untouched->points[0].value, 0.7f, 0.00001f);
            expect(volume->points[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(volume->points[1].tensionToNext, 0.7f, 0.00001f);
        }

        beginTest("Undo restores all original curves and survives save/reopen");
        expect(M::apply(manager, plan, false));
        const auto* restoredVol = manager.findLane("voice", vol);
        expect(restoredVol != nullptr);
        if (restoredVol != nullptr)
            expectWithinAbsoluteError(restoredVol->points[0].value, 0.8f, 0.00001f);
        expect(M::apply(manager, plan, true));
        AutomationManagerCore reopened;
        reopened.restoreState(manager.getState());
        const auto* savedCutoff = reopened.findLane("voice", vst);
        expect(savedCutoff != nullptr);
        if (savedCutoff != nullptr && savedCutoff->points.size() == 3)
        {
            expectWithinAbsoluteError(savedCutoff->points[2].value, 0.8f, 0.00001f);
            expect(savedCutoff->points[0].curveToNext == AutomationCurveType::Smooth);
        }
        expect(M::apply(manager, plan, false));

        beginTest("Never assume an unknown or stepped VST3 parameter is continuous");
        auto unverified = bindings;
        unverified[2].pluginIsVerifiedContinuous = false;
        M::Plan rejected = plan;
        expect(!M::compile(manager, macro, unverified, rejected));
        expect(!rejected.isValid());
        expect(!M::compile(manager, macro, {bindings[0], bindings[0]}, rejected));
        expect(!rejected.isValid());

        beginTest("Reject out-of-domain ranges and corrupt macro instead of clipping");
        auto invalidRange = bindings;
        invalidRange[1].minimum = -2.0f;
        expect(!M::compile(manager, macro, invalidRange, rejected));
        expect(!rejected.isValid());
        auto invalidValues = macro;
        invalidValues[1].value = 1.2f;
        expect(!M::compile(manager, invalidValues, bindings, rejected));
        expect(!rejected.isValid());
        invalidValues = macro;
        invalidValues[0].value = std::numeric_limits<float>::quiet_NaN();
        expect(!M::compile(manager, invalidValues, bindings, rejected));
        expect(!rejected.isValid());

        beginTest("Project macro saves curve and target ranges but not trusted VST3 status");
        M::Configuration definition;
        definition.name = "Vocal Lift";
        definition.controlPoints = macro;
        definition.bindings = bindings;
        const auto persisted = M::serializeConfiguration(definition);
        expect(persisted.hasType("AutomationMacro"));
        expectEquals(persisted.getNumChildren(), 4);
        M::Configuration loadedConfig;
        expect(M::deserializeConfiguration(persisted, loadedConfig));
        expectEquals(loadedConfig.name, juce::String("Vocal Lift"));
        expectEquals((int)loadedConfig.bindings.size(), 3);
        expectEquals((int)loadedConfig.controlPoints.size(), 3);
        if (loadedConfig.bindings.size() == 3 && loadedConfig.controlPoints.size() == 3)
        {
            expect(!loadedConfig.bindings[2].pluginIsVerifiedContinuous);
            expectEquals(loadedConfig.bindings[2].target.parameterId, vst);
            expectWithinAbsoluteError(loadedConfig.bindings[1].minimum, -1.0f, 0.00001f);
            expect(loadedConfig.bindings[1].inverted);
            expect(loadedConfig.controlPoints[0].curveToNext == AutomationCurveType::Smooth);
            expectWithinAbsoluteError(loadedConfig.controlPoints[1].tensionToNext,
                                      0.7f, 0.00001f);
            M::Plan requireReverification;
            expect(!M::compile(manager, loadedConfig.controlPoints,
                               loadedConfig.bindings, requireReverification));
            expect(!requireReverification.isValid());
            loadedConfig.bindings[2].pluginIsVerifiedContinuous = true;
            expect(M::compile(manager, loadedConfig.controlPoints,
                              loadedConfig.bindings, requireReverification));
        }

        beginTest("Malformed macro project data is rejected transactionally");
        auto damaged = persisted.createCopy();
        damaged.getChild(0).getChild(0).setProperty(
            "value", std::numeric_limits<float>::quiet_NaN(), nullptr);
        expect(!M::deserializeConfiguration(damaged, loadedConfig));
        expect(loadedConfig.bindings.empty());
        damaged = persisted.createCopy();
        damaged.getChild(3).setProperty("parameterId", vol, nullptr); // duplicate target
        expect(!M::deserializeConfiguration(damaged, loadedConfig));
        expect(loadedConfig.bindings.empty());
        damaged = persisted.createCopy();
        damaged.getChild(0).getChild(0).setProperty("curve", "CORRUPT", nullptr);
        expect(!M::deserializeConfiguration(damaged, loadedConfig));
        expect(loadedConfig.bindings.empty());

        beginTest("Clip ownership, bypass or absent lane fails whole macro");
        DAW::AutomationClipRegionCore owned;
        owned.trackId = "voice";
        owned.parameterId = pan;
        owned.startSample = 0;
        owned.lengthSamples = 48000;
        owned.localPoints = {AutomationPoint{0, 0.1f}};
        manager.addClipRegion(owned);
        expect(!M::compile(manager, macro, bindings, rejected));
        expect(!rejected.isValid());
        expect(manager.deleteClipRegion(owned.regionId));
        manager.setLaneEnabled("voice", vst, false);
        expect(!M::compile(manager, macro, bindings, rejected));
        expect(!rejected.isValid());
        manager.setLaneEnabled("voice", vst, true);

        beginTest("Conflict after plan creation prevents partial replay");
        expect(M::compile(manager, macro, bindings, plan));
        manager.getOrCreateLane("voice", pan).addPoint(24000, 0.25f);
        manager.publishSnapshot();
        const auto protectedBefore = manager.getState().toXmlString();
        const auto protectedVersion = manager.getSnapshotVersion();
        expect(!M::apply(manager, plan, true));
        expectEquals(manager.getState().toXmlString(), protectedBefore);
        expect(manager.getSnapshotVersion() == protectedVersion);
    }
};
static AutomationV2MacroMappingTests automationV2MacroMappingTests;


/**
 * This test exercises the exact host metadata / identity preflight used by
 * the producer GUI. The plugin processor and its format are checked by the
 * host before these metadata objects are passed to the verifier.
 */
class AutomationV2Vst3MacroLiveTargetTests final : public juce::UnitTest
{
public:
    AutomationV2Vst3MacroLiveTargetTests()
        : juce::UnitTest("automation.v2-vst3-macro-exact-live-target.v1",
                          "APEX.Diagnostics") {}

    void runTest() override
    {
        using Guard = DAW::AutomationMacroVst3TargetCore;
        using M = DAW::AutomationMacroMappingCore;
        DAW::AutomationManagerCore manager;
        const juce::String key = "plugin.0.instance-A.cutoff";
        const juce::String other = "plugin.0.instance-B.cutoff";
        const juce::String discreteKey = "plugin.0.instance-A.enabled";
        manager.getOrCreateLane("vocal", key).addPoint(0, 0.25f);
        manager.getOrCreateLane("vocal", other).addPoint(0, 0.81f);
        manager.getOrCreateLane("vocal", discreteKey).addPoint(0, 0.0f);
        manager.getOrCreateLane("vocal", "plugin.0.instance-A.quantized").addPoint(0, 0.5f);
        manager.publishSnapshot();

        juce::AudioParameterFloat cutoff(
            juce::ParameterID{"cutoff", 1}, "Cutoff",
            juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f);
        juce::AudioParameterBool enabled(
            juce::ParameterID{"enabled", 1}, "Enabled", false);
        juce::AudioParameterFloat quantized(
            juce::ParameterID{"quantized", 1}, "Quantized",
            juce::NormalisableRange<float>{0.0f, 1.0f, 0.25f}, 0.5f);
        juce::Array<juce::AudioProcessorParameter*> params;
        params.add(&cutoff);
        params.add(&enabled);
        params.add(&quantized);

        DAW::LastTouchedPluginParameter touched;
        touched.trackId = "vocal";
        touched.pluginSlotIndex = 0;
        touched.pluginInstanceId = "instance-A";
        touched.parameterId = "cutoff";
        M::Binding checked;

        beginTest("The live exact track, slot, UUID and stable ID enable continuous VST3 macro");
        expect(Guard::verify(touched, "vocal", 0, "instance-A",
                             params, manager, checked));
        expect(checked.pluginIsVerifiedContinuous);
        expectEquals(checked.target.parameterId, key);
        const std::vector<DAW::AutomationPoint> macro {
            {0, 0.0f, DAW::AutomationCurveType::Smooth, -0.3f},
            {24000, 1.0f, DAW::AutomationCurveType::Hold, 0.6f}
        };
        M::Plan plan;
        expect(M::compile(manager, macro, {checked}, plan));
        expect(M::apply(manager, plan, true));
        const auto* target = manager.findLane("vocal", key);
        const auto* foreign = manager.findLane("vocal", other);
        expect(target != nullptr && foreign != nullptr);
        if (target && foreign)
        {
            expectWithinAbsoluteError(target->points.back().value, 1.0f, 0.00001f);
            expectWithinAbsoluteError(foreign->points.front().value, 0.81f, 0.00001f);
            expect(target->points.front().curveToNext == DAW::AutomationCurveType::Smooth);
        }
        expect(M::apply(manager, plan, false));

        beginTest("Same display parameter in a different instance must never pass");
        expect(!Guard::verify(touched, "vocal", 0, "instance-B",
                              params, manager, checked));
        expect(!checked.pluginIsVerifiedContinuous);
        expect(!Guard::verify(touched, "another-track", 0, "instance-A",
                              params, manager, checked));
        expect(!Guard::verify(touched, "vocal", 1, "instance-A",
                              params, manager, checked));

        beginTest("Quantized float cannot impersonate continuous VST3 range");
        expect(quantized.getNumSteps()
            != juce::AudioProcessorParameter::getDefaultNumParameterSteps());
        touched.parameterId = "quantized";
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));

        beginTest("VST3 switch is discrete; missing and ambiguous IDs rejected");
        touched.parameterId = "enabled";
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));
        touched.parameterId = "not-found";
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));
        touched.parameterId = "cutoff";
        params.add(&cutoff); // corrupt host: duplicate stable identity
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));
        params.removeLast();

        beginTest("Clip ownership and lane bypass reject the whole target");
        DAW::AutomationClipRegionCore owned;
        owned.trackId = "vocal";
        owned.parameterId = key;
        owned.startSample = 0;
        owned.lengthSamples = 48000;
        owned.localPoints = {DAW::AutomationPoint{0, 0.5f}};
        manager.addClipRegion(owned);
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));
        expect(manager.deleteClipRegion(owned.regionId));
        manager.setLaneEnabled("vocal", key, false);
        expect(!Guard::verify(touched, "vocal", 0, "instance-A",
                              params, manager, checked));
        manager.setLaneEnabled("vocal", key, true);
        expect(Guard::verify(touched, "vocal", 0, "instance-A",
                             params, manager, checked));
    }
};
static AutomationV2Vst3MacroLiveTargetTests automationV2Vst3MacroLiveTargetTests;

class AutomationV2NativeMacroSelectionTests final : public juce::UnitTest
{
public:
    AutomationV2NativeMacroSelectionTests()
        : juce::UnitTest("automation.v2-native-macro-multitrack-volume-pan.v1",
                          "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationManagerCore;
        using DAW::AutomationMacroMappingCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;
        using M = AutomationMacroMappingCore;

        const juce::String volume = AutomationLaneCore::trackVolumeParameterId;
        const juce::String pan = AutomationLaneCore::trackPanParameterId;
        AutomationManagerCore manager;
        for (const auto& track : { juce::String("lead"), juce::String("double") })
        {
            manager.getOrCreateLane(track, volume).addPoint(0, 0.7f);
            manager.getOrCreateLane(track, pan).addPoint(0, -0.2f);
        }
        manager.getOrCreateLane("unrelated", pan).addPoint(0, 0.7f);
        manager.publishSnapshot();

        const std::vector<AutomationPoint> normalized{
            {0, 0.0f, AutomationCurveType::Smooth, -0.5f},
            {24000, 0.5f, AutomationCurveType::Hold, 0.7f},
            {48000, 1.0f, AutomationCurveType::Linear, 0.0f}
        };
        const std::vector<M::Binding> group{
            {{"lead", volume}, 0.0f, 2.0f},
            {{"lead", pan}, -1.0f, 1.0f},
            {{"double", volume}, 0.0f, 2.0f},
            {{"double", pan}, -1.0f, 1.0f}
        };

        beginTest("Explicit two-track native volume+pan macros compile to four lanes");
        M::Plan edit;
        expect(M::compile(manager, normalized, group, edit));
        expectEquals((int)edit.changes.size(), 4);
        const auto original = manager.getState().toXmlString();
        const auto oldVersion = manager.getSnapshotVersion();

        beginTest("All four selected lanes commit together exactly once");
        expect(M::apply(manager, edit, true));
        expectEquals((juce::int64)manager.getSnapshotVersion(),
                     (juce::int64)oldVersion + 1);
        for (const auto& track : { juce::String("lead"), juce::String("double") })
        {
            const auto* v = manager.findLane(track, volume);
            const auto* p = manager.findLane(track, pan);
            expect(v != nullptr && p != nullptr);
            if (v != nullptr && p != nullptr)
            {
                expectEquals((int)v->points.size(), 3);
                expectEquals((int)p->points.size(), 3);
                expectWithinAbsoluteError(v->points[1].value, 1.0f, 0.00001f);
                expectWithinAbsoluteError(p->points[0].value, -1.0f, 0.00001f);
                expectWithinAbsoluteError(p->points[2].value, 1.0f, 0.00001f);
                expect(v->points[0].curveToNext == AutomationCurveType::Smooth);
                expectWithinAbsoluteError(p->points[1].tensionToNext,
                                          0.7f, 0.00001f);
            }
        }
        const auto* outside = manager.findLane("unrelated", pan);
        expect(outside != nullptr);
        if (outside != nullptr)
            expectWithinAbsoluteError(outside->points[0].value, 0.7f, 0.00001f);

        beginTest("Global Undo returns exact four lanes and no unrelated edit");
        expect(M::apply(manager, edit, false));
        expectEquals(manager.getState().toXmlString(), original);

        beginTest("Missing one selected native lane must abort entire macro");
        auto invalidGroup = group;
        invalidGroup[3].target.trackId = "missing";
        M::Plan rejected;
        expect(!M::compile(manager, normalized, invalidGroup, rejected));
        expect(!rejected.isValid());

        beginTest("A clip-owned PAN lane blocks all targets, not just its track");
        DAW::AutomationClipRegionCore clipOwned;
        clipOwned.trackId = "double";
        clipOwned.parameterId = pan;
        clipOwned.startSample = 0;
        clipOwned.lengthSamples = 48000;
        clipOwned.localPoints = {AutomationPoint{0, 0.3f}};
        manager.addClipRegion(clipOwned);
        const auto beforeReject = manager.getState().toXmlString();
        expect(!M::compile(manager, normalized, group, rejected));
        expect(!rejected.isValid());
        expectEquals(manager.getState().toXmlString(), beforeReject);
    }
};
static AutomationV2NativeMacroSelectionTests automationV2NativeMacroSelectionTests;

class AutomationV2StateValidationTests final : public juce::UnitTest
{
public:
    AutomationV2StateValidationTests()
        : juce::UnitTest("automation.v2-undo-state-strict-bounded.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::AutomationLaneStateValidationCore;
        using DAW::AutomationLaneCore;
        using DAW::AutomationPoint;
        using DAW::AutomationCurveType;

        const juce::String pan = AutomationLaneCore::trackPanParameterId;
        const juce::String volume = AutomationLaneCore::trackVolumeParameterId;
        const juce::String vst = "plugin.1.my-exact-instance.gain";

        beginTest("Well-formed native PAN ValueTree roundtrips under point cap");
        AutomationLaneCore lane("exact-track", pan);
        lane.addPoint(AutomationPoint{0, -0.75f, AutomationCurveType::Smooth, 0.8f});
        lane.addPoint(AutomationPoint{48000, 0.50f, AutomationCurveType::Hold, -0.3f});
        const auto state = lane.getState();
        expect(AutomationLaneStateValidationCore::valid(state, pan, 2));
        expect(!AutomationLaneStateValidationCore::valid(state, pan, 1));
        expect(!AutomationLaneStateValidationCore::valid(state, volume, 2));

        beginTest("Out-of-domain levels, NaN, tension and reordered knots are rejected");
        auto bad = state.createCopy();
        bad.getChild(0).setProperty("value", -2.0f, nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));
        bad = state.createCopy();
        bad.getChild(0).setProperty("value",
            std::numeric_limits<float>::quiet_NaN(), nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));
        bad = state.createCopy();
        bad.getChild(1).setProperty("tensionToNext", 1.5f, nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));
        bad = state.createCopy();
        bad.getChild(1).setProperty("timeSamples", (juce::int64)0, nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));

        beginTest("Corrupt curve names, bypass and fabricated point layouts are rejected");
        bad = state.createCopy();
        bad.getChild(0).setProperty("curveToNextName", "UNKNOWN_CURVE_FAMILY", nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));
        bad = state.createCopy();
        bad.setProperty("enabled", false, nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));
        bad = state.createCopy();
        bad.getChild(0).removeProperty("timeSamples", nullptr);
        expect(!AutomationLaneStateValidationCore::valid(bad, pan, 2));

        beginTest("VST3 instance target checks normalized range without affecting other hosts");
        AutomationLaneCore plugin("exact-track", vst);
        plugin.addPoint(AutomationPoint{0, 0.7f});
        expect(AutomationLaneStateValidationCore::valid(plugin.getState(), vst, 1));
        auto tooHigh = plugin.getState().createCopy();
        tooHigh.getChild(0).setProperty("value", 1.5f, nullptr);
        expect(!AutomationLaneStateValidationCore::valid(tooHigh, vst, 1));
        expect(!AutomationLaneStateValidationCore::valid(plugin.getState(),
            "plugin.1.different-instance.gain", 1));
    }
};
static AutomationV2StateValidationTests automationV2StateValidationTests;

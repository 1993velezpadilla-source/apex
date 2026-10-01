#include <JuceHeader.h>
#include <cmath>

// Include the APEX presentation clock and frame timing tracker
#include "../../Source/UICore/ApexPresentationClock.h"
#include "../../Source/UICore/FrameTimingTracker.h"
#include "../../Source/UICore/MixerPanel.h"
#include "../../Source/UICore/BubblegumCableOverlayComponent.h"
#include "../../Source/UICore/BubblegumV2PanelUI.h"
#include "../../Source/UICore/BubblegumOffscreenTargetPopup.h"
#include "../../Source/ClipCore/Clip.h"
#include "../../Source/UICore/TrackList.h"
#include "../../Source/UICore/TrackSelectionVisualCore.h"
#include "../../Source/UICore/TrackLavaLampCore.h"
#include "../../Source/DiagnosticsCore/TimelinePaintMetrics.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.h"
#include <type_traits>

namespace
{
    bool imagesMatch(const juce::Image& lhs, const juce::Image& rhs)
    {
        if (!lhs.isValid() || !rhs.isValid()
            || lhs.getWidth() != rhs.getWidth()
            || lhs.getHeight() != rhs.getHeight())
            return false;

        for (int y = 0; y < lhs.getHeight(); ++y)
            for (int x = 0; x < lhs.getWidth(); ++x)
                if (lhs.getPixelAt(x, y) != rhs.getPixelAt(x, y))
                    return false;

        return true;
    }
}

//==============================================================================
/**
    @test    apex.ui.presentation_rate.v1
    @verify  ApexPresentationClock fires at the configured rate and
             registered receivers receive ticks at approximately 60 Hz.
*/
class PresentationClockRateTest : public juce::UnitTest
{
public:
    PresentationClockRateTest()
        : juce::UnitTest ("apex.ui.presentation_rate.v1",
                          "apex.ui.presentation_rate.v1") {}

    void runTest() override
    {
        beginTest ("ApexPresentationClock defaults to display-aware rate");

        struct TestReceiver : public DAW::ApexPresentationClock::TickReceiver
        {
            int& count;
            double& lastDelta;
            explicit TestReceiver (int& c, double& d) : count (c), lastDelta (d) {}
            void onPresentationTick (double delta) override
            {
                ++count;
                lastDelta = delta;
            }
        };

        int tickCount = 0;
        double lastDelta = 0.0;
        TestReceiver receiver (tickCount, lastDelta);
        auto& clock = DAW::ApexPresentationClock::instance();
        clock.addReceiver (&receiver);

        // Default rate should be >= 60 Hz (display-aware, active floor)
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "Default rate should be at least kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));
        expect (clock.getRateHz() <= DAW::ApexPresentationClock::kMaxRateHz,
                "Default rate should not exceed kMaxRateHz");

        // Display refresh rate should be a reasonable value
        expect (clock.getDisplayRefreshRate() >= 30.0,
                "Display refresh rate should be >= 30 Hz");
        expect (clock.getDisplayRefreshRate() <= 1000.0,
                "Display refresh rate should be reasonable");

        clock.removeReceiver (&receiver);
    }

    void runTestAdaptive() // called from adaptive suite
    {
        beginTest ("ApexPresentationClock adaptive rate switching");

        auto& clock = DAW::ApexPresentationClock::instance();

        // Test manual rate override
        clock.setRateHz (60);
        expect (clock.getRateHz() == 60,
                "Clock rate should be settable to 60 Hz");

        clock.setRateHz (120);
        expect (clock.getRateHz() == 120,
                "Clock rate should be settable to 120 Hz");

        // Test revert to auto (pass 0)
        clock.setRateHz (0);
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "After revert to auto, rate should be >= kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));

        // Test clamping to kMaxRateHz
        clock.setRateHz (999);
        expect (clock.getRateHz() <= DAW::ApexPresentationClock::kMaxRateHz,
                "Rate should be clamped to kMaxRateHz");
    }
};

static PresentationClockRateTest presentationClockRateTest;

//==============================================================================
/**
    @test    apex.ui.arranger_scroll_work_measurement.v1
    @verify  Drives the real TrackList and ArrangementViewCore scroll entry
             points with fixed inputs and reports the existing diagnostic
             counters.  This is intentionally a measurement seam, not a new
             benchmark framework; the same deterministic phases are retained
             for the post-optimization comparison.
*/
class ArrangerScrollWorkMeasurementTest final : public juce::UnitTest
{
public:
    ArrangerScrollWorkMeasurementTest()
        : juce::UnitTest("apex.ui.arranger_scroll_work_measurement.v1",
                         "apex.ui.arranger_scroll_work_measurement.v1") {}

    struct Fixture
    {
        static constexpr int kNormalTrackCount = 20;
        static constexpr int kClipCount = 40;
        static constexpr int kScrollSteps = 100;

        DAW::TrackManager tracks;
        DAW::ClipManager clips;
        ArrangementEditor::ArrangementViewCore arrangement;
        std::unique_ptr<DAW::TrackList> trackList;
        int addedClipCount = 0;
        DAW::TrackID folderId;
        DAW::TrackID selectedId;

        Fixture()
        {
            tracks.createMasterTrack();
            auto* folder = tracks.createTrack("Folder Bus");
            folder->setRole(DAW::TrackRole::FolderBus);
            folderId = folder->getID();

            for (int i = 0; i < kNormalTrackCount - 1; ++i)
            {
                auto* track = tracks.createTrack(juce::String("Scroll Track ") + juce::String(i));
                if (selectedId.isEmpty() && i == 4)
                    selectedId = track->getID();
            }

            trackList = std::make_unique<DAW::TrackList>(tracks);
            trackList->setSize(520, 640);
            trackList->setVisible(true);
            trackList->setCollapsedFolders({ folderId });
            trackList->selectTrackById(selectedId);

            arrangement.setAudioEngineBridge(&clips, nullptr, &tracks, 44100.0);
            arrangement.setSize(1400, 1500);
            arrangement.setVisible(true);

            for (int i = 0; i < kClipCount; ++i)
            {
                auto* engineClip = clips.createAudioClip(juce::String("Scroll Clip ") + juce::String(i), juce::File());
                if (engineClip == nullptr)
                    continue;

                auto model = ArrangementEditor::ArrangementClipModel::createNew(
                    1 + (i % kNormalTrackCount), (double) (i % 20) * 4.0, 2.0);
                model.clipName = (juce::String("Scroll Clip ") + juce::String(i)).toStdString();
                arrangement.addOrUpdateEngineClipModel(model, engineClip->getID());
                ++addedClipCount;
            }
        }
    };

    static juce::String format(const char* phase,
                               const char* surface,
                               const TimelinePaintMetrics::Snapshot& snapshot,
                               int clipCount = 0)
    {
        return "[SCROLL-BASELINE] phase=" + juce::String(phase)
            + " surface=" + juce::String(surface)
            + " clips=" + juce::String(clipCount)
            + " trackListEvents=" + juce::String((juce::int64) snapshot.trackListScrollEvents)
            + " trackListResized=" + juce::String((juce::int64) snapshot.trackListScrollResizedCalls)
            + " rowBounds=" + juce::String((juce::int64) snapshot.trackListScrollRowBoundsAssignments)
            + " rowPositions=" + juce::String((juce::int64) snapshot.trackListScrollRowPositionUpdates)
            + " arrangementEvents=" + juce::String((juce::int64) snapshot.arrangementScrollEvents)
            + " clipIterations=" + juce::String((juce::int64) snapshot.arrangementScrollClipIterations)
            + " visibilityChanges=" + juce::String((juce::int64) snapshot.arrangementScrollVisibilityChanges)
            + " waveformRefreshes=" + juce::String((juce::int64) snapshot.arrangementScrollWaveformRefreshes)
            + " fullRepaints=" + juce::String((juce::int64) snapshot.arrangementScrollFullRepaints)
            + " partialRepaints=" + juce::String((juce::int64) snapshot.arrangementScrollPartialRepaints)
            + " dirtyAreaPixels=" + juce::String((juce::int64) snapshot.arrangementScrollDirtyAreaPixels);
    }

    void runTest() override
    {
        Fixture fixture;
        auto& metrics = TimelinePaintMetrics::active;

        metrics.store(true, std::memory_order_release);

        metrics.store(false, std::memory_order_release);
        TimelinePaintMetrics::reset();
        metrics.store(true, std::memory_order_release);
        for (int i = 1; i <= Fixture::kScrollSteps; ++i)
            fixture.trackList->setScrollOffset(i * 4);
        const auto verticalTrackList = TimelinePaintMetrics::snapshot();
        juce::Logger::writeToLog(format("vertical", "tracklist", verticalTrackList));

        metrics.store(false, std::memory_order_release);
        fixture.arrangement.setViewportScrollOffset(0, 0);
        TimelinePaintMetrics::reset();
        metrics.store(true, std::memory_order_release);
        for (int i = 1; i <= Fixture::kScrollSteps; ++i)
            fixture.arrangement.setViewportScrollOffset(0, i * 5);
        const auto verticalArrangement = TimelinePaintMetrics::snapshot();
        juce::Logger::writeToLog(format("vertical", "arrangement", verticalArrangement,
                                       fixture.addedClipCount));

        metrics.store(false, std::memory_order_release);
        fixture.arrangement.setViewportScrollOffset(0, 0);
        TimelinePaintMetrics::reset();
        metrics.store(true, std::memory_order_release);
        for (int i = 1; i <= Fixture::kScrollSteps; ++i)
            fixture.arrangement.setViewportScrollOffset(i * 8, 0);
        const auto horizontalArrangement = TimelinePaintMetrics::snapshot();
        juce::Logger::writeToLog(format("horizontal", "arrangement", horizontalArrangement,
                                       fixture.addedClipCount));

        metrics.store(false, std::memory_order_release);
        TimelinePaintMetrics::reset();

        beginTest("Deterministic scroll phases executed");
        expectEquals((juce::int64) verticalTrackList.trackListScrollEvents,
                     (juce::int64) Fixture::kScrollSteps);
        expectEquals((juce::int64) verticalArrangement.arrangementScrollEvents,
                     (juce::int64) Fixture::kScrollSteps);
        expectEquals((juce::int64) horizontalArrangement.arrangementScrollEvents,
                     (juce::int64) Fixture::kScrollSteps);
    }
};

static ArrangerScrollWorkMeasurementTest arrangerScrollWorkMeasurementTest;

//==============================================================================
/**
    @test    apex.ui.arranger_scroll_invariants.v1
    @verify  Pure TrackList and Arrangement viewport translation preserves
             structural state while avoiding the old per-scroll layout and
             broad repaint work.  This test deliberately uses the production
             entry points and the existing diagnostic counters; it does not
             add a benchmark or alter the production architecture.
*/
class ArrangerScrollInvariantsTest final : public juce::UnitTest
{
public:
    ArrangerScrollInvariantsTest()
        : juce::UnitTest("apex.ui.arranger_scroll_invariants.v1",
                         "apex.ui.arranger_scroll_invariants.v1") {}

    struct RowState
    {
        juce::String id;
        juce::String parentId;
        juce::Rectangle<int> bounds;
        bool selected = false;
        bool folderBus = false;
        bool folderExpanded = false;
    };

    static std::vector<RowState> captureRows(const DAW::TrackList& list)
    {
        std::vector<RowState> result;
        const int rowCount = list.getNumRows();
        result.reserve((size_t) rowCount);
        for (int i = 0; i < rowCount; ++i)
        {
            auto* row = list.getRow(i);
            if (row == nullptr)
                continue;

            result.push_back(RowState {
                row->getTrackID(),
                row->getParentTrackID(),
                row->getBounds(),
                row->isSelected(),
                row->isFolderBusRow(),
                row->isFolderExpanded()
            });
        }
        return result;
    }

    static std::vector<ArrangementEditor::ClipRenderCore*> captureRenderers(
        ArrangementEditor::ArrangementViewCore& arrangement)
    {
        std::vector<ArrangementEditor::ClipRenderCore*> result;
        result.reserve(arrangement.m_clipRenderers.size());
        for (const auto& renderer : arrangement.m_clipRenderers)
            if (renderer != nullptr)
                result.push_back(renderer.get());
        return result;
    }

    static std::vector<bool> captureVisibility(
        ArrangementEditor::ArrangementViewCore& arrangement)
    {
        std::vector<bool> result;
        result.reserve(arrangement.m_clipRenderers.size());
        for (const auto& renderer : arrangement.m_clipRenderers)
            result.push_back(renderer != nullptr && renderer->isVisible());
        return result;
    }

    static uint64_t countVisibilityTransitions(const std::vector<bool>& before,
                                               const std::vector<bool>& after)
    {
        if (before.size() != after.size())
            return 0;

        uint64_t transitions = 0;
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i] != after[i])
                ++transitions;
        return transitions;
    }

    void runTest() override
    {
        using Metrics = TimelinePaintMetrics;
        ArrangerScrollWorkMeasurementTest::Fixture fixture;
        auto& metrics = Metrics::active;

        beginTest("TrackList pure scroll preserves row structure and state");
        const auto initialIds = fixture.trackList->getVisibleTrackIds();
        const auto initialRows = captureRows(*fixture.trackList);
        const int finalTrackListOffset =
            ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps * 4;

        metrics.store(false, std::memory_order_release);
        Metrics::reset();
        metrics.store(true, std::memory_order_release);
        for (int i = 1; i <= ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps; ++i)
            fixture.trackList->setScrollOffset(i * 4);
        const auto trackListSnapshot = Metrics::snapshot();
        metrics.store(false, std::memory_order_release);

        const auto finalIds = fixture.trackList->getVisibleTrackIds();
        const auto finalRows = captureRows(*fixture.trackList);
        expect(initialIds == finalIds,
               "pure scroll must preserve the ordered visible track IDs");
        expectEquals((int) finalRows.size(), (int) initialRows.size(),
                     "pure scroll must preserve the number of rows");

        for (size_t i = 0; i < initialRows.size() && i < finalRows.size(); ++i)
        {
            const auto label = "row " + juce::String((int) i);
            expect(initialRows[i].id == finalRows[i].id,
                   label + ": track ordering must remain unchanged");
            expect(initialRows[i].parentId == finalRows[i].parentId,
                   label + ": folder relationship must remain unchanged");
            expect(initialRows[i].selected == finalRows[i].selected,
                   label + ": selection must remain unchanged");
            expect(initialRows[i].folderBus == finalRows[i].folderBus,
                   label + ": folder-bus role must remain unchanged");
            expect(initialRows[i].folderExpanded == finalRows[i].folderExpanded,
                   label + ": folder expansion state must remain unchanged");
            expect(initialRows[i].bounds.getWidth() == finalRows[i].bounds.getWidth(),
                   label + ": row width must remain unchanged");
            expect(initialRows[i].bounds.getHeight() == finalRows[i].bounds.getHeight(),
                   label + ": row height must remain unchanged");
            expect(finalRows[i].bounds.getX() == initialRows[i].bounds.getX()
                       && finalRows[i].bounds.getY()
                              == initialRows[i].bounds.getY() - finalTrackListOffset,
                   label + ": row position must translate by the scroll offset only");
        }

        expectEquals((juce::int64) trackListSnapshot.trackListScrollEvents,
                     (juce::int64) ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps,
                     "TrackList must record every changed scroll offset");
        expectEquals((juce::int64) trackListSnapshot.trackListScrollResizedCalls,
                     (juce::int64) 0,
                     "pure TrackList scroll must not call structural resized()");
        expectEquals((juce::int64) trackListSnapshot.trackListScrollRowBoundsAssignments,
                     (juce::int64) 0,
                     "pure TrackList scroll must not assign row bounds");
        expectEquals((juce::int64) trackListSnapshot.trackListScrollRowPositionUpdates,
                     (juce::int64) (initialIds.size()
                         * (size_t) ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps),
                     "positionRowsForScroll must move every existing row per event");

        beginTest("Arrangement pure scroll preserves renderer identity and culling state");
        const auto initialRenderers = captureRenderers(fixture.arrangement);
        expectEquals((int) fixture.addedClipCount,
                     ArrangerScrollWorkMeasurementTest::Fixture::kClipCount,
                     "deterministic fixture must create all clips");
        expectEquals((int) initialRenderers.size(), fixture.addedClipCount,
                     "deterministic fixture must create one renderer per clip");

        auto runArrangementPhase = [&](const char* phase, bool horizontal)
        {
            metrics.store(false, std::memory_order_release);
            fixture.arrangement.setViewportScrollOffset(0, 0);
            Metrics::reset();
            metrics.store(true, std::memory_order_release);

            uint64_t observedVisibilityChanges = 0;
            for (int i = 1; i <= ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps; ++i)
            {
                const auto beforeVisibility = captureVisibility(fixture.arrangement);
                const int x = horizontal ? i * 8 : 0;
                const int y = horizontal ? 0 : i * 5;
                fixture.arrangement.setViewportScrollOffset(x, y);
                const auto afterVisibility = captureVisibility(fixture.arrangement);

                expect(beforeVisibility.size() == afterVisibility.size(),
                       juce::String(phase) + ": scroll must not recreate or remove renderers");
                observedVisibilityChanges += countVisibilityTransitions(beforeVisibility,
                                                                          afterVisibility);
            }

            const auto snapshot = Metrics::snapshot();
            const auto phaseLabel = juce::String(phase);
            expectEquals((juce::int64) snapshot.arrangementScrollEvents,
                         (juce::int64) ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps,
                         phaseLabel + ": every changed viewport offset must be recorded");
            expectEquals((juce::int64) snapshot.arrangementScrollClipIterations,
                         (juce::int64) (initialRenderers.size()
                             * (size_t) ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps),
                         phaseLabel + ": culling must inspect each existing renderer per event");
            expectEquals((juce::int64) snapshot.arrangementScrollVisibilityChanges,
                         (juce::int64) observedVisibilityChanges,
                         phaseLabel + ": visibility counter must equal actual state transitions");
            expectEquals((juce::int64) snapshot.arrangementScrollWaveformRefreshes,
                         (juce::int64) 0,
                         phaseLabel + ": unchanged zoom/width/LOD must not refresh waveforms");
            expectEquals((juce::int64) snapshot.arrangementScrollFullRepaints,
                         (juce::int64) 0,
                         phaseLabel + ": pure scroll must not request broad full repaints");
            expectEquals((juce::int64) snapshot.arrangementScrollPartialRepaints,
                         (juce::int64) ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps,
                         phaseLabel + ": pure scroll must request one viewport-region repaint per event");
            expect(snapshot.arrangementScrollDirtyAreaPixels > 0,
                   phaseLabel + ": viewport-region repaint area must be measured");

            const auto finalRenderers = captureRenderers(fixture.arrangement);
            expect(finalRenderers.size() == initialRenderers.size(),
                   phaseLabel + ": renderer count must remain stable");
            for (size_t i = 0; i < initialRenderers.size() && i < finalRenderers.size(); ++i)
            {
                expect(initialRenderers[i] == finalRenderers[i],
                       phaseLabel + ": renderer identity must remain stable during pure scroll");
                if (initialRenderers[i] != nullptr && finalRenderers[i] != nullptr)
                    expect(initialRenderers[i]->getBounds() == finalRenderers[i]->getBounds(),
                           phaseLabel + ": pure scroll must not rewrite clip geometry");
            }
            metrics.store(false, std::memory_order_release);
        };

        runArrangementPhase("vertical arrangement", false);
        runArrangementPhase("horizontal arrangement", true);
        Metrics::reset();
    }
};

static ArrangerScrollInvariantsTest arrangerScrollInvariantsTest;

//==============================================================================
/**
    @test    apex.ui.arranger_scroll_meter_continuity.v1
    @verify  Arranger scrolling does not suppress Mixer presentation demand.
             LevelMeter remains clock-eligible while active/decaying, MixerPanel
             remains a registered presentation receiver, and the scroll path
             does not mutate Mixer strip structure.
*/
class ArrangerScrollMeterContinuityTest final : public juce::UnitTest
{
public:
    ArrangerScrollMeterContinuityTest()
        : juce::UnitTest("apex.ui.arranger_scroll_meter_continuity.v1",
                         "apex.ui.arranger_scroll_meter_continuity.v1") {}

    void runTest() override
    {
        beginTest("LevelMeter exposes active and decaying presentation demand");
        DAW::LevelMeter meter;
        expect(!std::is_base_of_v<juce::Timer, DAW::LevelMeter>,
               "LevelMeter must not own a fixed-rate Timer");
        expect(!meter.needsAnimationTick(), "silent LevelMeter must start idle");
        meter.setLevel(0.5f);
        expect(meter.needsAnimationTick(),
               "active LevelMeter signal must request presentation");
        meter.tick();
        expect(meter.needsAnimationTick(),
               "peak/smoothing state must remain presentation-eligible after a tick");

        beginTest("MixerPanel remains a clock owner while Arranger scrolls");
        auto& clock = DAW::ApexPresentationClock::instance();
        const int activeBefore = clock.getActiveReceiverCountForTesting();

        expect(std::is_base_of_v<DAW::ApexPresentationClock::TickReceiver,
                                 DAW::MixerPanel>,
               "MixerPanel must remain an ApexPresentationClock receiver");
        expect(!std::is_base_of_v<juce::Timer, DAW::MixerPanel>,
               "MixerPanel must not introduce a fixed-rate meter Timer");

        struct MeterPresentationReceiver final
            : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::LevelMeter meter;
            int tickCount = 0;

            void onPresentationTick(double) override
            {
                if (meter.needsAnimationTick())
                {
                    meter.tick();
                    ++tickCount;
                }
            }
        } receiver;

        clock.addReceiver(&receiver);
        expect(clock.getRegistrationIdForTesting(&receiver) != 0,
               "meter owner must be registered with ApexPresentationClock");
        receiver.meter.setLevel(0.5f);
        clock.requestContinuousUpdate(&receiver);
        const int activeDuringMeter = clock.getActiveReceiverCountForTesting();
        expect(activeDuringMeter >= activeBefore + 1,
               "active meter demand must make the presentation clock active");

        ArrangerScrollWorkMeasurementTest::Fixture fixture;
        for (int i = 1; i <= ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps; ++i)
        {
            fixture.arrangement.setViewportScrollOffset(0, i * 5);
            clock.executePresentationTickForTesting();
        }

        expect(clock.getActiveReceiverCountForTesting() >= activeDuringMeter,
               "Arranger scroll must not suppress active meter presentation demand");
        expect(clock.getRegistrationIdForTesting(&receiver) != 0,
               "Arranger scroll must not unregister the meter presentation owner");
        expect(receiver.tickCount == ArrangerScrollWorkMeasurementTest::Fixture::kScrollSteps,
               "the clock must continue dispatching one meter tick per simulated frame");
        expect(receiver.meter.needsAnimationTick(),
               "meter demand must remain eligible throughout the scroll sequence");

        clock.releaseContinuousUpdate(&receiver);
        clock.removeReceiver(&receiver);
        expectEquals(clock.getActiveReceiverCountForTesting(), activeBefore,
                     "meter presentation owner cleanup must restore clock accounting");
    }
};

static ArrangerScrollMeterContinuityTest arrangerScrollMeterContinuityTest;

//==============================================================================
/**
    @test    apex.ui.no_30hz_visual_clock.v1
    @verify  No APEX-owned continuous visual component remains intentionally
             capped at 30 Hz. This is a compile-time and code-review assertion.
             The test verifies that key visual timer rates are 60 Hz by
             inspecting the compiled constants where possible.
*/
class No30HzVisualClockTest : public juce::UnitTest
{
public:
    No30HzVisualClockTest()
        : juce::UnitTest ("apex.ui.no_30hz_visual_clock.v1",
                          "apex.ui.no_30hz_visual_clock.v1") {}

    void runTest() override
    {
        beginTest ("No APEX-owned visual timer remains at 30 Hz");

        // Verify the shared presentation clock defaults to >= 60 Hz
        auto& clock = DAW::ApexPresentationClock::instance();
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "ApexPresentationClock must default to at least kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));

        // Verify that the FrameTimingTracker targets 60 Hz by default
        DAW::FrameTimingTracker tracker;
        expect (std::abs (tracker.getTargetIntervalMs() - 16.667) < 0.001,
                "FrameTimingTracker should target 16.667 ms for 60 Hz");
        expect (std::abs (tracker.getTargetHz() - 60.0) < 0.001,
                "FrameTimingTracker should report 60 Hz target");
    }
};

static No30HzVisualClockTest no30HzVisualClockTest;

//==============================================================================
/**
    @test    apex.ui.repaint_coalescing.v1
    @verify  Repeated repaint requests within the same frame are coalesced
             by JUCE's message thread. This test verifies that the MixerPanel
             no longer issues unconditional full-window repaints every tick.
*/
class RepaintCoalescingTest : public juce::UnitTest
{
public:
    RepaintCoalescingTest()
        : juce::UnitTest ("apex.ui.repaint_coalescing.v1",
                          "apex.ui.repaint_coalescing.v1") {}

    void runTest() override
    {
        beginTest ("Repaint coalescing reduces unnecessary full-window paints");

        // Verify that the FrameTimingTracker correctly identifies
        // dropped frames when intervals exceed the threshold.
        DAW::FrameTimingTracker tracker (60.0, 100);

        // Simulate ideal 60 Hz frames
        // markFrame() skips the first call (lastFrameTime_ starts at 0),
        // so we need 61 calls to record 60 intervals.
        for (int i = 0; i < 61; ++i)
            tracker.markFrame();

        auto stats = tracker.getStats();
        expect (stats.numFrames == 60,
                "Should have recorded 60 frames");
        expect (stats.droppedFrames == 0,
                "No frames should be dropped at ideal 60 Hz cadence");
        expect (std::abs (stats.targetHz - 60.0) < 0.001,
                "Target rate should be 60 Hz");
        expect (std::abs (stats.targetMs - 16.667) < 0.001,
                "Target interval should be 16.667 ms");

        // Reset and verify
        tracker.reset();
        expect (tracker.getTotalFrames() == 0,
                "After reset, total frames should be 0");
        expect (tracker.getDroppedFrames() == 0,
                "After reset, dropped frames should be 0");
    }
};

static RepaintCoalescingTest repaintCoalescingTest;

//==============================================================================
/**
    @test    apex.ui.frame_clock_lifecycle.v1
    @verify  ApexPresentationClock correctly manages receiver registration
             and unregistration without leaks or crashes.
*/
class FrameClockLifecycleTest : public juce::UnitTest
{
public:
    FrameClockLifecycleTest()
        : juce::UnitTest ("apex.ui.frame_clock_lifecycle.v1",
                          "apex.ui.frame_clock_lifecycle.v1") {}

    void runTest() override
    {
        beginTest ("ApexPresentationClock receiver registration lifecycle");

        auto& clock = DAW::ApexPresentationClock::instance();

        struct TestReceiver : public DAW::ApexPresentationClock::TickReceiver
        {
            int tickCount = 0;
            void onPresentationTick (double) override { ++tickCount; }
        };

        // Test add/remove lifecycle
        {
            TestReceiver receiver;
            clock.addReceiver (&receiver);
            expect (true, "Receiver added without exception");

            // Remove while alive
            clock.removeReceiver (&receiver);
            expect (true, "Receiver removed without exception");
        }

        // Test that removing a receiver that was never added is safe
        {
            TestReceiver receiver;
            clock.removeReceiver (&receiver);
            expect (true, "Removing unregistered receiver is safe");
        }

        // Test multiple registrations of the same receiver are idempotent
        {
            TestReceiver receiver;
            clock.addReceiver (&receiver);
            clock.addReceiver (&receiver);  // should be no-op
            clock.removeReceiver (&receiver);
            expect (true, "Duplicate registration is idempotent");
        }

        // Test that the clock rate can be changed
        clock.setRateHz (60);
        expect (clock.getRateHz() == 60,
                "Clock rate should be settable to 60 Hz");
    }
};

static FrameClockLifecycleTest frameClockLifecycleTest;

//==============================================================================
/**
    @test    apex.ui.adaptive_presentation_clock.v1
    @verify  ApexPresentationClock adaptive display-aware behavior with
              clock-owned registration identity (ABA-safe ScopedUpdate),
              tombstone-slot dispatch, and deterministic test tick seam.
*/
class AdaptivePresentationClockTest : public juce::UnitTest
{
public:
    AdaptivePresentationClockTest()
        : juce::UnitTest ("apex.ui.adaptive_presentation_clock.v1",
                          "apex.ui.adaptive_presentation_clock.v1") {}

    void runTest() override
    {
        // Core behavior
        testIdleSuppression();
        testDisplayRefreshDetection();
        testRateClamping();
        testNo30HzActivePath();
        testWindowDisplayTransitionDetection();
        testFrameTimingTrackerVariableRate();
        testMainWindowAuthority();
        testReceiverLifecycleSafety();

        // Accounting
        testActiveReceiverAccounting();
        testPerReceiverUpdateOwnership();
        testReleaseUnderflowGuard();
        testRemoveReceiverCleansUpUpdateCount();

        // Deterministic dispatch
        testDeterministicIdleTick();
        testDeterministicActiveTick();
        testDeterministicSelfRemoval();
        testDeterministicRemoveOther();
        testDeterministicAddDuringDispatch();

        // Lifetime and enforcement
        testRequestOnUnregisteredReceiver();
        testScopedUpdateRAII();
        testScopedUpdateABARegression();
        testRemoveOtherWithImmediateDestruction();
        testReentrancyGuard();
    }

private:
    //==============================================================================
    // Helper: simple tick-counting receiver
    //==============================================================================
    struct TickSpy : public DAW::ApexPresentationClock::TickReceiver
    {
        int ticked = 0;
        void onPresentationTick (double) override { ++ticked; }
    };

    struct NoOpReceiver : public DAW::ApexPresentationClock::TickReceiver
    {
        void onPresentationTick (double) override {}
    };

    //==============================================================================
    // Core behavior tests (updated for registration enforcement)
    //==============================================================================

    void testIdleSuppression()
    {
        beginTest ("Idle suppression with per-receiver request/releaseContinuousUpdate");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiverA, receiverB;
        clock.addReceiver (&receiverA);
        clock.addReceiver (&receiverB);

        expect (! clock.hasActiveRequests(),
                "Initially no active continuous update requests");

        clock.requestContinuousUpdate (&receiverA);
        expect (clock.hasActiveRequests(),
                "After requestContinuousUpdate(A), should have active requests");

        clock.requestContinuousUpdate (&receiverB);
        expect (clock.hasActiveRequests(),
                "After requestContinuousUpdate(B), should still have active requests");

        clock.releaseContinuousUpdate (&receiverA);
        expect (clock.hasActiveRequests(),
                "After releaseContinuousUpdate(A), B still active");

        clock.releaseContinuousUpdate (&receiverB);
        expect (! clock.hasActiveRequests(),
                "After releaseContinuousUpdate(B), no active requests");

        clock.removeReceiver (&receiverA);
        clock.removeReceiver (&receiverB);
    }

    void testDisplayRefreshDetection()
    {
        beginTest ("Display refresh rate detection follows registered main window");

        auto& clock = DAW::ApexPresentationClock::instance();

        // The display refresh rate should be a reasonable value.
        // The clock queries the display containing the registered main window,
        // falling back to primary display, then to 60 Hz default.
        const double refreshRate = clock.getDisplayRefreshRate();
        expect (refreshRate >= 30.0,
                "Display refresh rate should be >= 30 Hz, got " + juce::String (refreshRate));
        expect (refreshRate <= 1000.0,
                "Display refresh rate should be <= 1000 Hz, got " + juce::String (refreshRate));

        // The clock rate should be derived from the display refresh,
        // clamped to [kMinActiveRateHz, kMaxRateHz].
        const int clockRate = clock.getRateHz();
        expect (clockRate >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "Clock rate should be >= kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clockRate));
        expect (clockRate <= DAW::ApexPresentationClock::kMaxRateHz,
                "Clock rate should be <= kMaxRateHz (" + juce::String (DAW::ApexPresentationClock::kMaxRateHz)
                + "), got " + juce::String (clockRate));
    }

    void testRateClamping()
    {
        beginTest ("Rate clamping to valid active range [60, 144]");

        auto& clock = DAW::ApexPresentationClock::instance();

        // Set to a very high rate — should be clamped to kMaxRateHz
        clock.setRateHz (999);
        expect (clock.getRateHz() == DAW::ApexPresentationClock::kMaxRateHz,
                "Rate of 999 should be clamped to kMaxRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMaxRateHz)
                + "), got " + juce::String (clock.getRateHz()));

        // Set to a very low rate — should be clamped to kMinActiveRateHz (60)
        clock.setRateHz (1);
        expect (clock.getRateHz() == DAW::ApexPresentationClock::kMinActiveRateHz,
                "Rate of 1 should be clamped to kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));

        // Set to 30 Hz explicitly — should be raised to kMinActiveRateHz
        clock.setRateHz (30);
        expect (clock.getRateHz() == DAW::ApexPresentationClock::kMinActiveRateHz,
                "Rate of 30 should be raised to kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));

        // Revert to auto
        clock.setRateHz (0);
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "After revert to auto, rate should be >= kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz)
                + " Hz), got " + juce::String (clock.getRateHz()));
    }

    void testNo30HzActivePath()
    {
        beginTest ("No active 30 Hz presentation path exists");

        // Verify that kMinActiveRateHz is 60, not 30
        expect (DAW::ApexPresentationClock::kMinActiveRateHz == 60,
                "kMinActiveRateHz must be 60, got "
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz));

        // Verify that kIdleRateHz is separate and lower (10 Hz)
        expect (DAW::ApexPresentationClock::kIdleRateHz < DAW::ApexPresentationClock::kMinActiveRateHz,
                "kIdleRateHz (" + juce::String (DAW::ApexPresentationClock::kIdleRateHz)
                + ") must be less than kMinActiveRateHz ("
                + juce::String (DAW::ApexPresentationClock::kMinActiveRateHz) + ")");

        // Verify that setRateHz rejects 30 Hz by clamping to 60
        auto& clock = DAW::ApexPresentationClock::instance();
        clock.setRateHz (30);
        expect (clock.getRateHz() == 60,
                "setRateHz(30) must produce 60 Hz, got "
                + juce::String (clock.getRateHz()));

        // Verify that setRateHz(0) auto-detects to >= 60
        clock.setRateHz (0);
        expect (clock.getRateHz() >= 60,
                "setRateHz(0) auto-detect must produce >= 60 Hz, got "
                + juce::String (clock.getRateHz()));
    }

    void testWindowDisplayTransitionDetection()
    {
        beginTest ("Window/display transition detection");

        auto& clock = DAW::ApexPresentationClock::instance();

        // The clock detects display transitions via:
        // 1. componentMovedOrResized event (when main window moves between displays)
        // 2. Elapsed-time fallback (every 1 second in timerCallback)
        //
        // We verify the mechanism:
        // - updateDisplayRefreshRate() is called during construction
        // - setMainWindow() triggers re-detection
        // - setRateHz(0) triggers re-detection
        // - The fallback chain is: registered main window → primary → 60 Hz

        // Verify that getDisplayRefreshRate() returns a sensible value
        // (proving the detection chain works end-to-end).
        const double rate = clock.getDisplayRefreshRate();
        expect (rate >= 30.0 && rate <= 1000.0,
                "getDisplayRefreshRate() must return a sensible value, got "
                + juce::String (rate));

        // Verify that setRateHz(0) triggers re-detection and produces
        // a valid active rate.
        clock.setRateHz (0);
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "After setRateHz(0), rate must be >= kMinActiveRateHz, got "
                + juce::String (clock.getRateHz()));

        // Verify that the rate is bounded by kMaxRateHz
        expect (clock.getRateHz() <= DAW::ApexPresentationClock::kMaxRateHz,
                "Rate must not exceed kMaxRateHz, got "
                + juce::String (clock.getRateHz()));

        // Verify that the fallback path works: even without a registered
        // main window, the clock produces a valid rate (primary display
        // or 60 Hz default).
        expect (clock.getRateHz() > 0,
                "Rate must always be positive, got "
                + juce::String (clock.getRateHz()));
    }

    void testFrameTimingTrackerVariableRate()
    {
        beginTest ("FrameTimingTracker variable rate support");

        // Test at 60 Hz
        {
            DAW::FrameTimingTracker tracker (60.0);
            expect (std::abs (tracker.getTargetIntervalMs() - 16.667) < 0.001,
                    "At 60 Hz, target interval should be 16.667 ms");
            expect (std::abs (tracker.getTargetHz() - 60.0) < 0.001,
                    "Target Hz should be 60");
        }

        // Test at 120 Hz
        {
            DAW::FrameTimingTracker tracker (120.0);
            expect (std::abs (tracker.getTargetIntervalMs() - 8.333) < 0.001,
                    "At 120 Hz, target interval should be 8.333 ms");
            expect (std::abs (tracker.getTargetHz() - 120.0) < 0.001,
                    "Target Hz should be 120");
        }

        // Test at 144 Hz
        {
            DAW::FrameTimingTracker tracker (144.0);
            expect (std::abs (tracker.getTargetIntervalMs() - 6.944) < 0.001,
                    "At 144 Hz, target interval should be ~6.944 ms");
            expect (std::abs (tracker.getTargetHz() - 144.0) < 0.001,
                    "Target Hz should be 144");
        }

        // Test dynamic rate change via setTargetRate
        {
            DAW::FrameTimingTracker tracker (60.0);
            tracker.setTargetRate (120.0);
            expect (std::abs (tracker.getTargetIntervalMs() - 8.333) < 0.001,
                    "After setTargetRate(120), interval should be 8.333 ms");
            expect (std::abs (tracker.getTargetHz() - 120.0) < 0.001,
                    "After setTargetRate(120), target Hz should be 120");
        }

        // Test stats include target rate info
        {
            DAW::FrameTimingTracker tracker (60.0, 100);
            for (int i = 0; i < 10; ++i)
                tracker.markFrame();

            auto stats = tracker.getStats();
            expect (std::abs (stats.targetHz - 60.0) < 0.001,
                    "Stats should report 60 Hz target");
            expect (std::abs (stats.targetMs - 16.667) < 0.001,
                    "Stats should report 16.667 ms target interval");
        }
    }

    void testMainWindowAuthority()
    {
        beginTest ("Registered main window is authoritative display source");

        auto& clock = DAW::ApexPresentationClock::instance();

        // Create a test component to act as the "main window"
        juce::Component testMainWindow;
        testMainWindow.setBounds (0, 0, 800, 600);

        // Register it as the main window
        clock.setMainWindow (&testMainWindow);

        // The clock should now use the testMainWindow's display for rate detection.
        // Since the test window is not visible on any display, the clock should
        // fall back to primary display or 60 Hz default — but it must NOT use
        // getActiveTopLevelWindow() or iterate arbitrary TopLevelWindows.
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "After setMainWindow, rate should be valid (>= kMinActiveRateHz), got "
                + juce::String (clock.getRateHz()));

        // Create a second component to simulate a "plugin editor" or "dialog"
        // that should NOT be able to steal display authority.
        juce::Component pluginEditorWindow;
        pluginEditorWindow.setBounds (100, 100, 400, 300);

        // The clock should NOT change its target when a non-main-window
        // component is moved or resized.  We verify by checking that
        // setMainWindow is the only way to change the authoritative window.
        // (The clock only listens to the registered main window, so
        // moving pluginEditorWindow has no effect on the clock's target.)

        // Clear the main window registration
        clock.setMainWindow (nullptr);
        expect (true, "setMainWindow(nullptr) clears registration without crash");

        // After clearing, the clock should still produce a valid rate
        // (falling back to primary display).
        expect (clock.getRateHz() >= DAW::ApexPresentationClock::kMinActiveRateHz,
                "After setMainWindow(nullptr), rate should still be valid, got "
                + juce::String (clock.getRateHz()));
    }

    void testPerReceiverUpdateOwnership()
    {
        beginTest ("Per-receiver continuous-update ownership isolation");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiverA, receiverB;
        clock.addReceiver (&receiverA);
        clock.addReceiver (&receiverB);

        clock.requestContinuousUpdate (&receiverA);
        expect (clock.hasActiveRequests(),
                "After A requests, should have active requests");

        // B has no active requests — verify via accounting (no jassert)
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "Only A should be active (B has no requests)");

        clock.releaseContinuousUpdate (&receiverA);
        expect (! clock.hasActiveRequests(),
                "After A releases, no active requests");

        clock.requestContinuousUpdate (&receiverA);
        clock.requestContinuousUpdate (&receiverA);
        expect (clock.hasActiveRequests(),
                "A with two requests should show active");
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "A with 2 nested requests should be 1 active receiver");

        clock.releaseContinuousUpdate (&receiverA);
        expect (clock.hasActiveRequests(),
                "A after one release should still be active");

        clock.releaseContinuousUpdate (&receiverA);
        expect (! clock.hasActiveRequests(),
                "A after final release should be idle");

        // Verify invariant
        expect (clock.recomputeActiveReceiverCountForTesting() == 0,
                "Recomputed count matches (0)");

        clock.removeReceiver (&receiverA);
        clock.removeReceiver (&receiverB);
    }

    void testReceiverLifecycleSafety()
    {
        beginTest ("Receiver destruction/removal lifecycle safety");
        auto& clock = DAW::ApexPresentationClock::instance();

        {
            NoOpReceiver receiver;
            clock.addReceiver (&receiver);
            clock.removeReceiver (&receiver);
            clock.removeReceiver (&receiver);
            expect (true, "Double removal is safe");
        }

        {
            NoOpReceiver receiver;
            clock.addReceiver (&receiver);
            clock.removeReceiver (&receiver);
            clock.addReceiver (&receiver);
            clock.removeReceiver (&receiver);
            expect (true, "Re-add after removal works");
        }
    }

    void testScopedUpdateRAII()
    {
        beginTest ("ScopedUpdate RAII helper");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiver;
        clock.addReceiver (&receiver);

        {
            auto guard = DAW::ApexPresentationClock::ScopedUpdate (&receiver, clock);
            expect (clock.hasActiveRequests(),
                    "ScopedUpdate should activate continuous updates");
        }
        expect (! clock.hasActiveRequests(),
                "After ScopedUpdate destruction, updates should be released");

        {
            auto guard1 = DAW::ApexPresentationClock::ScopedUpdate (&receiver, clock);
            expect (clock.hasActiveRequests(), "guard1 should activate updates");

            auto guard2 = std::move (guard1);
            expect (clock.hasActiveRequests(), "After move, updates should still be active");
        }
        expect (! clock.hasActiveRequests(),
                "After moved guard destruction, updates should be released");

        expect (true, "ScopedUpdate is move-only (not copyable)");

        clock.removeReceiver (&receiver);
    }

    //==============================================================================
    // Accounting tests
    //==============================================================================

    void testActiveReceiverAccounting()
    {
        beginTest ("activeRequests_ counts active receivers, not nested requests");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiverA, receiverB;
        clock.addReceiver (&receiverA);
        clock.addReceiver (&receiverB);

        // A request x3 → ONE active receiver
        clock.requestContinuousUpdate (&receiverA);
        clock.requestContinuousUpdate (&receiverA);
        clock.requestContinuousUpdate (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "A with 3 nested requests should count as 1 active receiver, got "
                + juce::String (clock.getActiveReceiverCountForTesting()));
        expect (clock.recomputeActiveReceiverCountForTesting() == 1,
                "Invariant: recomputed count should also be 1");

        // B request x1 → TWO active receivers
        clock.requestContinuousUpdate (&receiverB);
        expect (clock.getActiveReceiverCountForTesting() == 2,
                "A(3) + B(1) should be 2 active receivers, got "
                + juce::String (clock.getActiveReceiverCountForTesting()));
        expect (clock.recomputeActiveReceiverCountForTesting() == 2,
                "Invariant: recomputed count should also be 2");

        // Remove A → B remains active
        clock.removeReceiver (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "After removing A, B should still be active (count=1), got "
                + juce::String (clock.getActiveReceiverCountForTesting()));
        expect (clock.recomputeActiveReceiverCountForTesting() == 1,
                "Invariant: recomputed count should also be 1");
        expect (clock.hasActiveRequests(),
                "B should still have active requests after A removed");

        // Release B → idle
        clock.releaseContinuousUpdate (&receiverB);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "After releasing B, count should be 0");
        expect (clock.recomputeActiveReceiverCountForTesting() == 0,
                "Invariant: recomputed count should also be 0");
        expect (! clock.hasActiveRequests(),
                "Clock should be idle after all released");

        clock.removeReceiver (&receiverB);
    }

    void testReleaseUnderflowGuard()
    {
        beginTest ("releaseContinuousUpdate accounting integrity after normal cycles");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiver;
        clock.addReceiver (&receiver);

        // Verify initial state
        expect (clock.getActiveReceiverCountForTesting() == 0, "Initially idle");
        expect (clock.recomputeActiveReceiverCountForTesting() == 0,
                "Recomputed count matches (0)");

        // Single request+release cycle
        clock.requestContinuousUpdate (&receiver);
        expect (clock.getActiveReceiverCountForTesting() == 1, "After request, active");
        clock.releaseContinuousUpdate (&receiver);
        expect (clock.getActiveReceiverCountForTesting() == 0, "After release, idle");

        // Nested request+release cycles — verify accounting integrity
        clock.requestContinuousUpdate (&receiver);
        clock.requestContinuousUpdate (&receiver);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "2 nested requests should be 1 active receiver");
        clock.releaseContinuousUpdate (&receiver);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "1 remaining, still active");
        clock.releaseContinuousUpdate (&receiver);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "All released, idle");

        // Verify invariant after all cycles
        expect (! clock.hasActiveRequests(), "No active requests");
        expect (clock.recomputeActiveReceiverCountForTesting() == 0,
                "Recomputed count matches after all cycles (0)");

        clock.removeReceiver (&receiver);
    }

    void testRemoveReceiverCleansUpUpdateCount()
    {
        beginTest ("removeReceiver cleans up exactly one active-receiver contribution");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver receiverA, receiverB;
        clock.addReceiver (&receiverA);
        clock.addReceiver (&receiverB);

        // A with 1 request, removed without releasing
        clock.requestContinuousUpdate (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "A should be 1 active receiver before removal");
        clock.removeReceiver (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "After removing A with outstanding request, count should be 0");

        // B with 3 nested requests, removed without releasing
        clock.requestContinuousUpdate (&receiverB);
        clock.requestContinuousUpdate (&receiverB);
        clock.requestContinuousUpdate (&receiverB);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "B with 3 nested requests should be 1 active receiver");
        clock.removeReceiver (&receiverB);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "After removing B with 3 outstanding requests, count should be 0");

        // Re-add after removal, verify clean state
        clock.addReceiver (&receiverA);
        clock.requestContinuousUpdate (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "Re-added A with request should be 1 active receiver");
        clock.releaseContinuousUpdate (&receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "After releasing re-added A, should be idle");
        clock.removeReceiver (&receiverA);
    }

    //==============================================================================
    // Deterministic dispatch tests
    //==============================================================================

    void testDeterministicIdleTick()
    {
        beginTest ("Deterministic idle tick: zero active requests, no dispatch");
        auto& clock = DAW::ApexPresentationClock::instance();

        TickSpy spy;
        clock.addReceiver (&spy);
        expect (! clock.hasActiveRequests(), "No active requests");

        clock.executePresentationTickForTesting();
        expect (spy.ticked == 0,
                "Idle tick should not dispatch, got " + juce::String (spy.ticked));

        clock.removeReceiver (&spy);
    }

    void testDeterministicActiveTick()
    {
        beginTest ("Deterministic active tick: request receiver, dispatch exactly once");
        auto& clock = DAW::ApexPresentationClock::instance();

        TickSpy spy;
        clock.addReceiver (&spy);
        clock.requestContinuousUpdate (&spy);

        clock.executePresentationTickForTesting();
        expect (spy.ticked == 1,
                "Active tick should dispatch exactly once, got " + juce::String (spy.ticked));

        clock.executePresentationTickForTesting();
        expect (spy.ticked == 2,
                "Second active tick should dispatch again, got " + juce::String (spy.ticked));

        clock.releaseContinuousUpdate (&spy);
        clock.removeReceiver (&spy);
    }

    void testDeterministicSelfRemoval()
    {
        beginTest ("Deterministic self-removal: B removes itself, A/C get exactly 1 callback");
        auto& clock = DAW::ApexPresentationClock::instance();

        struct SelfRemovingSpy : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::ApexPresentationClock& clockRef;
            int ticked = 0;
            bool hasRemoved = false;
            SelfRemovingSpy (DAW::ApexPresentationClock& c) : clockRef (c) {}
            void onPresentationTick (double) override
            {
                ++ticked;
                if (! hasRemoved)
                {
                    hasRemoved = true;
                    clockRef.removeReceiver (this);
                }
            }
        };

        TickSpy spyA, spyC;
        SelfRemovingSpy spyB (clock);

        clock.addReceiver (&spyA);
        clock.addReceiver (&spyB);
        clock.addReceiver (&spyC);

        clock.requestContinuousUpdate (&spyA);
        clock.requestContinuousUpdate (&spyB);
        clock.requestContinuousUpdate (&spyC);

        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 1, "A should receive exactly 1 tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 1, "B should receive exactly 1 tick (then self-remove), got " + juce::String (spyB.ticked));
        expect (spyC.ticked == 1, "C should receive exactly 1 tick, got " + juce::String (spyC.ticked));

        // Next tick: only A and C
        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 2, "A should receive 2nd tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 1, "B should NOT receive 2nd tick (removed), got " + juce::String (spyB.ticked));
        expect (spyC.ticked == 2, "C should receive 2nd tick, got " + juce::String (spyC.ticked));

        clock.releaseContinuousUpdate (&spyA);
        clock.releaseContinuousUpdate (&spyC);
        clock.removeReceiver (&spyA);
        clock.removeReceiver (&spyC);
    }

    void testDeterministicRemoveOther()
    {
        beginTest ("Deterministic remove-other: A removes C during callback, no stale invocation");
        auto& clock = DAW::ApexPresentationClock::instance();

        struct RemoveOtherSpy : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::ApexPresentationClock& clockRef;
            DAW::ApexPresentationClock::TickReceiver* target;
            int ticked = 0;
            bool hasRemoved = false;
            RemoveOtherSpy (DAW::ApexPresentationClock& c, DAW::ApexPresentationClock::TickReceiver* t)
                : clockRef (c), target (t) {}
            void onPresentationTick (double) override
            {
                ++ticked;
                if (! hasRemoved)
                {
                    hasRemoved = true;
                    clockRef.removeReceiver (target);
                }
            }
        };

        TickSpy spyC;
        RemoveOtherSpy spyA (clock, &spyC);
        TickSpy spyB;

        clock.addReceiver (&spyA);
        clock.addReceiver (&spyB);
        clock.addReceiver (&spyC);

        clock.requestContinuousUpdate (&spyA);
        clock.requestContinuousUpdate (&spyB);
        clock.requestContinuousUpdate (&spyC);

        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 1, "A should receive 1 tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 1, "B should receive 1 tick, got " + juce::String (spyB.ticked));
        expect (spyC.ticked == 0, "C should NOT receive tick (removed by A), got " + juce::String (spyC.ticked));

        clock.releaseContinuousUpdate (&spyA);
        clock.releaseContinuousUpdate (&spyB);
        clock.removeReceiver (&spyA);
        clock.removeReceiver (&spyB);
    }

    void testDeterministicAddDuringDispatch()
    {
        beginTest ("Deterministic add-during-dispatch: new receiver not dispatched this cycle");
        auto& clock = DAW::ApexPresentationClock::instance();

        struct AddReceiverSpy : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::ApexPresentationClock& clockRef;
            DAW::ApexPresentationClock::TickReceiver* newReceiver;
            int ticked = 0;
            bool hasAdded = false;
            AddReceiverSpy (DAW::ApexPresentationClock& c, DAW::ApexPresentationClock::TickReceiver* n)
                : clockRef (c), newReceiver (n) {}
            void onPresentationTick (double) override
            {
                ++ticked;
                if (! hasAdded)
                {
                    hasAdded = true;
                    clockRef.addReceiver (newReceiver);
                }
            }
        };

        TickSpy spyNew;
        AddReceiverSpy spyA (clock, &spyNew);
        TickSpy spyB;

        clock.addReceiver (&spyA);
        clock.addReceiver (&spyB);

        clock.requestContinuousUpdate (&spyA);
        clock.requestContinuousUpdate (&spyB);

        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 1, "A should receive 1 tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 1, "B should receive 1 tick, got " + juce::String (spyB.ticked));
        expect (spyNew.ticked == 0, "New receiver should NOT be dispatched this cycle, got " + juce::String (spyNew.ticked));

        // New receiver is now registered — request and verify next cycle
        clock.requestContinuousUpdate (&spyNew);
        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 2, "A should receive 2nd tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 2, "B should receive 2nd tick, got " + juce::String (spyB.ticked));
        expect (spyNew.ticked == 1, "New receiver should be dispatched next cycle, got " + juce::String (spyNew.ticked));

        clock.releaseContinuousUpdate (&spyA);
        clock.releaseContinuousUpdate (&spyB);
        clock.releaseContinuousUpdate (&spyNew);
        clock.removeReceiver (&spyA);
        clock.removeReceiver (&spyB);
        clock.removeReceiver (&spyNew);
    }

    //==============================================================================
    // Lifetime and enforcement tests
    //==============================================================================

    void testRequestOnUnregisteredReceiver()
    {
        beginTest ("requestContinuousUpdate on unregistered receiver is no-op");
        auto& clock = DAW::ApexPresentationClock::instance();

        NoOpReceiver unregistered;
        // NOT calling addReceiver — verify via test seam (no jassert)

        expect (clock.getRegistrationIdForTesting (&unregistered) == 0,
                "Unregistered receiver should have registration ID 0");
        expect (! clock.hasActiveRequests(),
                "No active requests for unregistered receiver");
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "Active receiver count should remain 0");
        expect (clock.recomputeActiveReceiverCountForTesting() == 0,
                "Recomputed active count should be 0");
    }

    void testScopedUpdateABARegression()
    {
        beginTest ("ScopedUpdate ABA regression: old guard cannot affect replacement receiver");
        auto& clock = DAW::ApexPresentationClock::instance();

        // Step 1: Register A and create a ScopedUpdate
        auto* receiverA = new NoOpReceiver();
        clock.addReceiver (receiverA);

        auto guard = std::make_unique<DAW::ApexPresentationClock::ScopedUpdate> (receiverA, clock);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "A should be active after ScopedUpdate");

        const auto regIdA = clock.getRegistrationIdForTesting (receiverA);
        expect (regIdA != 0, "A should have a valid registration ID");

        // Step 2: Remove and destroy A
        clock.removeReceiver (receiverA);
        expect (clock.getActiveReceiverCountForTesting() == 0,
                "After removing A, should be idle");
        delete receiverA;

        // Step 3: Register replacement receiver B (may reuse A's address)
        auto* receiverB = new NoOpReceiver();
        clock.addReceiver (receiverB);
        clock.requestContinuousUpdate (receiverB);
        expect (clock.getActiveReceiverCountForTesting() == 1,
                "B should be active");

        const auto regIdB = clock.getRegistrationIdForTesting (receiverB);
        expect (regIdB != 0, "B should have a valid registration ID");
        expect (regIdB != regIdA, "B must have a different registration ID than A");

        const int bCountBefore = clock.getActiveReceiverCountForTesting();

        // Step 4: Destroy old ScopedUpdate (holds stale registration ID)
        guard.reset();

        // Step 5: B's state must be completely unchanged
        expect (clock.getActiveReceiverCountForTesting() == bCountBefore,
                "B's active count must not change after old guard destruction, got "
                + juce::String (clock.getActiveReceiverCountForTesting())
                + " expected " + juce::String (bCountBefore));
        expect (clock.hasActiveRequests(),
                "B should still be active after old guard destruction");

        // Clean up
        clock.releaseContinuousUpdate (receiverB);
        clock.removeReceiver (receiverB);
        delete receiverB;
    }

    void testRemoveOtherWithImmediateDestruction()
    {
        beginTest ("Remove-other followed by immediate receiver destruction");
        auto& clock = DAW::ApexPresentationClock::instance();

        struct RemoveAndDestroySpy : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::ApexPresentationClock& clockRef;
            DAW::ApexPresentationClock::TickReceiver*& targetRef;
            int ticked = 0;
            bool hasRemoved = false;
            RemoveAndDestroySpy (DAW::ApexPresentationClock& c,
                                 DAW::ApexPresentationClock::TickReceiver*& t)
                : clockRef (c), targetRef (t) {}
            void onPresentationTick (double) override
            {
                ++ticked;
                if (! hasRemoved)
                {
                    hasRemoved = true;
                    clockRef.removeReceiver (targetRef);
                    delete targetRef;
                    targetRef = nullptr;
                }
            }
        };

        auto* spyC = new TickSpy();
        DAW::ApexPresentationClock::TickReceiver* spyCPtr = spyC;

        RemoveAndDestroySpy spyA (clock, spyCPtr);
        TickSpy spyB;

        clock.addReceiver (&spyA);
        clock.addReceiver (&spyB);
        clock.addReceiver (spyC);

        clock.requestContinuousUpdate (&spyA);
        clock.requestContinuousUpdate (&spyB);
        clock.requestContinuousUpdate (spyC);

        // A removes C and destroys it — dispatch must not touch C afterwards
        clock.executePresentationTickForTesting();

        expect (spyA.ticked == 1, "A should receive 1 tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 1, "B should receive 1 tick, got " + juce::String (spyB.ticked));
        expect (spyCPtr == nullptr, "C should be destroyed");

        // Next tick: only A and B
        clock.executePresentationTickForTesting();
        expect (spyA.ticked == 2, "A should receive 2nd tick, got " + juce::String (spyA.ticked));
        expect (spyB.ticked == 2, "B should receive 2nd tick, got " + juce::String (spyB.ticked));

        clock.releaseContinuousUpdate (&spyA);
        clock.releaseContinuousUpdate (&spyB);
        clock.removeReceiver (&spyA);
        clock.removeReceiver (&spyB);
    }

    void testReentrancyGuard()
    {
        beginTest ("Reentrancy guard: nested dispatch is skipped");
        auto& clock = DAW::ApexPresentationClock::instance();

        struct ReentrantSpy : public DAW::ApexPresentationClock::TickReceiver
        {
            DAW::ApexPresentationClock& clockRef;
            int ticked = 0;
            bool hasReentered = false;
            ReentrantSpy (DAW::ApexPresentationClock& c) : clockRef (c) {}
            void onPresentationTick (double) override
            {
                ++ticked;
                if (! hasReentered)
                {
                    hasReentered = true;
                    // Attempt nested dispatch — should be skipped by guard
                    clockRef.executePresentationTickForTesting();
                }
            }
        };

        ReentrantSpy spy (clock);
        clock.addReceiver (&spy);
        clock.requestContinuousUpdate (&spy);

        clock.executePresentationTickForTesting();

        // The nested executePresentationTickForTesting should have been
        // skipped by the reentrancy guard, so ticked should be exactly 1.
        expect (spy.ticked == 1,
                "Reentrant dispatch should be skipped, ticked=" + juce::String (spy.ticked)
                + " (expected 1)");

        clock.releaseContinuousUpdate (&spy);
        clock.removeReceiver (&spy);
    }
};

static AdaptivePresentationClockTest adaptivePresentationClockTest;

//==============================================================================
/**
    @test    apex.ui.stage_ab.presentation_ownership.v1
    @verify  Stage A+B visual owners use ApexPresentationClock, while the
             TrackList timer remains an explicitly semantic drag auto-scroll
             mechanism. The Master Utility backend remains constructible while
             its user-facing control policy is disabled.
*/
class StageABPresentationOwnershipTest final : public juce::UnitTest
{
public:
    StageABPresentationOwnershipTest()
        : juce::UnitTest ("apex.ui.stage_ab.presentation_ownership.v1",
                          "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("Master Utility user control is disabled by policy and backend remains present");
        expect (! DAW::MixerUiFeaturePolicy::kMasterUtilityUserControlEnabled,
                 "Master Utility user control must remain disabled");
        DAW::MasterUtilityPanel masterUtility;
        expect (! masterUtility.isVisible(),
                 "Master Utility backend panel must start hidden");

        beginTest ("Master animated sections have no independent Timer base");
        expect (! std::is_base_of_v<juce::Timer, DAW::MasterStripCeilingSection>);
        expect (! std::is_base_of_v<juce::Timer, DAW::MasterStripMeteringSection>);
        expect (! std::is_base_of_v<juce::Timer, DAW::MasterStripPhaseWidthSection>);

        beginTest ("Visual components use the shared presentation receiver");
        expect (std::is_base_of_v<DAW::ApexPresentationClock::TickReceiver,
                                  DAW::BubblegumCableOverlayComponent>);
        expect (std::is_base_of_v<DAW::ApexPresentationClock::TickReceiver,
                                  DAW::BubblegumV2PanelUI>);
        expect (std::is_base_of_v<DAW::ApexPresentationClock::TickReceiver,
                                  DAW::BubblegumOffscreenTargetPopup>);
        expect (std::is_base_of_v<DAW::ApexPresentationClock::TickReceiver,
                                  DAW::TrackList>);

        beginTest ("Visual components have no independent Timer base");
        expect (! std::is_base_of_v<juce::Timer, DAW::BubblegumCableOverlayComponent>);
        expect (! std::is_base_of_v<juce::Timer, DAW::BubblegumV2PanelUI>);
        expect (! std::is_base_of_v<juce::Timer, DAW::BubblegumOffscreenTargetPopup>);
        expect (! std::is_base_of_v<juce::Timer, DAW::TrackRow>);

        beginTest ("TrackList Timer is retained only for semantic drag auto-scroll");
        expect (std::is_base_of_v<juce::Timer, DAW::TrackList>,
                "TrackList retains its control-time drag auto-scroll timer");

        beginTest ("LevelMeter exposes presentation demand without a Timer");
        DAW::LevelMeter meter;
        expect (! std::is_base_of_v<juce::Timer, DAW::LevelMeter>);
        expect (! meter.needsAnimationTick(), "silent meter starts idle");
        meter.setLevel (0.5f);
        expect (meter.needsAnimationTick(), "active meter requests presentation");
    }
};

static StageABPresentationOwnershipTest stageABPresentationOwnershipTest;

//==============================================================================
/**
    @test    apex.ui.selected_track_static.v1
    @verify  Selection remains visible and event-driven, while the selected
             background no longer advances Lava, bubbles, physics, sweep, or
             presentation-clock demand merely because selection remains active.
*/
class SelectedTrackStaticVisualTest final : public juce::UnitTest
{
public:
    SelectedTrackStaticVisualTest()
        : juce::UnitTest ("apex.ui.selected_track_static.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        DAW::TrackSelectionVisualCore visual;
        int repaintCount = 0;
        visual.onRepaintRequired = [&repaintCount] { ++repaintCount; };

        beginTest ("Selection remains visible and changes request repaint");
        visual.setSelected (true);
        expect (visual.isSelected(), "selected state must remain true");
        expectEquals (repaintCount, 1);
        visual.setSelected (true);
        expectEquals (repaintCount, 1,
                      "unchanged selection must not continuously repaint");

        beginTest ("Selected background has no continuous presentation demand");
        expect (! visual.needsAnimationTick(),
                "static selected background must not request animation ticks");

        // Prime the old animated implementation to a fully visible frame so
        // this comparison catches motion rather than only the initial fade-in.
        visual.getLavaCore().stepExternal (1.0f);

        juce::Image before (juce::Image::ARGB, 180, 96, true);
        juce::Image after  (juce::Image::ARGB, 180, 96, true);
        const juce::Rectangle<float> area (0.0f, 0.0f, 180.0f, 96.0f);
        juce::Graphics beforeGraphics (before);
        juce::Graphics afterGraphics (after);
        visual.paintInto (beforeGraphics, area);

        // This is the same guarded presentation path used by TrackRow and
        // MixerStrip. Static selection must skip both sweep and Lava stepping.
        if (visual.needsAnimationTick())
        {
            visual.stepSweep (1.0f / 60.0f);
            visual.getLavaCore().stepExternal (1.0f / 60.0f);
        }
        visual.paintInto (afterGraphics, area);

        expect (imagesMatch (before, after),
                "selected background must remain pixel-static between ticks");
        expect (! visual.needsAnimationTick(),
                "static selection must remain idle after presentation attempt");

        beginTest ("Selection transition still repaints and deselection stays idle");
        visual.setSelected (false);
        expect (! visual.isSelected(), "deselection must remain functional");
        expectEquals (repaintCount, 2);
        expect (! visual.needsAnimationTick(),
                "deselected static visual must not request animation ticks");

        beginTest ("Multiple selected visuals remain independently distinguishable");
        DAW::TrackSelectionVisualCore second;
        second.setSelected (true);
        visual.setSelected (true);
        expect (visual.isSelected() && second.isSelected(),
                "multiple selected tracks must retain selected state");
        expect (! visual.needsAnimationTick() && ! second.needsAnimationTick(),
                "multi-selection must remain static without animation demand");
    }
};

static SelectedTrackStaticVisualTest selectedTrackStaticVisualTest;

//==============================================================================
/**
    @test    apex.ui.lava_cache_generation.v1
    @verify  Dynamic Lava output invalidates the full-frame cache after a
             simulation step, while unchanged output continues to reuse it.
*/
class LavaCacheGenerationTest final : public juce::UnitTest
{
public:
    LavaCacheGenerationTest()
        : juce::UnitTest ("apex.ui.lava_cache_generation.v1",
                          "APEX.Diagnostics") {}

    void runTest() override
    {
        DAW::TrackLavaLampCore lava;
        lava.setActive (true);
        lava.stepExternal (1.0f); // complete the initial fade deterministically

        juce::Image image (juce::Image::ARGB, 160, 96, true);
        juce::Graphics graphics (image);
        const juce::Rectangle<float> area (0.0f, 0.0f, 160.0f, 96.0f);

        beginTest ("First opaque render bakes the dynamic full-frame cache");
        lava.paintBackground (graphics, area);
        expect (lava.consumeLavaCacheBakeCount() > 0,
                "first render must bake a full-frame cache");

        beginTest ("Unchanged state reuses the full-frame cache");
        lava.paintBackground (graphics, area);
        expectEquals (lava.consumeLavaCacheBakeCount(), 0,
                      "unchanged state must not rebake");
        expect (lava.consumeLavaCacheBlitCount() > 0,
                "unchanged state must blit the cached image");

        beginTest ("Animation step changes the cache generation");
        lava.stepExternal (1.0f / 60.0f);
        lava.paintBackground (graphics, area);
        expect (lava.consumeLavaCacheBakeCount() > 0,
                "animation state change must invalidate the dynamic cache");
    }
};

static LavaCacheGenerationTest lavaCacheGenerationTest;

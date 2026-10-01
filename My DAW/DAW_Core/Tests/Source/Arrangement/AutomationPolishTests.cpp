#include <JuceHeader.h>
#include "../../../Source/AutomationCore/AutomationManagerCore.h"
#include "../../../Source/AutomationCore/AutomationUIHelper.h"
#include "../../../Source/AutomationCore/AutomationCurveEvalCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/AutomationUndoRedoCore.h"
#include <cmath>
#include <vector>

namespace
{
    bool pointsEqual(const std::vector<DAW::AutomationPoint>& a,
                     const std::vector<DAW::AutomationPoint>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].timeSamples != b[i].timeSamples)
                return false;
            if (std::abs(a[i].value - b[i].value) > 1.0e-6f)
                return false;
            if (a[i].curveToNext != b[i].curveToNext)
                return false;
            if (std::abs(a[i].tensionToNext - b[i].tensionToNext) > 1.0e-6f)
                return false;
        }
        return true;
    }

    bool hasUniqueTimes(const std::vector<DAW::AutomationPoint>& pts)
    {
        for (size_t i = 1; i < pts.size(); ++i)
            if (pts[i].timeSamples == pts[i - 1].timeSamples)
                return false;
        return true;
    }

    bool isSortedByTime(const std::vector<DAW::AutomationPoint>& pts)
    {
        for (size_t i = 1; i < pts.size(); ++i)
            if (pts[i].timeSamples < pts[i - 1].timeSamples)
                return false;
        return true;
    }
}

class AutomationPolishTests final : public juce::UnitTest
{
public:
    AutomationPolishTests()
        : juce::UnitTest("automation.polish.v1", "APEX.Arrangement")
    {
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        DAW::UndoManager undoManager;
        undoManager.clear();

        beginTest("A.interpolation-deterministic-and-bounded");
        testInterpolation();
        undoManager.clear();

        beginTest("B.tension-shapes-curve");
        testTension();
        undoManager.clear();

        beginTest("C.point-editing-collision");
        testPointEditing();
        undoManager.clear();

        beginTest("D.boundaries");
        testBoundaries();
        undoManager.clear();

        beginTest("E.drag-coalescing-one-undo-entry");
        testDragCoalescing();
        undoManager.clear();

        beginTest("F.clip-ownership");
        testClipOwnership();
        undoManager.clear();

        beginTest("G.plugin-identity");
        testPluginIdentity();
        undoManager.clear();

        beginTest("H.exact-undo-redo");
        testExactUndoRedo();
        undoManager.clear();

        beginTest("I.dense-automation");
        testDenseAutomation();
        undoManager.clear();
    }

private:
    void testInterpolation()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.2f);
        lane.addPoint(1000, 0.8f);

        // Exact point positions evaluate to the stored value.
        expectWithinAbsoluteError(lane.getValueAtSample(0, 0.0f), 0.2f, 1.0e-6f);
        expectWithinAbsoluteError(lane.getValueAtSample(1000, 0.0f), 0.8f, 1.0e-6f);

        // Between-point evaluation (linear midpoint).
        expectWithinAbsoluteError(lane.getValueAtSample(500, 0.0f), 0.5f, 1.0e-4f);

        // Repeated evaluation is deterministic.
        const float first = lane.getValueAtSample(333, 0.0f);
        bool deterministic = true;
        for (int i = 0; i < 100; ++i)
            if (lane.getValueAtSample(333, 0.0f) != first)
                deterministic = false;
        expect(deterministic, "repeated evaluation must be bit-identical");

        // No overshoot outside the legal value range between endpoints.
        float minV = 1.0f, maxV = 0.0f;
        for (int64_t s = 0; s <= 1000; s += 7)
        {
            const float v = lane.getValueAtSample(s, 0.0f);
            minV = juce::jmin(minV, v);
            maxV = juce::jmax(maxV, v);
        }
        expect(minV >= 0.2f - 1.0e-4f && maxV <= 0.8f + 1.0e-4f,
               "interpolation must stay bounded by the endpoint values");

        // Smooth curve midpoint is 0.5 (smoothstep), still bounded.
        lane.clear();
        lane.addPoint(0, 0.2f);
        lane.addPoint(1000, 0.8f);
        lane.setCurveToNext(0, DAW::AutomationCurveType::Smooth);
        expectWithinAbsoluteError(lane.getValueAtSample(500, 0.0f), 0.5f, 1.0e-3f);
        expect(lane.getValueAtSample(250, 0.0f) >= 0.2f - 1.0e-4f
               && lane.getValueAtSample(250, 0.0f) <= 0.8f + 1.0e-4f);
    }

    void testTension()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.0f);
        lane.addPoint(1000, 1.0f);

        const float neutral = lane.getValueAtSample(500, 0.0f);
        expectWithinAbsoluteError(neutral, 0.5f, 1.0e-4f);

        // Positive tension eases out (back-loaded): midpoint rises above 0.5.
        lane.setTensionToNext(0, 0.8f);
        const float easedOut = lane.getValueAtSample(500, 0.0f);
        expect(easedOut > neutral, "positive tension must push the midpoint up");

        // Negative tension eases in (front-loaded): midpoint drops below 0.5.
        lane.setTensionToNext(0, -0.8f);
        const float easedIn = lane.getValueAtSample(500, 0.0f);
        expect(easedIn < neutral, "negative tension must pull the midpoint down");

        // Tension is preserved through point moves.
        lane.setTensionToNext(0, 0.5f);
        helper.movePoint(track, param, 1, 1200, 1.0f);
        expectWithinAbsoluteError(lane.points[0].tensionToNext, 0.5f, 1.0e-6f);

        // Undo/Redo restores the exact tension.
        DAW::UndoManager undoManager;
        undoManager.clear();
        const float oldTension = lane.points[0].tensionToNext;
        undoManager.doAction(std::make_unique<DAW::SetTensionAction>(
            helper, track, param, 0, 0.3f, oldTension));
        expectWithinAbsoluteError(lane.points[0].tensionToNext, 0.3f, 1.0e-6f);
        undoManager.undo();
        expectWithinAbsoluteError(lane.points[0].tensionToNext, oldTension, 1.0e-6f);
        undoManager.redo();
        expectWithinAbsoluteError(lane.points[0].tensionToNext, 0.3f, 1.0e-6f);
        undoManager.clear();
    }

    void testPointEditing()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.1f);
        lane.addPoint(500, 0.5f);
        lane.addPoint(1000, 0.9f);
        expectEquals(lane.points.size(), (size_t)3);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));

        // Add a point at an existing time: last added wins, no duplicate.
        lane.addPoint(500, 0.7f);
        expectEquals(lane.points.size(), (size_t)3);
        expect(hasUniqueTimes(lane.points));
        bool found = false;
        for (const auto& p : lane.points)
            if (p.timeSamples == 500)
            {
                found = true;
                expectWithinAbsoluteError(p.value, 0.7f, 1.0e-6f);
            }
        expect(found, "the last-added point at a duplicate time must win");

        // Move a point to a new time.
        helper.movePoint(track, param, 1, 750, 0.6f);
        expectEquals(lane.points.size(), (size_t)3);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));
        expectEquals(lane.points[1].timeSamples, (int64_t)750);
        expectWithinAbsoluteError(lane.points[1].value, 0.6f, 1.0e-6f);

        // Force a timestamp collision: the moved point must win deterministically.
        helper.movePoint(track, param, 1, 1000, 0.6f);
        expectEquals(lane.points.size(), (size_t)2);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));
        bool at1000 = false;
        for (const auto& p : lane.points)
            if (p.timeSamples == 1000)
            {
                at1000 = true;
                expectWithinAbsoluteError(p.value, 0.6f, 1.0e-6f);
            }
        expect(at1000, "the moved point must win the collision");

        // Delete a point: remaining lane still evaluates cleanly.
        helper.removePoint(track, param, 0);
        expectEquals(lane.points.size(), (size_t)1);
        expectWithinAbsoluteError(lane.getValueAtSample(500, 0.0f), lane.points[0].value, 1.0e-6f);
    }

    void testBoundaries()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(100, 0.3f);
        lane.addPoint(900, 0.7f);

        // Before the first point: hold the first value.
        expectWithinAbsoluteError(lane.getValueAtSample(-50, 0.0f), 0.3f, 1.0e-6f);
        expectWithinAbsoluteError(lane.getValueAtSample(0, 0.0f), 0.3f, 1.0e-6f);
        expectWithinAbsoluteError(lane.getValueAtSample(99, 0.0f), 0.3f, 1.0e-6f);

        // At the first point.
        expectWithinAbsoluteError(lane.getValueAtSample(100, 0.0f), 0.3f, 1.0e-6f);

        // Between points.
        expectWithinAbsoluteError(lane.getValueAtSample(500, 0.0f), 0.5f, 1.0e-4f);

        // At the last point.
        expectWithinAbsoluteError(lane.getValueAtSample(900, 0.0f), 0.7f, 1.0e-6f);

        // After the last point: hold the last value.
        expectWithinAbsoluteError(lane.getValueAtSample(901, 0.0f), 0.7f, 1.0e-6f);
        expectWithinAbsoluteError(lane.getValueAtSample(2000, 0.0f), 0.7f, 1.0e-6f);

        // setPointExactTime clamps to >= 0 and keeps ordering/dedupe.
        helper.setPointExactTime(track, param, 0, -100);
        expectEquals(lane.points[0].timeSamples, (int64_t)0);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));
    }

    void testDragCoalescing()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.1f);
        lane.addPoint(500, 0.5f);
        lane.addPoint(1000, 0.9f);

        DAW::UndoManager undoManager;
        undoManager.clear();

        // Capture the before-state exactly as the UI does at drag start.
        const auto beforeState = lane.getState();
        const auto beforePoints = lane.points;

        // Simulate one continuous drag: many incremental movePoint calls.
        for (int i = 0; i < 20; ++i)
        {
            const int64_t t = 500 + i * 10;
            const float v = 0.5f + i * 0.01f;
            helper.movePoint(track, param, 1, t, v);
        }

        // One logical undo transaction for the whole gesture.
        undoManager.doAction(std::make_unique<DAW::MovePointAction>(
            helper, track, param, beforeState, lane.getState()));

        expectEquals(DAW::CommandManager::getInstance().getUndoCount(), 1,
                     "a drag must produce exactly ONE logical undo entry");

        // Undo restores the complete prior lane state.
        undoManager.undo();
        expect(pointsEqual(lane.points, beforePoints),
               "undo must restore the exact pre-drag lane state");

        // Redo restores the complete after state.
        undoManager.redo();
        expectEquals(lane.points[1].timeSamples, (int64_t)(500 + 19 * 10));
        expectWithinAbsoluteError(lane.points[1].value, 0.5f + 19 * 0.01f, 1.0e-6f);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));

        undoManager.clear();
    }

    void testClipOwnership()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String clipId = "clip-abc-123";
        const juce::String param = DAW::AutomationLaneCore::makeClipGainParameterId(clipId);

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.5f);
        lane.addPoint(1000, 1.0f);

        // The parameter ID resolves to the stable clip ID.
        expectEquals(DAW::AutomationLaneCore::tryExtractClipIdFromParameterId(param), clipId);

        // Edits through the existing automation APIs keep the lane identity.
        helper.movePoint(track, param, 1, 1200, 1.0f);
        helper.removePoint(track, param, 0);
        helper.addPoint(track, param, 200, 0.4f);
        expect(helper.findLane(track, param) != nullptr,
               "lane must remain associated with its clip parameter after edits");
        expectEquals(DAW::AutomationLaneCore::tryExtractClipIdFromParameterId(param), clipId);

        // captureClipAutomationState returns the lane for this clip.
        const auto snapshot = automation.captureClipAutomationState(clipId);
        expect(snapshot.isValid());
        bool foundLane = false;
        for (int i = 0; i < snapshot.getNumChildren(); ++i)
            if (snapshot.getChild(i).hasType("AutomationLane"))
                foundLane = true;
        expect(foundLane, "clip automation snapshot must contain the lane");
    }

    void testPluginIdentity()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");

        DAW::AutomationManagerCore::PluginParameterTarget targetA;
        targetA.trackId = track;
        targetA.pluginInstanceId = "inst-A";
        targetA.pluginSlotIndex = 0;
        targetA.parameterId = "cutoff";

        DAW::AutomationManagerCore::PluginParameterTarget targetB = targetA;
        targetB.pluginInstanceId = "inst-B";

        DAW::AutomationManagerCore::PluginParameterTarget targetC = targetA;
        targetC.pluginSlotIndex = 1;

        const auto idA = DAW::AutomationManagerCore::makePluginParameterId(targetA);
        const auto idB = DAW::AutomationManagerCore::makePluginParameterId(targetB);
        const auto idC = DAW::AutomationManagerCore::makePluginParameterId(targetC);

        expect(idA != idB, "different plugin instances must map to different lanes");
        expect(idA != idC, "different plugin slots must map to different lanes");
        expect(idB != idC);

        // Create all lanes first, then re-acquire references: getOrCreateLane may
        // reallocate the manager's lane vector, invalidating earlier references.
        // Once all lanes exist, getOrCreateLane returns the existing lane without
        // reallocating, so the re-acquired references stay valid.
        helper.getOrCreateLane(track, idA);
        helper.getOrCreateLane(track, idB);
        helper.getOrCreateLane(track, idC);

        auto& laneA = helper.getOrCreateLane(track, idA);
        auto& laneB = helper.getOrCreateLane(track, idB);
        auto& laneC = helper.getOrCreateLane(track, idC);

        laneA.addPoint(0, 0.1f);
        laneB.addPoint(0, 0.9f);
        laneC.addPoint(0, 0.5f);

        // Distinct lanes, no cross-instance contamination.
        expect(&laneA != &laneB);
        expect(&laneA != &laneC);
        expect(&laneB != &laneC);
        expect(helper.findLane(track, idA) == &laneA);
        expect(helper.findLane(track, idB) == &laneB);
        expect(helper.findLane(track, idC) == &laneC);
        expectWithinAbsoluteError(laneA.points[0].value, 0.1f, 1.0e-6f);
        expectWithinAbsoluteError(laneB.points[0].value, 0.9f, 1.0e-6f);
        expectWithinAbsoluteError(laneC.points[0].value, 0.5f, 1.0e-6f);
    }

    void testExactUndoRedo()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        lane.addPoint(0, 0.1f);
        lane.addPoint(500, 0.5f);
        lane.addPoint(1000, 0.9f);
        lane.setCurveToNext(1, DAW::AutomationCurveType::Smooth);
        lane.setTensionToNext(1, 0.4f);

        DAW::UndoManager undoManager;
        undoManager.clear();

        // --- Point move (snapshot-based) ---
        const auto beforeMove = lane.points;
        const auto beforeState = lane.getState();
        helper.movePoint(track, param, 1, 600, 0.6f);
        undoManager.doAction(std::make_unique<DAW::MovePointAction>(
            helper, track, param, beforeState, lane.getState()));
        undoManager.undo();
        expect(pointsEqual(lane.points, beforeMove),
               "move undo must restore the exact prior points");
        undoManager.redo();
        expectEquals(lane.points[1].timeSamples, (int64_t)600);
        expectWithinAbsoluteError(lane.points[1].value, 0.6f, 1.0e-6f);

        // --- Curve type edit ---
        const auto oldCurve = lane.points[1].curveToNext;
        undoManager.doAction(std::make_unique<DAW::SetCurveTypeAction>(
            helper, track, param, 1, DAW::AutomationCurveType::Hold, oldCurve));
        expect(lane.points[1].curveToNext == DAW::AutomationCurveType::Hold);
        undoManager.undo();
        expect(lane.points[1].curveToNext == oldCurve);
        undoManager.redo();
        expect(lane.points[1].curveToNext == DAW::AutomationCurveType::Hold);

        // --- Tension edit ---
        const auto oldTension = lane.points[1].tensionToNext;
        undoManager.doAction(std::make_unique<DAW::SetTensionAction>(
            helper, track, param, 1, -0.6f, oldTension));
        expectWithinAbsoluteError(lane.points[1].tensionToNext, -0.6f, 1.0e-6f);
        undoManager.undo();
        expectWithinAbsoluteError(lane.points[1].tensionToNext, oldTension, 1.0e-6f);
        undoManager.redo();
        expectWithinAbsoluteError(lane.points[1].tensionToNext, -0.6f, 1.0e-6f);

        // --- Point delete: undo restores the FULL point (curve + tension) ---
        const auto deletedPoint = lane.points[1];
        undoManager.doAction(std::make_unique<DAW::DeletePointAction>(
            helper, track, param, 1, deletedPoint));
        expectEquals(lane.points.size(), (size_t)2);
        undoManager.undo();
        expectEquals(lane.points.size(), (size_t)3);
        bool restored = false;
        for (const auto& p : lane.points)
            if (p.timeSamples == deletedPoint.timeSamples)
            {
                restored = true;
                expectWithinAbsoluteError(p.value, deletedPoint.value, 1.0e-6f);
                expect(p.curveToNext == deletedPoint.curveToNext,
                       "undo must restore the deleted point's curve type");
                expectWithinAbsoluteError(p.tensionToNext, deletedPoint.tensionToNext, 1.0e-6f);
            }
        expect(restored, "undo must restore the deleted point");
        undoManager.redo();
        expectEquals(lane.points.size(), (size_t)2);

        // --- Point insert: redo restores the FULL point ---
        DAW::AutomationPoint inserted;
        inserted.timeSamples = 750;
        inserted.value = 0.55f;
        inserted.curveToNext = DAW::AutomationCurveType::Exponential;
        inserted.tensionToNext = 0.3f;
        helper.addPoint(track, param, inserted);  // UI inserts first, then the action
        undoManager.doAction(std::make_unique<DAW::InsertPointAction>(
            helper, track, param, inserted));
        expectEquals(lane.points.size(), (size_t)3);
        undoManager.undo();
        expectEquals(lane.points.size(), (size_t)2);
        undoManager.redo();
        expectEquals(lane.points.size(), (size_t)3);
        bool reinserted = false;
        for (const auto& p : lane.points)
            if (p.timeSamples == 750)
            {
                reinserted = true;
                expectWithinAbsoluteError(p.value, 0.55f, 1.0e-6f);
                expect(p.curveToNext == DAW::AutomationCurveType::Exponential,
                       "redo must restore the inserted point's curve type");
                expectWithinAbsoluteError(p.tensionToNext, 0.3f, 1.0e-6f);
            }
        expect(reinserted, "redo must re-insert the full point");
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));

        undoManager.clear();
    }

    void testDenseAutomation()
    {
        DAW::AutomationManagerCore automation;
        DAW::AutomationUIHelper helper(automation);
        const DAW::TrackID track("t1");
        const juce::String param = DAW::AutomationLaneCore::trackVolumeParameterId;

        auto& lane = helper.getOrCreateLane(track, param);
        const int count = 500;
        for (int i = 0; i < count; ++i)
            lane.addPoint(i * 100, 0.1f + 0.8f * (float)i / (float)(count - 1));

        expectEquals(lane.points.size(), (size_t)count);
        expect(hasUniqueTimes(lane.points));
        expect(isSortedByTime(lane.points));

        // Deterministic, in-range evaluation across the whole lane.
        for (int64_t s = 0; s < count * 100; s += 137)
        {
            const float v1 = lane.getValueAtSample(s, 0.0f);
            const float v2 = lane.getValueAtSample(s, 0.0f);
            expect(v1 == v2, "dense evaluation must be deterministic");
            expect(v1 >= 0.0f && v1 <= 1.0f, "dense evaluation must stay in range");
        }

        // Deterministic replacement + dedupe on the RT lane representation.
        apex::automation::AutomationLane rtLane(apex::automation::ParameterID("dense-test"));
        apex::automation::AutomationLane::PointVector rtPoints;
        rtPoints.reserve((size_t)count);
        for (int i = 0; i < count; ++i)
            rtPoints.push_back({ (double)i * 10.0, 0.1f + 0.8f * (float)i / (float)(count - 1),
                                 apex::automation::CurveType::Linear, 0.0f });
        // Add a deliberate duplicate timestamp: keep-last must win deterministically.
        rtPoints.push_back({ 2500.0, 0.99f, apex::automation::CurveType::Linear, 0.0f });
        rtLane.replacePoints(std::move(rtPoints));

        const auto snap = rtLane.getSnapshot();
        expect(snap != nullptr);
        if (snap != nullptr)
        {
            expectEquals(snap->size(), (size_t)count, "duplicate must be deduped keep-last");
            bool sorted = true;
            for (size_t i = 1; i < snap->size(); ++i)
                if ((*snap)[i].timePPQ < (*snap)[i - 1].timePPQ)
                    sorted = false;
            expect(sorted, "RT lane must be deterministically sorted");
            // The retained duplicate must be the last-added one (0.99).
            for (const auto& bp : *snap)
                if (bp.timePPQ == 2500.0)
                    expectWithinAbsoluteError(bp.normalizedValue, 0.99f, 1.0e-6f);
        }
    }
};

static AutomationPolishTests automationPolishTests;
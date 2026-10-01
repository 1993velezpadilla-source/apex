/**
 * PHASE 1 TEST HELPER — Create Sample Automation Data
 * 
 * Use this to quickly populate test automation lanes with sample curves
 * for testing and demonstration.
 */

#pragma once

#include <JuceHeader.h>
#include "../../../Source/AutomationCore/AutomationManagerCore.h"
#include "../../../Source/AutomationCore/AutomationCurveTypesCore.h"

namespace DAW {

class Phase1TestHelper
{
public:
    /**
     * Create comprehensive test automation lanes with all 14 curve types.
     * 
     * Creates:
     * - Track: "test_track_volume" with volume automation showing curves 0-6
     * - Track: "test_track_volume2" with volume automation showing curves 7-13
     * - Track: "test_track_pan" with pan automation for testing pan range
     */
    static void createComprehensiveTestLanes(AutomationManagerCore& manager)
    {
        // ====================================================================
        // TRACK 1: Curve Types 0-6 (Linear, Smooth, Hold, SingleCurve x3)
        // ====================================================================
        {
            TrackID track("test_track_volume");
            auto& lane = manager.getOrCreateLane(track, "track.volume");

            // Clear existing points
            lane.points.clear();

            // Add 7 points for 7 curve types (0-6)
            for (int i = 0; i < 7; ++i)
            {
                int64_t time = (int64_t)i * 44100;  // 1 second per curve
                float value = 0.3f + (float)i * 0.08f;
                lane.addPoint(time, juce::jlimit(0.0f, 1.0f, value));
            }

            // Set curve types on each point
            if (lane.points.size() >= 7)
            {
                lane.points[0].curveToNext = AutomationCurveType::Linear;
                lane.points[0].tensionToNext = 0.0f;

                lane.points[1].curveToNext = AutomationCurveType::Smooth;
                lane.points[1].tensionToNext = 0.0f;

                lane.points[2].curveToNext = AutomationCurveType::Hold;
                lane.points[2].tensionToNext = 0.0f;

                lane.points[3].curveToNext = AutomationCurveType::SingleCurve;
                lane.points[3].tensionToNext = -0.5f;  // Ease-in

                lane.points[4].curveToNext = AutomationCurveType::SingleCurve;
                lane.points[4].tensionToNext = 0.0f;  // Neutral

                lane.points[5].curveToNext = AutomationCurveType::SingleCurve;
                lane.points[5].tensionToNext = 0.5f;  // Ease-out

                lane.points[6].curveToNext = AutomationCurveType::Linear;
                lane.points[6].tensionToNext = 0.0f;
            }
        }

        // ====================================================================
        // TRACK 2: Curve Types 7-13 (DoubleCurve x3, HalfSine, Stairs, etc)
        // ====================================================================
        {
            TrackID track("test_track_volume2");
            auto& lane = manager.getOrCreateLane(track, "track.volume");

            lane.points.clear();

            // Add 7 points for remaining curve types
            for (int i = 0; i < 7; ++i)
            {
                int64_t time = (int64_t)i * 44100;
                float value = 0.5f - (float)i * 0.06f;
                lane.addPoint(time, juce::jlimit(0.0f, 1.0f, value));
            }

            // Set curve types
            if (lane.points.size() >= 7)
            {
                lane.points[0].curveToNext = AutomationCurveType::DoubleCurve;
                lane.points[0].tensionToNext = -0.5f;  // Ease-in then out

                lane.points[1].curveToNext = AutomationCurveType::DoubleCurve;
                lane.points[1].tensionToNext = 0.0f;  // Neutral S-curve

                lane.points[2].curveToNext = AutomationCurveType::DoubleCurve;
                lane.points[2].tensionToNext = 0.5f;  // Ease-out then in

                lane.points[3].curveToNext = AutomationCurveType::HalfSine;
                lane.points[3].tensionToNext = 0.0f;

                lane.points[4].curveToNext = AutomationCurveType::Stairs;
                lane.points[4].tensionToNext = 0.0f;

                lane.points[5].curveToNext = AutomationCurveType::SmoothStairs;
                lane.points[5].tensionToNext = 0.0f;

                lane.points[6].curveToNext = AutomationCurveType::Wave;
                lane.points[6].tensionToNext = 0.0f;
            }
        }

        // ====================================================================
        // TRACK 3: Pan automation (to show bipolar range -1..1)
        // ====================================================================
        {
            TrackID track("test_track_pan");
            auto& lane = manager.getOrCreateLane(track, "track.pan");

            lane.points.clear();

            // Create LFO-like pattern across pan
            lane.addPoint(0,      0.0f);    // Center
            lane.addPoint(22050,  0.7f);    // Right
            lane.addPoint(44100, -0.7f);    // Left
            lane.addPoint(66150,  0.0f);    // Center

            if (lane.points.size() >= 3)
            {
                lane.points[0].curveToNext = AutomationCurveType::Smooth;
                lane.points[0].tensionToNext = 0.2f;

                lane.points[1].curveToNext = AutomationCurveType::Smooth;
                lane.points[1].tensionToNext = -0.2f;

                lane.points[2].curveToNext = AutomationCurveType::Smooth;
                lane.points[2].tensionToNext = 0.0f;
            }
        }

        // Publish snapshot for audio thread
        manager.publishSnapshot();
    }

    /**
     * Create simple test lane with just 3 points.
     * Good for quick testing of a single curve type.
     */
    static void createSimpleTestLane(AutomationManagerCore& manager)
    {
        TrackID track("test_simple");
        auto& lane = manager.getOrCreateLane(track, "track.volume");

        lane.points.clear();
        lane.addPoint(0,     0.2f);
        lane.addPoint(44100, 0.8f);
        lane.addPoint(88200, 0.4f);

        if (lane.points.size() >= 2)
        {
            lane.points[0].curveToNext = AutomationCurveType::Smooth;
            lane.points[0].tensionToNext = 0.0f;

            lane.points[1].curveToNext = AutomationCurveType::Smooth;
            lane.points[1].tensionToNext = 0.0f;
        }

        manager.publishSnapshot();
    }

    /**
     * Create test lane showing tension variations.
     * Shows -1.0, -0.5, 0.0, 0.5, 1.0 tension values.
     */
    static void createTensionTestLane(AutomationManagerCore& manager)
    {
        TrackID track("test_tension");
        auto& lane = manager.getOrCreateLane(track, "track.volume");

        lane.points.clear();

        // 6 points to show 5 different tension values
        for (int i = 0; i < 6; ++i)
        {
            int64_t time = (int64_t)i * 44100 / 2;
            float value = 0.5f;
            lane.addPoint(time, value);
        }

        // Set tensions: -1.0, -0.5, 0.0, 0.5, 1.0, 0.0
        if (lane.points.size() >= 5)
        {
            lane.points[0].curveToNext = AutomationCurveType::SingleCurve;
            lane.points[0].tensionToNext = -1.0f;  // Max ease-in

            lane.points[1].curveToNext = AutomationCurveType::SingleCurve;
            lane.points[1].tensionToNext = -0.5f;  // Half ease-in

            lane.points[2].curveToNext = AutomationCurveType::SingleCurve;
            lane.points[2].tensionToNext = 0.0f;  // Neutral

            lane.points[3].curveToNext = AutomationCurveType::SingleCurve;
            lane.points[3].tensionToNext = 0.5f;  // Half ease-out

            lane.points[4].curveToNext = AutomationCurveType::SingleCurve;
            lane.points[4].tensionToNext = 1.0f;  // Max ease-out
        }

        manager.publishSnapshot();
    }
};

} // namespace DAW
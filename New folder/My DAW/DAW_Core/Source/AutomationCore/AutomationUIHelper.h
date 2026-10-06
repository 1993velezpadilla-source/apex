/**
 * APEX AUTOMATION V2 — UI INTEGRATION HELPERS
 * 
 * This file provides wrapper utilities and helper functions for ArrangementEditor
 * to easily integrate with the core Automation V2 infrastructure.
 * 
 * Use these helpers to bridge between UI events and AutomationManagerCore operations.
 */

#pragma once

#include <JuceHeader.h>
#include "AutomationManagerCore.h"
#include "AutomationContextMenuCore.h"
#include "AutomationQuickCreateCore.h"
#include "AutomationClipRegionCore.h"

namespace DAW {

/**
 * AutomationUIHelper — Convenience wrapper for UI layer
 * 
 * Provides simple, high-level methods for common UI operations
 * that internally call the appropriate AutomationManagerCore methods.
 */
class AutomationUIHelper
{
public:
    explicit AutomationUIHelper(AutomationManagerCore& manager)
        : manager_(manager)
    {
    }

    // ========================================================================
    // POINT OPERATIONS (for right-click point menus)
    // ========================================================================

    /**
     * Delete a point at given index.
     * Safe to call even if point doesn't exist.
     */
    void deletePoint(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        manager_.removePoint(trackId, parameterId, pointIndex);
    }

    /**
     * Copy a point's value to clipboard.
     * Can then pastePointValue() onto another point.
     */
    bool copyPointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        return manager_.copyPointValue(trackId, parameterId, pointIndex);
    }

    /**
     * Paste copied point value onto another point.
     * Returns false if nothing to paste.
     */
    bool pastePointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        return manager_.pastePointValue(trackId, parameterId, pointIndex);
    }

    /**
     * Check if a point value is available to paste.
     */
    bool canPastePointValue() const
    {
        return manager_.canPastePointValue();
    }

    /**
     * Reset a point's value to the lane's default value.
     */
    bool resetPointValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex)
    {
        return manager_.resetPointValue(trackId, parameterId, pointIndex);
    }

    /**
     * Set point to an exact value (0..1 normalized, or parameter-specific range).
     */
    bool setPointExactValue(const TrackID& trackId, const juce::String& parameterId, int pointIndex, float value)
    {
        return manager_.setPointExactValue(trackId, parameterId, pointIndex, value);
    }

    /**
     * Set point to an exact time (sample position).
     */
    bool setPointExactTime(const TrackID& trackId, const juce::String& parameterId, int pointIndex, int64_t timeSamples)
    {
        return manager_.setPointExactTime(trackId, parameterId, pointIndex, timeSamples);
    }

    // ========================================================================
    // CURVE / TENSION OPERATIONS (for segment manipulation)
    // ========================================================================

    /**
     * Set the curve type for a segment (from left point to next point).
     * segmentIndex is the index of the left point.
     */
    void setSegmentCurveType(const TrackID& trackId, const juce::String& parameterId, int segmentIndex, AutomationCurveType curveType)
    {
        manager_.setPointCurveToNext(trackId, parameterId, segmentIndex, curveType);
    }

    /**
     * Set tension for a segment.
     * segmentIndex is the index of the left point.
     * tension: -1..1 (negative = ease-in, positive = ease-out)
     */
    void setSegmentTension(const TrackID& trackId, const juce::String& parameterId, int segmentIndex, float tension)
    {
        manager_.setPointTensionToNext(trackId, parameterId, segmentIndex, tension);
    }

    /**
     * Reset segment tension to 0.0 (neutral).
     */
    bool resetSegmentTension(const TrackID& trackId, const juce::String& parameterId, int segmentIndex)
    {
        return manager_.resetSegmentTension(trackId, parameterId, segmentIndex);
    }

    /**
     * Copy segment shape (curve type + tension) to clipboard.
     * Can then pasteSegmentShape() onto another segment.
     */
    bool copySegmentShape(const TrackID& trackId, const juce::String& parameterId, int segmentIndex)
    {
        return manager_.copySegmentShape(trackId, parameterId, segmentIndex);
    }

    /**
     * Paste segment shape onto another segment.
     * If pasteValues=true, also pastes the endpoint values.
     */
    bool pasteSegmentShape(const TrackID& trackId, const juce::String& parameterId, int segmentIndex, bool pasteValues = false)
    {
        return manager_.pasteSegmentShape(trackId, parameterId, segmentIndex, pasteValues);
    }

    /**
     * Check if a segment shape is available to paste.
     */
    bool canPasteSegmentShape() const
    {
        return manager_.canPasteSegmentShape();
    }

    // ========================================================================
    // POINT INSERTION (Shift+RightClick insert)
    // ========================================================================

    /**
     * Insert a new point at the given time, preserving the current automation level.
     * Returns the index of the inserted point, or -1 if failed.
     */
    int insertPointPreservingLevel(const TrackID& trackId, const juce::String& parameterId, int64_t timeSamples)
    {
        return manager_.insertPointPreservingLevel(trackId, parameterId, timeSamples);
    }

    void addOrReplacePoint(const TrackID& trackId, const juce::String& parameterId, int64_t timeSamples, float value, bool deferPublish = false)
    {
        manager_.addOrReplacePoint(trackId, parameterId, timeSamples, value, deferPublish);
    }

    void movePoint(const TrackID& trackId, const juce::String& parameterId, int pointIndex, int64_t timeSamples, float value)
    {
        manager_.movePoint(trackId, parameterId, pointIndex, timeSamples, value);
    }

    // ========================================================================
    // CLIP REGION OPERATIONS (for automation clip blocks)
    // ========================================================================

    /**
     * Create a new automation clip region.
     * Returns reference to created region (valid until next manager operation).
     */
    AutomationClipRegionCore& createClipRegion(const TrackID& trackId, const juce::String& parameterId, int64_t startSample, int64_t lengthSamples)
    {
        AutomationClipRegionCore region;
        region.trackId = trackId;
        region.parameterId = parameterId;
        region.startSample = startSample;
        region.lengthSamples = lengthSamples;
        return manager_.addClipRegion(std::move(region));
    }

    /**
     * Duplicate an existing clip region to a new start time.
     */
    bool duplicateClipRegion(const juce::Uuid& regionId, int64_t newStartSample)
    {
        return manager_.duplicateClipRegion(regionId, newStartSample);
    }

    /**
     * Move a clip region to a new timeline position.
     */
    bool moveClipRegion(const juce::Uuid& regionId, int64_t newStartSample)
    {
        return manager_.moveClipRegion(regionId, newStartSample);
    }

    /**
     * Resize a clip region (changes lengthSamples).
     */
    bool resizeClipRegion(const juce::Uuid& regionId, int64_t newLengthSamples)
    {
        return manager_.resizeClipRegion(regionId, newLengthSamples);
    }

    /**
     * Toggle mute state of a clip region.
     */
    bool setClipRegionMuted(const juce::Uuid& regionId, bool muted)
    {
        return manager_.setClipRegionMuted(regionId, muted);
    }

    /**
     * Rename a clip region.
     */
    bool renameClipRegion(const juce::Uuid& regionId, const juce::String& newName)
    {
        return manager_.renameClipRegion(regionId, newName);
    }

    /**
     * Change clip region color.
     */
    bool setClipRegionColor(const juce::Uuid& regionId, juce::Colour color)
    {
        return manager_.setClipRegionColor(regionId, color);
    }

    /**
     * Delete a clip region.
     */
    bool deleteClipRegion(const juce::Uuid& regionId)
    {
        return manager_.deleteClipRegion(regionId);
    }

    /**
     * Get clip region by ID (returns nullptr if not found).
     */
    AutomationClipRegionCore* findClipRegion(const juce::Uuid& regionId)
    {
        return manager_.findClipRegion(regionId);
    }

    // NOTE: getClipRegionsForParameter removed - use findClipRegion instead

    // ========================================================================
    // ARTICULATOR TOOLS (for clip transformations)
    // ========================================================================

    /**
     * Flip a clip region vertically (invert values around center).
     */
    bool flipClipVertically(const juce::Uuid& regionId)
    {
        return manager_.flipClipRegionVertically(regionId);
    }

    /**
     * Scale a clip region's levels around center point.
     * amount=1.0 (no change), amount=2.0 (double), amount=0.5 (half)
     */
    bool scaleClipLevels(const juce::Uuid& regionId, float amount)
    {
        return manager_.scaleClipRegionLevels(regionId, amount);
    }

    /**
     * Normalize a clip region to fill 0..1 range.
     */
    bool normalizeClipLevels(const juce::Uuid& regionId)
    {
        return manager_.normalizeClipRegionLevels(regionId);
    }

    /**
     * Reset a clip region to default value.
     */
    bool resetClipLevels(const juce::Uuid& regionId, float defaultValue = 0.5f)
    {
        return manager_.resetClipRegionLevels(regionId, defaultValue);
    }

    /**
     * Copy state from one clip region.
     */
    bool copyClipState(const juce::Uuid& regionId)
    {
        return manager_.copyClipRegionState(regionId);
    }

    /**
     * Paste copied state onto a clip region.
     */
    bool pasteClipState(const juce::Uuid& regionId)
    {
        return manager_.pasteClipRegionState(regionId);
    }

    // ========================================================================
    // QUICK AUTOMATION CREATION (right-click any control)
    // ========================================================================

    /**
     * Quick-create automation for track volume.
     */
    AutomationLaneCore& quickCreateTrackVolume(const TrackID& trackId)
    {
        return manager_.getOrCreateLane(trackId, AutomationLaneCore::trackVolumeParameterId);
    }

    /**
     * Quick-create automation for track pan.
     */
    AutomationLaneCore& quickCreateTrackPan(const TrackID& trackId)
    {
        return manager_.getOrCreateLane(trackId, AutomationLaneCore::trackPanParameterId);
    }

    /**
     * Quick-create automation for track tape stop.
     */
    AutomationLaneCore& quickCreateTrackTapeStop(const TrackID& trackId)
    {
        return manager_.getOrCreateLane(trackId, AutomationLaneCore::trackTapeStopParameterId);
    }

    /**
     * Quick-create automation for clip pitch.
     */
    AutomationLaneCore& quickCreateClipPitch(const TrackID& trackId, const juce::String& clipId)
    {
        return manager_.getOrCreateLane(trackId, AutomationLaneCore::makeClipPitchParameterId(clipId));
    }

    /**
     * Quick-create automation for plugin wet/dry.
     */
    AutomationLaneCore& quickCreatePluginWetDry(const TrackID& trackId, int slotIndex, const juce::String& pluginInstanceId)
    {
        auto parameterId = "plugin." + juce::String(slotIndex) + "." + pluginInstanceId + ".wet_dry";
        return manager_.getOrCreateLane(trackId, parameterId);
    }

    /**
     * Quick-create automation for plugin parameter.
     */
    AutomationLaneCore& quickCreatePluginParameter(const TrackID& trackId, int slotIndex, const juce::String& pluginInstanceId, const juce::String& paramId)
    {
        auto parameterId = "plugin." + juce::String(slotIndex) + "." + pluginInstanceId + "." + paramId;
        return manager_.getOrCreateLane(trackId, parameterId);
    }

    // ========================================================================
    // GENERAL LANE OPERATIONS
    // ========================================================================

    /**
     * Show/hide automation lane in UI.
     */
    void setLaneVisible(const TrackID& trackId, const juce::String& parameterId, bool visible)
    {
        manager_.setLaneVisible(trackId, parameterId, visible);
    }

    /**
     * Enable/disable automation playback for a lane.
     */
    void setLaneEnabled(const TrackID& trackId, const juce::String& parameterId, bool enabled)
    {
        manager_.setLaneEnabled(trackId, parameterId, enabled);
    }

    /**
     * Clear all automation for a parameter on a track.
     */
    void clearLane(const TrackID& trackId, const juce::String& parameterId)
    {
        manager_.clearLane(trackId, parameterId);
    }

    /**
     * Get or create a lane (for direct access to point manipulation).
     */
    AutomationLaneCore& getOrCreateLane(const TrackID& trackId, const juce::String& parameterId)
    {
        return manager_.getOrCreateLane(trackId, parameterId);
    }

    /**
     * Find a lane (returns nullptr if not found).
     */
    const AutomationLaneCore* findLane(const TrackID& trackId, const juce::String& parameterId) const
    {
        return manager_.findLane(trackId, parameterId);
    }

    // ========================================================================
    // STATE MANAGEMENT
    // ========================================================================

    /**
     * Force publish of automation snapshot to audio thread.
     * Call after batch edits to sync changes.
     */
    void publishSnapshot()
    {
        manager_.publishSnapshot();
    }

private:
    AutomationManagerCore& manager_;
};

/**
 * AutomationCurveTypeHelper — Utilities for curve type UI rendering
 */
class AutomationCurveTypeHelper
{
public:
    /**
     * Get all available curve types as array.
     */
    static juce::Array<AutomationCurveType> getAllCurveTypes()
    {
        return getAllAutomationCurveTypes();
    }

    /**
     * Get display name for a curve type (e.g., "Single Curve" not "SingleCurve").
     */
    static juce::String getDisplayName(AutomationCurveType curveType)
    {
        return automationCurveTypeToDisplayName(curveType);
    }

    /**
     * Get internal name for a curve type (e.g., "SingleCurve").
     */
    static juce::String getInternalName(AutomationCurveType curveType)
    {
        return automationCurveTypeToString(curveType);
    }

    /**
     * Parse curve type from string (handles both display and internal names).
     */
    static AutomationCurveType parseCurveType(const juce::String& name)
    {
        return automationCurveTypeFromString(name);
    }

    /**
     * Check if a curve type uses tension meaningfully.
     * Some curves ignore tension.
     */
    static bool usesTension(AutomationCurveType curveType)
    {
        switch (curveType)
        {
            case AutomationCurveType::Hold:
                return false; // Hold ignores tension
            default:
                return true;
        }
    }

    /**
     * Get recommended tension range for a curve type (-1..1).
     * Can be used to limit UI slider ranges.
     */
    static void getTensionRange(AutomationCurveType curveType, float& outMin, float& outMax)
    {
        outMin = -1.0f;
        outMax = 1.0f;

        switch (curveType)
        {
            case AutomationCurveType::Stairs:
            case AutomationCurveType::Wave:
            case AutomationCurveType::Pulse:
                // These curves benefit from full tension range
                break;
            default:
                // Most curves work well with ±0.5
                outMax = 0.5f;
                outMin = -0.5f;
                break;
        }
    }

    /**
     * Get default tension for a curve type.
     */
    static float getDefaultTension(AutomationCurveType curveType)
    {
        return 0.0f; // Most curves default to neutral tension
    }
};

} // namespace DAW

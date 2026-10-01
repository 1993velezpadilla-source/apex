/**
 * APEX AUTOMATION V2 — DEVELOPER QUICK REFERENCE
 * 
 * Fast lookup guide for common operations and API methods.
 * Use this as a cheat sheet during UI implementation.
 */

#pragma once

#include "AutomationUIHelper.h"

namespace DAW {

// ============================================================================
// QUICK LOOKUP: COMMON OPERATIONS
// ============================================================================

/*
 * USER ACTION → IMPLEMENTATION
 * ══════════════════════════════════════════════════════════════════════════
 * 
 * RIGHT-CLICK POINT
 * ─────────────────
 * Action: Delete point
 *   → helper.deletePoint(trackId, parameterId, pointIndex);
 * 
 * Action: Copy point value
 *   → helper.copyPointValue(trackId, parameterId, pointIndex);
 * 
 * Action: Paste point value
 *   → helper.pastePointValue(trackId, parameterId, pointIndex);
 *   (Check helper.canPastePointValue() first)
 * 
 * Action: Set curve type for segment from point
 *   → helper.setSegmentCurveType(trackId, parameterId, pointIndex, curveType);
 * 
 * Action: Change point to exact value
 *   → helper.setPointExactValue(trackId, parameterId, pointIndex, 0.5f);
 * 
 * 
 * RIGHT-CLICK SEGMENT (between two points)
 * ────────────────────────────────────────
 * Action: Add point at cursor
 *   → int64_t timeSamples = pixelToSample(mouseX);
 *   → helper.insertPointPreservingLevel(trackId, parameterId, timeSamples);
 * 
 * Action: Change segment curve type
 *   → helper.setSegmentCurveType(trackId, parameterId, segmentIndex, curveType);
 * 
 * Action: Adjust segment tension
 *   → helper.setSegmentTension(trackId, parameterId, segmentIndex, 0.25f);
 * 
 * Action: Reset segment tension to neutral
 *   → helper.resetSegmentTension(trackId, parameterId, segmentIndex);
 * 
 * 
 * TENSION HANDLE DRAG
 * ──────────────────
 * Action: Start drag (mouseDown)
 *   → store lastMouseY = e.getPosition().y
 * 
 * Action: Continue drag (mouseDrag)
 *   → deltaY = lastMouseY - e.getPosition().y
 *   → dragAmount = deltaY / componentHeight
 *   → newTension = currentTension + dragAmount * 0.01f
 *   → helper.setSegmentTension(..., newTension);
 * 
 * Action: Right-click to reset
 *   → helper.resetSegmentTension(trackId, parameterId, segmentIndex);
 * 
 * 
 * AUTOMATION CLIP BLOCK
 * ────────────────────
 * Action: Create new clip
 *   → AutomationClipRegion& clip = 
 *        helper.createClipRegion(trackId, parameterId, startSample, lengthSamples);
 * 
 * Action: Drag clip to move
 *   → int64_t newStart = currentStart + pixelDeltaToSamples(deltaX);
 *   → helper.moveClipRegion(clipId, newStart);
 * 
 * Action: Resize clip (drag edge)
 *   → int64_t newLength = currentLength + pixelDeltaToSamples(deltaX);
 *   → helper.resizeClipRegion(clipId, newLength);
 * 
 * Action: Duplicate clip
 *   → int64_t newStart = currentStart + currentLength + 1000;
 *   → helper.duplicateClipRegion(clipId, newStart);
 * 
 * Action: Mute/unmute clip
 *   → helper.setClipRegionMuted(clipId, !currentMuted);
 * 
 * Action: Right-click for context menu
 *   → Show menu with: Duplicate, Copy State, Mute, Articulator Tools, Rename, Color
 * 
 * 
 * ARTICULATOR TOOLS
 * ─────────────────
 * Action: Flip clip vertically
 *   → helper.flipClipVertically(clipId);
 * 
 * Action: Scale clip levels by 1.5x
 *   → helper.scaleClipLevels(clipId, 1.5f);
 * 
 * Action: Normalize clip to full 0..1 range
 *   → helper.normalizeClipLevels(clipId);
 * 
 * Action: Reset all points to default (0.5)
 *   → helper.resetClipLevels(clipId, 0.5f);
 * 
 * Action: Copy clip state to clipboard
 *   → helper.copyClipState(clipId);
 * 
 * Action: Paste state onto another clip
 *   → helper.pasteClipState(otherClipId);
 * 
 * 
 * QUICK AUTOMATION CREATION
 * ──────────────────────────
 * Action: Right-click track volume knob
 *   → helper.quickCreateTrackVolume(trackId);
 * 
 * Action: Right-click track pan knob
 *   → helper.quickCreateTrackPan(trackId);
 * 
 * Action: Right-click clip pitch slider
 *   → helper.quickCreateClipPitch(trackId, clipId);
 * 
 * Action: Right-click plugin wet/dry slider
 *   → helper.quickCreatePluginWetDry(trackId, slotIndex, pluginInstanceId);
 * 
 * Action: Right-click plugin parameter
 *   → helper.quickCreatePluginParameter(trackId, slotIndex, 
 *        pluginInstanceId, paramId);
 * 
 * 
 * CURVE TYPE OPERATIONS
 * ─────────────────────
 * Action: Get all curve types for dropdown
 *   → auto curves = AutomationCurveTypeHelper::getAllCurveTypes();
 * 
 * Action: Get display name for curve
 *   → juce::String name = AutomationCurveTypeHelper::getDisplayName(curve);
 * 
 * Action: Parse curve from string
 *   → AutomationCurveType curve = AutomationCurveTypeHelper::parseCurveType("Smooth");
 * 
 * Action: Get recommended tension range
 *   → float minTension, maxTension;
 *   → AutomationCurveTypeHelper::getTensionRange(curve, minTension, maxTension);
 */

// ============================================================================
// API QUICK REFERENCE
// ============================================================================

/*
 * AUTOMATIONUIHELPER METHODS
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * POINT OPERATIONS (8 methods)
 * ────────────────────────────
 * void deletePoint(trackId, parameterId, pointIndex)
 * bool copyPointValue(trackId, parameterId, pointIndex)
 * bool pastePointValue(trackId, parameterId, pointIndex)
 * bool canPastePointValue() → bool
 * bool resetPointValue(trackId, parameterId, pointIndex)
 * bool setPointExactValue(trackId, parameterId, pointIndex, value)
 * bool setPointExactTime(trackId, parameterId, pointIndex, timeSamples)
 * int insertPointPreservingLevel(trackId, parameterId, timeSamples)
 * 
 * CURVE / TENSION OPERATIONS (6 methods)
 * ──────────────────────────────────────
 * void setSegmentCurveType(trackId, parameterId, segmentIndex, curveType)
 * void setSegmentTension(trackId, parameterId, segmentIndex, tension)
 * bool resetSegmentTension(trackId, parameterId, segmentIndex)
 * bool copySegmentShape(trackId, parameterId, segmentIndex)
 * bool pasteSegmentShape(trackId, parameterId, segmentIndex, pasteValues=false)
 * bool canPasteSegmentShape() → bool
 * 
 * CLIP REGION OPERATIONS (10 methods)
 * ──────────────────────────────────
 * AutomationClipRegion& createClipRegion(trackId, parameterId, start, length)
 * bool duplicateClipRegion(regionId, newStartSample)
 * bool moveClipRegion(regionId, newStartSample)
 * bool resizeClipRegion(regionId, newLengthSamples)
 * bool setClipRegionMuted(regionId, muted)
 * bool renameClipRegion(regionId, newName)
 * bool setClipRegionColor(regionId, color)
 * bool deleteClipRegion(regionId)
 * AutomationClipRegion* findClipRegion(regionId)
 * std::vector<AutomationClipRegion*> getClipRegionsForParameter(parameterId)
 * 
 * ARTICULATOR TOOLS (6 methods)
 * ─────────────────────────────
 * bool flipClipVertically(regionId)
 * bool scaleClipLevels(regionId, amount) [1.0=no change, 2.0=double, 0.5=half]
 * bool normalizeClipLevels(regionId)
 * bool resetClipLevels(regionId, defaultValue=0.5f)
 * bool copyClipState(regionId)
 * bool pasteClipState(regionId)
 * 
 * QUICK CREATE (6 methods)
 * ────────────────────────
 * AutomationLaneCore& quickCreateTrackVolume(trackId)
 * AutomationLaneCore& quickCreateTrackPan(trackId)
 * AutomationLaneCore& quickCreateTrackTapeStop(trackId)
 * AutomationLaneCore& quickCreateClipPitch(trackId, clipId)
 * AutomationLaneCore& quickCreatePluginWetDry(trackId, slotIndex, instanceId)
 * AutomationLaneCore& quickCreatePluginParameter(trackId, slotIndex, instanceId, paramId)
 * 
 * GENERAL LANE OPERATIONS (5 methods)
 * ──────────────────────────────────
 * void setLaneVisible(trackId, parameterId, visible)
 * void setLaneEnabled(trackId, parameterId, enabled)
 * void clearLane(trackId, parameterId)
 * AutomationLaneCore& getOrCreateLane(trackId, parameterId)
 * const AutomationLaneCore* findLane(trackId, parameterId)
 * 
 * STATE MANAGEMENT (1 method)
 * ──────────────────────────
 * void publishSnapshot()
 */

// ============================================================================
// COMMON PATTERNS
// ============================================================================

/*
 * PATTERN: Converting Mouse Position to Timeline Samples
 * ──────────────────────────────────────────────────────
 * 
 * float pixelsPerSample_;  // Member variable: samples/pixel ratio
 * int64_t playheadSamplePos_;  // Member variable
 * 
 * int64_t pixelToSample(int pixelX) const
 * {
 *     return playheadSamplePos_ + (int64_t)(pixelX * pixelsPerSample_);
 * }
 * 
 * int sampleToPixel(int64_t samplePos) const
 * {
 *     return (int)((samplePos - playheadSamplePos_) / pixelsPerSample_);
 * }
 * 
 * // Usage: Get time at mouse click
 * void mouseDown(const juce::MouseEvent& e)
 * {
 *     int64_t timeSamples = pixelToSample(e.getPosition().x);
 *     // ... use timeSamples with AutomationUIHelper
 * }
 */

/*
 * PATTERN: Checking if Automation Lane Exists
 * ────────────────────────────────────────────
 * 
 * bool hasAutomation(const TrackID& trackId, const juce::String& parameterId)
 * {
 *     return helper.findLane(trackId, parameterId) != nullptr;
 * }
 * 
 * // Usage
 * if (hasAutomation(trackId, AutomationLaneCore::trackVolumeParameterId))
 * {
 *     // Lane exists, can edit
 * }
 * else
 * {
 *     // Lane doesn't exist, can create with quickCreate
 *     helper.quickCreateTrackVolume(trackId);
 * }
 */

/*
 * PATTERN: Batch Edits with Deferred Publish
 * ────────────────────────────────────────────
 * 
 * // Make multiple edits without publishing after each one
 * helper.setSegmentTension(trackId, parameterId, 0, 0.1f);
 * helper.setSegmentCurveType(trackId, parameterId, 0, AutomationCurveType::Smooth);
 * helper.setSegmentTension(trackId, parameterId, 1, -0.2f);
 * helper.setSegmentCurveType(trackId, parameterId, 1, AutomationCurveType::DoubleCurve);
 * 
 * // Publish once after all edits
 * helper.publishSnapshot();
 */

/*
 * PATTERN: Undo/Redo Support
 * ──────────────────────────
 * 
 * // Capture state before operation
 * auto stateBefore = automationManager.getState();
 * 
 * // Perform operation
 * helper.flipClipVertically(clipId);
 * 
 * // On undo: restore state
 * automationManager.restoreState(stateBefore);
 * helper.publishSnapshot();
 * 
 * // On redo: restore state
 * auto stateAfter = automationManager.getState();
 * automationManager.restoreState(stateAfter);
 * helper.publishSnapshot();
 */

/*
 * PATTERN: Context Menu with Curve Type Submenu
 * ──────────────────────────────────────────────
 * 
 * juce::PopupMenu curveMenu;
 * auto allCurves = AutomationCurveTypeHelper::getAllCurveTypes();
 * 
 * for (auto curve : allCurves)
 * {
 *     auto displayName = AutomationCurveTypeHelper::getDisplayName(curve);
 *     curveMenu.addItem(displayName, [this, curve]()
 *     {
 *         helper.setSegmentCurveType(trackId, parameterId, segmentIndex, curve);
 *         repaint();  // Update UI
 *     });
 * }
 * 
 * menu.addSubMenu("Curve Type", curveMenu);
 */

} // namespace DAW

#endif // APEX_AUTOMATION_QUICK_REFERENCE_H

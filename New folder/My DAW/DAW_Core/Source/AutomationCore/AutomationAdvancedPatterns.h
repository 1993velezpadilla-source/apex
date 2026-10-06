/**
 * APEX AUTOMATION V2 — TROUBLESHOOTING & ADVANCED PATTERNS
 * 
 * Advanced implementation patterns and solutions for complex scenarios
 * that may arise during UI integration.
 */

#pragma once

#include "AutomationUIHelper.h"

namespace DAW {

// ============================================================================
// ADVANCED PATTERNS
// ============================================================================

/*
 * PATTERN: Batch Operations with Progress Reporting
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Applying articulator tools to 100+ clips needs progress feedback
 * 
 * Solution:
 * 
 * class BatchArticulatorOperation
 * {
 * public:
 *     using ProgressCallback = std::function<void(float progress)>;
 *     
 *     static void flipAllClips(AutomationUIHelper& helper, 
 *                              const std::vector<juce::Uuid>& clipIds,
 *                              ProgressCallback onProgress)
 *     {
 *         for (size_t i = 0; i < clipIds.size(); ++i)
 *         {
 *             helper.flipClipVertically(clipIds[i]);
 *             
 *             float progress = (float)(i + 1) / (float)clipIds.size();
 *             if (onProgress)
 *                 onProgress(progress);
 *         }
 *         
 *         helper.publishSnapshot();  // Publish once at end
 *     }
 * };
 * 
 * Usage:
 * BatchArticulatorOperation::flipAllClips(helper, selectedClips, 
 *     [this](float progress)
 *     {
 *         progressBar.setValue(progress);
 *         MessageManager::callAsync([this]() { repaint(); });
 *     });
 */

/*
 * PATTERN: Undo/Redo Support for UI Operations
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Need full undo/redo for all automation operations
 * 
 * Solution: Create UndoableAction wrapper
 * 
 * class AutomationUndoableAction : public juce::UndoableAction
 * {
 * public:
 *     AutomationUndoableAction(AutomationManagerCore& mgr,
 *                              const juce::String& actionName,
 *                              std::function<void()> doFunc,
 *                              std::function<void()> undoFunc)
 *         : manager_(mgr), actionName_(actionName),
 *           doFunc_(doFunc), undoFunc_(undoFunc),
 *           stateBefore_(manager_.getState())
 *     {
 *         // Perform the action immediately
 *         doFunc_();
 *         stateAfter_ = manager_.getState();
 *     }
 *     
 *     bool perform() override
 *     {
 *         manager_.restoreState(stateAfter_);
 *         manager_.publishSnapshot();
 *         return true;
 *     }
 *     
 *     bool undo() override
 *     {
 *         manager_.restoreState(stateBefore_);
 *         manager_.publishSnapshot();
 *         return true;
 *     }
 *     
 *     int getSizeInUnits() override { return 256; }
 *     juce::String getDescription() const override { return actionName_; }
 * 
 * private:
 *     AutomationManagerCore& manager_;
 *     juce::String actionName_;
 *     std::function<void()> doFunc_, undoFunc_;
 *     juce::var stateBefore_, stateAfter_;
 * };
 * 
 * Usage:
 * undoManager.beginNewTransaction("Flip Clip Vertically");
 * undoManager.perform(new AutomationUndoableAction(
 *     manager_, "Flip Clip", 
 *     [&helper, clipId]() { helper.flipClipVertically(clipId); },
 *     []() { /* Will be called on undo */ }
 * ));
 */

/*
 * PATTERN: Real-Time Parameter Binding
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Control value changes during editing need to sync with automation
 * 
 * Solution: Live parameter binding
 * 
 * class LiveParameterBinding
 * {
 * public:
 *     LiveParameterBinding(AutomationUIHelper& helper,
 *                         const TrackID& trackId,
 *                         const juce::String& parameterId)
 *         : helper_(helper), trackId_(trackId), parameterId_(parameterId),
 *           lastTime_(0)
 *     {
 *     }
 *     
 *     void recordValue(float value, int64_t timeSamples)
 *     {
 *         // Only record if time has advanced enough (debounce)
 *         if (timeSamples - lastTime_ < 100)
 *             return;
 *         
 *         auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
 *         int idx = lane.insertPoint(timeSamples, value);
 *         lastTime_ = timeSamples;
 *     }
 *     
 *     void finalize()
 *     {
 *         helper_.publishSnapshot();
 *     }
 * 
 * private:
 *     AutomationUIHelper& helper_;
 *     TrackID trackId_;
 *     juce::String parameterId_;
 *     int64_t lastTime_;
 * };
 * 
 * Usage (during knob drag):
 * void KnobComponent::mouseDrag(const juce::MouseEvent& e)
 * {
 *     float newValue = calculateValueFromMouse(e);
 *     binding.recordValue(newValue, getCurrentTimeSamples());
 * }
 * 
 * void KnobComponent::mouseUp(const juce::MouseEvent& e)
 * {
 *     binding.finalize();
 * }
 */

/*
 * PATTERN: Curve Preview Rendering Optimization
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Rendering curves with 1000+ points is slow
 * 
 * Solution: Cached curve paths with LOD (Level of Detail)
 * 
 * class OptimizedCurveRenderer
 * {
 * public:
 *     OptimizedCurveRenderer(const AutomationLaneCore& lane, int maxPointsForLOD = 100)
 *         : lane_(lane), maxPointsForLOD_(maxPointsForLOD), cacheValid_(false)
 *     {
 *     }
 *     
 *     juce::Path getCurvePath(juce::Rectangle<float> bounds, int maxPathSegments)
 *     {
 *         // Check cache validity
 *         if (!cacheValid_ || lastBounds_ != bounds)
 *         {
 *             regenerateCache(bounds, maxPathSegments);
 *             cacheValid_ = true;
 *             lastBounds_ = bounds;
 *         }
 *         return cachedPath_;
 *     }
 *     
 *     void invalidateCache()
 *     {
 *         cacheValid_ = false;
 *     }
 * 
 * private:
 *     void regenerateCache(juce::Rectangle<float> bounds, int maxPathSegments)
 *     {
 *         const auto& points = lane_.getPoints();
 *         
 *         // Decimate points if too many
 *         int decimation = 1;
 *         if (points.size() > maxPointsForLOD_)
 *             decimation = (int)points.size() / maxPointsForLOD_;
 *         
 *         cachedPath_.clear();
 *         bool firstPoint = true;
 *         
 *         for (size_t i = 0; i < points.size(); i += decimation)
 *         {
 *             const auto& point = points[i];
 *             float x = bounds.getX() + (float)point.timeSamples / lane_.getTotalSamples() * bounds.getWidth();
 *             float y = bounds.getBottomLeft().y - point.value * bounds.getHeight();
 *             
 *             if (firstPoint)
 *             {
 *                 cachedPath_.startNewSubPath(x, y);
 *                 firstPoint = false;
 *             }
 *             else
 *             {
 *                 cachedPath_.lineTo(x, y);
 *             }
 *         }
 *     }
 *     
 *     const AutomationLaneCore& lane_;
 *     juce::Path cachedPath_;
 *     juce::Rectangle<float> lastBounds_;
 *     bool cacheValid_;
 *     int maxPointsForLOD_;
 * };
 * 
 * Usage in paint():
 * OptimizedCurveRenderer renderer(lane);
 * g.strokePath(renderer.getCurvePath(getLocalBounds().toFloat(), 500), 
 *              juce::PathStrokeType(1.0f));
 */

/*
 * PATTERN: Multi-Select Points for Batch Operations
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: User wants to select multiple points and apply operation to all
 * 
 * Solution: Multi-select tracking with batch operation support
 * 
 * class MultiSelectAutomationController
 * {
 * public:
 *     void selectPoint(int pointIndex, bool addToSelection = false)
 *     {
 *         if (!addToSelection)
 *             selectedPoints_.clear();
 *         selectedPoints_.insert(pointIndex);
 *     }
 *     
 *     void deselectPoint(int pointIndex)
 *     {
 *         selectedPoints_.erase(pointIndex);
 *     }
 *     
 *     void clearSelection()
 *     {
 *         selectedPoints_.clear();
 *     }
 *     
 *     bool isPointSelected(int pointIndex) const
 *     {
 *         return selectedPoints_.count(pointIndex) > 0;
 *     }
 *     
 *     void deleteAllSelected(AutomationUIHelper& helper,
 *                           const TrackID& trackId,
 *                           const juce::String& parameterId)
 *     {
 *         // Delete in reverse order to avoid index shifting
 *         for (auto it = selectedPoints_.rbegin(); it != selectedPoints_.rend(); ++it)
 *         {
 *             helper.deletePoint(trackId, parameterId, *it);
 *         }
 *         selectedPoints_.clear();
 *         helper.publishSnapshot();
 *     }
 *     
 *     void resetAllSelected(AutomationUIHelper& helper,
 *                          const TrackID& trackId,
 *                          const juce::String& parameterId)
 *     {
 *         for (int idx : selectedPoints_)
 *         {
 *             helper.resetPointValue(trackId, parameterId, idx);
 *         }
 *         helper.publishSnapshot();
 *     }
 * 
 * private:
 *     std::set<int> selectedPoints_;
 * };
 */

/*
 * PATTERN: Drag-to-Draw Automation
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: User wants to draw automation freehand by dragging
 * 
 * Solution: Freehand automation drawing
 * 
 * class FreehandAutomationDrawer
 * {
 * public:
 *     FreehandAutomationDrawer(AutomationUIHelper& helper,
 *                             const TrackID& trackId,
 *                             const juce::String& parameterId,
 *                             int64_t samplingIntervalSamples = 4410)  // ~100ms at 44.1k
 *         : helper_(helper), trackId_(trackId), parameterId_(parameterId),
 *           samplingInterval_(samplingIntervalSamples), isDrawing_(false)
 *     {
 *     }
 *     
 *     void startDrawing()
 *     {
 *         isDrawing_ = true;
 *         drawnPoints_.clear();
 *     }
 *     
 *     void addDrawnPoint(float value, int64_t timeSamples)
 *     {
 *         if (!isDrawing_) return;
 *         
 *         // Only add if time delta is large enough
 *         if (drawnPoints_.empty() || 
 *             timeSamples - drawnPoints_.back().time >= samplingInterval_)
 *         {
 *             drawnPoints_.push_back({timeSamples, value});
 *         }
 *     }
 *     
 *     void finishDrawing()
 *     {
 *         if (!isDrawing_) return;
 *         
 *         auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
 *         
 *         for (const auto& pt : drawnPoints_)
 *         {
 *             lane.insertPoint(pt.time, pt.value);
 *         }
 *         
 *         helper_.publishSnapshot();
 *         isDrawing_ = false;
 *         drawnPoints_.clear();
 *     }
 * 
 * private:
 *     struct Point { int64_t time; float value; };
 *     AutomationUIHelper& helper_;
 *     TrackID trackId_;
 *     juce::String parameterId_;
 *     int64_t samplingInterval_;
 *     bool isDrawing_;
 *     std::vector<Point> drawnPoints_;
 * };
 */

} // namespace DAW

// ============================================================================
// TROUBLESHOOTING GUIDE
// ============================================================================

/*
 * ISSUE: Curve not updating in UI after calling helper method
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: User sees stale curve display after operation
 * 
 * Solutions:
 * 1. Missing repaint() call:
 *    helper.setSegmentCurveType(...);
 *    repaint();  // ← REQUIRED
 * 
 * 2. Missing publishSnapshot() call:
 *    helper.setSegmentTension(...);
 *    helper.publishSnapshot();  // ← Call after batch operations
 *    repaint();
 * 
 * 3. Pointing to wrong lane/parameter:
 *    Check that trackId and parameterId are correct
 *    Use helper.findLane() to verify lane exists first
 */

/*
 * ISSUE: Context menu disappears immediately when clicked
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Menu doesn't stay visible or closes on first action
 * 
 * Solutions:
 * 1. Use showMenuAsync() not showMenu():
 *    menu.showMenuAsync(options);  // ← CORRECT (async, user can click)
 *    menu.show();                  // ← WRONG (blocks and closes immediately)
 * 
 * 2. Ensure menu is created on stack, not heap:
 *    juce::PopupMenu menu;  // ← Stack allocated
 *    auto* menu = new juce::PopupMenu();  // ← Avoid heap allocation
 * 
 * 3. Check e.mods.isPopupMenu() before building menu
 */

/*
 * ISSUE: Tension handle drag is jerky or unresponsive
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Tension value doesn't change smoothly during drag
 * 
 * Solutions:
 * 1. Ensure mouseDrag() is being called:
 *    void mouseDrag(const juce::MouseEvent& e) override
 *    {
 *        // This should fire continuously while dragging
 *    }
 * 
 * 2. Check drag sensitivity calculation:
 *    float dragAmount = deltaY / getHeight();  // ← Height-normalized
 *    NOT: float dragAmount = deltaY / 100;     // ← Fixed pixel count
 * 
 * 3. Clamp tension to valid range:
 *    tension = std::max(-0.99f, std::min(0.99f, tension));
 * 
 * 4. Publish after drag completes in mouseUp():
 *    void mouseUp(const juce::MouseEvent& e) override
 *    {
 *        helper.publishSnapshot();
 *    }
 */

/*
 * ISSUE: Audio glitches when editing automation during playback
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Pops, clicks, or dropouts when user interacts with automation
 * 
 * Solutions:
 * 1. This should NOT happen with proper implementation
 *    Verify you're using AutomationUIHelper methods (thread-safe)
 * 
 * 2. Don't call publishSnapshot() too frequently during drag:
 *    // DON'T do this:
 *    void mouseDrag(...) {
 *        helper.setSegmentTension(...);
 *        helper.publishSnapshot();  // ← Too frequent!
 *        repaint();
 *    }
 *    
 *    // DO this:
 *    void mouseDrag(...) {
 *        helper.setSegmentTension(...);
 *        repaint();  // ← Paint only
 *    }
 *    void mouseUp(...) {
 *        helper.publishSnapshot();  // ← Publish once at end
 *    }
 * 
 * 3. Ensure audio thread always has immutable snapshot
 *    Check AutomationSnapshotCore.h implementation
 */

/*
 * ISSUE: Memory usage grows continuously during long editing sessions
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: Memory isn't released after operations
 * 
 * Solutions:
 * 1. Verify AutomationClipClipboard is being cleared:
 *    helper.copyClipState(clipId);
 *    // After pasting, clipboard should be cleared by core
 *    // If not: check AutomationManagerCore clipboard implementation
 * 
 * 2. Check for accumulating undo history:
 *    Make sure undo manager isn't holding thousands of states
 *    Implement undo limit (e.g., keep last 100 actions)
 * 
 * 3. Verify paint() isn't allocating repeatedly:
 *    Use cachedPath_ member variable
 *    Only regenerate on property changes
 */

/*
 * ISSUE: Quick-create doesn't show automation lane
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Problem: After calling quickCreateTrackVolume(), lane isn't visible
 * 
 * Solutions:
 * 1. Verify lane was created:
 *    auto& lane = helper.quickCreateTrackVolume(trackId);
 *    if (lane.getPoints().size() >= 0)  // Lane created
 * 
 * 2. Set lane visibility explicitly:
 *    helper.quickCreateTrackVolume(trackId);
 *    helper.setLaneVisible(trackId, "track.volume", true);
 * 
 * 3. Check that ArrangementEditor is observing automation changes
 *    Verify observer pattern is set up correctly
 * 
 * 4. Refresh UI after creation:
 *    helper.publishSnapshot();
 *    arrangementView.refresh();  // or updateLayout()
 */

#endif // APEX_AUTOMATION_ADVANCED_PATTERNS_H

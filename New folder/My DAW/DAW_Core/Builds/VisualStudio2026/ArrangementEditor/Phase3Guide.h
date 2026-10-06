/**
 * PHASE 3: ADVANCED FEATURES & POLISH
 * 
 * OBJECTIVES:
 * ✓ Undo/Redo system for all operations
 * ✓ Keyboard shortcuts for common actions
 * ✓ Visual enhancements and feedback
 * ✓ Performance optimization
 * ✓ Edge case handling
 */

#pragma once

// ============================================================================
// PHASE 3 OVERVIEW
// ============================================================================

/*
 * PHASE 3 GOALS:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * 1. UNDO/REDO SYSTEM
 *    └─ Every user action reversible
 *    └─ Redo after undo
 *    └─ History management
 * 
 * 2. KEYBOARD SHORTCUTS
 *    ├─ Delete = Delete Point
 *    ├─ Ctrl+C = Copy Value
 *    ├─ Ctrl+V = Paste Value
 *    ├─ Ctrl+Z = Undo
 *    ├─ Ctrl+Shift+Z = Redo
 *    ├─ Alt+C = Copy Curve
 *    └─ Alt+V = Paste Curve
 * 
 * 3. VISUAL ENHANCEMENTS
 *    ├─ Hover effects (cursor changes)
 *    ├─ Selection highlights
 *    ├─ Animation on curve changes
 *    ├─ Tooltip previews
 *    └─ Better visual feedback
 * 
 * 4. OPTIMIZATION
 *    ├─ LOD rendering for many points
 *    ├─ Curve caching improvements
 *    ├─ Batch operations
 *    └─ Memory optimization
 * 
 * 5. POLISH
 *    ├─ Accessibility support
 *    ├─ Edge case handling
 *    ├─ Error messages
 *    └─ User guidance
 */

// ============================================================================
// PHASE 3 STEP 1: UNDO/REDO SYSTEM
// ============================================================================

/*
 * UNDO/REDO ARCHITECTURE:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Every user action creates an "action" that can be undone:
 * 
 * Action Types:
 * ├─ DeletePointAction
 * ├─ CopyPointValueAction (no undo needed)
 * ├─ PastePointValueAction
 * ├─ SetCurveTypeAction
 * ├─ SetTensionAction
 * ├─ DragTensionAction
 * └─ InsertPointAction
 * 
 * Each action stores:
 * ├─ trackId
 * ├─ parameterId
 * ├─ pointIndex/segmentIndex
 * ├─ before state (for undo)
 * └─ after state (for redo)
 * 
 * History Stack:
 * [Action1] [Action2] [Action3] ← Current
 *                     ↑ Undo pointer
 * 
 * Operations:
 * ├─ Do(action) → execute & add to history
 * ├─ Undo() → revert to before state
 * ├─ Redo() → revert to after state
 * └─ Clear() → on new action after undo
 * 
 * IMPLEMENTATION STRATEGY:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * Create AutomationAction base class:
 * 
 * ```cpp
 * class AutomationAction
 * {
 * public:
 *     virtual ~AutomationAction() = default;
 *     virtual void redo() = 0;
 *     virtual void undo() = 0;
 *     virtual juce::String getDescription() const = 0;
 * };
 * ```
 * 
 * Create specific actions:
 * 
 * ```cpp
 * class DeletePointAction : public AutomationAction
 * {
 *     TrackID trackId;
 *     juce::String parameterId;
 *     int pointIndex;
 *     AutomationPoint savedPoint;
 *     
 *     void redo() override {
 *         manager.removePoint(trackId, parameterId, pointIndex);
 *     }
 *     
 *     void undo() override {
 *         manager.insertPoint(trackId, parameterId, pointIndex, 
 *                            savedPoint.timeSamples, savedPoint.value);
 *     }
 * };
 * ```
 * 
 * Create UndoManager:
 * 
 * ```cpp
 * class UndoManager
 * {
 *     std::vector<std::unique_ptr<AutomationAction>> history;
 *     size_t currentIndex = 0;
 *     
 *     void doAction(std::unique_ptr<AutomationAction> action) {
 *         // Clear redo history
 *         history.erase(history.begin() + currentIndex + 1, history.end());
 *         
 *         // Execute action
 *         action->redo();
 *         
 *         // Add to history
 *         history.push_back(std::move(action));
 *         currentIndex++;
 *     }
 *     
 *     void undo() {
 *         if (currentIndex > 0) {
 *             history[currentIndex]->undo();
 *             currentIndex--;
 *         }
 *     }
 *     
 *     void redo() {
 *         if (currentIndex < history.size() - 1) {
 *             currentIndex++;
 *             history[currentIndex]->redo();
 *         }
 *     }
 * };
 * ```
 * 
 * Wire to AutomationLaneComponent:
 * 
 * In showPointContextMenu():
 * ```cpp
 * menu.addItem("Delete Point", [this, pointIndex]() {
 *     auto action = std::make_unique<DeletePointAction>(
 *         trackId_, parameterId_, pointIndex, 
 *         *helper_.findLane(trackId_, parameterId_)->points[pointIndex]);
 *     undoManager_.doAction(std::move(action));
 *     invalidateCurvePath();
 *     repaint();
 * });
 * ```
 */

// ============================================================================
// PHASE 3 STEP 2: KEYBOARD SHORTCUTS
// ============================================================================

/*
 * KEYBOARD SHORTCUTS IMPLEMENTATION:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Add keyPressed() to AutomationLaneComponent:
 * 
 * ```cpp
 * bool AutomationLaneComponent::keyPressed(const juce::KeyPress& key) override
 * {
 *     // Delete selected point
 *     if (key.getKeyCode() == juce::KeyPress::deleteKey && selectedPointIndex_ >= 0)
 *     {
 *         helper_.deletePoint(trackId_, parameterId_, selectedPointIndex_);
 *         selectedPointIndex_ = -1;
 *         invalidateCurvePath();
 *         repaint();
 *         return true;
 *     }
 *     
 *     // Ctrl+C = Copy Value
 *     if (key == juce::KeyPress('c', juce::ModifierKeys::ctrlModifier))
 *     {
 *         if (selectedPointIndex_ >= 0)
 *             helper_.copyPointValue(trackId_, parameterId_, selectedPointIndex_);
 *         return true;
 *     }
 *     
 *     // Ctrl+V = Paste Value
 *     if (key == juce::KeyPress('v', juce::ModifierKeys::ctrlModifier))
 *     {
 *         if (selectedPointIndex_ >= 0)
 *             helper_.pastePointValue(trackId_, parameterId_, selectedPointIndex_);
 *         invalidateCurvePath();
 *         repaint();
 *         return true;
 *     }
 *     
 *     // Ctrl+Z = Undo
 *     if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier))
 *     {
 *         undoManager_.undo();
 *         invalidateCurvePath();
 *         repaint();
 *         return true;
 *     }
 *     
 *     // Ctrl+Shift+Z = Redo
 *     if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier))
 *     {
 *         undoManager_.redo();
 *         invalidateCurvePath();
 *         repaint();
 *         return true;
 *     }
 *     
 *     return false;
 * }
 * ```
 * 
 * SHORTCUTS TABLE:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * Delete           → Delete selected point
 * Ctrl+C           → Copy point value
 * Ctrl+V           → Paste point value
 * Ctrl+Z           → Undo last action
 * Ctrl+Shift+Z     → Redo last undone action
 * Alt+C            → Copy segment curve
 * Alt+V            → Paste segment curve
 * Ctrl+A           → Select all points
 * Escape           → Deselect all
 * Arrow Up/Down    → Adjust tension
 * Arrow Left/Right → Move point in time
 */

// ============================================================================
// PHASE 3 STEP 3: VISUAL ENHANCEMENTS
// ============================================================================

/*
 * HOVER EFFECTS:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Add mouseMove() to AutomationLaneComponent:
 * 
 * ```cpp
 * void AutomationLaneComponent::mouseMove(const juce::MouseEvent& e) override
 * {
 *     auto* lane = helper_.findLane(trackId_, parameterId_);
 *     if (!lane)
 *         return;
 *     
 *     bool hoveredPoint = false;
 *     bool hoveredTension = false;
 *     bool hoveredSegment = false;
 *     
 *     // Check for point hover
 *     for (size_t i = 0; i < lane->points.size(); ++i)
 *     {
 *         if (isClickOnPoint(e, lane->points[i]))
 *         {
 *             hoveredPoint = true;
 *             setMouseCursor(juce::MouseCursor::PointingHandCursor);
 *             break;
 *         }
 *     }
 *     
 *     // Check for tension handle hover
 *     if (!hoveredPoint)
 *     {
 *         for (size_t i = 0; i < lane->points.size() - 1; ++i)
 *         {
 *             if (isClickOnTensionHandle(e, lane->points[i], lane->points[i + 1]))
 *             {
 *                 hoveredTension = true;
 *                 setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
 *                 break;
 *             }
 *         }
 *     }
 *     
 *     // Check for segment hover
 *     if (!hoveredPoint && !hoveredTension)
 *     {
 *         for (size_t i = 0; i < lane->points.size() - 1; ++i)
 *         {
 *             if (isClickOnSegment(e, lane->points[i], lane->points[i + 1]))
 *             {
 *                 hoveredSegment = true;
 *                 setMouseCursor(juce::MouseCursor::CrosshairCursor);
 *                 break;
 *             }
 *         }
 *     }
 *     
 *     if (!hoveredPoint && !hoveredTension && !hoveredSegment)
 *         setMouseCursor(juce::MouseCursor::NormalCursor);
 *     
 *     // Update hover state for rendering
 *     hoveredPointIndex_ = hoveredPoint ? getHoveredPointIndex(e) : -1;
 *     hoveredSegmentIndex_ = hoveredSegment ? getHoveredSegmentIndex(e) : -1;
 *     
 *     repaint();
 * }
 * ```
 * 
 * SELECTION HIGHLIGHT:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * Add to paint():
 * 
 * ```cpp
 * // Draw hover/selection highlight
 * if (hoveredPointIndex_ >= 0)
 * {
 *     auto& point = lane->points[hoveredPointIndex_];
 *     float x = bounds.getX() + (float)point.timeSamples / samplesPerPixel_;
 *     float y = bounds.getBottomLeft().y - point.value * bounds.getHeight();
 *     
 *     g.setColour(juce::Colours::yellow.withAlpha(0.3f));
 *     g.fillEllipse(x - 8.0f, y - 8.0f, 16.0f, 16.0f);
 * }
 * ```
 */

// ============================================================================
// PHASE 3 STEP 4: PERFORMANCE OPTIMIZATION
// ============================================================================

/*
 * LOD (LEVEL-OF-DETAIL) RENDERING:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * For curves with 1000+ points, render with less detail:
 * 
 * ```cpp
 * int AutomationLaneComponent::getSubdivisionCount() const
 * {
 *     auto* lane = helper_.findLane(trackId_, parameterId_);
 *     if (!lane)
 *         return 50;
 *     
 *     // LOD based on point count
 *     if (lane->points.size() > 1000)
 *         return 10;  // Low detail
 *     else if (lane->points.size() > 500)
 *         return 20;  // Medium detail
 *     else if (lane->points.size() > 100)
 *         return 30;  // Good detail
 *     else
 *         return 50;  // High detail
 * }
 * ```
 * 
 * Use in regenerateCurvePath():
 * 
 * ```cpp
 * const int numSegments = getSubdivisionCount();
 * for (int seg = 1; seg <= numSegments; ++seg)
 * {
 *     // ... existing code ...
 * }
 * ```
 * 
 * BATCH OPERATIONS:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * For operations on multiple points:
 * 
 * ```cpp
 * class BatchCurveOperation
 * {
 *     std::vector<int> affectedSegments;
 *     std::vector<AutomationCurveType> newTypes;
 *     
 *     void apply(AutomationManagerCore& manager) {
 *         for (size_t i = 0; i < affectedSegments.size(); ++i)
 *             manager.setPointCurveToNext(..., newTypes[i]);
 *         manager.publishSnapshot();  // Single publish!
 *     }
 * };
 * ```
 */

// ============================================================================
// PHASE 3 STEP 5: EDGE CASE HANDLING
// ============================================================================

/*
 * EDGE CASES TO HANDLE:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * 1. MINIMUM POINTS
 *    └─ Prevent deleting when only 2 points remain
 *    ```cpp
 *    if (lane->points.size() <= 2) {
 *        // Show "Cannot delete - minimum 2 points required"
 *        return;
 *    }
 *    ```
 * 
 * 2. INVALID TIMES
 *    └─ Prevent inserting at negative time
 *    ```cpp
 *    if (timeSamples < 0) {
 *        timeSamples = 0;
 *    }
 *    ```
 * 
 * 3. DUPLICATE TIMES
 *    └─ Handle multiple points at same time
 *    ```cpp
 *    // Check if time already has point
 *    for (auto& pt : lane->points) {
 *        if (pt.timeSamples == timeSamples) {
 *            // Move to adjacent time or show error
 *        }
 *    }
 *    ```
 * 
 * 4. DRAG OUTSIDE BOUNDS
 *    └─ Clamp to valid range
 *    ```cpp
 *    tension = juce::jlimit(-0.99f, 0.99f, tension);
 *    value = juce::jlimit(0.0f, 1.0f, value);
 *    ```
 * 
 * 5. EMPTY CLIPBOARD
 *    └─ Gray out paste when nothing copied
 *    ```cpp
 *    if (helper_.canPastePointValue()) {
 *        menu.addItem("Paste Value", ...);
 *    }
 *    ```
 */

// ============================================================================
// PHASE 3 IMPLEMENTATION ORDER
// ============================================================================

/*
 * DAY 1: UNDO/REDO SYSTEM
 * ─────────────────────────────────────────────────────────────────────────
 * [ ] Create AutomationAction base class
 * [ ] Create specific action classes (Delete, Paste, SetType, SetTension)
 * [ ] Create UndoManager class
 * [ ] Wire actions to menu callbacks
 * [ ] Test undo/redo functionality
 * [ ] Build verification
 * 
 * DAY 2: KEYBOARD SHORTCUTS
 * ─────────────────────────────────────────────────────────────────────────
 * [ ] Add keyPressed() to component
 * [ ] Implement Delete shortcut
 * [ ] Implement Ctrl+C/V shortcuts
 * [ ] Implement Ctrl+Z/Ctrl+Shift+Z
 * [ ] Test all shortcuts
 * [ ] Build verification
 * 
 * DAY 3: VISUAL ENHANCEMENTS
 * ─────────────────────────────────────────────────────────────────────────
 * [ ] Add mouseMove() handler
 * [ ] Implement hover cursor changes
 * [ ] Add selection highlighting
 * [ ] Add hover effects to paint
 * [ ] Test visual feedback
 * [ ] Build verification
 * 
 * DAYS 4-5: OPTIMIZATION & POLISH
 * ─────────────────────────────────────────────────────────────────────────
 * [ ] Implement LOD rendering
 * [ ] Optimize curve caching
 * [ ] Handle edge cases
 * [ ] Add error messages
 * [ ] Performance testing
 * [ ] Build verification
 * 
 * DAYS 6-7: FINAL TESTING & DEPLOYMENT
 * ─────────────────────────────────────────────────────────────────────────
 * [ ] Comprehensive testing
 * [ ] Performance profiling
 * [ ] Edge case verification
 * [ ] Documentation update
 * [ ] Final build
 * [ ] Deployment ready
 */

// ============================================================================
// PHASE 3 SUCCESS CRITERIA
// ============================================================================

/*
 * UNDO/REDO: ✓
 * ├─ All operations undoable
 * ├─ Redo works after undo
 * ├─ History persists correctly
 * └─ No state corruption
 * 
 * SHORTCUTS: ✓
 * ├─ All shortcuts responsive
 * ├─ No conflicts with OS shortcuts
 * ├─ Documentation clear
 * └─ User discoverable
 * 
 * VISUAL: ✓
 * ├─ Hover effects work
 * ├─ Selection visible
 * ├─ Cursor changes appropriate
 * └─ No visual glitches
 * 
 * PERFORMANCE: ✓
 * ├─ LOD improves large curves
 * ├─ Batch operations faster
 * ├─ No frame drops
 * └─ Memory stable
 * 
 * BUILD: ✓
 * ├─ 0 errors
 * ├─ 0 warnings
 * ├─ All symbols resolved
 * └─ Production ready
 */

#endif // APEX_AUTOMATION_PHASE_3_GUIDE_H

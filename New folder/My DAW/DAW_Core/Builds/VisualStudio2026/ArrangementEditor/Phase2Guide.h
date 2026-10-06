/**
 * PHASE 2: USER INTERACTION — IMPLEMENTATION GUIDE
 * 
 * PHASE 2 OBJECTIVES (Week 2):
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Enable users to interact with automation curves:
 * ✓ Right-click point menu (delete, copy, paste, set exact value)
 * ✓ Right-click segment menu (add point, set curve type, set tension)
 * ✓ Shift+RightClick point insertion (preserve level)
 * ✓ Drag tension handles to modify curve shape
 * ✓ Visual feedback during interaction
 * ✓ Immediate curve updates
 * 
 * DELIVERABLES:
 * ✓ Point interaction component
 * ✓ Segment interaction component
 * ✓ Tension handle drag component
 * ✓ Context menus (7 complete patterns in UI Reference)
 * ✓ Real-time feedback
 */

#pragma once

// ============================================================================
// PHASE 2 ARCHITECTURE
// ============================================================================

/*
 * PHASE 2 ADDS INTERACTION LAYERS:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * AutomationLaneComponent (from Phase 1)
 * └── Paint rendering (curves, points, grid)
 * └── Mouse events → interaction handling (NEW PHASE 2)
 *     ├── mouseDown() → Detect click target
 *     ├── mouseDrag() → Handle interaction
 *     └── mouseUp() → Finalize and publish
 * 
 * Click Detection:
 * 1. Point click? → Show point context menu
 * 2. Segment click? → Show segment context menu
 * 3. Shift+RightClick? → Insert point
 * 4. Tension handle? → Start drag
 * 5. Curve drag? → (Phase 3+)
 */

// ============================================================================
// PHASE 2 STEP 1: POINT INTERACTION
// ============================================================================

/*
 * OBJECTIVE:
 * User right-clicks on a point → context menu with options
 * 
 * Options:
 * - Delete Point
 * - Copy Point Value
 * - Paste Point Value
 * - Set Exact Time
 * - Set Exact Value
 * - Reset to Default
 * 
 * IMPLEMENTATION:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * 1. Detect point click in AutomationLaneComponent::mouseDown()
 * 
 *    void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 *    {
 *        auto* lane = helper_.findLane(trackId_, parameterId_);
 *        if (!lane)
 *            return;
 *        
 *        // Check if click is on a point
 *        for (size_t i = 0; i < lane->points.size(); ++i)
 *        {
 *            if (isClickOnPoint(e, lane->points[i]))
 *            {
 *                if (e.mods.isPopupMenu())  // Right-click
 *                {
 *                    showPointContextMenu(e, i);
 *                }
 *                return;
 *            }
 *        }
 *    }
 * 
 * 2. Check if click is within point bounds
 * 
 *    bool AutomationLaneComponent::isClickOnPoint(const juce::MouseEvent& e,
 *                                                  const AutomationPoint& point)
 *    {
 *        auto bounds = getLocalBounds().toFloat();
 *        float x = bounds.getX() + (float)point.timeSamples / samplesPerPixel_;
 *        float y = bounds.getBottomLeft().y - point.value * bounds.getHeight();
 *        
 *        float clickRadius = 5.0f;  // pixels
 *        return (float)e.x >= x - clickRadius && (float)e.x <= x + clickRadius &&
 *               (float)e.y >= y - clickRadius && (float)e.y <= y + clickRadius;
 *    }
 * 
 * 3. Show context menu
 * 
 *    void AutomationLaneComponent::showPointContextMenu(const juce::MouseEvent& e,
 *                                                       size_t pointIndex)
 *    {
 *        juce::PopupMenu menu;
 *        
 *        // Delete
 *        menu.addItem("Delete Point", [this, pointIndex]() {
 *            helper_.deletePoint(trackId_, parameterId_, (int)pointIndex);
 *        });
 *        
 *        menu.addSeparator();
 *        
 *        // Copy/Paste
 *        menu.addItem("Copy Value", [this, pointIndex]() {
 *            helper_.copyPointValue(trackId_, parameterId_, (int)pointIndex);
 *        });
 *        
 *        if (helper_.canPastePointValue()) {
 *            menu.addItem("Paste Value", [this, pointIndex]() {
 *                helper_.pastePointValue(trackId_, parameterId_, (int)pointIndex);
 *            });
 *        }
 *        
 *        // Show menu at mouse position
 *        menu.showMenuAsync(
 *            juce::PopupMenu::Options()
 *                .withTargetComponent(this)
 *                .withTargetScreenArea(e.getScreenPosition().toInt().withSize(1, 1)),
 *            nullptr);
 *    }
 * 
 * See: AutomationUIReference.h Pattern 1 for complete implementation
 */

// ============================================================================
// PHASE 2 STEP 2: SEGMENT INTERACTION
// ============================================================================

/*
 * OBJECTIVE:
 * User right-clicks on a curve segment → context menu with curve options
 * 
 * Options:
 * - Add Point
 * - Set Curve Type (submenu with 14 types)
 * - Set Tension Value
 * - Reset Tension
 * 
 * IMPLEMENTATION:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * 1. Detect segment click
 * 
 *    void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 *    {
 *        auto* lane = helper_.findLane(trackId_, parameterId_);
 *        
 *        // Check for point click first (higher priority)
 *        for (size_t i = 0; i < lane->points.size(); ++i)
 *        {
 *            if (isClickOnPoint(e, lane->points[i]))
 *            {
 *                // Point interaction
 *                if (e.mods.isPopupMenu())
 *                    showPointContextMenu(e, i);
 *                return;
 *            }
 *        }
 *        
 *        // Check for segment click
 *        for (size_t i = 0; i < lane->points.size() - 1; ++i)
 *        {
 *            if (isClickOnSegment(e, lane->points[i], lane->points[i + 1]))
 *            {
 *                if (e.mods.isPopupMenu())
 *                    showSegmentContextMenu(e, i);
 *                return;
 *            }
 *        }
 *    }
 * 
 * 2. Check if click is on curve segment
 * 
 *    bool AutomationLaneComponent::isClickOnSegment(const juce::MouseEvent& e,
 *                                                    const AutomationPoint& a,
 *                                                    const AutomationPoint& b)
 *    {
 *        auto bounds = getLocalBounds().toFloat();
 *        
 *        // Sample curve at regular intervals
 *        const int samples = 50;
 *        float clickRadius = 3.0f;
 *        
 *        for (int i = 0; i <= samples; ++i)
 *        {
 *            float t = (float)i / (float)samples;
 *            
 *            // Evaluate curve position
 *            float shapedT = AutomationCurveEvalCore::shapePosition(
 *                t, a.curveToNext, a.tensionToNext);
 *            
 *            float x = bounds.getX() + (a.timeSamples + (b.timeSamples - a.timeSamples) * t) / samplesPerPixel_;
 *            float y = bounds.getBottomLeft().y - (a.value + (b.value - a.value) * shapedT) * bounds.getHeight();
 *            
 *            if (std::abs((float)e.x - x) < clickRadius && std::abs((float)e.y - y) < clickRadius)
 *                return true;
 *        }
 *        return false;
 *    }
 * 
 * 3. Show segment context menu
 * 
 *    void AutomationLaneComponent::showSegmentContextMenu(const juce::MouseEvent& e,
 *                                                         size_t segmentIndex)
 *    {
 *        juce::PopupMenu menu;
 *        
 *        // Add Point
 *        menu.addItem("Add Point Here", [this, e, segmentIndex]() {
 *            auto* lane = helper_.findLane(trackId_, parameterId_);
 *            if (lane && segmentIndex + 1 < lane->points.size())
 *            {
 *                // Calculate insertion point based on click position
 *                insertPointAtClickLocation(e, segmentIndex);
 *            }
 *        });
 *        
 *        menu.addSeparator();
 *        
 *        // Curve type submenu
 *        juce::PopupMenu curveMenu;
 *        auto curveTypes = AutomationCurveTypeHelper::getAllCurveTypes();
 *        for (size_t i = 0; i < curveTypes.size(); ++i)
 *        {
 *            auto type = curveTypes[i];
 *            curveMenu.addItem(AutomationCurveTypeHelper::getDisplayName(type),
 *                [this, segmentIndex, type]() {
 *                    helper_.setSegmentCurveType(trackId_, parameterId_, 
 *                                               (int)segmentIndex, type);
 *                });
 *        }
 *        menu.addSubMenu("Set Curve Type", curveMenu);
 *        
 *        // Tension slider/value
 *        menu.addItem("Reset Tension", [this, segmentIndex]() {
 *            helper_.setSegmentTension(trackId_, parameterId_, (int)segmentIndex, 0.0f);
 *        });
 *        
 *        menu.showMenuAsync(
 *            juce::PopupMenu::Options()
 *                .withTargetComponent(this)
 *                .withTargetScreenArea(e.getScreenPosition().toInt().withSize(1, 1)),
 *            nullptr);
 *    }
 * 
 * See: AutomationUIReference.h Pattern 2 for complete implementation
 */

// ============================================================================
// PHASE 2 STEP 3: TENSION HANDLE DRAG
// ============================================================================

/*
 * OBJECTIVE:
 * User clicks and drags tension handle → modify curve shape interactively
 * 
 * Visual Feedback:
 * - Show tension indicator (square) at midpoint
 * - Color changes as tension changes
 * - Size varies with tension magnitude
 * 
 * IMPLEMENTATION:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * 1. Detect tension handle click
 * 
 *    void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 *    {
 *        // ... point check ...
 *        // ... segment check ...
 *        
 *        // Check for tension handle
 *        auto* lane = helper_.findLane(trackId_, parameterId_);
 *        for (size_t i = 0; i < lane->points.size() - 1; ++i)
 *        {
 *            if (isClickOnTensionHandle(e, lane->points[i], lane->points[i + 1]))
 *            {
 *                startTensionDrag(i, e);
 *                return;
 *            }
 *        }
 *    }
 * 
 * 2. Start drag state
 * 
 *    void AutomationLaneComponent::startTensionDrag(size_t segmentIndex,
 *                                                    const juce::MouseEvent& e)
 *    {
 *        tensionDragActive_ = true;
 *        tensionDragSegmentIndex_ = segmentIndex;
 *        tensionDragStartY_ = e.y;
 *        tensionDragStartValue_ = 0.0f;
 *        
 *        if (auto* lane = helper_.findLane(trackId_, parameterId_))
 *        {
 *            if (segmentIndex < lane->points.size())
 *                tensionDragStartValue_ = lane->points[segmentIndex].tensionToNext;
 *        }
 *    }
 * 
 * 3. Handle drag motion
 * 
 *    void AutomationLaneComponent::mouseDrag(const juce::MouseEvent& e)
 *    {
 *        if (!tensionDragActive_)
 *            return;
 *        
 *        // Calculate tension change based on vertical drag
 *        float deltaY = (float)(tensionDragStartY_ - e.y);
 *        float dragSensitivity = 0.01f;  // per pixel
 *        float newTension = tensionDragStartValue_ + deltaY * dragSensitivity;
 *        
 *        // Clamp to -1..1
 *        newTension = juce::jlimit(-0.99f, 0.99f, newTension);
 *        
 *        // Update tension (without publishing yet)
 *        helper_.setSegmentTension(trackId_, parameterId_, 
 *                                 (int)tensionDragSegmentIndex_, newTension);
 *        
 *        // Trigger repaint for visual feedback
 *        repaint();
 *    }
 * 
 * 4. Finalize drag
 * 
 *    void AutomationLaneComponent::mouseUp(const juce::MouseEvent& e)
 *    {
 *        if (tensionDragActive_)
 *        {
 *            tensionDragActive_ = false;
 *            
 *            // Publish final state
 *            helper_.publishSnapshot();
 *            
 *            invalidateCurvePath();
 *            repaint();
 *        }
 *    }
 * 
 * See: AutomationUIReference.h Pattern 3 for complete implementation
 */

// ============================================================================
// PHASE 2 STEP 4: SHIFT+RIGHTCLICK POINT INSERTION
// ============================================================================

/*
 * OBJECTIVE:
 * User Shift+RightClick on curve → insert point preserving level
 * 
 * Behavior:
 * - Insert point at click location
 * - Preserve current automation level
 * - Curve shape maintained
 * 
 * IMPLEMENTATION:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 * {
 *     if (e.mods.isShiftDown() && e.mods.isPopupMenu())
 *     {
 *         // Shift+RightClick → Insert point
 *         insertPointAtClickLocation(e);
 *         return;
 *     }
 *     
 *     // ... other interactions ...
 * }
 * 
 * void AutomationLaneComponent::insertPointAtClickLocation(const juce::MouseEvent& e)
 * {
 *     auto* lane = helper_.findLane(trackId_, parameterId_);
 *     if (!lane)
 *         return;
 *     
 *     auto bounds = getLocalBounds().toFloat();
 *     
 *     // Convert click to time and value
 *     int64_t timeSamples = (int64_t)((float)e.x - bounds.getX()) * samplesPerPixel_;
 *     float value = 1.0f - ((float)e.y - bounds.getY()) / bounds.getHeight();
 *     value = juce::jlimit(0.0f, 1.0f, value);
 *     
 *     // Find segment this time falls into
 *     size_t insertIndex = 0;
 *     for (size_t i = 0; i < lane->points.size(); ++i)
 *     {
 *         if (lane->points[i].timeSamples < timeSamples)
 *             insertIndex = i + 1;
 *         else
 *             break;
 *     }
 *     
 *     // Insert point
 *     helper_.insertPoint(trackId_, parameterId_, (int)insertIndex, 
 *                        timeSamples, value);
 * }
 * 
 * See: AutomationUIReference.h Pattern 4 for complete implementation
 */

// ============================================================================
// PHASE 2 IMPLEMENTATION ORDER
// ============================================================================

/*
 * DAY 1 (Monday): Point Interaction
 * ─────────────────────────────────
 * [ ] Add point click detection
 * [ ] Add context menu for points
 * [ ] Test point menu operations
 * [ ] Build + verify
 * 
 * DAY 2 (Tuesday): Segment Interaction
 * ──────────────────────────────────
 * [ ] Add segment click detection
 * [ ] Add curve type submenu
 * [ ] Add tension reset
 * [ ] Test segment menu operations
 * [ ] Build + verify
 * 
 * DAY 3 (Wednesday): Tension Drag
 * ─────────────────────────────────
 * [ ] Add tension handle click detection
 * [ ] Implement drag state machine
 * [ ] Add visual feedback during drag
 * [ ] Test tension modification
 * [ ] Build + verify
 * 
 * DAY 4 (Thursday): Shift+Click Insertion
 * ────────────────────────────────────
 * [ ] Add shift+click detection
 * [ ] Implement point insertion logic
 * [ ] Test point insertion
 * [ ] Build + verify
 * 
 * DAYS 5-7 (Fri-Sun): Testing & Polish
 * ──────────────────────────────────
 * [ ] Comprehensive interaction testing
 * [ ] Performance testing
 * [ ] Edge case handling
 * [ ] Documentation
 * [ ] Final polish
 */

// ============================================================================
// KEY HELPERS ALREADY AVAILABLE
// ============================================================================

/*
 * AutomationUIHelper has all needed methods:
 * 
 * Point Operations:
 * - deletePoint(trackId, parameterId, pointIndex)
 * - copyPointValue(trackId, parameterId, pointIndex)
 * - pastePointValue(trackId, parameterId, pointIndex)
 * - setPointExactValue(trackId, parameterId, pointIndex, value)
 * - setPointExactTime(trackId, parameterId, pointIndex, timeSamples)
 * 
 * Segment Operations:
 * - setSegmentCurveType(trackId, parameterId, segmentIndex, type)
 * - setSegmentTension(trackId, parameterId, segmentIndex, tension)
 * - insertPoint(trackId, parameterId, index, timeSamples, value)
 * 
 * General:
 * - publishSnapshot() — Finalize changes
 * - canPastePointValue() — Check clipboard
 * - getAllCurveTypes() — Get curve options
 */

// ============================================================================
// TESTING PHASE 2
// ============================================================================

/*
 * TEST CHECKLIST:
 * 
 * [ ] Right-click point shows menu
 * [ ] Delete point works
 * [ ] Copy/paste values work
 * [ ] Right-click segment shows menu
 * [ ] Curve type submenu appears (14 items)
 * [ ] Changing curve type updates display immediately
 * [ ] Tension reset works
 * [ ] Tension drag is smooth and responsive
 * [ ] Tension changes curve shape in real-time
 * [ ] Shift+RightClick inserts point
 * [ ] Inserted point preserves level
 * [ ] All operations are undo-able (Phase 3)
 * [ ] No performance degradation
 * [ ] No memory leaks
 */

#endif // APEX_AUTOMATION_PHASE_2_GUIDE_H

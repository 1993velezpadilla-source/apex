/**
 * PHASE 1: STEP 1 — COMPLETION & STEP 2 PREPARATION
 * 
 * ✓ STEP 1 COMPLETE: AutomationLaneContainerComponent created
 * 
 * Status: Ready for Step 2
 */

#pragma once

// ============================================================================
// STEP 1 COMPLETION SUMMARY
// ============================================================================

/*
 * STEP 1: CREATE AUTOMATION LANE CONTAINER ✓
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * FILE CREATED:
 * └─ ArrangementEditor\AutomationLaneContainerComponent.h ✓
 * 
 * WHAT WAS CREATED:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * AutomationLaneContainerComponent
 * └─ Purpose: Hold multiple AutomationLaneComponent instances
 * └─ Location: ArrangementEditor directory
 * └─ Dependencies: JUCE, AutomationLaneComponent, AutomationUIHelper
 * 
 * KEY FEATURES IMPLEMENTED:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Lane Management:
 * ✓ addLane(parameterId) — Create & add new automation lane
 * ✓ removeLane(parameterId) — Remove automation lane
 * ✓ getLane(parameterId) — Get lane component reference
 * ✓ hasLane(parameterId) — Check if lane exists
 * ✓ setLaneVisible(parameterId, visible) — Show/hide lane
 * ✓ clearAllLanes() — Remove all lanes
 * ✓ getAllParameterIds() — Get all parameter IDs
 * ✓ getNumLanes() — Count of lanes
 * 
 * Layout Management:
 * ✓ resized() — Stack lanes vertically (60px each)
 * ✓ getTotalVisibleHeight() — Get total height needed
 * ✓ paint() — Draw background
 * 
 * Zoom & Scale:
 * ✓ setSamplesPerPixel(samples) — Apply zoom to all lanes
 * ✓ setZoomLevel(zoom) — Apply zoom level to all lanes
 * 
 * Invalidation:
 * ✓ invalidateAllCurvePaths() — Force curve regeneration
 * 
 * HOW TO USE:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * // Create container
 * AutomationLaneContainerComponent container(helper, trackId);
 * addAndMakeVisible(container);
 * 
 * // Add lanes
 * container.addLane("track.volume");
 * container.addLane("track.pan");
 * 
 * // Update layout
 * int totalHeight = container.getTotalVisibleHeight();
 * container.setSize(containerWidth, totalHeight);
 * 
 * // When automation changes
 * container.invalidateAllCurvePaths();
 * container.repaint();
 */

// ============================================================================
// STEP 2: INTEGRATE WITH ARRANGEMENT EDITOR
// ============================================================================

/*
 * STEP 2 OVERVIEW:
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * OBJECTIVE:
 * Integrate AutomationLaneContainerComponent into ArrangementViewCore
 * so that automation lanes appear and update in the arrangement editor.
 * 
 * FILES TO MODIFY:
 * ─────────────────
 * 1. ArrangementEditor/ArrangementViewCore.h
 *    └─ Add member variable for automation lane containers
 *    └─ Add methods to manage automation lanes
 * 
 * 2. ArrangementEditor/ArrangementViewCore.cpp
 *    └─ Implement automation lane management
 *    └─ Wire up change listeners
 *    └─ Update layout/painting
 * 
 * WHAT NEEDS TO HAPPEN:
 * ─────────────────────
 * 
 * 1. Get reference to AutomationManagerCore
 *    └─ Available from application context
 *    └─ Create AutomationUIHelper(automationManager)
 * 
 * 2. Create AutomationLaneContainerComponent for each track
 *    └─ One container per track
 *    └─ Added to arrangement view
 *    └─ Positioned below track clips
 * 
 * 3. Add automation lanes on demand
 *    └─ When user requests lane visibility
 *    └─ When quick-create is triggered
 *    └─ Lane persists until user hides it
 * 
 * 4. Wire up change listeners
 *    └─ Listen to automation manager changes
 *    └─ Listen to track selection changes
 *    └─ Invalidate lanes when data updates
 * 
 * 5. Update layout
 *    └─ Resize automation containers as needed
 *    └─ Stack lanes vertically below clips
 *    └─ Update on zoom/pan
 */

// ============================================================================
// STEP 2 DETAILED IMPLEMENTATION GUIDE
// ============================================================================

/*
 * MODIFICATION 1: ArrangementViewCore.h
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Add to member variables:
 * ──────────────────────
 * 
 * private:
 *     // Automation support
 *     std::unique_ptr<AutomationUIHelper> automationHelper_;
 *     std::map<TrackID, std::unique_ptr<AutomationLaneContainerComponent>> automationContainers_;
 * 
 * Add methods to public interface:
 * ───────────────────────────────
 * 
 * public:
 *     // Automation lane management
 *     AutomationLaneContainerComponent* getOrCreateAutomationContainer(const TrackID& trackId);
 *     void showAutomationLane(const TrackID& trackId, const juce::String& parameterId);
 *     void hideAutomationLane(const TrackID& trackId, const juce::String& parameterId);
 *     void clearAutomationLanes(const TrackID& trackId);
 *     
 *     // Layout
 *     void updateAutomationLayout();
 */

/*
 * MODIFICATION 2: ArrangementViewCore.cpp — Constructor
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * In constructor, initialize automation:
 * ─────────────────────────────────────
 * 
 * ArrangementViewCore::ArrangementViewCore(/* params */)
 *     : /* ... existing initializers ... */
 * {
 *     // Initialize automation helper
 *     auto& automationManager = Application::getInstance().getAutomationManager();
 *     automationHelper_ = std::make_unique<AutomationUIHelper>(automationManager);
 *     
 *     // Subscribe to automation changes
 *     automationManager.addChangeListener(this);
 *     
 *     // ... rest of constructor ...
 * }
 */

/*
 * MODIFICATION 3: ArrangementViewCore.cpp — resized()
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Update resized() to position automation lanes:
 * ─────────────────────────────────────────────
 * 
 * void ArrangementViewCore::resized()
 * {
 *     // ... existing layout code for tracks/clips ...
 *     
 *     // Update automation containers below tracks
 *     updateAutomationLayout();
 * }
 * 
 * void ArrangementViewCore::updateAutomationLayout()
 * {
 *     for (auto& [trackId, container] : automationContainers_)
 *     {
 *         // Position each automation container
 *         // Typically below the track's clip area
 *         
 *         int yPos = getTrackYPosition(trackId) + getTrackHeight(trackId);
 *         int totalHeight = container->getTotalVisibleHeight();
 *         
 *         container->setBounds(0, yPos, getWidth(), totalHeight);
 *     }
 * }
 */

/*
 * MODIFICATION 4: ArrangementViewCore.cpp — changeListenerCallback()
 * ═══════════════════════════════════════════════════════════════════════
 * 
 * Implement to handle automation changes:
 * ──────────────────────────────────────
 * 
 * void ArrangementViewCore::changeListenerCallback(juce::ChangeBroadcaster* source)
 * {
 *     // When automation manager notifies us of changes
 *     
 *     // Invalidate all curve paths
 *     for (auto& [trackId, container] : automationContainers_)
 *     {
 *         container->invalidateAllCurvePaths();
 *     }
 *     
 *     // Repaint
 *     repaint();
 * }
 */

/*
 * MODIFICATION 5: Implement new methods
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * AutomationLaneContainerComponent* ArrangementViewCore::getOrCreateAutomationContainer(
 *     const TrackID& trackId)
 * {
 *     auto it = automationContainers_.find(trackId);
 *     if (it != automationContainers_.end())
 *         return it->second.get();
 *     
 *     // Create new container
 *     auto container = std::make_unique<AutomationLaneContainerComponent>(
 *         *automationHelper_, trackId
 *     );
 *     auto* ptr = container.get();
 *     
 *     addAndMakeVisible(container.get());
 *     automationContainers_[trackId] = std::move(container);
 *     
 *     resized();
 *     return ptr;
 * }
 * 
 * void ArrangementViewCore::showAutomationLane(const TrackID& trackId,
 *                                             const juce::String& parameterId)
 * {
 *     auto* container = getOrCreateAutomationContainer(trackId);
 *     if (!container->hasLane(parameterId))
 *         container->addLane(parameterId);
 *     container->setLaneVisible(parameterId, true);
 *     resized();
 * }
 * 
 * void ArrangementViewCore::hideAutomationLane(const TrackID& trackId,
 *                                             const juce::String& parameterId)
 * {
 *     auto it = automationContainers_.find(trackId);
 *     if (it != automationContainers_.end())
 *     {
 *         it->second->removeLane(parameterId);
 *         resized();
 *     }
 * }
 */

// ============================================================================
// INTEGRATION CHECKLIST
// ============================================================================

/*
 * STEP 2 PRE-IMPLEMENTATION CHECKLIST:
 * 
 * [ ] Read this guide completely
 * [ ] Examine ArrangementViewCore.h structure
 * [ ] Understand track layout (how clips are positioned)
 * [ ] Locate where tracks are drawn/painted
 * [ ] Find resized() method
 * [ ] Understand existing ChangeListener usage
 * [ ] Build project: 0 errors, 0 warnings
 * [ ] AutomationLaneContainerComponent.h included
 * 
 * STEP 2 IMPLEMENTATION CHECKLIST:
 * 
 * [ ] Add #include "AutomationLaneContainerComponent.h" to ArrangementViewCore.h
 * [ ] Add member: automationHelper_
 * [ ] Add member: automationContainers_
 * [ ] Add methods: getOrCreateAutomationContainer, showAutomationLane, etc.
 * [ ] Update constructor to initialize automationHelper_
 * [ ] Update resized() to call updateAutomationLayout()
 * [ ] Implement updateAutomationLayout()
 * [ ] Implement changeListenerCallback()
 * [ ] Build: 0 errors, 0 warnings
 * [ ] Test: Create test lane and verify it appears
 * [ ] Test: Verify lane is positioned correctly
 * [ ] Test: Verify lane updates on data change
 * 
 * STEP 2 TESTING CHECKLIST:
 * 
 * [ ] Automation container created successfully
 * [ ] Can add lanes to container
 * [ ] Lanes appear in arrangement view
 * [ ] Lanes positioned correctly below tracks
 * [ ] Multiple lanes stack vertically
 * [ ] Lane visibility toggle works
 * [ ] Lane removal works
 * [ ] Layout updates on resize
 * [ ] Change listener triggers on automation change
 * [ ] Container repaints on invalidation
 */

// ============================================================================
// KEY CONCEPTS FOR STEP 2
// ============================================================================

/*
 * TRACK POSITIONING:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * Need to know:
 * • How tracks are organized vertically
 * • Y position of each track
 * • Height of each track
 * • Whether to show automation above or below clips
 * 
 * Typical approach:
 * • Automation lanes go BELOW track clips
 * • Use getTotalVisibleHeight() to get height needed
 * • Position: y = trackBottom, x = 0, w = viewWidth, h = containerHeight
 */

/*
 * CHANGE LISTENING:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * When to invalidate curves:
 * • Point added/removed/modified
 * • Curve type changed
 * • Tension changed
 * • Lane enabled/disabled
 * 
 * How to trigger:
 * • Listen to AutomationManagerCore changes
 * • Call container->invalidateAllCurvePaths()
 * • Repaint view
 * • Update layout if height changes
 */

/*
 * ZOOM COORDINATION:
 * ─────────────────────────────────────────────────────────────────────────
 * 
 * When user zooms/pans:
 * • Update samplesPerPixel in all containers
 * • Update zoomLevel in all containers
 * • Curves regenerate (using caching)
 * • No repaint needed (happens automatically)
 */

// ============================================================================
// COMMON ISSUES & SOLUTIONS
// ============================================================================

/*
 * ISSUE: Lanes don't appear
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Causes:
 * 1. Container not added to parent with addAndMakeVisible()
 * 2. Container size is 0x0 (need to call setBounds or resized)
 * 3. Container is hidden (setVisible(false))
 * 4. No lanes added to container
 * 
 * Solution:
 * 1. Verify addAndMakeVisible(container) called
 * 2. Verify setBounds() or resized() called to set size
 * 3. Verify container->setVisible(true)
 * 4. Verify container->addLane() called
 */

/*
 * ISSUE: Lanes positioned incorrectly
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Causes:
 * 1. Y position calculated incorrectly
 * 2. Height not accounting for all visible lanes
 * 3. resized() not being called when needed
 * 4. getTotalVisibleHeight() returning wrong value
 * 
 * Solution:
 * 1. Add debug visualization: draw rectangles at container bounds
 * 2. Log Y position and height values
 * 3. Call resized() explicitly after changes
 * 4. Verify lane visibility affects height calculation
 */

/*
 * ISSUE: Curves not updating on data change
 * ──────────────────────────────────────────────────────────────────────────
 * 
 * Causes:
 * 1. changeListenerCallback() not called
 * 2. invalidateAllCurvePaths() not called
 * 3. Container not subscribed to changes
 * 4. repaint() not called
 * 
 * Solution:
 * 1. Verify addChangeListener(this) called in constructor
 * 2. Verify changeListenerCallback() implemented
 * 3. Add breakpoint in changeListenerCallback() to verify it's called
 * 4. Call repaint() after invalidating paths
 */

// ============================================================================
// NEXT: STEP 3 PREVIEW
// ============================================================================

/*
 * STEP 3: WIRE UP DATA FROM AUTOMATION MANAGER
 * ═════════════════════════════════════════════════════════════════════════
 * 
 * Will involve:
 * 1. Getting AutomationManagerCore reference
 * 2. Creating AutomationUIHelper instance
 * 3. Observing automation changes
 * 4. Passing helper to containers
 * 
 * Already done in Step 2, so Step 3 will be minimal.
 */

#endif // APEX_AUTOMATION_PHASE_1_STEP_1_COMPLETE_H

/**
 * APEX AUTOMATION V2 — UI IMPLEMENTATION REFERENCE GUIDE
 * 
 * This document provides detailed examples and patterns for integrating
 * Automation V2 features into the ArrangementEditor UI.
 * 
 * Use these patterns as templates for your UI component implementations.
 */

#pragma once

#include <JuceHeader.h>
#include "AutomationUIHelper.h"

namespace DAW {

// ============================================================================
// PATTERN 1: RIGHT-CLICK POINT MENU
// ============================================================================

/*
 * Example Usage in ArrangementEditor Automation Lane Component:
 * 
 * void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 * {
 *     if (e.mods.isPopupMenu())
 *     {
 *         auto pointIndex = getPointIndexAtMouse(e);
 *         if (pointIndex >= 0)
 *         {
 *             showPointContextMenu(pointIndex, e.getScreenPosition());
 *         }
 *     }
 * }
 * 
 * void AutomationLaneComponent::showPointContextMenu(int pointIndex, juce::Point<int> screenPos)
 * {
 *     AutomationUIHelper helper(automationManager);
 *     
 *     juce::PopupMenu menu;
 *     
 *     menu.addItem("Delete Point", [this, pointIndex]()
 *     {
 *         helper.deletePoint(trackId, parameterId, pointIndex);
 *     });
 *     
 *     menu.addItem("Reset Value", [this, pointIndex]()
 *     {
 *         helper.resetPointValue(trackId, parameterId, pointIndex);
 *     });
 *     
 *     menu.addItem("Copy Value", [this, pointIndex]()
 *     {
 *         helper.copyPointValue(trackId, parameterId, pointIndex);
 *     });
 *     
 *     if (helper.canPastePointValue())
 *     {
 *         menu.addItem("Paste Value", [this, pointIndex]()
 *         {
 *             helper.pastePointValue(trackId, parameterId, pointIndex);
 *         });
 *     }
 *     
 *     menu.addSeparator();
 *     
 *     // Curve type submenu
 *     juce::PopupMenu curveMenu;
 *     auto allCurves = AutomationCurveTypeHelper::getAllCurveTypes();
 *     for (auto curve : allCurves)
 *     {
 *         auto displayName = AutomationCurveTypeHelper::getDisplayName(curve);
 *         curveMenu.addItem(displayName, [this, pointIndex, curve]()
 *         {
 *             helper.setSegmentCurveType(trackId, parameterId, pointIndex, curve);
 *         });
 *     }
 *     menu.addSubMenu("Curve to Next", curveMenu);
 *     
 *     menu.addItem("Set Exact Value...", [this, pointIndex]()
 *     {
 *         // Show dialog to set exact value
 *         showSetPointValueDialog(pointIndex);
 *     });
 *     
 *     menu.showMenuAsync(juce::PopupMenu::Options()
 *         .withTargetScreenPosition(screenPos));
 * }
 */

// ============================================================================
// PATTERN 2: RIGHT-CLICK SEGMENT MENU
// ============================================================================

/*
 * Example Usage:
 * 
 * void AutomationLaneComponent::showSegmentContextMenu(int segmentIndex, juce::Point<int> screenPos)
 * {
 *     AutomationUIHelper helper(automationManager);
 *     
 *     juce::PopupMenu menu;
 *     
 *     menu.addItem("Add Point Here", [this, segmentIndex]()
 *     {
 *         auto mousePos = getLastMousePosition();
 *         auto timeSamples = pixelToSample(mousePos.x);
 *         helper.insertPointPreservingLevel(trackId, parameterId, timeSamples);
 *     });
 *     
 *     menu.addItem("Add Point (Preserving Level)", [this, segmentIndex]()
 *     {
 *         // Same as above — insertPointPreservingLevel already preserves level
 *     });
 *     
 *     menu.addSeparator();
 *     
 *     menu.addItem("Reset Tension", [this, segmentIndex]()
 *     {
 *         helper.resetSegmentTension(trackId, parameterId, segmentIndex);
 *     });
 *     
 *     menu.addSeparator();
 *     
 *     juce::PopupMenu curveMenu;
 *     auto allCurves = AutomationCurveTypeHelper::getAllCurveTypes();
 *     for (auto curve : allCurves)
 *     {
 *         auto displayName = AutomationCurveTypeHelper::getDisplayName(curve);
 *         curveMenu.addItem(displayName, [this, segmentIndex, curve]()
 *         {
 *             helper.setSegmentCurveType(trackId, parameterId, segmentIndex, curve);
 *         });
 *     }
 *     menu.addSubMenu("Curve Type", curveMenu);
 *     
 *     menu.addItem("Copy Segment Shape", [this, segmentIndex]()
 *     {
 *         helper.copySegmentShape(trackId, parameterId, segmentIndex);
 *     });
 *     
 *     if (helper.canPasteSegmentShape())
 *     {
 *         menu.addItem("Paste Segment Shape", [this, segmentIndex]()
 *         {
 *             helper.pasteSegmentShape(trackId, parameterId, segmentIndex, false);
 *         });
 *     }
 *     
 *     menu.showMenuAsync(juce::PopupMenu::Options()
 *         .withTargetScreenPosition(screenPos));
 * }
 */

// ============================================================================
// PATTERN 3: TENSION HANDLE DRAG
// ============================================================================

/*
 * Example Usage for Tension Handle Component:
 * 
 * class TensionHandleComponent : public juce::Component
 * {
 * public:
 *     void mouseDown(const juce::MouseEvent& e) override
 *     {
 *         if (e.mods.isPopupMenu())
 *         {
 *             AutomationUIHelper helper(automationManager);
 *             helper.resetSegmentTension(trackId, parameterId, segmentIndex);
 *             repaint();
 *             return;
 *         }
 *         
 *         isDragging = true;
 *         lastMouseY = e.getPosition().y;
 *     }
 *     
 *     void mouseDrag(const juce::MouseEvent& e) override
 *     {
 *         if (!isDragging) return;
 *         
 *         AutomationUIHelper helper(automationManager);
 *         
 *         // Calculate tension change from vertical drag
 *         float deltaY = (float)(lastMouseY - e.getPosition().y);
 *         float dragAmount = deltaY / getHeight(); // normalize to component height
 *         
 *         // Fine adjustment with Ctrl
 *         if (e.mods.isCtrlDown())
 *             dragAmount *= 0.25f;
 *         
 *         // Update tension
 *         float newTension = currentTension + dragAmount;
 *         newTension = std::max(-0.99f, std::min(0.99f, newTension));
 *         
 *         helper.setSegmentTension(trackId, parameterId, segmentIndex, newTension);
 *         currentTension = newTension;
 *         lastMouseY = e.getPosition().y;
 *         
 *         repaint();
 *     }
 *     
 *     void mouseUp(const juce::MouseEvent& e) override
 *     {
 *         isDragging = false;
 *     }
 * 
 * private:
 *     bool isDragging = false;
 *     float currentTension = 0.0f;
 *     int lastMouseY = 0;
 * };
 */

// ============================================================================
// PATTERN 4: SHIFT+RIGHTCLICK POINT INSERTION
// ============================================================================

/*
 * Example Usage in Automation Lane:
 * 
 * void AutomationLaneComponent::mouseDown(const juce::MouseEvent& e)
 * {
 *     // Shift+RightClick = insert point preserving level
 *     if (e.mods.isShiftDown() && e.mods.isPopupMenu())
 *     {
 *         AutomationUIHelper helper(automationManager);
 *         auto timeSamples = pixelToSample(e.getPosition().x);
 *         int pointIndex = helper.insertPointPreservingLevel(trackId, parameterId, timeSamples);
 *         
 *         if (pointIndex >= 0)
 *         {
 *             selectPoint(pointIndex);
 *             repaint();
 *         }
 *         return;
 *     }
 *     
 *     // Normal right-click = show menu
 *     if (e.mods.isPopupMenu())
 *     {
 *         auto pointIndex = getPointIndexAtMouse(e);
 *         if (pointIndex >= 0)
 *             showPointContextMenu(pointIndex, e.getScreenPosition());
 *         return;
 *     }
 * }
 */

// ============================================================================
// PATTERN 5: QUICK AUTOMATION CREATION
// ============================================================================

/*
 * Example Usage for Right-Click on Track Control:
 * 
 * void TrackVolumeKnob::mouseDown(const juce::MouseEvent& e)
 * {
 *     if (e.mods.isPopupMenu())
 *     {
 *         showQuickAutomationMenu();
 *         return;
 *     }
 *     
 *     // Normal drag to change volume
 *     isDragging = true;
 * }
 * 
 * void TrackVolumeKnob::showQuickAutomationMenu()
 * {
 *     AutomationUIHelper helper(automationManager);
 *     
 *     juce::PopupMenu menu;
 *     
 *     menu.addItem("Create Automation", [this]()
 *     {
 *         helper.quickCreateTrackVolume(trackId);
 *     });
 *     
 *     menu.addItem("Show Automation Lane", [this]()
 *     {
 *         helper.setLaneVisible(trackId, 
 *             AutomationLaneCore::trackVolumeParameterId, true);
 *     });
 *     
 *     menu.showMenuAsync(juce::PopupMenu::Options());
 * }
 * 
 * Example for Plugin Parameter:
 * 
 * void PluginParameterSlider::showQuickAutomationMenu()
 * {
 *     AutomationUIHelper helper(automationManager);
 *     
 *     juce::PopupMenu menu;
 *     
 *     menu.addItem("Create Automation", [this]()
 *     {
 *         helper.quickCreatePluginParameter(trackId, pluginSlotIndex, 
 *             pluginInstanceId, paramId);
 *     });
 *     
 *     menu.addItem("Show Automation Lane", [this]()
 *     {
 *         auto parameterId = "plugin." + juce::String(pluginSlotIndex) 
 *             + "." + pluginInstanceId + "." + paramId;
 *         helper.setLaneVisible(trackId, parameterId, true);
 *     });
 *     
 *     menu.showMenuAsync(juce::PopupMenu::Options());
 * }
 */

// ============================================================================
// PATTERN 6: AUTOMATION CLIP REGION BLOCK
// ============================================================================

/*
 * Example Component for Rendering Automation Clip Block:
 * 
 * class AutomationClipBlockComponent : public juce::Component
 * {
 * public:
 *     AutomationClipBlockComponent(AutomationManagerCore& mgr, const juce::Uuid& clipId)
 *         : manager_(mgr), clipId_(clipId)
 *     {
 *     }
 *     
 *     void paint(juce::Graphics& g) override
 *     {
 *         auto clipRegion = manager_.findClipRegion(clipId_);
 *         if (!clipRegion)
 *             return;
 *         
 *         // Draw background
 *         g.fillAll(clipRegion->color.withAlpha(0.3f));
 *         
 *         // Draw header with parameter name
 *         auto headerRect = getLocalBounds().removeFromTop(20);
 *         g.setColour(clipRegion->color.withAlpha(0.8f));
 *         g.fillRect(headerRect);
 *         g.setColour(juce::Colours::white);
 *         g.drawFittedText(clipRegion->name, headerRect, juce::Justification::left, 1);
 *         
 *         // Draw curve preview inside block
 *         if (clipRegion->localPoints.size() >= 2)
 *             drawCurvePreview(g, getLocalBounds().reduced(2));
 *         
 *         // Draw border
 *         g.setColour(clipRegion->color);
 *         g.drawRect(getLocalBounds(), 1.0f);
 *         
 *         // Draw muted indicator if muted
 *         if (clipRegion->muted)
 *         {
 *             g.setColour(juce::Colours::white.withAlpha(0.5f));
 *             g.drawLine(0.0f, 0.0f, (float)getWidth(), (float)getHeight(), 2.0f);
 *         }
 *     }
 *     
 *     void mouseDown(const juce::MouseEvent& e) override
 *     {
 *         if (e.mods.isPopupMenu())
 *         {
 *             showClipContextMenu(e.getScreenPosition());
 *             return;
 *         }
 *         
 *         isDragging = true;
 *         dragStartX = e.getPosition().x;
 *     }
 *     
 *     void mouseDrag(const juce::MouseEvent& e) override
 *     {
 *         if (!isDragging) return;
 *         
 *         AutomationUIHelper helper(manager_);
 *         int deltaPixels = e.getPosition().x - dragStartX;
 *         int64_t deltaSamples = pixelToSample(deltaPixels);
 *         
 *         auto clipRegion = manager_.findClipRegion(clipId_);
 *         if (clipRegion)
 *         {
 *             helper.moveClipRegion(clipId_, clipRegion->startSample + deltaSamples);
 *             dragStartX = e.getPosition().x;
 *         }
 *     }
 *     
 *     void mouseUp(const juce::MouseEvent& e) override
 *     {
 *         isDragging = false;
 *     }
 *     
 *     void showClipContextMenu(juce::Point<int> screenPos)
 *     {
 *         AutomationUIHelper helper(manager_);
 *         
 *         juce::PopupMenu menu;
 *         
 *         menu.addItem("Duplicate", [this]()
 *         {
 *             auto clipRegion = manager_.findClipRegion(clipId_);
 *             if (clipRegion)
 *             {
 *                 helper.duplicateClipRegion(clipId_, 
 *                     clipRegion->startSample + clipRegion->lengthSamples);
 *             }
 *         });
 *         
 *         menu.addItem("Copy State", [this]()
 *         {
 *             helper.copyClipState(clipId_);
 *         });
 *         
 *         menu.addItem("Mute", [this]()
 *         {
 *             auto clipRegion = manager_.findClipRegion(clipId_);
 *             if (clipRegion)
 *                 helper.setClipRegionMuted(clipId_, !clipRegion->muted);
 *         });
 *         
 *         juce::PopupMenu articulatorMenu;
 *         articulatorMenu.addItem("Flip Vertically", [this]()
 *         {
 *             helper.flipClipVertically(clipId_);
 *         });
 *         articulatorMenu.addItem("Normalize Levels", [this]()
 *         {
 *             helper.normalizeClipLevels(clipId_);
 *         });
 *         menu.addSubMenu("Articulator Tools", articulatorMenu);
 *         
 *         menu.showMenuAsync(juce::PopupMenu::Options()
 *             .withTargetScreenPosition(screenPos));
 *     }
 * 
 * private:
 *     void drawCurvePreview(juce::Graphics& g, juce::Rectangle<int> area)
 *     {
 *         // Draw points as small dots
 *         auto clipRegion = manager_.findClipRegion(clipId_);
 *         if (!clipRegion)
 *             return;
 *         
 *         for (const auto& point : clipRegion->localPoints)
 *         {
 *             float x = (float)point.timeSamples / (float)clipRegion->lengthSamples;
 *             float y = 1.0f - point.value; // invert Y for screen coords
 *             
 *             int screenX = area.getX() + (int)(x * area.getWidth());
 *             int screenY = area.getY() + (int)(y * area.getHeight());
 *             
 *             g.drawEllipse(screenX - 2, screenY - 2, 4, 4, 1.0f);
 *         }
 *     }
 *     
 *     int64_t pixelToSample(int pixels)
 *     {
 *         // Convert pixel delta to sample delta based on zoom level
 *         return (int64_t)(pixels * samplesPerPixel_);
 *     }
 *     
 *     AutomationManagerCore& manager_;
 *     juce::Uuid clipId_;
 *     bool isDragging = false;
 *     int dragStartX = 0;
 *     double samplesPerPixel_ = 100.0;
 * };
 */

// ============================================================================
// PATTERN 7: ARTICULATOR TOOLS DIALOG
// ============================================================================

/*
 * Example Dialog Component:
 * 
 * class ArticulatorToolsDialog : public juce::DialogWindow
 * {
 * public:
 *     ArticulatorToolsDialog(AutomationManagerCore& mgr, const juce::Uuid& clipId)
 *         : juce::DialogWindow("Articulator Tools", juce::Colours::lightgrey, true),
 *           manager_(mgr), clipId_(clipId)
 *     {
 *         setResizable(false, false);
 *         setContentOwned(new ArticulatorToolsContent(mgr, clipId), true);
 *         setSize(300, 200);
 *     }
 * };
 * 
 * class ArticulatorToolsContent : public juce::Component
 * {
 * public:
 *     ArticulatorToolsContent(AutomationManagerCore& mgr, const juce::Uuid& clipId)
 *         : manager_(mgr), clipId_(clipId)
 *     {
 *         addAndMakeVisible(flipButton);
 *         flipButton.setButtonText("Flip Vertically");
 *         flipButton.onClick = [this]() { onFlip(); };
 *         
 *         addAndMakeVisible(normalizeButton);
 *         normalizeButton.setButtonText("Normalize Levels");
 *         normalizeButton.onClick = [this]() { onNormalize(); };
 *         
 *         addAndMakeVisible(resetButton);
 *         resetButton.setButtonText("Reset to Default");
 *         resetButton.onClick = [this]() { onReset(); };
 *         
 *         setSize(300, 100);
 *     }
 *     
 *     void resized() override
 *     {
 *         auto area = getLocalBounds().reduced(10);
 *         flipButton.setBounds(area.removeFromTop(30));
 *         area.removeFromTop(5);
 *         normalizeButton.setBounds(area.removeFromTop(30));
 *         area.removeFromTop(5);
 *         resetButton.setBounds(area.removeFromTop(30));
 *     }
 * 
 * private:
 *     void onFlip()
 *     {
 *         AutomationUIHelper helper(manager_);
 *         helper.flipClipVertically(clipId_);
 *     }
 *     
 *     void onNormalize()
 *     {
 *         AutomationUIHelper helper(manager_);
 *         helper.normalizeClipLevels(clipId_);
 *     }
 *     
 *     void onReset()
 *     {
 *         AutomationUIHelper helper(manager_);
 *         helper.resetClipLevels(clipId_, 0.5f);
 *     }
 *     
 *     AutomationManagerCore& manager_;
 *     juce::Uuid clipId_;
 *     juce::TextButton flipButton, normalizeButton, resetButton;
 * };
 */

} // namespace DAW

#endif // APEX_AUTOMATION_UI_REFERENCE_H

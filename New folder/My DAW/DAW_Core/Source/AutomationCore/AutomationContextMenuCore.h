#pragma once
#include <JuceHeader.h>
#include "AutomationCurveTypesCore.h"
#include <functional>

namespace DAW {

struct AutomationContextMenuCore
{
    struct PointCallbacks
    {
        std::function<void()> deletePoint;
        std::function<void()> resetPointValue;
        std::function<void()> copyPointValue;
        std::function<void()> pastePointValue;
        std::function<void(AutomationCurveType)> setCurveToNext;
        std::function<void()> resetTensionToNext;
        std::function<void()> setExactValue;
        std::function<void()> setExactTime;
        bool hasNextPoint = false;
        bool canPastePointValue = false;
    };

    struct SegmentCallbacks
    {
        std::function<void(bool)> addPointHere;
        std::function<void()> resetSegmentTension;
        std::function<void(AutomationCurveType)> setCurveType;
        std::function<void()> copySegmentShape;
        std::function<void()> pasteSegmentShape;
        std::function<void()> deletePointsInSegment;
        bool canPasteSegmentShape = false;
    };

    struct ClipCallbacks
    {
        std::function<void()> preview;
        std::function<void(bool)> setMuted;
        std::function<void()> renameAndColor;
        std::function<void()> randomColor;
        std::function<void()> changeColor;
        std::function<void()> changeIcon;
        std::function<void()> selectSourceOrTarget;
        std::function<void()> channelSettings;
        std::function<void()> makeUnique;
        std::function<void()> selectAllSimilar;
        std::function<void()> deleteClip;

        std::function<void()> targetParameter;
        std::function<void()> showLane;
        std::function<void()> hideLane;
        std::function<void()> clearAutomation;

        std::function<void()> copyState;
        std::function<void()> pasteState;
        std::function<void()> flipVertically;
        std::function<void()> scaleLevels;
        std::function<void()> normalizeLevels;
        std::function<void()> resetLevels;

        std::function<void()> selectRegion;
        std::function<void()> duplicate;
        std::function<void()> copy;
        std::function<void()> paste;
        std::function<void()> resizeToSelection;
        std::function<void()> fitToClip;

        bool isMuted = false;
        bool canPasteState = false;
        bool canPasteRegion = false;
    };

    static juce::PopupMenu buildCurveMenu(int baseId)
    {
        juce::PopupMenu curveMenu;
        const auto curves = getAllAutomationCurveTypes();
        for (int i = 0; i < curves.size(); ++i)
            curveMenu.addItem(baseId + i, automationCurveTypeToDisplayName(curves.getReference(i)));
        return curveMenu;
    }

    static void showPointMenu(juce::Point<int> screenPos, PointCallbacks callbacks)
    {
        juce::PopupMenu menu;
        menu.addItem(kDeletePoint, "Delete Point", callbacks.deletePoint != nullptr);
        menu.addItem(kResetPointValue, "Reset Point Value", callbacks.resetPointValue != nullptr);
        menu.addItem(kCopyPointValue, "Copy Point Value", callbacks.copyPointValue != nullptr);
        menu.addItem(kPastePointValue, "Paste Point Value", callbacks.pastePointValue != nullptr && callbacks.canPastePointValue);
        menu.addSeparator();
        menu.addSubMenu("Curve to Next", buildCurveMenu(kCurveBase), callbacks.hasNextPoint && callbacks.setCurveToNext != nullptr);
        menu.addItem(kResetTensionToNext, "Reset Tension to Next", callbacks.hasNextPoint && callbacks.resetTensionToNext != nullptr);
        menu.addSeparator();
        menu.addItem(kSetExactValue, "Set Exact Value...", callbacks.setExactValue != nullptr);
        menu.addItem(kSetExactTime, "Set Exact Time...", callbacks.setExactTime != nullptr);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
            [callbacks](int result)
            {
                if (result == kDeletePoint && callbacks.deletePoint) callbacks.deletePoint();
                else if (result == kResetPointValue && callbacks.resetPointValue) callbacks.resetPointValue();
                else if (result == kCopyPointValue && callbacks.copyPointValue) callbacks.copyPointValue();
                else if (result == kPastePointValue && callbacks.pastePointValue) callbacks.pastePointValue();
                else if (result == kResetTensionToNext && callbacks.resetTensionToNext) callbacks.resetTensionToNext();
                else if (result == kSetExactValue && callbacks.setExactValue) callbacks.setExactValue();
                else if (result == kSetExactTime && callbacks.setExactTime) callbacks.setExactTime();
                else if (result >= kCurveBase && result < kCurveBase + getAllAutomationCurveTypes().size() && callbacks.setCurveToNext)
                    callbacks.setCurveToNext(getAllAutomationCurveTypes()[result - kCurveBase]);
            });
    }

    static void showSegmentMenu(juce::Point<int> screenPos, SegmentCallbacks callbacks)
    {
        juce::PopupMenu menu;
        menu.addItem(kAddPointHere, "Add Point Here", callbacks.addPointHere != nullptr);
        menu.addItem(kAddPointHereKeepingLevel, "Add Point Here Keeping Level", callbacks.addPointHere != nullptr);
        menu.addSeparator();
        menu.addItem(kResetSegmentTension, "Reset Segment Tension", callbacks.resetSegmentTension != nullptr);
        menu.addSubMenu("Curve Type", buildCurveMenu(kCurveBase), callbacks.setCurveType != nullptr);
        menu.addSeparator();
        menu.addItem(kCopySegmentShape, "Copy Segment Shape", callbacks.copySegmentShape != nullptr);
        menu.addItem(kPasteSegmentShape, "Paste Segment Shape", callbacks.pasteSegmentShape != nullptr && callbacks.canPasteSegmentShape);
        menu.addItem(kDeletePointsInSegment, "Delete Points In Segment", callbacks.deletePointsInSegment != nullptr);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
            [callbacks](int result)
            {
                if (result == kAddPointHere && callbacks.addPointHere) callbacks.addPointHere(false);
                else if (result == kAddPointHereKeepingLevel && callbacks.addPointHere) callbacks.addPointHere(true);
                else if (result == kResetSegmentTension && callbacks.resetSegmentTension) callbacks.resetSegmentTension();
                else if (result == kCopySegmentShape && callbacks.copySegmentShape) callbacks.copySegmentShape();
                else if (result == kPasteSegmentShape && callbacks.pasteSegmentShape) callbacks.pasteSegmentShape();
                else if (result == kDeletePointsInSegment && callbacks.deletePointsInSegment) callbacks.deletePointsInSegment();
                else if (result >= kCurveBase && result < kCurveBase + getAllAutomationCurveTypes().size() && callbacks.setCurveType)
                    callbacks.setCurveType(getAllAutomationCurveTypes()[result - kCurveBase]);
            });
    }

    static void showClipMenu(juce::Point<int> screenPos, ClipCallbacks callbacks)
    {
        juce::PopupMenu clipMenu;
        clipMenu.addItem(kClipPreview, "Preview", callbacks.preview != nullptr);
        clipMenu.addItem(kClipMuted, callbacks.isMuted ? "Unmute" : "Mute", callbacks.setMuted != nullptr);
        clipMenu.addItem(kClipRenameColor, "Rename and Color...", callbacks.renameAndColor != nullptr);
        clipMenu.addItem(kClipRandomColor, "Random Color", callbacks.randomColor != nullptr);
        clipMenu.addItem(kClipChangeColor, "Change Color...", callbacks.changeColor != nullptr);
        clipMenu.addItem(kClipChangeIcon, "Change Icon...", callbacks.changeIcon != nullptr);
        clipMenu.addSeparator();
        clipMenu.addItem(kClipSelectSourceTarget, "Select Source Channel / Target", callbacks.selectSourceOrTarget != nullptr);
        clipMenu.addItem(kClipChannelSettings, "Channel Settings / Automation Settings", callbacks.channelSettings != nullptr);
        clipMenu.addItem(kClipMakeUnique, "Make Unique", callbacks.makeUnique != nullptr);
        clipMenu.addItem(kClipSelectAllSimilar, "Select All Similar Clips", callbacks.selectAllSimilar != nullptr);
        clipMenu.addSeparator();
        clipMenu.addItem(kClipDelete, "Delete", callbacks.deleteClip != nullptr);

        juce::PopupMenu automationMenu;
        automationMenu.addItem(kAutomationTargetParameter, "Target Parameter", callbacks.targetParameter != nullptr);
        automationMenu.addItem(kAutomationShowLane, "Show Lane", callbacks.showLane != nullptr);
        automationMenu.addItem(kAutomationHideLane, "Hide Lane", callbacks.hideLane != nullptr);
        automationMenu.addItem(kAutomationClear, "Clear Automation", callbacks.clearAutomation != nullptr);

        juce::PopupMenu articulatorMenu;
        articulatorMenu.addItem(kArtCopyState, "Copy State", callbacks.copyState != nullptr);
        articulatorMenu.addItem(kArtPasteState, "Paste State", callbacks.pasteState != nullptr && callbacks.canPasteState);
        articulatorMenu.addItem(kArtFlipVertically, "Flip Vertically", callbacks.flipVertically != nullptr);
        articulatorMenu.addItem(kArtScaleLevels, "Scale Levels...", callbacks.scaleLevels != nullptr);
        articulatorMenu.addItem(kArtNormalizeLevels, "Normalize Levels", callbacks.normalizeLevels != nullptr);
        articulatorMenu.addItem(kArtResetLevels, "Reset Levels", callbacks.resetLevels != nullptr);

        juce::PopupMenu regionMenu;
        regionMenu.addItem(kRegionSelect, "Select Region", callbacks.selectRegion != nullptr);
        regionMenu.addItem(kRegionDuplicate, "Duplicate", callbacks.duplicate != nullptr);
        regionMenu.addItem(kRegionCopy, "Copy", callbacks.copy != nullptr);
        regionMenu.addItem(kRegionPaste, "Paste", callbacks.paste != nullptr && callbacks.canPasteRegion);
        regionMenu.addItem(kRegionResizeToSelection, "Resize to Selection", callbacks.resizeToSelection != nullptr);
        regionMenu.addItem(kRegionFitToClip, "Fit to Clip", callbacks.fitToClip != nullptr);

        juce::PopupMenu menu;
        menu.addSubMenu("Automation Clip", clipMenu);
        menu.addSubMenu("Automation", automationMenu);
        menu.addSubMenu("Articulator Tools", articulatorMenu);
        menu.addSubMenu("Region", regionMenu);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
            [callbacks](int result)
            {
                if (result == kClipPreview && callbacks.preview) callbacks.preview();
                else if (result == kClipMuted && callbacks.setMuted) callbacks.setMuted(!callbacks.isMuted);
                else if (result == kClipRenameColor && callbacks.renameAndColor) callbacks.renameAndColor();
                else if (result == kClipRandomColor && callbacks.randomColor) callbacks.randomColor();
                else if (result == kClipChangeColor && callbacks.changeColor) callbacks.changeColor();
                else if (result == kClipChangeIcon && callbacks.changeIcon) callbacks.changeIcon();
                else if (result == kClipSelectSourceTarget && callbacks.selectSourceOrTarget) callbacks.selectSourceOrTarget();
                else if (result == kClipChannelSettings && callbacks.channelSettings) callbacks.channelSettings();
                else if (result == kClipMakeUnique && callbacks.makeUnique) callbacks.makeUnique();
                else if (result == kClipSelectAllSimilar && callbacks.selectAllSimilar) callbacks.selectAllSimilar();
                else if (result == kClipDelete && callbacks.deleteClip) callbacks.deleteClip();
                else if (result == kAutomationTargetParameter && callbacks.targetParameter) callbacks.targetParameter();
                else if (result == kAutomationShowLane && callbacks.showLane) callbacks.showLane();
                else if (result == kAutomationHideLane && callbacks.hideLane) callbacks.hideLane();
                else if (result == kAutomationClear && callbacks.clearAutomation) callbacks.clearAutomation();
                else if (result == kArtCopyState && callbacks.copyState) callbacks.copyState();
                else if (result == kArtPasteState && callbacks.pasteState) callbacks.pasteState();
                else if (result == kArtFlipVertically && callbacks.flipVertically) callbacks.flipVertically();
                else if (result == kArtScaleLevels && callbacks.scaleLevels) callbacks.scaleLevels();
                else if (result == kArtNormalizeLevels && callbacks.normalizeLevels) callbacks.normalizeLevels();
                else if (result == kArtResetLevels && callbacks.resetLevels) callbacks.resetLevels();
                else if (result == kRegionSelect && callbacks.selectRegion) callbacks.selectRegion();
                else if (result == kRegionDuplicate && callbacks.duplicate) callbacks.duplicate();
                else if (result == kRegionCopy && callbacks.copy) callbacks.copy();
                else if (result == kRegionPaste && callbacks.paste) callbacks.paste();
                else if (result == kRegionResizeToSelection && callbacks.resizeToSelection) callbacks.resizeToSelection();
                else if (result == kRegionFitToClip && callbacks.fitToClip) callbacks.fitToClip();
            });
    }

private:
    enum IDs
    {
        kDeletePoint = 11000,
        kResetPointValue,
        kCopyPointValue,
        kPastePointValue,
        kResetTensionToNext,
        kSetExactValue,
        kSetExactTime,
        kAddPointHere,
        kAddPointHereKeepingLevel,
        kResetSegmentTension,
        kCopySegmentShape,
        kPasteSegmentShape,
        kDeletePointsInSegment,
        kCurveBase = 11200,
        kClipPreview = 11400,
        kClipMuted,
        kClipRenameColor,
        kClipRandomColor,
        kClipChangeColor,
        kClipChangeIcon,
        kClipSelectSourceTarget,
        kClipChannelSettings,
        kClipMakeUnique,
        kClipSelectAllSimilar,
        kClipDelete,
        kAutomationTargetParameter,
        kAutomationShowLane,
        kAutomationHideLane,
        kAutomationClear,
        kArtCopyState,
        kArtPasteState,
        kArtFlipVertically,
        kArtScaleLevels,
        kArtNormalizeLevels,
        kArtResetLevels,
        kRegionSelect,
        kRegionDuplicate,
        kRegionCopy,
        kRegionPaste,
        kRegionResizeToSelection,
        kRegionFitToClip
    };
};

} // namespace DAW

#pragma once

#include <JuceHeader.h>
#include "../../../Source/AutomationCore/AutomationUIHelper.h"
#include "../../../Source/AutomationCore/AutomationCurveEvalCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationTransportStateCore.h"
#include "AutomationUndoRedoCore.h"
#include "../../../Source/AutomationCore/AutomationLaneCurveClipboardCore.h"
#include "../../../Source/AutomationCore/AutomationMusicalLfoCore.h"
#include "../../../Source/AutomationCore/AutomationFreehandStrokeCore.h"
#include "../../../Source/AutomationCore/AutomationCurveTransformCore.h"
#include "../../../Source/AutomationCore/AutomationCurveMorphCore.h"

namespace DAW {

/**
 * PHASE 1: BASIC RENDERING
 * 
 * AutomationLaneComponent — Displays automation curve with curve types and tension values.
 * 
 * Features (Phase 1):
 * ✓ Render automation curve from lane data
 * ✓ Display all 14 curve types correctly
 * ✓ Show tension value visually
 * ✓ Real-time updates as data changes
 * ✓ Zoom-aware rendering
 * ✓ Performance optimized (cached curve path)
 */
class AutomationLaneComponent : public juce::Component, public juce::ChangeListener
{
public:
    AutomationLaneComponent(AutomationUIHelper& helper,
                           const TrackID& trackId,
                           const juce::String& parameterId)
        : helper_(helper), trackId_(trackId), parameterId_(parameterId),
          curvePath_(nullptr), curvePathValid_(false),
          samplesPerPixel_(100.0), zoomLevel_(1.0),
          sampleRate_(44100.0), tempoBpm_(120.0)
    {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(true, true);
        // Listen to automation lane changes
        // (Will be wired up when we have automation manager observer)
    }

    ~AutomationLaneComponent() override = default;

    using GroupedCurveCallback = std::function<bool(
        const TrackID&, const juce::String&, const std::vector<AutomationPoint>&)>;
    void setGroupedCurveCallback(GroupedCurveCallback callback)
    {
        groupedCurveCallback_ = std::move(callback);
    }

    // Session-local multi-track A/B memories. These are deliberately limited
    // to native volume/pan until the VST3 host can verify exact plugin IDs
    // and publish PPQ updates for multiple plugin parameters coherently.
    enum class SelectedSnapshotAction { CaptureA, CaptureB, MorphHalf };
    using SelectedSnapshotCallback = std::function<bool(
        const TrackID&, const juce::String&, SelectedSnapshotAction)>;
    void setSelectedSnapshotCallback(SelectedSnapshotCallback callback)
    {
        selectedSnapshotCallback_ = std::move(callback);
    }

    // Same exact PPQ bridge as ordinary point, pencil and curve edits.
    // Callable after multi-track Undo/Redo without constructing UI lanes.
    static void syncModelLaneToApex(const AutomationUIHelper& helper,
                                    const TrackID& trackId,
                                    const juce::String& parameterId,
                                    double sampleRate, double tempoBpm)
    {
        if (!std::isfinite(sampleRate) || !std::isfinite(tempoBpm)
            || sampleRate <= 0.0 || tempoBpm <= 0.0)
            return;

        juce::String key;
        using KR = apex::automation::AutomationParameterKeyRegistry;
        if (parameterId == AutomationLaneCore::trackVolumeParameterId)
            key = KR::trackVolumeKey(trackId);
        else if (parameterId == AutomationLaneCore::trackPanParameterId)
            key = KR::trackPanKey(trackId);
        else
            return; // explicit native volume/pan only: no VST3 guessing

        const auto* lane = helper.findLane(trackId, parameterId);
        if (lane == nullptr)
            return;

        auto& registry = KR::getInstance();
        const auto id = registry.getOrCreateID(key);
        auto& apexLane = apex::automation::AutomationLaneStore::getInstance()
                             .getOrCreateLane(id);
        apex::automation::AutomationLane::PointVector ppqPoints;
        ppqPoints.reserve(lane->points.size());
        const double ppqPerSample = tempoBpm / (60.0 * sampleRate);
        for (const auto& point : lane->points)
        {
            ppqPoints.push_back({
                juce::jmax(0.0, static_cast<double>(point.timeSamples) * ppqPerSample),
                normaliseForApex(parameterId, point.value),
                toApexCurveType(point.curveToNext),
                juce::jlimit(-1.0f, 1.0f, point.tensionToNext)
            });
        }
        apexLane.replacePoints(std::move(ppqPoints));
    }

    bool canBulkEditCurve() const noexcept
    {
        return !helper_.hasClipRegionsForParameter(trackId_, parameterId_);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::transparentBlack);

        const auto b = getLocalBounds().toFloat();
        juce::ColourGradient fill(juce::Colour(0x5A173142), b.getX(), b.getY(),
                                  juce::Colour(0x3A0D1822), b.getX(), b.getBottom(), false);
        g.setGradientFill(fill);
        g.fillRect(b);

        // Draw automation curve
        if (shouldRenderCurve())
        {
            if (!curvePathValid_)
                regenerateCurvePath();

            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.strokePath(*curvePath_, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colours::cyan.brighter(0.25f));
            g.strokePath(*curvePath_, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Draw points
        drawPoints(g);

        // Transient pencil preview; live DSP is untouched until mouseUp.
        if (freehandDrawing_ && !freehandStroke_.empty())
        {
            juce::Path preview;
            bool first = true;
            for (const auto& pt : freehandStroke_)
            {
                const float x = timeSamplesToX(pt.timeSamples, b);
                const float y = valueToY(pt.value, b);
                if (first) { preview.startNewSubPath(x, y); first = false; }
                else preview.lineTo(x, y);
            }
            g.setColour(juce::Colours::yellow.withAlpha(0.85f));
            g.strokePath(preview, juce::PathStrokeType(2.0f));
        }

        // Draw tension indicators
        drawTensionIndicators(g);

    }

    void resized() override
    {
        invalidateCurvePath();
    }

    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        // Called when automation data changes
        invalidateCurvePath();
        repaint();
    }

    // ========================================================================
    // MOUSE EVENT HANDLING (Phase 2)
    // ========================================================================

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        auto& laneRef = helper_.getOrCreateLane(trackId_, parameterId_);
        const auto* lane = &laneRef;

        DBG("[AUTO-RECOVERY] lane mouseDown track=" << trackId_
            << " target=" << parameterId_
            << " x=" << e.x
            << " y=" << e.y);

        pointDragActive_ = false;
        tensionDragActive_ = false;

        const bool canEdit = isDrawingEnabled();
        if (freehandArmed_ && !e.mods.isPopupMenu() && canEdit
            && canBulkEditCurve())
        {
            freehandArmed_ = false;
            freehandDrawing_ = true;
            freehandStroke_.clear();
            freehandBeforeState_ = laneRef.getState();
            appendFreehandPoint(e);
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;  // pencil owns this gesture; no point/segment drag
        }

        // Check for point click (highest priority)
        for (size_t i = 0; i < lane->points.size(); ++i)
        {
            if (isClickOnPoint(e, lane->points[i]))
            {
                selectedPointIndex_ = (int)i;
                if (e.mods.isPopupMenu())  // Right-click
                {
                    DBG("[AUTO-RECOVERY] lane rightClick hit=point");
                    showPointContextMenu(e, i);
                }
                else if (canEdit)
                {
                    startPointDrag(i, e);
                }

                invalidateCurvePath();
                repaint();
                return;
            }
        }

        // Check for Shift+RightClick point insertion
        if (e.mods.isShiftDown() && e.mods.isPopupMenu())
        {
            insertPointAtClickLocation(e);
            return;
        }

        // Check for tension handle click
        for (size_t i = 0; i + 1 < lane->points.size(); ++i)
        {
            if (isClickOnTensionHandle(e, lane->points[i], lane->points[i + 1]))
            {
                if (e.mods.isPopupMenu())
                {
                    DBG("[AUTO-RECOVERY] lane rightClick hit=tension");
                    showSegmentContextMenu(e, i);
                    return;
                }

                if (canEdit)
                    startTensionDrag(i, e);
                return;
            }
        }

        // Check for segment click
        for (size_t i = 0; i + 1 < lane->points.size(); ++i)
        {
            if (isClickOnSegment(e, lane->points[i], lane->points[i + 1]))
            {
                if (e.mods.isPopupMenu())  // Right-click
                {
                    DBG("[AUTO-RECOVERY] lane rightClick hit=segment");
                    showSegmentContextMenu(e, i);
                }
                return;
            }
        }

        if (e.mods.isPopupMenu())
        {
            DBG("[AUTO-RECOVERY] lane rightClick hit=empty");
            showEmptyContextMenu(e);
        }
    }

    bool hitTest(int x, int y) override
    {
        return getLocalBounds().contains(x, y);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        DBG("[AUTO-RECOVERY] lane mouseDoubleClick track=" << trackId_
            << " target=" << parameterId_
            << " x=" << e.x
            << " y=" << e.y);
        insertPointAtClickLocation(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (freehandDrawing_)
        {
            appendFreehandPoint(e);
            repaint();
            return;
        }

        if (pointDragActive_)
        {
            dragSelectedPoint(e);
            return;
        }

        if (!tensionDragActive_)
            return;

        // Calculate tension change based on vertical drag
        float deltaY = (float)(tensionDragStartY_ - e.y);
        float dragSensitivity = 0.01f;  // per pixel
        float newTension = tensionDragStartValue_ + deltaY * dragSensitivity;

        // Clamp to -1..+1
        newTension = juce::jlimit(-0.99f, 0.99f, newTension);

        // Update tension
        helper_.setSegmentTension(trackId_, parameterId_, 
                                 (int)tensionDragSegmentIndex_, newTension);
            syncApexLaneFromCoreLane();

        // Trigger repaint for visual feedback
        DBG("[AUTO-RECOVERY] lane mouseDrag mode=tension");
        DBG("[AUTO-RECOVERY] dragTension segment=" << (int)tensionDragSegmentIndex_
            << " tension=" << newTension);
        invalidateCurvePath();
        repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (freehandDrawing_)
        {
            appendFreehandPoint(e);
            freehandDrawing_ = false;
            auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
            if (canBulkEditCurve()
                && AutomationFreehandStrokeCore::commit(lane, freehandStroke_))
            {
                undoManager_.doAction(std::make_unique<MovePointAction>(
                    helper_, trackId_, parameterId_,
                    freehandBeforeState_, lane.getState()));
                syncApexLaneFromCoreLane();
                helper_.publishSnapshot();
            }
            freehandBeforeState_ = {};
            freehandStroke_.clear();
            invalidateCurvePath();
            repaint();
            return;
        }

        if (tensionDragActive_)
        {
            tensionDragActive_ = false;

            // Publish final state
            helper_.publishSnapshot();

            invalidateCurvePath();
            repaint();
        }

        if (pointDragActive_)
        {
            pointDragActive_ = false;

            // Record undo for the point move if it actually changed
            if (selectedPointIndex_ >= 0)
            {
                auto* lane = helper_.findLane(trackId_, parameterId_);
                if (lane && selectedPointIndex_ < (int)lane->points.size())
                {
                    const int64_t newTime = lane->points[(size_t)selectedPointIndex_].timeSamples;
                    const float newValue = lane->points[(size_t)selectedPointIndex_].value;

                    if (newTime != pointDragOldTime_ || newValue != pointDragOldValue_)
                    {
                        // ONE logical undo entry for the whole drag gesture,
                        // restoring the exact before/after lane state.
                        auto action = std::make_unique<MovePointAction>(
                            helper_, trackId_, parameterId_,
                            pointDragBeforeState_, lane->getState());

                        undoManager_.doAction(std::move(action));
                    }
                }
            }

            pointDragBeforeState_ = {};
            helper_.publishSnapshot();
            invalidateCurvePath();
            repaint();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        // Ctrl+Shift+Z = Redo  (check BEFORE plain Ctrl+Z so Shift variant is reachable)
        if (key.getKeyCode() == 'z' && key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown())
        {
            if (undoManager_.canRedo())
            {
                undoManager_.redo();
                syncApexLaneFromCoreLane();
                invalidateCurvePath();
                repaint();
            }
            return true;
        }

        // Ctrl+Y = Redo (Windows-style)
        if (key.getKeyCode() == 'y' && key.getModifiers().isCtrlDown())
        {
            if (undoManager_.canRedo())
            {
                undoManager_.redo();
                syncApexLaneFromCoreLane();
                invalidateCurvePath();
                repaint();
            }
            return true;
        }

        // Ctrl+Z = Undo
        if (key.getKeyCode() == 'z' && key.getModifiers().isCtrlDown())
        {
            if (undoManager_.canUndo())
            {
                undoManager_.undo();
                syncApexLaneFromCoreLane();
                invalidateCurvePath();
                repaint();
            }
            return true;
        }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelectedPoint();
            return true;
        }

        return false;
    }

    // ========================================================================
    // CURVE RENDERING
    // ========================================================================

    void setSamplesPerPixel(double samples)
    {
        if (std::abs(samplesPerPixel_ - samples) > 0.1)
        {
            samplesPerPixel_ = samples;
            invalidateCurvePath();
        }
    }

    void setZoomLevel(double zoom)
    {
        if (std::abs(zoomLevel_ - zoom) > 0.01)
        {
            zoomLevel_ = zoom;
            invalidateCurvePath();
        }
    }

    /** Engine sample rate used to convert point timeSamples to seconds when
     *  mirroring the core lane into the APEX lane store. Pushed by the
     *  arrangement view alongside setSamplesPerPixel/setZoomLevel. */
    void setSampleRate(double sampleRate)
    {
        sampleRate_ = juce::jmax(1.0, sampleRate);
    }

    /** Project tempo (BPM) used to convert seconds to PPQ beats when
     *  mirroring the core lane into the APEX lane store. Pushed by the
     *  arrangement view alongside setSamplesPerPixel/setZoomLevel. */
    void setTempoBpm(double bpm)
    {
        tempoBpm_ = juce::jmax(1.0, bpm);
    }

    /**
     * Invalidate curve path cache (call after data changes).
     */
    void invalidateCurvePath()
    {
        curvePathValid_ = false;
    }

private:
    // ========================================================================
    // PRIVATE RENDERING
    // ========================================================================

    // Drawing/editing automation points is permitted when the lane is enabled.
    // The record-arm gate is only for live automation recording (moving faders,
    // knobs, plugin parameters during playback) — not for manual point editing.
    bool isDrawingEnabled() const
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        return (lane != nullptr) && lane->isEnabled();
    }

    bool shouldRenderCurve() const
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        return lane != nullptr && lane->points.size() >= 2;
    }

    void regenerateCurvePath()
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (!lane || lane->points.empty())
        {
            curvePath_ = std::make_unique<juce::Path>();
            curvePathValid_ = true;
            return;
        }

        auto newPath = std::make_unique<juce::Path>();
        const auto& points = lane->points;

        auto bounds = getLocalBounds().toFloat();
        bool firstPoint = true;

        // Draw curve segments between each pair of points
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& pointA = points[i];
            float x = timeSamplesToX(pointA.timeSamples, bounds);
            float y = valueToY(pointA.value, bounds);

            if (firstPoint)
            {
                newPath->startNewSubPath(x, y);
                firstPoint = false;
            }
            else if (i < points.size() - 1)
            {
                // Get curve type and tension from previous point
                const auto& prevPoint = points[i - 1];
                const auto curveType = prevPoint.curveToNext;
                const float tension = prevPoint.tensionToNext;

                // Draw curved segment to current point
                drawCurveSegment(*newPath, prevPoint, pointA, curveType, tension, bounds);
            }
            else
            {
                // Last point - just line to it
                newPath->lineTo(x, y);
            }
        }

        curvePath_ = std::move(newPath);
        curvePathValid_ = true;
    }

    void drawCurveSegment(juce::Path& path,
                         const AutomationPoint& pointA,
                         const AutomationPoint& pointB,
                         AutomationCurveType curveType,
                         float tension,
                         juce::Rectangle<float> bounds)
    {
        // Number of segments to subdivide the curve (higher = smoother but slower)
        const int numSegments = 50;

        float x1 = timeSamplesToX(pointA.timeSamples, bounds);
        float y1 = valueToY(pointA.value, bounds);

        float x2 = timeSamplesToX(pointB.timeSamples, bounds);
        float y2 = valueToY(pointB.value, bounds);

        // Draw smooth curve from pointA to pointB
        for (int seg = 1; seg <= numSegments; ++seg)
        {
            float t = (float)seg / (float)numSegments;  // 0..1

            // Evaluate curve shape at this normalized position
            float shapedT = AutomationCurveEvalCore::shapePosition(t, curveType, tension);

            // Interpolate position
            float x = x1 + (x2 - x1) * t;
            float y = y1 + (y2 - y1) * shapedT;

            path.lineTo(x, y);
        }
    }

    void drawPoints(juce::Graphics& g)
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (!lane)
            return;

        const auto& points = lane->points;
        auto bounds = getLocalBounds().toFloat();

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& point = points[i];
            float x = timeSamplesToX(point.timeSamples, bounds);
            float y = valueToY(point.value, bounds);

            // Clamp to bounds
            if (x >= bounds.getX() && x <= bounds.getRight())
            {
                g.setColour((int)i == selectedPointIndex_ ? juce::Colours::yellow : juce::Colours::cyan.brighter(0.5f));
                g.drawEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f, 1.5f);
            }
        }
    }

    void drawTensionIndicators(juce::Graphics& g)
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (!lane || lane->points.size() < 2)
            return;

        const auto& points = lane->points;
        auto bounds = getLocalBounds().toFloat();

        // Draw tension indicators at segment midpoints
        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            const auto& pointA = points[i];
            const auto& pointB = points[i + 1];

            // Midpoint time
            int64_t midTime = (pointA.timeSamples + pointB.timeSamples) / 2;
            float midX = timeSamplesToX(midTime, bounds);

            // Midpoint value (approximate)
            float midValue = (pointA.value + pointB.value) * 0.5f;
            float midY = valueToY(midValue, bounds);

            // Clamp to bounds
            if (midX >= bounds.getX() && midX <= bounds.getRight())
            {
                // Draw tension value as small indicator
                float tension = pointA.tensionToNext;

                // Color based on tension direction
                if (tension > 0.1f)
                {
                    g.setColour(juce::Colours::orange);  // Ease-out (back-loaded)
                }
                else if (tension < -0.1f)
                {
                    g.setColour(juce::Colours::lightblue);  // Ease-in (front-loaded)
                }
                else
                {
                    g.setColour(juce::Colours::grey);  // Neutral
                }

                // Draw small square as tension indicator
                float tensionRadius = 2.0f + std::abs(tension) * 3.0f;  // Size varies with tension
                g.drawRect(midX - tensionRadius, midY - tensionRadius, 
                          tensionRadius * 2.0f, tensionRadius * 2.0f, 1.0f);
            }
        }
    }

    // ========================================================================
    // MEMBERS
    // ========================================================================

    AutomationUIHelper& helper_;
    TrackID trackId_;
    juce::String parameterId_;

    std::unique_ptr<juce::Path> curvePath_;
    bool curvePathValid_;

    double samplesPerPixel_;
    double zoomLevel_;
    double sampleRate_;
    double tempoBpm_;

    // ========================================================================
    // UNDO/REDO SYSTEM (Phase 3)
    // ========================================================================

    UndoManager undoManager_;
    GroupedCurveCallback groupedCurveCallback_;
    SelectedSnapshotCallback selectedSnapshotCallback_;

    // ========================================================================
    // INTERACTION STATE & HELPERS (Phase 2)
    // ========================================================================

    bool tensionDragActive_ = false;
    size_t tensionDragSegmentIndex_ = 0;
    int tensionDragStartY_ = 0;
    float tensionDragStartValue_ = 0.0f;
    bool pointDragActive_ = false;
    int selectedPointIndex_ = -1;
    int64_t pointDragMinTime_ = 0;
    int64_t pointDragMaxTime_ = std::numeric_limits<int64_t>::max();
    int64_t pointDragOldTime_ = 0;
    float pointDragOldValue_ = 0.0f;
    // Complete lane state captured at drag start — the MovePointAction undo
    // payload.  One action per drag gesture, not one per mouse-motion sample.
    juce::ValueTree pointDragBeforeState_;

    // A one-shot touch/mouse pencil gesture. Buffers only UI samples and
    // commits once, so large gestures do not publish thousands of DSP lanes.
    bool freehandArmed_ = false;
    bool freehandDrawing_ = false;
    std::vector<AutomationPoint> freehandStroke_;
    juce::ValueTree freehandBeforeState_;

    void appendFreehandPoint(const juce::MouseEvent& e)
    {
        AutomationPoint point;
        point.timeSamples = xToTimeSamples((float)e.x);
        point.value = yToValue((float)e.y);
        point.curveToNext = AutomationCurveType::Linear;
        point.tensionToNext = 0.0f;

        if (!freehandStroke_.empty()
            && freehandStroke_.back().timeSamples == point.timeSamples)
            freehandStroke_.back() = point;
        else if (freehandStroke_.size() < AutomationFreehandStrokeCore::maxStrokePoints)
            freehandStroke_.push_back(point);
        else
            freehandStroke_.back() = point; // always retain newest endpoint
    }

    bool isClickOnPoint(const juce::MouseEvent& e, const AutomationPoint& point)
    {
        return isPointAt((float)e.x, (float)e.y, point);
    }

    bool isPointAt(float clickX, float clickY, const AutomationPoint& point)
    {
        auto bounds = getLocalBounds().toFloat();
        float x = bounds.getX() + (float)point.timeSamples / samplesPerPixel_;
        float y = valueToY(point.value, bounds);

        float clickRadius = 5.0f;  // pixels
        return clickX >= x - clickRadius && clickX <= x + clickRadius &&
               clickY >= y - clickRadius && clickY <= y + clickRadius;
    }

    bool isClickOnTensionHandle(const juce::MouseEvent& e, 
                               const AutomationPoint& a,
                               const AutomationPoint& b)
    {
        return isTensionHandleAt((float)e.x, (float)e.y, a, b);
    }

    bool isTensionHandleAt(float clickX, float clickY,
                           const AutomationPoint& a,
                           const AutomationPoint& b)
    {
        auto bounds = getLocalBounds().toFloat();

        // Midpoint of segment
        int64_t midTime = (a.timeSamples + b.timeSamples) / 2;
        float midValue = (a.value + b.value) * 0.5f;

        float x = bounds.getX() + (float)midTime / samplesPerPixel_;
        float y = valueToY(midValue, bounds);

        float clickRadius = 4.0f;
        return clickX >= x - clickRadius && clickX <= x + clickRadius &&
               clickY >= y - clickRadius && clickY <= y + clickRadius;
    }

    bool isClickOnSegment(const juce::MouseEvent& e,
                         const AutomationPoint& a,
                         const AutomationPoint& b)
    {
        return isSegmentAt((float)e.x, (float)e.y, a, b);
    }

    bool isSegmentAt(float clickX, float clickY,
                     const AutomationPoint& a,
                     const AutomationPoint& b)
    {
        auto bounds = getLocalBounds().toFloat();

        // Sample curve at regular intervals
        const int samples = 50;
        float clickRadius = 3.0f;

        for (int i = 0; i <= samples; ++i)
        {
            float t = (float)i / (float)samples;

            // Evaluate curve position
            float shapedT = AutomationCurveEvalCore::shapePosition(
                t, a.curveToNext, a.tensionToNext);

            float curveTime = a.timeSamples + (b.timeSamples - a.timeSamples) * t;
            float curveValue = a.value + (b.value - a.value) * shapedT;

            float x = bounds.getX() + (float)curveTime / samplesPerPixel_;
            float y = valueToY(curveValue, bounds);

            if (std::abs(clickX - x) < clickRadius && std::abs(clickY - y) < clickRadius)
                return true;
        }
        return false;
    }

    void startTensionDrag(size_t segmentIndex, const juce::MouseEvent& e)
    {
        tensionDragActive_ = true;
        tensionDragSegmentIndex_ = segmentIndex;
        tensionDragStartY_ = e.y;
        tensionDragStartValue_ = 0.0f;

        if (auto* lane = helper_.findLane(trackId_, parameterId_))
        {
            if (segmentIndex < lane->points.size())
                tensionDragStartValue_ = lane->points[segmentIndex].tensionToNext;
        }
    }

    void startPointDrag(size_t pointIndex, const juce::MouseEvent& e)
    {
        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (lane == nullptr || pointIndex >= lane->points.size())
            return;

        pointDragActive_ = true;
        selectedPointIndex_ = (int)pointIndex;
        pointDragMinTime_ = pointIndex > 0 ? lane->points[pointIndex - 1].timeSamples + 1 : 0;
        pointDragMaxTime_ = pointIndex + 1 < lane->points.size()
            ? lane->points[pointIndex + 1].timeSamples - 1
            : std::numeric_limits<int64_t>::max();

        // Save original position for undo
        pointDragOldTime_ = lane->points[pointIndex].timeSamples;
        pointDragOldValue_ = lane->points[pointIndex].value;

        // Capture the complete lane state — the MovePointAction undo payload.
        pointDragBeforeState_ = lane->getState();

        DBG("[AUTO-RECOVERY] lane mouseDrag mode=point begin x=" << e.x << " y=" << e.y);
    }

    void dragSelectedPoint(const juce::MouseEvent& e)
    {
        if (selectedPointIndex_ < 0)
            return;

        const auto time = juce::jlimit(pointDragMinTime_, pointDragMaxTime_, xToTimeSamples((float)e.x));
        const auto value = yToValue((float)e.y);

        helper_.movePoint(trackId_, parameterId_, selectedPointIndex_, time, value);
        syncApexLaneFromCoreLane();
        DBG("[AUTO-RECOVERY] lane mouseDrag mode=point");
        DBG("[AUTO-RECOVERY] dragPoint target=" << parameterId_
            << " point=" << selectedPointIndex_
            << " time=" << (juce::int64)time
            << " value=" << value);

        invalidateCurvePath();
        repaint();
    }

    void deleteSelectedPoint()
    {
        if (selectedPointIndex_ < 0)
            return;

        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (lane && selectedPointIndex_ < (int)lane->points.size())
        {
            // Push undo action before deleting
            auto action = std::make_unique<DeletePointAction>(
                helper_, trackId_, parameterId_, selectedPointIndex_,
                lane->points[(size_t)selectedPointIndex_]);

            undoManager_.doAction(std::move(action));
        }

        syncApexLaneFromCoreLane();
        selectedPointIndex_ = -1;
        invalidateCurvePath();
        repaint();
    }

    void showPointContextMenu(const juce::MouseEvent& e, size_t pointIndex)
    {
        juce::PopupMenu menu;

        // Delete
        menu.addItem("Delete Point", [this, pointIndex]() {
            auto* lane = helper_.findLane(trackId_, parameterId_);
            if (lane && pointIndex < lane->points.size())
            {
                // Create undo action with saved point
                auto action = std::make_unique<DeletePointAction>(
                    helper_, trackId_, parameterId_, (int)pointIndex,
                    lane->points[pointIndex]);

                undoManager_.doAction(std::move(action));
                syncApexLaneFromCoreLane();
                selectedPointIndex_ = -1;
                invalidateCurvePath();
                repaint();
            }
        });

        menu.addSeparator();

        // Copy/Paste values
        menu.addItem("Copy Value", [this, pointIndex]() {
            helper_.copyPointValue(trackId_, parameterId_, (int)pointIndex);
        });

        if (helper_.canPastePointValue())
        {
            menu.addItem("Paste Value", [this, pointIndex]() {
                auto* lane = helper_.findLane(trackId_, parameterId_);
                if (lane && pointIndex < lane->points.size())
                {
                    float oldValue = lane->points[pointIndex].value;
                    float newValue = 0.0f;

                    // Get the copied value
                    helper_.pastePointValue(trackId_, parameterId_, (int)pointIndex);

                    // Get new value
                    if (auto* updatedLane = helper_.findLane(trackId_, parameterId_))
                    {
                        if (pointIndex < updatedLane->points.size())
                            newValue = updatedLane->points[pointIndex].value;
                    }

                    // Create undo action (need to reverse the paste first)
                    helper_.setPointExactValue(trackId_, parameterId_, (int)pointIndex, oldValue);

                    auto action = std::make_unique<PastePointValueAction>(
                        helper_, trackId_, parameterId_, (int)pointIndex,
                        newValue, oldValue);

                    undoManager_.doAction(std::move(action));
                    syncApexLaneFromCoreLane();
                    invalidateCurvePath();
                    repaint();
                }
            });
        }

        menu.addItem("Reset Value", [this, pointIndex]() {
            if (helper_.resetPointValue(trackId_, parameterId_, (int)pointIndex))
            {
                syncApexLaneFromCoreLane();
                invalidateCurvePath();
                repaint();
            }
        });

        auto addSegmentActions = [this, &menu](int segmentIndex, const juce::String& label)
        {
            auto* lane = helper_.findLane(trackId_, parameterId_);
            if (lane == nullptr || segmentIndex < 0 || segmentIndex + 1 >= (int)lane->points.size())
                return;

            menu.addSeparator();

            juce::PopupMenu curveMenu;
            auto allCurveTypes = getAllCurveTypes();
            for (auto type : allCurveTypes)
            {
                curveMenu.addItem(getCurveTypeName(type), [this, segmentIndex, type]() {
                    auto* editableLane = helper_.findLane(trackId_, parameterId_);
                    if (editableLane && segmentIndex < (int)editableLane->points.size())
                    {
                        AutomationCurveType oldType = editableLane->points[(size_t)segmentIndex].curveToNext;
                        auto action = std::make_unique<SetCurveTypeAction>(
                            helper_, trackId_, parameterId_, segmentIndex,
                            type, oldType);

                        undoManager_.doAction(std::move(action));
                    syncApexLaneFromCoreLane();
                        invalidateCurvePath();
                        repaint();
                    }
                });
            }

            menu.addSubMenu(label + " Curve Type", curveMenu);

            menu.addItem("Copy " + label + " Shape", [this, segmentIndex]() {
                helper_.copySegmentShape(trackId_, parameterId_, segmentIndex);
            });

            if (helper_.canPasteSegmentShape())
            {
                menu.addItem("Paste " + label + " Shape", [this, segmentIndex]() {
                    if (helper_.pasteSegmentShape(trackId_, parameterId_, segmentIndex, false))
                    {
                        invalidateCurvePath();
                        repaint();
                    }
                });

                menu.addItem("Paste " + label + " Shape + Values", [this, segmentIndex]() {
                    if (helper_.pasteSegmentShape(trackId_, parameterId_, segmentIndex, true))
                    {
                        invalidateCurvePath();
                        repaint();
                    }
                });
            }

            menu.addItem("Reset " + label + " Tension", [this, segmentIndex]() {
                auto* editableLane = helper_.findLane(trackId_, parameterId_);
                if (editableLane && segmentIndex < (int)editableLane->points.size())
                {
                    float oldTension = editableLane->points[(size_t)segmentIndex].tensionToNext;
                    auto action = std::make_unique<ResetTensionAction>(
                        helper_, trackId_, parameterId_, segmentIndex, oldTension);

                    undoManager_.doAction(std::move(action));
                syncApexLaneFromCoreLane();
                    invalidateCurvePath();
                    repaint();
                }
            });
        };

        addSegmentActions((int)pointIndex - 1, "Previous Segment");
        addSegmentActions((int)pointIndex, "Next Segment");

        // Show menu
        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(this)
                .withTargetScreenArea(juce::Rectangle<int>::leftTopRightBottom(
                    e.getScreenX(), e.getScreenY(), e.getScreenX() + 1, e.getScreenY() + 1)),
            nullptr);
    }

    void showSegmentContextMenu(const juce::MouseEvent& e, size_t segmentIndex)
    {
        juce::PopupMenu menu;

        // Add Point
        menu.addItem("Add Point Here", [this, e, segmentIndex]() {
            insertPointAtClickLocation(e);
        });

        menu.addSeparator();

        menu.addItem("Add Point Keeping Level", [this, e]() {
            const auto timeSamples = xToTimeSamples((float)e.x);
            const auto index = helper_.insertPointPreservingLevel(trackId_, parameterId_, timeSamples);
            syncApexLaneFromCoreLane();
            selectedPointIndex_ = index;
            DBG("[AUTO-RECOVERY] insertPoint target=" << parameterId_
                << " time=" << (juce::int64)timeSamples
                << " value=preserve");
            invalidateCurvePath();
            repaint();
        });

        menu.addSeparator();

        // Curve type submenu
        juce::PopupMenu curveMenu;
        auto allCurveTypes = getAllCurveTypes();

        for (size_t i = 0; i < allCurveTypes.size(); ++i)
        {
            auto type = allCurveTypes[i];
            juce::String typeName = getCurveTypeName(type);

            curveMenu.addItem(typeName, [this, segmentIndex, type]() {
                auto* lane = helper_.findLane(trackId_, parameterId_);
                if (lane && segmentIndex < lane->points.size())
                {
                    // Get old type
                    AutomationCurveType oldType = lane->points[segmentIndex].curveToNext;

                    // Create undo action
                    auto action = std::make_unique<SetCurveTypeAction>(
                        helper_, trackId_, parameterId_, (int)segmentIndex,
                        type, oldType);

                    undoManager_.doAction(std::move(action));
                    invalidateCurvePath();
                    repaint();
                }
            });
        }
        menu.addSubMenu("Set Curve Type", curveMenu);

        menu.addSeparator();

        // Tension reset
        menu.addItem("Reset Tension", [this, segmentIndex]() {
            auto* lane = helper_.findLane(trackId_, parameterId_);
            if (lane && segmentIndex < lane->points.size())
            {
                float oldTension = lane->points[segmentIndex].tensionToNext;

                // Create undo action
                auto action = std::make_unique<ResetTensionAction>(
                    helper_, trackId_, parameterId_, (int)segmentIndex, oldTension);

                undoManager_.doAction(std::move(action));
                invalidateCurvePath();
                repaint();
            }
        });

        // Show menu
        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(this)
                .withTargetScreenArea(juce::Rectangle<int>::leftTopRightBottom(
                    e.getScreenX(), e.getScreenY(), e.getScreenX() + 1, e.getScreenY() + 1)),
            nullptr);
    }

    void showEmptyContextMenu(const juce::MouseEvent& e)
    {
        juce::PopupMenu menu;
        menu.addItem("Add Point", [this, e]() { insertPointAtClickLocation(e); });
        if (isDrawingEnabled() && canBulkEditCurve())
            menu.addItem("Freehand Pencil - Next Drag", [this]()
            {
                freehandArmed_ = true;
                setMouseCursor(juce::MouseCursor::CrosshairCursor);
            });
        menu.addItem("Add Point Keeping Level", [this, e]() {
            const auto timeSamples = xToTimeSamples((float)e.x);
            selectedPointIndex_ = helper_.insertPointPreservingLevel(trackId_, parameterId_, timeSamples);
            syncApexLaneFromCoreLane();

            // Push undo action for the inserted point
            if (selectedPointIndex_ >= 0)
            {
                if (auto* lane = helper_.findLane(trackId_, parameterId_))
                {
                    if (selectedPointIndex_ < (int)lane->points.size())
                    {
                        const auto& pt = lane->points[(size_t)selectedPointIndex_];
                        auto action = std::make_unique<InsertPointAction>(
                            helper_, trackId_, parameterId_, pt);
                        undoManager_.doAction(std::move(action));
                    }
                }
            }

            DBG("[AUTO-RECOVERY] insertPoint target=" << parameterId_
                << " time=" << (juce::int64)timeSamples
                << " value=preserve");
            invalidateCurvePath();
            repaint();
        });

        // Explicit full-lane edit, distinct from point/segment clipboards.
        // A snapshot publish reapplies clip-owned regions; never offer a bulk
        // edit that would be silently reverted or partially masked in DSP.
        if (!canBulkEditCurve())
            menu.addSectionHeader("Bulk edits unavailable: Automation Clip owns this lane");

        // Only an exact parameter match can paste: pan/volume/foreign VST3
        // ranges must never be silently reinterpreted.
        menu.addSeparator();
        if (auto* sourceLane = helper_.findLane(trackId_, parameterId_);
            sourceLane != nullptr && !sourceLane->points.empty())
        {
            menu.addItem("Copy Entire Curve", [this]()
            {
                if (auto* lane = helper_.findLane(trackId_, parameterId_))
                    AutomationLaneCurveClipboardCore::shared().copyFrom(*lane, sampleRate_, tempoBpm_);
            });
        }

        if (isDrawingEnabled() && canBulkEditCurve()
            && AutomationLaneCurveClipboardCore::shared().hasCurveFor(parameterId_))
        {
            menu.addItem("Replace Entire Curve (Undoable)", [this]()
            {
                // findLane() intentionally exposes a const view. Edits must
                // use the message-thread mutable getOrCreateLane() API.
                auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
                if (!lane.isEnabled() || !canBulkEditCurve())
                    return;

                std::vector<AutomationPoint> pasted;
                if (!AutomationLaneCurveClipboardCore::shared()
                         .makePastedPoints(parameterId_, sampleRate_, tempoBpm_, pasted))
                    return;

                const auto before = lane.getState();
                lane.points = std::move(pasted);
                lane.sortAndResolveDuplicates();
                const auto after = lane.getState();

                // A single atomic history entry (rather than one per point).
                undoManager_.doAction(std::make_unique<MovePointAction>(
                    helper_, trackId_, parameterId_, before, after));
                syncApexLaneFromCoreLane();
                helper_.publishSnapshot();
                selectedPointIndex_ = -1;
                invalidateCurvePath();
                repaint();
            });
        }

        // Producer-friendly one-click musical modulation patterns. Generate
        // OFFLINE points once; playback consumes normal APEX lane snapshots.
        if (isDrawingEnabled() && canBulkEditCurve()
            && AutomationMusicalLfoCore::supports(parameterId_))
        {
            juce::PopupMenu lfoMenu;
            const auto addLfoItem = [this, &lfoMenu](const juce::String& name,
                                                    AutomationMusicalLfoCore::Shape shape)
            {
                lfoMenu.addItem(name, [this, shape]()
                {
                    std::vector<AutomationPoint> generated;
                    if (!AutomationMusicalLfoCore::generate(parameterId_, shape,
                            sampleRate_, tempoBpm_, generated))
                        return;

                    auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
                    if (!lane.isEnabled() || !canBulkEditCurve())
                        return;

                    const auto before = lane.getState();
                    lane.points = std::move(generated);
                    lane.sortAndResolveDuplicates();
                    const auto after = lane.getState();

                    undoManager_.doAction(std::make_unique<MovePointAction>(
                        helper_, trackId_, parameterId_, before, after));
                    syncApexLaneFromCoreLane();
                    helper_.publishSnapshot();
                    selectedPointIndex_ = -1;
                    invalidateCurvePath();
                    repaint();
                });
            };

            addLfoItem("Sine · 1 bar", AutomationMusicalLfoCore::Shape::Sine);
            addLfoItem("Triangle · 1 bar", AutomationMusicalLfoCore::Shape::Triangle);
            addLfoItem("Square · 1 bar", AutomationMusicalLfoCore::Shape::Square);
            addLfoItem("Saw · 1 bar", AutomationMusicalLfoCore::Shape::Saw);
            menu.addSubMenu("Generate 1-Bar LFO Curve (Undoable)", lfoMenu);
        }

        if (isDrawingEnabled() && canBulkEditCurve()
            && AutomationCurveTransformCore::supports(parameterId_))
        {
            juce::PopupMenu transformations;
            const auto addTransform = [this, &transformations](
                const juce::String& label, AutomationCurveTransformCore::Operation op)
            {
                transformations.addItem(label, [this, op]()
                {
                    auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
                    if (!lane.isEnabled() || lane.points.empty() || !canBulkEditCurve())
                        return;

                    std::vector<AutomationPoint> transformed;
                    if (!AutomationCurveTransformCore::transform(parameterId_, op, lane.points,
                            sampleRate_, tempoBpm_, transformed))
                        return;

                    const auto before = lane.getState();
                    lane.points = std::move(transformed);
                    lane.sortAndResolveDuplicates();
                    undoManager_.doAction(std::make_unique<MovePointAction>(
                        helper_, trackId_, parameterId_, before, lane.getState()));
                    syncApexLaneFromCoreLane();
                    helper_.publishSnapshot();
                    selectedPointIndex_ = -1;
                    invalidateCurvePath();
                    repaint();
                });
            };
            addTransform("Invert Values", AutomationCurveTransformCore::Operation::InvertValues);
            addTransform("Compress Levels 50%", AutomationCurveTransformCore::Operation::CompressHalf);
            addTransform("Expand Levels 200%", AutomationCurveTransformCore::Operation::ExpandDouble);
            addTransform("Shift Right 1 Beat", AutomationCurveTransformCore::Operation::ShiftRightBeat);
            menu.addSubMenu("Transform Entire Curve (Undoable)", transformations);
        }

        // Creative A/B blend against the copied whole-curve snapshot. This
        // is OFFLINE point generation, not signal-rate or VST3 parameter DSP.
        if (isDrawingEnabled() && canBulkEditCurve()
            && AutomationLaneCurveClipboardCore::shared().hasCurveFor(parameterId_))
        {
            menu.addItem("Morph 50% Toward Copied Curve (Undoable)", [this]()
            {
                std::vector<AutomationPoint> target;
                if (!AutomationLaneCurveClipboardCore::shared()
                         .makePastedPoints(parameterId_, sampleRate_, tempoBpm_, target))
                    return;

                auto& lane = helper_.getOrCreateLane(trackId_, parameterId_);
                if (!lane.isEnabled() || lane.points.empty() || !canBulkEditCurve())
                    return;

                std::vector<AutomationPoint> blended;
                if (!AutomationCurveMorphCore::blend(
                        parameterId_, lane.points, target, 0.5f, blended))
                    return;

                const auto before = lane.getState();
                lane.points = std::move(blended);
                lane.sortAndResolveDuplicates();
                undoManager_.doAction(std::make_unique<MovePointAction>(
                    helper_, trackId_, parameterId_, before, lane.getState()));
                syncApexLaneFromCoreLane();
                helper_.publishSnapshot();
                selectedPointIndex_ = -1;
                invalidateCurvePath();
                repaint();
            });
        }

        // Deliberate grouped edit: only pre-selected, existing native lanes.
        // The Arrangement owner decides the exact set of selected tracks.
        if (isDrawingEnabled() && groupedCurveCallback_
            && (parameterId_ == AutomationLaneCore::trackVolumeParameterId
                || parameterId_ == AutomationLaneCore::trackPanParameterId))
        {
            juce::Component::SafePointer<AutomationLaneComponent> safeLane(this);
            menu.addItem("Copy This Curve to Selected Clip Tracks (Undoable)", [safeLane]()
            {
                auto* lane = safeLane.getComponent();
                if (lane == nullptr || !lane->groupedCurveCallback_
                    || !lane->isDrawingEnabled())
                    return;

                const auto* source = lane->helper_.findLane(
                    lane->trackId_, lane->parameterId_);
                if (source != nullptr && source->isEnabled()
                    && !source->points.empty()
                    && lane->groupedCurveCallback_(
                        lane->trackId_, lane->parameterId_, source->points))
                {
                    lane->invalidateCurvePath();
                    lane->repaint();
                }
            });
        }

        // Visible, producer-facing multi-track A/B: only explicitly selected
        // audio clips are permitted to define participating track IDs.
        // The owner rejects stale selections and mismatched A/B target sets.
        if (isDrawingEnabled() && selectedSnapshotCallback_
            && (parameterId_ == AutomationLaneCore::trackVolumeParameterId
                || parameterId_ == AutomationLaneCore::trackPanParameterId))
        {
            juce::PopupMenu snapshots;
            juce::Component::SafePointer<AutomationLaneComponent> safeLane(this);
            const auto action = [safeLane](SelectedSnapshotAction what)
            {
                if (auto* lane = safeLane.getComponent())
                    if (lane->selectedSnapshotCallback_)
                        lane->selectedSnapshotCallback_(
                            lane->trackId_, lane->parameterId_, what);
            };
            snapshots.addItem("Capture A (selected tracks)", [action]()
            {
                action(SelectedSnapshotAction::CaptureA);
            });
            snapshots.addItem("Capture B (selected tracks)", [action]()
            {
                action(SelectedSnapshotAction::CaptureB);
            });
            snapshots.addSeparator();
            snapshots.addItem("Morph A/B 50% (Undoable)", [action]()
            {
                action(SelectedSnapshotAction::MorphHalf);
            });
            menu.addSubMenu("Multi-Track A/B · Session Memory", snapshots);
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(this)
                .withTargetScreenArea(juce::Rectangle<int>::leftTopRightBottom(
                    e.getScreenX(), e.getScreenY(), e.getScreenX() + 1, e.getScreenY() + 1)),
            nullptr);
    }

    void insertPointAtClickLocation(const juce::MouseEvent& e)
    {
        if (!isDrawingEnabled())
        {
            DBG("[AUTO-RECOVERY] insertPoint blocked: lane disabled or record not armed");
            return;
        }

        const auto timeSamples = xToTimeSamples((float)e.x);
        const auto value = yToValue((float)e.y);

        // Check if there's already a point at this exact time (replace vs insert)
        const bool isReplace = [this, timeSamples]() {
            if (auto* lane = helper_.findLane(trackId_, parameterId_))
            {
                for (const auto& pt : lane->points)
                    if (pt.timeSamples == timeSamples)
                        return true;
            }
            return false;
        }();

        helper_.addOrReplacePoint(trackId_, parameterId_, timeSamples, value);
        syncApexLaneFromCoreLane();

        if (auto* lane = helper_.findLane(trackId_, parameterId_))
        {
            selectedPointIndex_ = -1;
            for (size_t i = 0; i < lane->points.size(); ++i)
                if (lane->points[i].timeSamples == timeSamples)
                    selectedPointIndex_ = (int)i;

            // Push an undo action for this point insertion (full point,
            // preserving curve/tension for exact redo).
            if (selectedPointIndex_ >= 0)
            {
                auto action = std::make_unique<InsertPointAction>(
                    helper_, trackId_, parameterId_, lane->points[(size_t)selectedPointIndex_]);

                undoManager_.doAction(std::move(action));
            }
        }

        DBG("[AUTO-RECOVERY] insertPoint target=" << parameterId_
            << " time=" << (juce::int64)timeSamples
            << " value=" << value);

        invalidateCurvePath();
        repaint();
    }

    void syncApexLaneFromCoreLane()
    {
        const auto key = apexKeyForParameter();
        if (key.isEmpty())
            return;

        auto* lane = helper_.findLane(trackId_, parameterId_);
        if (lane == nullptr)
            return;

        auto& registry = apex::automation::AutomationParameterKeyRegistry::getInstance();
        const auto id = registry.getOrCreateID(key);
        auto& apexLane = apex::automation::AutomationLaneStore::getInstance().getOrCreateLane(id);

        apex::automation::AutomationLane::PointVector points;
        points.reserve(lane->points.size());
        // Convert core-lane sample positions to APEX PPQ beats using the real
        // engine sample rate and project tempo. The previous implementation
        // divided by (samplesPerPixel_ * zoomLevel_) — a zoom-dependent pixel
        // quantity, not seconds — and then hardcoded 2.0 beats/second (120 BPM).
        // At any other tempo the mirrored points landed at wrong PPQ positions,
        // so the APEX evaluator (default mode Read) wrote wrong values into the
        // mixer parameters (volume/mute/pan/solo), muting or silencing tracks
        // during playback after any lane edit.
        const double ppqPerSecond = tempoBpm_ / 60.0;
        for (const auto& point : lane->points)
        {
            const double timeSeconds = (double)point.timeSamples / juce::jmax(1.0, sampleRate_);
            points.push_back({
                juce::jmax(0.0, timeSeconds) * ppqPerSecond,
                normaliseForApex(parameterId_, point.value),
                toApexCurveType(point.curveToNext),
                juce::jlimit(-1.0f, 1.0f, point.tensionToNext)
            });
        }

        apexLane.replacePoints(std::move(points));
    }

    juce::String apexKeyForParameter() const
    {
        using KR = apex::automation::AutomationParameterKeyRegistry;

        if (parameterId_.startsWith("track." + trackId_ + ".") || parameterId_.startsWith("plugin." + trackId_ + "."))
            return parameterId_;

        // Manual plugin automation lanes use the core/legacy parameter format:
        // plugin.<slot>.<pluginInstanceId>.<parameterId>.  Playback already
        // consumes this from AutomationManagerCore, and we mirror it to APEX
        // with a stable track-scoped bridge key so sequencer/APEX readers can
        // resolve it without needing the plugin display name.
        if (parameterId_.startsWith("plugin."))
            return "plugin." + trackId_ + ".core." + parameterId_;

        if (parameterId_ == AutomationLaneCore::trackVolumeParameterId)
            return KR::trackVolumeKey(trackId_);

        if (parameterId_ == AutomationLaneCore::trackPanParameterId)
            return KR::trackPanKey(trackId_);

        if (parameterId_ == "track.mute")
            return KR::trackMuteKey(trackId_);

        if (parameterId_ == "track.solo")
            return KR::trackSoloKey(trackId_);

        return {};
    }

    static float normaliseForApex(const juce::String& parameterId, float value) noexcept
    {
        if (parameterId == AutomationLaneCore::trackPanParameterId)
            return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);

        if (parameterId == "track.mute" || parameterId == "track.solo")
            return value >= 0.5f ? 1.0f : 0.0f;

        return juce::jlimit(0.0f, 1.0f, value);
    }

    static apex::automation::CurveType toApexCurveType(AutomationCurveType type) noexcept
    {
        switch (type)
        {
            case AutomationCurveType::Hold:
            case AutomationCurveType::Stairs:
            case AutomationCurveType::Pulse:
                return apex::automation::CurveType::Hold;
            case AutomationCurveType::Smooth:
            case AutomationCurveType::Exponential:
            case AutomationCurveType::Logarithmic:
            case AutomationCurveType::SCurve:
            case AutomationCurveType::HalfSine:
            case AutomationCurveType::SmoothStairs:
            case AutomationCurveType::Wave:
            case AutomationCurveType::TapeBrake:
                return apex::automation::CurveType::Smooth;
            case AutomationCurveType::Linear:
            default:
                return apex::automation::CurveType::Linear;
        }
    }

    int64_t xToTimeSamples(float x) const noexcept
    {
        return juce::jmax<int64_t>(0, (int64_t)std::llround((double)juce::jmax(0.0f, x) * samplesPerPixel_));
    }

    float timeSamplesToX(int64_t timeSamples, juce::Rectangle<float> bounds) const noexcept
    {
        return bounds.getX() + (float)((double)timeSamples / juce::jmax(1.0, samplesPerPixel_));
    }

    float yToValue(float y) const noexcept
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto normalised = juce::jlimit(0.0f, 1.0f, 1.0f - ((y - bounds.getY()) / juce::jmax(1.0f, bounds.getHeight())));

        if (parameterId_ == AutomationLaneCore::trackPanParameterId)
            return normalised * 2.0f - 1.0f;

        if (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipPitchSuffix))
            return normalised * 72.0f - 36.0f;

        if (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipStretchSuffix))
            return 0.1f + normalised * 3.9f;

        if (parameterId_ == AutomationLaneCore::trackTapeStopParameterId
            || (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipTapeStopSuffix)))
            return 1.0f - normalised;

        if (parameterId_.startsWith("plugin.") || parameterId_.startsWith("instrument."))
            return normalised;

        return normalised * 2.0f;
    }

    float valueToNormalised(float value) const noexcept
    {
        if (parameterId_ == AutomationLaneCore::trackPanParameterId)
            return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);

        if (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipPitchSuffix))
            return juce::jlimit(0.0f, 1.0f, (value + 36.0f) / 72.0f);

        if (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipStretchSuffix))
            return juce::jlimit(0.0f, 1.0f, (value - 0.1f) / 3.9f);

        if (parameterId_ == AutomationLaneCore::trackTapeStopParameterId
            || (parameterId_.startsWith("clip.") && parameterId_.endsWith(AutomationLaneCore::clipTapeStopSuffix)))
            return juce::jlimit(0.0f, 1.0f, 1.0f - value);

        if (parameterId_.startsWith("plugin.") || parameterId_.startsWith("instrument."))
            return juce::jlimit(0.0f, 1.0f, value);

        return juce::jlimit(0.0f, 1.0f, value * 0.5f);
    }

    float valueToY(float value, juce::Rectangle<float> bounds) const noexcept
    {
        return bounds.getBottom() - valueToNormalised(value) * bounds.getHeight();
    }

    static std::vector<AutomationCurveType> getAllCurveTypes()
    {
        return {
            AutomationCurveType::Linear,
            AutomationCurveType::Smooth,
            AutomationCurveType::Hold,
            AutomationCurveType::SingleCurve,
            AutomationCurveType::SingleCurve2,
            AutomationCurveType::SingleCurve3,
            AutomationCurveType::DoubleCurve,
            AutomationCurveType::DoubleCurve2,
            AutomationCurveType::DoubleCurve3,
            AutomationCurveType::HalfSine,
            AutomationCurveType::Stairs,
            AutomationCurveType::SmoothStairs,
            AutomationCurveType::Wave,
            AutomationCurveType::Pulse
        };
    }

    static juce::String getCurveTypeName(AutomationCurveType type)
    {
        switch (type)
        {
            case AutomationCurveType::Linear: return "Linear";
            case AutomationCurveType::Smooth: return "Smooth";
            case AutomationCurveType::Hold: return "Hold";
            case AutomationCurveType::SingleCurve: return "Single Curve";
            case AutomationCurveType::SingleCurve2: return "Single Curve 2";
            case AutomationCurveType::SingleCurve3: return "Single Curve 3";
            case AutomationCurveType::DoubleCurve: return "Double Curve";
            case AutomationCurveType::DoubleCurve2: return "Double Curve 2";
            case AutomationCurveType::DoubleCurve3: return "Double Curve 3";
            case AutomationCurveType::HalfSine: return "Half Sine";
            case AutomationCurveType::Stairs: return "Stairs";
            case AutomationCurveType::SmoothStairs: return "Smooth Stairs";
            case AutomationCurveType::Wave: return "Wave";
            case AutomationCurveType::Pulse: return "Pulse";
            default: return "Unknown";
        }
    }
};

} // namespace DAW
#include "MainComponent.h"
#include "KeyBindingCore/GlobalTransportSpaceHook.h"
#include "AutomationSequence/AutomationSequenceDialogComponent.h"
#include "Automation/AutomationSystemCore.h"
#include "Automation/AutomationTransportStateCore.h"
#include "PluginScanCore/PluginScanAuditLogCore.h"
#include "PluginHostCore/PluginHostingProductPolicyCore.h"
#include "PluginSafetyCore/PluginCrashGuardCore.h"
#include "Bubblegum/BubblegumAppearanceSettings.h"
#include "Bubblegum/BubblegumFolderProjectionCore.h"
#include "ThemeCore/Theme.h"
#include "MidiCore/MidiClip.h"
#include "CommandCore/CommandManager.h"
#include "CommandCore/GeneralCommands.h"
#include "RenderCore/ExportProgressWindow.h"
#include "UICore/CrashRecoveryDialogComponent.h"
#include "TrackCore/TrackReorderCore.h"
#include "UICore/CursorThemeCore.h"
#include "DeviceCore/SafeAsioDeviceTypeCore.h"
#include "DeviceCore/AudioDeviceBlockAdapterCore.h"
#include "VocalTuneCore/ApexTuneIntegrationCore.h"
#include "RecordingCore/RecordingInputValidityCore.h"
#include "UICore/QuickSendPopup.h"
#include <set>
#include <algorithm>
#include <cstdlib>

namespace
{
    // Request a practical multichannel input ceiling so JUCE can expose and
    // compact the user's active ASIO input pair instead of hard-limiting the
    // app to channels 1-2.
    static constexpr int kMaxHardwareInputChannels = 64;

    class RealtimeDeviceCallbackGuard
    {
    public:
        explicit RealtimeDeviceCallbackGuard(DAW::ApplicationCore& app) noexcept
            : app_(app.beginRealtimeDeviceCallback() ? &app : nullptr)
        {
        }

        ~RealtimeDeviceCallbackGuard() noexcept
        {
            if (app_ != nullptr)
                app_->endRealtimeDeviceCallback();
        }

        bool wasAdmitted() const noexcept { return app_ != nullptr; }

    private:
        DAW::ApplicationCore* app_ = nullptr;
    };

    static void clearBackendOutput(float* const* outputChannelData,
                                   int numOutputChannels,
                                   int numSamples) noexcept
    {
        if (outputChannelData == nullptr || numSamples <= 0)
            return;

        for (int channel = 0; channel < juce::jmax(0, numOutputChannels); ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }

    static void appendMainStartupTrace(const juce::String& stage)
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DAW_Core");
        dir.createDirectory();

        auto file = dir.getChildFile("startup_trace.log");
        auto line = juce::Time::getCurrentTime().toString(true, true)
            + " [MainComponent] " + stage + "\n";
        file.appendText(line, false, false, "\n");
    }
}

using DAW::AddClipCommand;
using DAW::AddTrackCommand;
using DAW::ClipPropertyChangeCommand;
using DAW::CommandManager;
using DAW::DeleteClipCommand;
using DAW::DeleteTrackCommand;
using DAW::DuplicateClipCommand;
using DAW::ImportAudioFilesCommand;
using DAW::PluginChainStateCommand;
using DAW::ProjectTopologyStateCommand;
using DAW::TrackPropertyChangeCommand;
using DAW::TrackReorderCommand;
using DAW::TrackRole;

namespace
{
    static constexpr bool kUseLegacyDevicePanel = false;
}

class TimelineZoomScrollBar final : public juce::ScrollBar
{
public:
    explicit TimelineZoomScrollBar(bool isVertical) : juce::ScrollBar(isVertical) {}

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.fillAll(juce::Colours::transparentBlack);

        g.setColour(juce::Colour(0xFF17151B));
        g.fillRect(bounds);

        auto thumb = calcThumbBounds().toFloat();
        if (thumb.isEmpty())
            return;

        const bool hovered = isMouseOverOrDragging();
        const auto thumbColour = juce::Colour(0xFF53C7F4).withAlpha(hovered ? 0.95f : 0.88f);
        const auto glowColour = juce::Colour(0xFF8BE7FF).withAlpha(hovered ? 0.20f : 0.10f);

        g.setColour(glowColour);
        g.fillRoundedRectangle(thumb.expanded(isVertical() ? 1.0f : 0.0f, isVertical() ? 0.0f : 1.0f), 4.0f);

        g.setColour(thumbColour);
        g.fillRoundedRectangle(thumb.reduced(1.0f), 4.0f);
    }

    // Filled once at mouseDown, then sent unchanged on every mouseDrag event.
    struct EdgeDragInfo
    {
        bool  isVertical   = false;
        bool  isStartEdge  = false;   // top/left edge; false = bottom/right edge
        int   trackStartPixels  = 0;
        int   trackSizePixels   = 0;
        int   minThumbSizePixels = 0;
        float initialThumbStartPixels = 0.0f;
        float initialThumbSizePixels  = 0.0f;
        float dragStartPosPixels = 0.0f;
        float currentPosPixels   = 0.0f;
        // Stable reference values captured once at mouseDown
        double referencePixelsPerSecond = 100.0;
        double referenceLaneHeight      = 60.0;
        int    referenceViewX           = 0;
        int    referenceViewY           = 0;
        int    referenceContentWidth    = 1;
        int    referenceContentHeight   = 1;
    };

    // Callbacks set by TimelineViewport
    std::function<void(const EdgeDragInfo&)> onThumbEdgeDragged;
    std::function<double()> getPixelsPerSecond;
    std::function<double()> getLaneHeight;
    std::function<juce::Point<int>()> getViewPosition;
    std::function<juce::Point<int>()> getContentSize;
    std::function<int()> getVisibleSpan;

private:
    enum class DragMode { None, ThumbMove, StartEdgeZoom, EndEdgeZoom };
    static constexpr int kEdgeZone = 12;

    // ------------------------------------------------------------------ helpers
    int axisPos(const juce::MouseEvent& e) const noexcept
    {
        return isVertical() ? e.y : e.x;
    }

    int thumbLength(const juce::Rectangle<int>& tb) const noexcept
    {
        return isVertical() ? tb.getHeight() : tb.getWidth();
    }

    int thumbStart(const juce::Rectangle<int>& tb) const noexcept
    {
        return isVertical() ? tb.getY() : tb.getX();
    }

    int thumbEnd(const juce::Rectangle<int>& tb) const noexcept
    {
        return isVertical() ? tb.getBottom() : tb.getRight();
    }

    juce::Rectangle<int> calcThumbBounds()
    {
        const int length = isVertical() ? getHeight() : getWidth();
        auto& laf = getLookAndFeel();
        const int minSz  = laf.getMinimumScrollbarThumbSize(*this);
        const bool hasBtns = laf.areScrollbarButtonsVisible();
        const int btnSz  = hasBtns ? juce::jmin(laf.getScrollbarButtonSize(*this), length / 2) : 0;

        if (length < 32 + minSz) return {};

        const int areaStart = btnSz;
        const int areaSize  = length - 2 * btnSz;
        if (areaSize <= 0) return {};

        const auto contentSize = getContentSize ? getContentSize() : juce::Point<int>{ 1, 1 };
        const auto viewPos = getViewPosition ? getViewPosition() : juce::Point<int>{};
        const int contentSpan = juce::jmax(1, isVertical() ? contentSize.y : contentSize.x);
        const int visibleSpan = juce::jmax(1, getVisibleSpan ? getVisibleSpan() : 1);

        if (contentSpan <= visibleSpan)
        {
            return isVertical() ? juce::Rectangle<int>(0, areaStart, getWidth(), areaSize)
                                : juce::Rectangle<int>(areaStart, 0, areaSize, getHeight());
        }

        int sz = juce::roundToInt(((double)visibleSpan * (double)areaSize) / (double)contentSpan);
        sz = juce::jlimit(juce::jmin(minSz, areaSize - 1), areaSize, sz);

        const int maxScroll = juce::jmax(1, contentSpan - visibleSpan);
        const int scrollPos = juce::jlimit(0, maxScroll, isVertical() ? viewPos.y : viewPos.x);
        int start = areaStart;
        if (areaSize > sz)
            start += juce::roundToInt(((double)scrollPos * (double)(areaSize - sz)) / (double)maxScroll);

        return isVertical() ? juce::Rectangle<int>(0, start, getWidth(), sz)
                            : juce::Rectangle<int>(start, 0, sz, getHeight());
    }

    juce::MouseCursor edgeCursor() const
    {
        return DAW::CursorThemeCore::getStandard(
            isVertical() ? juce::MouseCursor::UpDownResizeCursor
                         : juce::MouseCursor::LeftRightResizeCursor);
    }

    bool isEdgePoint(int pos) const
    {
        auto tb = const_cast<TimelineZoomScrollBar*>(this)->calcThumbBounds();
        if (tb.isEmpty()) return false;
        const int ts = thumbStart(tb), te = thumbEnd(tb);
        const int ez = juce::jmin(kEdgeZone, juce::jmax(4, thumbLength(tb) / 3));
        return pos >= ts && pos <= te && (pos <= ts + ez || pos >= te - ez);
    }

    bool isBodyPoint(int pos) const
    {
        auto tb = const_cast<TimelineZoomScrollBar*>(this)->calcThumbBounds();
        if (tb.isEmpty()) return false;
        const int ts = thumbStart(tb), te = thumbEnd(tb);
        const int ez = juce::jmin(kEdgeZone, juce::jmax(4, thumbLength(tb) / 3));
        return pos >= ts && pos <= te && pos > ts + ez && pos < te - ez;
    }

    void refreshCursor(const juce::MouseEvent& e)
    {
        if (dragMode_ == DragMode::StartEdgeZoom || dragMode_ == DragMode::EndEdgeZoom)
        { setMouseCursor(edgeCursor()); return; }

        const int p = axisPos(e);
        if (isEdgePoint(p))
            setMouseCursor(edgeCursor());
        else if (isBodyPoint(p))
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::DraggingHandCursor));
        else
            setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
    }

    // ---------------------------------------------------------------- overrides
    void mouseMove(const juce::MouseEvent& e) override { refreshCursor(e); juce::ScrollBar::mouseMove(e); }
    void mouseEnter(const juce::MouseEvent& e) override { refreshCursor(e); juce::ScrollBar::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override
    {
        if (dragMode_ == DragMode::None) setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
        juce::ScrollBar::mouseExit(e);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragMode_ = DragMode::None;

        const int pos = axisPos(e);
        const auto tb = calcThumbBounds();
        if (!tb.isEmpty())
        {
            const int ts = thumbStart(tb), te = thumbEnd(tb);
            const int ez = juce::jmin(kEdgeZone, juce::jmax(4, thumbLength(tb) / 3));
            if (pos >= ts && pos <= te)
            {
                if (pos <= ts + ez)       dragMode_ = DragMode::StartEdgeZoom;
                else if (pos >= te - ez)  dragMode_ = DragMode::EndEdgeZoom;
                else                      dragMode_ = DragMode::ThumbMove;
            }
        }

        if (dragMode_ == DragMode::StartEdgeZoom || dragMode_ == DragMode::EndEdgeZoom)
        {
            // Capture everything ONCE here; never re-read during drag
            const auto tb2 = calcThumbBounds();
            cachedThumbStart_ = (float)thumbStart(tb2);
            cachedThumbSize_  = (float)thumbLength(tb2);
            cachedDragStart_  = (float)pos;
            cachedRefPps_     = getPixelsPerSecond ? getPixelsPerSecond() : 100.0;
            cachedRefLaneH_   = getLaneHeight       ? getLaneHeight()      : 60.0;
            const auto vp = getViewPosition ? getViewPosition() : juce::Point<int>{};
            cachedRefViewX_   = vp.x;
            cachedRefViewY_   = vp.y;
            const auto cs = getContentSize ? getContentSize() : juce::Point<int>{1,1};
            cachedRefContentW_ = juce::jmax(1, cs.x);
            cachedRefContentH_ = juce::jmax(1, cs.y);
            setMouseCursor(edgeCursor());
            return;
        }

        juce::ScrollBar::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragMode_ == DragMode::StartEdgeZoom || dragMode_ == DragMode::EndEdgeZoom)
        {
            setMouseCursor(edgeCursor());
            if (onThumbEdgeDragged)
            {
                const int length = isVertical() ? getHeight() : getWidth();
                auto& laf = getLookAndFeel();
                const bool hasBtns = laf.areScrollbarButtonsVisible();
                const int btnSz = hasBtns ? juce::jmin(laf.getScrollbarButtonSize(*this), length / 2) : 0;

                EdgeDragInfo info;
                info.isVertical             = isVertical();
                info.isStartEdge            = (dragMode_ == DragMode::StartEdgeZoom);
                info.trackStartPixels       = btnSz;
                info.trackSizePixels        = length - 2 * btnSz;
                info.minThumbSizePixels     = laf.getMinimumScrollbarThumbSize(*this);
                info.initialThumbStartPixels = cachedThumbStart_;
                info.initialThumbSizePixels  = cachedThumbSize_;
                info.dragStartPosPixels      = cachedDragStart_;
                info.currentPosPixels        = (float)axisPos(e);
                info.referencePixelsPerSecond = cachedRefPps_;
                info.referenceLaneHeight      = cachedRefLaneH_;
                info.referenceViewX           = cachedRefViewX_;
                info.referenceViewY           = cachedRefViewY_;
                info.referenceContentWidth    = cachedRefContentW_;
                info.referenceContentHeight   = cachedRefContentH_;
                onThumbEdgeDragged(info);
            }
            return;
        }
        juce::ScrollBar::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        dragMode_ = DragMode::None;
        refreshCursor(e);
        if (dragMode_ == DragMode::None)
            juce::ScrollBar::mouseUp(e);
    }

    // ------------------------------------------------------------------ state
    DragMode dragMode_      = DragMode::None;
    float cachedThumbStart_ = 0.0f;
    float cachedThumbSize_  = 0.0f;
    float cachedDragStart_  = 0.0f;
    double cachedRefPps_    = 100.0;
    double cachedRefLaneH_  = 60.0;
    int    cachedRefViewX_  = 0;
    int    cachedRefViewY_  = 0;
    int    cachedRefContentW_ = 1;
    int    cachedRefContentH_ = 1;
};

// Small square zoom button docked where the two timeline scrollbars meet
// (pro-DAW style corner zoom controls).
class TimelineCornerZoomButton final : public juce::Button
{
public:
    explicit TimelineCornerZoomButton(bool isPlus)
        : juce::Button(isPlus ? "zoomIn" : "zoomOut"), plus_(isPlus)
    {
        setRepaintsOnMouseActivity(true);
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const auto base = juce::Colour(0xFF17171B);
        g.setColour(down ? base.brighter(0.30f) : over ? base.brighter(0.15f) : base);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(over ? 0.35f : 0.18f));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);

        // Crisp +/- glyph
        g.setColour(juce::Colours::white.withAlpha(over || down ? 0.95f : 0.75f));
        const auto c   = r.getCentre();
        const float arm = juce::jmin(r.getWidth(), r.getHeight()) * 0.26f;
        const float th  = 1.6f;
        g.fillRoundedRectangle(c.x - arm, c.y - th * 0.5f, arm * 2.0f, th, th * 0.5f);
        if (plus_)
            g.fillRoundedRectangle(c.x - th * 0.5f, c.y - arm, th, arm * 2.0f, th * 0.5f);
    }

private:
    bool plus_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineCornerZoomButton)
};

class TimelineViewport final : public juce::Viewport
{
public:
    explicit TimelineViewport(const juce::String& name = {}) : juce::Viewport(name)
    {
        // JUCE enables drag-to-scroll by default for non-hover (touch) input,
        // which made a one-finger drag scroll the timeline instead of ever
        // reaching the clip — so clips could not be moved by touch at all.
        // Navigation is handled explicitly by the arrangement view.
        setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);

        recreateScrollbars();

        for (auto* b : { &hZoomOutBtn_, &hZoomInBtn_, &vZoomOutBtn_, &vZoomInBtn_ })
            addAndMakeVisible(*b);

        hZoomInBtn_.onClick  = [this] { if (onZoomButton) onZoomButton(false, true);  };
        hZoomOutBtn_.onClick = [this] { if (onZoomButton) onZoomButton(false, false); };
        vZoomInBtn_.onClick  = [this] { if (onZoomButton) onZoomButton(true,  true);  };
        vZoomOutBtn_.onClick = [this] { if (onZoomButton) onZoomButton(true,  false); };

        // JUCE's Viewport re-lays-out the scrollbars internally whenever the
        // visible area changes, so listen to their bounds and re-dock the
        // corner buttons + shrink the tracks each time.
        barListener_.owner = this;
        getHorizontalScrollBar().addComponentListener(&barListener_);
        getVerticalScrollBar().addComponentListener(&barListener_);
    }

    ~TimelineViewport() override
    {
        getHorizontalScrollBar().removeComponentListener(&barListener_);
        getVerticalScrollBar().removeComponentListener(&barListener_);
    }

    std::function<void(const TimelineZoomScrollBar::EdgeDragInfo&)> onThumbEdgeDragged;
    std::function<double()> getPixelsPerSecond;
    std::function<double()> getLaneHeight;

    /** (isVertical, zoomIn) — fired by the corner zoom buttons. */
    std::function<void(bool, bool)> onZoomButton;

    void resized() override
    {
        juce::Viewport::resized();
        layoutCornerZoomButtons();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // Vertical wheel scrolls lanes; Shift+wheel scrolls horizontally.
        if (auto* c = getViewedComponent())
        {
            const int contentW = juce::jmax(1, c->getWidth());
            const int contentH = juce::jmax(1, c->getHeight());

            const int viewW = juce::jmax(1, getViewWidth());
            const int viewH = juce::jmax(1, getViewHeight());

            auto pos = getViewPosition();

            const float dx = wheel.deltaX;
            const float dy = wheel.deltaY;

            const bool shift = e.mods.isShiftDown();
            const float primary = shift ? dx : dy;

            // Normalize to pixels; keep it feeling consistent across wheel types.
            const int step = (int)std::lround((float)(shift ? viewW : viewH) * primary * -0.65f);

            if (step != 0)
            {
                if (shift)
                    pos.x = juce::jlimit(0, juce::jmax(0, contentW - viewW), pos.x + step);
                else
                    pos.y = juce::jlimit(0, juce::jmax(0, contentH - viewH), pos.y + step);

                setViewPosition(pos);
            }
        }

        juce::Viewport::mouseWheelMove(e, wheel);
    }

protected:
    juce::ScrollBar* createScrollBarComponent(bool isVertical) override
    {
        auto* bar = new TimelineZoomScrollBar(isVertical);
        bar->onThumbEdgeDragged = [this](const TimelineZoomScrollBar::EdgeDragInfo& info)
        {
            if (onThumbEdgeDragged)
                onThumbEdgeDragged(info);
        };
        bar->getPixelsPerSecond = [this]() -> double
        {
            return getPixelsPerSecond ? getPixelsPerSecond() : 100.0;
        };
        bar->getLaneHeight = [this]() -> double
        {
            return getLaneHeight ? getLaneHeight() : 60.0;
        };
        bar->getViewPosition = [this]() -> juce::Point<int>
        {
            return getViewPosition();
        };
        bar->getContentSize = [this]() -> juce::Point<int>
        {
            if (auto* c = getViewedComponent())
                return { c->getWidth(), c->getHeight() };
            return { 1, 1 };
        };
        bar->getVisibleSpan = [this, isVertical]() -> int
        {
            return isVertical ? getViewHeight() : getViewWidth();
        };
        return bar;
    }

private:
    // ── Corner zoom buttons (bottom-right, where the two scrollbars meet) ──
    struct BarListener final : juce::ComponentListener
    {
        TimelineViewport* owner = nullptr;

        void componentMovedOrResized(juce::Component&, bool, bool) override
        {
            // Re-dock whenever the viewport repositions its scrollbars.
            if (owner != nullptr && !owner->layoutGuard_)
                owner->layoutCornerZoomButtons();
        }
    };

    void layoutCornerZoomButtons()
    {
        if (layoutGuard_)
            return;

        const juce::ScopedValueSetter<bool> svs(layoutGuard_, true);

        auto& hBar = getHorizontalScrollBar();
        auto& vBar = getVerticalScrollBar();

        const bool hVisible = hBar.isVisible();
        const bool vVisible = vBar.isVisible();

        const int th = getScrollBarThickness();
        const int btn = th; // square buttons matching scrollbar thickness

        const bool showButtons = hVisible && vVisible && getWidth() > btn * 6 && getHeight() > btn * 6;

        hZoomInBtn_.setVisible(showButtons);
        hZoomOutBtn_.setVisible(showButtons);
        vZoomInBtn_.setVisible(showButtons);
        vZoomOutBtn_.setVisible(showButtons);

        if (!showButtons)
            return;

        // Steal 2 button widths from the end of the horizontal bar, and
        // 2 button heights from the end of the vertical bar. The four
        // buttons stack into the corner block:
        //   [-][+] on the horizontal axis, [-][+] above the corner vertically.
        auto hb = hBar.getBounds();
        auto vb = vBar.getBounds();

        const int hBtnSpace = btn * 2;
        const int vBtnSpace = btn * 2;

        if (hb.getWidth() > hBtnSpace * 2)
            hBar.setBounds(hb.withWidth(hb.getWidth() - hBtnSpace));

        if (vb.getHeight() > vBtnSpace * 2)
            vBar.setBounds(vb.withHeight(vb.getHeight() - vBtnSpace));

        hb = hBar.getBounds();
        vb = vBar.getBounds();

        // Horizontal zoom pair: docked at the right end of the H bar.
        hZoomOutBtn_.setBounds(hb.getRight(),        hb.getY(), btn, btn);
        hZoomInBtn_.setBounds (hb.getRight() + btn,  hb.getY(), btn, btn);

        // Vertical zoom pair: docked at the bottom end of the V bar.
        vZoomOutBtn_.setBounds(vb.getX(), vb.getBottom(),       btn, btn);
        vZoomInBtn_.setBounds (vb.getX(), vb.getBottom() + btn, btn, btn);

        for (auto* b : { (juce::Component*)&hZoomOutBtn_, (juce::Component*)&hZoomInBtn_,
                         (juce::Component*)&vZoomOutBtn_, (juce::Component*)&vZoomInBtn_ })
            b->toFront(false);
    }

    TimelineCornerZoomButton hZoomInBtn_  { true  };
    TimelineCornerZoomButton hZoomOutBtn_ { false };
    TimelineCornerZoomButton vZoomInBtn_  { true  };
    TimelineCornerZoomButton vZoomOutBtn_ { false };
    BarListener barListener_;
    bool layoutGuard_ = false;
};

class TimelineTrackHeightHandle final : public juce::Component
{
public:
    TimelineTrackHeightHandle()
    {
        setRepaintsOnMouseActivity(true);
    }

    std::function<void(float deltaSteps)> onDragged;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const auto glow = juce::Colour(0xFFFF7ACF).withAlpha(isMouseOverOrDragging() ? 0.28f : 0.14f);
        g.setColour(glow);
        g.fillRoundedRectangle(bounds.expanded(isMouseOverOrDragging() ? 2.0f : 1.0f, 2.0f), 7.0f);

        g.setColour(juce::Colour(0xFF20151E).withAlpha(0.96f));
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(juce::Colour(0xFFFFB4E5).withAlpha(isMouseOverOrDragging() ? 0.92f : 0.72f));
        const float centreX = bounds.getCentreX();
        g.drawLine(centreX, bounds.getY() + 8.0f, centreX, bounds.getBottom() - 8.0f, 1.6f);
        g.fillEllipse(centreX - 2.2f, bounds.getCentreY() - 2.2f, 4.4f, 4.4f);
    }

    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor)); }
    void mouseExit(const juce::MouseEvent&) override  { if (!dragging_) setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor)); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = true;
        lastScreenY_ = e.getScreenPosition().y;
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor));
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int currentScreenY = e.getScreenPosition().y;
        const int deltaY = currentScreenY - lastScreenY_;
        lastScreenY_ = currentScreenY;

        if (onDragged != nullptr && deltaY != 0)
            onDragged((float)-deltaY);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragging_ = false;
        setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor));
    }

private:
    bool dragging_ = false;
    int lastScreenY_ = 0;
};

namespace
{
    constexpr int kFolderDropModeAsk = 0;
    constexpr int kFolderDropModeAlwaysCreateFolderBus = 1;
    constexpr int kFolderDropModeAlwaysConvertDestinationTrack = 2;

    float sequenceValueToAutomationValue(const juce::String& parameterId, float normalisedValue) noexcept
    {
        const float norm = juce::jlimit(0.0f, 1.0f, normalisedValue);
        if (parameterId == DAW::AutomationLaneCore::trackPanParameterId)
            return norm * 2.0f - 1.0f;

        if (parameterId == DAW::AutomationLaneCore::trackTapeStopParameterId)
            return norm;

        if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipPitchSuffix))
            return norm * 72.0f - 36.0f;

        if (parameterId.startsWith("clip.") && parameterId.endsWith(DAW::AutomationLaneCore::clipStretchSuffix))
            return 0.1f + norm * 3.9f;

        return norm;
    }

    class LiveColourSelector final : public juce::ColourSelector,
                                     private juce::ChangeListener
    {
    public:
        LiveColourSelector()
            : juce::ColourSelector(juce::ColourSelector::showAlphaChannel
                                 | juce::ColourSelector::showColourAtTop
                                 | juce::ColourSelector::showSliders
                                 | juce::ColourSelector::showColourspace)
        {
            addChangeListener(this);
        }

        ~LiveColourSelector() override
        {
            removeChangeListener(this);
        }

        std::function<void(juce::Colour)> onColourChanged;

    private:
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            if (onColourChanged)
                onColourChanged(getCurrentColour());
        }
    };

    juce::Colour parseStoredColour(const juce::var& value, juce::Colour fallback)
    {
        auto text = value.toString().trim();
        if (text.isEmpty())
            return fallback;

        auto parsed = static_cast<uint32_t>(text.getHexValue64());
        return parsed == 0 ? fallback : juce::Colour(parsed);
    }

    class ClipRegionPluginWindow final : public juce::DocumentWindow
    {
    public:
        ClipRegionPluginWindow(const juce::String& title, DAW::TransportController& transport,
                               juce::Component* ownerComponent)
            : juce::DocumentWindow(title,
                                   juce::Colour(0xFF202020),
                                   juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton,
                                   true),
              transport_(transport),
              ownerComponent_(ownerComponent)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, false);
            setResizeLimits(240, 160, 3200, 1800);
            setDropShadowEnabled(true);
        }

        std::function<void()> onCloseRequested;

        void closeButtonPressed() override
        {
            closeRequestedByUser_ = true;
            if (onCloseRequested) onCloseRequested();
        }

        void minimiseButtonPressed() override
        {
            minimiseRequestedByUser_ = true;
            setVisible(false);
            if (onMinimizeRequested) onMinimizeRequested();
        }

        void visibilityChanged() override
        {
            juce::DocumentWindow::visibilityChanged();

            if (isVisible())
            {
                // Shown (initial open or restored from minimize): clear intent
                // flags so the re-show guard below covers the full lifetime.
                closeRequestedByUser_    = false;
                minimiseRequestedByUser_ = false;
                DAW::patchPluginEditorWindowExStyle(this, ownerComponent_.getComponent());
            }
            else if (!closeRequestedByUser_ && !minimiseRequestedByUser_)
            {
                // Hidden by something other than the user (e.g. Windows hiding
                // an owned window). Force it back — but only while the DAW is
                // the foreground process so we never fight an OS-level minimize
                // or alt-tab away from the app.
                juce::Component::SafePointer<ClipRegionPluginWindow> safeWin(this);
                juce::MessageManager::callAsync([safeWin]
                {
                    if (safeWin != nullptr && !safeWin->isVisible()
                        && !safeWin->closeRequestedByUser_
                        && !safeWin->minimiseRequestedByUser_
                        && juce::Process::isForegroundProcess())
                    {
                        safeWin->setVisible(true);
                        safeWin->toFront(false);
                    }
                });
            }
        }

        void addToDesktop(int styleFlags, void* nativeParent) override
        {
            juce::DocumentWindow::addToDesktop(styleFlags, nativeParent);
            DAW::patchPluginEditorWindowExStyle(this, ownerComponent_.getComponent());
        }

        int getDesktopWindowStyleFlags() const override
        {
            // Do NOT include windowIsTemporary - that flag tells Windows to
            // auto-close the window whenever the parent window is focused,
            // which is exactly what causes plugin windows to vanish unexpectedly.
            return (DocumentWindow::getDesktopWindowStyleFlags()
                    & ~juce::ComponentPeer::windowIsTemporary)
                & ~juce::ComponentPeer::windowAppearsOnTaskbar;
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::spaceKey)
            {
                // Route through the single canonical action so behavior
                // is identical to the main component spacebar handler.
                DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TransportPlayStop);
                return true;
            }

            return juce::DocumentWindow::keyPressed(key);
        }

        void maximiseButtonPressed() override
        {
            if (!maximized_)
            {
                restoreBounds_ = getBounds();
                auto displays = juce::Desktop::getInstance().getDisplays();
                auto* display = displays.getDisplayForPoint(getBounds().getCentre());
                if (display == nullptr)
                    display = displays.getPrimaryDisplay();

                if (display != nullptr)
                    setBounds(display->userArea);

                maximized_ = true;
            }
            else
            {
                if (!restoreBounds_.isEmpty())
                    setBounds(restoreBounds_);

                maximized_ = false;
            }
        }

        std::function<void()> onMinimizeRequested;

    private:
        DAW::TransportController& transport_;
        juce::Component::SafePointer<juce::Component> ownerComponent_;
        juce::Rectangle<int> restoreBounds_;
        bool maximized_ = false;
        bool closeRequestedByUser_    = false;
        bool minimiseRequestedByUser_ = false;
    };
}

//==============================================================================
void MainComponent::syncTrackSelectionVisuals()
{
    std::vector<DAW::TrackID> ids;
    for (const auto& target : multiSelection_.getSelected(DAW::SelectionKind::Track))
        if (target.trackId.isNotEmpty())
            ids.push_back(target.trackId);

    const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
    const DAW::TrackID primaryId = primary.isValid() ? primary.trackId : DAW::TrackID{};

    if (trackList_)
        trackList_->applyMultiSelectionVisual(ids);
    if (mixerPanel_)
        mixerPanel_->setTrackSelectionVisual(ids, primaryId);
}

std::vector<DAW::TrackID> MainComponent::getSelectedTrackIdsInProjectOrder() const
{
    std::vector<DAW::TrackID> ids;
    const auto& selection = multiSelection_.getSelected(DAW::SelectionKind::Track);
    const auto& tm = appCore_.getTrackManager();
    ids.reserve(selection.size());

    for (auto* track : tm.getAllTracks())
    {
        if (track == nullptr)
            continue;

        const auto id = track->getID();
        if (std::find_if(selection.begin(), selection.end(),
                         [&id](const DAW::SelectionTarget& target)
                         {
                             return target.trackId == id;
                         }) != selection.end())
            ids.push_back(id);
    }

    if (tm.hasMasterTrack())
    {
        if (auto* master = tm.getMasterTrack())
        {
            const auto id = master->getID();
            if (std::find_if(selection.begin(), selection.end(),
                             [&id](const DAW::SelectionTarget& target)
                             {
                                 return target.trackId == id;
                             }) != selection.end())
                ids.push_back(id);
        }
    }

    return ids;
}

void MainComponent::handleTrackSelection(const DAW::TrackID& id,
                                         const juce::ModifierKeys& mods)
{
    if (id.isEmpty())
        return;

    const auto target = DAW::SelectionTarget::track(id);
    if (mods.isPopupMenu())
    {
        // Context-clicking a selected member changes the primary target but
        // never collapses the canonical selected set.
        multiSelection_.selectContext(target);
    }
    else
    {
        const bool ctrl  = mods.isCtrlDown() || mods.isCommandDown();
        const bool shift = mods.isShiftDown();
        std::vector<DAW::SelectionTarget> orderedTargets;
        if (trackList_)
        {
            const auto ordered = trackList_->getVisibleTrackIds();
            orderedTargets.reserve(ordered.size());
            for (const auto& orderedId : ordered)
                orderedTargets.push_back(DAW::SelectionTarget::track(orderedId));
        }

        if (ctrl && shift)
            multiSelection_.selectRange(target, orderedTargets, true);
        else if (shift)
            multiSelection_.selectRange(target, orderedTargets, false);
        else if (ctrl)
            multiSelection_.toggle(target);
        else
            multiSelection_.selectSingle(target);
    }

    const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
    const DAW::TrackID primaryId = primary.isValid() ? primary.trackId : DAW::TrackID{};
    appCore_.getState().selectedTrackID.setValue(primaryId);
    syncTrackSelectionVisuals();

    auto& bgV2 = appCore_.getBubblegumV2();
    syncBubblegumSourceSelection(primaryId);

    if (pluginSidePanel_)
    {
        auto* track = primaryId.isNotEmpty()
            ? appCore_.getTrackManager().getTrack(primaryId) : nullptr;
        auto* chain = track ? appCore_.getPluginChain(primaryId) : nullptr;
        if (track) pluginSidePanel_->setTrack(track, chain);
        else       pluginSidePanel_->clearTrack();
    }

    if (bgV2.isActive())
    {
        if (mixerPanel_)
            mixerPanel_->repaint();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible())
            bubblegumPanel_->refresh();
        refreshBubblegumFeedback();
    }
    refreshBubblegumOffscreenState();
    cableOverlay_.notifyMotion();
    cableOverlay_.repaint();

    updateQuickSendUI();
}

void MainComponent::syncBubblegumSourceSelection(const DAW::TrackID& trackId)
{
    if (quickSendMode_.isActive() || trackId.isEmpty())
        return;

    // Legacy cable presentation refreshes through the timer/polling path.
    appCore_.getBubblegumV2().onTrackSelected(trackId);
    cableOverlay_.notifyMotion();
}

MainComponent::MainComponent()
{
    appendMainStartupTrace("ctor.begin");
    // Global spacebar -> transport authority. Installed here (message thread)
    // so space toggles play/stop no matter which window has focus — including
    // native plugin editor HWNDs whose keys never reach JUCE's keyPressed
    // dispatch. Routes through KeyBindingManager, same as the timeline.
    DAW::GlobalTransportSpaceHook::install();
    addKeyListener(this);  // Mixer selection + Arrange/Timeline zoom policy

    // Install VEH crash guard EARLY — before any plugin operations.
    // This catches access violations from misbehaving plugins at the OS level.
    DAW::PluginCrashGuardCore::install();

    DAW::CommandManager::getInstance();
    setSize(1280, 800);

    // Global tooltip window — auto-registers with desktop for global mouse
    // tracking and must be in the component tree for tooltip painting.
    addAndMakeVisible(tooltipWindow_);

    appCore_.initialize();
    appendMainStartupTrace("ctor.after-appCore-initialize");

    // ?? Wire autosave status callbacks into UI ????????????????????????????????
    appCore_.getAutosaveManager().onAutosaveSucceeded = [this](const juce::String& timeStr)
    {
        // Message thread - safe to update UI state
        autosaveStatusText_  = "Autosaved " + timeStr;
        // If this was a recovered project, the autosave is a new write - banner stays
        // until user does Save As. We don't clear recoveryBannerVisible_ here.
        if (transportBar_) transportBar_->repaint();
        repaint();
    };

    appCore_.getAutosaveManager().onAutosaveFailed = [this](const juce::String& /*reason*/)
    {
        auto now = juce::Time::getCurrentTime();
        if ((now - lastAutosaveFailToastTime_).inSeconds() >= kAutosaveFailToastIntervalSec)
        {
            lastAutosaveFailToastTime_ = now;
            DBG("[Autosave] FAILED - check disk permissions");
            autosaveStatusText_ = "Autosave FAILED " + now.formatted("%I:%M %p") + " - check disk";
            repaint();
        }
    };

    // Subscribe to routing graph changes for real-time Bubblegum updates
    appCore_.getRoutingGraph().addListener(this);
    appendMainStartupTrace("ctor.after-routing-listener");

    // Subscribe to project manager for save/load events
    appCore_.getProjectManager().addListener(this);
    appendMainStartupTrace("ctor.after-project-listener");

    // Unified undo history: refresh menu/toolbar state on every change and
    // block undo/redo while a take is rolling (a structural undo mid-take
    // would corrupt the recording - same protection as the major DAWs).
    DAW::CommandManager::getInstance().addListener(this);
    // Undo/redo protection: only block while the transport is actively in
    // record mode (a take is rolling).  We intentionally do NOT check
    // RecordingEngine::isRecording() here because it includes the
    // finalizing_ flag which stays true AFTER the user stops the transport
    // until the async clip-creation callback completes.  Blocking undo
    // during that window causes Bug #2: the user presses Undo after
    // stopping, the undo is silently swallowed, and the just-recorded
    // clips remain live — triggering a false overwrite warning on the
    // next record and ghost audio on play.
    DAW::CommandManager::getInstance().setUndoRedoBlockedQuery([this]
    {
        return appCore_.getTransport().isRecording();
    });
    appendMainStartupTrace("ctor.after-command-listener");

    // Subscribe to track manager for real-time Bubblegum/offscreen refresh
    appCore_.getTrackManager().addListener(this);
    appendMainStartupTrace("ctor.after-track-listener");

    // Subscribe to transport position changes so the timeline content extent
    // can grow when the playhead moves past the current material (scrub/loop).
    appCore_.getTransport().addListener(this);
    appendMainStartupTrace("ctor.after-transport-listener");

    // Ensure master track exists before any UI is built
    appCore_.getTrackManager().createMasterTrack();
    appendMainStartupTrace("ctor.after-master-track");

    inputTrimPanelManager_ = std::make_unique<DAW::InputTrimPanelManager>(appCore_.getTrackManager(), this);

    quitSafetyDialog_ = std::make_unique<DAW::QuitSafetyDialog>();
    quitSafetyDialog_->setVisible(false);
    quitSafetyDialog_->callbacks.onCancel = [this]
    {
        quitSafetyDialogOpen_ = false;
        if (quitSafetyDialog_) quitSafetyDialog_->setVisible(false);
        repaint();
    };
    quitSafetyDialog_->callbacks.onQuitWithoutSaving = [this]
    {
        quitSafetyDialogOpen_ = false;
        if (quitSafetyDialog_) quitSafetyDialog_->setVisible(false);
        juce::JUCEApplication::getInstance()->quit();
    };
    quitSafetyDialog_->callbacks.onSaveAndQuit = [this]
    {
        // Match pro DAWs: Save triggers Save-As if needed, and only quits
        // after the save operation succeeds.
        auto& pm = appCore_.getProjectManager();

        if (pm.getProjectFile() != juce::File()
            && saveProjectToFile(pm.getProjectFile()))
        {
            quitSafetyDialogOpen_ = false;
            if (quitSafetyDialog_) quitSafetyDialog_->setVisible(false);
            juce::JUCEApplication::getInstance()->quit();
            return;
        }

        // No file yet -> open Save As UI and keep DAW running.
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectSaveAs);
    };
    addChildComponent(quitSafetyDialog_.get());

    // Add several demo tracks
    appCore_.getTrackManager().createTrack("Drums");
    appCore_.getTrackManager().createTrack("Bass");
    appCore_.getTrackManager().createTrack("Keys");

    // Menu bar
    menuBar_ = std::make_unique<DAW::DAWMenuBar>();
    menuBar_->setMenuAction("Track", "Add Audio Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackAddAudio);
    });
    menuBar_->setMenuAction("Track", "Add MIDI Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackAddMIDI);
    });
    menuBar_->setMenuAction("Track", "Duplicate Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackDuplicate);
    });
    menuBar_->setMenuAction("Track", "Delete Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackDeleteSelected);
    });
    menuBar_->setMenuAction("Track", "Arm Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackArmSelected);
    });
    menuBar_->setMenuAction("Track", "Monitor Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackMuteSelected); // Placeholder, should be monitor
    });
    menuBar_->setMenuAction("Track", "Mute Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackMuteSelected);
    });
    menuBar_->setMenuAction("Track", "Solo Track", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackSoloSelected);
    });
    menuBar_->setMenuAction("Track", "Track Color", [this]
    {
        // Placeholder for color picker
    });
    menuBar_->setMenuAction("Track", "Track Rename", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackRenameSelected);
    });
    menuBar_->setMenuAction("View", "Mixer", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ViewToggleMixer);
    });
    menuBar_->setMenuAction("View", "Piano Roll", [this]
    {
        auto selectedId = appCore_.getState().selectedClipID.getValue().toString();
        if (selectedId.isNotEmpty())
        {
            if (auto* clip = appCore_.getClipManager().getClip(selectedId))
                if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
                    openPianoRollForClip(*midiClip);
        }
    });
    menuBar_->setMenuAction("View", "Track List", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ViewToggleTrackList);
    });
    menuBar_->setMenuAction("View", "Full Screen", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ViewToggleFullScreen);
    });
    menuBar_->setMenuAction("Edit", "Undo", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditUndo);
    });
    menuBar_->setMenuAction("Edit", "Redo", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditRedo);
    });
    menuBar_->setMenuAction("Edit", "Duplicate", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ClipDuplicate);
    });
    menuBar_->setMenuAction("Edit", "Delete", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ClipDelete);
    });
    menuBar_->setMenuAction("Edit", "Select All", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::EditSelectAll);
    });
    menuBar_->setMenuAction("Clip", "Vocal Tune", [this]
    {
        openVocalTuneForSelectedClip();
    });
    menuBar_->setMenuAction("Edit", "History...", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ViewToggleHistory);
    });
    menuBar_->setMenuAction("File", "New Project", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectNew);
    });
    menuBar_->setMenuAction("File", "Open Project", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectOpen);
    });
    menuBar_->setMenuAction("File", "Save Project", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectSave);
    });
    menuBar_->setMenuAction("File", "Save As...", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectSaveAs);
    });
    menuBar_->setMenuAction("File", "Import Audio...", []
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ImportFiles);
    });
    menuBar_->setMenuAction("File", "Export Audio / Stems...", [this]
    {
        showExportAudioDialog();
    });

    // Settings menu wiring
    menuBar_->setMenuAction("Settings", "Audio Device...", [this]
    {
        showAudioDevicePanel();
    });
    menuBar_->setMenuAction("Settings", "Control Room / Monitor...", [this]
    {
        bool vis = !controlRoomPanel_->isVisible();
        controlRoomPanel_->setVisible(vis);
        if (vis) { controlRoomPanel_->setBounds(getWidth()/2-180, 100, 360, 480); controlRoomPanel_->toFront(true); }
    });
    menuBar_->setMenuAction("Settings", "Interface: Standard", []
    {
        DAW::Theme::getInstance().depthMode = DAW::Theme::InterfaceDepthMode::Standard;
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::None); // trigger repaint
        if (auto* mc = dynamic_cast<juce::Component*>(
                juce::Desktop::getInstance().getComponent(0)))
            mc->repaint();
    });
    menuBar_->setMenuAction("Settings", "Interface: Immersive 3D", []
    {
        DAW::Theme::getInstance().depthMode = DAW::Theme::InterfaceDepthMode::Immersive3D;
        if (auto* mc = dynamic_cast<juce::Component*>(
                juce::Desktop::getInstance().getComponent(0)))
            mc->repaint();
    });
    menuBar_->setMenuAction("Settings", "Preferences...", [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ViewToggleSettings);
    });
    menuBar_->setMenuAction("Settings", "Upgrade Project...", [this]
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select the project file to upgrade",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.dawproj");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                        self->upgradeProjectFile(file);
                }
            });
    });
    menuBar_->setMenuAction("Settings", "Forensic Audit Log...", [this]
    {
        if (!forensicWindow_)
            forensicWindow_ = std::make_unique<DAW::ForensicAuditWindow>();
        forensicWindow_->setVisible(true);
        forensicWindow_->toFront(true);
    });

    // Assign ActionIDs for shortcut hints in menus
    menuBar_->setMenuItemActionID("File",  "New Project",       DAW::ActionID::ProjectNew);
    menuBar_->setMenuItemActionID("File",  "Open Project",      DAW::ActionID::ProjectOpen);
    menuBar_->setMenuItemActionID("File",  "Save Project",      DAW::ActionID::ProjectSave);
    menuBar_->setMenuItemActionID("File",  "Save As...",        DAW::ActionID::ProjectSaveAs);
    menuBar_->setMenuItemActionID("File",  "Import Audio...",   DAW::ActionID::ImportFiles);
    menuBar_->setMenuItemActionID("Edit",  "Undo",              DAW::ActionID::EditUndo);
    menuBar_->setMenuItemActionID("Edit",  "Redo",              DAW::ActionID::EditRedo);
    menuBar_->setMenuItemActionID("Edit",  "Select All",        DAW::ActionID::EditSelectAll);
    menuBar_->setMenuItemActionID("Edit",  "History...",        DAW::ActionID::ViewToggleHistory);
    menuBar_->setMenuItemActionID("Edit",  "Duplicate",         DAW::ActionID::ClipDuplicate);
    menuBar_->setMenuItemActionID("View",  "Mixer",             DAW::ActionID::ViewToggleMixer);
    menuBar_->setMenuItemActionID("View",  "Track List",        DAW::ActionID::ViewToggleTrackList);
    menuBar_->setMenuItemActionID("View",  "Full Screen",       DAW::ActionID::ViewToggleFullScreen);
    menuBar_->setMenuItemActionID("Track", "Add Audio Track",   DAW::ActionID::TrackAddAudio);
    menuBar_->setMenuItemActionID("Track", "Add MIDI Track",    DAW::ActionID::TrackAddMIDI);
    menuBar_->setMenuItemActionID("Track", "Add MIDI Track",    DAW::ActionID::TrackAddMIDI);
    menuBar_->setMenuItemActionID("Track", "Duplicate Track",   DAW::ActionID::TrackDuplicate);
    menuBar_->setMenuItemActionID("Track", "Delete Track",      DAW::ActionID::TrackDeleteSelected);
    menuBar_->setMenuItemActionID("Track", "Arm Track",         DAW::ActionID::TrackArmSelected);
    menuBar_->setMenuItemActionID("Track", "Mute Track",        DAW::ActionID::TrackMuteSelected);
    menuBar_->setMenuItemActionID("Track", "Solo Track",        DAW::ActionID::TrackSoloSelected);
    addAndMakeVisible(menuBar_.get());

    // Transport bar
    // Use the single authoritative click state from ApplicationCore.
    clickState_ = &appCore_.getClickState();
    transportBar_ = std::make_unique<DAW::TransportBar>(appCore_.getTransport(), clickState_);
    addAndMakeVisible(transportBar_.get());

    // Playhead clock: convert transport sample position to seconds at the
    // current device/project sample rate.
    transportBar_->sampleRateProvider = [this]() { return appCore_.getCurrentSampleRate(); };

    // Project Key: persist through ApplicationState (serialized with the project).
    transportBar_->onProjectKeyChanged = [this](const juce::String& newKey)
    {
        appCore_.getState().projectKey.setValue(newKey);
        appCore_.markProjectDirty("Project key changed");
    };
    transportBar_->setProjectKeyText(appCore_.getState().projectKey.getValue().toString());

    // Tempo changes must update MIDI clip timing so clips follow the musical grid.
    appCore_.getState().onTempoChanged = [this](double bpm)
    {
        const double sr = appCore_.getCurrentSampleRate();
        auto& cm = appCore_.getClipManager();
        for (auto* clip : cm.getAllClips())
        {
            if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
            {
                midiClip->setSampleRate(sr);
                midiClip->setTempo(bpm);
                midiClip->syncLengthFromModel();
                midiClip->notifyStateRestored();
            }
        }

        if (arrangement_)
        {
            arrangement_->requestClipRemirror();
        }
    };

    // Track list (headers on left)
    trackList_ = std::make_unique<DAW::TrackList>(appCore_.getTrackManager());
    trackList_->onWidthDrag = [this](int deltaX)
    {
        trackListW_ = juce::jlimit(kTrackListMin, kTrackListMax, trackListW_ + deltaX);
        resized();
    };
    trackList_->onRowHeightChanged = [this](int index, int newHeight)
    {
        const int masterOffset = appCore_.getTrackManager().hasMasterTrack() ? 1 : 0;
        arrangement_->setLaneHeight(index + masterOffset, newHeight);
    };
    trackList_->onTrackSelected = [this](const DAW::TrackID& id)
    {
        handleTrackSelection(id, {});
    };

    trackList_->onTrackSelectedWithModifiers = [this](const DAW::TrackID& id, const juce::ModifierKeys& mods)
    {
        handleTrackSelection(id, mods);
    };
    trackList_->onOpenPianoRollRequested = [this](const DAW::TrackID& id)
    {
        for (auto* clip : appCore_.getClipManager().getClipsOnTrack(id))
        {
            if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
            {
                openPianoRollForClip(*midiClip);
                return;
            }
        }
    };
    trackList_->onOpenInputTrimRequested = [this](const DAW::TrackID& id)
    {
        if (inputTrimPanelManager_)
            if (auto* track = appCore_.getTrackManager().getTrack(id))
            {
                auto* panel = inputTrimPanelManager_->showForTrack(*track);
                if (panel && bubbleTaskbar_)
                {
                    auto windowId = "inputtrim_" + id;
                    auto trackLabel = track->getName();
                    
                    panel->onMinimizedExternal = [this, windowId, trackLabel, id]()
                    {
                        if (bubbleTaskbar_)
                        {
                            bubbleTaskbar_->registerWindow(windowId, "Input Trim", "Panels",
                                [this, id]()
                                {
                                    if (inputTrimPanelManager_)
                                        if (auto* t = appCore_.getTrackManager().getTrack(id))
                                            if (auto* p = inputTrimPanelManager_->showForTrack(*t))
                                                p->restoreFromTaskbar();
                                    if (bubbleTaskbar_)
                                        bubbleTaskbar_->setWindowMinimized("inputtrim_" + id, false);
                                },
                                [this, id]()
                                {
                                    if (inputTrimPanelManager_)
                                        inputTrimPanelManager_->closeForTrack(id);
                                    if (bubbleTaskbar_)
                                        bubbleTaskbar_->unregisterWindow("inputtrim_" + id);
                                },
                                id, "Input Trim", "Track: " + trackLabel);
                            bubbleTaskbar_->setWindowMinimized(windowId, true);
                        }
                    };
                    
                    panel->onClosedExternal = [this, windowId]()
                    {
                        if (bubbleTaskbar_)
                            bubbleTaskbar_->unregisterWindow(windowId);
                    };
                }
            }
    };
    trackList_->onGetHardwareInputNamesRequested = [this]() -> juce::StringArray
    {
        juce::StringArray names;
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            const auto allNames  = device->getInputChannelNames();
            const auto activeIns = device->getActiveInputChannels();
            for (int ch = 0; ch < allNames.size(); ++ch)
                if (activeIns[ch])
                    names.add(allNames[ch]);
        }
        return names;
    };
    trackList_->onSetColorRequested = [this](const DAW::TrackID& id, juce::Colour colour)
    {
        auto& tm = appCore_.getTrackManager();
        if (multiSelection_.hasMultiple(DAW::SelectionKind::Track)
            && multiSelection_.contains(DAW::SelectionTarget::track(id)))
        {
            DAW::SelectionBulkActionCore::applyColorToSelectedTracks(multiSelection_, tm, colour);
        }
        else
        {
            if (auto* track = tm.getTrack(id))
                track->setColor(colour);
        }
        if (mixerPanel_) mixerPanel_->repaint();
        if (trackList_) trackList_->repaint();
    };

    // Multi-selection query / bulk action callbacks for track row context menu
    trackList_->onGetIsMultiSelectedCallback = [this](const DAW::TrackID& id) -> bool
    {
        return multiSelection_.hasMultiple(DAW::SelectionKind::Track)
            && multiSelection_.contains(DAW::SelectionTarget::track(id));
    };

    trackList_->onMultiRenameCallback = [this](const DAW::TrackID& /*id*/)
    {
        // Show a floating text input asking for the base name, then apply numbered rename
        auto* alertWin = new juce::AlertWindow("Rename Selected Tracks",
            "Enter base name. Tracks will be numbered: Name 1, Name 2, ...",
            juce::MessageBoxIconType::QuestionIcon);
        alertWin->addTextEditor("name", "", "Base name:");
        alertWin->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alertWin->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<MainComponent> safeThis(this);
        alertWin->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, alertWin](int result)
            {
                if (result == 1 && safeThis)
                {
                    auto baseName = alertWin->getTextEditorContents("name").trim();
                    if (baseName.isNotEmpty())
                    {
                        auto& tm = safeThis->appCore_.getTrackManager();
                        DAW::SelectionBulkActionCore::applyNumberedRenameToSelectedTracks(
                            safeThis->multiSelection_, tm, baseName);
                    }
                }
                delete alertWin;
            }), true);
    };

    trackList_->onMultiDeleteCallback = [this](const DAW::TrackID& /*id*/)
    {
        auto& tm = appCore_.getTrackManager();
        std::vector<DAW::TrackID> toDelete;
        for (const auto& id : getSelectedTrackIdsInProjectOrder())
            if (tm.canDeleteTrack(id))
                toDelete.push_back(id);
        if (toDelete.empty()) return;

        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Selected Tracks")
            .withMessage("Delete " + juce::String(toDelete.size()) + " selected tracks?")
            .withButton("Delete").withButton("Cancel").withAssociatedComponent(this);
        juce::AlertWindow::showAsync(options, [safeThis, toDelete](int result)
        {
            if (result != 1 || !safeThis) return;
            safeThis->deleteTracksAsOneTopologyTransaction(toDelete);
        });
    };
    trackList_->onMultiMuteCallback = [this](const DAW::TrackID& id)
    {
        auto& tm = appCore_.getTrackManager();
        bool newMuted = true;
        if (auto* tr = tm.getTrack(id)) newMuted = !tr->isMuted();
        DAW::SelectionBulkActionCore::applyMuteToSelectedTracks(multiSelection_, tm, newMuted);
    };

    trackList_->onMultiSoloCallback = [this](const DAW::TrackID& id)
    {
        auto& tm = appCore_.getTrackManager();
        bool newSoloed = true;
        if (auto* tr = tm.getTrack(id)) newSoloed = !tr->isSoloed();
        DAW::SelectionBulkActionCore::applySoloToSelectedTracks(multiSelection_, tm, newSoloed);
    };

    trackList_->onMultiArmCallback = [this](const DAW::TrackID& id)
    {
        auto& tm = appCore_.getTrackManager();
        bool newArmed = true;
        if (auto* tr = tm.getTrack(id)) newArmed = !tr->isArmed();
        DAW::SelectionBulkActionCore::applyArmToSelectedTracks(multiSelection_, tm, newArmed);
    };

    trackList_->onMultiColorCallback = [this](const DAW::TrackID& id)
    {
        auto& tm = appCore_.getTrackManager();
        auto* primaryTrack = tm.getTrack(id);
        if (!primaryTrack) return;
        juce::Component::SafePointer<MainComponent> safeThis(this);
        DAW::TrackColorPalette::showWithCallback(*primaryTrack, *trackList_,
            trackList_->getScreenBounds().withWidth(8),
            [safeThis](juce::Colour colour)
            {
                if (!safeThis) return;
                DAW::SelectionBulkActionCore::applyColorToSelectedTracks(
                    safeThis->multiSelection_, safeThis->appCore_.getTrackManager(), colour);
                if (safeThis->mixerPanel_) safeThis->mixerPanel_->repaint();
                if (safeThis->trackList_)  safeThis->trackList_->repaint();
            });
    };
    trackList_->onDeleteTrackRequested = [this](const DAW::TrackID& id)
    {
        deleteTracksAsOneTopologyTransaction({ id });
    };
    trackList_->onDuplicateTrackRequested = [this](const DAW::TrackID& id)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::AddTrackCommand>(
                appCore_.getTrackManager(), track->getName() + " (copy)", track->getRole()));
    };
    trackList_->onToggleMuteRequested = [this](const DAW::TrackID& id)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Muted,
                track->isMuted(), !track->isMuted(), "Toggle Track Mute"));
    };
    trackList_->onToggleSoloRequested = [this](const DAW::TrackID& id)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Soloed,
                track->isSoloed(), !track->isSoloed(), "Toggle Track Solo"));
    };
    trackList_->onToggleArmRequested = [this](const DAW::TrackID& id)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Armed,
                track->isArmed(), !track->isArmed(), "Toggle Track Arm"));
    };
    trackList_->onSetMonitoringRequested = [this](const DAW::TrackID& id, bool enabled)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Monitoring,
                track->isMonitoring(), enabled, enabled ? "Enable Monitoring" : "Disable Monitoring"));
    };
    trackList_->onRenameTrackRequested = [this](const DAW::TrackID& id, const juce::String& name)
    {
        auto& tm = appCore_.getTrackManager();
        if (auto* track = tm.getTrack(id))
        {
            auto trimmed = name.trim();
            if (trimmed.isNotEmpty() && trimmed != track->getName())
                DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                    tm, id, DAW::TrackPropertyChangeCommand::Property::Name,
                    track->getName(), trimmed, "Rename Track"));
        }
    };
    trackList_->onAutomationMuteChanged = [this](const DAW::TrackID& id, bool muted)
    {
        auto& mgr = appCore_.getAutomationManager();
        bool changed = false;
        for (const auto& lane : mgr.getLanes())
        {
            if (lane.trackId == id)
            {
                mgr.setLaneEnabled(id, lane.parameterId, !muted);
                changed = true;
            }
        }

        if (!changed)
            mgr.setLaneEnabled(id, DAW::AutomationLaneCore::trackVolumeParameterId, !muted);

        if (arrangement_)
            arrangement_->repaint();
    };
    trackList_->onAutomationTargetMuteChanged = [this](const DAW::TrackID& id, const juce::String& paramId, bool muted)
    {
        appCore_.getAutomationManager().setLaneEnabled(id, paramId, !muted);
        if (arrangement_)
            arrangement_->repaint();
    };
    trackList_->onAutomationTargetSoloRequested = [this](const DAW::TrackID& id, const juce::String& paramId)
    {
        auto& mgr = appCore_.getAutomationManager();
        juce::StringArray parameterIds;
        for (const auto& lane : mgr.getLanes())
            if (lane.trackId == id && !parameterIds.contains(lane.parameterId))
                parameterIds.add(lane.parameterId);

        if (!parameterIds.contains(paramId))
            parameterIds.add(paramId);

        for (const auto& parameterId : parameterIds)
            mgr.setLaneEnabled(id, parameterId, parameterId == paramId);

        if (auto* track = appCore_.getTrackManager().getTrack(id))
            track->setAutomationMuted(false);

        if (arrangement_)
            arrangement_->repaint();
    };
    trackList_->onAutomationClearRequested = [this](const DAW::TrackID& id)
    {
        auto& mgr = appCore_.getAutomationManager();
        juce::StringArray parameterIds;
        for (const auto& lane : mgr.getLanes())
            if (lane.trackId == id && !parameterIds.contains(lane.parameterId))
                parameterIds.add(lane.parameterId);

        if (parameterIds.isEmpty())
            parameterIds.add(DAW::AutomationLaneCore::trackVolumeParameterId);

        for (const auto& parameterId : parameterIds)
            mgr.clearLane(id, parameterId);

        auto& apexRegistry = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto& apexLanes = apex::automation::AutomationLaneStore::getInstance();
        apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(id)));
        apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackPanKey(id)));
        apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackMuteKey(id)));
        apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackSoloKey(id)));
        apexLanes.removeLanesWithKeyPrefix("track." + id + ".send.");
        apexLanes.removeLanesWithKeyPrefix("plugin." + id + ".");

        if (auto* track = appCore_.getTrackManager().getTrack(id))
            track->setAutomationMuted(false);

        if (arrangement_)
            arrangement_->repaint();
    };
    trackList_->onAutomationTargetClearRequested = [this](const DAW::TrackID& id, const juce::String& paramId)
    {
        appCore_.getAutomationManager().clearLane(id, paramId);

        auto& apexRegistry = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto& apexLanes = apex::automation::AutomationLaneStore::getInstance();
        if (paramId.startsWith("track." + id + ".") || paramId.startsWith("plugin." + id + "."))
            apexLanes.removeLane(apexRegistry.findID(paramId));
        else if (paramId == DAW::AutomationLaneCore::trackVolumeParameterId)
            apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(id)));
        else if (paramId == DAW::AutomationLaneCore::trackPanParameterId)
            apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackPanKey(id)));
        else if (paramId == "track.mute")
            apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackMuteKey(id)));
        else if (paramId == "track.solo")
            apexLanes.removeLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackSoloKey(id)));

        if (arrangement_)
            arrangement_->repaint();
    };
    trackList_->onCreateSequenceRequested = [this](const DAW::TrackID& trackId,
                                                    const juce::String& paramId)
    {
        using namespace APEX::AutomationSeq;

        juce::Component::SafePointer<MainComponent> safeThis(this);
        const double sr  = appCore_.getCurrentSampleRate();
        auto applySequence = [safeThis, sr](const DAW::TrackID& targetTrackId,
                                            const juce::String& targetParamId,
                                            const GeneratedCurve& curve,
                                            const SequenceParams& params,
                                            bool closeWindow)
        {
            if (!safeThis)
                return;

            const double bpm = static_cast<double>(safeThis->appCore_.getState().tempo.getValue());
            const double safeBpm = (bpm > 0.0) ? bpm : 120.0;
            const double samplesPerBeat = sr * (60.0 / safeBpm);
            const double totalSamples = samplesPerBeat * 4.0 * juce::jmax(1.0f, params.patternBars);

            auto& mgr = safeThis->appCore_.getAutomationManager();
            mgr.clearLane(targetTrackId, targetParamId);
            auto& lane = mgr.getOrCreateLane(targetTrackId, targetParamId);
            lane.setVisible(true);
            lane.setDefaultValue(sequenceValueToAutomationValue(targetParamId, 0.5f));

            constexpr float kThreshold = 0.005f;
            float lastEmitted = -999.0f;
            const int N = static_cast<int>(curve.samples.size());

            for (int i = 0; i < N; ++i)
            {
                const float normalised = curve.samples[i];
                if (std::abs(normalised - lastEmitted) >= kThreshold
                    || i == 0 || i == N - 1)
                {
                    const int64_t tSamp = static_cast<int64_t>(
                        std::round((i / static_cast<double>(N - 1))
                                   * totalSamples));
                    mgr.addOrReplacePoint(targetTrackId, targetParamId,
                                          tSamp, sequenceValueToAutomationValue(targetParamId, normalised),
                                          /*deferPublish=*/true);
                    lastEmitted = normalised;
                }
            }

            mgr.publishSnapshot();
            if (safeThis->arrangement_)
                safeThis->arrangement_->repaint();
            if (closeWindow)
                safeThis->createSequenceWindow_ = nullptr;
        };

        auto* dlgComp = new AutomationSequenceDialogComponent(
            [applySequence, trackId, paramId]
            (const GeneratedCurve& curve, const SequenceParams& params)
            {
                applySequence(trackId, paramId, curve, params, true);
            },
            [applySequence, trackId, paramId]
            (const GeneratedCurve& curve, const SequenceParams& params)
            {
                applySequence(trackId, paramId, curve, params, false);
            },
            [safeThis, applySequence, paramId]
            (const GeneratedCurve& curve, const SequenceParams& params)
            {
                if (!safeThis)
                    return;

                const auto& tracks = safeThis->appCore_.getTrackManager().getAllTracks();
                for (auto* track : tracks)
                    if (track != nullptr && !track->isMaster())
                        applySequence(track->getID(), paramId, curve, params, false);
            },
            [safeThis]
            {
                if (safeThis)
                    safeThis->createSequenceWindow_ = nullptr;
            }
        );

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(dlgComp);
        opts.dialogTitle            = "";
        opts.dialogBackgroundColour = juce::Colour(0xFF18121A);
        opts.useNativeTitleBar      = false;
        opts.resizable              = false;
        opts.componentToCentreAround = trackList_.get();
        createSequenceWindow_ = opts.launchAsync();
    };
    trackList_->hasAutomationData = [this](const DAW::TrackID& id)
    {
        const auto* track = appCore_.getTrackManager().getTrack(id);
        const auto activeParam = track != nullptr && track->getActiveAutomationParameterId().isNotEmpty()
            ? track->getActiveAutomationParameterId()
            : juce::String(DAW::AutomationLaneCore::trackVolumeParameterId);

        if (auto* lane = appCore_.getAutomationManager().findLane(id, activeParam))
            if (!lane->points.empty())
                return true;

        auto& apexRegistry = apex::automation::AutomationParameterKeyRegistry::getInstance();
        auto hasApexLane = [](apex::automation::ParameterID pid)
        {
            if (pid == apex::automation::kInvalidParameterID)
                return false;
            auto lane = apex::automation::AutomationLaneStore::getInstance().findLane(pid);
            if (lane == nullptr)
                return false;
            auto snap = lane->getSnapshot();
            return snap != nullptr && !snap->empty();
        };

        if (activeParam.startsWith("track." + id + ".") || activeParam.startsWith("plugin." + id + "."))
        {
            if (hasApexLane(apexRegistry.findID(activeParam)))
                return true;
        }
        else if (activeParam == DAW::AutomationLaneCore::trackVolumeParameterId)
        {
            if (hasApexLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(id))))
                return true;
        }
        else if (activeParam == DAW::AutomationLaneCore::trackPanParameterId)
        {
            if (hasApexLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackPanKey(id))))
                return true;
        }

        if (activeParam != DAW::AutomationLaneCore::trackVolumeParameterId)
            if (auto* lane = appCore_.getAutomationManager().findLane(id, DAW::AutomationLaneCore::trackVolumeParameterId))
                return !lane->points.empty();

        if (activeParam != DAW::AutomationLaneCore::trackVolumeParameterId)
            if (hasApexLane(apexRegistry.findID(apex::automation::AutomationParameterKeyRegistry::trackVolumeKey(id))))
                return true;

        return false;
    };
    trackList_->onGetAudioClipsOnTrack = [this](const DAW::TrackID& id) -> juce::Array<DAW::Clip*>
    {
        juce::Array<DAW::Clip*> result;
        for (auto* clip : appCore_.getClipManager().getClipsOnTrack(id))
            if (clip != nullptr && clip->getType() == DAW::ClipType::Audio)
                result.add(clip);
        return result;
    };
    trackList_->onGetRoutingGraph = [this]() -> DAW::RoutingGraph*
    {
        return &appCore_.getRoutingGraph();
    };
    trackList_->onAutomationLaneStateChanged = [this](const DAW::TrackID&)
    {
        if (arrangement_)
            arrangement_->requestAutomationSync();
    };
    trackList_->onPluginDropReceived = [this](const DAW::TrackID& srcTrack, int srcSlot,
                                               const DAW::TrackID& destTrack)
    {
        if (copyPluginBetweenTracks(srcTrack, srcSlot, destTrack))
            DBG("[MainComponent] Cross-track plugin copy via timeline row: src=" + srcTrack
                + " slot=" + juce::String(srcSlot) + " -> dest=" + destTrack);
    };

    // Timeline folder-drop: dragging a track row onto another row either
    // creates a new FolderBus (drop on regular track) or adopts into the
    // existing FolderBus (drop on folder-bus row). Mirrors mixer behaviour.
    trackList_->onFolderDropRequested = [this](const DAW::TrackID& draggedId,
                                                const DAW::TrackID& targetId)
    {
        handleFolderDropRequest(draggedId, targetId);
    };
    // Authoritative folder-topology lookups for the drop-target resolver so
    // dropping on a visible child of an open folder adopts into that folder
    // even when Track::parentTrackID_ is empty/stale.
    trackList_->onGetParentFolderBus = [this](const DAW::TrackID& id) -> DAW::TrackID
    {
        return appCore_.getFolderBus().getParentFolderBus(id);
    };
    trackList_->onIsFolderBus = [this](const DAW::TrackID& id) -> bool
    {
        return appCore_.getFolderBus().isFolderBus(id);
    };
    trackList_->onFolderCollapseChanged = [this](const DAW::TrackID& folderTrackId, bool expanded)
    {
        setFolderCollapsed(folderTrackId, !expanded);
    };
    trackList_->onTrackReorderRequested = [this](const DAW::TrackID& trackId, int targetIndex)
    {
        moveTracksWithFolderAwareness({ trackId }, targetIndex);
    };

    // Multi-track reorder: the whole selection moves as one block in a single
    // undo step. Routing is keyed by stable TrackID, so cables follow the
    // reorder automatically.
    trackList_->onGetMultiSelectedTrackIds = [this]() -> std::vector<DAW::TrackID>
    {
        return getSelectedTrackIdsInProjectOrder();
    };
    trackList_->onMultiTrackReorderRequested = [this](const std::vector<DAW::TrackID>& selectedIds, int targetIndex)
    {
        moveTracksWithFolderAwareness(selectedIds, targetIndex);
    };
    trackList_->onReorderAutoScroll = [this](int deltaY) -> bool
    {
        if (timelineViewport_ == nullptr)
            return false;

        auto* viewed = timelineViewport_->getViewedComponent();
        if (viewed == nullptr)
            return false;

        const auto current = timelineViewport_->getViewPosition();
        const int maxY = juce::jmax(0, viewed->getHeight() - timelineViewport_->getViewHeight());
        const int nextY = juce::jlimit(0, maxY, current.y + deltaY);
        if (nextY == current.y)
            return false;

        timelineViewport_->setViewPosition(current.x, nextY);
        if (trackList_ != nullptr)
            trackList_->setScrollOffset(nextY);
        return true;
    };

    // ── Quick Send mode ──────────────────────────────────────────────────
    trackList_->onQuickSendToggleRequested = [this](DAW::TrackRow* row)
    {
        if (row == nullptr) return;
        toggleQuickSendMode(row->getTrackID());
    };
    // Left-click toggles send (create if missing, remove if exists)
    trackList_->onQuickSendToggleSend = [this](DAW::TrackRow* row)
    {
        if (row == nullptr) return;
        const auto source = quickSendMode_.getSourceTrackId();
        if (source.isNotEmpty() && row->getTrackID() != source)
        {
            // Use the Quick Send source explicitly — never sourceSync, which
            // the row click already re-synced to the clicked track (via
            // onTrackSelected → bgV2.onTrackSelected). Otherwise hasSendTo()
            // queries the wrong source and the second click never removes.
            if (appCore_.getBubblegumV2().hasSendFromTo(source, row->getTrackID()))
                quickSendDeleteSendTo(row->getTrackID());
            else
                quickSendCreateSend(row->getTrackID());
        }
    };
    // Right-click toggles sidechain (create if missing, remove if exists)
    trackList_->onQuickSendToggleSidechain = [this](DAW::TrackRow* row)
    {
        if (row == nullptr) return;
        const auto source = quickSendMode_.getSourceTrackId();
        if (source.isNotEmpty() && row->getTrackID() != source)
        {
            auto& bgV2 = appCore_.getBubblegumV2();
            if (bgV2.hasSidechainFromTo(source, row->getTrackID()))
                bgV2.removeSidechainFrom(source, row->getTrackID());
            else
                bgV2.handleSidechainTapFrom(source, row->getTrackID());
            refreshBubblegumFeedback();
            updateQuickSendUI();
            cableOverlay_.notifyMotionFinished();
        }
    };
    trackList_->onQuickSendExitRequested = [this]
    {
        exitQuickSendMode();
    };
    trackList_->onPreFaderToggleRequested = [this](const DAW::TrackID& id)
    {
        handlePreFaderTogglePressed(id);
    };

    // NOTE: the floating Quick Send popup was removed by design — the gold
    // aura on the source row is the only Quick Send hint. Send level editing
    // lives in the destination-track cable anchor (wet/dry knob) and in the
    // Bubblegum send panel (BubblegumV2PanelUI), both kept in sync through
    // BubblegumV2System::setSendLevelFrom.

    trackList_->onQuickAddTrackRequested = [this]
    {
        // The visible TRACKS "+" opens the Quick Track Builder — it must NOT
        // create a generic "Audio N" track anymore.
        openQuickWorkflow(DAW::QuickWorkflowTab::QuickTrack,
                          trackList_->getQuickAddButtonScreenBounds());
    };

    // Floating window chrome wrapping the track list
    trackListWindow_ = std::make_unique<DAW::TrackListWindow>(*trackList_);
    trackListWindow_->setEmbedded(true); // No chrome when docked in the main layout
    trackListWindow_->onCloseClicked = [this]
    {
        trackListW_ = 0;
        trackListWindow_->setVisible(false);
        resized();
    };
    addAndMakeVisible(trackListWindow_.get());

    // NOTE: the floating Quick Send popup was removed by design — only the
    // gold source aura remains.

    auto timelineViewport = std::make_unique<TimelineViewport>("timelineViewport");

    // Arrangement view (main timeline)
    arrangement_ = std::make_unique<DAW::ArrangementView>(
        appCore_.getTrackManager(),
        appCore_.getClipManager(),
        appCore_.getState(),
        appCore_.getMarkerManager(),
         appCore_.getTransport(),
         &appCore_.getAudioFileManager(),
         &appCore_.getAutomationManager(),
         &appCore_.getClipRegionPluginCore(),
         &appCore_.getPluginScanner().getFormatManager());

    // ========================================================================
    // PHASE 3: INTEGRATE AUTOMATION SYSTEM
    // ========================================================================
    // Wire automation manager and initialize test lanes
    {
        auto& automationManager = appCore_.getAutomationManager();
        auto& trackManager = appCore_.getTrackManager();

        // Create test automation data so we can see something immediately
        #if JUCE_DEBUG
            // Try to enable automation on "Drums" track (created above)
            if (auto* drums = trackManager.getTrack(DAW::TrackID("Drums")))
            {
                // Create a simple test volume automation curve
                auto& volumeLane = automationManager.getOrCreateLane(
                    DAW::TrackID("Drums"), "track.volume");

                // Add test points for volume curve
                volumeLane.addPoint(0,      0.5f);
                volumeLane.addPoint(44100, 0.7f);
                volumeLane.addPoint(88200, 0.3f);

                // Set curve types
                if (volumeLane.points.size() >= 2)
                {
                    volumeLane.points[0].curveToNext = DAW::AutomationCurveType::Smooth;
                    volumeLane.points[0].tensionToNext = 0.0f;
                    volumeLane.points[1].curveToNext = DAW::AutomationCurveType::DoubleCurve;
                    volumeLane.points[1].tensionToNext = 0.5f;
                }

                // Enable automation visibility on this track
                // (This makes paintAutomationOverlay() render the curves)
                drums->setAutomationVisible(true);
            }

            // Publish snapshot
            automationManager.publishSnapshot();
        #endif
    }
    // ========================================================================

    arrangement_->setPluginChainProvider([this](const DAW::TrackID& trackId) -> DAW::PluginChainCore*
    {
        return appCore_.getPluginChain(trackId);
    });
    arrangement_->onPluginDropCopy = [this](const DAW::TrackID& srcTrack, int srcSlot,
                                             const DAW::TrackID& destTrack)
    {
        // The Mixer owns the existing cross-track dispatch callback.  Routing
        // the Arranger target through the same callback keeps all surfaces on
        // one canonical plugin-copy operation; its Undo/dirty handling is
        // normalized in the shared callback below during plugin-copy cleanup.
        if (mixerPanel_ != nullptr && mixerPanel_->onPluginDropCopy)
            mixerPanel_->onPluginDropCopy(srcTrack, srcSlot, destTrack);
    };

    // Wire top-toolbar metronome button to the shared click state.
    arrangement_->setMetronomeBindings(
        [this]() -> bool
        {
            return clickState_ != nullptr && clickState_->getActiveMode() != DAW::ClickActiveMode::Off;
        },
        [this]()
        {
            if (clickState_ == nullptr)
                return;
            const int next = ((int)clickState_->getActiveMode() + 1) % 3;
            clickState_->activeMode.store(next, std::memory_order_relaxed);
        });

    // Wire top-toolbar automation record arm button
    arrangement_->setAutoArmBindings(
        []() -> bool
        {
            return apex::automation::AutomationTransportState::getInstance().isRecordArmed();
        },
        []()
        {
            auto& state = apex::automation::AutomationTransportState::getInstance();
            state.setRecordArmed(!state.isRecordArmed());
        });

    // Wire the compact "+" add-track toolbar button to the Quick Track
    // Builder: clicking it now OPENS the builder popup (anchored to the real
    // AddTrackButton). It no longer creates a generic track immediately.
    quickWorkflow_ = std::make_unique<DAW::QuickWorkflowCore>(
        appCore_.getTrackManager(),
        appCore_.getRoutingGraph(),
        *appCore_.getBubblegumV2().getMasterRoute(),
        appCore_.getBubblegumV2().sendState,
        appCore_.getQuickTrackColors());
    arrangement_->setAddTrackRequested([this]
    {
        openQuickWorkflow();
    });

    arrangement_->setRoutingGraphProvider([this]() -> DAW::RoutingGraph*
    {
        return &appCore_.getRoutingGraph();
    });

    auto syncTrackHeaders = [this]()
    {
        if (!arrangement_ || !trackList_)
            return;

        updateTimelineViewportContentBounds();

        trackList_->setScrollOffset(arrangement_->getVerticalScrollOffset());

        // Master lane is at index 0 in ArrangementView; offset regular tracks by 1
        const int masterOffset = appCore_.getTrackManager().hasMasterTrack() ? 1 : 0;
        if (masterOffset > 0)
            trackList_->setMasterRowHeight(arrangement_->getLaneHeight(0));

        auto n = juce::jmin(trackList_->getNumRows(), arrangement_->getNumLanes() - masterOffset);
        for (int i = 0; i < n; ++i)
            trackList_->setRowHeight(i, arrangement_->getLaneHeight(i + masterOffset));
        trackList_->resized();
        trackList_->repaint();
    };

    // Keep TrackList in sync when Arrangement lanes are resized vertically
    arrangement_->onLaneHeightChanged = [this](int index, int newHeight)
    {
        if (auto* row = trackList_->getRow(index))
        {
            row->setDesiredHeight(newHeight);
            trackList_->resized();
        }
    };
    arrangement_->onVerticalScrollChanged = [this](int y)
    {
        if (trackList_)
            trackList_->setScrollOffset(y);
    };
    arrangement_->onLaneLayoutChanged = syncTrackHeaders;
    arrangement_->onPreparedAudioImport = [this](juce::Array<juce::File> files,
                                                  std::vector<DAW::AudioFileManager::PreparedAudio> preparedAudio,
                                                  DAW::TrackID selectedTrackId,
                                                  DAW::SamplePosition insertPosition)
    {
        const auto sampleRate = appCore_.getCurrentSampleRate() > 0.0
            ? appCore_.getCurrentSampleRate() : 44100.0;
        DAW::CommandManager::getInstance().execute(
            std::make_unique<DAW::ImportAudioFilesCommand>(
                appCore_.getTrackManager(),
                appCore_.getClipManager(),
                appCore_.getAudioFileManager(),
                std::move(files),
                std::move(selectedTrackId),
                insertPosition,
                sampleRate,
                std::move(preparedAudio)));
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible())
            bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    };
    trackList_->onExclusiveMuteRequested = [this](const DAW::TrackID& id)
    {
        exclusiveMuteTrack(id);
    };
    trackList_->onConvertFolderToTrackRequested = [this](const DAW::TrackID& id)
    {
        convertFolderBusToRegularTrack(id);
    };
    arrangement_->onSendToBubble = [this]
    {
        if (timelineWindow_)  timelineWindow_->setVisible(false);
        if (trackListWindow_) trackListWindow_->setVisible(false);
        if (timelineBubble_)
        {
            timelineBubble_->setVisible(true);
            timelineBubble_->toFront(false);
        }
        resized();
    };
    // Quick-access from timeline toolbar - wired to shared panel instances after chrome creation
    // (callbacks assigned later in constructor once slimeSidePanel_/browserChrome_ exist)
    arrangement_->onOpenMidiClip = [this](DAW::MidiClip& clip)
    {
        openPianoRollForClip(clip);
    };
    arrangement_->onOpenClipVocalTune = [this](DAW::Clip& clip)
    {
        appCore_.getState().selectedClipID.setValue(clip.getID());
        appCore_.getState().selectedTrackID.setValue(clip.getTrackID());
        syncBubblegumSourceSelection(clip.getTrackID());
        openVocalTuneForSelectedClip();
    };
    arrangement_->onToggleClipVocalTuneBypass = [this](DAW::Clip& clip)
    {
        if (auto* integration = appCore_.getVocalTuneIntegrationPtr())
        {
            const bool current = integration->isBypassedForClip(clip.getID());
            integration->setBypassedForClip(clip.getID(), !current);
            appCore_.markProjectDirty("VocalTune bypass toggled");
        }
    };
    arrangement_->onIsClipVocalTuneBypassed = [this](const DAW::Clip& clip) -> bool
    {
        if (auto* integration = appCore_.getVocalTuneIntegrationPtr())
            return integration->isBypassedForClip(clip.getID());
        return false;
    };

    arrangement_->onBoxSelectionCommitted = [this](const std::vector<juce::String>& clipIds, bool additive)
    {
        if (!additive)
            multiSelection_.clearKind(DAW::SelectionKind::Clip);

        for (const auto& id : clipIds)
        {
            auto* clip = appCore_.getClipManager().getClip(id);
            if (!clip) continue;
            multiSelection_.add(DAW::SelectionTarget::clip(clip->getTrackID(), id));
        }

        std::unordered_set<juce::String> ids;
        for (const auto& t : multiSelection_.getSelected(DAW::SelectionKind::Clip))
            ids.insert(t.clipId);
        if (arrangement_)
            arrangement_->setMultiSelectedClips(ids);

        if (!clipIds.empty())
        {
            if (auto* clip = appCore_.getClipManager().getClip(clipIds.front()))
            {
                appCore_.getState().selectedClipID.setValue(clipIds.front());
                appCore_.getState().selectedTrackID.setValue(clip->getTrackID());
                syncBubblegumSourceSelection(clip->getTrackID());
            }
        }
    };

    arrangement_->onClipSelectionCleared = [this]()
    {
        multiSelection_.clearKind(DAW::SelectionKind::Clip);
    };
    arrangement_->onClipDeleted = [this](const DAW::ClipID& clipId)
    {
        if (clipPropertiesWindow_ != nullptr && clipPropertiesWindow_->isShowingClip(clipId))
        {
            clipFxPanelOpenByClipId_.erase(clipId);
            clipPropertiesWindow_->clearClip();
            clipPropertiesWindow_->setVisible(false);
            if (bubbleTaskbar_)
                bubbleTaskbar_->unregisterWindow("clipproperties");
        }
    };
    // The arrangement view must use the SAME automation manager the audio
    // engine reads. Nothing was wiring it, so ArrangementViewCore silently
    // created its own fallback manager: timeline lanes were empty (no dots to
    // edit) and preset curves written by the view never reached the engine.
    if (arrangement_ && arrangement_->getCore() != nullptr)
        arrangement_->getCore()->setAutomationManager(&appCore_.getAutomationManager());

    arrangement_->onOpenClipProperties = [this](DAW::Clip& clip)
    {
        if (!clipPropertiesWindow_)
            return;

        const auto clipId = clip.getID();
        const auto previouslyOpenClipId = clipPropertiesWindow_->getPanel().getClip() != nullptr
            ? clipPropertiesWindow_->getPanel().getClip()->getID()
            : juce::String();

        if (previouslyOpenClipId.isNotEmpty())
            clipFxPanelOpenByClipId_[previouslyOpenClipId] = clipPropertiesWindow_->isFxPanelOpen();

        appCore_.getState().selectedClipID.setValue(clip.getID());
        clipPropertiesWindow_->setClip(&clip, &appCore_.getAudioFileManager(), appCore_.getCurrentSampleRate());
        clipPropertiesWindow_->setKnownPlugins(&appCore_.getPluginScanner().getKnownPlugins());
        clipPropertiesWindow_->setClipRegionPluginCore(&appCore_.getClipRegionPluginCore());

        // Do NOT activate automation mode here. Opening clip properties must
        // not switch the clip into the automation editor — that made every
        // double-click leave the clip with an active automation area that
        // could not be dismissed. Automation mode is activated explicitly by
        // "Automate This Clip" (onOpenClipAutomation), which wires the same
        // callbacks and calls activateAutomationMode() itself.

        clipPropertiesWindow_->getPanel().onOpenPianoRoll = [this](DAW::MidiClip& midiClip)
        {
            openPianoRollForClip(midiClip);
        };
        auto openClipRegionPluginEditor = [this](DAW::Clip& c, juce::AudioPluginInstance& instance,
                                                 const juce::String& windowKey, const juce::String& title)
        {
            const auto trackId = c.getTrackID();
            auto existingWindow = std::find_if(clipRegionPluginWindows_.begin(), clipRegionPluginWindows_.end(),
                [&windowKey](const std::unique_ptr<juce::DocumentWindow>& window)
                {
                    return window != nullptr && window->getComponentID() == windowKey;
                });

            if (existingWindow != clipRegionPluginWindows_.end() && *existingWindow != nullptr)
            {
                (*existingWindow)->setVisible(true);
                (*existingWindow)->toFront(true);
                if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowKey, false);
                return;
            }

            if (!instance.hasEditor())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "No Plugin Editor", title + " loaded for this clip but does not provide an editor.", "OK");
                return;
            }

            auto editor = std::unique_ptr<juce::AudioProcessorEditor>(instance.createEditor());
            if (editor == nullptr)
                return;

            auto window = std::make_unique<ClipRegionPluginWindow>(
                title, appCore_.getTransport(), getTopLevelComponent());
            auto* windowRaw = window.get();
            windowRaw->setComponentID(windowKey);
            windowRaw->setContentOwned(editor.release(), true);
            windowRaw->centreWithSize(juce::jmax(360, windowRaw->getContentComponent() ? windowRaw->getContentComponent()->getWidth() : 640),
                                      juce::jmax(240, windowRaw->getContentComponent() ? windowRaw->getContentComponent()->getHeight() : 420));
            windowRaw->onCloseRequested = [this, windowRaw, windowKey]
            {
                if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowKey);
                auto it = std::find_if(clipRegionPluginWindows_.begin(), clipRegionPluginWindows_.end(),
                    [windowRaw](const std::unique_ptr<juce::DocumentWindow>& window) { return window.get() == windowRaw; });
                if (it != clipRegionPluginWindows_.end())
                    clipRegionPluginWindows_.erase(it);
            };
            windowRaw->onMinimizeRequested = [this, windowRaw, windowKey, trackId]
            {
                if (bubbleTaskbar_)
                {
                    juce::String trackLabel;
                    if (auto* track = appCore_.getTrackManager().getTrack(trackId))
                        trackLabel = track->getName();
                    else
                        trackLabel = trackId;

                    juce::String clipName;
                    if (auto* clip = appCore_.getClipManager().getClip(windowKey))
                        clipName = clip->getName();
                    else
                    {
                        auto titleText = windowRaw->getName();
                        auto clipTag = titleText.fromLastOccurrenceOf("Clip: ", false, false).trim();
                        clipName = clipTag.isNotEmpty() ? clipTag : titleText;
                    }

                    bubbleTaskbar_->registerWindow(windowKey, windowRaw->getName(), "Panels",
                        [windowRaw, this, windowKey]
                        {
                            windowRaw->setVisible(true);
                            windowRaw->toFront(true);
                            if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowKey, false);
                        },
                        [windowRaw]
                        {
                            if (windowRaw != nullptr)
                                windowRaw->closeButtonPressed();
                        },
                        trackId, windowRaw->getName(), "Clip FX - Track: " + trackLabel + "\nClip: " + clipName);
                    bubbleTaskbar_->setWindowMinimized(windowKey, true);
                }
            };
            windowRaw->setVisible(true);
            windowRaw->toFront(true);

            clipRegionPluginWindows_.push_back(std::move(window));
        };
        clipPropertiesWindow_->getPanel().onOpenClipRegionPlugin = [this, openClipRegionPluginEditor](DAW::Clip& c, const juce::PluginDescription& desc)
        {
            if (dynamic_cast<DAW::AudioClip*>(&c) == nullptr)
                return;

            auto loaded = appCore_.getClipRegionPluginCore().loadForClip(
                c.getID(), desc, appCore_.getPluginScanner().getFormatManager());

            if (!loaded.success || loaded.instance == nullptr)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Clip Plugin Load Failed",
                    loaded.message.isNotEmpty() ? loaded.message : "Could not load clip-region plugin.",
                    "OK");
                return;
            }

            auto entries = appCore_.getClipRegionPluginCore().getEntriesForClip(c.getID());
            if (entries.empty()) return;
            auto instanceId = entries.back().instanceId;
            auto* entry = appCore_.getClipRegionPluginCore().findEntryById(c.getID(), instanceId);
            if (entry != nullptr && entry->instance != nullptr)
                openClipRegionPluginEditor(c, *entry->instance, instanceId, desc.name + " - Clip: " + c.getName());

            DBG("[ClipRegionPlugin] opened dedicated plugin=" << desc.name << " clipId=" << c.getID());
        };
        clipPropertiesWindow_->getPanel().onOpenActiveClipRegionPlugin = [this, openClipRegionPluginEditor](DAW::Clip& c, const juce::String& instanceId)
        {
            auto* entry = appCore_.getClipRegionPluginCore().findEntryById(c.getID(), instanceId);
            if (entry != nullptr && entry->instance != nullptr)
                openClipRegionPluginEditor(c, *entry->instance, instanceId, entry->description.name + " - Clip: " + c.getName());
        };
        clipPropertiesWindow_->setEmbedded(false);
        clipPropertiesWindow_->onKeyPress = [this](const juce::KeyPress& key)
        {
            if (key == juce::KeyPress::spaceKey)
            {
                DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TransportPlayStop);
                return true;
            }

            return false;
        };
        clipPropertiesWindow_->getPanel().setWantsKeyboardFocus(true);
        clipPropertiesWindow_->getPanel().setMouseClickGrabsKeyboardFocus(true);
        if (auto* fxPanel = clipPropertiesWindow_->getPanel().getFxFloatingPanel())
        {
            fxPanel->onKeyPress = [this](const juce::KeyPress& key)
            {
                if (key == juce::KeyPress::spaceKey)
                {
                    DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TransportPlayStop);
                    return true;
                }

                return false;
            };
        }
        auto area = getLocalBounds().reduced(80, 60).withSizeKeepingCentre(420, 360);
        clipPropertiesWindow_->setBounds(area);
        clipPropertiesWindow_->setVisible(true);
        clipPropertiesWindow_->restoreIfMinimized();
        if (auto it = clipFxPanelOpenByClipId_.find(clipId); it != clipFxPanelOpenByClipId_.end())
            clipPropertiesWindow_->setFxPanelOpen(it->second);
        else
            clipPropertiesWindow_->setFxPanelOpen(false);
        clipPropertiesWindow_->toFront(true);
        clipPropertiesWindow_->repaint();
    };
    arrangement_->onOpenSelectedClipProperties = [this]()
    {
        auto selectedId = appCore_.getState().selectedClipID.getValue().toString();
        if (selectedId.isEmpty())
            return;

        if (auto* clip = appCore_.getClipManager().getClip(selectedId))
            if (arrangement_->onOpenClipProperties)
                arrangement_->onOpenClipProperties(*clip);
    };
    arrangement_->onOpenSelectedMidiClip = [this]()
    {
        auto selectedId = appCore_.getState().selectedClipID.getValue().toString();
        if (selectedId.isNotEmpty())
        {
            if (auto* clip = appCore_.getClipManager().getClip(selectedId))
                if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
                {
                    openPianoRollForClip(*midiClip);
                    return;
                }
        }
        // No MIDI clip selected — open Piano Roll in empty state
        if (pianoRollWindow_)
        {
            pianoRollWindow_->setVisible(true);
            if (pianoRollWindow_->getBounds().isEmpty())
                pianoRollWindow_->setBounds(getLocalBounds().reduced(90, 70));
            pianoRollWindow_->toFront(true);
        }
    };
    arrangement_->onOpenClipAutomation = [this](DAW::Clip& clip)
    {
        if (!clipPropertiesWindow_)
            return;

        if (arrangement_ && arrangement_->onOpenClipProperties)
            arrangement_->onOpenClipProperties(clip);

        DAW::TrackID trackId = clip.getTrackID();
        ArrangementEditor::ClipAutomationPanelCallbacks panelCb;

        panelCb.createLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            DAW::AutomationQuickCreateCore::createLaneAndOptionalRegion(
                appCore_.getAutomationManager(), t, t.regionLengthSamples > 0);
            if (arrangement_)
            {
                arrangement_->showAutomationLane(t.trackId, t.parameterId);
                arrangement_->repaint();
            }
        };

        panelCb.showLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            if (arrangement_)
                arrangement_->showAutomationLane(t.trackId, t.parameterId);
        };

        panelCb.hideLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            if (arrangement_)
                arrangement_->hideAutomationLane(t.trackId, t.parameterId);
        };

        panelCb.clearLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            appCore_.getAutomationManager().clearLane(t.trackId, t.parameterId);
            if (arrangement_)
            {
                // Clearing the automation must also remove its lane row from
                // the timeline — otherwise the line stays visible with nothing
                // in it and there is no way to dismiss it.
                arrangement_->hideAutomationLane(t.trackId, t.parameterId);
                arrangement_->repaint();
            }
        };

        panelCb.laneExists = [this](const DAW::TrackID& laneTrackId, const juce::String& parameterId) -> bool
        {
            return appCore_.getAutomationManager().findLane(laneTrackId, parameterId) != nullptr;
        };

        clipPropertiesWindow_->activateAutomationMode(trackId, clip.getID(), panelCb, &appCore_.getAutomationManager());
        clipPropertiesWindow_->toFront(true);
    };

    timelineViewport->setViewedComponent(arrangement_.get(), false);
    timelineViewport->setScrollBarsShown(true, true);
    timelineViewport->setScrollBarThickness(14);
    timelineViewport->setSingleStepSizes(32, 32);
    timelineViewport->getPixelsPerSecond = [this]() -> double
    {
        return arrangement_ ? arrangement_->getPixelsPerSecond() : 100.0;
    };
    timelineViewport->getLaneHeight = [this]() -> double
    {
        return arrangement_ ? (double)arrangement_->getLaneHeight(0) : 60.0;
    };
    // Single callback handles both horizontal and vertical edge-drag zoom
    timelineViewport->onThumbEdgeDragged = [this](const TimelineZoomScrollBar::EdgeDragInfo& info)
    {
        if (!arrangement_ || !timelineViewport_)
            return;
        if (info.trackSizePixels <= 0 || info.initialThumbSizePixels <= 0.0f)
            return;

        const double delta     = (double)info.currentPosPixels - (double)info.dragStartPosPixels;
        const double trackSz   = (double)info.trackSizePixels;
        const double minSz     = juce::jmax(20.0, (double)info.minThumbSizePixels);
        const double initStart = (double)info.initialThumbStartPixels;
        const double initEnd   = initStart + (double)info.initialThumbSizePixels;

        double newStart = initStart, newEnd = initEnd;
        if (info.isStartEdge)
            newStart = juce::jlimit((double)info.trackStartPixels, initEnd - minSz, initStart + delta);
        else
            newEnd = juce::jlimit(initStart + minSz, (double)(info.trackStartPixels + info.trackSizePixels), initEnd + delta);

        const double newThumbSize = juce::jmax(minSz, newEnd - newStart);
        const double zoomRatio    = (double)info.initialThumbSizePixels / newThumbSize;

        if (!info.isVertical)
        {
            const double refPps   = juce::jmax(1.0, info.referencePixelsPerSecond);
            const double newPps   = juce::jlimit(10.0, 4000.0, refPps * zoomRatio);
            const double vpWidth  = (double)timelineViewport_->getViewWidth();

            // Anchor: the edge that does NOT move stays at the same time position.
            // Start edge drag ? right side of viewport is anchor.
            // End edge drag   ? left  side of viewport is anchor.
            double anchorTime;
            if (info.isStartEdge)
                anchorTime = ((double)info.referenceViewX + vpWidth) / refPps;
            else
                anchorTime = (double)info.referenceViewX / refPps;

            arrangement_->setPixelsPerSecond(newPps);
            updateTimelineViewportContentBounds();

            int newViewX;
            if (info.isStartEdge)
                newViewX = juce::roundToInt(anchorTime * newPps - vpWidth);
            else
                newViewX = juce::roundToInt(anchorTime * newPps);

            const auto va = timelineViewport_->getViewArea();
            const int maxX = juce::jmax(0, arrangement_->getWidth() - va.getWidth());
            timelineViewport_->setViewPosition(juce::jlimit(0, maxX, newViewX), va.getY());
        }
        else
        {
            const double refLaneH  = juce::jmax(1.0, info.referenceLaneHeight);
            const double newLaneH  = juce::jlimit(24.0, 240.0, refLaneH * zoomRatio);
            const double vpHeight  = (double)timelineViewport_->getViewHeight();

            double anchorContentY;
            if (info.isStartEdge)
                anchorContentY = ((double)info.referenceViewY + vpHeight) / juce::jmax(1.0, refLaneH);
            else
                anchorContentY = (double)info.referenceViewY / juce::jmax(1.0, refLaneH);

            arrangement_->setLaneHeight(0, juce::roundToInt(newLaneH));
            updateTimelineViewportContentBounds();

            int newViewY;
            if (info.isStartEdge)
                newViewY = juce::roundToInt(anchorContentY * newLaneH - vpHeight);
            else
                newViewY = juce::roundToInt(anchorContentY * newLaneH);

            const auto va = timelineViewport_->getViewArea();
            const int maxY = juce::jmax(0, arrangement_->getHeight() - va.getHeight());
            timelineViewport_->setViewPosition(va.getX(), juce::jlimit(0, maxY, newViewY));

            if (trackList_)
                trackList_->setScrollOffset(timelineViewport_->getViewPositionY());
        }

        // Force scrollbar repaint so thumb redraws at the new size immediately
        timelineViewport_->getHorizontalScrollBar().repaint();
        timelineViewport_->getVerticalScrollBar().repaint();
    };
    // Corner zoom buttons and Ctrl+arrow keyboard zoom share one operation.
    // Zoom keeps the viewport centre anchored — pro-DAW behaviour.
    timelineViewport->onZoomButton = [this](bool isVertical, bool zoomIn)
    {
        applyTimelineZoom(isVertical, zoomIn);
    };
    timelineViewport_ = std::move(timelineViewport);
    timelineViewport_->getVerticalScrollBar().addListener(this);
    timelineViewport_->getHorizontalScrollBar().addListener(this);
    addAndMakeVisible(*timelineViewport_);

    arrangement_->setCollapsedFolders(collapsedFolderTrackIds_);
    syncTrackHeaders();

    // Timeline floating window (visible by default)
    timelineWindow_ = std::make_unique<DAW::TimelineWindow>();
    timelineWindow_->setContent(timelineViewport_.get());
    timelineWindow_->onCloseClicked = [this]
    {
        timelineWindow_->setVisible(false);
        if (trackListWindow_) trackListWindow_->setVisible(false);
        timelineBubble_->setVisible(true);
        timelineBubble_->toFront(false);
        resized();
    };
    timelineWindow_->onMinimized = [this]
    {
        timelineWindow_->setVisible(false);
        if (trackListWindow_) trackListWindow_->setVisible(false);
        timelineBubble_->setVisible(true);
        timelineBubble_->toFront(false);
        resized();
    };
    timelineWindow_->onRestored = [this]
    {
        if (trackListWindow_ && trackListW_ > 0) trackListWindow_->setVisible(true);
        timelineBubble_->setVisible(false);
    };
    addAndMakeVisible(timelineWindow_.get());
    timelineWindow_->setEmbedded(true);
    timelineWindow_->setVisible(true);

    pianoRollWindow_ = std::make_unique<DAW::PianoRollWindow>();
    pianoRollWindow_->setTransport(&appCore_.getTransport());
    pianoRollWindow_->setVirtualKeyboardCore(&appCore_.getVirtualKeyboard());
    pianoRollWindow_->setVisible(false);
    pianoRollWindow_->onCloseClicked = [this]
    {
        if (pianoRollWindow_)
            pianoRollWindow_->setVisible(false);
    };
    addAndMakeVisible(pianoRollWindow_.get());

    // Beat-making system (FL Studio clone)
    stepSequencerWindow_ = std::make_unique<DAW::StepSequencerWindow>();
    drumSamplerEngine_ = std::make_unique<DAW::DrumSamplerEngine>(16);
    patternManager_ = std::make_unique<DAW::PatternManagerCore>(stepSequencerWindow_->getStepSequencer().getModel());
    stepSeqPlayback_.prepareToPlay(44100.0, 512);

    // Wire drum sampler to step sequencer
    stepSequencerWindow_->getStepSequencer().setDrumSampler(drumSamplerEngine_.get());

    clipPropertiesWindow_ = std::make_unique<DAW::ClipPropertiesWindow>();
    clipPropertiesWindow_->setVisible(false);
    clipPropertiesWindow_->onMinimized = [this]
    {
        if (!clipPropertiesWindow_)
            return;

        const auto clipId = clipPropertiesWindow_->getClipId();
        auto* clip = clipPropertiesWindow_->getPanel().getClip();
        if (clipId.isEmpty() || clip == nullptr)
            return;

        const bool shouldRestoreFxPanel = clipPropertiesWindow_->isFxPanelOpen();
        clipFxPanelOpenByClipId_[clipId] = shouldRestoreFxPanel;
        clipPropertiesWindow_->setFxPanelOpen(false);
        clipPropertiesWindow_->setVisible(false);

        auto clipTitle = clip->getName();
        juce::String trackTitle = "Track";
        if (auto* track = appCore_.getTrackManager().getTrack(clip->getTrackID()))
            trackTitle = track->getName();

        if (bubbleTaskbar_)
        {
            bubbleTaskbar_->registerWindow("clipproperties", "Clip Properties", "Panels", [this, clipId]
            {
                if (!clipPropertiesWindow_)
                    return;

                clipPropertiesWindow_->setVisible(true);
                clipPropertiesWindow_->restoreIfMinimized();
                if (auto* restoredClip = clipPropertiesWindow_->getPanel().getClip())
                {
                    auto it = clipFxPanelOpenByClipId_.find(restoredClip->getID());
                    clipPropertiesWindow_->setFxPanelOpen(it != clipFxPanelOpenByClipId_.end() ? it->second : false);
                }
                clipPropertiesWindow_->toFront(true);
                if (bubbleTaskbar_)
                    bubbleTaskbar_->setWindowMinimized("clipproperties", false);
            },
            [this]
            {
                if (!clipPropertiesWindow_)
                    return;

                const auto activeClipId = clipPropertiesWindow_->getClipId();
                if (activeClipId.isNotEmpty())
                    clipFxPanelOpenByClipId_[activeClipId] = clipPropertiesWindow_->isFxPanelOpen();

                clipPropertiesWindow_->setFxPanelOpen(false);
                clipPropertiesWindow_->setVisible(false);
                if (bubbleTaskbar_)
                    bubbleTaskbar_->unregisterWindow("clipproperties");
            },
            {}, {}, "Track: " + trackTitle + "\nClip: " + clipTitle);
            bubbleTaskbar_->setWindowMinimized("clipproperties", true);
        }
    };
    clipPropertiesWindow_->onRestored = [this]
    {
        if (clipPropertiesWindow_)
        {
            const auto clipId = clipPropertiesWindow_->getClipId();
            if (clipId.isNotEmpty())
            {
                auto it = clipFxPanelOpenByClipId_.find(clipId);
                clipPropertiesWindow_->setFxPanelOpen(it != clipFxPanelOpenByClipId_.end() ? it->second : false);
            }
        }
        if (bubbleTaskbar_)
            bubbleTaskbar_->setWindowMinimized("clipproperties", false);
    };
    clipPropertiesWindow_->onCloseClicked = [this]
    {
        if (clipPropertiesWindow_)
        {
            const auto clipId = clipPropertiesWindow_->getClipId();
            if (clipId.isNotEmpty())
                clipFxPanelOpenByClipId_[clipId] = clipPropertiesWindow_->isFxPanelOpen();

            clipPropertiesWindow_->setFxPanelOpen(false);
            clipPropertiesWindow_->setVisible(false);
        }
        if (bubbleTaskbar_)
            bubbleTaskbar_->unregisterWindow("clipproperties");
    };
    addAndMakeVisible(clipPropertiesWindow_.get());

    // Mixer (the only panel that should be open/visible by default)
    mixerPanel_ = std::make_unique<DAW::MixerPanel>(appCore_.getTrackManager(), appCore_.getFaderRangeCore());
    mixerPanel_->setTransportActive(appCore_.getTransport().isPlaying()
                                     || appCore_.getTransport().isRecording());
    mixerPanel_->bindBubblegumV2(&appCore_.getBubblegumV2());
    mixerPanel_->bindFolderBus(&appCore_.getFolderBus());
    mixerPanel_->bindRouting(&appCore_.getRoutingGraph(),
                              nullptr); // MasterRouteStateCore owned inside BubblegumV2System; pass null for now

    // Master strip wire-up: ceiling, dither, post-meter, phase/width
    mixerPanel_->bindMasterEngines(&appCore_.getMasterBus().getCeiling(),
                                   &appCore_.getMasterBus().getDither(),
                                   &appCore_.getMasterBus().getPostMeter(),
                                   &appCore_.getControlRoom().getPhaseWidth());

    monitorSection_ = std::make_unique<DAW::MonitorSectionUI>(appCore_.getControlRoom());
    addChildComponent(monitorSection_.get());

    mixerViewport_.setViewedComponent(mixerPanel_.get(), false);
    mixerPanel_->setViewport(&mixerViewport_);
    mixerViewport_.setScrollBarsShown(false, true);
    mixerViewport_.setScrollBarThickness(14);
    mixerViewport_.setVisible(true);
    addChildComponent(mixerViewport_);

    mixerWindow_ = std::make_unique<DAW::MixerWindow>();
    mixerWindow_->setContent(&mixerViewport_);
    mixerWindow_->setVisible(true);
    addChildComponent(mixerWindow_.get());
    mixerWindow_->addComponentListener(this);

    // The mixer scrollbars must never take keyboard focus: a focused ScrollBar
    // consumes arrow keys for scrolling before MainComponent ever sees them.
    // Mouse interaction is unaffected — only keyboard focus is disabled.
    mixerViewport_.getHorizontalScrollBar().setWantsKeyboardFocus(false);
    mixerViewport_.getVerticalScrollBar().setWantsKeyboardFocus(false);
    mixerWindow_->onCloseClicked = [this]
    {
        mixerWindow_->setVisible(false);
        if (mixerBubble_) mixerBubble_->setVisible(true);
        if (slimeSidePanel_ && slimeSidePanel_->isAttached())
            slimeSidePanel_->setVisible(false);
    };
    mixerWindow_->onDragEnded = [this]
    {
        mixerWindowPlaced_ = true;
        cableOverlay_.notifyMotionFinished();
        refreshBubblegumOffscreenState();
    };

    // Reset Size: restore the default six-normal-strip + Master size and the safe default
    // position (below the app header). Works from any current size/position.
    mixerWindow_->onResetSizeClicked = [this]
    {
        if (!mixerWindow_ || !mixerPanel_) return;
        setDefaultMixerWindowBounds();
        cableOverlay_.notifyMotion();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
        if (slimeSidePanel_ && mixerPanel_)
            updateSlimeAttachedPosition();
    };

    cableOverlay_.bind(&appCore_.getBubblegumV2(), mixerPanel_.get(), &mixerViewport_);
    mixerViewport_.getHorizontalScrollBar().addListener(this);
    mixerPanel_->onCableRepaintNeeded = [this]
    {
        cableOverlay_.notifyMotion();
        cableOverlay_.repaint();
    };

    // The panel owns its complete content width.  A floating window is sized
    // once to the six-normal-strip + Master default; later track rebuilds must
    // update the viewport extent without overriding a user's window resize.
    if (!mixerDocked_ && mixerWindow_ && mixerPanel_)
        setDefaultMixerWindowBounds();

    // Re-enable mixer titlebar toolbar buttons for FX Chain + Plugin Browser
    mixerWindow_->onSidePanelClicked = [this]
    {
        if (!slimeSidePanel_ || !mixerWindow_ || !mixerPanel_)
            return;

        slimeSidePanel_->toggle();
        mixerWindow_->sidePanelOpen = slimeSidePanel_->isOpen();
        mixerWindow_->refreshToolbar();
        resized();
    };

    mixerWindow_->onBrowserClicked = [this]
    {
        if (!browserChrome_ || !mixerWindow_ || !mixerPanel_)
            return;

        const bool wasVisible = browserChrome_->isVisible();
        mixerPanel_->toggleBrowser();
        if (!wasVisible && pluginBrowser_) pluginBrowser_->refresh();
        mixerWindow_->browserOpen = browserChrome_->isVisible();
        mixerWindow_->refreshToolbar();
    };
    addAndMakeVisible(cableOverlay_);
    // Drive offscreen bubble/panel updates at the same 60 Hz cadence as cable motion.
    cableOverlay_.onTick = [this]() {     if (loadingOverlay_)
        loadingOverlay_->setBounds(getLocalBounds());
    if (startupPanel_)
        startupPanel_->setBounds(getLocalBounds());

    refreshBubblegumOffscreenState();
    };
    addAndMakeVisible(offscreenEndpoint_);
    offscreenEndpoint_.toFront(false);

    // Bind offscreen endpoint
    offscreenEndpoint_.bind(&appCore_.getBubblegumV2(), mixerPanel_.get());
    offscreenEndpoint_.onScrollToTrack = [this](const DAW::TrackID& trackId)
    {
        scrollMixerViewportToTrack(trackId);
    };
    offscreenEndpoint_.onToggleSend = [this](const DAW::TrackID& targetId)
    {
        auto& bgV2 = appCore_.getBubblegumV2();
        bgV2.handleTargetTap(targetId);
        refreshBubblegumOffscreenState();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        cableOverlay_.repaint();
    };
    offscreenEndpoint_.onDeleteSend = [this](const DAW::TrackID& targetId)
    {
        appCore_.getBubblegumV2().removeSend(targetId);
        refreshBubblegumOffscreenState();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        cableOverlay_.repaint();
    };

    mixerWindow_->onCableToggleClicked = [this]
    {
        if (mixerPanel_)
            mixerPanel_->toggleCableForceVisible();
        if (mixerWindow_ && mixerPanel_)
        {
            mixerWindow_->cableVisible = mixerPanel_->areCablesVisible();
            mixerWindow_->refreshToolbar();
        }
        cableOverlay_.repaint();
    };

    pluginSidePanel_ = std::make_unique<DAW::MixerPluginSidePanel>(appCore_.getPluginScanner());
    pluginSidePanel_->setTrackManager(&appCore_.getTrackManager());

    // Arranger, docked Mixer, and Floating Mixer all feed the same canonical
    // MultiSelectionCore state.  MixerPanel is the shared content component for
    // both mixer presentations, so there is deliberately no floating-only
    // selection model.
    mixerPanel_->onTrackSelected = [this](const DAW::TrackID& id)
    {
        handleTrackSelection(id, {});
    };
    mixerPanel_->onTrackSelectedWithModifiers = [this](const DAW::TrackID& id,
                                                        const juce::ModifierKeys& mods)
    {
        handleTrackSelection(id, mods);
    };
    mixerPanel_->onGetIsMultiSelected = [this](const DAW::TrackID& id) -> bool
    {
        return multiSelection_.hasMultiple(DAW::SelectionKind::Track)
            && multiSelection_.contains(DAW::SelectionTarget::track(id));
    };
    mixerPanel_->onGetMultiSelectedTrackIds = [this]() -> std::vector<DAW::TrackID>
    {
        return getSelectedTrackIdsInProjectOrder();
    };

    mixerPanel_->onTrackLensRequested = [this](const DAW::TrackID& id)
    {
        // Toggle: if already visible for this track, hide it; otherwise show
        if (peakBubble_.isVisible())
        {
            peakBubble_.setVisible(false);
            return;
        }
        appCore_.getState().selectedTrackID.setValue(id);
        peakBubble_.setCloseToBubble(false);
        peakBubble_.requestShowForCurrentSelection();
        peakBubble_.toFront(false);
    };

    mixerPanel_->onResizeDrag = [this](int deltaY)
    {
        const int maxDockedMixerH = juce::jmax(kMixerHMin,
            getHeight() - (kMenuH + kTransportH + 120));
        int newH = juce::jlimit(kMixerHMin, juce::jmin(kMixerHMax, maxDockedMixerH), mixerH_ + deltaY);
        if (newH == mixerH_) return;
        mixerH_ = newH;
        // Only relayout mixer + arrangement, skip full resized()
        auto b = getLocalBounds();
        b.removeFromTop(kMenuH + kTransportH);
        auto mixerArea = b.removeFromBottom(mixerH_);

        // Monitor section to the right of mixer
        auto monitorArea = mixerArea.removeFromRight(DAW::MonitorSectionUI::kPreferredWidth);
        monitorSection_->setBounds(monitorArea);

        mixerViewport_.setBounds(mixerArea);
        // MixerPanel::rebuildStrips() is the content-width authority.  Only
        // update the height here; never replace the Master-aware width with a
        // normal-strip-only estimate.
        mixerPanel_->setSize(mixerPanel_->getWidth(), mixerArea.getHeight());
        if (trackListW_ > 0 && trackListWindow_ && trackListWindow_->isVisible()
            && !trackListWindow_->isMaximized())
            trackListWindow_->setBounds(b.removeFromLeft(trackListW_));
        arrangement_->setBounds(b);
        mixerViewport_.toFront(false);
        cableOverlay_.toFront(false);
    };
    mixerPanel_->onUndockRequested = [this] { undockMixer(); };
    mixerPanel_->onDockRequested   = [this] { dockMixer(); };
    mixerPanel_->onToggleSidePanel = [this](bool show)
    {
        if (!slimeSidePanel_)
            return;

        auto beforeOpen = slimeSidePanel_->isOpen();
        if (show)
        {
            slimeSidePanel_->setAnimationState(1.0f);
            DBG("[MainComponent] Slime side panel opened via mixer toggle");
        }
        else
        {
            slimeSidePanel_->setAnimationState(0.0f);
            DBG("[MainComponent] Slime side panel closed via mixer toggle");
        }
        resized();
        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::onToggleSidePanel"
                " beforeOpen=" + juce::String(beforeOpen ? 1 : 0)
                + " afterOpen=" + juce::String(slimeSidePanel_->isOpen() ? 1 : 0));
    };
    mixerPanel_->onTogglePluginBrowser = [this](bool show)
    {
        if (!browserChrome_)
            return;

        auto beforeVisible = browserChrome_->isVisible();
        if (show)
        {
            browserChrome_->restorePanel();
            if (pluginBrowser_) pluginBrowser_->refresh();
            DBG("[MainComponent] FX browser opened via mixer toggle");
        }
        else
        {
            browserChrome_->closePanel();
            DBG("[MainComponent] FX browser closed via mixer toggle");
        }
        resized();
        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::onTogglePluginBrowser"
                " beforeVisible=" + juce::String(beforeVisible ? 1 : 0)
                + " afterVisible=" + juce::String(browserChrome_->isVisible() ? 1 : 0)
                + " panelState=" + juce::String((int)browserChrome_->getState())
                + " bounds=" + browserChrome_->getBounds().toString()
                + " browserItemCount=" + juce::String(pluginBrowser_ ? pluginBrowser_->getVisiblePluginItemCount() : 0));
    };
    mixerPanel_->onCreateBus = [this]
    {
        auto& tm = appCore_.getTrackManager();
        DAW::CommandManager::getInstance().execute(std::make_unique<DAW::AddTrackCommand>(
            tm, "Bus " + juce::String(tm.getNumTracks() + 1), DAW::TrackRole::Bus));
    };

    mixerPanel_->onCreateFolderBus = [this]
    {
        auto& tm = appCore_.getTrackManager();
        auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
        if (!masterRoute) return;
        executeTopologyCommand([&]
        {
            const auto fbId = appCore_.getFolderBus().createFolderBus(
                "Folder " + juce::String(tm.getNumTracks() + 1), {}, {},
                appCore_.getRoutingGraph(), *masterRoute, tm);

            // createFolderBus() appends empty folder buses at the end of the
            // track list. When a track is selected, insert the new bus right
            // after it so the folder appears where the user is working
            // instead of at the bottom of the list.
            if (fbId.isNotEmpty())
            {
                const auto selectedId = appCore_.getState().selectedTrackID.getValue().toString();
                if (selectedId.isNotEmpty())
                {
                    const int selectedIndex = tm.getTrackIndex(selectedId);
                    if (selectedIndex >= 0 && selectedIndex < tm.getNumTracks())
                    {
                        const int targetIndex = juce::jlimit(0, tm.getNumTracks() - 1, selectedIndex + 1);
                        if (tm.getTrackIndex(fbId) != targetIndex)
                            tm.moveTrack(fbId, targetIndex);
                    }
                }
            }
        }, "Create Folder Bus");
    };
    mixerPanel_->onDeleteTrack = [this](const DAW::TrackID& id)
    {
        deleteTracksAsOneTopologyTransaction({ id });
    };
    mixerPanel_->onMultiDeleteTrack = [this](const DAW::TrackID& /*id*/)
    {
        auto& tm = appCore_.getTrackManager();
        std::vector<DAW::TrackID> toDelete;
        for (const auto& selectedId : getSelectedTrackIdsInProjectOrder())
            if (tm.canDeleteTrack(selectedId))
                toDelete.push_back(selectedId);
        if (toDelete.empty())
            return;

        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Selected Tracks")
            .withMessage("Delete " + juce::String(toDelete.size()) + " selected tracks?")
            .withButton("Delete")
            .withButton("Cancel")
            .withAssociatedComponent(this);
        juce::AlertWindow::showAsync(options, [safeThis, toDelete](int result)
        {
            if (result == 1 && safeThis)
                safeThis->deleteTracksAsOneTopologyTransaction(toDelete);
        });
    };
    mixerPanel_->onConvertFolderToTrack = [this](const DAW::TrackID& id)
    {
        convertFolderBusToRegularTrack(id);
    };
    mixerPanel_->onToggleTrackMute = [this](const DAW::TrackID& id, bool muted)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Muted,
                track->isMuted(), muted, muted ? "Mute Track" : "Unmute Track"));
    };
    mixerPanel_->onToggleTrackSolo = [this](const DAW::TrackID& id, bool soloed)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Soloed,
                track->isSoloed(), soloed, soloed ? "Solo Track" : "Unsolo Track"));
    };
    mixerPanel_->onToggleTrackArm = [this](const DAW::TrackID& id)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Armed,
                track->isArmed(), !track->isArmed(), "Toggle Track Arm"));
    };
    mixerPanel_->onSetTrackMonitoring = [this](const DAW::TrackID& id, bool enabled)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Monitoring,
                track->isMonitoring(), enabled, enabled ? "Enable Monitoring" : "Disable Monitoring"));
    };
    mixerPanel_->onSetTrackRole = [this](const DAW::TrackID& id, DAW::TrackRole role)
    {
        if (auto* track = appCore_.getTrackManager().getTrack(id))
        {
            if (track->getRole() != role)
                DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                    appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Role,
                    (int) track->getRole(), (int) role, "Change Track Type"));
        }
    };
    mixerPanel_->onOpenTrackPianoRoll = [this](const DAW::TrackID& id)
    {
        for (auto* clip : appCore_.getClipManager().getClipsOnTrack(id))
        {
            if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
            {
                openPianoRollForClip(*midiClip);
                return;
            }
        }
    };
    mixerPanel_->onOpenInputTrimPanel = [this](const DAW::TrackID& id)
    {
        if (inputTrimPanelManager_)
            if (auto* track = appCore_.getTrackManager().getTrack(id))
                inputTrimPanelManager_->showForTrack(*track);
    };
    mixerPanel_->onCommitTrackVolume = [this](const DAW::TrackID& id, float oldGain, float newGain)
    {
        // MixerStrip supplies canonical Track::getVolume() linear gain.  The
        // command's Volume property deliberately remains in that same domain;
        // display dB values must never cross this boundary.
        if (std::abs(oldGain - newGain) > 0.0001f)
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Volume,
                (double) oldGain, (double) newGain, "Adjust Track Volume", true));
    };
    mixerPanel_->onCommitTrackPan = [this](const DAW::TrackID& id, float oldValue, float newValue)
    {
        if (std::abs(oldValue - newValue) > 0.0001f)
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Pan,
                (double) oldValue, (double) newValue, "Adjust Track Pan", true));
    };
    mixerPanel_->onCreateFolderFromDrop = [this](const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
    {
        handleFolderDropRequest(draggedId, targetId);
    };
    mixerPanel_->onFolderDropRequested = [this](const DAW::TrackID& draggedId,
                                                const DAW::TrackID& targetId,
                                                DAW::DragMode mode)
    {
        handleMixerFolderDropRequest(draggedId, targetId, mode);
    };
    mixerPanel_->onTrackReorderRequested = [this](const DAW::TrackID& trackId, int targetIndex)
    {
        moveTracksWithFolderAwareness({ trackId }, targetIndex);
        refreshBubblegumFeedback();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    };
    mixerPanel_->onMultiTrackReorderRequested = [this](const std::vector<DAW::TrackID>& selectedIds,
                                                       int targetIndex)
    {
        moveTracksWithFolderAwareness(selectedIds, targetIndex);
        refreshBubblegumFeedback();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    };
    mixerPanel_->onFolderCollapseChanged = [this](const DAW::TrackID& folderTrackId, bool expanded)
    {
        setFolderCollapsed(folderTrackId, !expanded);
    };

    mixerPanel_->onRescanPlugins = [this]
    {
        // Route the rescan through the dialog so progress + completion callbacks
        // are wired to the visible loading bar. startScan(true) performs the same
        // "wipe all scan state" clean rescan that cleanFullRescan() did, but with
        // the dialog as the single scan-start owner.
        if (scanDialog_)
        {
            scanDialog_->setBounds(getLocalBounds());
            scanDialog_->setVisible(true);
            scanDialog_->toFront(true);
            scanDialog_->startScan(true);
        }
    };
    mixerPanel_->onPluginDropCopy = [this](const DAW::TrackID& srcTrack, int srcSlot,
                                            const DAW::TrackID& destTrack)
    {
        if (copyPluginBetweenTracks(srcTrack, srcSlot, destTrack))
            DBG("[MainComponent] Cross-track plugin copy via mixer strip: src=" + srcTrack
                + " slot=" + juce::String(srcSlot) + " -> dest=" + destTrack);
    };
    pluginSidePanel_->onPluginEditorOpened = [this](DAW::PluginInstanceCore* slot,
                                                      const DAW::TrackID& trackId, int slotIdx)
    {
        if (!slot || !bubbleTaskbar_) return;
        auto windowId = "plugin_" + trackId + "_" + juce::String(slotIdx);
        auto pluginName = slot->getName();

        // Build a descriptive label: "TrackName - PluginName"
        juce::String trackLabel;
        if (auto* track = appCore_.getTrackManager().getTrack(trackId))
            trackLabel = track->getName();
        else if (trackId == "master" || appCore_.getTrackManager().hasMasterTrack())
            trackLabel = "Master";
        else
            trackLabel = trackId;
        auto bubbleTitle = pluginName;
        auto bubbleSubtitle = "Track FX - Track: " + trackLabel + "\nSlot: " + juce::String(slotIdx + 1);

        slot->onEditorMinimized = [this, windowId, trackId, slotIdx, slot]
        {
            if (!bubbleTaskbar_ || slot == nullptr)
                return;

            juce::String trackLabel;
            if (auto* track = appCore_.getTrackManager().getTrack(trackId))
                trackLabel = track->getName();
            else if (trackId == "master" || appCore_.getTrackManager().hasMasterTrack())
                trackLabel = "Master";
            else
                trackLabel = trackId;

            bubbleTaskbar_->registerWindow(windowId, slot->getName(), "Plugins", [slot, windowId, this]
            {
                slot->showEditor();
                if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowId, false);
                DBG("[MainComponent] Plugin editor restored from bubble: " + windowId);
            },
            [slot, windowId, this]
            {
                slot->closeEditor();
                if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
                DBG("[MainComponent] Plugin editor closed from bubble X: " + windowId);
            },
            trackId, slot->getName(), "Track FX - Track: " + trackLabel + "\nSlot: " + juce::String(slotIdx + 1));
            bubbleTaskbar_->setWindowMinimized(windowId, true);
            bubbleTaskbar_->notifyWindowMinimized(windowId);
        };
        slot->onEditorClosed = [this, windowId]
        {
            if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
        };

        bubbleTaskbar_->registerWindow(windowId, bubbleTitle, "Plugins", [slot, windowId, this]
        {
            slot->showEditor();
            if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowId, false);
            DBG("[MainComponent] Plugin editor restored from bubble: " + windowId);
        },
        [slot, windowId, this]
        {
            slot->closeEditor();
            if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
            DBG("[MainComponent] Plugin editor closed from bubble X: " + windowId);
        },
        trackId, pluginName, bubbleSubtitle);
    };

    pluginSidePanel_->onPluginDragCopy = [this](const DAW::TrackID& srcTrack, int srcSlot,
                                                  const DAW::TrackID& destTrack)
    {
        copyPluginBetweenTracks(srcTrack, srcSlot, destTrack);
    };
    pluginSidePanel_->onPluginChainEditRequested = [this](DAW::PluginChainCore& chain, std::function<void()> edit, const juce::String& description)
    {
        auto before = chain.getState();
        edit();
        auto after = chain.getState();
        if (!before.isEquivalentTo(after))
            CommandManager::getInstance().execute(std::make_unique<PluginChainStateCommand>(
                chain, appCore_.getPluginScanner().getFormatManager(), before, after, description, true));
    };

    pluginSidePanel_->onSlotClickedWithModifiers = [this](int slotIndex, const juce::ModifierKeys& mods)
    {
        auto trackId = appCore_.getState().selectedTrackID.getValue().toString();
        if (trackId.isEmpty()) return;

        auto target = DAW::SelectionTarget::pluginSlot(trackId, slotIndex);
        const bool ctrl = mods.isCtrlDown() || mods.isCommandDown();
        const bool shift = mods.isShiftDown();

        if (ctrl)
        {
            multiSelection_.toggle(target);
        }
        else if (shift)
        {
            auto* chain = appCore_.getPluginChain(trackId);
            if (!chain) { multiSelection_.selectSingle(target); }
            else
            {
                std::vector<DAW::SelectionTarget> ordered;
                for (int i = 0; i < chain->getNumSlots(); ++i)
                    ordered.push_back(DAW::SelectionTarget::pluginSlot(trackId, i));
                multiSelection_.selectRange(target, ordered, false);
            }
        }
        else
        {
            multiSelection_.selectSingle(target);
        }

        // Sync visual selection on slot widgets
        if (pluginSidePanel_)
        {
            pluginSidePanel_->clearSlotSelection();
            for (const auto& t : multiSelection_.getSelected(DAW::SelectionKind::PluginSlot))
                if (t.trackId == trackId)
                    pluginSidePanel_->setSlotSelected(t.pluginSlotIndex, true);
        }
    };
    // NOTE: pluginSidePanel_ is parented inside slimeSidePanel_

    // Wire side panel header button callbacks (X close, detach toggle)
    pluginSidePanel_->onCloseRequested = [this]
    {
        if (slimeSidePanel_)
            slimeSidePanel_->closePanel();
    };
    pluginSidePanel_->onDetachToggleRequested = [this]
    {
        if (slimeSidePanel_)
        {
            slimeSidePanel_->toggleAttachment();
            // Immediately re-attach position when switching back to attached mode
            if (slimeSidePanel_->isAttached())
                updateSlimeAttachedPosition();
        }
    };
    pluginSidePanel_->isSlimeAttached = [this]() -> bool
    {
        return slimeSidePanel_ ? slimeSidePanel_->isAttached() : true;
    };

    // Slime Side Panel — organic slime extension of the mixer edge
    slimeSidePanel_ = std::make_unique<DAW::SlimeSidePanel>(pluginSidePanel_.get());
    slimeSidePanel_->onStateChanged = [this](bool open)
    {
        DBG("[MainComponent] Slime side panel state changed: open=" + juce::String((int)open));
        if (mixerWindow_)
            mixerWindow_->sidePanelOpen = open;
        if (bubbleTaskbar_ && !open)
            bubbleTaskbar_->unregisterWindow("sidepanel");
        resized();
    };
    addAndMakeVisible(slimeSidePanel_.get());

    // Plugin browser panel - categorized FX explorer
    pluginBrowser_ = std::make_unique<DAW::PluginBrowserPanel>(appCore_.getPluginScanner());
    auto handleBrowserPlugin = [this](const juce::PluginDescription& desc,
                                      DAW::PluginExecutionMode executionMode)
    {
        if (executionMode == DAW::PluginExecutionMode::Sandboxed
            && ! DAW::PluginHostingProductPolicyCore::kSandboxUserFeatureEnabled)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Plugin Hosting Unavailable",
                DAW::PluginHostingProductPolicyCore::kSandboxDisabledMessage);
            return;
        }

        auto selectedId = appCore_.getState().selectedTrackID.getValue().toString();
        if (selectedId.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "No Track Selected",
                "Select a track first to add plugins.");
            return;
        }

        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::pluginBrowserItemClicked"
                " plugin=\"" + desc.name + "\""
                + " manufacturer=\"" + desc.manufacturerName + "\""
                + " selectedTrack=\"" + selectedId + "\""
                + " executionMode=\""
                + (executionMode == DAW::PluginExecutionMode::Sandboxed
                    ? "sandboxed" : "in_process") + "\"");

        if (selectedId.isEmpty())
        {
            DAW::PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MainComponent::pluginBrowserItemClicked aborted noSelectedTrack");
            return;
        }

        auto* chain = appCore_.getPluginChain(selectedId);
        if (!chain)
        {
            DAW::PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MainComponent::pluginBrowserItemClicked aborted nullChain"
                    " selectedTrack=\"" + selectedId + "\"");
            return;
        }

        auto before = chain->getState();
        juce::String err;
        DAW::PluginInsertOptions insertOptions;
        insertOptions.executionMode = executionMode;
        auto slotIndex = chain->appendPlugin(
            desc, insertOptions, appCore_.getPluginScanner().getFormatManager(), err);
        if (slotIndex < 0)
        {
            DAW::PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MainComponent::pluginLoadFailure"
                    " plugin=\"" + desc.name + "\""
                    + " selectedTrack=\"" + selectedId + "\""
                    + " error=\"" + err + "\"");
            if (err.isNotEmpty())
                DBG("Plugin load error: " + err);
            return;
        }

        auto after = chain->getState();
        if (!before.isEquivalentTo(after))
            CommandManager::getInstance().execute(std::make_unique<PluginChainStateCommand>(
                *chain, appCore_.getPluginScanner().getFormatManager(), before, after,
                executionMode == DAW::PluginExecutionMode::Sandboxed
                    ? "Add Sandboxed Plugin" : "Add Plugin", true));

        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::pluginLoadSuccess"
                " plugin=\"" + desc.name + "\""
                + " selectedTrack=\"" + selectedId + "\""
                + " slotIndex=" + juce::String(slotIndex)
                + " chainSlotCount=" + juce::String(chain->getNumSlots())
                + " executionMode=\""
                + (executionMode == DAW::PluginExecutionMode::Sandboxed
                    ? "sandboxed" : "in_process") + "\"");

        if (pluginSidePanel_)
        {
            auto* track = appCore_.getTrackManager().getTrack(selectedId);
            if (track) pluginSidePanel_->setTrack(track, chain);
        }
        if (slimeSidePanel_ && !slimeSidePanel_->isOpen())
        {
            slimeSidePanel_->setAnimationState(1.0f);
            DBG("[MainComponent] Slime side panel auto-shown after plugin add from browser");
        }

        if (auto* slot = chain->getSlot(slotIndex))
        {
            DAW::PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MainComponent::pluginEditorOpenAttempt"
                    " plugin=\"" + slot->getName() + "\""
                    + " hasEditor=" + juce::String(slot->hasEditor() ? 1 : 0));

            auto windowId = "plugin_" + selectedId + "_" + juce::String(slotIndex);
            auto pluginName = slot->getName();

            // Build descriptive label for bubble
            juce::String trackLabel;
            if (auto* trk = appCore_.getTrackManager().getTrack(selectedId))
                trackLabel = trk->getName();
            else
                trackLabel = selectedId;
            auto bubbleTitle = pluginName;
            auto bubbleSubtitle = "Track FX - Track: " + trackLabel + "\nBrowser Insert"
                + (executionMode == DAW::PluginExecutionMode::Sandboxed
                    ? " (Sandboxed)" : "");

            slot->onEditorMinimized = [this, windowId, selectedId, slotIndex, slot, executionMode]
            {
                if (!bubbleTaskbar_ || slot == nullptr)
                    return;

                juce::String trackLabel;
                if (auto* trk = appCore_.getTrackManager().getTrack(selectedId))
                    trackLabel = trk->getName();
                else
                    trackLabel = selectedId;

                bubbleTaskbar_->registerWindow(windowId, slot->getName(), "Plugins", [slot, windowId, this]
                {
                    slot->showEditor();
                    if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowId, false);
                    DBG("[MainComponent] Plugin editor restored from bubble (browser flow): " + windowId);
                },
                [slot, windowId, this]
                {
                    slot->closeEditor();
                    if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
                    DBG("[MainComponent] Plugin editor closed from bubble X (browser flow): " + windowId);
                },
                 selectedId, slot->getName(),
                 "Track FX - Track: " + trackLabel + "\nBrowser Insert"
                     + (executionMode == DAW::PluginExecutionMode::Sandboxed
                         ? " (Sandboxed)" : ""));
                bubbleTaskbar_->setWindowMinimized(windowId, true);
                bubbleTaskbar_->notifyWindowMinimized(windowId);
            };
            slot->onEditorClosed = [this, windowId]
            {
                if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
            };

            if (bubbleTaskbar_)
            {
                bubbleTaskbar_->registerWindow(windowId, bubbleTitle, "Plugins", [slot, windowId, this]
                {
                    slot->showEditor();
                    if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized(windowId, false);
                    DBG("[MainComponent] Plugin editor restored from bubble (browser flow): " + windowId);
                },
                [slot, windowId, this]
                {
                    slot->closeEditor();
                    if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow(windowId);
                    DBG("[MainComponent] Plugin editor closed from bubble X (browser flow): " + windowId);
                },
                selectedId, pluginName, bubbleSubtitle);
            }

            juce::Logger::writeToLog("[PluginOpenSource] source=FXBrowser plugin=\"" + slot->getName()
                + "\" track=\"" + selectedId + "\" slotIndex=" + juce::String(slotIndex)
                + " descValid=" + juce::String(desc.name.isNotEmpty() ? 1 : 0)
                + " slotValid=1 parentValid=" + juce::String(this != nullptr ? 1 : 0));
            slot->openEditor(this);
            DAW::PluginScanAuditLogCore::appendLine(
                "plugin_ui_flow.log",
                "MainComponent::pluginEditorOpenResult"
                    " plugin=\"" + slot->getName() + "\""
                    + " editorOpen=" + juce::String(slot->isEditorOpen() ? 1 : 0));
        }
    
        resized();
    };
    pluginBrowser_->onPluginSelected = [handleBrowserPlugin](const juce::PluginDescription& desc)
    {
        handleBrowserPlugin(desc, DAW::PluginExecutionMode::InProcess);
    };
    pluginBrowser_->onPluginSelectedSandboxed = [handleBrowserPlugin](const juce::PluginDescription& desc)
    {
        handleBrowserPlugin(desc, DAW::PluginExecutionMode::Sandboxed);
    };
    // NOTE: pluginBrowser_ is parented inside browserChrome_ (not directly on MainComponent)

    // Wrap browser panel in floating chrome (X / _ / ? + drag)
    browserChrome_ = std::make_unique<DAW::FloatingPanelChrome>("Plugin Browser", pluginBrowser_.get());
    browserChrome_->onClosed = [this]
    {
        DBG("[MainComponent] FX browser close pressed");
        if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow("fxbrowser");
        resized();
    };
    browserChrome_->onMinimized = [this]
    {
        DBG("[MainComponent] FX browser minimize pressed -> bubble");
        if (bubbleTaskbar_)
        {
            bubbleTaskbar_->registerWindow("fxbrowser", "Plugin Browser", "Panels", [this]
            {
                if (browserChrome_)
                {
                    browserChrome_->restorePanel();
                    if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized("fxbrowser", false);
                    DBG("[MainComponent] FX browser restored from bubble");
                    resized();
                }
            },
            [this]
            {
                // Direct close from bubble X
                if (browserChrome_) browserChrome_->closePanel();
                DBG("[MainComponent] FX browser closed from bubble X");
            });
            bubbleTaskbar_->setWindowMinimized("fxbrowser", true);
            DBG("[MainComponent] Minimized bubble entry created for FX browser");
        }
        resized(); // Collapse layout space immediately
    };
    browserChrome_->onMaximized = [this]
    {
        DBG("[MainComponent] FX browser maximized");
        resized();
    };
    browserChrome_->onRestored = [this]
    {
        DBG("[MainComponent] FX browser restored state=Open");
        resized();
    };
    browserChrome_->onMoved = [this]
    {
        DBG("[MainComponent] FX browser moved to " + browserChrome_->getBounds().toString());
    };
    addChildComponent(browserChrome_.get());

    // Wire TransportBar quick-access buttons (shared from mixer + timeline)
    auto quickAccessSidePanel = [this]
    {
        if (!slimeSidePanel_) return;
        auto wasOpen = slimeSidePanel_->isOpen();
        DBG("[MainComponent] Quick access Slime Side Panel toggle, wasOpen=" + juce::String((int)wasOpen));
        slimeSidePanel_->toggle();
        if (bubbleTaskbar_ && wasOpen)
            bubbleTaskbar_->setWindowMinimized("sidepanel", false);
        DBG("[MainComponent] Slime side panel toggled via quick access, nowOpen=" + juce::String((int)slimeSidePanel_->isOpen()));
        resized();
    };
    auto quickAccessFxBrowser = [this]
    {
        if (!browserChrome_) return;
        auto stateBefore = browserChrome_->getState();
        DBG("[MainComponent] Quick access FX Browser toggle, stateBefore=" + juce::String((int)stateBefore));
        if (browserChrome_->isVisible())
        {
            // Panel is open ? close it (toggle off)
            browserChrome_->closePanel();
            DBG("[MainComponent] FX browser closed via quick access toggle");
        }
        else
        {
            // Panel is closed/minimized ? open it
            browserChrome_->restorePanel();
            if (pluginBrowser_) pluginBrowser_->refresh();
            if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized("fxbrowser", false);
            DBG("[MainComponent] FX browser opened via quick access toggle, stateAfter=" + juce::String((int)browserChrome_->getState()));
        }
        resized();
    };
    transportBar_->onQuickAccessSidePanel = quickAccessSidePanel;
    transportBar_->onQuickAccessFxBrowser = quickAccessFxBrowser;

    // Wire same quick-access to ArrangementView timeline toolbar
    if (arrangement_)
    {
        arrangement_->onQuickAccessSidePanel = quickAccessSidePanel;
        arrangement_->onQuickAccessFxBrowser = quickAccessFxBrowser;
    }

    // Startup scan dialog
    scanDialog_ = std::make_unique<DAW::PluginScanStartupDialog>(appCore_.getPluginScanner());
    scanDialog_->onScanComplete = [this]
    {
        if (pluginBrowser_) pluginBrowser_->refresh();
        // Startup should not auto-open panels. Keep everything closed unless the user opens it.
        if (slimeSidePanel_) slimeSidePanel_->setAnimationState(0.0f);
        if (browserChrome_)   browserChrome_->closePanel();
        resized();
        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::scanComplete"
                + juce::String(appCore_.getPluginScanner().getCache().getCount())
                + " knownPluginCount=" + juce::String(appCore_.getPluginScanner().getKnownPlugins().getNumTypes())
                + " browserItemCount=" + juce::String(pluginBrowser_ ? pluginBrowser_->getVisiblePluginItemCount() : 0)
                + " sidePanelVisible=" + juce::String(pluginSidePanel_ && pluginSidePanel_->isVisible() ? 1 : 0)
                + " browserVisible=" + juce::String(pluginBrowser_ && pluginBrowser_->isVisible() ? 1 : 0));

        // Show startup panel on first launch after scan
        if (startupPanel_ && startupPanelMayBeShown_
            && (!loadingOverlay_ || !loadingOverlay_->hasActiveLoad()))
        {
            startupPanel_->setProjectName("APEX");
            startupPanel_->setPluginInfo(
                appCore_.getPluginScanner().getKnownPlugins().getNumTypes(),
                appCore_.getPluginScanner().getFailureStore().getCount());
            startupPanel_->addRecentFile(appCore_.getProjectManager().getProjectFile());
            startupPanel_->setVisible(true);
            startupPanel_->toFront(false);
        }
    };
    addChildComponent(scanDialog_.get());

    // Loading overlay — shown during project load
    loadingOverlay_ = std::make_unique<DAW::ProjectLoadingOverlay>();
    addChildComponent(loadingOverlay_.get());
    appCore_.getProjectManager().setLoadProgressCallback([this](const DAW::ProjectManager::LoadProgress& progress)
    {
        if (loadingOverlay_ == nullptr)
            return;
        loadingOverlay_->setProgress(progress.stage, progress.currentItem,
                                     progress.fraction, progress.determinate);
        // Do not flush the whole peer while the model is between restore
        // stages; unrelated invalid components must never paint partial state.
        loadingOverlay_->repaint();
    });

    // Startup panel — shown after first scan
    startupPanel_ = std::make_unique<DAW::StartupPanel>();
    startupPanel_->onNewProject = [this]
    {
        startupPanel_->dismiss();
        // C6-plugin-registry: the scanned-plugin registry is APPLICATION-GLOBAL
        // state and must NEVER be cleared by a project operation. Clearing it
        // here caused "New Project → all plugins disappear" and persisted the
        // empty list over the valid plugin database.
        if (!appCore_.getProjectManager().newProject())
        {
            juce::Logger::writeToLog("[PROJECT] New Project failed: "
                + appCore_.getProjectManager().getLastLoadError());
            return;
        }
        updateWindowTitle();
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    };
    startupPanel_->onOpenProject = [this]
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Open Project",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.dawproj");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                        self->openProjectFileWithOverlay(file, "Open");
                }
            });
    };
    startupPanel_->onAudioSettings = [this]
    {
        if (startupPanel_)
            startupPanel_->setVisible(false);
        showAudioDevicePanel();
    };
    startupPanel_->onOpenRecent = [this](const juce::File& file)
    {
        if (file.existsAsFile())
            openProjectFileWithOverlay(file, "Open Recent");
    };
    startupPanel_->onUpgradeProject = [this]
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select the project file to upgrade",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.dawproj");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                        self->upgradeProjectFile(file);
                }
            });
    };
    addChildComponent(startupPanel_.get());
    startupPanel_->onRescanPlugins = [this]
    {
        // Don't dismiss startup panel — let user see it close via X or after scan.
        // Route through the dialog so the loading bar receives progress + completion.
        if (scanDialog_)
        {
            scanDialog_->setBounds(getLocalBounds());
            scanDialog_->setVisible(true);
            scanDialog_->toFront(true);
            scanDialog_->startScan(true);
        }
    };

    updateWindowTitle();

    if (appCore_.getTrackManager().getNumTracks() > 0)
    {
        auto firstTrackId = appCore_.getTrackManager().getTrack(0)->getID();
        mixerPanel_->selectTrack(firstTrackId);
            DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::defaultTrackSelection"
                " selectedTrack=\"" + firstTrackId + "\"");
    }

    syncFolderCollapseState();

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]()
    {
        if (auto* self = safeThis.getComponent())
            self->startPluginScanIfNeeded();
    });

    // Force startup layout: Timeline + Mixer only. Everything else must stay closed
    // until the user opens it (no state restore surprises).
    if (slimeSidePanel_) slimeSidePanel_->setAnimationState(0.0f);
    if (browserChrome_)   browserChrome_->closePanel();
    if (pianoRollWindow_) pianoRollWindow_->setVisible(false);
    if (clipPropertiesWindow_) clipPropertiesWindow_->setVisible(false);

   #if JUCE_DEBUG
    // FPS overlay (Debug only)
    addAndMakeVisible(fpsOverlay_);
   #endif
    appendMainStartupTrace("ctor.audio-init-deferred");

    // Bubble taskbar - shows minimized floating windows (not mixer)
    bubbleTaskbar_ = std::make_unique<DAW::BubbleTaskbar>();
    addAndMakeVisible(bubbleTaskbar_.get());

    bubblegumTaskbar_ = std::make_unique<DAW::BubblegumTaskbarComponent>();
    {
        auto cfg = bubblegumTaskbar_->getConfig();
        cfg.paintTitle = false;
        bubblegumTaskbar_->setConfig(cfg);
    }
    addAndMakeVisible(bubblegumTaskbar_.get());

    // Register floating windows with bubble taskbar by category
    bubbleTaskbar_->registerWindow("tracklist", "Track List", "Workspace", [this]
    {
        trackListWindow_->setVisible(true);
        trackListWindow_->restoreIfMinimized();
        trackListW_ = 220;
        bubbleTaskbar_->setWindowMinimized("tracklist", false);
        resized();
    });

    trackListWindow_->onMinimized = [this]
    {
        trackListWindow_->setVisible(false);
        bubbleTaskbar_->setWindowMinimized("tracklist", true);
    };
    trackListWindow_->onRestored = [this]
    {
        bubbleTaskbar_->setWindowMinimized("tracklist", false);
    };

    // Mixer bubble - dedicated Messenger-style bubble for the mixer
    mixerBubble_ = std::make_unique<DAW::MixerBubble>();
    mixerBubble_->setInterceptsMouseClicks(true, true);
    mixerBubble_->onClicked = [this]
    {
        if (!mixerDocked_)
        {
            mixerWindow_->setVisible(true);
            mixerWindow_->restoreIfMinimized();
            mixerWindow_->toFront(false);
            mixerBubble_->setVisible(false);
            if (slimeSidePanel_ && slimeSidePanel_->isAttached())
                slimeSidePanel_->setVisible(true);
        }
    };
    addAndMakeVisible(mixerBubble_.get()); // visible from start - mixer starts in bubble
    mixerBubble_->setBounds(getLocalBounds());
    mixerBubble_->toFront(false);

    mixerWindow_->onMinimized = [this]
    {
        mixerWindow_->setVisible(false);
        mixerBubble_->setVisible(true);
        mixerBubble_->toFront(false);
        if (slimeSidePanel_ && slimeSidePanel_->isAttached())
            slimeSidePanel_->setVisible(false);
        resized();
    };
    mixerWindow_->onRestored = [this]
    {
        mixerBubble_->setVisible(false);
        if (slimeSidePanel_ && slimeSidePanel_->isAttached())
            slimeSidePanel_->setVisible(true);
    };

    // Timeline bubble - shows when timeline window is minimized/closed
    timelineBubble_ = std::make_unique<DAW::TimelineBubble>();
    timelineBubble_->onClicked = [this]
    {
        timelineWindow_->setVisible(true);
        timelineWindow_->restoreIfMinimized();
        timelineWindow_->toFront(false);
        if (trackListWindow_ && trackListW_ > 0)
            trackListWindow_->setVisible(true);
        timelineBubble_->setVisible(false);
        resized();
    };
    addChildComponent(timelineBubble_.get()); // hidden - timeline starts open

    // Master bubble - hub that can store feature bubbles
    masterBubble_ = std::make_unique<DAW::MasterBubble>();
    addChildComponent(masterBubble_.get()); // hidden until a slot is stored

    // Marker bubble - section navigation
    markerBubble_ = std::make_unique<DAW::MarkerBubble>(appCore_.getMarkerManager());
    markerBubble_->onJumpToPosition = [this](DAW::SamplePosition pos)
    {
        appCore_.getTransport().setPosition(pos);
        arrangement_->repaint();
    };
    addChildComponent(markerBubble_.get());
    markerBubble_->setVisible(false);

    // Bubblegum Orb - floating global launcher (primary Bubblegum control)
    bubblegumOrb_ = std::make_unique<DAW::BubblegumOrbComponent>();
    bubblegumOrb_->setBubblegumSystem(&appCore_.getBubblegumV2());

    // Bubblegum V2 floating send panel - lives on MainComponent, not inside mixer
    bubblegumPanel_ = std::make_unique<DAW::BubblegumV2PanelUI>();
    bubblegumPanel_->bind(&appCore_.getBubblegumV2(), &appCore_.getTrackManager());
    bubblegumPanel_->onIsMasterSelected = [this]
    {
        auto& mgr = appCore_.getTrackManager();
        if (!mgr.hasMasterTrack() || !mixerPanel_)
            return false;
        return mixerPanel_->getSelectedTrackId() == mgr.getMasterTrack()->getID();
    };
    appCore_.getBubblegumV2().onTopologyMutationRequested = [this](std::function<void()> mutation, const juce::String& description)
    {
        executeTopologyCommand(std::move(mutation), description);
    };
    appCore_.getBubblegumV2().onContinuousTopologyGestureBegin = [this](const juce::String& description)
    {
        beginContinuousTopologyGesture(description);
    };
    appCore_.getBubblegumV2().onContinuousTopologyGestureCommit = [this](const juce::String& description)
    {
        commitContinuousTopologyGesture(description);
    };
    bubblegumPanel_->onCloseRequested = [this]
    {
        // X button on panel ? deactivate Bubblegum system + collapse to orb
        DBG("[MainComponent] Bubblegum panel X close requested");
        mixerPanel_->deactivateBubblegumMode();
    };
    bubblegumPanel_->onSendChanged = [this]
    {
        // Send toggled in Bubblegum panel ? immediately refresh feedback in mixer + timeline
        auto& bgV2 = appCore_.getBubblegumV2();
        auto srcId = bgV2.sourceSync.getSourceTrackId();
        DBG("[MainComponent] onSendChanged fired -> refreshing feedback, source=" + srcId);
        if (mixerPanel_)
            mixerPanel_->debugLastSendToggle_ = "Src=" + srcId;
        refreshBubblegumFeedback();
    };
    addChildComponent(bubblegumPanel_.get());

    // Wire mixer ? floating panel toggle
    mixerPanel_->onBubblegumToggled = [this](bool active)
    {
        if (active) showBubblegumPanel();
        else        hideBubblegumPanel();

        if (mixerWindow_ && mixerPanel_)
        {
            mixerWindow_->bgActive = active;
            mixerWindow_->cableVisible = mixerPanel_->areCablesVisible();
            mixerWindow_->refreshToolbar();
        }
        cableOverlay_.notifyMotion();
    };

    DBG("[MainComponent] Bubblegum instance audit:"
        " orbCreated=1"
        " bgV2System=0x" + juce::String::toHexString((juce::pointer_sized_int)&appCore_.getBubblegumV2())
        + " mixerPanel_bgV2_bound=" + juce::String(mixerPanel_ != nullptr ? 1 : 0)
        + " routingGraph=" + juce::String(appCore_.getBubblegumV2().routingGraph != nullptr ? 1 : 0)
        + " trackManager=" + juce::String(appCore_.getBubblegumV2().trackManager != nullptr ? 1 : 0));
    bubblegumOrb_->onClicked = [this]
    {
        auto& bgV2 = appCore_.getBubblegumV2();
        auto selectedId = appCore_.getState().selectedTrackID.getValue().toString();
        DBG("[MainComponent::orbClicked] STEP 2: toggleBubblegumUI() entered"
            " wasActive=" + juce::String(bgV2.isActive() ? 1 : 0)
            + " selectedTrack=" + selectedId
            + " mixerWindowVisible=" + juce::String(mixerWindow_->isVisible() ? 1 : 0)
            + " mixerDocked=" + juce::String(mixerDocked_ ? 1 : 0));

        // Ensure mixer is visible
        if (!mixerWindow_->isVisible() && !mixerDocked_)
        {
            DBG("[MainComponent::orbClicked] opening mixer window");
            mixerWindow_->setVisible(true);
            mixerWindow_->restoreIfMinimized();
            mixerWindow_->toFront(false);
            mixerBubble_->setVisible(false);
            if (slimeSidePanel_ && slimeSidePanel_->isAttached())
                slimeSidePanel_->setVisible(true);
        }

        // Toggle via single source of truth
        if (bgV2.isActive())
        {
            DBG("[MainComponent::orbClicked] STEP 3: deactivateBubblegumMode()");
            mixerPanel_->deactivateBubblegumMode();
            if (mixerPanel_) mixerPanel_->debugLastLauncher_ = "Orb(deactivate)";
        }
        else
        {
            // Use the currently selected track as source (not first track)
            DAW::TrackID sourceId = appCore_.getState().selectedTrackID.getValue();
            if (sourceId.isEmpty() && appCore_.getTrackManager().getNumTracks() > 0)
                sourceId = appCore_.getTrackManager().getTrack(0)->getID();
            DBG("[MainComponent::orbClicked] STEP 3: activateBubblegumMode(source=" + sourceId + ")");
            if (mixerPanel_) mixerPanel_->debugLastLauncher_ = "Orb";
            mixerPanel_->activateBubblegumMode(sourceId);
            // Floating panel shown via onBubblegumToggled callback
        }

        DBG("[MainComponent::orbClicked] STEP 8-9: done, isActive now=" + juce::String(bgV2.isActive() ? 1 : 0)
            + " panel.isOpen=" + juce::String(bgV2.panel.isOpen() ? 1 : 0)
            + " source=" + bgV2.sourceSync.getSourceTrackId()
            + " targets=" + juce::String(bgV2.targetList.getCount()));
    };
    bubblegumOrb_->getSourceTrackLevel = [this]() -> float
    {
        auto& bgV2 = appCore_.getBubblegumV2();
        auto srcId = bgV2.sourceSync.getSourceTrackId();
        if (srcId.isEmpty()) return 0.f;
        auto& tm = appCore_.getTrackManager();
        for (int i = 0; i < tm.getNumTracks(); ++i)
        {
            auto* t = tm.getTrack(i);
            if (t && t->getID() == srcId)
                return t->getPeakLevel();
        }
        return 0.f;
    };
    addAndMakeVisible(bubblegumOrb_.get());

    // -- Selected-track peak bubble ----------------------------------------
    peakBubble_.setMeterManager(&appCore_.getTrackPeakMeterManager());
    peakBubble_.setApplicationState(&appCore_.getState());
    peakBubble_.setCloseToBubble(false);
    peakBubble_.setTrackNameResolver([this](const juce::String& id) -> juce::String
    {
        if (auto* t = appCore_.getTrackManager().getTrack(id))
            return t->getName();
        return {};
    });
    peakBubble_.setSize(DAW::SelectedTrackPeakBubbleComponent::preferredWidth(),
                        DAW::SelectedTrackPeakBubbleComponent::preferredHeight());
    addChildComponent(peakBubble_);  // visible only when a track is selected
    peakBubble_.toFront(false);
    virtualCursor_ = std::make_unique<DAW::VirtualCursor>();
    virtualCursor_->setMode(appCore_.getInteractionManager().getMode());
    addAndMakeVisible(virtualCursor_.get());
    virtualCursor_->toFront(false);

    // Settings panel (floating, hidden by default)
    settingsPanel_ = std::make_unique<DAW::SettingsPanel>();
    settingsPanel_->setGamepadManager(&appCore_.getGamepadManager());
    settingsPanel_->setInteractionManager(&appCore_.getInteractionManager());
    settingsPanel_->getGamepadToggle()->onClick = [this](bool enabled)
    {
        appCore_.setGamepadEnabled(enabled);
    };
    settingsPanel_->getWaveformScrollToggle()->onClick = [this](bool enabled)
    {
        if (arrangement_)
            arrangement_->setKeepWaveformsVisibleWhileScrolling(enabled);
    };
    settingsPanel_->onBubblegumRoutingSettingsChanged = [this](bool autoScroll, bool autoClose, bool keepCables, bool keepOffscreen)
    {
        auto& state = appCore_.getState();
        state.bubblegumOffscreenAutoScroll.setValue(autoScroll);
        state.bubblegumOffscreenAutoClose.setValue(autoClose);
        state.bubblegumKeepCablesVisibleWhenClosed.setValue(keepCables);
        state.bubblegumKeepOffscreenVisibleWhenClosed.setValue(keepOffscreen);

        offscreenEndpoint_.setPopupRowClickBehavior(autoScroll, autoClose);
        if (mixerPanel_)
        {
            mixerPanel_->setCableForceVisible(keepCables);
            mixerPanel_->setOffscreenForceVisible(keepOffscreen);
        }

        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    };
    settingsPanel_->onFolderDropModeChanged = [this](int mode)
    {
        appCore_.getState().folderDropMode.setValue(mode);
    };

    settingsPanel_->onAutoFadeOnSplitChanged = [this](bool enabled)
    {
        appCore_.getState().autoFadeOnClipSplit.setValue(enabled);
    };

    settingsPanel_->onAllTracksFullDepthChanged = [this](bool v)
    {
        if (mixerPanel_) mixerPanel_->setAllTracksFullDepth(v);
    };

    settingsPanel_->onBubblegumSelectedTrackColourChanged = [this](DAW::BubblegumAppearanceSettings::PaletteFamily family,
                                                                   int presetIndex,
                                                                   juce::Colour colour)
    {
        auto& state = appCore_.getState();
        state.bubblegumSelectedTrackPaletteFamily.setValue((int) family);
        state.bubblegumSelectedTrackPaletteIndex.setValue(presetIndex);
        state.bubblegumSelectedTrackColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
        if (mixerPanel_)
            mixerPanel_->setBubblegumSelectedAccentColour(colour);
    };

    settingsPanel_->onBubblegumCableColourChanged = [this](DAW::BubblegumAppearanceSettings::PaletteFamily family,
                                                           int presetIndex,
                                                           juce::Colour colour)
    {
        auto& state = appCore_.getState();
        state.bubblegumCablePaletteFamily.setValue((int) family);
        state.bubblegumCablePaletteIndex.setValue(presetIndex);
        state.bubblegumCableColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
        cableOverlay_.setCableAccentColour(colour);
    };

    settingsPanel_->onBubblegumRoutingTargetColourChanged = [this](juce::Colour colour)
    {
        appCore_.getState().bubblegumRoutingTargetColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
        if (mixerPanel_)
            mixerPanel_->setBubblegumTargetAccentColour(colour);
    };

    settingsPanel_->onBubblegumCableThicknessChanged = [this](float thickness)
    {
        appCore_.getState().bubblegumCableThickness.setValue(thickness);
        auto style = cableOverlay_.getRenderStyle();
        style.baseThickness = thickness;
        cableOverlay_.setRenderStyle(style);
        cableOverlay_.repaint();
    };

    settingsPanel_->onMasterTrackAccentColourChanged = [this](juce::Colour colour)
    {
        appCore_.getState().bubblegumMasterAccentColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
        if (mixerPanel_)
            mixerPanel_->setMasterAccentColour(colour);
    };

    settingsPanel_->onBubblegumSendPillModeChanged = [this](int mode)
    {
        appCore_.getState().bubblegumSendPillMode.setValue(mode);
        if (mixerPanel_)
            mixerPanel_->setBubblegumSendPillDisplayMode(mode);
    };

    settingsPanel_->onShowSendBadgesChanged = [this](bool show)
    {
        appCore_.getState().showSendBadges.setValue(show);
        cableOverlay_.setShowSendBadges(show);
    };

    settingsPanel_->onShowSidechainBadgesChanged = [this](bool show)
    {
        appCore_.getState().showSidechainBadges.setValue(show);
        cableOverlay_.setShowSidechainBadges(show);
    };

    settingsPanel_->onSidechainCableThicknessChanged = [this](float thickness)
    {
        appCore_.getState().sidechainCableThickness.setValue(thickness);
        cableOverlay_.setSidechainCableThickness(thickness);
    };

    if (auto* appearancePanel = settingsPanel_->getBubblegumAppearancePanel())
    {
        appearancePanel->onSelectedSphereColourChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumSelectedSphereColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            if (mixerPanel_)
                mixerPanel_->setBubblegumSelectedSphereColour(colour);
        };

        appearancePanel->onSelectedParticleColourChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumSelectedParticleColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            if (mixerPanel_)
                mixerPanel_->setBubblegumSelectedParticleColour(colour);
        };

        appearancePanel->onCableBodyTopChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableBodyTopColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.bodyTop = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableCoreTopChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableCoreTopColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.coreTop = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableCoreBottomChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableCoreBottomColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.coreBottom = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableMistTopChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableMistTopColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.mistTop = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableMistBottomChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableMistBottomColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.mistBottom = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableDropletChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableDropletColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.droplet = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onCableShadowChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumCableShadowColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            auto style = cableOverlay_.getRenderStyle();
            style.shadow = colour;
            cableOverlay_.setRenderStyle(style);
        };

        appearancePanel->onSidechainCableColourChanged = [this](juce::Colour colour)
        {
            appCore_.getState().bubblegumSidechainCableColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            appCore_.getBubblegumV2().setSidechainCableColour(colour, colour.brighter(0.55f));
            if (mixerPanel_)
                mixerPanel_->repaint();
            cableOverlay_.repaint();
        };

        appearancePanel->onApplyCableColourToRoutingTargets = [this]()
        {
            auto colour = cableOverlay_.getCableAccentColour();
            appCore_.getState().bubblegumRoutingTargetColour.setValue(juce::String::toHexString(static_cast<int64_t>(colour.getARGB())));
            if (mixerPanel_)
                mixerPanel_->setBubblegumTargetAccentColour(colour);
        };

        appearancePanel->onApplyRoutingTargetColourToCables = [this]()
        {
            auto colour = parseStoredColour(appCore_.getState().bubblegumRoutingTargetColour.getValue(), cableOverlay_.getCableAccentColour());
            auto style = cableOverlay_.getRenderStyle();
            style.bodyBottom = colour;
            cableOverlay_.setRenderStyle(style);
            cableOverlay_.setCableAccentColour(colour);
        };
    }

    settingsPanel_->onSelectedTabChanged = [this](int index)
    {
        appCore_.getState().settingsSelectedTab.setValue(index);
    };

    {
        auto& state = appCore_.getState();
        const bool autoScroll = static_cast<bool>(state.bubblegumOffscreenAutoScroll.getValue());
        const bool autoClose = static_cast<bool>(state.bubblegumOffscreenAutoClose.getValue());
        const bool keepCables = static_cast<bool>(state.bubblegumKeepCablesVisibleWhenClosed.getValue());
        const bool keepOffscreen = static_cast<bool>(state.bubblegumKeepOffscreenVisibleWhenClosed.getValue());
        const bool autoFadeOnSplit = static_cast<bool>(state.autoFadeOnClipSplit.getValue());
        const int folderDropMode = (int) state.folderDropMode.getValue();
        const auto selectedTrackFamily = (DAW::BubblegumAppearanceSettings::PaletteFamily) (int) state.bubblegumSelectedTrackPaletteFamily.getValue();
        const int selectedTrackIndex = (int) state.bubblegumSelectedTrackPaletteIndex.getValue();
        const auto cableFamily = (DAW::BubblegumAppearanceSettings::PaletteFamily) (int) state.bubblegumCablePaletteFamily.getValue();
        const int cableIndex = (int) state.bubblegumCablePaletteIndex.getValue();
        const int settingsTabIndex = (int) state.settingsSelectedTab.getValue();
        const auto selectedTrackColour = parseStoredColour(state.bubblegumSelectedTrackColour.getValue(),
                                                           DAW::BubblegumAppearanceSettings::resolveColour(false, selectedTrackFamily, selectedTrackIndex));
        const auto cableColour = parseStoredColour(state.bubblegumCableColour.getValue(),
                                                   DAW::BubblegumAppearanceSettings::resolveColour(true, cableFamily, cableIndex));
        const auto sidechainCableColour = parseStoredColour(state.bubblegumSidechainCableColour.getValue(), juce::Colour(0xFF3A7BD5));
        const auto routingTargetColour = parseStoredColour(state.bubblegumRoutingTargetColour.getValue(), selectedTrackColour);
        const auto selectedSphereColour = parseStoredColour(state.bubblegumSelectedSphereColour.getValue(), juce::Colour(0xFF1A1A1A));
        const auto selectedParticleColour = parseStoredColour(state.bubblegumSelectedParticleColour.getValue(), selectedTrackColour.brighter(0.2f));
        const auto cableBodyTopColour = parseStoredColour(state.bubblegumCableBodyTopColour.getValue(), cableColour.interpolatedWith(juce::Colours::white, 0.70f));
        const auto cableCoreTopColour = parseStoredColour(state.bubblegumCableCoreTopColour.getValue(), cableColour.interpolatedWith(juce::Colours::white, 0.88f));
        const auto cableCoreBottomColour = parseStoredColour(state.bubblegumCableCoreBottomColour.getValue(), cableColour.withAlpha(0.28f));
        const auto cableMistTopColour = parseStoredColour(state.bubblegumCableMistTopColour.getValue(), cableColour.interpolatedWith(juce::Colours::white, 0.80f).withAlpha(0.03f));
        const auto cableMistBottomColour = parseStoredColour(state.bubblegumCableMistBottomColour.getValue(), cableColour.withAlpha(0.10f));
        const auto cableDropletColour = parseStoredColour(state.bubblegumCableDropletColour.getValue(), cableColour.interpolatedWith(juce::Colours::white, 0.92f).withAlpha(0.42f));
        const auto cableShadowColour = parseStoredColour(state.bubblegumCableShadowColour.getValue(), cableColour.darker(1.8f).withAlpha(0.16f));
        const float cableThickness = (float) state.bubblegumCableThickness.getValue();
        const int sendPillMode = (int) state.bubblegumSendPillMode.getValue();
        const auto masterAccentColour = parseStoredColour(state.bubblegumMasterAccentColour.getValue(), juce::Colour(0xFFCC9900));

        settingsPanel_->setBubblegumRoutingSettings(autoScroll, autoClose, keepCables, keepOffscreen);
        settingsPanel_->setFolderDropMode(folderDropMode);
        settingsPanel_->setAutoFadeOnSplitEnabled(autoFadeOnSplit);
        settingsPanel_->setBubblegumSelectedTrackColourState(selectedTrackFamily, selectedTrackIndex, selectedTrackColour);
        settingsPanel_->setBubblegumCableColourState(cableFamily, cableIndex, cableColour);
        settingsPanel_->setBubblegumSidechainCableColour(sidechainCableColour);
        settingsPanel_->setSelectedTabIndex(settingsTabIndex);
        offscreenEndpoint_.setPopupRowClickBehavior(autoScroll, autoClose);
        if (mixerPanel_)
        {
            mixerPanel_->setCableForceVisible(keepCables);
            mixerPanel_->setOffscreenForceVisible(keepOffscreen);
            mixerPanel_->setBubblegumSelectedAccentColour(selectedTrackColour);
            mixerPanel_->setBubblegumSelectedSphereColour(selectedSphereColour);
            mixerPanel_->setBubblegumSelectedParticleColour(selectedParticleColour);
            mixerPanel_->setBubblegumTargetAccentColour(routingTargetColour);
            mixerPanel_->setBubblegumSendPillDisplayMode(sendPillMode);
            mixerPanel_->setMasterAccentColour(masterAccentColour);
        }
        cableOverlay_.setCableAccentColour(cableColour);
        appCore_.getBubblegumV2().setSidechainCableColour(sidechainCableColour, sidechainCableColour.brighter(0.55f));
        {
            auto style = cableOverlay_.getRenderStyle();
            style.baseThickness = cableThickness > 0.0f ? cableThickness : style.baseThickness;
            style.bodyTop = cableBodyTopColour;
            style.coreTop = cableCoreTopColour;
            style.coreBottom = cableCoreBottomColour;
            style.mistTop = cableMistTopColour;
            style.mistBottom = cableMistBottomColour;
            style.droplet = cableDropletColour;
            style.shadow = cableShadowColour;
            cableOverlay_.setRenderStyle(style);
        }

        DBG("Bubblegum settings: autoScroll=" << (int)autoScroll
            << " autoClose=" << (int)autoClose
            << " keepCablesVisible=" << (int)keepCables);
    }

    // Audio Device floating panel is created lazily when opened. Constructing JUCE's
    // device selector at startup can probe ASIO drivers before the DAW has selected
    // a safe default backend.

    // Control Room floating panel
    controlRoomPanel_ = std::make_unique<DAW::ControlRoomSettingsUI>(appCore_.getControlRoom());
    addChildComponent(controlRoomPanel_.get());

    // Wire settings panel buttons to show floating panels
    settingsPanel_->onOpenAudioDevice = [this]
    {
        showAudioDevicePanel();
    };
    settingsPanel_->onOpenControlRoom = [this]
    {
        bool vis = !controlRoomPanel_->isVisible();
        controlRoomPanel_->setVisible(vis);
        if (vis)
        {
            controlRoomPanel_->setBounds(getWidth() / 2 - 180, 100, 360, 480);
            controlRoomPanel_->toFront(true);
        }
    };

    // Wire settings panel [?] help button
    settingsPanel_->onOpenShortcutHelp = [this]
    {
        if (!shortcutHelpWindow_)
        {
            shortcutHelpWindow_ = std::make_unique<DAW::ShortcutHelpFloatingWindow>();
            shortcutHelpWindow_->onCloseClicked = [this]
            {
                shortcutHelpWindow_->setVisible(false);
            };
            addChildComponent(shortcutHelpWindow_.get());
        }
        bool vis = !shortcutHelpWindow_->isVisible();
        shortcutHelpWindow_->setVisible(vis);
        if (vis)
        {
            // Default position: center of window
            if (shortcutHelpWindow_->getPosition().isOrigin())
                shortcutHelpWindow_->setBounds(
                    getWidth() / 2 - 260, 80, 520, 560);
            shortcutHelpWindow_->toFront(true);
        }
    };

    settingsPanel_->onProfileApplied = [this]
    {
        grabKeyboardFocus();
        DBG("[PROFILE] Applied: " + DAW::KeyBindingManager::getInstance().getActiveProfile().name);
    };

    // -- Action handler registrations -----------------------------------------
    auto& am = DAW::ActionManager::getInstance();
    auto& tm = appCore_.getTrackManager();

    regProjectNew_ = am.scoped(DAW::ActionID::ProjectNew, [this]
    {
        if (!appCore_.getProjectManager().newProject())
        {
            juce::Logger::writeToLog("[PROJECT] New Project failed: "
                + appCore_.getProjectManager().getLastLoadError());
            return;
        }
        updateWindowTitle();
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    });

    regProjectOpen_ = am.scoped(DAW::ActionID::ProjectOpen, [this]
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Open Project",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.dawproj");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto file = fc.getResult();
                    if (file.existsAsFile())
                        self->openProjectFileWithOverlay(file, "Open");
    appendMainStartupTrace("ctor.complete");
                }
            });
    });

    regProjectSave_ = am.scoped(DAW::ActionID::ProjectSave, [this]
    {
        auto& pm = appCore_.getProjectManager();
        if (pm.getProjectFile() != juce::File())
        {
            saveProjectToFile(pm.getProjectFile());
            return;
        }
        // No file yet - fall through to Save As
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::ProjectSaveAs);
    });

    regProjectSaveAs_ = am.scoped(DAW::ActionID::ProjectSaveAs, [this]
    {
        auto currentFile = appCore_.getProjectManager().getProjectFile();
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Project As",
            currentFile == juce::File()
                ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                : currentFile,
            "*.dawproj");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto file = fc.getResult();
                    if (file == juce::File())
                        return;
                    if (file.getFileExtension().isEmpty())
                        file = file.withFileExtension(".dawproj");
                    self->saveProjectToFile(file);
                }
            });
    });

    regImportFiles_ = am.scoped(DAW::ActionID::ImportFiles, [this]
    {
        juce::Component::SafePointer<MainComponent> safeThis(this);
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import Audio",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (auto* self = safeThis.getComponent())
                {
                    auto files = fc.getResults();
                    if (files.isEmpty())
                        return;
                    auto selectedTrackId = self->appCore_.getState().selectedTrackID.getValue().toString();
                    const auto insertPos = self->appCore_.getTransport().getPosition();
                    if (self->arrangement_ != nullptr)
                        self->arrangement_->queueAudioFilesForImport(files, selectedTrackId, insertPos);
                }
            });
    });

    regPlayStop_ = am.scoped(DAW::ActionID::TransportPlayStop, [this]
    {
        juce::Logger::writeToLog ("[AUTO-TRANSPORT] MainComponent TransportPlayStop handler called");

        // Single canonical spacebar handler:
        // stop (and return to play-start) when playing or recording,
        // play from current position when stopped.
        auto& transport = appCore_.getTransport();
        if (transport.isPlaying() || transport.isRecording())
            transport.stop();
        else
            transport.play();
    });

    regSettings_ = am.scoped(DAW::ActionID::TransportStop, [this]
    {
        appCore_.getTransport().stop();
    });

    regMarkerAdd_ = am.scoped(DAW::ActionID::TransportJumpStart, [this]
    {
        appCore_.getTransport().returnToZero();
    });

    regRecord_ = am.scoped(DAW::ActionID::TransportRecord, [this]
    {
        auto& transport = appCore_.getTransport();
        if (transport.isRecording())
        {
            transport.stopRecording();
            return;
        }

        // ?? Record preflight (pro DAW behavior) ?????????????????????????????
        // If no input channels are active, repair them BEFORE the take starts
        // (a device restart mid-take would corrupt the recording). If repair
        // fails, still record - the engine writes a silent, sample-aligned
        // take instead of losing it - but tell the user what is wrong.
        if (getEnabledAudioInputChannelCount() <= 0)
        {
            juce::Logger::writeToLog("[REC] preflight: no active input channels - attempting repair before recording");
            ensureInputChannelsActive("record-preflight");

            const int repairedInputs = getEnabledAudioInputChannelCount();
            cachedEnabledAudioInputChannels_.store(repairedInputs, std::memory_order_release);
            appCore_.setHardwareInputChannelCount(repairedInputs);

            if (repairedInputs <= 0)
            {
                juce::Logger::writeToLog("[REC] preflight: repair FAILED - recording will capture silence. "
                                         "Check Windows microphone privacy settings and the audio device panel.");
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("No Audio Input")
                        .withMessage("Recording will start, but no audio input is available - the take will be silent.\n\n"
                                     "Check:\n"
                                     "  - Windows Settings > Privacy > Microphone access\n"
                                     "  - Audio device panel (input device selected?)\n"
                                     "  - The input isn't claimed exclusively by another app")
                        .withButton("Record Anyway")
                        .withAssociatedComponent(this),
                    nullptr);
            }
            else
            {
                juce::Logger::writeToLog("[REC] preflight: repair OK - " + juce::String(repairedInputs) + " input channel(s) active");
            }
        }

        auto warnings = appCore_.checkRecordArmSafety();
        if (warnings.empty())
        {
            transport.record();
            return;
        }

        juce::Component::SafePointer<MainComponent> safeThis(this);
        DAW::RecordOverwriteConfirmDialog::show(warnings, [safeThis](bool shouldRecord)
        {
            if (shouldRecord)
                if (auto* self = safeThis.getComponent())
                    self->appCore_.getTransport().recordWithoutSafetyCheck();
        });
    });

    regTrackAddAudio_ = am.scoped(DAW::ActionID::TrackAddAudio, [this, &tm]
    {
        DAW::CommandManager::getInstance().execute(
            std::make_unique<DAW::AddTrackCommand>(tm, "Audio " + juce::String(tm.getNumTracks() + 1), DAW::TrackRole::Audio));
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    });

    regTrackAddMidi_ = am.scoped(DAW::ActionID::TrackAddMIDI, [this, &tm]
    {
        juce::ignoreUnused(tm);
        // DISABLED (product decision): beat-making surfaces (MIDI tracks,
        // piano roll, step sequencer) are hidden. The ActionID registration
        // stays so shortcuts/menu bindings remain valid and the feature can
        // be re-enabled without code archaeology.
    });

    regTrackDuplicate_ = am.scoped(DAW::ActionID::TrackDuplicate, [this, &tm]
    {
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (auto* track = tm.getTrack(selId))
            DAW::CommandManager::getInstance().execute(
                std::make_unique<DAW::AddTrackCommand>(tm, track->getName() + " (copy)", track->getRole()));
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    });

    regTrackDeleteSelected_ = am.scoped(DAW::ActionID::TrackDeleteSelected, [this]
    {
        // If multiple tracks selected, delete all
        if (multiSelection_.hasMultiple(DAW::SelectionKind::Track))
        {
            auto& tm = appCore_.getTrackManager();
            std::vector<DAW::TrackID> toDelete;
            for (const auto& id : getSelectedTrackIdsInProjectOrder())
                if (tm.canDeleteTrack(id))
                    toDelete.push_back(id);
            if (!toDelete.empty())
            {
                juce::Component::SafePointer<MainComponent> safeThis(this);
                auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Delete Selected Tracks")
                    .withMessage("Delete " + juce::String(toDelete.size()) + " selected tracks?")
                    .withButton("Delete").withButton("Cancel").withAssociatedComponent(this);
                juce::AlertWindow::showAsync(options, [safeThis, toDelete](int result)
                {
                    if (result != 1 || !safeThis) return;
                    safeThis->deleteTracksAsOneTopologyTransaction(toDelete);
                });
            }
            return;
        }
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (selId.isNotEmpty())
            deleteTracksAsOneTopologyTransaction({ selId });
    });

    regTrackArmSelected_ = am.scoped(DAW::ActionID::TrackArmSelected, [this, &tm]
    {
        if (multiSelection_.hasMultiple(DAW::SelectionKind::Track))
        {
            // Determine new state from primary
            auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
            bool newArmed = true;
            if (primary.isValid())
                if (auto* tr = tm.getTrack(primary.trackId))
                    newArmed = !tr->isArmed();
            DAW::SelectionBulkActionCore::applyArmToSelectedTracks(multiSelection_, tm, newArmed);
            return;
        }
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (auto* track = tm.getTrack(selId))
            DAW::CommandManager::getInstance().execute(
                std::make_unique<DAW::TrackPropertyChangeCommand>(
                    tm, selId, DAW::TrackPropertyChangeCommand::Property::Armed,
                    track->isArmed(), !track->isArmed(), "Toggle Track Arm"));
    });

    regTrackMuteSelected_ = am.scoped(DAW::ActionID::TrackMuteSelected, [this, &tm]
    {
        if (multiSelection_.hasMultiple(DAW::SelectionKind::Track))
        {
            auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
            bool newMuted = true;
            if (primary.isValid())
                if (auto* tr = tm.getTrack(primary.trackId))
                    newMuted = !tr->isMuted();
            DAW::SelectionBulkActionCore::applyMuteToSelectedTracks(multiSelection_, tm, newMuted);
            return;
        }
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (auto* track = tm.getTrack(selId))
            DAW::CommandManager::getInstance().execute(
                std::make_unique<DAW::TrackPropertyChangeCommand>(
                    tm, selId, DAW::TrackPropertyChangeCommand::Property::Muted,
                    track->isMuted(), !track->isMuted(), "Toggle Track Mute"));
    });

    regTrackSoloSelected_ = am.scoped(DAW::ActionID::TrackSoloSelected, [this, &tm]
    {
        if (multiSelection_.hasMultiple(DAW::SelectionKind::Track))
        {
            auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
            bool newSoloed = true;
            if (primary.isValid())
                if (auto* tr = tm.getTrack(primary.trackId))
                    newSoloed = !tr->isSoloed();
            DAW::SelectionBulkActionCore::applySoloToSelectedTracks(multiSelection_, tm, newSoloed);
            return;
        }
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (auto* track = tm.getTrack(selId))
            DAW::CommandManager::getInstance().execute(
                std::make_unique<DAW::TrackPropertyChangeCommand>(
                    tm, selId, DAW::TrackPropertyChangeCommand::Property::Soloed,
                    track->isSoloed(), !track->isSoloed(), "Toggle Track Solo"));
    });

    regSelectAll_ = am.scoped(DAW::ActionID::EditSelectAll, [this]
    {
        // Ctrl+A / Edit ▸ Select All: select every clip on the arrangement
        // timeline (standard DAW behavior). Clips on tracks hidden by a
        // collapsed folder are skipped.
        if (arrangement_ == nullptr)
            return;
        auto* core = arrangement_->getCore();
        if (core == nullptr)
            return;

        std::set<juce::Uuid> allIds;
        for (const auto& clip : core->getClipState().allClips())
        {
            if (auto* renderer = core->findClipRendererFor(clip.id))
                if (renderer->getBounds().getY() < 0) // parked offscreen (collapsed folder)
                    continue;
            allIds.insert(clip.id);
        }
        core->getSelection().setSelection(allIds);
    });

    regTrackRenameSelected_ = am.scoped(DAW::ActionID::TrackRenameSelected, [this]
    {
        juce::ignoreUnused(this); // rename triggered via track list row double-click
    });

    regViewMixer_ = am.scoped(DAW::ActionID::ViewToggleMixer, [this]
    {
        if (mixerDocked_)
        {
            mixerViewport_.setVisible(!mixerViewport_.isVisible());
            if (slimeSidePanel_ && slimeSidePanel_->isAttached())
                slimeSidePanel_->setVisible(mixerViewport_.isVisible());
        }
        else
        {
            if (mixerWindow_->isVisible()) { mixerWindow_->setVisible(false); mixerBubble_->setVisible(true); if (slimeSidePanel_ && slimeSidePanel_->isAttached()) slimeSidePanel_->setVisible(false); }
            else { mixerWindow_->setVisible(true); mixerWindow_->restoreIfMinimized(); mixerWindow_->toFront(false); mixerBubble_->setVisible(false); if (slimeSidePanel_ && slimeSidePanel_->isAttached()) slimeSidePanel_->setVisible(true); }
        }
        resized();
    });

    regViewTrackList_ = am.scoped(DAW::ActionID::ViewToggleTrackList, [this]
    {
        bool vis = trackListWindow_ && trackListWindow_->isVisible();
        if (trackListWindow_) trackListWindow_->setVisible(!vis);
        if (!vis) trackListW_ = 220; else trackListW_ = 0;
        resized();
    });

    regViewSettings_ = am.scoped(DAW::ActionID::ViewToggleSettings, [this]
    {
        if (settingsPanel_)
        {
            bool vis = !settingsPanel_->isVisible();
            settingsPanel_->setVisible(vis);
            if (vis) settingsPanel_->setBounds(getLocalBounds().reduced(120, 70));
        }
    });

    regViewHistory_ = am.scoped(DAW::ActionID::ViewToggleHistory, [this]
    {
        historyVisible_ = !historyVisible_;
        if (historyPanel_) { historyPanel_->setVisible(historyVisible_); if (historyVisible_) historyPanel_->toFront(false); }
    });

    regViewStepSequencer_ = am.scoped(DAW::ActionID::ViewToggleStepSequencer, [this]
    {
        // DISABLED (product decision): the step sequencer is a beat-making
        // surface and is hidden. The ActionID registration stays so the menu
        // binding remains valid and the feature can be re-enabled later.
        juce::ignoreUnused(stepSequencerWindow_);
    });

    juce::Component::SafePointer<MainComponent> safeForAudioInit(this);
    juce::MessageManager::callAsync([safeForAudioInit]
    {
        if (auto* self = safeForAudioInit.getComponent())
        {
            appendMainStartupTrace("audio-init.begin");
            self->initializeSafeStartupAudio();
            appendMainStartupTrace("audio-init.after-setAudioChannels");
        }
    });

    appendMainStartupTrace("ctor.complete");
}

//==============================================================================
// ProjectManager::Listener
//==============================================================================

void MainComponent::projectSaved()
{
    // Notify autosave manager that a manual save just completed ? clear userDirty
    appCore_.notifyManualSave();

    // Clear recovery banner on first successful manual save
    if (recoveryBannerVisible_)
    {
        recoveryBannerVisible_ = false;
        appCore_.clearRecoveredFlag();
        repaint();
    }
}

bool MainComponent::augmentProjectStateForSave(juce::ValueTree& state,
                                                juce::String& error)
{
    error.clear();
    if (!state.isValid())
    {
        error = "Project state augmentation received an invalid state.";
        return false;
    }

    // Add collapsed folder IDs before ProjectManager serializes and publishes
    // the single authoritative candidate.
    juce::ValueTree collapsedFolders("CollapsedFolders");
    for (const auto& folderId : collapsedFolderTrackIds_)
    {
        juce::ValueTree folderNode("Folder");
        folderNode.setProperty("trackId", folderId, nullptr);
        collapsedFolders.addChild(folderNode, -1, nullptr);
    }
    state.addChild(collapsedFolders, -1, nullptr);
    return true;
}

bool MainComponent::saveProjectToFile(const juce::File& file)
{
    auto& pm = appCore_.getProjectManager();
    const auto saved = pm.saveToFile(
        file,
        [this](juce::ValueTree& state, juce::String& error)
        {
            return augmentProjectStateForSave(state, error);
        });

    if (!saved && pm.getLastSaveError().isNotEmpty())
        juce::Logger::writeToLog("[PROJECT] MainComponent save failed: "
                                 + pm.getLastSaveError());

    return saved;
}

void MainComponent::openProjectFileWithOverlay(const juce::File& file,
                                               const juce::String& failureContext,
                                               std::function<void(bool)> onComplete)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (!file.existsAsFile())
        return;

    startupPanelMayBeShown_ = false;
    if (startupPanel_)
        startupPanel_->setVisible(false);

    const auto generation = loadingOverlay_
        ? loadingOverlay_->showWithProjectName(file.getFileNameWithoutExtension())
        : DAW::ProjectLoadingOverlay::Generation{};

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, file, generation, failureContext, onComplete]
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        const bool loaded = self->appCore_.getProjectManager().loadFromFile(file);
        if (!loaded)
        {
            juce::Logger::writeToLog("[PROJECT] " + failureContext + " failed: "
                + self->appCore_.getProjectManager().getLastLoadError());
        }

        auto completeVisibility = [safeThis, loaded, onComplete]
        {
            if (auto* owner = safeThis.getComponent())
            {
                if (loaded)
                {
                    owner->startupPanelMayBeShown_ = false;
                    if (owner->startupPanel_)
                        owner->startupPanel_->dismiss();
                }
                else
                {
                    owner->startupPanelMayBeShown_ = true;
                    if (owner->startupPanel_)
                    {
                        owner->startupPanel_->setVisible(true);
                        owner->startupPanel_->toFront(false);
                    }
                }
            }

            if (onComplete)
                onComplete(loaded);
        };

        if (self->loadingOverlay_ && generation != 0)
            self->loadingOverlay_->dismissAfterMinimumVisible(generation,
                                                               std::move(completeVisibility));
        else
            completeVisibility();
    });
}

void MainComponent::upgradeProjectFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    // Backup FIRST — the upgrade must never be able to lose the original.
    const auto extension = file.getFileExtension().isEmpty()
        ? juce::String(".dawproj") : file.getFileExtension();
    const auto backup = file.getSiblingFile(
        file.getFileNameWithoutExtension()
        + ".backup_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S")
        + extension);

    if (! file.copyFileTo(backup))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Upgrade Project",
            "Could not create the backup copy:\n" + backup.getFullPathName()
            + "\n\nThe project was left untouched.");
        return;
    }

    juce::Component::SafePointer<MainComponent> safeThis(this);
    openProjectFileWithOverlay(file, "Upgrade",
        [safeThis, file, backup](bool loaded)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            auto& pm = self->appCore_.getProjectManager();

            if (! loaded)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Upgrade Project",
                    "The project could not be loaded:\n" + pm.getLastLoadError()
                    + "\n\nThe original was not modified. Backup:\n"
                    + backup.getFullPathName());
                return;
            }

            const auto report = pm.getLastUpgradeReport();

            // Persist the upgraded project. The save is crash-safe (temp file
            // + transactional replace), so the previous valid file survives a
            // failed publication.
            const bool saved = pm.saveToFile(file);

            juce::String message;
            message << (saved ? "Project upgraded and saved."
                              : "Project upgraded, but saving FAILED.")
                    << "\n\nBackup of the original:\n" << backup.getFullPathName()
                    << "\n\n" << report.toText();

            juce::AlertWindow::showMessageBoxAsync(
                saved ? juce::MessageBoxIconType::InfoIcon
                      : juce::MessageBoxIconType::WarningIcon,
                "Upgrade Project", message);
        });
}

void MainComponent::projectLoaded()
{
    // A loaded/new project invalidates every recorded command - the old
    // entries reference track/clip IDs that no longer exist. Every major
    // DAW starts a fresh history per project; replaying a stale command
    // against dead IDs would silently corrupt the new session.
    DAW::CommandManager::getInstance().clearHistory();

    // Quick Send mode references a track ID that may not exist in the new
    // project — always exit on project load.
    exitQuickSendMode();

    // Update window title with project name
    updateWindowTitle();

    // Update startup panel with current project info
    startupPanelMayBeShown_ = false;
    if (startupPanel_)
    {
        startupPanel_->setProjectName("APEX");
        startupPanel_->setPluginInfo(
            appCore_.getPluginScanner().getKnownPlugins().getNumTypes(),
            appCore_.getPluginScanner().getFailureStore().getCount());
        startupPanel_->addRecentFile(appCore_.getProjectManager().getProjectFile());
        startupPanel_->dismiss();
    }

    // Restore collapsed folder state from the already parsed project tree.
    // Re-parsing the complete project here doubled XML I/O on the message thread.
    auto& pm = appCore_.getProjectManager();
    collapsedFolderTrackIds_.clear();
    for (const auto& folderId : pm.getLastLoadedCollapsedFolderIds())
    {
        if (folderId.isNotEmpty())
            collapsedFolderTrackIds_.insert(folderId);
    }
    syncFolderCollapseState();

    // Audio buffers were reloaded by ProjectManager while the destructive
    // project-restore callback gate was still active.
    if (arrangement_)
        arrangement_->projectAudioCachePublished();

    // Restore the master project key readout in the transport bar.
    if (transportBar_)
        transportBar_->setProjectKeyText(appCore_.getState().projectKey.getValue().toString());

    // Full UI layout + repaint so tracks, clips, and automation lanes show correctly.
    resized();
    if (arrangement_)  arrangement_->repaint();
    if (trackList_)    trackList_->repaint();
    if (mixerPanel_)   mixerPanel_->repaint();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();

    // Refresh the plugin side panel for the currently selected track. After a
    // project load, selectedTrackID may already point at a track that has
    // plugins restored, but the side panel was last bound to the previous
    // (empty) chain. Re-bind it now so the user sees the plugins immediately
    // without having to click another track and back.
    if (pluginSidePanel_)
    {
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (selId.isNotEmpty())
        {
            auto* track = appCore_.getTrackManager().getTrack(selId);
            auto* chain = track ? appCore_.getPluginChain(selId) : nullptr;
            if (track)
                pluginSidePanel_->setTrack(track, chain);
            else
                pluginSidePanel_->clearTrack();
        }
    }
}

void MainComponent::updateWindowTitle()
{
    auto& pm = appCore_.getProjectManager();
    juce::String title;
    if (pm.getProjectFile() != juce::File())
        title = pm.getProjectFile().getFileNameWithoutExtension();
    else
        title = "Untitled";
    title += " - ";
    title += ProjectInfo::projectName;
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
        dw->setName(title);
}

void MainComponent::syncFolderCollapseState()
{
    if (trackList_)
        trackList_->setCollapsedFolders(collapsedFolderTrackIds_);
    if (arrangement_)
        arrangement_->setCollapsedFolders(collapsedFolderTrackIds_);
    if (mixerPanel_)
        mixerPanel_->setCollapsedFolderBuses(collapsedFolderTrackIds_);
}

void MainComponent::openVocalTuneForSelectedClip()
{
    const auto selectedClipId = appCore_.getState().selectedClipID.getValue().toString();
    if (selectedClipId.isEmpty())
        return;

    auto* clip = appCore_.getClipManager().getClip(selectedClipId);
    if (clip == nullptr)
        return;

    if (dynamic_cast<DAW::AudioClip*>(clip) == nullptr)
        return;

    if (auto* integration = appCore_.getVocalTuneIntegrationPtr())
        integration->openEditorForClip(selectedClipId);
}

bool MainComponent::shouldDetachTrackFromOpenFolder(const DAW::TrackID& trackId, int targetIndex) const
{
    auto& fb = appCore_.getFolderBus();
    auto& tm = appCore_.getTrackManager();

    if (!fb.isChildOfAnyFolderBus(trackId))
        return false;

    const auto& parentId = fb.getParentFolderBus(trackId);
    if (parentId.isEmpty() || collapsedFolderTrackIds_.count(parentId) > 0)
        return false;

    int parentIndex = tm.getTrackIndex(parentId);
    if (parentIndex < 0)
        return false;

    int lastDescendantIndex = parentIndex;
    for (const auto& descendantId : fb.getAllDescendants(parentId))
        lastDescendantIndex = juce::jmax(lastDescendantIndex, tm.getTrackIndex(descendantId));

    return targetIndex <= parentIndex || targetIndex > lastDescendantIndex + 1;
}

void MainComponent::moveTrackWithFolderAwareness(const DAW::TrackID& trackId, int targetIndex)
{
    moveTracksWithFolderAwareness({ trackId }, targetIndex);
}

void MainComponent::moveTracksWithFolderAwareness(const std::vector<DAW::TrackID>& selectedIds,
                                                  int destinationGap)
{
    auto& tm = appCore_.getTrackManager();
    if (tm.getNumTracks() <= 0 || selectedIds.empty())
        return;

    std::vector<DAW::TrackID> oldOrder;
    oldOrder.reserve((size_t) tm.getNumTracks());
    for (auto* t : tm.getAllTracks())
        if (t != nullptr)
            oldOrder.push_back(t->getID());

    // TrackManager's normal order intentionally excludes the protected Master.
    // The shared transform consequently filters a stray Master ID naturally
    // while retaining every eligible normal TrackID in project order.
    const auto plan = DAW::TrackReorderCore::makePlan(
        oldOrder, selectedIds, destinationGap);
    if (!plan.changed)
        return;

    auto& folderBus = appCore_.getFolderBus();
    bool hasFolderTopology = false;
    for (const auto& id : oldOrder)
    {
        if (folderBus.isFolderBus(id))
        {
            hasFolderTopology = true;
            break;
        }
    }

    const auto applyPlannedOrder = [&]()
    {
        auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
        for (const auto& id : plan.selectedInProjectOrder)
        {
            const int newIndex = DAW::TrackReorderCore::indexOf(plan.finalOrder, id);
            if (newIndex >= 0 && shouldDetachTrackFromOpenFolder(id, newIndex)
                && masterRoute != nullptr)
            {
                folderBus.removeChildFromFolderBus(id,
                                                   appCore_.getRoutingGraph(),
                                                   *masterRoute,
                                                   &tm);
            }
        }

        tm.applyTrackOrder(plan.finalOrder);
        if (hasFolderTopology)
            for (const auto& id : plan.selectedInProjectOrder)
                adoptTrackIntoFolderBlockWithoutUndo(id);
    };

    // A project containing folders uses the full topology-state command so
    // detach/adopt and order changes remain one user-level transaction.  The
    // lower-level folder calls above deliberately do not create commands.
    if (hasFolderTopology)
    {
        // FolderBusCore may publish connection/property callbacks while the
        // order is being applied.  Keep those callbacks inside the same
        // topology boundary so only the command below represents this user
        // gesture in dirty history.
        const auto applyInsideTopologyBoundary = [&]()
        {
            const bool wasInProgress = topologyMutationInProgress_;
            topologyMutationInProgress_ = true;
            tm.beginTopologyMutation();
            applyPlannedOrder();
            tm.endTopologyMutation();
            topologyMutationInProgress_ = wasInProgress;
        };
        executeTopologyCommand(applyInsideTopologyBoundary, "Move Tracks");
        return;
    }

    applyPlannedOrder();
    // One undo step for the whole N-track move.  `alreadyApplied` prevents the
    // command from applying the same order a second time on initial execute.
    DAW::CommandManager::getInstance().execute(
        std::make_unique<DAW::TrackReorderMultiCommand>(
            tm, oldOrder, plan.finalOrder, true));
}

bool MainComponent::adoptTrackIntoFolderBlockWithoutUndo(const DAW::TrackID& trackId)
{
    auto& tm = appCore_.getTrackManager();
    auto& folderBus = appCore_.getFolderBus();

    // Already a child of a folder → ordinary sibling reorder, nothing to do.
    if (folderBus.getParentFolderBus(trackId).isNotEmpty())
        return false;

    const int movedIdx = tm.getTrackIndex(trackId);
    if (movedIdx <= 0)
        return false;

    // Find the nearest folder bus above the moved track (by track order).
    // Walking up, a root-level track ends any folder block above it.
    DAW::TrackID folderId;
    for (int i = movedIdx - 1; i >= 0; --i)
    {
        auto* t = tm.getTrack(i);
        if (t == nullptr || t->isMaster())
            continue;
        if (folderBus.isFolderBus(t->getID()))
        {
            folderId = t->getID();
            break;
        }
        if (folderBus.getParentFolderBus(t->getID()).isEmpty())
            break;
    }
    if (folderId.isEmpty())
        return false;

    // Does the moved track sit INSIDE that folder's child block (above the
    // folder's last descendant)?
    const int folderIdx = tm.getTrackIndex(folderId);
    int blockEnd = folderIdx;
    for (const auto& desc : folderBus.getAllDescendants(folderId))
    {
        const int di = tm.getTrackIndex(desc);
        if (di > blockEnd)
            blockEnd = di;
    }
    if (movedIdx <= folderIdx || movedIdx > blockEnd)
        return false; // outside the block — ordinary root-level reorder

    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    if (masterRoute == nullptr)
        return false;

    folderBus.addChildToFolderBus(folderId, trackId,
                                  appCore_.getRoutingGraph(),
                                  *masterRoute,
                                  tm);
    return true;
}

void MainComponent::autoAdoptTrackIntoFolderBlock(const DAW::TrackID& trackId)
{
    executeTopologyCommand([&]()
    {
        adoptTrackIntoFolderBlockWithoutUndo(trackId);
    }, "Add Track To Folder Bus");
}

void MainComponent::executeTopologyCommand(std::function<void()> mutation, const juce::String& description)
{
    if (!mutation)
        return;

    auto& pm = appCore_.getProjectManager();
    auto before = pm.buildState();
    mutation();
    auto after = pm.buildState();
    if (!before.isEquivalentTo(after))
        CommandManager::getInstance().execute(std::make_unique<ProjectTopologyStateCommand>(
            pm, before, after, description, true));

    // Topology mutations include sidechain connections. Without this the
    // destination plugin's auxiliary bus is never enabled, so the chain skips
    // the sidechain silently (cable visible, compressor set up, no signal).
    appCore_.refreshSidechainBusConfig();
}

void MainComponent::beginContinuousTopologyGesture(const juce::String&)
{
    if (continuousTopologyGestureActive_)
        return;

    continuousTopologyBeforeState_ = appCore_.getProjectManager().buildState();
    continuousTopologyGestureActive_ = true;
}

void MainComponent::commitContinuousTopologyGesture(const juce::String& description)
{
    if (!continuousTopologyGestureActive_)
        return;

    auto& pm = appCore_.getProjectManager();
    auto after = pm.buildState();
    if (!continuousTopologyBeforeState_.isEquivalentTo(after))
        CommandManager::getInstance().execute(std::make_unique<ProjectTopologyStateCommand>(
            pm, continuousTopologyBeforeState_, after, description, true));

    continuousTopologyGestureActive_ = false;

    // Same as executeTopologyCommand: a dragged connection (sidechain cable)
    // must enable the destination plugin's auxiliary bus or it is never fed.
    appCore_.refreshSidechainBusConfig();
}

void MainComponent::handleFolderDropRequest(const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
{
    // ── Folder-bus drag & drop DISABLED (product decision) ────────────────
    // Drops never create folder buses and never show the "Create New Folder
    // Bus / Convert Destination Track to Folder Bus" popup. Retained as a
    // no-op safety net so no code path can re-enable folder creation from a
    // drop without an explicit change here.
    (void) draggedId;
    (void) targetId;
    return;

    // Master bus never participates in folder stacks (Logic/Reaper convention)
    auto& tm = appCore_.getTrackManager();
    if (auto* dragged = tm.getTrack(draggedId); dragged && dragged->isMaster()) return;
    if (auto* target = tm.getTrack(targetId); target && target->isMaster()) return;

    // An existing FolderBus is always an adoption target. Creation preference
    // applies only to regular destination tracks; otherwise "Always Create"
    // could wrap a FolderBus in a second unintended FolderBus.
    //
    // REAPER/Logic-style guarantee: ANY drop target that lives inside an open
    // folder's child block (an explicit folder, OR any track that is a child /
    // descendant of a folder) adopts into that folder. Tracks outside folder
    // blocks fall through to the creation-preference switch below.
    auto& folderBus = appCore_.getFolderBus();
    DAW::TrackID effectiveFolder = targetId;
    if (!folderBus.isFolderBus(effectiveFolder))
    {
        const DAW::TrackID parentId = folderBus.getParentFolderBus(effectiveFolder);
        if (parentId.isNotEmpty() && folderBus.isFolderBus(parentId))
            effectiveFolder = parentId;
    }
    if (folderBus.isFolderBus(effectiveFolder))
    {
        addTrackToExistingFolderFromDrop(draggedId, effectiveFolder);
        return;
    }

    int mode = (int)appCore_.getState().folderDropMode.getValue();
    switch (mode)
    {
        case kFolderDropModeAlwaysCreateFolderBus:
            createFolderBusFromDrop(draggedId, targetId);
            break;
        case kFolderDropModeAlwaysConvertDestinationTrack:
            convertDestinationTrackToFolderBus(draggedId, targetId);
            break;
        case kFolderDropModeAsk:
        default:
            showFolderDropModePopup(draggedId, targetId);
            break;
    }
}

void MainComponent::convertFolderBusToRegularTrack(const DAW::TrackID& folderBusId)
{
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    if (masterRoute == nullptr || !appCore_.getFolderBus().isFolderBus(folderBusId))
        return;

    executeTopologyCommand([&]
    {
        appCore_.getFolderBus().convertFolderBusToTrack(
            folderBusId, appCore_.getRoutingGraph(), *masterRoute,
            appCore_.getTrackManager());
    }, "Convert Folder Bus to Track");

    collapsedFolderTrackIds_.erase(folderBusId);
    syncFolderCollapseState();
    refreshBubblegumFeedback();
}

void MainComponent::exclusiveMuteTrack(const DAW::TrackID& trackId)
{
    auto& tracks = appCore_.getTrackManager();
    auto* target = tracks.getTrack(trackId);
    if (target == nullptr || target->isMaster())
        return;

    std::unordered_set<DAW::TrackID> audibleBranch { trackId };
    auto& folders = appCore_.getFolderBus();

    // A leaf cannot be audible if one of its parent folders is muted.
    auto parentId = folders.getParentFolderBus(trackId);
    while (parentId.isNotEmpty() && audibleBranch.insert(parentId).second)
        parentId = folders.getParentFolderBus(parentId);

    // A FolderBus needs its descendants to contribute audio.
    if (folders.isFolderBus(trackId))
        for (const auto& descendantId : folders.getAllDescendants(trackId))
            audibleBranch.insert(descendantId);

    std::vector<DAW::ExclusiveTrackMuteCommand::Entry> entries;
    entries.reserve((size_t) tracks.getNumTracks());
    bool changesState = false;
    for (int i = 0; i < tracks.getNumTracks(); ++i)
    {
        auto* track = tracks.getTrack(i);
        if (track == nullptr || track->isMaster())
            continue;

        const bool shouldMute = audibleBranch.count(track->getID()) == 0;
        DAW::ExclusiveTrackMuteCommand::Entry entry;
        entry.trackId = track->getID();
        entry.beforeMuted = track->isMuted();
        entry.afterMuted = shouldMute;
        entry.beforeSoloed = track->isSoloed();
        entry.afterSoloed = false;
        changesState = changesState
            || entry.beforeMuted != entry.afterMuted
            || entry.beforeSoloed != entry.afterSoloed;
        entries.push_back(std::move(entry));
    }

    if (changesState)
        DAW::CommandManager::getInstance().execute(
            std::make_unique<DAW::ExclusiveTrackMuteCommand>(tracks, std::move(entries)));
}

void MainComponent::handleMixerFolderDropRequest(const DAW::TrackID& draggedId,
                                                 const DAW::TrackID& targetId,
                                                 DAW::DragMode mode)
{
    if (draggedId.isEmpty() || targetId.isEmpty() || draggedId == targetId)
        return;

    // Master bus never participates in folder stacks (Logic/Reaper convention)
    {
        auto& tm = appCore_.getTrackManager();
        if (auto* dragged = tm.getTrack(draggedId); dragged && dragged->isMaster()) return;
        if (auto* target = tm.getTrack(targetId); target && target->isMaster()) return;
    }

    switch (mode)
    {
        case DAW::DragMode::CreateFolder:
            handleFolderDropRequest(draggedId, targetId);
            break;

        case DAW::DragMode::AdoptIntoFolder:
            addTrackToExistingFolderFromDrop(draggedId, targetId);
            break;

        default:
            return;
    }

    syncFolderCollapseState();
    if (mixerPanel_)
        mixerPanel_->repaint();
    refreshBubblegumFeedback();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();
}

void MainComponent::showFolderDropModePopup(const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Create New Folder Bus");
    menu.addItem(2, "Convert Destination Track to Folder Bus");

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, draggedId, targetId](int result)
        {
            if (result == 1)
                createFolderBusFromDrop(draggedId, targetId);
            else if (result == 2)
                convertDestinationTrackToFolderBus(draggedId, targetId);
        });
}

bool MainComponent::createFolderBusFromDrop(const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
{
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    if (!masterRoute) return false;

    executeTopologyCommand([&]()
    {
        appCore_.getFolderBus().createFolderBus(
            "Folder",
            {draggedId, targetId},
            {},
            appCore_.getRoutingGraph(),
            *masterRoute,
            appCore_.getTrackManager());
    }, "Create Folder Bus");

    return true;
}

bool MainComponent::addTrackToExistingFolderFromDrop(const DAW::TrackID& draggedId, const DAW::TrackID& folderBusId)
{
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    if (!masterRoute)
        return false;

    auto& folderBus = appCore_.getFolderBus();

    // Resolve the effective folder: accept an explicit folder, OR any track
    // that is a child/descendant of a folder (the drop resolver may pass a
    // child row/strip as the target — the child area is one adoption zone).
    DAW::TrackID effectiveFolder = folderBusId;
    if (!folderBus.isFolderBus(effectiveFolder))
    {
        const DAW::TrackID parentId = folderBus.getParentFolderBus(effectiveFolder);
        if (parentId.isNotEmpty() && folderBus.isFolderBus(parentId))
            effectiveFolder = parentId;
    }

    if (!folderBus.isFolderBus(effectiveFolder))
        return false;

    if (folderBus.getParentFolderBus(draggedId) == effectiveFolder)
        return true;

    executeTopologyCommand([&]()
    {
        folderBus.addChildToFolderBus(
            effectiveFolder,
            draggedId,
            appCore_.getRoutingGraph(),
            *masterRoute,
            appCore_.getTrackManager());
    }, "Add Track To Folder Bus");

    return true;
}

bool MainComponent::convertDestinationTrackToFolderBus(const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
{
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    if (!masterRoute) return false;

    executeTopologyCommand([&]()
    {
        appCore_.getFolderBus().convertTrackToFolderBus(
            targetId,
            appCore_.getRoutingGraph(),
            *masterRoute,
            appCore_.getTrackManager());

        appCore_.getFolderBus().addChildToFolderBus(
            targetId,
            draggedId,
            appCore_.getRoutingGraph(),
            *masterRoute,
            appCore_.getTrackManager());
    }, "Convert To Folder Bus");

    return true;
}

void MainComponent::setFolderCollapsed(const DAW::TrackID& folderId, bool collapsed)
{
    if (collapsed)
        collapsedFolderTrackIds_.insert(folderId);
    else
        collapsedFolderTrackIds_.erase(folderId);

    syncFolderCollapseState();
}

bool MainComponent::deleteTracksAsOneTopologyTransaction(
    const std::vector<DAW::TrackID>& requestedIds)
{
    if (requestedIds.empty())
        return false;

    auto& tm = appCore_.getTrackManager();
    std::unordered_set<DAW::TrackID> requested;
    requested.reserve(requestedIds.size());
    for (const auto& id : requestedIds)
        if (id.isNotEmpty())
            requested.insert(id);

    // Resolve the caller's stable IDs against the current project order.  The
    // vector may change as tracks are removed below; the IDs never do.
    std::vector<DAW::TrackID> eligible;
    eligible.reserve(requested.size());
    for (auto* track : tm.getAllTracks())
    {
        if (track == nullptr)
            continue;

        const auto id = track->getID();
        if (requested.count(id) > 0 && tm.canDeleteTrack(id))
            eligible.push_back(id);
    }

    // Master and any policy-protected tracks are deliberately filtered before
    // any project-state capture or mutation.  A fully protected selection is
    // a true no-op: no dirty state, no Undo entry, and no selection rewrite.
    if (eligible.empty())
        return false;

    if (quickSendMode_.isActive())
    {
        const auto sourceId = quickSendMode_.getSourceTrackId();
        if (std::find(eligible.begin(), eligible.end(), sourceId) != eligible.end())
            exitQuickSendMode();
    }

    auto& projectManager = appCore_.getProjectManager();
    const auto before = projectManager.buildState();
    auto& folderBus = appCore_.getFolderBus();
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();
    std::vector<DAW::TrackID> folderBusIds;
    std::vector<DAW::TrackID> regularTrackIds;
    folderBusIds.reserve(eligible.size());
    regularTrackIds.reserve(eligible.size());
    for (const auto& id : eligible)
    {
        if (folderBus.isFolderBus(id))
            folderBusIds.push_back(id);
        else
            regularTrackIds.push_back(id);
    }

    std::vector<DAW::TrackID> deleted;
    deleted.reserve(eligible.size());

    // All lifecycle/property/routing callbacks remain active for subsystem
    // correctness, but they are inside one explicit topology boundary so
    // dirty owners do not publish one user mutation per shifted/deleted track.
    topologyMutationInProgress_ = true;
    tm.beginTopologyMutation();
    // FolderBusCore owns the folder-specific dissolve/reparent operation.  It
    // removes the folder track as part of that canonical operation.
    for (const auto& id : folderBusIds)
    {
        if (tm.getTrack(id) == nullptr)
            continue;

        if (masterRoute == nullptr)
            continue;

        folderBus.dissolveFolderBus(id, appCore_.getRoutingGraph(),
                                     *masterRoute, tm);
        if (tm.getTrack(id) == nullptr)
            deleted.push_back(id);
    }

    // Detach ordinary folder children through FolderBusCore before handing the
    // actual ID batch to TrackManager.  This prevents a deleted child ID from
    // remaining in folder maps or FolderSum topology.
    std::vector<DAW::TrackID> batchTrackIds;
    batchTrackIds.reserve(regularTrackIds.size());
    for (const auto& id : regularTrackIds)
    {
        if (tm.getTrack(id) == nullptr)
            continue;

        if (folderBus.isChildOfAnyFolderBus(id))
        {
            if (masterRoute == nullptr)
                continue;

            folderBus.removeChildFromFolderBus(id,
                                               appCore_.getRoutingGraph(),
                                               *masterRoute,
                                               &tm);
        }
        batchTrackIds.push_back(id);
    }

    const auto regularDeleted = tm.deleteTracks(batchTrackIds);
    deleted.insert(deleted.end(), regularDeleted.begin(), regularDeleted.end());
    tm.endTopologyMutation();
    topologyMutationInProgress_ = false;

    // A policy gate may reject an ID between the preflight and mutation, or a
    // folder bus may be unavailable without its master route.  Do not publish
    // a command for a mutation that did not actually remove a track.
    if (deleted.empty())
        return false;

    // Existing APEX post-delete behavior clears the track selection.  Remove
    // IDs individually first so no dead stable ID survives, then clear any
    // protected/non-deleted members in the same way the prior UI paths did.
    for (const auto& id : deleted)
        multiSelection_.remove(DAW::SelectionTarget::track(id));
    multiSelection_.clearKind(DAW::SelectionKind::Track);
    appCore_.getState().selectedTrackID.setValue(DAW::TrackID{});

    for (const auto& id : deleted)
        collapsedFolderTrackIds_.erase(id);
    syncFolderCollapseState();
    syncTrackSelectionVisuals();

    const auto after = projectManager.buildState();
    CommandManager::getInstance().execute(
        std::make_unique<ProjectTopologyStateCommand>(
            projectManager, before, after, "Delete Tracks", true));

    // CommandManager's undo-history listener is the single user-level dirty
    // owner for this command.  Do not call markProjectDirty here as that would
    // add a second logical dirty event.
    return true;
}

bool MainComponent::deleteTrackOrFolder(const DAW::TrackID& trackId)
{
    return deleteTracksAsOneTopologyTransaction({ trackId });
}

// ── Quick Send mode ─────────────────────────────────────────────────────────

void MainComponent::toggleQuickSendMode(const DAW::TrackID& sourceId)
{
    if (sourceId.isEmpty())
        return;

    if (quickSendMode_.isActive())
    {
        // Double-click the current source again → exit; double-click another
        // row → retarget the source.
        if (quickSendMode_.getSourceTrackId() == sourceId)
            exitQuickSendMode();
        else
            quickSendMode_.enter(sourceId);
    }
    else
    {
        quickSendMode_.enter(sourceId);
    }

    // Auto-open the floating mixer and reveal the source strip so Quick Send
    // can also be driven from the mixer side (parity with the timeline).
    if (quickSendMode_.isActive())
    {
        if (!mixerDocked_ && mixerWindow_ != nullptr)
        {
            if (!mixerWindow_->isVisible())
                mixerWindow_->setVisible(true);
            // Restore from a minimized header-only state so the strips are
            // actually usable, and enforce a usable height if it collapsed.
            if (auto* mw = dynamic_cast<DAW::MixerWindow*>(mixerWindow_.get()))
            {
                mw->restoreIfMinimized();
                if (mw->getHeight() < DAW::MixerWindow::kMixerMinH)
                    mw->setSize(juce::jmax(mw->getWidth(), 800),
                                DAW::MixerWindow::kMixerMinH);
                // Force a re-layout of the content viewport. When the window
                // was minimized the content is hidden and its bounds are only
                // restored by layoutContent(), which runs on resized() — and
                // setSize() is a no-op if the size did not actually change.
                // The explicit call guarantees the strips (not just the header)
                // are visible and sized before the user starts clicking.
                mw->resized();
            }
        }
        if (mixerPanel_ != nullptr)
        {
            mixerPanel_->selectTrackVisual(sourceId);
            mixerPanel_->scrollToTrack(sourceId);
        }

        // Pin the bubblegum source to the Quick Send source WITHOUT opening
        // the bubblegum panel. The cable overlay resolves its source from
        // sourceSync, so cables draw in realtime from the Quick Send source;
        // handleTargetTapFrom's sendExists check prevents duplicates.
        auto& bgV2 = appCore_.getBubblegumV2();
        bgV2.sourceSync.sync(sourceId);
        cableOverlay_.notifyMotion();
    }

    updateQuickSendUI();
    if (quickSendMode_.isActive())
        openQuickWorkflow(DAW::QuickWorkflowTab::QuickSend, {},
                          quickSendMode_.getSourceTrackId());
}

void MainComponent::exitQuickSendMode()
{
    if (!quickSendMode_.isActive())
        return;
    quickSendMode_.exit();

    // Restore Rule 1 (Selected Track = Source): the bubblegum source was
    // pinned to the Quick Send source while the mode was active. Re-sync it
    // to the current selection so cables follow the selection again. The
    // panel stays closed — this is a source sync, not an open().
    auto& bgV2 = appCore_.getBubblegumV2();
    const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
    if (primary.isValid())
        bgV2.sourceSync.sync(primary.trackId);
    cableOverlay_.notifyMotion();

    updateQuickSendUI();
}

void MainComponent::quickSendCreateSend(const DAW::TrackID& targetId)
{
    if (!quickSendMode_.isActive())
        return;

    const auto source = quickSendMode_.getSourceTrackId();
    if (source.isEmpty())
        return;

    // Clicking the source row again exits the mode instead of self-routing.
    if (source == targetId)
    {
        exitQuickSendMode();
        return;
    }

    auto& bgV2 = appCore_.getBubblegumV2();
    bgV2.handleTargetTapFrom(source, targetId);

    // The row click that created this send also changed the track selection
    // (onSelectedWithModifiers fires before handleQuickSendRowClick), which
    // re-synced sourceSync to the target. Re-pin the bubblegum source to the
    // Quick Send source so the cable overlay keeps drawing from the correct
    // source — the panel itself stays closed.
    bgV2.sourceSync.sync(source);

    refreshBubblegumFeedback();
    updateQuickSendUI();
    cableOverlay_.notifyMotion();
    cableOverlay_.notifyMotionFinished();
}

void MainComponent::quickSendDeleteSend()
{
    if (!quickSendMode_.isActive())
        return;

    const auto source = quickSendMode_.getSourceTrackId();
    const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
    if (source.isEmpty() || !primary.isValid())
        return;

    quickSendDeleteSendTo(primary.trackId);
}

void MainComponent::quickSendDeleteSendTo(const DAW::TrackID& targetId)
{
    if (!quickSendMode_.isActive())
        return;

    const auto source = quickSendMode_.getSourceTrackId();
    if (source.isEmpty() || targetId.isEmpty())
        return;

    auto& bg = appCore_.getBubblegumV2();
    bg.removeSendFrom(source, targetId);

    // Re-pin the bubblegum source to the Quick Send source (see
    // quickSendCreateSend — the selection may have moved during the gesture).
    bg.sourceSync.sync(source);

    refreshBubblegumFeedback();
    cableOverlay_.notifyMotion();
    updateQuickSendUI();
}

void MainComponent::updateQuickSendUI()
{
    if (trackList_)
        trackList_->setQuickSendMode(quickSendMode_.isActive(), quickSendMode_.getSourceTrackId());
    if (mixerPanel_)
        mixerPanel_->setQuickSendMode(quickSendMode_.isActive(), quickSendMode_.getSourceTrackId());

    // The floating Quick Send popup was removed by design — only the gold
    // source aura remains. Receiving rows are highlighted with the send's
    // colour, sourced from the live routing graph.
    if (trackList_ && quickSendMode_.isActive())
    {
        auto& bgV2 = appCore_.getBubblegumV2();
        const auto source = quickSendMode_.getSourceTrackId();
        auto& tm = appCore_.getTrackManager();
        std::vector<std::pair<DAW::TrackID, float>> targets;
        targets.reserve(tm.getNumTracks());
        for (int i = 0; i < tm.getNumTracks(); ++i)
        {
            auto* track = tm.getTrack(i);
            if (track == nullptr)
                continue;
            const bool isSendTarget = bgV2.hasSendTo(track->getID());
            if (isSendTarget)
                targets.emplace_back(track->getID(), bgV2.getSendLevelTo(track->getID()));
            // Set receive-target highlight: true for all tracks except the source
            const bool isReceiveTarget = (track->getID() != source);
            if (trackList_)
                trackList_->setTrackQuickSendReceiveTarget(track->getID(), isReceiveTarget);
            if (mixerPanel_)
                mixerPanel_->setStripQuickSendReceiveTarget(track->getID(), isReceiveTarget);
        }
        trackList_->applyBubblegumFeedback(source, targets, cableOverlay_.getCableAccentColour());
    }
    else
    {
        // When not active, ensure all highlights are off
        if (trackList_)
        {
            auto& tm = appCore_.getTrackManager();
            for (int i = 0; i < tm.getNumTracks(); ++i)
            {
                auto* track = tm.getTrack(i);
                if (track == nullptr)
                    continue;
                if (trackList_)
                    trackList_->setTrackQuickSendReceiveTarget(track->getID(), false);
                if (mixerPanel_)
                    mixerPanel_->setStripQuickSendReceiveTarget(track->getID(), false);
            }
        }
        if (trackList_)
            trackList_->clearBubblegumFeedback();
    }
}

void MainComponent::refreshPreFaderStates()
{
    auto& bgV2 = appCore_.getBubblegumV2();
    auto query = [&bgV2](const DAW::TrackID& id) { return bgV2.getSendPreFaderSummaryFrom(id); };
    if (mixerPanel_)
        mixerPanel_->refreshPreFaderStates(query);
    if (trackList_)
        trackList_->refreshPreFaderStates(query);
}

void MainComponent::handlePreFaderTogglePressed(const DAW::TrackID& sourceId)
{
    auto& bgV2 = appCore_.getBubblegumV2();

    // Gather every send target with its current pre/post-fader state.
    auto targets = bgV2.getSendPreFaderTargetsFrom(sourceId);
    if (targets.empty())
        return;   // nothing to toggle — button shows "—"

    if (targets.size() == 1)
    {
        // Single send — toggle it directly.
        bgV2.togglePreFaderFromTo(sourceId, targets.front().first);
        refreshBubblegumFeedback();
        return;
    }

    // Multiple sends — per-target menu.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::PopupMenu menu;
    menu.addSectionHeader("Send Pre/Post-Fader");
    for (const auto& [targetId, isPre] : targets)
    {
        juce::String targetName = targetId;
        auto* track = appCore_.getTrackManager().getTrack(targetId);
        if (track != nullptr)
            targetName = track->getName();

        const juce::String label = targetName + (isPre ? "  [PRE]" : "  [POST]");
        menu.addItem(label, true, isPre, [safeThis, sourceId, targetId]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            auto& bg = self->appCore_.getBubblegumV2();
            bg.togglePreFaderFromTo(sourceId, targetId);
            self->refreshBubblegumFeedback();
        });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withMinimumWidth(220)
                           .withTargetScreenArea(juce::Rectangle<int>(juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition())));
}

void MainComponent::refreshBubblegumFeedback()
{
    if (mixerPanel_)
        mixerPanel_->repaint();

    // POST/PRE buttons follow the live routing graph (sends created, deleted
    // or re-tapped pre/post-fader from any surface).
    refreshPreFaderStates();

    // If sourceSync has no source (e.g. send created from full matrix without
    // a selected track), fall back to the mixer's selected track so the overlay
    // paint() doesn't bail early at sourceId.isEmpty().
    auto& bgV2 = appCore_.getBubblegumV2();
    bool sourceWasSynchronized = false;
    if (bgV2.sourceSync.getSourceTrackId().isEmpty() && mixerPanel_)
    {
        const auto selId = mixerPanel_->getSelectedTrackId();
        if (selId.isNotEmpty())
        {
            bgV2.sourceSync.sync(selId);
            sourceWasSynchronized = true;
        }
    }

    // Invalidate the cable overlay snapshot cache so send cables
    // appear immediately without requiring a mixer click.
    if (sourceWasSynchronized)
        cableOverlay_.notifyMotion();
    else
        cableOverlay_.notifyMotion();
}

void MainComponent::refreshBubblegumOffscreenState()
{
    if (!mixerPanel_)
        return;

    const bool mixerVisible = mixerDocked_ ? mixerViewport_.isVisible()
                                           : (mixerWindow_ && mixerWindow_->isVisible());

    // Only resize when bounds have actually changed to avoid layout thrash at 60 Hz.
    if (offscreenEndpoint_.getBounds() != getLocalBounds())
        offscreenEndpoint_.setBounds(getLocalBounds());

    const auto mainBounds = getLocalBounds();

    // Resolve the mixer geometry first: the endpoint anchor rects depend on it,
    // and the occlusion test below needs those anchors.
    juce::Rectangle<int> mixerBounds;
    juce::Rectangle<int> viewportBounds;
    if (mixerVisible)
    {
        if (mixerDocked_)
        {
            mixerBounds = mixerViewport_.getBounds();
            viewportBounds = mixerViewport_.getBounds();
        }
        else if (mixerWindow_)
        {
            // MixerWindow is a MainComponent child. Convert from the component
            // itself instead of reinterpreting parent-local bounds as screen space.
            mixerBounds = getLocalArea(mixerWindow_.get(), mixerWindow_->getLocalBounds());
            viewportBounds = getLocalArea(&mixerViewport_, mixerViewport_.getLocalBounds());
        }
        offscreenEndpoint_.setMixerBoundsInLocal(mixerBounds.toFloat());
    }

    // Offscreen bubbles are shown when the Mixer is visible and Bubblegum
    // routing is active (or the existing force-visible option is enabled).
    const bool offscreenActive = mixerVisible
        && (appCore_.getBubblegumV2().isActive() || mixerPanel_->isOffscreenForceVisible());

    offscreenEndpoint_.setMixerVisible(offscreenActive);

    if (!offscreenActive)
    {
        offscreenRefreshCache_.active = false;
        offscreenRefreshCache_.effectiveSource = {};
        offscreenRefreshCache_.graphVersion = 0;
        return;
    }

    if (offscreenEndpoint_.getBounds() != mainBounds)
        offscreenEndpoint_.setBounds(mainBounds);

    auto& bgV2 = appCore_.getBubblegumV2();
    const auto sourceId = bgV2.sourceSync.getSourceTrackId();

    // When Bubblegum is closed but force-visible is on, use the selected track
    // as the implicit source so we can still query the routing graph for sends.
    const bool bgActive = bgV2.isActive();
    DAW::TrackID effectiveSource = sourceId;
    if (effectiveSource.isEmpty())
    {
        auto selId = appCore_.getState().selectedTrackID.getValue();
        if (selId.toString().isNotEmpty())
            effectiveSource = selId.toString();
    }

    const uint64_t graphVersion = bgV2.routingGraph != nullptr
        ? bgV2.routingGraph->getGraphVersion()
        : 0;

    const bool refreshInputsChanged = !offscreenRefreshCache_.active
        || offscreenRefreshCache_.mixerDocked != mixerDocked_
        || offscreenRefreshCache_.viewportScrollX != mixerViewport_.getViewPositionX()
        || offscreenRefreshCache_.mainBounds != mainBounds
        || offscreenRefreshCache_.mixerBounds != mixerBounds
        || offscreenRefreshCache_.viewportBounds != viewportBounds
        || offscreenRefreshCache_.effectiveSource != effectiveSource
        || offscreenRefreshCache_.graphVersion != graphVersion;

    if (!refreshInputsChanged)
        return;

    offscreenRefreshCache_.active = true;
    offscreenRefreshCache_.mixerDocked = mixerDocked_;
    offscreenRefreshCache_.viewportScrollX = mixerViewport_.getViewPositionX();
    offscreenRefreshCache_.mainBounds = mainBounds;
    offscreenRefreshCache_.mixerBounds = mixerBounds;
    offscreenRefreshCache_.viewportBounds = viewportBounds;
    offscreenRefreshCache_.effectiveSource = effectiveSource;
    offscreenRefreshCache_.graphVersion = graphVersion;

    std::map<DAW::TrackID, float> targetPositions;
    std::map<DAW::TrackID, juce::String> targetNames;
    std::map<DAW::TrackID, int> targetNumbers;
    std::map<DAW::TrackID, float> sendLevels;
    std::map<DAW::TrackID, bool> sendActiveStates;
    std::map<DAW::TrackID, bool> sidechainActiveStates;
    std::set<DAW::TrackID> sendIds;
    std::set<DAW::TrackID> sidechainIds;

    int trackNumber = 1;

    std::unordered_map<DAW::TrackID, DAW::MixerStrip*> visibleStrips;
    visibleStrips.reserve(mixerPanel_->getStrips().size());
    for (auto* strip : mixerPanel_->getStrips())
        if (strip != nullptr && strip->isVisible())
            visibleStrips[strip->getTrack().getID()] = strip;

    const auto* folderCore = mixerPanel_->getFolderBusCore();
    const auto& collapsedFolders = mixerPanel_->getCollapsedFolderBuses();
    auto resolveVisibleStrip = [&](const DAW::TrackID& trackId) -> DAW::MixerStrip*
    {
        const auto representative = DAW::BubblegumFolderProjectionCore::resolveVisibleRepresentative(
            trackId, folderCore, collapsedFolders,
            [&visibleStrips](const DAW::TrackID& candidate)
            {
                return visibleStrips.find(candidate) != visibleStrips.end();
            });
        auto visible = visibleStrips.find(representative);
        return visible != visibleStrips.end() ? visible->second : nullptr;
    };
    mixerPanel_->onExclusiveTrackMute = [this](const DAW::TrackID& id)
    {
        exclusiveMuteTrack(id);
    };
    mixerPanel_->onStripQuickSendToggleRequested = [this](const DAW::TrackID& id)
    {
        toggleQuickSendMode(id);
    };
    mixerPanel_->onQuickSendToggleSendRequested = [this](const DAW::TrackID& id)
    {
        if (quickSendMode_.isActive())
        {
            const auto source = quickSendMode_.getSourceTrackId();
            if (source.isNotEmpty() && id != source)
            {
                // Toggle send: if exists, remove; else create. Query with the
                // explicit Quick Send source (see timeline onQuickSendToggleSend).
                if (appCore_.getBubblegumV2().hasSendFromTo(source, id))
                {
                    quickSendDeleteSendTo(id);
                }
                else
                {
                    quickSendCreateSend(id);
                }
            }
        }
    };
    mixerPanel_->onQuickSendToggleSidechainRequested = [this](const DAW::TrackID& id)
    {
        if (quickSendMode_.isActive())
        {
            const auto source = quickSendMode_.getSourceTrackId();
            if (source.isNotEmpty() && id != source)
            {
                // Toggle sidechain: if exists, remove; else create
                auto& bgV2 = appCore_.getBubblegumV2();
                if (bgV2.hasSidechainFromTo(source, id))
                    bgV2.removeSidechainFrom(source, id);
                else
                    bgV2.handleSidechainTapFrom(source, id);
                refreshBubblegumFeedback();
                updateQuickSendUI();
                cableOverlay_.notifyMotionFinished();
            }
        }
    };
    mixerPanel_->onStripPreFaderToggled = [this](const DAW::TrackID& id)
    {
        handlePreFaderTogglePressed(id);
    };

    // Helper: query send/sidechain existence using effectiveSource directly on the graph
    // when bgV2 panel is closed (sourceSync is empty).
    auto trackHasSend = [&](const DAW::TrackID& tid) -> bool
    {
        if (bgActive) return bgV2.hasSendTo(tid);
        if (!bgV2.routingGraph || effectiveSource.isEmpty()) return false;
        return bgV2.sendState.sendExists(*bgV2.routingGraph, effectiveSource, tid);
    };
    auto trackHasSidechain = [&](const DAW::TrackID& tid) -> bool
    {
        if (bgActive) return bgV2.hasSidechainTo(tid);
        if (!bgV2.routingGraph || effectiveSource.isEmpty()) return false;
        return bgV2.sidechainState.sidechainExists(*bgV2.routingGraph, effectiveSource, tid);
    };
    auto trackSendLevel = [&](const DAW::TrackID& tid) -> float
    {
        if (bgActive) return bgV2.getSendLevelTo(tid);
        if (!bgV2.routingGraph || effectiveSource.isEmpty()) return 0.f;
        return bgV2.sendLevel.getLevel(*bgV2.routingGraph, effectiveSource, tid);
    };
    auto trackSendActive = [&](const DAW::TrackID& tid) -> bool
    {
        if (bgActive) return bgV2.isSendActive(tid);
        if (!bgV2.routingGraph || effectiveSource.isEmpty()) return false;
        return bgV2.sendState.isSendActive(*bgV2.routingGraph, effectiveSource, tid);
    };
    auto trackSidechainActive = [&](const DAW::TrackID& tid) -> bool
    {
        if (bgActive) return bgV2.isSidechainActive(tid);
        if (!bgV2.routingGraph || effectiveSource.isEmpty()) return false;
        return bgV2.sidechainState.isSidechainActive(*bgV2.routingGraph, effectiveSource, tid);
    };

    auto addTrackIfTargeted = [&](DAW::Track* track)
    {
        if (track == nullptr)
            return;

        const auto trackId = track->getID();
        const bool hasSend      = trackHasSend(trackId);
        const bool hasSidechain = trackHasSidechain(trackId);
        if (trackId != effectiveSource && (hasSend || hasSidechain))
        {
            // Use the actual visible strip geometry. This automatically covers
            // folder indentation/collapse, horizontal scrolling, and the pinned
            // 170px Master strip instead of synthesising incompatible widths.
            auto* visibleStrip = resolveVisibleStrip(trackId);
            if (visibleStrip == nullptr)
            {
                ++trackNumber;
                return;
            }
            const auto stripBounds = getLocalArea(visibleStrip, visibleStrip->getLocalBounds());
            targetPositions[trackId] = (float) stripBounds.getCentreX();
            targetNames[trackId]     = track->getName();
            targetNumbers[trackId]   = trackNumber;
            sendLevels[trackId]      = hasSend ? trackSendLevel(trackId) : 0.0f;
            sendActiveStates[trackId]     = hasSend      ? trackSendActive(trackId)     : false;
            sidechainActiveStates[trackId]= hasSidechain ? trackSidechainActive(trackId): false;
            if (hasSend)      sendIds.insert(trackId);
            if (hasSidechain) sidechainIds.insert(trackId);
        }

        ++trackNumber;
    };

    auto& tm = appCore_.getTrackManager();
    for (int i = 0; i < tm.getNumTracks(); ++i)
        addTrackIfTargeted(tm.getTrack(i));

    if (tm.hasMasterTrack())
        addTrackIfTargeted(tm.getMasterTrack());

    offscreenEndpoint_.updateOffscreenTargets(
        viewportBounds.toFloat(),
        targetPositions,
        targetNames,
        targetNumbers,
        sendLevels,
        sendActiveStates,
        sendIds,
        sidechainIds,
        sidechainActiveStates);
}

bool MainComponent::isMixerOpen() const noexcept
{
    return mixerDocked_ ? mixerViewport_.isVisible()
                        : (mixerWindow_ != nullptr && mixerWindow_->isVisible());
}

void MainComponent::cycleMixerSelection(int direction)
{
    if (mixerPanel_ == nullptr)
        return;

    auto& tm = appCore_.getTrackManager();
    const int numTracks = tm.getNumTracks();
    const bool hasMaster = tm.hasMasterTrack();
    const int total = numTracks + (hasMaster ? 1 : 0);
    if (total <= 0)
        return;

    // Current selection: prefer the mixer's own selection, fall back to app state.
    DAW::TrackID currentId = mixerPanel_->getSelectedTrackId();
    if (currentId.isEmpty())
        currentId = appCore_.getState().selectedTrackID.getValue().toString();

    int index = -1;
    for (int i = 0; i < numTracks; ++i)
        if (auto* t = tm.getTrack(i))
            if (t->getID() == currentId) { index = i; break; }
    if (index < 0 && hasMaster)
        if (auto* m = tm.getMasterTrack())
            if (m->getID() == currentId) index = numTracks;

    // No valid current selection → pick a sensible starting strip.
    if (index < 0)
        index = (direction > 0) ? -1 : 0;

    const int next = (index + direction + total) % total;

    DAW::TrackID targetId;
    if (next < numTracks)
    {
        if (auto* t = tm.getTrack(next))
            targetId = t->getID();
    }
    else if (hasMaster)
    {
        if (auto* m = tm.getMasterTrack())
            targetId = m->getID();
    }

    if (targetId.isEmpty())
        return;

    // Change the selected track. The mixer selection is the keyboard
    // navigation authority here; selectTrack fires onTrackSelected which
    // syncs the DAW selection, timeline, Bubblegum and plugin panel.
    mixerPanel_->selectTrack(targetId);

    // One consistent rule: keyboard navigation that changes the selected
    // track must reveal that track in the mixer viewport. scrollMixerViewportToTrack
    // scrolls ONLY when the strip is not already fully visible (minimal delta,
    // no re-centering, no oscillation).
    scrollMixerViewportToTrack(targetId);

    // Realtime cable + offscreen-state refresh: every visible Bubblegum cable
    // (sends, sidechains, master) must reroute to the newly selected source
    // immediately, exactly like a strip click. (selectTrack already triggers
    // onTrackSelected; these calls are idempotent and guarantee the overlay
    // re-syncs even if the callback path changes later.)
    cableOverlay_.notifyMotion();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();
    if (mixerPanel_)
        mixerPanel_->repaint();
}

void MainComponent::scrollMixerViewportToTrack(const DAW::TrackID& trackId)
{
    if (mixerPanel_ == nullptr)
        return;

    // Reveal the target strip in the mixer viewport — but ONLY when it is not
    // already fully visible. Minimal scroll delta; never re-centers (that
    // would cause unnecessary motion and oscillation during repeated
    // keyboard navigation). The reveal decision is the pure, unit-tested
    // DAW::MixerPanel::computeRevealScrollX.
    const auto& strips = mixerPanel_->getStrips();
    for (auto* strip : strips)
    {
        if (strip && strip->getTrack().getID() == trackId)
        {
            // The Master is pinned to the right edge and is already visible;
            // never scroll normal strips underneath it merely because Master
            // became the selected track.
            if (strip->getTrack().isMaster())
                return;

            // A strip can already be clipped by the pinned Master hard wall.
            // Its current component width is therefore only the rendered
            // portion, not the width required to decide whether the strip is
            // fully revealed.  Reveal against the strip's full preferred
            // width so a partial strip cannot falsely pass the visibility test.
            const int stripW = juce::jmax(1, strip->getPreferredWidth());
            const int viewW  = mixerViewport_.getViewWidth();
            const int safeW  = DAW::MixerPanel::computeSafeNormalViewportWidth(
                viewW, mixerPanel_->getMasterStripWidth());
            const int curX   = mixerViewport_.getViewPositionX();
            auto* viewed     = mixerViewport_.getViewedComponent();
            const int maxX   = viewed ? juce::jmax(0, viewed->getWidth() - viewW) : 0;
            const int newX   = DAW::MixerPanel::computeRevealScrollX(
                strip->getBounds().getX(), stripW, safeW, curX, maxX);
            if (newX != curX)
                mixerViewport_.setViewPosition(newX, 0);
            return;
        }
    }
}

void MainComponent::connectionAdded(DAW::RoutingConnection* conn)
{
    if (!topologyMutationInProgress_
        && !appCore_.getTrackManager().isRestoringState())
        appCore_.markProjectDirty("routing_changed");
    refreshBubblegumFeedback();
    refreshBubblegumOffscreenState();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    // Routing mutation → invalidate the cable overlay's topology cache.
    // A bare repaint() would redraw the stale cached frame (the
    // selection-change-only refresh bug): the new cable must appear
    // immediately without requiring a track reselection.
    cableOverlay_.requestTopologyRefresh();
}

void MainComponent::connectionRemoved(const juce::String& connId)
{
    if (!topologyMutationInProgress_
        && !appCore_.getTrackManager().isRestoringState())
        appCore_.markProjectDirty("routing_changed");
    refreshBubblegumFeedback();
    refreshBubblegumOffscreenState();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    // Same canonical invalidation: removed cables disappear immediately.
    cableOverlay_.requestTopologyRefresh();
}

void MainComponent::trackAdded(DAW::Track*)
{
    if (appCore_.getTrackManager().isRestoringState())
        return;
    if (!topologyMutationInProgress_)
        appCore_.markProjectDirty("track_added");

    auto& bgV2 = appCore_.getBubblegumV2();
    if (bgV2.isActive() && bgV2.trackManager)
        bgV2.targetList.rebuild(bgV2.sourceSync.getSourceTrackId(), *bgV2.trackManager);

    updateTimelineViewportContentBounds();
    resized();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();
}

void MainComponent::openQuickWorkflow(DAW::QuickWorkflowTab tab,
                                      const juce::Rectangle<int>& anchorOverride,
                                      const DAW::TrackID& sourceId)
{
    if (tab == DAW::QuickWorkflowTab::QuickSend
        && !DAW::kQuickSendWorkflowUiEnabled)
        return;

    // One presentation shell for all three Quick Workflow tabs.  CallOutBox
    // owns the popup lifetime; all model mutations stay in QuickWorkflowCore.
    if (quickWorkflow_ == nullptr)
        return;

    auto anchor = anchorOverride;
    if (anchor.isEmpty() && arrangement_ != nullptr)
    {
        auto* core = arrangement_->getCore();
        if (core != nullptr)
        {
            const auto toolbarAnchor = core->getToolbar().getAddTrackButtonScreenBounds();
            if (!toolbarAnchor.isEmpty())
                anchor = toolbarAnchor;
        }
    }
    if (anchor.isEmpty())
        return;

    auto effectiveSource = sourceId;
    if (effectiveSource.isEmpty())
    {
        const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
        if (primary.isValid())
            effectiveSource = primary.trackId;
    }

    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto popup = std::make_unique<DAW::QuickWorkflowPopup>(*quickWorkflow_, effectiveSource);
    popup->onApply = [safeThis](DAW::QuickWorkflowTab workflowTab,
                                std::vector<DAW::QuickWorkflowRequest> requests)
    {
        DAW::QuickTrackBatchResult result;
        auto* self = safeThis.getComponent();
        if (self == nullptr || self->quickWorkflow_ == nullptr)
            return result;

        auto& projectManager = self->appCore_.getProjectManager();
        const auto before = projectManager.buildState();
        result = self->quickWorkflow_->apply(workflowTab, requests);
        if (!result.ok())
            return result;

        const auto after = projectManager.buildState();
        if (before.isEquivalentTo(after))
            return result;

        const auto description = workflowTab == DAW::QuickWorkflowTab::QuickRoute
            ? "Quick Route Workflow" : "Quick Track Workflow";
        CommandManager::getInstance().execute(
            std::make_unique<ProjectTopologyStateCommand>(
                projectManager, before, after, description, true));
        self->appCore_.markProjectDirty("quick_workflow");
        return result;
    };
    popup->onSendMutation = [safeThis](std::function<bool()> mutation,
                                       juce::String description)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr || !mutation)
            return false;

        auto& projectManager = self->appCore_.getProjectManager();
        const auto before = projectManager.buildState();
        if (!mutation())
            return false;

        const auto after = projectManager.buildState();
        if (before.isEquivalentTo(after))
            return false;

        CommandManager::getInstance().execute(
            std::make_unique<ProjectTopologyStateCommand>(
                projectManager, before, after, std::move(description), true));
        self->appCore_.markProjectDirty("quick_workflow_send");
        self->refreshBubblegumFeedback();
        self->updateQuickSendUI();
        self->resized();
        return true;
    };
    popup->showTab(tab);
    popup->onChanged = [safeThis]
    {
        if (auto* self = safeThis.getComponent())
        {
            self->refreshBubblegumFeedback();
            self->updateQuickSendUI();
            self->resized();
        }
    };
    juce::CallOutBox::launchAsynchronously(std::move(popup), anchor, nullptr);
}

bool MainComponent::copyPluginBetweenTracks(const DAW::TrackID& srcTrack,
                                             int srcSlot,
                                             const DAW::TrackID& destTrack)
{
    if (srcTrack.isEmpty() || destTrack.isEmpty() || srcTrack == destTrack || srcSlot < 0)
        return false;

    auto* srcChain = appCore_.getPluginChain(srcTrack);
    auto* dstChain = appCore_.getPluginChain(destTrack);
    if (srcChain == nullptr || dstChain == nullptr)
        return false;

    auto before = dstChain->getState();
    juce::String error;
    const int destinationSlot = dstChain->copyPluginFrom(
        *srcChain, srcSlot, appCore_.getPluginScanner().getFormatManager(), error);
    if (destinationSlot < 0)
    {
        if (error.isNotEmpty())
            DBG("[MainComponent] Plugin copy failed: " + error);
        return false;
    }

    auto after = dstChain->getState();
    if (before.isEquivalentTo(after))
        return false;

    // copyPluginFrom has already applied the control-plane mutation.  The
    // state command records exactly one logical Undo transaction; its redo
    // restores the destination state and never touches the source chain.
    CommandManager::getInstance().execute(std::make_unique<PluginChainStateCommand>(
        *dstChain, appCore_.getPluginScanner().getFormatManager(),
        before, after, "Copy Plugin", true));
    appCore_.markProjectDirty("plugin_copy");

    if (pluginSidePanel_ && slimeSidePanel_ && slimeSidePanel_->isOpen())
    {
        const auto selectedId = appCore_.getState().selectedTrackID.getValue().toString();
        if (selectedId == destTrack)
        {
            auto* track = appCore_.getTrackManager().getTrack(destTrack);
            pluginSidePanel_->setTrack(track, dstChain);
        }
    }

    return true;
}

void MainComponent::trackRemoved(const DAW::TrackID&)
{
    if (appCore_.getTrackManager().isRestoringState())
        return;
    if (!topologyMutationInProgress_)
        appCore_.markProjectDirty("track_removed");

    auto& bgV2 = appCore_.getBubblegumV2();
    if (bgV2.isActive() && bgV2.trackManager)
        bgV2.targetList.rebuild(bgV2.sourceSync.getSourceTrackId(), *bgV2.trackManager);

    resized();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();
}

void MainComponent::trackOrderChanged()
{
    auto& bgV2 = appCore_.getBubblegumV2();
    if (bgV2.isActive() && bgV2.trackManager)
        bgV2.targetList.rebuild(bgV2.sourceSync.getSourceTrackId(), *bgV2.trackManager);

    updateTimelineViewportContentBounds();
    resized();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    refreshBubblegumOffscreenState();
    cableOverlay_.repaint();
}

void MainComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double /*newRangeStart*/)
{
    // Mixer horizontal scroll - sync cables and offscreen indicators immediately
    // without waiting for the 60 Hz timer poll.
    if (scrollBar == &mixerViewport_.getHorizontalScrollBar())
    {
        if (mixerPanel_) mixerPanel_->updateMasterStripPosition();
        cableOverlay_.notifyMotion();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
        return;
    }

    if (timelineViewport_ != nullptr
        && (scrollBar == &timelineViewport_->getHorizontalScrollBar()
            || scrollBar == &timelineViewport_->getVerticalScrollBar()))
    {
        if (arrangement_)
            arrangement_->setViewportScrollOffsets(timelineViewport_->getViewPositionX(), timelineViewport_->getViewPositionY());
        if (trackList_)
            trackList_->setScrollOffset(timelineViewport_->getViewPositionY());
        return;
    }

    resized();
}

void MainComponent::componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized)
{
    if (&component != mixerWindow_.get())
        return;

    // Any user (or programmatic) move/resize counts as "placed": resized()
    // must never re-place the mixer over the app header afterwards.
    mixerWindowPlaced_ = true;

    if (wasResized || wasMoved)
    {
        cableOverlay_.notifyMotion();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
        if (slimeSidePanel_ && mixerPanel_)
            updateSlimeAttachedPosition();
    }
}

void MainComponent::childrenChanged()
{
    juce::Component::childrenChanged();

    // Keep the mixer's attached FX side panel stacked with the mixer:
    // whenever the mixer stack (floating window or docked viewport) is moved
    // UP in z-order, the slime panel must come with it instead of staying
    // behind other floating windows. The index-delta guard ensures bringing
    // OTHER windows forward (browser, piano roll, ...) never lifts the slime.
    const juce::Component* mixerContainer = mixerDocked_
        ? static_cast<const juce::Component*>(&mixerViewport_)
        : mixerWindow_.get();

    if (mixerContainer == nullptr || !mixerContainer->isVisible())
        return;

    const int idx = getIndexOfChildComponent(mixerContainer);
    if (idx < 0)
        return;

    const bool mixerMovedUp = (lastMixerZIndex_ >= 0 && idx > lastMixerZIndex_);
    lastMixerZIndex_ = idx;

    if (mixerMovedUp && slimeSidePanel_ && slimeSidePanel_->isAttached() && slimeSidePanel_->isVisible())
        slimeSidePanel_->toFront(false);

}

void MainComponent::updateSlimeAttachedPosition()
{
    if (!slimeSidePanel_ || !mixerWindow_ || !slimeSidePanel_->isAttached())
        return;

    auto mixerBounds = mixerWindow_->getBounds();
    auto anchorLocal = getLocalPoint(nullptr, mixerBounds.getTopRight());

    int posX = anchorLocal.x + 5;
    int posY = anchorLocal.y + 18; // offset so slime top aligns with mixer top
    slimeSidePanel_->setTopLeftPosition(posX, posY);
}

void MainComponent::setupBubblePhysics()
{
    // Bubble physics setup - placeholder
}

void MainComponent::undockMixer()
{
    mixerDocked_ = false;
    mixerPanel_->setDockedState(false);
    // Re-attach the viewport to the window (dockMixer reparents it back to MainComponent)
    mixerWindow_->setContent(&mixerViewport_);
    mixerViewport_.setVisible(true);
    mixerWindow_->setVisible(true);
    mixerBubble_->setVisible(false);
    mixerWindow_->onCloseClicked = [this]
    {
        mixerWindow_->setVisible(false);
        if (mixerBubble_) mixerBubble_->setVisible(true);
        if (slimeSidePanel_ && slimeSidePanel_->isAttached())
            slimeSidePanel_->setVisible(false);
    };
    resized();
    mixerWindow_->resized();
}

void MainComponent::dockMixer()
{
    mixerDocked_ = true;
    mixerPanel_->setDockedState(true);
    addChildComponent(mixerViewport_);
    mixerViewport_.setViewedComponent(mixerPanel_.get(), false);
    mixerViewport_.setVisible(true);
    mixerWindow_->setVisible(false);
    mixerBubble_->setVisible(false);
    resized();
}

void MainComponent::initializeSafeStartupAudio()
{
    DAW::installSafeAudioDeviceTypes(getOwnedDeviceManager());
    selectSafeStartupAudioDeviceType();
    setAudioChannels(kMaxHardwareInputChannels, 2);

    if (!repairInvalidCurrentAudioDevice())
    {
        shutdownAudio();
        selectSafeStartupAudioDeviceType();
        setAudioChannels(kMaxHardwareInputChannels, 2);
    }

    recoverFromFailedAsioPlaybackInit();

    ensureInputChannelsActive("startup");

    cachedEnabledAudioInputChannels_.store(getEnabledAudioInputChannelCount(), std::memory_order_release);
    appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));

    asioZombieRestartAttempts_ = 0;
    armAsioCallbackVerification("startup");

    // Watchdog: heals input channels that die mid-session (device change,
    // WASAPI session drop, privacy toggle) - bounded retries, never during a take.
    inputWatchdog_.startTimer(2000);
}

void MainComponent::selectSafeStartupAudioDeviceType()
{
    auto currentType = getOwnedDeviceManager().getCurrentAudioDeviceType();
    if (currentType.isNotEmpty() && !currentType.containsIgnoreCase("asio"))
        return;

    auto& types = getOwnedDeviceManager().getAvailableDeviceTypes();

    auto findTypeName = [&types](auto predicate) -> juce::String
    {
        for (auto* type : types)
            if (type != nullptr && predicate(type->getTypeName()))
                return type->getTypeName();
        return {};
    };

    auto safeType = findTypeName([](const juce::String& name)
    {
        return name.containsIgnoreCase("windows audio")
            && !name.containsIgnoreCase("exclusive")
            && !name.containsIgnoreCase("asio");
    });

    if (safeType.isEmpty())
        safeType = findTypeName([](const juce::String& name)
        {
            return name.containsIgnoreCase("directsound")
                && !name.containsIgnoreCase("asio");
        });

    if (safeType.isEmpty())
        safeType = findTypeName([](const juce::String& name)
        {
            return !name.containsIgnoreCase("asio");
        });

    if (safeType.isNotEmpty() && safeType != currentType)
    {
        prepareForAudioDeviceMutation();
        getOwnedDeviceManager().setCurrentAudioDeviceType(safeType, true);
    }
}

void MainComponent::suspendAudioForDevicePanel()
{
    if (audioSuspendedForDevicePanel_)
        return;

    DBG("[AudioDevice] suspendAudioForDevicePanel currentType=" + deviceManager.getCurrentAudioDeviceType()
        + " hasDevice=" + juce::String(deviceManager.getCurrentAudioDevice() != nullptr ? 1 : 0));

    audioWasRunningBeforeDevicePanel_ = getOwnedDeviceManager().getCurrentAudioDevice() != nullptr;
    shutdownAudio();
    audioSuspendedForDevicePanel_ = true;
}

bool MainComponent::recoverFromFailedAsioPlaybackInit()
{
    auto* currentType = getOwnedDeviceManager().getCurrentDeviceTypeObject();
    if (currentType == nullptr || !currentType->getTypeName().containsIgnoreCase("asio"))
        return false;

    auto* device = getOwnedDeviceManager().getCurrentAudioDevice();
    const auto lastError = device != nullptr ? device->getLastError().trim() : juce::String();

    // ASIO4ALL starts its callback thread asynchronously - isPlaying() may be false
    // immediately after open() even when the driver opened successfully.  Treat an
    // open, error-free ASIO4ALL device as healthy regardless of isPlaying().
    const bool isAsio4ALL = device != nullptr
        && device->getName().containsIgnoreCase("asio4all");

    const bool asioPlaybackHealthy = device != nullptr
        && device->isOpen()
        && lastError.isEmpty()
        && (device->isPlaying() || isAsio4ALL);

    DBG("[AudioDevice] recoverFromFailedAsioPlaybackInit device="
        + (device != nullptr ? device->getName() : juce::String("<null>"))
        + " open=" + juce::String(device != nullptr && device->isOpen() ? 1 : 0)
        + " playing=" + juce::String(device != nullptr && device->isPlaying() ? 1 : 0)
        + " isAsio4ALL=" + juce::String(isAsio4ALL ? 1 : 0)
        + " lastError=" + (lastError.isEmpty() ? juce::String("<none>") : lastError)
        + " healthy=" + juce::String(asioPlaybackHealthy ? 1 : 0));

    if (asioPlaybackHealthy)
        return false;

    DBG("[AudioDevice] ASIO playback init failed; falling back to a safe startup device. lastError="
        + (lastError.isEmpty() ? juce::String("<none>") : lastError));

    shutdownAudio();
    selectSafeStartupAudioDeviceType();

    auto* fallbackType = getOwnedDeviceManager().getCurrentDeviceTypeObject();
    if (fallbackType == nullptr || fallbackType->getTypeName().containsIgnoreCase("asio"))
        return false;

    setAudioChannels(kMaxHardwareInputChannels, 2);
    return true;
}

void MainComponent::resumeAudioAfterDevicePanel()
{
    if (!audioSuspendedForDevicePanel_)
        return;

    DBG("[AudioDevice] resumeAudioAfterDevicePanel beforeRestart currentType=" + deviceManager.getCurrentAudioDeviceType()
        + " hasDevice=" + juce::String(deviceManager.getCurrentAudioDevice() != nullptr ? 1 : 0)
        + " wasRunningBefore=" + juce::String(audioWasRunningBeforeDevicePanel_ ? 1 : 0));

    audioSuspendedForDevicePanel_ = false;

    if (!audioWasRunningBeforeDevicePanel_)
        return;

    if (!repairInvalidCurrentAudioDevice())
        selectSafeStartupAudioDeviceType();

    // Do NOT pass a saved-state XML here.
    // If a device is already open (e.g. ASIO4ALL was opened by session_.commit()
    // inside applyPendingRequest), passing xml == nullptr + device-exists takes
    // JUCE branch-1, which skips initialise() and just re-registers the
    // audioSourcePlayer callback against the already-running device - which is
    // exactly what we need.
    // If no device is open (panel closed without applying), branch-2 fires and
    // initialise() opens the safe default device from scratch.
    setAudioChannels(kMaxHardwareInputChannels, 2);
    recoverFromFailedAsioPlaybackInit();

    if (auto* resumedDevice = getOwnedDeviceManager().getCurrentAudioDevice())
    {
        DBG("[AudioDevice] resumeAudioAfterDevicePanel afterRestart type=" + deviceManager.getCurrentAudioDeviceType()
            + " device=" + resumedDevice->getName()
            + " open=" + juce::String(resumedDevice->isOpen() ? 1 : 0)
            + " playing=" + juce::String(resumedDevice->isPlaying() ? 1 : 0)
            + " lastError=" + resumedDevice->getLastError());
    }
    else
    {
        DBG("[AudioDevice] resumeAudioAfterDevicePanel afterRestart type=" + deviceManager.getCurrentAudioDeviceType()
            + " device=<null>");
    }

    ensureInputChannelsActive("resume");

    cachedEnabledAudioInputChannels_.store(getEnabledAudioInputChannelCount(), std::memory_order_release);
    appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));

    // Fresh device choice = fresh healing budget. Without this a watchdog
    // that exhausted its retries on the previous device would never attempt
    // to heal the newly selected one.
    inputWatchdogRepairAttempts_ = 0;
    inputWatchdogWasHealthy_ = false;

    asioZombieRestartAttempts_ = 0;
    armAsioCallbackVerification("resume");
}

bool MainComponent::repairInvalidCurrentAudioDevice()
{
    auto* currentType = getOwnedDeviceManager().getCurrentDeviceTypeObject();
    if (currentType == nullptr)
        return false;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    getOwnedDeviceManager().getAudioDeviceSetup(setup);

    const bool isAsio = currentType->getTypeName().containsIgnoreCase("asio");
    if (!isAsio)
        return true;

    // A running ASIO device that opened cleanly is VALID - do not "repair" it.
    // The old flow flipped useDefaultInputChannels and re-applied the setup,
    // which closed+reopened ASIO4ALL right after commit already opened it.
    // ASIO4ALL often fails that rapid reopen and dies silently.
    if (auto* dev = getOwnedDeviceManager().getCurrentAudioDevice())
    {
        if (dev->isOpen()
            && dev->getActiveOutputChannels().countNumberOfSetBits() > 0
            && dev->getLastError().trim().isEmpty())
        {
            juce::Logger::writeToLog("[AudioDevice] ASIO device already open and healthy ("
                + dev->getName() + ") - skipping repair reopen");
            return true;
        }
    }

    currentType->scanForDevices();
    auto outputs = currentType->getDeviceNames(false);

    if (outputs.isEmpty())
    {
        DBG("[AudioDevice] ASIO selected but no output devices are available; falling back to a non-ASIO startup device.");
        return false;
    }

    const auto outputName = setup.outputDeviceName.trim();
    if (outputName.isEmpty()
        || outputName.containsIgnoreCase("none")
        || !outputs.contains(outputName))
    {
        DBG("[AudioDevice] ASIO selected without an explicit valid output device; refusing auto-pick and falling back to a non-ASIO startup device.");
        return false;
    }

    auto inputs = currentType->getDeviceNames(true);

    const auto requestedInputName = setup.inputDeviceName.trim();
    const auto inputName = requestedInputName.isNotEmpty()
        && !requestedInputName.containsIgnoreCase("none")
        && inputs.contains(requestedInputName)
            ? requestedInputName
            : juce::String();

    const bool hasExplicitValidOutputPair = !setup.useDefaultOutputChannels
        && setup.outputChannels.countNumberOfSetBits() > 0;

    setup.outputDeviceName = outputName;
    setup.inputDeviceName = inputName;

    if (hasExplicitValidOutputPair)
    {
        DBG("[AudioDevice] Preserving explicit ASIO output pair during device repair. bits="
            + setup.outputChannels.toString(2));
    }
    else
    {
        setup.useDefaultOutputChannels = true;
        setup.outputChannels.clear();
    }

    // Explicit input mask - the default-channel path replays the manager's
    // cached input count, which reopens with zero inputs once that cache is
    // poisoned. The ASIO wrapper clamps oversized masks to the driver's
    // real channel set.
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (inputName.isNotEmpty())
        setup.inputChannels.setRange(0, kMaxHardwareInputChannels, true);

    if (setup.sampleRate <= 0.0)
        setup.sampleRate = 48000.0;
    if (setup.bufferSize <= 0)
        setup.bufferSize = 512;

    prepareForAudioDeviceMutation();
    const auto error = getOwnedDeviceManager().setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
    {
        DBG("[AudioDevice] ASIO setup validation failed: " + error);
        return false;
    }

    return true;
}

void MainComponent::armAsioCallbackVerification(const char* stage)
{
    // isOpen()/getLastError()/isPlaying() can all look healthy on an ASIO
    // device that will never deliver a single callback (ASIO4ALL after a
    // rapid reopen, or GuardedAsioDevice::start() failing silently - JUCE's
    // AudioDeviceManager never checks whether start succeeded).  The only
    // reliable health signal is proof of life: real callback ticks.
    if (!getOwnedDeviceManager().getCurrentAudioDeviceType().containsIgnoreCase("asio"))
        return;
    if (getOwnedDeviceManager().getCurrentAudioDevice() == nullptr)
        return;

    const auto baseline = audioCallbackTickCounter_.load(std::memory_order_relaxed);
    const juce::String stageName(stage);
    juce::Component::SafePointer<MainComponent> safeThis(this);

    juce::Timer::callAfterDelay(1500, [safeThis, baseline, stageName]
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        if (self->audioSuspendedForDevicePanel_)
            return; // the device panel owns the device right now

        auto& dm = self->getOwnedDeviceManager();
        if (!dm.getCurrentAudioDeviceType().containsIgnoreCase("asio"))
            return;

        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr)
            return;

        const auto now = self->audioCallbackTickCounter_.load(std::memory_order_relaxed);
        if (now != baseline)
        {
            juce::Logger::writeToLog("[ASIO-VERIFY] stage=" + stageName
                + " callbacks flowing - " + dev->getName() + " is healthy ("
                + juce::String((int) (now - baseline)) + " blocks in 1.5s)");
            self->asioZombieRestartAttempts_ = 0;
            return;
        }

        // Zero callbacks in 1.5s: the driver is a zombie.  A restart cannot
        // hurt recording - with no callbacks, no audio is being captured
        // anyway - and it is the only way to bring playback/monitoring back.
        juce::Logger::writeToLog("[ASIO-VERIFY] stage=" + stageName + " ZOMBIE ASIO device: "
            + dev->getName()
            + " open=" + juce::String(dev->isOpen() ? 1 : 0)
            + " playing=" + juce::String(dev->isPlaying() ? 1 : 0)
            + " lastError=[" + dev->getLastError() + "]"
            + " - 0 callbacks in 1.5s, forcing full close+reopen");

        self->restartZombieAsioDevice();
    });
}

void MainComponent::restartZombieAsioDevice()
{
    auto& dm = getOwnedDeviceManager();

    if (asioZombieRestartAttempts_ >= kMaxAsioZombieRestartAttempts)
    {
        juce::Logger::writeToLog("[ASIO-VERIFY] restart attempts exhausted - falling back to Windows Audio so playback keeps working");
        shutdownAudio();
        selectSafeStartupAudioDeviceType();
        setAudioChannels(kMaxHardwareInputChannels, 2);
        ensureInputChannelsActive("asio-fallback");
        cachedEnabledAudioInputChannels_.store(getEnabledAudioInputChannelCount(), std::memory_order_release);
        appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
        return;
    }

    ++asioZombieRestartAttempts_;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup(setup);

    juce::Logger::writeToLog("[ASIO-VERIFY] full close+reopen attempt "
        + juce::String(asioZombieRestartAttempts_) + "/" + juce::String(kMaxAsioZombieRestartAttempts)
        + " out=[" + setup.outputDeviceName + "] sr=" + juce::String(setup.sampleRate)
        + " buf=" + juce::String(setup.bufferSize));

    // Full close releases the WDM endpoints ASIO4ALL wraps.  The reopen is
    // deferred so endpoint teardown can finish - an immediate reopen is
    // exactly the race that creates the zombie in the first place.
    shutdownAudio();

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::Timer::callAfterDelay(400, [safeThis, setup]
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        if (self->audioSuspendedForDevicePanel_)
            return;

        auto& dm2 = self->getOwnedDeviceManager();
        self->prepareForAudioDeviceMutation();
        const auto err = dm2.setAudioDeviceSetup(setup, true);

        juce::Logger::writeToLog("[ASIO-VERIFY] reopen err=[" + err + "] device="
            + (dm2.getCurrentAudioDevice() != nullptr ? dm2.getCurrentAudioDevice()->getName()
                                                      : juce::String("<null>")));

        if (err.isNotEmpty() || dm2.getCurrentAudioDevice() == nullptr)
        {
            self->shutdownAudio();
            self->selectSafeStartupAudioDeviceType();
        }

        self->setAudioChannels(kMaxHardwareInputChannels, 2);
        self->ensureInputChannelsActive("asio-restart");
        self->cachedEnabledAudioInputChannels_.store(self->getEnabledAudioInputChannelCount(), std::memory_order_release);
        self->appCore_.setHardwareInputChannelCount(self->cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));

        // Verify the restart actually produced callbacks; falls back to
        // Windows Audio when attempts are exhausted.
        self->armAsioCallbackVerification("zombie-restart");
    });
}

void MainComponent::showAudioDevicePanel()
{
    if constexpr (kUseLegacyDevicePanel)
    {
        const bool willShowPanel = audioDevicePanel_ == nullptr || !audioDevicePanel_->isVisible();

        if (willShowPanel)
        {
            suspendAudioForDevicePanel();

            if (!repairInvalidCurrentAudioDevice())
                selectSafeStartupAudioDeviceType();
        }

        if (!audioDevicePanel_)
        {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            audioDevicePanel_ = std::make_unique<DAW::AudioDeviceSettingsUI>(
                getOwnedDeviceManager(),
                [safeThis]
                {
                    if (auto* self = safeThis.getComponent())
                        self->prepareForAudioDeviceMutation();
                });
            audioDevicePanel_->onPanelClosed = [safeThis]
            {
                if (auto* self = safeThis.getComponent())
                {
                    self->resumeAudioAfterDevicePanel();
                    // Restore startup panel if it was visible before
                    if (self->startupPanel_ && !self->startupPanel_->isVisible())
                    {
                        self->startupPanel_->setVisible(true);
                        self->startupPanel_->toFront(false);
                    }
                }
            };
            audioDevicePanel_->onSettingsConfirmed = [safeThis]
            {
                if (auto* self = safeThis.getComponent())
                {
                    self->cachedEnabledAudioInputChannels_.store(self->getEnabledAudioInputChannelCount(), std::memory_order_release);
                    self->appCore_.setHardwareInputChannelCount(self->cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
                    self->resumeAudioAfterDevicePanel();
                }
            };
            addChildComponent(audioDevicePanel_.get());
        }

        const bool vis = !audioDevicePanel_->isVisible();

        audioDevicePanel_->setVisible(vis);
        if (!vis)
        {
            cachedEnabledAudioInputChannels_.store(getEnabledAudioInputChannelCount(), std::memory_order_release);
            appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
            resumeAudioAfterDevicePanel();
        }

        if (vis)
        {
            audioDevicePanel_->setBounds(getWidth() / 2 - 200, 100, 400, 500);
            audioDevicePanel_->toFront(true);
        }
    }
    else
    {
        const bool willShowPanel = stagedAudioDevicePanel_ == nullptr
                                || ! stagedAudioDevicePanel_->isVisible();
        if (willShowPanel)
        {
            suspendAudioForDevicePanel();
            if (! repairInvalidCurrentAudioDevice())
                selectSafeStartupAudioDeviceType();
        }

        if (!stagedDeviceSession_)
        {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            stagedDeviceSession_ = std::make_unique<DAW::DeviceSessionCore>(
                getOwnedDeviceManager(),
                [safeThis]()
                {
                    if (auto* self = safeThis.getComponent())
                        return self->appCore_.getTransport().isRecording();
                    return false;
                },
                [safeThis]
                {
                    if (auto* self = safeThis.getComponent())
                        self->prepareForAudioDeviceMutation();
                });
        }

        if (!stagedDevicePanelModel_)
            stagedDevicePanelModel_ = std::make_unique<DAW::DevicePanelModelCore>(getOwnedDeviceManager());

        if (!stagedAudioDevicePanel_)
        {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            stagedAudioDevicePanel_ = std::make_unique<DAW::AudioDevicePanelUI>(
                *stagedDeviceSession_,
                *stagedDevicePanelModel_,
                getOwnedDeviceManager(),
                [safeThis]
                {
                    if (auto* self = safeThis.getComponent())
                        self->prepareForAudioDeviceMutation();
                });

            stagedAudioDevicePanel_->onPanelClosed = [safeThis]
            {
                if (auto* self = safeThis.getComponent())
                    self->resumeAudioAfterDevicePanel();
            };
            stagedAudioDevicePanel_->onSettingsConfirmed = [safeThis]
            {
                if (auto* self = safeThis.getComponent())
                {
                    self->cachedEnabledAudioInputChannels_.store(self->getEnabledAudioInputChannelCount(), std::memory_order_release);
                    self->appCore_.setHardwareInputChannelCount(self->cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
                    self->resumeAudioAfterDevicePanel();
                }
            };
            addChildComponent(stagedAudioDevicePanel_.get());
        }

        const bool vis = !stagedAudioDevicePanel_->isVisible();
        stagedAudioDevicePanel_->setVisible(vis);

        if (!vis)
        {
            cachedEnabledAudioInputChannels_.store(getEnabledAudioInputChannelCount(), std::memory_order_release);
            appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
            resumeAudioAfterDevicePanel();
        }

        if (vis)
        {
            stagedAudioDevicePanel_->setBounds(getWidth() / 2 - 230, 100, 460, 330);
            stagedAudioDevicePanel_->toFront(true);
        }
    }
}

void MainComponent::showBubblegumPanel()
{
    if (!bubblegumPanel_ || !bubblegumOrb_) return;

    auto orbCentre = bubblegumOrb_->getBounds().getCentre();
    bubblegumPanel_->refresh();
    bubblegumPanel_->showNear(orbCentre);
    bubblegumOrb_->setVisible(false);
}

void MainComponent::hideBubblegumPanel()
{
    if (bubblegumPanel_)
        bubblegumPanel_->hideAnimated();

    if (bubblegumOrb_)
        bubblegumOrb_->setVisible(true);
}

MainComponent::~MainComponent()
{
    // 1. Detach listeners first - prevents callbacks into dying objects
    removeKeyListener(this);
    DAW::GlobalTransportSpaceHook::shutdown();
    if (mixerWindow_)
        mixerWindow_->removeComponentListener(this);
    mixerViewport_.getHorizontalScrollBar().removeListener(this);
    if (timelineViewport_)
    {
        timelineViewport_->getHorizontalScrollBar().removeListener(this);
        timelineViewport_->getVerticalScrollBar().removeListener(this);
    }
    inputWatchdog_.stopTimer();
    DAW::CommandManager::getInstance().setUndoRedoBlockedQuery({});
    DAW::CommandManager::getInstance().removeListener(this);
    appCore_.getProjectManager().setLoadProgressCallback({});
    appCore_.getProjectManager().removeListener(this);
    appCore_.getRoutingGraph().removeListener(this);
    appCore_.getTrackManager().removeListener(this);
    appCore_.getTransport().removeListener(this);
    appCore_.getAutosaveManager().onAutosaveSucceeded = {};
    appCore_.getAutosaveManager().onAutosaveFailed = {};

    // 2. The centralized shutdown path joins export before releasing audio.
    shutdownAudio();

    // 3. Destroy all floating/plugin/editor windows explicitly before appCore
    clipRegionPluginWindows_.clear();
    createSequenceWindow_ = nullptr;
    forensicWindow_.reset();
    shortcutHelpWindow_.reset();
    scanDialog_.reset();
    browserChrome_.reset();
    slimeSidePanel_.reset();
    pluginBrowser_.reset();
    pluginSidePanel_.reset();
    bubbleTaskbar_.reset();
    bubblegumTaskbar_.reset();
    inputTrimPanelManager_.reset();
    mixerWindow_.reset();
    trackListWindow_.reset();
    pianoRollWindow_.reset();
    clipPropertiesWindow_.reset();
    timelineWindow_.reset();
    settingsPanel_.reset();
    audioDevicePanel_.reset();
    controlRoomPanel_.reset();
    mixerViewport_.setViewedComponent(nullptr, false);
    if (timelineViewport_)
        timelineViewport_->setViewedComponent(nullptr, false);
    // The cable overlay is a non-owning ComponentListener of MixerPanel.
    // Detach it before releasing the panel it observes.
    cableOverlay_.unbind();
    mixerPanel_.reset();
    arrangement_.reset();
    trackList_.reset();
    // MarkerBubble is a non-owning MarkerManager listener. Destroy it before
    // ApplicationCore::shutdown() releases the manager it must detach from.
    markerBubble_.reset();
    timelineViewport_.reset();
    transportBar_.reset();
    menuBar_.reset();

    // 4. Shut down application subsystems (releases plugin chains, etc.)
    appCore_.shutdown();
    apex::automation::AutomationSystem::getInstance().shutdownForApplicationExit();

    // 5. Clear the JUCE typeface LRU cache - this releases the last Typeface::Ptr
    //    references held by the cache before JUCE's own statics fire their
    //    LeakCounter destructors. Without this, the 2 default DirectWrite
    //    typefaces (Verdana + Tahoma) survive and trigger the jassertfalse.
    juce::Typeface::clearTypefaceCache();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    appendMainStartupTrace("prepareToPlay.begin sr=" + juce::String(sampleRate) + " block=" + juce::String(samplesPerBlockExpected));
    appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
    appCore_.prepareToPlay(sampleRate, samplesPerBlockExpected);
    appendMainStartupTrace("prepareToPlay.after-appCore");

    // Device-rate re-sync for MIDI clips. Runs on the message thread inside
    // the safe device-reconfiguration lifecycle (audioDeviceAboutToStart ->
    // applyAudioDevicePreparation -> prepareToPlay): the new device's
    // callbacks have not started flowing, and the same path runs at startup
    // prepare. No audio-thread mutation is introduced. Seconds-preserving
    // setSampleRate keeps every clip's seconds/tick extent across rate
    // changes; mirrors the established tempo-change path below.
    {
        auto& cm = appCore_.getClipManager();
        for (auto* clip : cm.getAllClips())
            if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
                midiClip->setSampleRate (sampleRate);
    }

    if (stepSequencerWindow_)
        stepSequencerWindow_->setTransportReference(&appCore_.getTransport(), sampleRate);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, sampleRate]()
    {
        if (auto* self = safeThis.getComponent())
            if (self->arrangement_)
                self->arrangement_->setEngineSampleRate(sampleRate);
    });
}

juce::AudioDeviceManager& MainComponent::getOwnedDeviceManager() noexcept
{
    return deviceManager;
}

const juce::AudioDeviceManager& MainComponent::getOwnedDeviceManager() const noexcept
{
    return deviceManager;
}

bool& MainComponent::getAudioCallbackRegisteredFlag() noexcept
{
    return audioCallbackRegistered_;
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    const int configuredInputChannels =
        cachedEnabledAudioInputChannels_.load(std::memory_order_acquire);
    processNextAudioBlock(
        bufferToFill,
        configuredInputChannels > 0 ? &liveCallbackInputBuffer_ : nullptr,
        liveCallbackInputBuffer_.getNumSamples(),
        configuredInputChannels);
}

void MainComponent::processNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill,
    const juce::AudioBuffer<float>* hardwareInputBuffer,
    int hardwareInputNumSamples,
    int validInputChannels)
{
    const bool processRealtimeTail = appCore_.getNextAudioBlock(
        bufferToFill, hardwareInputBuffer, hardwareInputNumSamples, validInputChannels);
    if (! processRealtimeTail)
        return;

    // Process beat-making system (step sequencer → drum sampler)
    if (drumSamplerEngine_ && stepSequencerWindow_ && bufferToFill.buffer != nullptr)
    {
        juce::MidiBuffer stepSeqMidi;
        const double tempo = appCore_.getTransport().getTempo();
        const int64_t position = (int64_t)appCore_.getTransport().getPosition();

        // Get prebuilt immutable snapshot from message thread — no lock, no allocation.
        auto snapshotPtr = stepSequencerWindow_->getStepSequencer().getModel().getSnapshotRT();
        if (!snapshotPtr) return; // No snapshot published yet — skip this block.
        const auto& snapshot = *snapshotPtr;
        stepSeqPlayback_.processBlock(stepSeqMidi, bufferToFill.numSamples,
                                       position, tempo, snapshot);

        // Feed MIDI to drum sampler
        drumSamplerEngine_->processBlock(stepSeqMidi, *bufferToFill.buffer,
                                          bufferToFill.startSample, bufferToFill.numSamples);
    }
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);

    RealtimeDeviceCallbackGuard callbackGuard(appCore_);
    const auto callbackAction = pendingAudioPreparationGate_.callbackAction(
        callbackGuard.wasAdmitted());
    if (callbackAction == DAW::PendingAudioPreparationGateCore::CallbackAction::clearOutputAndReturn)
    {
        clearBackendOutput(outputChannelData, numOutputChannels, numSamples);
        return;
    }

    const juce::int64 auditStartTicks = callbackAuditEnabled_
        ? juce::Time::getHighResolutionTicks() : 0;

    audioCallbackTickCounter_.fetch_add(1, std::memory_order_relaxed);

    const int maximumPreparedSamples = appCore_.getCurrentBlockSize();
    if (maximumPreparedSamples <= 0 || outputChannelData == nullptr || numOutputChannels <= 0)
    {
        clearBackendOutput(outputChannelData, numOutputChannels, numSamples);
        return;
    }

    juce::AudioBuffer<float> outputBuffer(outputChannelData, numOutputChannels, numSamples);
    const int validInputChannels = DAW::RecordingInputValidityCore::clampCallbackChannels(
        numInputChannels, liveCallbackInputBuffer_.getNumChannels());

    DAW::AudioDeviceBlockAdapterCore::forEachPreparedChunk(
        numSamples, maximumPreparedSamples,
        [this, inputChannelData, numInputChannels, validInputChannels, &outputBuffer]
        (int frameOffset, int chunkSamples)
        {
            const int safeSamples = juce::jmin(
                chunkSamples, liveCallbackInputBuffer_.getNumSamples());

            if (validInputChannels > 0 && safeSamples > 0)
            {
                if (validInputChannels < numInputChannels || safeSamples < chunkSamples)
                {
                    // Backend exceeded prepared input capacity — request an
                    // off-thread device reprepare, never resize in callback.
                    needsInputBufferReprepare_.store(true, std::memory_order_release);
                }

                for (int ch = 0; ch < validInputChannels; ++ch)
                {
                    auto* dst = liveCallbackInputBuffer_.getWritePointer(ch);
                    const auto* src = inputChannelData != nullptr ? inputChannelData[ch] : nullptr;

                    if (src != nullptr)
                        juce::FloatVectorOperations::copy(dst, src + frameOffset, safeSamples);
                    else
                        juce::FloatVectorOperations::clear(dst, safeSamples);
                }

                // Clear channels beyond the valid backend inputs so no prior
                // sub-block can leak into this one.
                for (int ch = validInputChannels;
                     ch < liveCallbackInputBuffer_.getNumChannels(); ++ch)
                    liveCallbackInputBuffer_.clear(ch, 0, safeSamples);
            }
            else
            {
                for (int ch = 0; ch < liveCallbackInputBuffer_.getNumChannels(); ++ch)
                    liveCallbackInputBuffer_.clear(ch, 0, safeSamples);
            }

            juce::AudioSourceChannelInfo outputInfo(
                &outputBuffer, frameOffset, chunkSamples);
            processNextAudioBlock(
                outputInfo,
                validInputChannels > 0 && safeSamples > 0
                    ? &liveCallbackInputBuffer_ : nullptr,
                safeSamples,
                validInputChannels);
        });

    if (callbackAuditEnabled_)
    {
        const juce::int64 auditEndTicks = juce::Time::getHighResolutionTicks();
        CallbackAuditRecord rec;
        rec.sequence = callbackAuditSequence_++;
        rec.startTicks = auditStartTicks;
        rec.durationTicks = auditEndTicks - auditStartTicks;
        rec.periodTicks = callbackAuditPeriodTicks_;
        // Measured start-to-start interval: distinguishes "APEX exceeded the
        // deadline" from "the driver/OS delivered this callback late".
        rec.intervalTicks = callbackAuditPrevStartTicks_ != 0
            ? auditStartTicks - callbackAuditPrevStartTicks_
            : callbackAuditPeriodTicks_;
        callbackAuditPrevStartTicks_ = auditStartTicks;
        rec.numSamples = numSamples;
        rec.streamGeneration = callbackAuditStreamGeneration_;
        rec.deadlineMiss = (callbackAuditPeriodTicks_ > 0 && rec.durationTicks > callbackAuditPeriodTicks_) ? 1 : 0;
        // Stage breakdown + spike context captured inside getNextAudioBlock.
        const auto& telemetry = appCore_.getCallbackStageTelemetry();
        rec.stageTicks = telemetry.stageTicks;
        rec.flags = telemetry.flags;
        rec.contextTrackCount = telemetry.trackCount;
        rec.contextGraphVersion = telemetry.graphVersion;
        callbackAuditRing_.tryPush (rec);
        callbackAuditAccumulator_.add (rec);
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const juce::ScopedLock lock(audioDevicePreparationLock_);
    PendingAudioDevicePreparation preparation;
    preparation.generation = audioResourceReleaseState_.deviceStarted();
    preparation.activeInputs = device != nullptr
        ? juce::jmax(0, device->getActiveInputChannels().countNumberOfSetBits()) : 0;
    preparation.blockSize = device != nullptr
        ? juce::jmax(1, device->getCurrentBufferSizeSamples()) : 1;
    preparation.sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
    preparation.valid = true;

    if (audioResourceReleaseState_.isExportOwned())
    {
        pendingAudioDevicePreparation_ = preparation;
        pendingAudioDevicePreparation_.callbackDrainFailed = false;
        pendingAudioPreparationGate_.defer(preparation.generation);
        return;
    }

    applyAudioDevicePreparation(preparation);
    pendingAudioDevicePreparation_.valid = false;
    pendingAudioPreparationGate_.cancel();
}

void MainComponent::applyAudioDevicePreparation(
    const PendingAudioDevicePreparation& preparation)
{
    const int activeInputs = preparation.activeInputs;
    const int currentBlockSize = preparation.blockSize;
    const double currentSampleRate = preparation.sampleRate;

    // Pre-allocate to worst-case: max inputs x max(8192, actual block size).
    liveCallbackInputBuffer_.setSize(juce::jmax(1, activeInputs), juce::jmax(currentBlockSize, 8192), false, false, true);
    liveCallbackInputBuffer_.clear();
    cachedEnabledAudioInputChannels_.store(activeInputs, std::memory_order_release);
    needsInputBufferReprepare_.store(false, std::memory_order_release);
    prepareToPlay(currentBlockSize, currentSampleRate);

    // Prepare step sequencer playback with actual audio device settings
    stepSeqPlayback_.prepareToPlay(currentSampleRate, currentBlockSize);

    // Drum sampler: pad resampling + envelopes track the actual device rate.
    if (drumSamplerEngine_ != nullptr)
        drumSamplerEngine_->prepare (currentSampleRate);

    // ── Callback audit: read environment and precompute period ticks ──────
    {
        const char* auditVal = std::getenv ("APEX_CALLBACK_AUDIT");
        callbackAuditEnabled_ = (auditVal != nullptr && auditVal[0] == '1' && auditVal[1] == '\0');
        const char* outVal = std::getenv ("APEX_CALLBACK_AUDIT_OUTPUT");
        callbackAuditOutputPath_ = (outVal != nullptr) ? juce::String (outVal) : juce::String();
    }
    if (callbackAuditEnabled_)
    {
        callbackAuditStreamGeneration_++;
        callbackAuditSequence_ = 0;
        const double ticksPerSecond = static_cast<double> (juce::Time::getHighResolutionTicksPerSecond());
        callbackAuditPeriodTicks_ = computeCallbackPeriodTicks (currentBlockSize, currentSampleRate, ticksPerSecond);
        callbackAuditDrainIntervalSeconds_ = computeAuditDrainIntervalSeconds (
            static_cast<double> (currentBlockSize) / currentSampleRate,
            decltype (callbackAuditRing_)::kCapacity);
        callbackAuditPrevStartTicks_ = 0;
        callbackAuditAccumulator_.reset();

        // Frequent cheap drain (records are discarded — the accumulator is
        // authoritative) so ringOverflows only fires on a genuinely stalled
        // drain. The 5 s report cadence in inputWatchdogTick is unchanged.
        callbackAuditDrainTimer_.startTimer (juce::jmax (1, (int) std::round (callbackAuditDrainIntervalSeconds_ * 1000.0)));
    }
    else
    {
        callbackAuditDrainTimer_.stopTimer();
    }
    appCore_.setCallbackStageTimingArmed (callbackAuditEnabled_);
}

void MainComponent::applyPendingAudioDevicePreparationIfReady()
{
    if (!pendingAudioDevicePreparation_.valid)
    {
        pendingAudioPreparationGate_.cancel();
        return;
    }

    const auto preparation = pendingAudioDevicePreparation_;
    const bool generationCurrent =
        audioResourceReleaseState_.canApplyPreparation(preparation.generation);
    auto action = pendingAudioPreparationGate_.prepareAction(
        preparation.generation, generationCurrent, 1);
    if (action == DAW::PendingAudioPreparationGateCore::PrepareAction::stale)
    {
        pendingAudioDevicePreparation_.valid = false;
        pendingAudioPreparationGate_.cancel();
        return;
    }

    constexpr uint32_t callbackDrainTimeoutMs = 2000;
    if (!appCore_.waitForRealtimeDeviceCallbacksToDrain(callbackDrainTimeoutMs))
    {
        pendingAudioDevicePreparation_.callbackDrainFailed = true;
        juce::Logger::writeToLog(
            "[AudioDevice] pending generation " + juce::String(preparation.generation)
            + " preparation blocked: admitted realtime callbacks did not drain within "
            + juce::String(callbackDrainTimeoutMs) + " ms; output remains gated");
        return;
    }

    action = pendingAudioPreparationGate_.prepareAction(
        preparation.generation,
        audioResourceReleaseState_.canApplyPreparation(preparation.generation),
        0);
    if (action != DAW::PendingAudioPreparationGateCore::PrepareAction::prepare)
    {
        pendingAudioDevicePreparation_.valid = false;
        pendingAudioPreparationGate_.cancel();
        return;
    }

    applyAudioDevicePreparation(preparation);
    pendingAudioDevicePreparation_.valid = false;
    pendingAudioDevicePreparation_.callbackDrainFailed = false;
    pendingAudioPreparationGate_.complete(preparation.generation);
}

void MainComponent::audioDeviceStopped()
{
    const juce::ScopedLock lock(audioDevicePreparationLock_);
    const auto action = audioResourceReleaseState_.deviceStopped();
    if (action == DAW::AudioResourceReleaseStateCore::StopAction::defer)
        return;
    if (action != DAW::AudioResourceReleaseStateCore::StopAction::release)
        return;

    releaseStoppedDeviceResources();
}

void MainComponent::releaseStoppedDeviceResources()
{
    pendingAudioDevicePreparation_.valid = false;
    pendingAudioPreparationGate_.cancel();
    releaseResources();
    liveCallbackInputBuffer_.setSize(0, 0);
    cachedEnabledAudioInputChannels_.store(0, std::memory_order_release);

    if (callbackAuditEnabled_)
    {
        callbackAuditEnabled_ = false;
        appCore_.setCallbackStageTimingArmed (false);
        callbackAuditDrainTimer_.stopTimer();
        callbackAuditAccumulator_.reset();
        callbackAuditLastDrainTicks_ = 0;
        callbackAuditPrevStartTicks_ = 0;
    }
}

void MainComponent::setAudioChannels(int numInputChannels, int numOutputChannels)
{
    prepareForAudioDeviceMutation();

    if (getAudioCallbackRegisteredFlag())
    {
        getOwnedDeviceManager().removeAudioCallback(this);
        getAudioCallbackRegisteredFlag() = false;
    }

    juce::String audioError;
    if (getOwnedDeviceManager().getCurrentAudioDevice() != nullptr)
    {
        auto* dev = getOwnedDeviceManager().getCurrentAudioDevice();
        const bool isAsio = getOwnedDeviceManager().getCurrentAudioDeviceType().containsIgnoreCase("asio");

        if (isAsio && dev->isOpen())
        {
            // ASIO channel sets are fixed by the driver and were already sanitised
            // by GuardedAsioDevice::open. Forcing our 64-in/2-out mask here would
            // close+reopen the driver; ASIO4ALL frequently fails rapid reopen
            // cycles (WDM endpoint not yet released) and comes back as a zombie:
            // open, "healthy", but never delivering callbacks - no playback,
            // no recording, no monitoring. Keep the driver-native channel set.
            juce::Logger::writeToLog("[AudioDevice] setAudioChannels: ASIO device open ("
                + dev->getName() + ") - keeping driver-native channels, in="
                + juce::String(dev->getActiveInputChannels().countNumberOfSetBits())
                + " out=" + juce::String(dev->getActiveOutputChannels().countNumberOfSetBits()));
        }
        else
        {
            auto setup = getOwnedDeviceManager().getAudioDeviceSetup();

            // Resolve a concrete input device when inputs are requested but
            // none is named - JUCE clears the input mask for empty names.
            if (numInputChannels > 0 && setup.inputDeviceName.trim().isEmpty())
            {
                if (auto* type = getOwnedDeviceManager().getCurrentDeviceTypeObject())
                {
                    if (type->hasSeparateInputsAndOutputs())
                    {
                        const auto inputs = type->getDeviceNames(true);
                        if (! inputs.isEmpty())
                            setup.inputDeviceName = inputs[juce::jlimit(0, inputs.size() - 1,
                                                                        type->getDefaultDeviceIndex(true))];
                    }
                    else
                    {
                        // ASIO-style single unit: the inputs live on the same
                        // driver as the outputs.
                        setup.inputDeviceName = setup.outputDeviceName;
                    }
                }
            }

            const int wantedIns = setup.inputDeviceName.trim().isNotEmpty() ? numInputChannels : 0;

            if (setup.inputChannels.countNumberOfSetBits() != wantedIns
                || setup.outputChannels.countNumberOfSetBits() < juce::jmin(numOutputChannels, 1))
            {
                // Explicit masks with useDefault*Channels = false. JUCE derives
                // numInputChansNeeded from an explicit mask; the default-channel
                // path replays the manager's cached count instead - and once that
                // cache hits 0 (the zero-input trap) every "successful" reopen
                // comes back with no inputs and monitoring/recording stay silent.
                setup.useDefaultInputChannels = false;
                setup.inputChannels.clear();
                setup.inputChannels.setRange(0, wantedIns, true);

                // Only rebuild the output mask when it is empty - an existing
                // mask is a deliberate choice (e.g. an explicit ASIO out pair).
                if (setup.outputChannels.countNumberOfSetBits() == 0)
                {
                    setup.useDefaultOutputChannels = false;
                    setup.outputChannels.clear();
                    setup.outputChannels.setRange(0, numOutputChannels, true);
                }

                audioError = getOwnedDeviceManager().setAudioDeviceSetup(setup, false);

                // Pro-DAW guarantee: playback must never die because an input
                // endpoint is blocked (Windows mic privacy, exclusive-mode
                // claim). If the combined open failed, retry output-only and
                // let the input watchdog/preflight surface the input problem.
                if (audioError.isNotEmpty() && wantedIns > 0)
                {
                    juce::Logger::writeToLog("[AudioDevice] setAudioChannels: input+output open failed ["
                        + audioError + "] - retrying output-only so playback keeps working");
                    setup.inputDeviceName.clear();
                    setup.inputChannels.clear();
                    audioError = getOwnedDeviceManager().setAudioDeviceSetup(setup, false);
                }
            }
        }
    }
    else
    {
        audioError = getOwnedDeviceManager().initialise(numInputChannels, numOutputChannels, nullptr, true);

        // Same guarantee on cold start: if the default input endpoint blocks
        // the combined open, bring the engine up output-only instead of dead.
        if (audioError.isNotEmpty() && numInputChannels > 0)
        {
            juce::Logger::writeToLog("[AudioDevice] setAudioChannels: initialise with inputs failed ["
                + audioError + "] - retrying output-only so playback keeps working");
            audioError = getOwnedDeviceManager().initialise(0, numOutputChannels, nullptr, true);
        }

        if (audioError.isEmpty())
        {
            auto setup = getOwnedDeviceManager().getAudioDeviceSetup();

            if (setup.inputDeviceName.trim().isNotEmpty() && setup.inputChannels.countNumberOfSetBits() == 0 && numInputChannels > 0)
            {
                const auto preFix = setup;
                setup.useDefaultInputChannels = false;
                setup.inputChannels.clear();
                setup.inputChannels.setRange(0, numInputChannels, true);
                audioError = getOwnedDeviceManager().setAudioDeviceSetup(setup, false);

                // Input endpoint blocked the reopen - keep the working
                // output-only device instead of leaving audio dead.
                if (audioError.isNotEmpty())
                {
                    juce::Logger::writeToLog("[AudioDevice] setAudioChannels: explicit input mask failed ["
                        + audioError + "] - restoring output-only setup");
                    audioError = getOwnedDeviceManager().setAudioDeviceSetup(preFix, false);
                }
            }
        }
    }

    jassert(audioError.isEmpty());
    getOwnedDeviceManager().addAudioCallback(this);
    getAudioCallbackRegisteredFlag() = true;
}

void MainComponent::shutdownAudio()
{
    cancelAndJoinExportBeforeAudioRelease();

    if (getAudioCallbackRegisteredFlag())
    {
        getOwnedDeviceManager().removeAudioCallback(this);
        getAudioCallbackRegisteredFlag() = false;
    }

    getOwnedDeviceManager().closeAudioDevice();
}

void MainComponent::prepareForAudioDeviceMutation()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    cancelAndJoinExportBeforeAudioRelease();
}

void MainComponent::cancelAndJoinExportBeforeAudioRelease()
{
    if (exportRenderCore_)
    {
        if (auto* progressWindow = dynamic_cast<DAW::ExportProgressWindow*>(
                exportProgressWindow_.getComponent()))
            progressWindow->prepareForOwnerShutdown();
        exportProgressWindow_ = nullptr;
        exportRenderCore_->prepareForShutdown();
        exportRenderCore_.reset();
    }

    activeExportLifecycleGeneration_ = 0;
    const juce::ScopedLock lock(audioDevicePreparationLock_);
    const auto action = audioResourceReleaseState_.finishExport();
    if (action == DAW::AudioResourceReleaseStateCore::FinishAction::release)
    {
        pendingAudioDevicePreparation_.valid = false;
        releaseStoppedDeviceResources();
    }
    else if (pendingAudioDevicePreparation_.valid)
        applyPendingAudioDevicePreparationIfReady();
    else
        pendingAudioPreparationGate_.cancel();
}

void MainComponent::finishExportOnMessageThread(uint64_t exportGeneration)
{
    if (exportGeneration != activeExportLifecycleGeneration_)
        return;

    activeExportLifecycleGeneration_ = 0;
    const juce::ScopedLock lock(audioDevicePreparationLock_);
    const auto action = audioResourceReleaseState_.finishExport();
    if (action == DAW::AudioResourceReleaseStateCore::FinishAction::release)
    {
        pendingAudioDevicePreparation_.valid = false;
        releaseStoppedDeviceResources();
    }
    else if (pendingAudioDevicePreparation_.valid)
        applyPendingAudioDevicePreparationIfReady();
    else
        pendingAudioPreparationGate_.cancel();
}

int MainComponent::getEnabledAudioInputChannelCount() const
{
    if (auto* dev = getOwnedDeviceManager().getCurrentAudioDevice())
    {
        const int active = dev->getActiveInputChannels().countNumberOfSetBits();
        if (active > 0)
            return active;
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    getOwnedDeviceManager().getAudioDeviceSetup(setup);
    return setup.inputChannels.countNumberOfSetBits();
}

bool MainComponent::ensureInputChannelsActive(const char* stage)
{
    auto& dm = getOwnedDeviceManager();
    auto* dev = dm.getCurrentAudioDevice();
    const int activeIn = dev != nullptr
        ? dev->getActiveInputChannels().countNumberOfSetBits() : -1;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup(setup);

    juce::Logger::writeToLog(juce::String("[APEX-INPUT-FIX] stage=") + stage
        + " type=" + dm.getCurrentAudioDeviceType()
        + " inDev=[" + setup.inputDeviceName + "]"
        + " outDev=[" + setup.outputDeviceName + "]"
        + " maskIn=" + juce::String(setup.inputChannels.countNumberOfSetBits())
        + " useDefIn=" + juce::String(setup.useDefaultInputChannels ? 1 : 0)
        + " activeIn=" + juce::String(activeIn));

    if (dev == nullptr)
        return false;
    if (activeIn > 0)
        return true;

    // ASIO: if the driver exposes no input channels at all, a reopen cannot
    // conjure them - and reopen cycles are exactly what kills ASIO4ALL.
    // Tell the user to enable the mic inside the driver instead.
    if (dm.getCurrentAudioDeviceType().containsIgnoreCase("asio")
        && dev->getInputChannelNames().isEmpty())
    {
        juce::Logger::writeToLog(juce::String("[APEX-INPUT-FIX] stage=") + stage
            + " ASIO driver exposes ZERO input channels - skipping reopen."
              " Enable the input device inside the ASIO driver's control panel"
              " (ASIO4ALL: wrench icon > activate the mic endpoint).");
        return false;
    }

    // Repair 1: empty input name -> resolve the current type's default input
    if (setup.inputDeviceName.trim().isEmpty())
    {
        if (auto* type = dm.getCurrentDeviceTypeObject())
        {
            const auto inputs = type->getDeviceNames(true);
            if (! inputs.isEmpty())
            {
                const int def = juce::jlimit(0, inputs.size() - 1,
                                             type->getDefaultDeviceIndex(true));
                setup.inputDeviceName = inputs[def];
                DBG("[APEX-INPUT-FIX] stage=" << stage
                    << " resolved default input=[" << setup.inputDeviceName << "]");
            }
        }
    }

    if (setup.inputDeviceName.trim().isEmpty())
    {
        juce::Logger::writeToLog(juce::String("[APEX-INPUT-FIX] stage=") + stage
            + " NO input device available on this type - cannot repair"
              " (endpoint missing or claimed exclusively by another app)");
        return false;
    }

    // Repair 2: force an EXPLICIT input mask and re-apply.
    // Never use useDefaultInputChannels here: JUCE replays the manager's
    // cached numInputChansNeeded for default channels, and when that cache
    // is 0 (the zero-input trap) the "repair" reopens the device with no
    // inputs while reporting success. An explicit mask re-derives the count
    // from the mask itself; drivers clamp oversized masks safely.
    juce::AudioDeviceManager::AudioDeviceSetup preRepair;
    dm.getAudioDeviceSetup(preRepair);

    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(0, kMaxHardwareInputChannels, true);
    prepareForAudioDeviceMutation();
    const auto err = dm.setAudioDeviceSetup(setup, true);

    // Playback survival: if the input endpoint blocked the combined reopen
    // (mic privacy / exclusive claim) JUCE deletes the device entirely -
    // restore the previous working setup rather than leaving audio dead.
    if (err.isNotEmpty() && dm.getCurrentAudioDevice() == nullptr)
    {
        juce::Logger::writeToLog(juce::String("[APEX-INPUT-FIX] stage=") + stage
            + " repair killed the device [" + err + "] - restoring previous setup");
        dm.setAudioDeviceSetup(preRepair, true);
    }

    dev = dm.getCurrentAudioDevice();
    const int activeAfter = dev != nullptr
        ? dev->getActiveInputChannels().countNumberOfSetBits() : -1;

    juce::Logger::writeToLog(juce::String("[APEX-INPUT-FIX] stage=") + stage
        + " repair err=[" + err + "] activeInAfter=" + juce::String(activeAfter)
        + (activeAfter <= 0
            ? "  << STILL ZERO: endpoint busy/blocked (close other DAWs /"
              " ASIO4ALL holds it / check Windows mic privacy), not a code path"
            : ""));

    return activeAfter > 0;
}

void MainComponent::drainCallbackAuditRing()
{
    // Message thread. Pops are discarded by design; every record was already
    // folded into the accumulator on the audio thread.
    CallbackAuditRecord drained;
    while (callbackAuditRing_.tryPop (drained)) {}
}

void MainComponent::inputWatchdogTick()
{
    // ── Callback audit: drain and report from message thread (5-second interval) ──
    if (callbackAuditEnabled_)
    {
        const int64_t now = juce::Time::getHighResolutionTicks();
        const double ticksPerSecond = static_cast<double> (juce::Time::getHighResolutionTicksPerSecond());
        const double elapsed = static_cast<double> (now - callbackAuditLastDrainTicks_) / ticksPerSecond;

        if (elapsed >= 5.0 || callbackAuditLastDrainTicks_ == 0)
        {
            callbackAuditLastDrainTicks_ = now;

            CallbackAuditRecord drained;
            while (callbackAuditRing_.tryPop (drained)) {}

            auto snap = callbackAuditAccumulator_.snapshot (callbackAuditRing_.getOverflowCount());

            auto* dev = getOwnedDeviceManager().getCurrentAudioDevice();
            const double sampleRate = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
            const int blockSize = dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;
            const int xruns = deviceManager.getXRunCount();

            juce::Logger::writeToLog ("[APEX-CALLBACK-AUDIT] callbacks=" + juce::String (snap.callbacks)
                + " deadlineMisses=" + juce::String (snap.deadlineMisses)
                + " maxConsecutiveMisses=" + juce::String (snap.maximumConsecutiveMisses)
                + " ringOverflow=" + juce::String (snap.ringOverflows)
                + " engineOverruns=" + juce::String (snap.engineOverruns)
                + " lateDeliveries=" + juce::String (snap.lateDeliveries)
                + " p50=" + juce::String (snap.p50, 4)
                + " p95=" + juce::String (snap.p95, 4)
                + " p99=" + juce::String (snap.p99, 4)
                + " p99.9=" + juce::String (snap.p999, 4)
                + " p99.99=" + juce::String (snap.p9999, 4)
                + " max=" + juce::String (snap.maximum, 4)
                + " intervalMax=" + juce::String (snap.intervalMaximum, 4)
                + " cpu=" + juce::String (deviceManager.getCpuUsage() * 100.0, 1) + "%"
                + " xruns=" + juce::String (xruns)
                + " sampleRate=" + juce::String (sampleRate, 1)
                + " blockSize=" + juce::String (blockSize)
                + " streamGen=" + juce::String (callbackAuditStreamGeneration_));

            // Slowest-callback forensics: which stage consumed the time and
            // in what session context (transport/tracks/graph generation).
            if (snap.maxRecord.durationTicks > 0)
            {
                static const char* stageNames[kCallbackAuditStageCount] =
                    { "input", "rec", "click", "monTrim", "engine", "master", "clickSum", "ctrlRoom" };
                juce::String stages;
                for (size_t s = 0; s < (size_t) kCallbackAuditStageCount; ++s)
                {
                    const double ms = 1000.0 * (double) snap.maxRecord.stageTicks[s] / ticksPerSecond;
                    stages += stageNames[s];
                    stages += "=" + juce::String (ms, 3) + "ms";
                    if (s + 1 < (size_t) kCallbackAuditStageCount) stages += " ";
                }
                juce::Logger::writeToLog ("[APEX-CALLBACK-AUDIT-MAX] seq=" + juce::String ((juce::int64) snap.maxRecord.sequence)
                    + " total=" + juce::String (1000.0 * (double) snap.maxRecord.durationTicks / ticksPerSecond, 3) + "ms"
                    + " interval=" + juce::String (1000.0 * (double) snap.maxRecord.intervalTicks / ticksPerSecond, 3) + "ms"
                    + " n=" + juce::String ((int) snap.maxRecord.numSamples)
                    + " flags=" + juce::String ((int) snap.maxRecord.flags)
                    + " tracks=" + juce::String ((int) snap.maxRecord.contextTrackCount)
                    + " graphV=" + juce::String ((juce::int64) snap.maxRecord.contextGraphVersion)
                    + " stages=[" + stages + "]");
            }

            if (callbackAuditOutputPath_.isNotEmpty())
            {
                juce::DynamicObject::Ptr root = new juce::DynamicObject();
                root->setProperty ("callbacks", static_cast<juce::int64> (snap.callbacks));
                root->setProperty ("deadlineMisses", static_cast<juce::int64> (snap.deadlineMisses));
                root->setProperty ("maximumConsecutiveMisses", static_cast<juce::int64> (snap.maximumConsecutiveMisses));
                root->setProperty ("ringOverflows", static_cast<juce::int64> (snap.ringOverflows));
                root->setProperty ("p50", snap.p50);
                root->setProperty ("p95", snap.p95);
                root->setProperty ("p99", snap.p99);
                root->setProperty ("p999", snap.p999);
                root->setProperty ("p9999", snap.p9999);
                root->setProperty ("maximum", snap.maximum);
                root->setProperty ("intervalMaximum", snap.intervalMaximum);
                root->setProperty ("engineOverruns", static_cast<juce::int64> (snap.engineOverruns));
                root->setProperty ("lateDeliveries", static_cast<juce::int64> (snap.lateDeliveries));
                if (snap.maxRecord.durationTicks > 0)
                {
                    juce::DynamicObject::Ptr maxObj = new juce::DynamicObject();
                    maxObj->setProperty ("sequence", static_cast<juce::int64> (snap.maxRecord.sequence));
                    maxObj->setProperty ("durationTicks", static_cast<juce::int64> (snap.maxRecord.durationTicks));
                    maxObj->setProperty ("intervalTicks", static_cast<juce::int64> (snap.maxRecord.intervalTicks));
                    maxObj->setProperty ("numSamples", static_cast<juce::int64> (snap.maxRecord.numSamples));
                    maxObj->setProperty ("flags", static_cast<juce::int64> (snap.maxRecord.flags));
                    maxObj->setProperty ("trackCount", static_cast<juce::int64> (snap.maxRecord.contextTrackCount));
                    maxObj->setProperty ("graphVersion", static_cast<juce::int64> (snap.maxRecord.contextGraphVersion));
                    juce::DynamicObject::Ptr stageObj = new juce::DynamicObject();
                    static const char* stageNames[kCallbackAuditStageCount] =
                        { "input", "recording", "click", "monitorTrim", "engine", "masterBus", "clickSum", "controlRoom" };
                    for (size_t s = 0; s < (size_t) kCallbackAuditStageCount; ++s)
                        stageObj->setProperty (stageNames[s], static_cast<juce::int64> (snap.maxRecord.stageTicks[s]));
                    maxObj->setProperty ("stageTicks", juce::var (stageObj.get()));
                    root->setProperty ("maxRecord", juce::var (maxObj.get()));
                }
                root->setProperty ("cpuPercent", deviceManager.getCpuUsage() * 100.0);
                root->setProperty ("xruns", xruns);
                root->setProperty ("sampleRate", sampleRate);
                root->setProperty ("blockSize", blockSize);
                root->setProperty ("streamGeneration", static_cast<juce::int64> (callbackAuditStreamGeneration_));

                const auto jsonString = juce::JSON::toString (juce::var (root.get()), true);
                const auto file = juce::File (callbackAuditOutputPath_);
                file.getParentDirectory().createDirectory();
                const auto tempPath = callbackAuditOutputPath_ + ".tmp";
                juce::FileOutputStream ofs (tempPath);
                if (ofs.openedOk())
                {
                    ofs.writeText (jsonString, false, false, nullptr);
                    ofs.flush();
                    juce::File (tempPath).moveFileTo (callbackAuditOutputPath_);
                }
            }
        }
    }

    // Drain recording-transition counters (RT-safe counters → message-thread Logger).
    appCore_.drainRecGuardCounters();

    // C5: destroy plugins retired from non-message threads (never via callSync).
    DAW::PluginChainCore::drainRetiredPlugins();

    // Phase D: sandbox worker death/hang detection + bounded automatic restart
    // (control plane only; the audio thread only observes the existing gate).
    appCore_.serviceSandboxWorkers();

    // Never touch the device while the device panel owns it or while a take
    // is rolling - a device restart mid-take would destroy the recording.
    if (audioSuspendedForDevicePanel_)
        return;
    if (appCore_.getTransport().isRecording() || appCore_.getRecordingEngine().isRecording())
        return;

    auto* dev = getOwnedDeviceManager().getCurrentAudioDevice();
    if (dev == nullptr)
        return;

    // Topology-change reprepare (previously the flag was set in the realtime
    // callback but never consumed — I/O changes such as S/MUX mode or custom
    // I/O matrix edits were silently clamped until the next device restart).
    // Guarded identically to the watchdog below: never while the panel owns
    // the device or a take is rolling. Uses the proven repair path, which
    // forces a device restart so audioDeviceAboutToStart re-reads the true
    // channel topology and re-prepares the input buffers.
    if (needsInputBufferReprepare_.exchange(false, std::memory_order_acq_rel))
    {
        juce::Logger::writeToLog("[APEX-INPUT-FIX] input topology changed - forcing reprepare");
        ensureInputChannelsActive("topology-change");
        return;
    }

    const int activeIn = dev->getActiveInputChannels().countNumberOfSetBits();

    if (activeIn > 0)
    {
        if (!inputWatchdogWasHealthy_)
            juce::Logger::writeToLog("[APEX-INPUT-WATCHDOG] input healthy: "
                + juce::String(activeIn) + " active channel(s) on " + dev->getName());
        inputWatchdogWasHealthy_ = true;
        inputWatchdogRepairAttempts_ = 0;
        return;
    }

    inputWatchdogWasHealthy_ = false;

    if (inputWatchdogRepairAttempts_ >= kMaxInputWatchdogRepairAttempts)
        return; // exhausted - preflight/record path will surface the warning

    ++inputWatchdogRepairAttempts_;
    juce::Logger::writeToLog("[APEX-INPUT-WATCHDOG] active inputs = 0 on " + dev->getName()
        + " - repair attempt " + juce::String(inputWatchdogRepairAttempts_)
        + "/" + juce::String(kMaxInputWatchdogRepairAttempts));

    if (ensureInputChannelsActive("watchdog"))
    {
        const int repaired = getEnabledAudioInputChannelCount();
        cachedEnabledAudioInputChannels_.store(repaired, std::memory_order_release);
        appCore_.setHardwareInputChannelCount(repaired);
        inputWatchdogRepairAttempts_ = 0;
        inputWatchdogWasHealthy_ = true;
        juce::Logger::writeToLog("[APEX-INPUT-WATCHDOG] repair OK - "
            + juce::String(repaired) + " input channel(s) active");
    }
}

void MainComponent::releaseResources()
{
    appCore_.releaseResources();
}

void MainComponent::paint(juce::Graphics& g)
{
    auto& apex = DAW::Theme::getInstance().apex;

    // APEX signal-core base: the near-black creative space.
    g.fillAll(apex.color.deepestA);

    // ── Recovery banner (functional warning; amber semantics preserved) ──────
    if (recoveryBannerVisible_)
    {
        auto bannerBounds = juce::Rectangle<int>(0, kMenuH + kTransportH, getWidth(), 22);
        g.setColour(DAW::Theme::getInstance().colors.warning.darker(0.25f).withAlpha(0.90f));
        g.fillRect(bannerBounds);
        g.setColour(apex.color.textPrimary);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("  Recovered autosave - Save As recommended",
                   bannerBounds.reduced(4, 0),
                   juce::Justification::centredLeft, true);
    }

    // ── Autosave status text (bottom-right corner, subtle) ───────────────────
    if (autosaveStatusText_.isNotEmpty())
    {
        g.setColour(apex.color.textMuted);
        g.setFont(juce::Font(10.5f));
        g.drawText(autosaveStatusText_,
                   getLocalBounds().reduced(8, 4),
                   juce::Justification::bottomRight, true);
    }

    // Track list / timeline separator — magenta→violet signal hairline.
    // Starts below the top command/control zone (menu bar + transport).
    if (trackListWindow_ && trackListWindow_->isVisible() && trackListW_ > 0)
    {
        const int x = trackListW_;
        const float top = (float)(kMenuH + kTransportH);
        g.setColour(apex.color.deepestB.withAlpha(0.9f));
        g.fillRect((float)x - 2.0f, top, 4.0f, (float)getHeight() - top);

        juce::ColourGradient sep(apex.color.magenta.withAlpha(0.60f), (float)x, top,
                                 apex.color.violet.withAlpha(0.45f),  (float)x, (float)getHeight(), false);
        g.setGradientFill(sep);
        g.drawLine((float)x, top, (float)x, (float)getHeight(), 1.5f);
    }
}

void MainComponent::resized()
{
    // ── One authoritative inner content rectangle ─────────────────────────
    // The top-level window shell (see MainWindow in Main.cpp) insets this
    // component by the shell frame thickness, so every child — including the
    // timeline's scrollbars — stays fully inside the outer border. Major
    // sections are derived from this single rectangle by vertical division:
    //   1. APEX header (custom title bar: menus + window controls)
    //   2. TransportBar (top command/control zone)
    //   3. main workspace (track list + timeline; docked mixer at its bottom)
    // No section positions itself from unrelated magic constants.
    auto area = getLocalBounds();

    if (scanDialog_)
        scanDialog_->setBounds(area);
    if (startupPanel_)
        startupPanel_->setBounds(getLocalBounds());
    if (loadingOverlay_)
        loadingOverlay_->setBounds(getLocalBounds());

    if (menuBar_)
        menuBar_->setBounds(area.removeFromTop(kMenuH));

    // Transport lives in the TOP command/control zone (the original APEX shell
    // design); the bottom belongs to the workspace and the docked mixer.
    if (transportBar_)
        transportBar_->setBounds(area.removeFromTop(kTransportH));

    if (bubblegumTaskbar_)
        bubblegumTaskbar_->setBounds({});
    if (bubbleTaskbar_)
        bubbleTaskbar_->setBounds(getLocalBounds());

    const auto bubbleSize = 54;
    const auto timelineBubbleSize = DAW::TimelineBubble::kBubbleSize;
    if (mixerBubble_)
    {
        // Always keep the bubble component covering the whole window so drag works.
        // MixerBubble draws the circle at its own internal bubbleX_/bubbleY_.
        // If we clamp its bounds to a tiny square, it cannot be moved outside it.
        mixerBubble_->setBounds(getLocalBounds());
        if (!mixerBubble_->wasDraggedByUser())
            mixerBubble_->resized();
    }
    if (timelineBubble_)
        timelineBubble_->setBounds(getWidth() - timelineBubbleSize - 86, getHeight() - timelineBubbleSize - 76,
                                   timelineBubbleSize, timelineBubbleSize);
    if (markerBubble_)
        markerBubble_->setBounds(getLocalBounds());
    if (bubblegumOrb_)
        bubblegumOrb_->setBounds(getWidth() - bubbleSize - 18, 92, bubbleSize, bubbleSize);
    if (masterBubble_)
        masterBubble_->setBounds(84, getHeight() - bubbleSize - 76, bubbleSize, bubbleSize);

    // TrackLens peak bubble - top-right, only auto-position if not manually dragged
    {
        const int bw = DAW::SelectedTrackPeakBubbleComponent::preferredWidth();
        const int bh = DAW::SelectedTrackPeakBubbleComponent::preferredHeight();
        const int margin = 12;
        if (!peakBubble_.wasDraggedByUser())
            peakBubble_.setTopLeftPosition(getWidth() - bw - margin,
                                           kMenuH + kTransportH + margin);
    }

    auto content = area;
    if (mixerDocked_ && mixerPanel_)
    {
        auto mixerArea = content.removeFromBottom(mixerH_);
        if (monitorSection_)
            monitorSection_->setBounds(mixerArea.removeFromRight(DAW::MonitorSectionUI::kPreferredWidth));

        mixerViewport_.setBounds(mixerArea);
        // MixerPanel::rebuildStrips() owns the Master-aware content width.
        mixerPanel_->setSize(mixerPanel_->getWidth(), mixerArea.getHeight());
    }

    if (trackListWindow_ && trackListWindow_->isVisible() && trackListW_ > 0 && !trackListWindow_->isMaximized())
        trackListWindow_->setBounds(content.removeFromLeft(trackListW_));

    if (timelineWindow_)
        timelineWindow_->setBounds(content);

    updateTimelineViewportContentBounds();

    if (mixerWindow_ && !mixerDocked_)
    {
        // Place the floating mixer exactly once, in a SAFE position that never
        // covers the app header (kMenuH + kTransportH). The MixerWindow ctor
        // pre-sizes itself, so bounds are never empty — the placed flag (set
        // by user move/resize and by Reset Size) is the real guard.
        if (!mixerWindowPlaced_)
        {
            // Default to six normal strips plus the pinned Master; the user can resize the floating
            // window freely (the viewport scrolls horizontally for the rest).
            setDefaultMixerWindowBounds();
        }
    }

    if (pianoRollWindow_ && pianoRollWindow_->getBounds().isEmpty())
        pianoRollWindow_->setBounds(getLocalBounds().reduced(90, 70));

    updateSlimeAttachedPosition();
    if (browserChrome_ && browserChrome_->isVisible() && !browserChrome_->isMaximized())
        browserChrome_->setBounds(24, kMenuH + kTransportH + 18, 340, juce::jmin(560, getHeight() - 150));

    if (settingsPanel_ && settingsPanel_->isVisible())
        settingsPanel_->setBounds(getLocalBounds().reduced(120, 70));

    if (quitSafetyDialog_)
        quitSafetyDialog_->setBounds(getLocalBounds());

    cableOverlay_.setBounds(getLocalBounds());
    offscreenEndpoint_.setBounds(getLocalBounds());
   #if JUCE_DEBUG
    // Left of the custom title-bar window controls so the overlay never covers
    // the minimize / maximize / close buttons.
    fpsOverlay_.setBounds(getWidth() - 70 - 160, 6, 60, 20);
   #endif

    refreshBubblegumOffscreenState();
}

void MainComponent::setDefaultMixerWindowBounds()
{
    if (!mixerWindow_ || !mixerPanel_)
        return;

    // The outer frame/title chrome is not part of the Mixer content width.
    // Derive it from the live window rather than duplicating frame constants.
    const int chromeW = juce::jmax(0,
        mixerWindow_->getWidth() - mixerWindow_->getContentArea().getWidth());
    const int desiredContentW = mixerPanel_->getDefaultVisibleContentWidth();
    const int availableContentW = mixerPanel_->getWidth();
    const int contentW = availableContentW > 0
        ? juce::jmin(desiredContentW, availableContentW)
        : desiredContentW;

    // A project with fewer than six normal tracks should not receive an
    // unnecessary blank region, while a larger project remains scrollable.
    const int desiredWindowW = juce::jmax(DAW::FloatingWindowBase::kMinW,
                                         contentW + chromeW);
    const int maxWindowW = juce::jmax(DAW::FloatingWindowBase::kMinW,
                                      availableContentW + chromeW);
    const int windowW = juce::jmin(desiredWindowW, maxWindowW);
    const int windowY = juce::jmax(kMenuH + kTransportH + 20,
                                   getHeight() - 380);

    mixerWindow_->setBounds(80, windowY, windowW, 320);
    mixerWindowPlaced_ = true;
}

void MainComponent::updateTimelineViewportContentBounds()
{
    if (!arrangement_)
        return;

    const auto viewportBounds = timelineWindow_ != nullptr && timelineWindow_->isVisible()
        ? timelineWindow_->getContentArea()
        : (timelineViewport_ != nullptr && timelineViewport_->getParentComponent() != nullptr ? timelineViewport_->getBounds() : juce::Rectangle<int>());

    const int viewportW = juce::jmax(1, viewportBounds.getWidth());
    const int viewportH = juce::jmax(1, viewportBounds.getHeight());

    const int trackCount = juce::jmax(1, arrangement_->getNumLanes());
    int contentH = 80;
    for (int i = 0; i < trackCount; ++i)
        contentH += arrangement_->getLaneHeight(i);

    const auto zoomPxPerSecond = arrangement_->getPixelsPerSecond();
    double projectLengthSeconds = 30.0;
    const auto sampleRate = juce::jmax(1.0, appCore_.getCurrentSampleRate());
    for (auto* clip : appCore_.getClipManager().getAllClips())
    {
        if (clip == nullptr)
            continue;

        const double clipEndSeconds = (double)clip->getEndPosition() / sampleRate;
        projectLengthSeconds = juce::jmax(projectLengthSeconds, clipEndSeconds + 2.0);
    }

    // The timeline must never clamp at the last clip: the playhead and the
    // loop-region end also deserve scroll room, otherwise scrubbing/looping
    // past the material hits a hard scrollbar limit.
    projectLengthSeconds = juce::jmax(projectLengthSeconds,
        (double) appCore_.getTransport().getPosition() / sampleRate + 2.0);
    const auto loopRange = appCore_.getTransport().getLoopRange();
    projectLengthSeconds = juce::jmax(projectLengthSeconds,
        (double) loopRange.second / sampleRate + 2.0);
    lastTimelineContentLengthSeconds_ = projectLengthSeconds;

    // Content width = zoom * project length, but never below viewportW + 1.
    // At extreme zoom-out the raw length would shrink under the viewport and
    // collapse into an empty void with the horizontal scrollbar hidden — the
    // same way the vertical clamp to viewportH + 1 keeps the vertical bar
    // alive. Keeping one extra pixel of scroll range means the timeline
    // always fills the view and the scrollbar never disappears.
    const int contentW = juce::jmax(viewportW + 1,
        juce::jmax(4, (int)std::ceil(zoomPxPerSecond * projectLengthSeconds)));

    arrangement_->setSize(contentW, juce::jmax(viewportH + 1, contentH));

    // Force the viewport to re-sync its scrollbar ranges and repaint the thumbs.
    // JUCE only does this when the view position is set, not on content resize alone.
    if (timelineViewport_ != nullptr)
    {
        const auto cur = timelineViewport_->getViewPosition();
        const int maxX = juce::jmax(0, contentW    - timelineViewport_->getViewWidth());
        const int maxY = juce::jmax(0, contentH    - timelineViewport_->getViewHeight());
        timelineViewport_->setViewPosition(juce::jlimit(0, maxX, cur.x),
                                           juce::jlimit(0, maxY, cur.y));
        if (arrangement_)
            arrangement_->setViewportScrollOffsets(timelineViewport_->getViewPositionX(), timelineViewport_->getViewPositionY());
        timelineViewport_->getHorizontalScrollBar().repaint();
        timelineViewport_->getVerticalScrollBar().repaint();
    }

    if (trackList_)
        trackList_->setScrollOffset(timelineViewport_ != nullptr ? timelineViewport_->getViewPosition().y : 0);
}

void MainComponent::positionChanged(DAW::SamplePosition newPosition)
{
    // Grow the timeline scroll extent when the playhead approaches or passes
    // the current content end (ruler scrubbing, transport commands, looping).
    // Guarded by the cached length so the viewport bounds are not rebuilt on
    // every transport tick during playback.
    const double sampleRate = juce::jmax(1.0, appCore_.getCurrentSampleRate());
    const double playheadSeconds = (double) newPosition / sampleRate;
    if (playheadSeconds + 2.0 > lastTimelineContentLengthSeconds_)
        updateTimelineViewportContentBounds();
}

void MainComponent::transportStateChanged()
{
    if (mixerPanel_ != nullptr)
    {
        auto& transport = appCore_.getTransport();
        mixerPanel_->setTransportActive(transport.isPlaying() || transport.isRecording());
    }
}

void MainComponent::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void MainComponent::mouseDrag(const juce::MouseEvent&)
{
}

void MainComponent::mouseMove(const juce::MouseEvent&)
{
}

bool MainComponent::requestQuitFromWindowClose()
{
    if (!appCore_.getProjectManager().isDirty())
        return true;

    quitSafetyDialogOpen_ = true;
    if (quitSafetyDialog_)
    {
        quitSafetyDialog_->setVisible(true);
        quitSafetyDialog_->toFront(true);
    }
    return false;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // Space is handled exclusively through KeyBindingManager -> ActionID::TransportPlayStop
    // to avoid double-firing (direct call + action dispatch in same keyPressed).

    // Quick Send mode: Delete removes the send from the source to the selected
    // track; Escape exits the mode. Both take priority over the generic
    // multi-selection delete/escape below.
    if (quickSendMode_.isActive())
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            quickSendDeleteSend();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            exitQuickSendMode();
            return true;
        }
    }

    // Delete: if multiple tracks selected, delete all selected (non-master)
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        const auto& selTracks = multiSelection_.getSelected(DAW::SelectionKind::Track);
        if (selTracks.size() > 1)
        {
            auto& tm = appCore_.getTrackManager();
            std::vector<juce::String> toDelete;
            for (const auto& t : selTracks)
                if (auto* track = tm.getTrack(t.trackId))
                    if (!track->isMaster())
                        toDelete.push_back(t.trackId);

            if (!toDelete.empty())
            {
                auto options = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Delete Selected Tracks")
                    .withMessage("Delete " + juce::String(toDelete.size()) + " selected tracks?")
                    .withButton("Delete")
                    .withButton("Cancel")
                    .withAssociatedComponent(this);

                juce::Component::SafePointer<MainComponent> safeThis(this);
                juce::AlertWindow::showAsync(options, [safeThis, toDelete](int result)
                {
                    if (result != 1 || !safeThis) return;
                    safeThis->deleteTracksAsOneTopologyTransaction(toDelete);
                });
                return true;
            }
        }
    }

    // Escape: clear multi-selection
    if (key == juce::KeyPress::escapeKey)
    {
        if (multiSelection_.count(DAW::SelectionKind::Track) > 1 ||
            multiSelection_.count(DAW::SelectionKind::Clip) > 0 ||
            multiSelection_.count(DAW::SelectionKind::PluginSlot) > 0)
        {
            auto primaryTrack = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
            multiSelection_.clear();
            // Restore single-track visual if one was primary
            if (primaryTrack.isValid() && trackList_)
            {
                multiSelection_.selectSingle(primaryTrack);
                trackList_->applyMultiSelectionVisual({ primaryTrack.trackId });
            }
            return true;
        }
    }

    // Arrow navigation / Arrange zoom are handled by the KeyListener
    // (keyPressed(key, component)) so they run BEFORE any focused scrollbar
    // consumes the arrows.
    return DAW::KeyBindingManager::getInstance().handleKeyPress(key);
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const bool mixerOpen = isMixerOpen();
    const bool mixerFocus = mixerOpen && hasMixerKeyboardFocus();
    const auto action = DAW::MixerKeyboardRoutingCore::resolve(key, mixerOpen, mixerFocus);

    switch (action)
    {
        case DAW::MixerKeyboardAction::ZoomHorizontalOut:
            applyTimelineZoom(false, false);
            return true;
        case DAW::MixerKeyboardAction::ZoomHorizontalIn:
            applyTimelineZoom(false, true);
            return true;
        case DAW::MixerKeyboardAction::ZoomVerticalIn:
            applyTimelineZoom(true, true);
            return true;
        case DAW::MixerKeyboardAction::ZoomVerticalOut:
            applyTimelineZoom(true, false);
            return true;
        case DAW::MixerKeyboardAction::SelectPrevious:
            cycleMixerSelection(-1);
            return true;
        case DAW::MixerKeyboardAction::SelectNext:
            cycleMixerSelection(1);
            return true;
        case DAW::MixerKeyboardAction::None:
        default:
            return false;
    }
}

bool MainComponent::hasMixerKeyboardFocus() const
{
    auto* fc = juce::Component::getCurrentlyFocusedComponent();
    if (fc == nullptr)
        return false;
    if (fc == this)
        return true;
    if (mixerDocked_)
        return mixerViewport_.isParentOf(fc);
    if (mixerWindow_ != nullptr && mixerWindow_->isParentOf(fc))
        return true;
    // The Bubblegum send panel participates in the mixer workflow.
    if (bubblegumPanel_ != nullptr && bubblegumPanel_->isParentOf(fc))
        return true;
    return false;
}

void MainComponent::applyTimelineZoom(bool isVertical, bool zoomIn)
{
    if (!arrangement_ || !timelineViewport_)
        return;

    if (!isVertical)
    {
        const double oldPps = juce::jmax(1.0, arrangement_->getPixelsPerSecond());
        const double newPps = juce::jlimit(5.0, 4000.0,
                                           zoomIn ? oldPps * 1.25 : oldPps / 1.25);
        if (juce::approximatelyEqual(oldPps, newPps))
            return;

        const double vpW       = (double) timelineViewport_->getViewWidth();
        const double centreSec = ((double) timelineViewport_->getViewPositionX()
                                  + vpW * 0.5) / oldPps;

        arrangement_->setPixelsPerSecond(newPps);
        updateTimelineViewportContentBounds();

        const int maxX = juce::jmax(0, arrangement_->getWidth()
                                       - timelineViewport_->getViewWidth());
        const int newX = juce::jlimit(0, maxX,
                                      juce::roundToInt(centreSec * newPps - vpW * 0.5));
        timelineViewport_->setViewPosition(newX, timelineViewport_->getViewPositionY());
    }
    else
    {
        const double oldLaneH = juce::jmax(1.0,
                                           (double) arrangement_->getLaneHeight(0));
        const double newLaneH = juce::jlimit(24.0, 240.0,
                                             zoomIn ? oldLaneH * 1.15
                                                    : oldLaneH / 1.15);
        if (juce::approximatelyEqual(oldLaneH, newLaneH))
            return;

        const double vpH        = (double) timelineViewport_->getViewHeight();
        const double centreLane = ((double) timelineViewport_->getViewPositionY()
                                   + vpH * 0.5) / oldLaneH;

        arrangement_->setLaneHeight(0, juce::roundToInt(newLaneH));
        updateTimelineViewportContentBounds();

        const int maxY = juce::jmax(0, arrangement_->getHeight()
                                       - timelineViewport_->getViewHeight());
        const int newY = juce::jlimit(0, maxY,
                                      juce::roundToInt(centreLane * newLaneH - vpH * 0.5));
        timelineViewport_->setViewPosition(timelineViewport_->getViewPositionX(), newY);

        if (trackList_)
            trackList_->setScrollOffset(timelineViewport_->getViewPositionY());
    }

    timelineViewport_->getHorizontalScrollBar().repaint();
    timelineViewport_->getVerticalScrollBar().repaint();
}

void MainComponent::parentHierarchyChanged()
{
    refreshBubblegumOffscreenState();
}

void MainComponent::visibilityChanged()
{
    refreshBubblegumOffscreenState();

    // Fire recovery prompt exactly once, after the component is on-screen
    if (isVisible() && !recoveryPromptFired_)
    {
        recoveryPromptFired_ = true;
        juce::Component::SafePointer<MainComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]()
        {
            if (auto* self = safeThis.getComponent())
            {
                self->appCore_.checkAndShowRecoveryPromptIfNeeded(self);

                if (self->appCore_.isRecoveredProject())
                {
                    self->recoveryBannerVisible_ = true;
                    self->repaint();
                }
            }
        });
    }
}

void MainComponent::undoHistoryChanged()
{
    // Every push/undo/redo mutates project state - keep dirty tracking and
    // autosave in sync exactly like manual edits do (pro-DAW behavior:
    // undoing back past the save point still counts as "modified").
    // clearHistory() also lands here with both stacks empty (project load) -
    // that must NOT dirty the freshly loaded project.
    auto& cm = DAW::CommandManager::getInstance();
    if (cm.canUndo() || cm.canRedo())
        appCore_.markProjectDirty("undo_history");

    // Live-update the menu bar's Undo/Redo labels + enabled states and the
    // toolbar arrow buttons.
    if (menuBar_)
        menuBar_->repaint();

    if (historyPanel_)
        historyPanel_->repaint();
}

void MainComponent::startPluginScanIfNeeded()
{
    if (scanStarted_ || scanDialog_ == nullptr)
        return;

    // Only auto-scan when there is no cached plugin data yet.
    // If cache exists, keep startup fast and rely on the persisted list.
    auto& scanner = appCore_.getPluginScanner();
    const bool hasCachedPlugins = scanner.getCache().getCount() > 0
        || scanner.getKnownPlugins().getNumTypes() > 0;
    if (hasCachedPlugins)
    {
        scanStarted_ = true;
        // Show scan dialog briefly with cached status, then proceed to startup panel
        scanDialog_->setBounds(getLocalBounds());
        scanDialog_->setVisible(true);
        scanDialog_->toFront(true);
        scanDialog_->showCachedLoadAndAutoClose();
        return;
    }

    appendMainStartupTrace("startPluginScanIfNeeded.begin");
    scanStarted_ = true;
    scanDialog_->setBounds(getLocalBounds());
    scanDialog_->setVisible(true);
    scanDialog_->toFront(true);
    scanDialog_->startScan();
    appendMainStartupTrace("startPluginScanIfNeeded.after-startScan");
}

void MainComponent::openPianoRollForClip(DAW::MidiClip& clip)
{
    juce::ignoreUnused(clip);
    // DISABLED (product decision): the piano roll is a beat-making surface
    // and is hidden. The wiring stays in place so double-clicks on MIDI clips
    // remain inert instead of opening an editor; re-enable by restoring the
    // body below.
    // if (!pianoRollWindow_) return;
    // pianoRollWindow_->setMidiClip(&clip);
    // pianoRollWindow_->setVisible(true);
    // if (pianoRollWindow_->getBounds().isEmpty())
    //     pianoRollWindow_->setBounds(getLocalBounds().reduced(90, 70));
    // pianoRollWindow_->toFront(true);
}

void MainComponent::showExportAudioDialog()
{
    const auto makeFormatMenu = [](int base)
    {
        juce::PopupMenu menu;
        menu.addItem(base + 1, "WAV — 16-bit PCM");
        menu.addItem(base + 2, "WAV — 24-bit PCM");
        menu.addItem(base + 3, "WAV — 32-bit float");
        menu.addSeparator();
        menu.addItem(base + 4, "AIFF — 16-bit PCM");
        menu.addItem(base + 5, "AIFF — 24-bit PCM");
        menu.addItem(base + 6, "FLAC — 16-bit lossless");
        menu.addItem(base + 7, "FLAC — 24-bit lossless");
        menu.addItem(base + 8, "Ogg Vorbis — high quality");
        return menu;
    };

    juce::PopupMenu menu;
    menu.addSubMenu("Full Mix", makeFormatMenu(0));
    menu.addSeparator();
    menu.addSubMenu("Stems — Individual Tracks (Pre-Master)", makeFormatMenu(100));
    menu.addSubMenu("Stems — Folder Buses (Pre-Master)", makeFormatMenu(200));
    menu.addSubMenu("Stems — Tracks + Folder Buses (Pre-Master)", makeFormatMenu(300));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int choice)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr || choice <= 0)
            return;

        const int scope = choice / 100;
        const int formatChoice = choice % 100;
        DAW::ExportSettings settings;
        settings.content = scope == 0 ? DAW::ExportContent::FullMix : DAW::ExportContent::Stems;
        settings.tailSeconds = 4.0;
        settings.qualityIndex = 8;

        switch (formatChoice)
        {
            case 1: settings.format = DAW::ExportAudioFormat::Wav; settings.bitDepth = 16; break;
            case 2: settings.format = DAW::ExportAudioFormat::Wav; settings.bitDepth = 24; break;
            case 3: settings.format = DAW::ExportAudioFormat::Wav; settings.bitDepth = 32; break;
            case 4: settings.format = DAW::ExportAudioFormat::Aiff; settings.bitDepth = 16; break;
            case 5: settings.format = DAW::ExportAudioFormat::Aiff; settings.bitDepth = 24; break;
            case 6: settings.format = DAW::ExportAudioFormat::Flac; settings.bitDepth = 16; settings.qualityIndex = 5; break;
            case 7: settings.format = DAW::ExportAudioFormat::Flac; settings.bitDepth = 24; settings.qualityIndex = 5; break;
            case 8: settings.format = DAW::ExportAudioFormat::OggVorbis; settings.bitDepth = 32; settings.qualityIndex = 8; break;
            default: return;
        }

        if (settings.content == DAW::ExportContent::Stems)
        {
            auto& tm = self->appCore_.getTrackManager();
            auto& folders = self->appCore_.getFolderBus();
            const bool includeTracks = scope == 1 || scope == 3;
            const bool includeFolders = scope == 2 || scope == 3;

            for (int i = 0; i < tm.getNumTracks(); ++i)
            {
                auto* track = tm.getTrack(i);
                if (track == nullptr || track->isMaster())
                    continue;

                const bool isFolder = track->getRole() == DAW::TrackRole::FolderBus;
                if ((isFolder && !includeFolders) || (!isFolder && !includeTracks))
                    continue;
                // Generic Aux/Bus return stems need a separate upstream-tap
                // policy. Do not mislabel them as individual source tracks.
                if (!isFolder && (track->getRole() == DAW::TrackRole::Aux
                                  || track->getRole() == DAW::TrackRole::Bus))
                    continue;

                DAW::StemExportTarget target;
                target.trackId = track->getID();
                target.displayName = track->getName();
                target.kind = isFolder ? DAW::StemTargetKind::FolderBus : DAW::StemTargetKind::Track;
                target.sourceTrackIds.push_back(track->getID());
                if (isFolder)
                    for (const auto& descendant : folders.getAllDescendants(track->getID()))
                        target.sourceTrackIds.push_back(descendant);
                settings.stemTargets.push_back(std::move(target));
            }
            if (settings.stemTargets.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "No Stem Targets", "This project has no tracks matching that stem category.");
                return;
            }
        }

        const auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
        const auto projectName = self->appCore_.getProjectManager().getProjectName();
        const auto defaultPath = settings.content == DAW::ExportContent::Stems
            ? desktop.getChildFile(projectName + " Stems")
            : desktop.getChildFile(projectName + exportFormatExtension(settings.format));
        const auto wildcard = settings.content == DAW::ExportContent::Stems
            ? juce::String("*") : "*" + exportFormatExtension(settings.format);
        auto chooser = std::make_shared<juce::FileChooser>(
            settings.content == DAW::ExportContent::Stems ? "Name Stem Package" : "Export Audio",
            defaultPath, wildcard);

        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | (settings.content == DAW::ExportContent::FullMix
                                ? juce::FileBrowserComponent::warnAboutOverwriting : 0),
            [safeThis, chooser, settings = std::move(settings)](const juce::FileChooser& fc) mutable
            {
                auto* owner = safeThis.getComponent();
                if (owner == nullptr)
                    return;
                auto destination = fc.getResult();
                if (destination == juce::File{})
                    return;

                if (settings.content == DAW::ExportContent::FullMix)
                {
                    const auto expected = exportFormatExtension(settings.format);
                    if (!destination.getFileExtension().equalsIgnoreCase(expected))
                        destination = destination.withFileExtension(expected);
                    settings.outputFile = destination;
                }
                else
                {
                    if (destination.getFileExtension().isNotEmpty())
                        destination = destination.withFileExtension({});
                    settings.outputDirectory = destination;
                }
                owner->startAudioExport(std::move(settings), destination);
            });
    });
}

void MainComponent::startAudioExport(DAW::ExportSettings settings, const juce::File& displayDestination)
{
    if (exportRenderCore_ && exportRenderCore_->isExportRunning())
        return;
    if (exportRenderCore_)
        cancelAndJoinExportBeforeAudioRelease();

    DAW::AudioResourceReleaseStateCore::Generation claimedDeviceGeneration = 0;
    {
        const juce::ScopedLock lock(audioDevicePreparationLock_);
        claimedDeviceGeneration = audioResourceReleaseState_.claimExport();
    }
    if (claimedDeviceGeneration == 0)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Export Unavailable", "Start an audio device before exporting.");
        return;
    }

    exportRenderCore_ = std::make_unique<DAW::ExportRenderCore>(appCore_, std::move(settings));
    const auto exportGeneration = ++exportLifecycleGeneration_;
    activeExportLifecycleGeneration_ = exportGeneration;
    auto* progressWindow = new DAW::ExportProgressWindow(*exportRenderCore_, displayDestination, this);
    exportProgressWindow_ = progressWindow;
    juce::Component::SafePointer<DAW::ExportProgressWindow> safeProgressWindow(progressWindow);
    juce::Component::SafePointer<MainComponent> safeThis(this);

    const bool started = exportRenderCore_->startExport(
        [safeProgressWindow, safeThis, exportGeneration](DAW::ExportRenderCore::Result result) mutable
        {
            if (auto* window = safeProgressWindow.getComponent()) window->exportFinished(result);
            if (auto* main = safeThis.getComponent()) main->finishExportOnMessageThread(exportGeneration);
        });
    if (!started)
    {
        progressWindow->prepareForOwnerShutdown();
        exportProgressWindow_ = nullptr;
        exportRenderCore_.reset();
        finishExportOnMessageThread(exportGeneration);
    }
}

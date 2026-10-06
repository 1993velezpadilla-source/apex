#include "MainComponent.h"
#include "AutomationSequence/AutomationSequenceDialogComponent.h"
#include "PluginScanCore/PluginScanAuditLogCore.h"
#include "Bubblegum/BubblegumAppearanceSettings.h"
#include "ThemeCore/Theme.h"
#include "MidiCore/MidiClip.h"
#include "CommandCore/CommandManager.h"
#include "CommandCore/GeneralCommands.h"
#include "RenderCore/ExportProgressWindow.h"
#include "UICore/CrashRecoveryDialogComponent.h"
#include "DeviceCore/SafeAsioDeviceTypeCore.h"
#include "VocalTuneCore/ApexTuneIntegrationCore.h"
#include <set>

namespace
{
    // Request a practical multichannel input ceiling so JUCE can expose and
    // compact the user's active ASIO input pair instead of hard-limiting the
    // app to channels 1-2.
    static constexpr int kMaxHardwareInputChannels = 64;

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
        return isVertical() ? juce::MouseCursor::UpDownResizeCursor
                            : juce::MouseCursor::LeftRightResizeCursor;
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
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // ---------------------------------------------------------------- overrides
    void mouseMove(const juce::MouseEvent& e) override { refreshCursor(e); juce::ScrollBar::mouseMove(e); }
    void mouseEnter(const juce::MouseEvent& e) override { refreshCursor(e); juce::ScrollBar::mouseEnter(e); }
    void mouseExit(const juce::MouseEvent& e) override
    {
        if (dragMode_ == DragMode::None) setMouseCursor(juce::MouseCursor::NormalCursor);
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

    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    void mouseExit(const juce::MouseEvent&) override  { if (!dragging_) setMouseCursor(juce::MouseCursor::NormalCursor); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = true;
        lastScreenY_ = e.getScreenPosition().y;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
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
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
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
        ClipRegionPluginWindow(const juce::String& title, DAW::TransportController& transport)
            : juce::DocumentWindow(title,
                                   juce::Colour(0xFF202020),
                                   juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::closeButton,
                                   true),
              transport_(transport)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, false);
            setResizeLimits(240, 160, 3200, 1800);
            setDropShadowEnabled(true);
        }

        std::function<void()> onCloseRequested;

        void closeButtonPressed() override
        {
            if (onCloseRequested) onCloseRequested();
        }

        void minimiseButtonPressed() override
        {
            setVisible(false);
            if (onMinimizeRequested) onMinimizeRequested();
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
        juce::Rectangle<int> restoreBounds_;
        bool maximized_ = false;
    };
}

//==============================================================================
MainComponent::MainComponent()
{
    appendMainStartupTrace("ctor.begin");
    DAW::CommandManager::getInstance();
    setSize(1280, 800);
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
    DAW::CommandManager::getInstance().setUndoRedoBlockedQuery([this]
    {
        return appCore_.getTransport().isRecording()
            || appCore_.getRecordingEngine().isRecording();
    });
    appendMainStartupTrace("ctor.after-command-listener");

    // Subscribe to track manager for real-time Bubblegum/offscreen refresh
    appCore_.getTrackManager().addListener(this);
    appendMainStartupTrace("ctor.after-track-listener");

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

        if (pm.save())
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
    menuBar_->setMenuAction("File", "Export WAV...", [this]
    {
        showExportWavDialog();
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
        // Single-click without modifiers: single-select
        multiSelection_.selectSingle(DAW::SelectionTarget::track(id));

        appCore_.getState().selectedTrackID.setValue(id);
        DBG("[MainComponent] Shared selected-track state updated (from timeline): id=" + id);

        // Update visuals
        trackList_->applyMultiSelectionVisual(
            [&]{ std::vector<juce::String> ids;
                 for (auto& t : multiSelection_.getSelected(DAW::SelectionKind::Track)) ids.push_back(t.trackId);
                 return ids; }());

        // Keep Bubblegum source + offscreen endpoint synced immediately to selection,
        // without waiting for a viewport scroll invalidation.
        auto& bgV2 = appCore_.getBubblegumV2();
        bgV2.onTrackSelected(id);

        // Sync mixer strip selection (visual only - no callback loop)
        if (mixerPanel_)
            mixerPanel_->selectTrackVisual(id);

        // Update plugin side panel with selected track
        if (pluginSidePanel_)
        {
            auto* track = appCore_.getTrackManager().getTrack(id);
            auto* chain = track ? appCore_.getPluginChain(id) : nullptr;
            if (track)
                pluginSidePanel_->setTrack(track, chain);
            else
                pluginSidePanel_->clearTrack();
        }
    };

    trackList_->onTrackSelectedWithModifiers = [this](const DAW::TrackID& id, const juce::ModifierKeys& mods)
    {
        const bool ctrl  = mods.isCtrlDown() || mods.isCommandDown();
        const bool shift = mods.isShiftDown();

        if (ctrl && shift)
        {
            // Add range to existing selection
            auto ordered = trackList_->getVisibleTrackIds();
            std::vector<DAW::SelectionTarget> orderedTargets;
            orderedTargets.reserve(ordered.size());
            for (auto& oid : ordered) orderedTargets.push_back(DAW::SelectionTarget::track(oid));
            multiSelection_.selectRange(DAW::SelectionTarget::track(id), orderedTargets, /*additive=*/true);
        }
        else if (shift)
        {
            auto ordered = trackList_->getVisibleTrackIds();
            std::vector<DAW::SelectionTarget> orderedTargets;
            orderedTargets.reserve(ordered.size());
            for (auto& oid : ordered) orderedTargets.push_back(DAW::SelectionTarget::track(oid));
            multiSelection_.selectRange(DAW::SelectionTarget::track(id), orderedTargets, /*additive=*/false);
        }
        else if (ctrl)
        {
            multiSelection_.toggle(DAW::SelectionTarget::track(id));
        }
        else
        {
            multiSelection_.selectSingle(DAW::SelectionTarget::track(id));
        }

        // Determine primary (last/clicked) track for plugin panel and Bubblegum
        const auto primary = multiSelection_.getPrimaryTarget(DAW::SelectionKind::Track);
        const juce::String primaryId = primary.isValid() ? primary.trackId : id;

        appCore_.getState().selectedTrackID.setValue(primaryId);

        // Update TrackList visuals to reflect full multi-selection
        {
            std::vector<juce::String> ids;
            for (auto& t : multiSelection_.getSelected(DAW::SelectionKind::Track))
                ids.push_back(t.trackId);
            trackList_->applyMultiSelectionVisual(ids);
        }

        // Bubblegum stays linked to primary
        auto& bgV2 = appCore_.getBubblegumV2();
        bgV2.onTrackSelected(primaryId);

        // Sync mixer strip selection to primary
        if (mixerPanel_)
            mixerPanel_->selectTrackVisual(primaryId);

        // Plugin side panel follows primary track
        if (pluginSidePanel_)
        {
            auto* track = appCore_.getTrackManager().getTrack(primaryId);
            auto* chain = track ? appCore_.getPluginChain(primaryId) : nullptr;
            if (track) pluginSidePanel_->setTrack(track, chain);
            else       pluginSidePanel_->clearTrack();
        }
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
        std::vector<juce::String> toDelete;
        for (const auto& t : multiSelection_.getSelected(DAW::SelectionKind::Track))
            if (auto* track = tm.getTrack(t.trackId))
                if (!track->isMaster())
                    toDelete.push_back(t.trackId);
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
            for (const auto& id : toDelete) safeThis->deleteTrackOrFolder(id);
            safeThis->multiSelection_.clearKind(DAW::SelectionKind::Track);
            if (safeThis->trackList_) safeThis->trackList_->applyMultiSelectionVisual({});
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
        deleteTrackOrFolder(id);
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
        auto* srcChain = appCore_.getPluginChain(srcTrack);
        auto* dstChain = appCore_.getPluginChain(destTrack);
        if (srcChain && dstChain)
        {
            auto before = dstChain->getState();
            dstChain->copyPluginFrom(*srcChain, srcSlot, appCore_.getPluginScanner().getFormatManager());
            auto after = dstChain->getState();
            if (!before.isEquivalentTo(after))
                DAW::CommandManager::getInstance().execute(std::make_unique<DAW::PluginChainStateCommand>(
                    *dstChain, appCore_.getPluginScanner().getFormatManager(), before, after, "Copy Plugin", true));
            DBG("[MainComponent] Cross-track plugin copy via timeline row: src=" + srcTrack
                + " slot=" + juce::String(srcSlot) + " -> dest=" + destTrack);
            if (pluginSidePanel_ && pluginSidePanel_->isVisible())
            {
                auto selId = appCore_.getState().selectedTrackID.getValue().toString();
                if (selId == destTrack)
                {
                    auto* track = appCore_.getTrackManager().getTrack(destTrack);
                    pluginSidePanel_->setTrack(track, dstChain);
                }
            }
        }
    };

    // Timeline folder-drop: dragging a track row onto another row either
    // creates a new FolderBus (drop on regular track) or adopts into the
    // existing FolderBus (drop on folder-bus row). Mirrors mixer behaviour.
    trackList_->onFolderDropRequested = [this](const DAW::TrackID& draggedId,
                                                const DAW::TrackID& targetId)
    {
        handleFolderDropRequest(draggedId, targetId);
    };
    trackList_->onFolderCollapseChanged = [this](const DAW::TrackID& folderTrackId, bool expanded)
    {
        setFolderCollapsed(folderTrackId, !expanded);
    };
    trackList_->onTrackReorderRequested = [this](const DAW::TrackID& trackId, int targetIndex)
    {
        auto& tm = appCore_.getTrackManager();
        const int oldIndex = tm.getTrackIndex(trackId);
        if (oldIndex < 0)
            return;

        targetIndex = juce::jlimit(0, juce::jmax(0, tm.getNumTracks() - 1), targetIndex);
        if (targetIndex == oldIndex)
            return;

        moveTrackWithFolderAwareness(trackId, targetIndex);
        const int finalIndex = tm.getTrackIndex(trackId);
        if (finalIndex != oldIndex)
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackReorderCommand>(tm, trackId, oldIndex, finalIndex, true));
    };

    trackList_->onQuickAddTrackRequested = [this]
    {
        DAW::ActionManager::getInstance().dispatch(DAW::ActionID::TrackAddAudio);
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

    auto timelineViewport = std::make_unique<TimelineViewport>("timelineViewport");

    // Arrangement view (main timeline)
    arrangement_ = std::make_unique<DAW::ArrangementView>(
        appCore_.getTrackManager(),
        appCore_.getClipManager(),
        appCore_.getState(),
        appCore_.getMarkerManager(),
        appCore_.getTransport(),
        &appCore_.getAudioFileManager(),
        &appCore_.getAutomationManager());

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
    // (callbacks assigned later in constructor once sidePanelChrome_/browserChrome_ exist)
    arrangement_->onOpenMidiClip = [this](DAW::MidiClip& clip)
    {
        openPianoRollForClip(clip);
    };
    arrangement_->onOpenClipVocalTune = [this](DAW::Clip& clip)
    {
        appCore_.getState().selectedClipID.setValue(clip.getID());
        appCore_.getState().selectedTrackID.setValue(clip.getTrackID());
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
        ArrangementEditor::ClipAutomationPanelCallbacks clipAutomationCallbacks;
        clipAutomationCallbacks.createLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            DAW::AutomationQuickCreateCore::createLaneAndOptionalRegion(
                appCore_.getAutomationManager(), t, t.regionLengthSamples > 0);
            if (arrangement_)
            {
                arrangement_->showAutomationLane(t.trackId, t.parameterId);
                arrangement_->repaint();
            }
        };
        clipAutomationCallbacks.showLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            if (arrangement_)
                arrangement_->showAutomationLane(t.trackId, t.parameterId);
        };
        clipAutomationCallbacks.hideLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            if (arrangement_)
                arrangement_->hideAutomationLane(t.trackId, t.parameterId);
        };
        clipAutomationCallbacks.clearLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            auto& lane = appCore_.getAutomationManager().getOrCreateLane(t.trackId, t.parameterId);
            lane.clear();
            appCore_.getAutomationManager().publishSnapshot();
            if (arrangement_)
                arrangement_->repaint();
        };
        clipAutomationCallbacks.laneExists = [this](const DAW::TrackID& trackId, const juce::String& parameterId) -> bool
        {
            return appCore_.getAutomationManager().findLane(trackId, parameterId) != nullptr;
        };
        clipPropertiesWindow_->activateAutomationMode(clip.getTrackID(), clip.getID(), clipAutomationCallbacks,
                                                      &appCore_.getAutomationManager(),
                                                      &appCore_.getTransport());
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

            auto window = std::make_unique<ClipRegionPluginWindow>(title, appCore_.getTransport());
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
            if (auto* clip = appCore_.getClipManager().getClip(selectedId))
                if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
                    openPianoRollForClip(*midiClip);
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
            // NOTE: Do NOT call showAutomationLane here - automation is shown
            // only inside clip properties waveform preview, not in timeline lanes.
        };

        panelCb.showLane = [](const DAW::AutomationQuickCreateCore::ControlTarget&)
        {
            // Visibility is managed by the waveform overlay; no timeline lane shown.
        };

        panelCb.hideLane = [](const DAW::AutomationQuickCreateCore::ControlTarget&)
        {
            // Visibility is managed by the waveform overlay; no timeline lane hidden.
        };

        panelCb.clearLane = [this](const DAW::AutomationQuickCreateCore::ControlTarget& t)
        {
            appCore_.getAutomationManager().clearLane(t.trackId, t.parameterId);
            if (arrangement_)
                arrangement_->repaint();
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
    // Corner zoom buttons (bottom-right where the two scrollbars meet).
    // Zoom keeps the viewport centre anchored — pro-DAW behaviour.
    timelineViewport->onZoomButton = [this](bool isVertical, bool zoomIn)
    {
        if (!arrangement_ || !timelineViewport_)
            return;

        if (!isVertical)
        {
            const double oldPps = juce::jmax(1.0, arrangement_->getPixelsPerSecond());
            const double newPps = juce::jlimit(5.0, 4000.0, zoomIn ? oldPps * 1.25 : oldPps / 1.25);
            if (juce::approximatelyEqual(oldPps, newPps))
                return;

            const double vpW       = (double)timelineViewport_->getViewWidth();
            const double centreSec = ((double)timelineViewport_->getViewPositionX() + vpW * 0.5) / oldPps;

            arrangement_->setPixelsPerSecond(newPps);
            updateTimelineViewportContentBounds();

            const int maxX = juce::jmax(0, arrangement_->getWidth() - timelineViewport_->getViewWidth());
            const int newX = juce::jlimit(0, maxX, juce::roundToInt(centreSec * newPps - vpW * 0.5));
            timelineViewport_->setViewPosition(newX, timelineViewport_->getViewPositionY());
        }
        else
        {
            const double oldLaneH = juce::jmax(1.0, (double)arrangement_->getLaneHeight(0));
            const double newLaneH = juce::jlimit(24.0, 240.0, zoomIn ? oldLaneH * 1.15 : oldLaneH / 1.15);
            if (juce::approximatelyEqual(oldLaneH, newLaneH))
                return;

            const double vpH        = (double)timelineViewport_->getViewHeight();
            const double centreLane = ((double)timelineViewport_->getViewPositionY() + vpH * 0.5) / oldLaneH;

            arrangement_->setLaneHeight(0, juce::roundToInt(newLaneH));
            updateTimelineViewportContentBounds();

            const int maxY = juce::jmax(0, arrangement_->getHeight() - timelineViewport_->getViewHeight());
            const int newY = juce::jlimit(0, maxY, juce::roundToInt(centreLane * newLaneH - vpH * 0.5));
            timelineViewport_->setViewPosition(timelineViewport_->getViewPositionX(), newY);

            if (trackList_)
                trackList_->setScrollOffset(timelineViewport_->getViewPositionY());
        }

        timelineViewport_->getHorizontalScrollBar().repaint();
        timelineViewport_->getVerticalScrollBar().repaint();
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
    mixerViewport_.setScrollBarsShown(false, true);
    mixerViewport_.setScrollBarThickness(14);
    mixerViewport_.setVisible(true);
    addChildComponent(mixerViewport_);

    mixerWindow_ = std::make_unique<DAW::MixerWindow>();
    mixerWindow_->setContent(&mixerViewport_);
    mixerWindow_->setVisible(true);
    addChildComponent(mixerWindow_.get());
    mixerWindow_->addComponentListener(this);
    mixerWindow_->onDragEnded = [this]
    {
        cableOverlay_.notifyMotionFinished();
        refreshBubblegumOffscreenState();
    };

    cableOverlay_.bind(&appCore_.getBubblegumV2(), mixerPanel_.get(), &mixerViewport_);
    // Wire viewport scroll ? cable overlay so cables track mixer scroll in real-time
    // (the cable overlay timer also polls scroll pos each 60Hz tick as a safety net)
    mixerViewport_.getHorizontalScrollBar().addListener(this);
    mixerPanel_->onCableRepaintNeeded = [this]
    {
        cableOverlay_.notifyMotion();
        cableOverlay_.repaint();
    };

    // Re-enable mixer titlebar toolbar buttons for FX Chain + Plugin Browser
    mixerWindow_->onSidePanelClicked = [this]
    {
        if (!sidePanelChrome_ || !mixerWindow_ || !mixerPanel_)
            return;

        mixerPanel_->toggleSidePanel();
        mixerWindow_->sidePanelOpen = sidePanelChrome_->isVisible();
        mixerWindow_->refreshToolbar();
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
    cableOverlay_.onTick = [this]() { refreshBubblegumOffscreenState(); };
    addAndMakeVisible(offscreenEndpoint_);
    offscreenEndpoint_.toFront(false);

    // Bind offscreen endpoint
    offscreenEndpoint_.bind(&appCore_.getBubblegumV2(), mixerPanel_.get());
    offscreenEndpoint_.onScrollToTrack = [this](const DAW::TrackID& trackId)
    {
        // Scroll the mixer viewport so the target strip is visible
        if (!mixerPanel_) return;
        const auto& strips = mixerPanel_->getStrips();
        int x = 0;
        for (auto* strip : strips)
        {
            if (strip && strip->getTrack().getID() == trackId)
            {
                mixerViewport_.setViewPosition(juce::jmax(0, x - mixerViewport_.getWidth() / 2), 0);
                break;
            }
            if (strip) x += strip->getPreferredWidth() + DAW::MixerPanel::kStripGap;
        }
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

    // Track selection from mixer strip ? update app state + side panel + Bubblegum V2 source sync
    mixerPanel_->onTrackSelected = [this](const DAW::TrackID& id)
    {
        appCore_.getState().selectedTrackID.setValue(id);
        auto& bgV2 = appCore_.getBubblegumV2();
        DBG("[MainComponent::onTrackSelected(mixer)] id=" + id
            + " bgActive=" + juce::String(bgV2.isActive() ? 1 : 0)
            + " oldSource=" + bgV2.sourceSync.getSourceTrackId());

        // Bubblegum V2: auto-sync source from track selection (Rule 1)
        bgV2.onTrackSelected(id);
        DBG("[MainComponent::onTrackSelected(mixer)] source after sync=" + bgV2.sourceSync.getSourceTrackId()
            + " targets=" + juce::String(bgV2.targetList.getCount()));

        // Force mixer repaint so Bubblegum visuals update instantly
        if (bgV2.isActive())
        {
            mixerPanel_->repaint();
            // Refresh floating panel with new source
            if (bubblegumPanel_ && bubblegumPanel_->isVisible())
                bubblegumPanel_->refresh();
            // Push send feedback to mixer + timeline
            refreshBubblegumFeedback();
        }

        // Offscreen endpoint + sidechain/send cable visibility must also follow
        // track selection immediately, not only after viewport scroll changes.
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();

        // Sync timeline track list selection (visual only - no callback loop)
        if (trackList_)
            trackList_->selectTrackById(id);

        if (pluginSidePanel_)
        {
            auto* track = appCore_.getTrackManager().getTrack(id);
            auto* chain = track ? appCore_.getPluginChain(id) : nullptr;
            if (track) pluginSidePanel_->setTrack(track, chain);
            else       pluginSidePanel_->clearTrack();
        }
    };

    mixerPanel_->onTrackLensRequested = [this](const DAW::TrackID& id)
    {
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
        int numTracks = appCore_.getTrackManager().getNumTracks();
        mixerPanel_->setSize(
            juce::jmax(mixerArea.getWidth(),
                       numTracks * (DAW::MixerPanel::kStripW + DAW::MixerPanel::kStripGap) + 90),
            mixerArea.getHeight());
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
        if (!sidePanelChrome_)
            return;

        auto beforeVisible = sidePanelChrome_->isVisible();
        if (show)
        {
            sidePanelChrome_->restorePanel();
            DBG("[MainComponent] Side panel opened via mixer toggle");
        }
        else
        {
            sidePanelChrome_->closePanel();
            DBG("[MainComponent] Side panel closed via mixer toggle");
        }
        resized();
        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::onToggleSidePanel"
                " beforeVisible=" + juce::String(beforeVisible ? 1 : 0)
                + " afterVisible=" + juce::String(sidePanelChrome_->isVisible() ? 1 : 0)
                + " panelState=" + juce::String((int)sidePanelChrome_->getState())
                + " bounds=" + sidePanelChrome_->getBounds().toString());
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
        DAW::CommandManager::getInstance().execute(std::make_unique<DAW::AddTrackCommand>(
            tm, "Folder " + juce::String(tm.getNumTracks() + 1), DAW::TrackRole::FolderBus));
    };
    mixerPanel_->onDeleteTrack = [this](const DAW::TrackID& id)
    {
        deleteTrackOrFolder(id);
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
    mixerPanel_->onCommitTrackVolume = [this](const DAW::TrackID& id, float oldValue, float newValue)
    {
        if (std::abs(oldValue - newValue) > 0.0001f)
            DAW::CommandManager::getInstance().execute(std::make_unique<DAW::TrackPropertyChangeCommand>(
                appCore_.getTrackManager(), id, DAW::TrackPropertyChangeCommand::Property::Volume,
                (double) oldValue, (double) newValue, "Adjust Track Volume", true));
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
        moveTrackWithFolderAwareness(trackId, targetIndex);
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
        appCore_.getPluginScanner().cleanFullRescan();
        if (scanDialog_)
        {
            scanDialog_->setBounds(getLocalBounds());
            scanDialog_->setVisible(true);
            scanDialog_->toFront(true);
            scanDialog_->startScan();
        }
    };
    mixerPanel_->onPluginDropCopy = [this](const DAW::TrackID& srcTrack, int srcSlot,
                                            const DAW::TrackID& destTrack)
    {
        auto* srcChain = appCore_.getPluginChain(srcTrack);
        auto* dstChain = appCore_.getPluginChain(destTrack);
        if (srcChain && dstChain)
        {
            dstChain->copyPluginFrom(*srcChain, srcSlot, appCore_.getPluginScanner().getFormatManager());
            DBG("[MainComponent] Cross-track plugin copy via mixer strip: src=" + srcTrack
                + " slot=" + juce::String(srcSlot) + " -> dest=" + destTrack);
            // Refresh side panel if dest track is selected
            if (pluginSidePanel_ && sidePanelChrome_ && sidePanelChrome_->isVisible())
            {
                auto selId = appCore_.getState().selectedTrackID.getValue().toString();
                if (selId == destTrack)
                {
                    auto* track = appCore_.getTrackManager().getTrack(destTrack);
                    pluginSidePanel_->setTrack(track, dstChain);
                }
            }
        }
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
        auto* srcChain = appCore_.getPluginChain(srcTrack);
        auto* dstChain = appCore_.getPluginChain(destTrack);
        if (srcChain && dstChain)
        {
            auto before = dstChain->getState();
            dstChain->copyPluginFrom(*srcChain, srcSlot, appCore_.getPluginScanner().getFormatManager());
            auto after = dstChain->getState();
            if (!before.isEquivalentTo(after))
                CommandManager::getInstance().execute(std::make_unique<PluginChainStateCommand>(
                    *dstChain, appCore_.getPluginScanner().getFormatManager(), before, after, "Copy Plugin", true));
            // If the dest track is currently shown in the side panel, refresh it
            if (pluginSidePanel_ && sidePanelChrome_ && sidePanelChrome_->isVisible())
            {
                auto selId = appCore_.getState().selectedTrackID.getValue().toString();
                if (selId == destTrack)
                {
                    auto* track = appCore_.getTrackManager().getTrack(destTrack);
                    pluginSidePanel_->setTrack(track, dstChain);
                }
            }
        }
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
    // NOTE: pluginSidePanel_ is parented inside sidePanelChrome_ (not directly on MainComponent)

    // Wrap side panel in floating chrome (X / _ / ? + drag)
    sidePanelChrome_ = std::make_unique<DAW::FloatingPanelChrome>("FX Chain", pluginSidePanel_.get());
    sidePanelChrome_->onClosed = [this]
    {
        DBG("[MainComponent] Side panel close pressed");
        if (bubbleTaskbar_) bubbleTaskbar_->unregisterWindow("sidepanel");
        resized();
    };
    sidePanelChrome_->onMinimized = [this]
    {
        DBG("[MainComponent] Side panel minimize pressed -> bubble");
        if (bubbleTaskbar_)
        {
            bubbleTaskbar_->registerWindow("sidepanel", "FX Chain", "Panels", [this]
            {
                if (sidePanelChrome_)
                {
                    sidePanelChrome_->restorePanel();
                    if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized("sidepanel", false);
                    DBG("[MainComponent] Side panel restored from bubble");
                    resized();
                }
            },
            [this]
            {
                // Direct close from bubble X
                if (sidePanelChrome_) sidePanelChrome_->closePanel();
                DBG("[MainComponent] Side panel closed from bubble X");
            });
            bubbleTaskbar_->setWindowMinimized("sidepanel", true);
            DBG("[MainComponent] Minimized bubble entry created for side panel");
        }
        resized(); // Collapse layout space immediately
    };
    sidePanelChrome_->onMaximized = [this]
    {
        DBG("[MainComponent] Side panel maximized");
        resized();
    };
    sidePanelChrome_->onRestored = [this]
    {
        DBG("[MainComponent] Side panel restored state=Open");
        resized();
    };
    sidePanelChrome_->onMoved = [this]
    {
        DBG("[MainComponent] Side panel moved to " + sidePanelChrome_->getBounds().toString());
    };
    addChildComponent(sidePanelChrome_.get());

    // Plugin browser panel - categorized FX explorer
    pluginBrowser_ = std::make_unique<DAW::PluginBrowserPanel>(appCore_.getPluginScanner());
    pluginBrowser_->onPluginSelected = [this](const juce::PluginDescription& desc)
    {
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
                + " selectedTrack=\"" + selectedId + "\"");

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
        auto slotIndex = chain->appendPlugin(desc, appCore_.getPluginScanner().getFormatManager(), err);
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
                *chain, appCore_.getPluginScanner().getFormatManager(), before, after, "Add Plugin", true));

        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::pluginLoadSuccess"
                " plugin=\"" + desc.name + "\""
                + " selectedTrack=\"" + selectedId + "\""
                + " slotIndex=" + juce::String(slotIndex)
                + " chainSlotCount=" + juce::String(chain->getNumSlots()));

        if (pluginSidePanel_)
        {
            auto* track = appCore_.getTrackManager().getTrack(selectedId);
            if (track) pluginSidePanel_->setTrack(track, chain);
        }
        if (sidePanelChrome_ && !sidePanelChrome_->isVisible())
        {
            sidePanelChrome_->restorePanel();
            DBG("[MainComponent] Side panel auto-shown after plugin add from browser");
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
            auto bubbleSubtitle = "Track FX - Track: " + trackLabel + "\nBrowser Insert";

            slot->onEditorMinimized = [this, windowId, selectedId, slotIndex, slot]
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
                selectedId, slot->getName(), "Track FX - Track: " + trackLabel + "\nBrowser Insert");
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
        if (!sidePanelChrome_) return;
        auto stateBefore = sidePanelChrome_->getState();
        DBG("[MainComponent] Quick access Side Panel toggle, stateBefore=" + juce::String((int)stateBefore));
        if (sidePanelChrome_->isVisible())
        {
            // Panel is open ? close it (toggle off)
            sidePanelChrome_->closePanel();
            DBG("[MainComponent] Side panel closed via quick access toggle");
        }
        else
        {
            // Panel is closed/minimized ? open it
            sidePanelChrome_->restorePanel();
            if (bubbleTaskbar_) bubbleTaskbar_->setWindowMinimized("sidepanel", false);
            DBG("[MainComponent] Side panel opened via quick access toggle, stateAfter=" + juce::String((int)sidePanelChrome_->getState()));
        }
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
        if (sidePanelChrome_) sidePanelChrome_->closePanel();
        if (browserChrome_)   browserChrome_->closePanel();
        resized();
        DAW::PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "MainComponent::scanComplete"
                " finalCacheEntryCount=" + juce::String(appCore_.getPluginScanner().getCache().getCount())
                + " finalKnownPluginCount=" + juce::String(appCore_.getPluginScanner().getKnownPlugins().getNumTypes())
                + " browserItemCount=" + juce::String(pluginBrowser_ ? pluginBrowser_->getVisiblePluginItemCount() : 0)
                + " sidePanelVisible=" + juce::String(pluginSidePanel_ && pluginSidePanel_->isVisible() ? 1 : 0)
                + " browserVisible=" + juce::String(pluginBrowser_ && pluginBrowser_->isVisible() ? 1 : 0));
    };
    addChildComponent(scanDialog_.get());

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
    if (sidePanelChrome_) sidePanelChrome_->closePanel();
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
        resized();
    };
    mixerWindow_->onRestored = [this]
    {
        mixerBubble_->setVisible(false);
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
        cableOverlay_.repaint();
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
        appCore_.getProjectManager().newProject();
        appCore_.clearAllPluginChains();
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
                    if (file.existsAsFile() && self->appCore_.getProjectManager().loadFromFile(file))
                    {
                        self->resized();
                        if (self->bubblegumPanel_ && self->bubblegumPanel_->isVisible()) self->bubblegumPanel_->refresh();
                        self->refreshBubblegumOffscreenState();
                        self->cableOverlay_.repaint();
                    }
    appendMainStartupTrace("ctor.complete");
                }
            });
    });

    regProjectSave_ = am.scoped(DAW::ActionID::ProjectSave, [this]
    {
        if (appCore_.getProjectManager().save())
            return;
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
                    self->appCore_.getProjectManager().saveToFile(file);
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
                    auto sr = self->appCore_.getCurrentSampleRate() > 0.0
                                  ? self->appCore_.getCurrentSampleRate() : 44100.0;
                    auto insertPos = (DAW::SamplePosition) std::round(
                        self->appCore_.getTransport().getPosition() * sr);
                    DAW::CommandManager::getInstance().execute(
                        std::make_unique<DAW::ImportAudioFilesCommand>(
                            self->appCore_.getTrackManager(),
                            self->appCore_.getClipManager(),
                            self->appCore_.getAudioFileManager(),
                            files,
                            selectedTrackId,
                            insertPos,
                            sr));
                    self->resized();
                    if (self->bubblegumPanel_ && self->bubblegumPanel_->isVisible()) self->bubblegumPanel_->refresh();
                    self->refreshBubblegumOffscreenState();
                    self->cableOverlay_.repaint();
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
        DAW::CommandManager::getInstance().execute(
            std::make_unique<DAW::AddTrackCommand>(tm, "MIDI " + juce::String(tm.getNumTracks() + 1), DAW::TrackRole::MIDI));
        resized();
        if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
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
            std::vector<juce::String> toDelete;
            for (const auto& t : multiSelection_.getSelected(DAW::SelectionKind::Track))
                if (auto* track = tm.getTrack(t.trackId))
                    if (!track->isMaster())
                        toDelete.push_back(t.trackId);
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
                    for (const auto& id : toDelete) safeThis->deleteTrackOrFolder(id);
                    safeThis->multiSelection_.clearKind(DAW::SelectionKind::Track);
                    if (safeThis->trackList_) safeThis->trackList_->applyMultiSelectionVisual({});
                });
            }
            return;
        }
        auto selId = appCore_.getState().selectedTrackID.getValue().toString();
        if (selId.isNotEmpty())
            deleteTrackOrFolder(selId);
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

    regTrackRenameSelected_ = am.scoped(DAW::ActionID::TrackRenameSelected, [this]
    {
        juce::ignoreUnused(this); // rename triggered via track list row double-click
    });

    regViewMixer_ = am.scoped(DAW::ActionID::ViewToggleMixer, [this]
    {
        if (mixerDocked_)
        {
            mixerViewport_.setVisible(!mixerViewport_.isVisible());
        }
        else
        {
            if (mixerWindow_->isVisible()) { mixerWindow_->setVisible(false); mixerBubble_->setVisible(true); }
            else { mixerWindow_->setVisible(true); mixerWindow_->restoreIfMinimized(); mixerWindow_->toFront(false); mixerBubble_->setVisible(false); }
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
    // Save collapsed folder state to project file by augmenting the current state
    auto& pm = appCore_.getProjectManager();

    // Only save if we have a valid project file
    if (pm.getProjectFile() == juce::File())
        return;

    auto state = pm.buildState();

    // Add collapsed folder IDs to the state
    juce::ValueTree collapsedFolders("CollapsedFolders");
    for (const auto& folderId : collapsedFolderTrackIds_)
    {
        juce::ValueTree folderNode("Folder");
        folderNode.setProperty("trackId", folderId, nullptr);
        collapsedFolders.addChild(folderNode, -1, nullptr);
    }
    state.addChild(collapsedFolders, -1, nullptr);

    // Write the augmented state back to the project file
    auto xml = state.createXml();
    if (xml)
        xml->writeTo(pm.getProjectFile());
}

void MainComponent::projectLoaded()
{
    // A loaded/new project invalidates every recorded command - the old
    // entries reference track/clip IDs that no longer exist. Every major
    // DAW starts a fresh history per project; replaying a stale command
    // against dead IDs would silently corrupt the new session.
    DAW::CommandManager::getInstance().clearHistory();

    // Restore collapsed folder state from project file
    auto& pm = appCore_.getProjectManager();

    // Only restore if we have a valid project file
    if (pm.getProjectFile() == juce::File())
        return;

    auto xml = juce::XmlDocument::parse(pm.getProjectFile());
    if (!xml) return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;

    auto collapsedFolders = state.getChildWithName("CollapsedFolders");
    if (collapsedFolders.isValid())
    {
        collapsedFolderTrackIds_.clear();
        for (int i = 0; i < collapsedFolders.getNumChildren(); ++i)
        {
            auto folderNode = collapsedFolders.getChild(i);
            if (folderNode.hasType("Folder"))
            {
                auto folderId = folderNode.getProperty("trackId").toString();
                if (folderId.isNotEmpty())
                    collapsedFolderTrackIds_.insert(folderId);
            }
        }

        // Sync the restored state to all UI components
        syncFolderCollapseState();
    }

    // Reload audio buffers for every AudioClip from their source files.
    // Without this, restored clips have no waveform data and play silence.
    appCore_.reloadAudioFiles();

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
    auto& tm = appCore_.getTrackManager();
    if (tm.getNumTracks() <= 0)
        return;

    targetIndex = juce::jlimit(0, tm.getNumTracks() - 1, targetIndex);

    const bool topologyMutation = shouldDetachTrackFromOpenFolder(trackId, targetIndex);
    if (!topologyMutation)
    {
        tm.moveTrack(trackId, targetIndex);
        return;
    }

    executeTopologyCommand([&]()
    {
        if (auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute())
            appCore_.getFolderBus().removeChildFromFolderBus(trackId,
                                                             appCore_.getRoutingGraph(),
                                                             *masterRoute,
                                                             &tm);
        tm.moveTrack(trackId, targetIndex);
    }, "Detach Track From Folder");
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
}

void MainComponent::handleFolderDropRequest(const DAW::TrackID& draggedId, const DAW::TrackID& targetId)
{
    // Master bus never participates in folder stacks (Logic/Reaper convention)
    auto& tm = appCore_.getTrackManager();
    if (auto* dragged = tm.getTrack(draggedId); dragged && dragged->isMaster()) return;
    if (auto* target = tm.getTrack(targetId); target && target->isMaster()) return;

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
    if (!folderBus.isFolderBus(folderBusId))
        return false;

    if (folderBus.getParentFolderBus(draggedId) == folderBusId)
        return true;

    executeTopologyCommand([&]()
    {
        folderBus.addChildToFolderBus(
            folderBusId,
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

bool MainComponent::deleteTrackOrFolder(const DAW::TrackID& trackId)
{
    auto& fb = appCore_.getFolderBus();
    auto& tm = appCore_.getTrackManager();
    auto* masterRoute = appCore_.getBubblegumV2().getMasterRoute();

    if (fb.isFolderBus(trackId))
    {
        executeTopologyCommand([&]()
        {
            fb.dissolveFolderBus(trackId, appCore_.getRoutingGraph(), *masterRoute, tm);
        }, "Dissolve Folder Bus");
        collapsedFolderTrackIds_.erase(trackId);
        syncFolderCollapseState();
        return true;
    }
    else
    {
        return tm.deleteTrack(trackId);
    }
}

void MainComponent::refreshBubblegumFeedback()
{
    if (mixerPanel_)
        mixerPanel_->repaint();

    // If sourceSync has no source (e.g. send created from full matrix without
    // a selected track), fall back to the mixer's selected track so the overlay
    // paint() doesn't bail early at sourceId.isEmpty().
    auto& bgV2 = appCore_.getBubblegumV2();
    if (bgV2.sourceSync.getSourceTrackId().isEmpty() && mixerPanel_)
    {
        const auto selId = mixerPanel_->getSelectedTrackId();
        if (selId.isNotEmpty())
            bgV2.sourceSync.sync(selId);
    }

    // Invalidate the cable overlay snapshot cache so send cables
    // appear immediately without requiring a mixer click.
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

    // Offscreen bubbles are shown when:
    //  - Mixer is visible AND
    //  - Bubblegum routing is active OR offscreen force-visible is on
    const bool offscreenActive = mixerVisible
        && (appCore_.getBubblegumV2().isActive() || mixerPanel_->isOffscreenForceVisible());

    const auto mainBounds = getLocalBounds();

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

    juce::Rectangle<int> mixerBounds;
    juce::Rectangle<int> viewportBounds;
    if (mixerDocked_)
    {
        mixerBounds = mixerViewport_.getBounds();
        viewportBounds = mixerViewport_.getBounds();
    }
    else if (mixerWindow_)
    {
        // mixerWindow_->getBounds() is in screen coordinates; convert to MainComponent local space.
        auto screenBounds = mixerWindow_->getBounds();
        mixerBounds = juce::Rectangle<int>(
            getLocalPoint(nullptr, screenBounds.getTopLeft()),
            getLocalPoint(nullptr, screenBounds.getBottomRight()));
        viewportBounds = getLocalArea(&mixerViewport_, mixerViewport_.getLocalBounds());
    }

    offscreenEndpoint_.setMixerBoundsInLocal(mixerBounds.toFloat());

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

    const auto viewportPos = mixerViewport_.getViewPositionX();
    int stripX = 0;
    int trackNumber = 1;

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
        const bool isMaster = track->isMaster();
        if (isMaster)
            stripX += 10;

        const int stripWidth = isMaster ? DAW::MixerPanel::kStripW + 10
                                        : DAW::MixerPanel::kStripW;
        const float stripCentreX = (float)stripX + (float)stripWidth * 0.5f;

        const bool hasSend      = trackHasSend(trackId);
        const bool hasSidechain = trackHasSidechain(trackId);
        if (trackId != effectiveSource && (hasSend || hasSidechain))
        {
            targetPositions[trackId] = (float)viewportBounds.getX() + stripCentreX - (float)viewportPos;
            targetNames[trackId]     = track->getName();
            targetNumbers[trackId]   = trackNumber;
            sendLevels[trackId]      = hasSend ? trackSendLevel(trackId) : 0.0f;
            sendActiveStates[trackId]     = hasSend      ? trackSendActive(trackId)     : false;
            sidechainActiveStates[trackId]= hasSidechain ? trackSidechainActive(trackId): false;
            if (hasSend)      sendIds.insert(trackId);
            if (hasSidechain) sidechainIds.insert(trackId);
        }

        stripX += stripWidth + DAW::MixerPanel::kStripGap;
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

void MainComponent::connectionAdded(DAW::RoutingConnection* conn)
{
    appCore_.markProjectDirty("routing_changed");
    refreshBubblegumFeedback();
    refreshBubblegumOffscreenState();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    cableOverlay_.repaint();
}

void MainComponent::connectionRemoved(const juce::String& connId)
{
    appCore_.markProjectDirty("routing_changed");
    refreshBubblegumFeedback();
    refreshBubblegumOffscreenState();
    if (bubblegumPanel_ && bubblegumPanel_->isVisible()) bubblegumPanel_->refresh();
    cableOverlay_.repaint();
}

void MainComponent::trackAdded(DAW::Track*)
{
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

void MainComponent::trackRemoved(const DAW::TrackID&)
{
    appCore_.markProjectDirty("track_removed");

    auto& bgV2 = appCore_.getBubblegumV2();
    if (bgV2.isActive() && bgV2.trackManager)
        bgV2.targetList.rebuild(bgV2.sourceSync.getSourceTrackId(), *bgV2.trackManager);

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

    if (wasResized || wasMoved)
    {
        cableOverlay_.notifyMotion();
        refreshBubblegumOffscreenState();
        cableOverlay_.repaint();
    }
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
        getOwnedDeviceManager().setCurrentAudioDeviceType(safeType, true);
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
    if (getAudioCallbackRegisteredFlag())
    {
        dm.removeAudioCallback(this);
        getAudioCallbackRegisteredFlag() = false;
    }
    dm.closeAudioDevice();

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::Timer::callAfterDelay(400, [safeThis, setup]
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        if (self->audioSuspendedForDevicePanel_)
            return;

        auto& dm2 = self->getOwnedDeviceManager();
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
            audioDevicePanel_ = std::make_unique<DAW::AudioDeviceSettingsUI>(getOwnedDeviceManager());
            juce::Component::SafePointer<MainComponent> safeThis(this);
            audioDevicePanel_->onPanelClosed = [safeThis]
            {
                if (auto* self = safeThis.getComponent())
                    self->resumeAudioAfterDevicePanel();
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
            stagedDeviceSession_ = std::make_unique<DAW::DeviceSessionCore>(
                getOwnedDeviceManager(),
                [this]() { return appCore_.getTransport().isRecording(); });
        }

        if (!stagedDevicePanelModel_)
            stagedDevicePanelModel_ = std::make_unique<DAW::DevicePanelModelCore>(getOwnedDeviceManager());

        if (!stagedAudioDevicePanel_)
        {
            stagedAudioDevicePanel_ = std::make_unique<DAW::AudioDevicePanelUI>(
                *stagedDeviceSession_,
                *stagedDevicePanelModel_,
                getOwnedDeviceManager());

            juce::Component::SafePointer<MainComponent> safeThis(this);
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
    appCore_.getProjectManager().removeListener(this);
    appCore_.getRoutingGraph().removeListener(this);
    appCore_.getTrackManager().removeListener(this);
    appCore_.getAutosaveManager().onAutosaveSucceeded = {};
    appCore_.getAutosaveManager().onAutosaveFailed = {};

    // 2. Stop audio thread before destroying anything it touches
    shutdownAudio();

    // 3. Destroy all floating/plugin/editor windows explicitly before appCore
    clipRegionPluginWindows_.clear();
    createSequenceWindow_ = nullptr;
    forensicWindow_.reset();
    shortcutHelpWindow_.reset();
    scanDialog_.reset();
    browserChrome_.reset();
    sidePanelChrome_.reset();
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
    mixerPanel_.reset();
    arrangement_.reset();
    trackList_.reset();
    timelineViewport_.reset();
    transportBar_.reset();
    menuBar_.reset();

    // 4. Shut down application subsystems (releases plugin chains, etc.)
    appCore_.shutdown();

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
    appCore_.setHardwareInputChannelCount(cachedEnabledAudioInputChannels_.load(std::memory_order_acquire));
    appCore_.getNextAudioBlock(bufferToFill,
                               liveCallbackInputBuffer_.getNumChannels() > 0 ? &liveCallbackInputBuffer_ : nullptr,
                               liveCallbackInputBuffer_.getNumSamples());
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);

    audioCallbackTickCounter_.fetch_add(1, std::memory_order_relaxed);

    const int safeOutputChannels = juce::jmax(1, numOutputChannels);
    juce::AudioBuffer<float> outputBuffer(outputChannelData, safeOutputChannels, numSamples);
    juce::AudioSourceChannelInfo outputInfo(&outputBuffer, 0, numSamples);

    if (numInputChannels > 0 && numSamples > 0)
    {
        if (liveCallbackInputBuffer_.getNumChannels() < numInputChannels || liveCallbackInputBuffer_.getNumSamples() < numSamples)
            liveCallbackInputBuffer_.setSize(numInputChannels, numSamples, false, false, true);

        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            auto* dst = liveCallbackInputBuffer_.getWritePointer(ch);
            const auto* src = inputChannelData != nullptr ? inputChannelData[ch] : nullptr;

            if (src != nullptr)
                juce::FloatVectorOperations::copy(dst, src, numSamples);
            else
                juce::FloatVectorOperations::clear(dst, numSamples);
        }
    }
    else
    {
        liveCallbackInputBuffer_.setSize(0, 0);
    }

    cachedEnabledAudioInputChannels_.store(juce::jmax(0, numInputChannels), std::memory_order_release);
    getNextAudioBlock(outputInfo);
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const int activeInputs = device != nullptr ? juce::jmax(0, device->getActiveInputChannels().countNumberOfSetBits()) : 0;
    const int currentBlockSize = device != nullptr ? juce::jmax(1, device->getCurrentBufferSizeSamples()) : 1;

    liveCallbackInputBuffer_.setSize(juce::jmax(1, activeInputs), juce::jmax(currentBlockSize, 8192), false, false, true);
    liveCallbackInputBuffer_.clear();
    cachedEnabledAudioInputChannels_.store(activeInputs, std::memory_order_release);
    prepareToPlay(currentBlockSize, device != nullptr ? device->getCurrentSampleRate() : 44100.0);
}

void MainComponent::audioDeviceStopped()
{
    releaseResources();
    liveCallbackInputBuffer_.setSize(0, 0);
    cachedEnabledAudioInputChannels_.store(0, std::memory_order_release);
}

void MainComponent::setAudioChannels(int numInputChannels, int numOutputChannels)
{
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
    if (getAudioCallbackRegisteredFlag())
    {
        getOwnedDeviceManager().removeAudioCallback(this);
        getAudioCallbackRegisteredFlag() = false;
    }

    getOwnedDeviceManager().closeAudioDevice();
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

void MainComponent::inputWatchdogTick()
{
    // Never touch the device while the device panel owns it or while a take
    // is rolling - a device restart mid-take would destroy the recording.
    if (audioSuspendedForDevicePanel_)
        return;
    if (appCore_.getTransport().isRecording() || appCore_.getRecordingEngine().isRecording())
        return;

    auto* dev = getOwnedDeviceManager().getCurrentAudioDevice();
    if (dev == nullptr)
        return;

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
    g.fillAll(DAW::Theme::getInstance().colors.background);

    // Outer frame border (whole DAW edge)
    {
        auto b = getLocalBounds().toFloat();

        // Main frame (thick)
        g.setColour(juce::Colour(0xFF050407).withAlpha(0.95f));
        g.drawRect(b, 8.0f);

        // Inner highlight stroke to keep it premium instead of just chunky
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawRect(b.reduced(6.0f), 1.0f);
    }

    // ?? Recovery banner ????????????????????????????????????????????????????????
    if (recoveryBannerVisible_)
    {
        auto bannerBounds = juce::Rectangle<int>(0, kMenuH + kTransportH, getWidth(), 22);
        g.setColour(juce::Colour(0xFFBF5500).withAlpha(0.88f));
        g.fillRect(bannerBounds);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("  Recovered autosave - Save As recommended",
                   bannerBounds.reduced(4, 0),
                   juce::Justification::centredLeft, true);
    }

    // ?? Autosave status text (bottom-right corner, subtle) ????????????????????
    if (autosaveStatusText_.isNotEmpty())
    {
        g.setColour(juce::Colour(0xFF888888));
        g.setFont(juce::Font(10.5f));
        g.drawText(autosaveStatusText_,
                   getLocalBounds().reduced(8, 4),
                   juce::Justification::bottomRight, true);
    }

    // Track list / timeline separator line (only when track list is visible)
    if (trackListWindow_ && trackListWindow_->isVisible() && trackListW_ > 0)
    {
        const int x = trackListW_;
        const float top = (float)(kMenuH);
        g.setColour(juce::Colour(0xFF0B0A0D).withAlpha(0.9f));
        g.fillRect((float)x - 2.0f, top, 4.0f, (float)getHeight() - top);
        g.setColour(juce::Colour(0xFFFF7ACF).withAlpha(0.65f));
        g.drawLine((float)x, top, (float)x, (float)getHeight(), 2.0f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    if (scanDialog_)
        scanDialog_->setBounds(area);

    if (menuBar_)
        menuBar_->setBounds(area.removeFromTop(kMenuH));

    // Transport moved to bottom-left (user request)
    auto transportArea = area.removeFromBottom(kTransportH);
    if (transportBar_)
    {
        const int transportW = juce::jmin(520, juce::jmax(320, (int)std::round(getWidth() * 0.42)));
        transportBar_->setBounds(transportArea.removeFromLeft(transportW));
    }

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
        const int numTracks = appCore_.getTrackManager().getNumTracks() + (appCore_.getTrackManager().hasMasterTrack() ? 1 : 0);
        mixerPanel_->setSize(juce::jmax(mixerArea.getWidth(), numTracks * (DAW::MixerPanel::kStripW + DAW::MixerPanel::kStripGap) + 90),
                             mixerArea.getHeight());
    }

    if (trackListWindow_ && trackListWindow_->isVisible() && trackListW_ > 0 && !trackListWindow_->isMaximized())
        trackListWindow_->setBounds(content.removeFromLeft(trackListW_));

    if (timelineWindow_)
        timelineWindow_->setBounds(content);

    updateTimelineViewportContentBounds();

    if (mixerWindow_ && !mixerDocked_)
    {
        if (mixerWindow_->getBounds().isEmpty())
            mixerWindow_->setBounds(80, juce::jmax(kMenuH + kTransportH + 20, getHeight() - 380), juce::jmax(760, getWidth() - 160), 320);
    }

    if (pianoRollWindow_ && pianoRollWindow_->getBounds().isEmpty())
        pianoRollWindow_->setBounds(getLocalBounds().reduced(90, 70));

    if (sidePanelChrome_ && sidePanelChrome_->isVisible() && !sidePanelChrome_->isMaximized())
        sidePanelChrome_->setBounds(getWidth() - 360, kMenuH + kTransportH + 18, 340, juce::jmin(560, getHeight() - 150));
    if (browserChrome_ && browserChrome_->isVisible() && !browserChrome_->isMaximized())
        browserChrome_->setBounds(24, kMenuH + kTransportH + 18, 340, juce::jmin(560, getHeight() - 150));

    if (settingsPanel_ && settingsPanel_->isVisible())
        settingsPanel_->setBounds(getLocalBounds().reduced(120, 70));

    if (quitSafetyDialog_)
        quitSafetyDialog_->setBounds(getLocalBounds());

    cableOverlay_.setBounds(getLocalBounds());
    offscreenEndpoint_.setBounds(getLocalBounds());
   #if JUCE_DEBUG
    fpsOverlay_.setBounds(getWidth() - 70, 6, 60, 20);
   #endif

    refreshBubblegumOffscreenState();
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

    // Content width is purely zoom * project length.
    // Do NOT clamp to viewportW - that would prevent the thumb from growing on zoom-out.
    const int contentW = juce::jmax(4, (int)std::ceil(zoomPxPerSecond * projectLengthSeconds));

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
                    for (const auto& id : toDelete)
                        safeThis->deleteTrackOrFolder(id);
                    safeThis->multiSelection_.clearKind(DAW::SelectionKind::Track);
                    if (safeThis->trackList_)
                        safeThis->trackList_->applyMultiSelectionVisual({});
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

    return DAW::KeyBindingManager::getInstance().handleKeyPress(key);
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
    if (!pianoRollWindow_)
        return;

    pianoRollWindow_->setMidiClip(&clip);
    pianoRollWindow_->setVisible(true);
    if (pianoRollWindow_->getBounds().isEmpty())
        pianoRollWindow_->setBounds(getLocalBounds().reduced(90, 70));
    pianoRollWindow_->toFront(true);
}

void MainComponent::showExportWavDialog()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export WAV",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.wav");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            auto result = fc.getResult();
            if (result == juce::File{}) return;

            DAW::ExportSettings settings;
            settings.outputFile  = result;
            settings.bitDepth    = 24;
            settings.tailSeconds = 4.0;

            if (self->exportRenderCore_ && self->exportRenderCore_->isExportRunning())
                return;

            self->exportRenderCore_ = std::make_unique<DAW::ExportRenderCore>(self->appCore_, settings);

            // Create the progress window (self-owned; deletes itself on finish).
            auto* progressWindow = new DAW::ExportProgressWindow(
                *self->exportRenderCore_, result, self);

            self->exportRenderCore_->startExport(
                // ProgressCallback - called on message thread via callAsync
                [progressWindow](float p)
                {
                    progressWindow->setProgress(p);
                },
                // CompletionCallback - called on message thread via callAsync
                [progressWindow](DAW::ExportRenderCore::Result r)
                {
                    progressWindow->exportFinished(r);
                });
        });
}

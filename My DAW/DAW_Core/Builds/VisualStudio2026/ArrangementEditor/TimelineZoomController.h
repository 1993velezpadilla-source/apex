#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace ArrangementEditor
{
// Coordinates are viewport-local logical pixels. Time and lane position are
// the anchors. Component sizes and scrollbar positions are projections.
class TimelineZoomController final
{
public:
    struct Point { double x = 0.0, y = 0.0; };
    struct Scale { double pixelsPerSecond = 100.0, laneHeight = 80.0; };
    struct Lanes
    {
        int headerHeight = 80;
        int trackHeight = 80;
        int expandedLane = -1;
        int takePixels = 0;
    };

    struct Anchor
    {
        double seconds = 0.0;
        double row = 0.0;
        enum class Band { header, tracks, takes } band = Band::tracks;
    };

    struct Request
    {
        Scale scale;
        Anchor anchor;
        Point focus;
        bool projectX = false;
        bool projectY = false;
    };

    static constexpr double minPixelsPerSecond = 2.0;
    static constexpr double maxPixelsPerSecond = 16'000.0;
    static constexpr int minTrackHeight = 24;
    static constexpr int maxTrackHeight = 360;
    static constexpr int maxContentWidth = std::numeric_limits<int>::max() / 2;

    Scale scale() const noexcept { return scale_; }
    double getPixelsPerSecond() const noexcept { return scale_.pixelsPerSecond; }
    int getTrackHeightPx() const noexcept { return displayHeight(scale_.laneHeight); }
    double timeToX(double seconds) const noexcept { return seconds * scale_.pixelsPerSecond; }
    double xToTime(double x) const noexcept { return x / scale_.pixelsPerSecond; }

    int timeToPixel(double seconds) const noexcept
    {
        const auto x = timeToX(seconds);
        return (int)std::clamp(std::isnan(x) ? 0.0 : x,
                              -(double)maxContentWidth, (double)maxContentWidth);
    }

    void apply(Scale next) noexcept { scale_ = clamp(next, scale_); }

    static Scale clamp(Scale next, Scale current) noexcept
    {
        if (std::isfinite(next.pixelsPerSecond))
            current.pixelsPerSecond = std::clamp(next.pixelsPerSecond, minPixelsPerSecond, maxPixelsPerSecond);
        if (std::isfinite(next.laneHeight))
            current.laneHeight = std::clamp(next.laneHeight, (double)minTrackHeight, (double)maxTrackHeight);
        return current;
    }

    static int displayHeight(double height) noexcept
    {
        return (int)std::lround(std::clamp(height, (double)minTrackHeight, (double)maxTrackHeight));
    }

    static Anchor capture(Scale scale, Point origin, Point focus, Lanes lanes) noexcept
    {
        Anchor anchor;
        anchor.seconds = (origin.x + focus.x) / scale.pixelsPerSecond;
        const double y = origin.y + focus.y;
        if (y < lanes.headerHeight)
        {
            anchor.band = Anchor::Band::header;
            anchor.row = y;
            return anchor;
        }

        double body = y - lanes.headerHeight;
        const double takeStart = (lanes.expandedLane + 1.0) * lanes.trackHeight;
        if (lanes.expandedLane >= 0 && lanes.takePixels > 0 && body >= takeStart)
        {
            if (body < takeStart + lanes.takePixels)
            {
                anchor.band = Anchor::Band::takes;
                anchor.row = (body - takeStart) / lanes.takePixels;
                return anchor;
            }
            body -= lanes.takePixels;
        }
        anchor.row = body / lanes.trackHeight;
        return anchor;
    }

    static double projectY(Anchor anchor, Lanes lanes) noexcept
    {
        if (anchor.band == Anchor::Band::header)
            return anchor.row;
        const double afterExpandedLane = lanes.expandedLane + 1.0;
        if (anchor.band == Anchor::Band::takes)
            return lanes.headerHeight + afterExpandedLane * lanes.trackHeight
                 + anchor.row * lanes.takePixels;
        return lanes.headerHeight + anchor.row * lanes.trackHeight
             + (lanes.expandedLane >= 0 && anchor.row >= afterExpandedLane ? lanes.takePixels : 0);
    }

    static Point projectOrigin(const Request& request, Lanes lanes, Point unchanged, Point scrollLimit) noexcept
    {
        if (request.projectX)
            unchanged.x = request.anchor.seconds * request.scale.pixelsPerSecond - request.focus.x;
        if (request.projectY)
            unchanged.y = projectY(request.anchor, lanes) - request.focus.y;
        return { std::clamp(unchanged.x, 0.0, std::max(0.0, scrollLimit.x)),
                 std::clamp(unchanged.y, 0.0, std::max(0.0, scrollLimit.y)) };
    }

    // JUCE scrollbars store integer positions. Keep the exact position until
    // a real scroll changes that axis, so repeated zoom cycles don't drift.
    Point origin(Point actual) const noexcept
    {
        return { actual.x == displayed_.x ? exact_.x : actual.x,
                 actual.y == displayed_.y ? exact_.y : actual.y };
    }

    void remember(Point exact, Point projected, Point actual) noexcept
    {
        displayed_ = actual;
        exact_ = { projected.x == actual.x ? exact.x : actual.x,
                   projected.y == actual.y ? exact.y : actual.y };
    }

    void observeScroll(Point actual) noexcept
    {
        exact_ = origin(actual);
        displayed_ = actual;
    }

    static double wheelFactor(double delta) noexcept
    {
        return std::isfinite(delta) ? std::pow(1.2, 2.0 * std::clamp(delta, -20.0, 20.0)) : 1.0;
    }

    static int contentWidth(double seconds, double pixelsPerSecond, int viewportWidth) noexcept
    {
        const int minimum = std::clamp(viewportWidth, 0, maxContentWidth - 1) + 1;
        const double width = std::ceil(seconds * pixelsPerSecond);
        return (int)std::clamp(std::isnan(width) ? 0.0 : width, (double)minimum, (double)maxContentWidth);
    }

    // Keep a continuous time surface ahead of the playhead during playback
    // and REC. The viewport paints only its visible area.
    static constexpr double minimumTimelineSeconds = 600.0;
    static constexpr double timelineGrowthWarningSeconds = 120.0;

    static double timelineExtentSeconds(double projectEnd, double playhead) noexcept
    {
        const auto extent = std::max(minimumTimelineSeconds, std::isfinite(projectEnd) ? projectEnd : 0.0);
        if (!std::isfinite(playhead) || playhead < 0.0
            || playhead + timelineGrowthWarningSeconds <= extent)
            return extent;
        return std::max(extent, playhead + minimumTimelineSeconds);
    }

    class Pinch final
    {
    public:
        void begin(Point centre, Point distance) noexcept
        {
            previousCentre = centre;
            initialDistance = lastDistance = distance;
            axis = Axis::undecided;
        }

        Request move(const TimelineZoomController& camera, Point actualOrigin, Lanes lanes,
                     Point centre, Point distance, Scale requested) noexcept
        {
            if (axis == Axis::undecided)
            {
                const double x = travel(initialDistance.x, distance.x);
                const double y = travel(initialDistance.y, distance.y);
                if (std::max(x, y) >= 3.0)
                    axis = x >= 1.5 * y ? Axis::horizontal : y >= 1.5 * x ? Axis::vertical : Axis::both;
            }

            auto next = requested;
            if (axis == Axis::horizontal || axis == Axis::both)
                next.pixelsPerSecond *= ratio(lastDistance.x, distance.x);
            if (axis == Axis::vertical || axis == Axis::both)
                next.laneHeight *= ratio(lastDistance.y, distance.y);

            next = clamp(next, requested);
            Request request{ next, capture(camera.scale(), camera.origin(actualOrigin), previousCentre, lanes),
                             centre, next.pixelsPerSecond != requested.pixelsPerSecond || centre.x != previousCentre.x,
                             displayHeight(next.laneHeight) != displayHeight(requested.laneHeight)
                                 || centre.y != previousCentre.y };
            previousCentre = centre;
            return request;
        }

    private:
        enum class Axis { undecided, horizontal, vertical, both };
        static double travel(double first, double current) noexcept
        {
            return std::min(std::abs(first), std::abs(current)) >= 12.0
                 ? std::abs(current - first) : 0.0;
        }

        static double ratio(double& previous, double current) noexcept
        {
            const double first = previous;
            previous = current;
            // Crossing two contacts on one axis is a new baseline for that axis.
            if (first * current <= 0.0 || std::min(std::abs(first), std::abs(current)) < 12.0)
                return 1.0;
            return std::abs(current / first);
        }

        Axis axis = Axis::undecided;
        Point previousCentre, initialDistance, lastDistance;
    };

private:
    Scale scale_;
    Point displayed_, exact_;
};
}

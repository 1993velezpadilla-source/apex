#pragma once
#include <cmath>

namespace DAW
{
// JUCE-free, deterministic hit test used by mouse, touch, stylus and tests.
// Wedges start at 12 o'clock and proceed clockwise.
struct ApexRadialGeometry
{
    static int sectorAt(double x, double y, double cx, double cy,
                        double innerRadius, double outerRadius, int sectors) noexcept
    {
        if (sectors <= 0 || innerRadius < 0 || outerRadius <= innerRadius)
            return -1;
        const double dx = x - cx, dy = y - cy;
        const double dist2 = dx * dx + dy * dy;
        if (dist2 < innerRadius * innerRadius || dist2 > outerRadius * outerRadius)
            return -1;
        constexpr double pi = 3.1415926535897932384626433832795;
        double angle = std::atan2(dy, dx) + pi / 2.0;
        while (angle < 0.0) angle += 2.0 * pi;
        while (angle >= 2.0 * pi) angle -= 2.0 * pi;
        const int result = static_cast<int>(angle * sectors / (2.0 * pi));
        return result < sectors ? result : sectors - 1;
    }
    static bool isCentre(double x, double y, double cx, double cy,
                         double innerRadius) noexcept
    {
        const double dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= innerRadius * innerRadius;
    }
};
} // namespace DAW

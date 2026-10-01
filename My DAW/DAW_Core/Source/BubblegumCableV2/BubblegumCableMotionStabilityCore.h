#pragma once
#include <JuceHeader.h>
#include <limits>

namespace DAW::BgV2 {

class BubblegumCableMotionStabilityCore {
public:
    void update(int viewportScrollX, int viewportScrollY,
                int mixerScreenX,    int mixerScreenY,
                double nowSec) noexcept {
        bool changed =
            viewportScrollX != lastVpX_ || viewportScrollY != lastVpY_ ||
            mixerScreenX    != lastMxX_ || mixerScreenY    != lastMxY_;
        lastVpX_ = viewportScrollX; lastVpY_ = viewportScrollY;
        lastMxX_ = mixerScreenX;    lastMxY_ = mixerScreenY;
        if (changed) freezeUntilSec_ = nowSec + kFreezeTailSec;
    }

    bool isMoving(double nowSec) const noexcept { return nowSec < freezeUntilSec_; }

    void forceFreeze(double nowSec, double durationSec = kFreezeTailSec) noexcept {
        freezeUntilSec_ = nowSec + durationSec;
    }

private:
    static constexpr double kFreezeTailSec = 0.22;
    int lastVpX_ = -1, lastVpY_ = -1;
    int lastMxX_ = std::numeric_limits<int>::min();
    int lastMxY_ = std::numeric_limits<int>::min();
    double freezeUntilSec_ = 0.0;
};

} // namespace DAW::BgV2

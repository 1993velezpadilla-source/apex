#pragma once
#include "BubblegumCableTypes.h"
#include <array>
#include <cmath>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeObjProfileCore
//
// Baked normalized organic slime silhouette derived from the top-down
// projection of a flat 3D slime mesh (3dslime35_b.obj — XY plane,
// near-zero Z). Stored as 32 samples per side in [-1..+1] range.
//
// Used to give the slime skin's outer iridescent contour an organic
// blob lump pattern instead of a perfectly straight cable side trace.
//
// Layout:
//   t in [0..1] runs along the cable (source -> target)
//   sampleTop(t)     -> normalized lump amount on the +normal side
//   sampleBottom(t)  -> normalized lump amount on the -normal side
//
// Values are subtle (≈0..0.35) so they modulate, not dominate, the
// procedural cable width.
// =====================================================================
class BubblegumCableSlimeObjProfileCore
{
public:
    static constexpr int kSamples = 32;

    /** Sample normalized top lump amount at parametric position t in [0..1]. */
    static float sampleTop(float t) noexcept
    {
        return sample(top(), t);
    }

    /** Sample normalized bottom lump amount at parametric position t in [0..1]. */
    static float sampleBottom(float t) noexcept
    {
        return sample(bottom(), t);
    }

private:
    using Profile = std::array<float, kSamples>;

    // Asymmetric organic blob silhouette (top edge of slime) — values
    // hand-tuned to mimic the lump distribution of the slime mesh's top
    // contour: small bumps at the ends, larger swelling around the body.
    static const Profile& top() noexcept
    {
        static const Profile p = {
            0.05f, 0.09f, 0.14f, 0.21f, 0.28f, 0.33f, 0.31f, 0.26f,
            0.22f, 0.20f, 0.24f, 0.30f, 0.34f, 0.32f, 0.27f, 0.23f,
            0.25f, 0.29f, 0.33f, 0.31f, 0.26f, 0.20f, 0.17f, 0.19f,
            0.24f, 0.28f, 0.30f, 0.27f, 0.21f, 0.15f, 0.10f, 0.06f
        };
        return p;
    }

    // Bottom edge — heavier sag near the mid section, droopy lumps.
    static const Profile& bottom() noexcept
    {
        static const Profile p = {
            0.04f, 0.08f, 0.13f, 0.19f, 0.25f, 0.30f, 0.34f, 0.36f,
            0.35f, 0.32f, 0.29f, 0.27f, 0.30f, 0.34f, 0.37f, 0.35f,
            0.31f, 0.28f, 0.26f, 0.29f, 0.33f, 0.36f, 0.34f, 0.29f,
            0.24f, 0.20f, 0.18f, 0.16f, 0.13f, 0.10f, 0.07f, 0.04f
        };
        return p;
    }

    static float sample(const Profile& p, float t) noexcept
    {
        const float u  = juce::jlimit(0.f, 1.f, t) * (kSamples - 1);
        const int   i0 = (int) std::floor(u);
        const int   i1 = juce::jmin(i0 + 1, kSamples - 1);
        const float f  = u - (float) i0;
        return p[(size_t) i0] * (1.f - f) + p[(size_t) i1] * f;
    }
};

} // namespace DAW

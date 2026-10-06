#pragma once
#include "BubblegumCableTypes.h"
#include "BubblegumCableAnimationCore.h"
#include "BubblegumCableGravityCore.h"
#include <cmath>
#include <array>

namespace DAW {

// =====================================================================
// BubblegumCableSlimeDripCore — large attached slime drips.
//
// Generates 3-5 glossy slime drips hanging from the cable underside,
// like the reference photos (pink slime / viscous liquid drip look).
//
// Each drip is:
//   - A neck that widens from the cable underside
//   - An elongated rounded teardrop body
//   - A small detached bead at the tip (about to fall)
//
// All positions are CABLE-LOCAL (derived from t along cable centerline).
// No world-space coordinates are stored between frames.
// Scroll / resize NEVER cause stale visual state.
//
// Drip elongation animates slightly to look alive (like surface tension).
// =====================================================================
class BubblegumCableSlimeDripCore
{
public:
    static constexpr int kMaxDrips = 5;

    struct TipPos { float x = 0, y = 0; bool valid = false; };

    int  count() const noexcept { return count_; }

    TipPos tipPosition(const BubblegumCableGeometry& geo, int idx) const noexcept
    {
        if (idx < 0 || idx >= count_) return {};
        const auto& d = drips_[idx];
        const float fi = juce::jlimit(0.f, 1.f, d.t) * (float)BubblegumCableGeometry::kSeg;
        const int i0 = juce::jlimit(0, BubblegumCableGeometry::kSeg - 1, (int)std::floor(fi));
        const int i1 = juce::jlimit(0, BubblegumCableGeometry::kSeg, i0 + 1);
        const float a  = fi - (float)i0;
        const auto attach = BubblegumCableGravityCore::undersideAttachment(geo, d.t);
        const float dripH = d.bodyRadius * (2.2f + d.elongation * 0.8f);
        return { attach.x, attach.y + dripH, true };
    }

    struct DripDesc
    {
        float t            = 0.5f;   // cable-local t position
        float phase        = 0.0f;   // individual animation phase
        float sizeScale    = 1.0f;   // size multiplier
        float elongation   = 1.0f;   // current animated drip height
        float neckWidth    = 4.0f;   // px at cable halfW = 1
        float bodyRadius   = 9.0f;   // base body radius
        float beadRadius   = 3.5f;   // tip bead radius
    };

    void buildDrips(const BubblegumCableGeometry& geo,
                    const BubblegumCableInput&    in,
                    float                         time,
                    float                         energy)
    {
        const float seed  = BubblegumCableAnimationCore::cableSeed(in);
        const float e     = juce::jlimit(0.f, 1.f, energy);
        const float thick = geo.thickness;

        count_ = 3 + (int)(BubblegumCableAnimationCore::stable01(seed + 0.3f) * 3.f);
        count_ = juce::jlimit(3, kMaxDrips, count_);

        for (int i = 0; i < count_; ++i)
        {
            auto& d = drips_[i];
            const float si = seed + (float)i;

            // t spread: centre-weighted, 0.18..0.82
            d.t = juce::jlimit(0.18f, 0.82f,
                juce::jmap((float)(i + 1) / (float)(count_ + 1), 0.18f, 0.82f)
                + (BubblegumCableAnimationCore::stable01(si * 4.7f) - 0.5f) * 0.04f);

            d.phase    = BubblegumCableAnimationCore::stable01(si * 13.1f)
                       * juce::MathConstants<float>::twoPi;
            d.sizeScale = 0.7f + BubblegumCableAnimationCore::stable01(si * 8.3f) * 0.6f
                        + e * 0.3f;

            // Animated drip elongation — breathing / surface tension
            const float elongBase = 1.0f + e * 0.4f;
            const float elongWave = 0.18f * std::sin(time * (1.2f + 0.15f * (float)i) + d.phase);
            d.elongation = elongBase + elongWave;

            const float hw = thick * 0.5f;
            d.neckWidth  = juce::jmax(1.6f, hw * 0.22f * d.sizeScale);
            d.bodyRadius = juce::jmax(4.0f, hw * (0.55f + e * 0.18f) * d.sizeScale);
            d.beadRadius = juce::jmax(1.4f, d.bodyRadius * 0.32f);
        }
    }

    void paint(juce::Graphics& g,
               const BubblegumCableGeometry& geo) const
    {
        for (int i = 0; i < count_; ++i)
            paintDrip(g, geo, drips_[i]);
    }

private:
    int                          count_ = 0;
    std::array<DripDesc, kMaxDrips> drips_;

    // ── Palette ──────────────────────────────────────────────────────────
    const juce::Colour kDripTop  { 0xFFFF9AC6 };
    const juce::Colour kDripMid  { 0xFFFF6FB0 };
    const juce::Colour kDripBot  { 0xFFCC3380 };
    const juce::Colour kDripDeep { 0xFF8A1A44 };
    const juce::Colour kGloss    { 0xFFFFF0F6 };
    const juce::Colour kRim      { 0xFF6A1038 };

    void paintDrip(juce::Graphics& g,
                   const BubblegumCableGeometry& geo,
                   const DripDesc& d) const
    {
        // (sampling done via GravityCore above)

        // Attachment point — straight down from cable underside (gravity-aligned)
        const auto attach = BubblegumCableGravityCore::undersideAttachment(geo, d.t);
        const float ax = attach.x;
        const float ay = attach.y;

        // Gravity is always straight DOWN in screen space
        const float gravX = BubblegumCableGravityCore::kGravX;
        const float gravY = BubblegumCableGravityCore::kGravY;

        const float neckW = d.neckWidth;
        const float bodyR = d.bodyRadius;
        const float dripH = bodyR * (2.2f + d.elongation * 0.8f);
        const float beadR = d.beadRadius;

        // Horizontal (perpendicular to gravity in screen space)
        const float tgX = 1.f;   // always horizontal
        const float tgY = 0.f;

        // Tip centre (bottom of drip — straight down)
        const float tipX = ax + gravX * dripH;
        const float tipY = ay + gravY * dripH;

        // Build drip path
        juce::Path drip;
        drip.startNewSubPath(ax - tgX * neckW, ay);
        drip.cubicTo(ax - tgX * bodyR * 1.05f, ay + gravY * dripH * 0.1f,
                     ax - tgX * bodyR,          tipY - gravY * bodyR * 0.5f,
                     tipX,                       tipY);
        drip.cubicTo(ax + tgX * bodyR,          tipY - gravY * bodyR * 0.5f,
                     ax + tgX * bodyR * 1.05f,  ay + gravY * dripH * 0.1f,
                     ax + tgX * neckW,           ay);
        drip.closeSubPath();

        // ── Gradient fill ─────────────────────────────────────────────────
        const float topFillY = ay;
        const float botFillY = tipY + beadR;
        juce::ColourGradient grad(
            kDripTop.withAlpha(0.90f), ax, topFillY,
            kDripDeep.withAlpha(0.95f), tipX, botFillY,
            false);
        grad.addColour(0.30, kDripMid.withAlpha(0.92f));
        grad.addColour(0.65, kDripBot.withAlpha(0.94f));
        g.setGradientFill(grad);
        g.fillPath(drip);

        // ── Rim ───────────────────────────────────────────────────────────
        g.setColour(kRim.withAlpha(0.38f));
        g.strokePath(drip, juce::PathStrokeType(0.9f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ── Gloss highlight on the drip (left shoulder) ───────────────────
        const float gx = ax - tgX * bodyR * 0.42f + gravX * dripH * 0.25f;
        const float gy = ay - tgY * bodyR * 0.42f + gravY * dripH * 0.25f;
        juce::ColourGradient gloss(
            kGloss.withAlpha(0.55f), gx, gy,
            kGloss.withAlpha(0.0f),  gx + tgX * bodyR * 0.5f,
                                      gy + tgY * bodyR * 0.5f + gravY * dripH * 0.18f,
            false);
        g.setGradientFill(gloss);
        g.fillEllipse(gx - bodyR * 0.28f, gy - bodyR * 0.28f,
                      bodyR * 0.56f, bodyR * 0.80f);

        // ── Detached bead at the very tip ─────────────────────────────────
        const float bdX = tipX;
        const float bdY = tipY + gravY * (beadR * 1.6f);
        juce::ColourGradient beadGrad(
            kDripMid.withAlpha(0.88f), bdX, bdY - beadR * 0.3f,
            kDripDeep.withAlpha(0.92f), bdX, bdY + beadR, false);
        beadGrad.addColour(0.5, kDripBot.withAlpha(0.90f));
        g.setGradientFill(beadGrad);
        g.fillEllipse(bdX - beadR, bdY - beadR, beadR * 2.0f, beadR * 2.6f);
        g.setColour(kGloss.withAlpha(0.38f));
        g.fillEllipse(bdX - beadR * 0.35f, bdY - beadR * 0.6f,
                      beadR * 0.5f, beadR * 0.42f);
    }
};

} // namespace DAW

#include "BubblegumCableImpactSplashCore.h"
#include <cmath>

namespace bubblegum
{
    void BubblegumCableImpactSplashCore::paintImpactSplash(
        juce::Graphics& g,
        juce::Point<float> anchor,
        float thickness,
        float timeSeconds,
        float audioEnergy01,
        float sendLevel01,
        float intensity,
        int   seed,
        bool  isDestination,
        const BubblegumCableStyleSettingsCore::Style& style) const
    {
        if (intensity <= 0.0f) return;

        const float T    = thickness;
        const float send = juce::jlimit(0.0f, 1.0f, sendLevel01);
        const float audio = juce::jlimit(0.0f, 1.0f, audioEnergy01);

        // ── Fixed blob size — does NOT change with send level ─────────────────
        // Destination splash is wider and flatter — reads as liquid LANDING and
        // spreading. Source is a tighter teardrop (liquid leaving under pressure).
        const float splatScale = isDestination ? 1.85f : 1.0f;
        const float blobW  = juce::jmax(12.0f, T * 2.20f) * splatScale;
        const float blobH  = juce::jmax(16.0f, T * 2.80f) * (isDestination ? 0.75f : 1.0f);
        const float blobCx = anchor.x;
        const float blobTy = anchor.y - juce::jmax(0.9f, T * 0.16f);
        const float blobBy = blobTy + blobH;

        // ── Send-driven animation speed & deformation ─────────────────────────
        // Low send still moves; high send gets faster/more deformed.
        const float animSpeed = 1.20f + send * 2.20f + audio * 0.90f;
        const float deformAmt = 0.040f + send * 0.10f + audio * 0.040f;

        // Organic wobble — the blob surface deforms like pressurised liquid
        const float wobbleX = deformAmt * blobW
            * (0.72f * std::sin(timeSeconds * animSpeed * 1.3f + (float)seed * 0.71f)
             + 0.28f * std::sin(timeSeconds * animSpeed * 2.4f + (float)seed * 1.19f));
        const float wobbleY = deformAmt * blobH * 0.7f
            * (0.68f * std::sin(timeSeconds * animSpeed * 1.7f + (float)seed * 1.43f)
             + 0.32f * std::sin(timeSeconds * animSpeed * 3.1f + (float)seed * 0.57f));

        // ── Cable entry notch ─────────────────────────────────────────────────
        const float neckW = T * 0.45f;

        // ── Top seal fill ─────────────────────────────────────────────────────
        // Covers the tiny unpainted seam at the scrollbar contact point so the
        // droplet reads as fully coated at the top edge.
        {
            const float sealW = blobW * 0.92f;
            const float sealH = juce::jmax(3.0f, blobH * 0.16f);
            juce::ColourGradient seal(
                style.bodyTop.withAlpha(1.0f),    blobCx, blobTy - sealH * 0.40f,
                style.bodyBottom.withAlpha(1.0f), blobCx, blobTy + sealH * 0.60f,
                false);
            g.setGradientFill(seal);
            g.fillEllipse(blobCx - sealW * 0.5f,
                          blobTy - sealH * 0.52f,
                          sealW,
                          sealH);
        }

        // ── Puddle/splash teardrop path ───────────────────────────────────────
        // Wider and flatter than a pure teardrop — reads as a water-drop
        // puddle that splashed onto the scrollbar underside.
        juce::Path blob;
        blob.startNewSubPath(blobCx - blobW * 0.42f + wobbleX * 0.3f, blobTy);
        // Left shoulder → notch
        blob.cubicTo(
            blobCx - blobW * 0.42f, blobTy - blobH * 0.04f,
            blobCx - neckW * 1.2f,  blobTy - blobH * 0.02f,
            blobCx - neckW * 0.5f,  blobTy + blobH * 0.06f + wobbleY * 0.5f);
        // Notch dip
        blob.cubicTo(
            blobCx - neckW * 0.2f,  blobTy + blobH * 0.11f + wobbleY,
            blobCx + neckW * 0.2f,  blobTy + blobH * 0.11f + wobbleY,
            blobCx + neckW * 0.5f,  blobTy + blobH * 0.06f + wobbleY * 0.5f);
        // Right shoulder
        blob.cubicTo(
            blobCx + neckW * 1.2f,  blobTy - blobH * 0.02f,
            blobCx + blobW * 0.42f, blobTy - blobH * 0.04f,
            blobCx + blobW * 0.42f - wobbleX * 0.3f, blobTy);
        // Right belly — wider outward bulge for puddle shape
        blob.cubicTo(
            blobCx + blobW + wobbleX,         blobTy + blobH * 0.38f,
            blobCx + blobW * 0.52f + wobbleX * 0.5f, blobTy + blobH * 0.85f,
            blobCx,                            blobBy + wobbleY * 0.4f);
        // Left belly (mirror)
        blob.cubicTo(
            blobCx - blobW * 0.52f - wobbleX * 0.5f, blobTy + blobH * 0.85f,
            blobCx - blobW - wobbleX,         blobTy + blobH * 0.38f,
            blobCx - blobW * 0.42f + wobbleX * 0.3f, blobTy);
        blob.closeSubPath();

        // ── Opaque gradient fill ──────────────────────────────────────────────
        juce::ColourGradient fill(
            style.bodyTop.withAlpha(1.0f),    blobCx, blobTy,
            style.bodyBottom.withAlpha(1.0f), blobCx, blobBy,
            false);
        fill.addColour(0.38, style.bodyTop.interpolatedWith(
            style.bodyBottom, 0.28f).withAlpha(1.0f));
        g.setGradientFill(fill);
        g.fillPath(blob);

        // ── Subtle crescent highlight ─────────────────────────────────────────
        {
            juce::Path crescent;
            crescent.addCentredArc(
                blobCx - blobW * 0.08f,
                blobTy + blobH * 0.30f,
                blobW * 0.18f,
                blobH * 0.12f,
                0.0f,
                juce::MathConstants<float>::pi * 1.15f,
                juce::MathConstants<float>::pi * 1.70f,
                true);
            g.setColour(style.highlightSoft.withMultipliedAlpha(0.35f));
            g.strokePath(crescent, juce::PathStrokeType(
                juce::jmax(0.6f, blobW * 0.05f),
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // ── Outline ───────────────────────────────────────────────────────────
        g.setColour(style.bodyBottom.withAlpha(1.0f));
        g.strokePath(blob, juce::PathStrokeType(0.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ── Drip tails from the droplet tip ───────────────────────────────────
        // Send-reactive again, but shorter overall than the earlier long-tail look.
        // dripTailDivisor_: 1 = full (Cinematic), 2 = half (Performance).
        const int dropCount = [&]
        {
            const int full = isDestination
                ? juce::jlimit(2, 5, 2 + (int)std::floor(send * 3.2f))
                : juce::jlimit(1, 3, 1 + (int)std::floor(send * 2.1f));
            return juce::jmax(1, full / dripTailDivisor_);
        }();
        const float tailLenBase = T * (isDestination ? 1.05f + send * 1.30f
                                                     : 0.82f + send * 0.95f);
        const float tailSpeed   = 0.95f + send * 2.35f + audio * 0.85f;
        const float tailOpacity = isDestination ? 0.66f + send * 0.20f
                                                : 0.52f + send * 0.24f;
        const float halfSpan    = juce::jmax(T * 0.18f,
            blobW * (isDestination ? 0.26f : 0.18f) * (0.76f + send * 0.18f));
        const float undersideHalfW = blobW * (isDestination ? 0.34f : 0.26f);

        for (int i = 0; i < dropCount; ++i)
        {
            const float lateralT = dropCount == 1
                ? 0.0f
                : juce::jmap((float)i, 0.0f, (float)(dropCount - 1), -1.0f, 1.0f);
            const float lateralOff = lateralT * halfSpan;

            const float phase = timeSeconds * tailSpeed * (1.1f + (float)i * 0.3f)
                              + (float)(i + seed) * 1.37f;

            const float centreBias = 1.0f - 0.18f * std::abs(lateralT);

            const float hangLen = juce::jmax(T * 1.10f,
                tailLenBase * centreBias * (0.84f + 0.16f * std::sin(phase)));

            const float sizeVar = 0.80f + 0.30f
                * std::abs(std::sin(phase * 0.7f + (float)i * 2.3f));
            const float neckR  = T * (0.06f + send * 0.02f) * sizeVar;
            const float bulgeR = T * (0.10f + send * 0.04f
                + 0.03f * std::abs(std::sin(phase))) * sizeVar;

            const float contourX = lateralOff / juce::jmax(1.0f, undersideHalfW);
            const float contourY = std::sqrt(juce::jmax(0.0f, 1.0f - contourX * contourX));
            const float bx = blobCx + lateralOff;
            const float by = blobTy + blobH * (0.60f + 0.28f * contourY) + wobbleY * 0.20f;

            g.setColour(style.bodyBottom.withAlpha(tailOpacity));
            g.drawLine(bx, by, bx, by + hangLen - bulgeR * 0.45f, neckR * 1.45f);

            juce::ColourGradient bead(
                style.bodyTop.withAlpha(tailOpacity),
                bx, by + hangLen - bulgeR * 1.8f,
                style.bodyBottom.withAlpha(tailOpacity),
                bx, by + hangLen,
                false);
            g.setGradientFill(bead);
            g.fillEllipse(bx - bulgeR, by + hangLen - bulgeR * 1.4f,
                          bulgeR * 2.0f, bulgeR * 1.9f);
        }

        // Static decorative beads around the anchor splash. These are baked into
        // the cable image cache with the rest of the static cable.
        constexpr int kStaticAnchorBeadCount = 5;
        constexpr float beadX[kStaticAnchorBeadCount] = { -0.42f, -0.22f, 0.10f, 0.31f, 0.48f };
        constexpr float beadY[kStaticAnchorBeadCount] = {  0.50f,  0.82f, 0.64f, 0.92f, 0.58f };
        constexpr float beadR[kStaticAnchorBeadCount] = {  0.13f,  0.18f, 0.15f, 0.20f, 0.12f };

        for (int i = 0; i < kStaticAnchorBeadCount; ++i)
        {
            const float bx = blobCx + beadX[i] * blobW * (isDestination ? 1.06f : 0.86f);
            const float by = blobTy + beadY[i] * blobH;
            const float r = juce::jmax(0.9f, T * beadR[i] * (isDestination ? 1.10f : 0.95f));
            const float alpha = intensity * (isDestination ? 0.70f : 0.58f);

            juce::ColourGradient bead(
                style.bodyTop.withAlpha(alpha),
                bx - r * 0.35f, by - r * 0.55f,
                style.bodyBottom.withAlpha(alpha),
                bx, by + r,
                false);
            g.setGradientFill(bead);
            g.fillEllipse(bx - r, by - r, r * 2.0f, r * 2.0f);

            const float hr = r * 0.32f;
            g.setColour(style.highlightSharp.withAlpha(alpha * 0.55f));
            g.fillEllipse(bx - r * 0.25f - hr, by - r * 0.28f - hr,
                          hr * 2.0f, hr * 2.0f);
        }
    }

} // namespace bubblegum

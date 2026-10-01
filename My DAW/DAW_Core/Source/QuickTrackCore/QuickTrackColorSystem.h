// ===========================================================================
// QuickTrackColorSystem.h
// APEX Quick Track Builder — project-scoped automatic role-color families.
//
// NON-NEGOTIABLE INVARIANTS
// -------------------------
// 1. Same normalized role/name key → SAME color within one project.
// 2. Different role/name keys → DIFFERENT automatic colors within one
//    project. Automatic colors NEVER repeat across role families.
// 3. Color source priority: canonical APEX track palette (shuffled per
//    project) → dynamically generated perceptually-spaced colors when the
//    palette is exhausted. The palette EXTENDS — it never cycles a color
//    that another family already uses.
// 4. Manual user choices are authoritative and MAY duplicate any color.
// 5. The map is project-scoped: New Project starts fresh; save/reload
//    restores every assignment exactly (no rerandomization).
//
// Determinism: assignment order + a per-project seed fully determine the
// outcome, so tests can use fixed seeds and reloads never drift.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>
#include <random>
#include <algorithm>
#include "../UICore/TrackColorPalette.h"
#include "../QuickTrackCore/QuickTrackNaming.h"

namespace DAW {

class QuickTrackColorSystem
{
public:
    struct Entry
    {
        juce::Colour color;
        bool manual = false;
    };

    explicit QuickTrackColorSystem(juce::int64 projectSeed)
        : projectSeed_(projectSeed)
    {
        reshufflePalette();
    }

    // ── Palette / generation (shared, deterministic) ──────────────────────

    /** The canonical APEX 48-swatch track palette (single source of truth). */
    static juce::Array<juce::Colour> canonicalPalette()
    {
        return TrackColorPalette::getCanonicalPalette();
    }

    /** Deterministic perceptually-spaced extended color (golden-angle hue
     *  spacing, readability-constrained saturation/lightness). */
    static juce::Colour generateExtendedColor(int index, juce::int64 seedOffset)
    {
        constexpr double goldenAngle = 137.50776405003785;
        double hue = std::fmod((double) seedOffset + (double) index * goldenAngle, 360.0);
        if (hue < 0.0) hue += 360.0;
        // Slight per-cycle variation keeps generated sets from looking flat.
        const double sat   = 0.70 + 0.06 * ((index / 7) % 3);
        const double light = 0.56 + 0.05 * ((index / 5) % 3);
        return juce::Colour::fromHSV((float) (hue / 360.0),
                                     (float) juce::jlimit(0.45, 0.85, sat),
                                     (float) juce::jlimit(0.42, 0.72, light),
                                     1.0f);
    }

    /** CIE76 perceptual distance in Lab (0..~100; < ~12 is a near match). */
    static float perceptualDistance(const juce::Colour& a, const juce::Colour& b)
    {
        auto toLab = [](const juce::Colour& c, float& L, float& A, float& B)
        {
            auto srgbToLinear = [](float x)
            {
                x = juce::jlimit(0.0f, 1.0f, x);
                return x <= 0.04045f ? x / 12.92f
                                     : std::pow((x + 0.055f) / 1.055f, 2.4f);
            };
            const float r = srgbToLinear(c.getFloatRed());
            const float g = srgbToLinear(c.getFloatGreen());
            const float b2 = srgbToLinear(c.getFloatBlue());
            // sRGB → XYZ (D65)
            const float X = (r * 0.4124564f + g * 0.3575761f + b2 * 0.1804375f) / 0.95047f;
            const float Y = (r * 0.2126729f + g * 0.7151522f + b2 * 0.0721750f) / 1.00000f;
            const float Z = (r * 0.0193339f + g * 0.1191920f + b2 * 0.9503041f) / 1.08883f;
            auto f = [](float t)
            {
                const float eps = 216.0f / 24389.0f;
                const float kappa = 24389.0f / 27.0f;
                return t > eps ? std::cbrt(t) : (kappa * t + 16.0f) / 116.0f;
            };
            const float fx = f(X), fy = f(Y), fz = f(Z);
            L = 116.0f * fy - 16.0f;
            A = 500.0f * (fx - fy);
            B = 200.0f * (fy - fz);
        };
        float l1, a1, b1, l2, a2, b2;
        toLab(a, l1, a1, b1);
        toLab(b, l2, a2, b2);
        const float dL = l1 - l2, dA = a1 - a2, dB = b1 - b2;
        return std::sqrt(dL * dL + dA * dA + dB * dB);
    }

    // ── Assignment ────────────────────────────────────────────────────────

    /** Normalize any role display name / custom name into a color-family key. */
    static juce::String normalizeKey(const juce::String& name)
    {
        return QuickTrackNaming::normalizeFamilyKey(name);
    }

    /** Get the family color, or assign one on first use (auto). The key is
     *  normalized so numbered copies ("Coro / Hook 2") share their base
     *  family ("coro / hook"). */
    juce::Colour assignColorForRole(const juce::String& normalizedKey)
    {
        const auto key = normalizeKey(normalizedKey);
        const auto it = entries_.find(key);
        if (it != entries_.end())
            return it->second.color;

        juce::Colour chosen;
        bool found = false;

        // 1) Curated APEX palette, shuffled per project, randomized
        //    without replacement.
        while (paletteCursor_ < paletteOrder_.size())
        {
            const juce::Colour candidate = paletteOrder_[paletteCursor_++];
            if (!isUsedByAnyFamily(candidate))
            {
                chosen = candidate;
                found = true;
                break;
            }
        }

        // 2) Dynamic extension — never reuse an assigned automatic color.
        if (!found)
        {
            const int maxAttempts = 96;
            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                const int genIndex = generatedCount_ + attempt;
                juce::Colour candidate = generateExtendedColor(genIndex, projectSeed_);
                // Nudge hue until perceptually distinct from every assigned
                // family color and readable on the dark APEX UI.
                for (int nudge = 0; nudge < 24; ++nudge)
                {
                    bool distinct = true;
                    for (const auto& [existingKey, entry] : entries_)
                    {
                        if (perceptualDistance(candidate, entry.color) < kMinPerceptualDistance)
                        {
                            distinct = false;
                            break;
                        }
                    }
                    if (distinct)
                        break;
                    const double hueDeg = juce::jmap((float) nudge, 0.0f, 23.0f, 7.5f, 180.0f);
                    const float hue = candidate.getHue();
                    const float sat = candidate.getSaturation();
                    const float bri = candidate.getBrightness();
                    candidate = juce::Colour::fromHSV(std::fmod(hue + (float) (hueDeg / 360.0), 1.0f),
                                                      sat, bri, 1.0f);
                }
                // Re-check against all families after nudging.
                bool distinct = true;
                for (const auto& [existingKey, entry] : entries_)
                {
                    if (perceptualDistance(candidate, entry.color) < kMinPerceptualDistance)
                    {
                        distinct = false;
                        break;
                    }
                }
                if (distinct)
                {
                    chosen = candidate;
                    generatedCount_ += attempt + 1;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                // Absolute fallback (practically unreachable): farthest from
                // the assigned set.
                float bestDist = -1.0f;
                for (int i = 0; i < 360; i += 5)
                {
                    const auto candidate = juce::Colour::fromHSV(i / 360.0f, 0.7f, 0.6f, 1.0f);
                    float minDist = 1e9f;
                    for (const auto& [existingKey, entry] : entries_)
                        minDist = juce::jmin(minDist, perceptualDistance(candidate, entry.color));
                    if (minDist > bestDist)
                    {
                        bestDist = minDist;
                        chosen = candidate;
                    }
                }
                ++generatedCount_;
            }
        }

        entries_[key] = Entry { chosen, false };
        return chosen;
    }

    juce::Colour getColorForRole(const juce::String& normalizedKey) const
    {
        const auto key = normalizeKey(normalizedKey);
        const auto it = entries_.find(key);
        return it != entries_.end() ? it->second.color : juce::Colour();
    }

    bool hasColorForRole(const juce::String& normalizedKey) const
    {
        const auto key = normalizeKey(normalizedKey);
        return entries_.find(key) != entries_.end();
    }

    bool isManual(const juce::String& normalizedKey) const
    {
        const auto key = normalizeKey(normalizedKey);
        const auto it = entries_.find(key);
        return it != entries_.end() && it->second.manual;
    }

    /** Explicit user choice — wins over auto; duplicates are allowed. */
    void setManualRoleColor(const juce::String& normalizedKey, const juce::Colour& colour)
    {
        entries_[normalizeKey(normalizedKey)] = Entry { colour, true };
    }

    /** Return a family to AUTO (removes manual override; auto reassigns next
     *  time the role is created). */
    void clearManualOverride(const juce::String& normalizedKey)
    {
        const auto key = normalizeKey(normalizedKey);
        const auto it = entries_.find(key);
        if (it != entries_.end() && it->second.manual)
            it->second.manual = false;
    }

    // ── Project lifecycle ─────────────────────────────────────────────────

    /** New Project: completely fresh color map + fresh palette shuffle. */
    void reset(juce::int64 newProjectSeed)
    {
        projectSeed_ = newProjectSeed;
        entries_.clear();
        generatedCount_ = 0;
        reshufflePalette();
    }

    void clear()
    {
        entries_.clear();
        generatedCount_ = 0;
        paletteCursor_ = 0;
    }

    // ── Persistence (project-scoped) ──────────────────────────────────────

    juce::ValueTree getState() const
    {
        juce::ValueTree tree("QuickTrackColors");
        tree.setProperty("version", 1, nullptr);
        for (const auto& [key, entry] : entries_)
        {
            juce::ValueTree roleTree("RoleColor");
            roleTree.setProperty("key", key, nullptr);
            roleTree.setProperty("color", entry.color.toString(), nullptr);
            roleTree.setProperty("manual", entry.manual, nullptr);
            tree.appendChild(roleTree, nullptr);
        }
        return tree;
    }

    void restoreState(const juce::ValueTree& state)
    {
        entries_.clear();
        generatedCount_ = 0;
        if (!state.isValid() || !state.hasType("QuickTrackColors"))
        {
            paletteCursor_ = 0;
            return;
        }
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            const auto child = state.getChild(i);
            if (!child.hasType("RoleColor"))
                continue;
            const auto key = child.getProperty("key").toString();
            if (key.isEmpty())
                continue;
            Entry entry;
            entry.color = juce::Colour::fromString(child.getProperty("color", "ffffffff").toString());
            entry.manual = (bool) child.getProperty("manual", false);
            entries_[key] = entry;
        }
        // Resume palette scanning AFTER the occupied colors so new
        // assignments never collide with restored families.
        paletteCursor_ = 0;
        for (const auto& c : paletteOrder_)
        {
            if (isUsedByAnyFamily(c))
                ++paletteCursor_;
            else
                break;
        }
    }

    int getNumAssignedFamilies() const noexcept { return (int) entries_.size(); }
    juce::int64 getProjectSeed() const noexcept { return projectSeed_; }

    const std::map<juce::String, Entry>& getEntries() const noexcept { return entries_; }

private:
    static constexpr float kMinPerceptualDistance = 16.0f;

    bool isUsedByAnyFamily(const juce::Colour& colour) const
    {
        for (const auto& [key, entry] : entries_)
            if (entry.color == colour)
                return true;
        return false;
    }

    void reshufflePalette()
    {
        paletteOrder_.clear();
        const auto palette = canonicalPalette();
        paletteOrder_.reserve((size_t) palette.size());
        for (auto c : palette)
            paletteOrder_.push_back(c);
        std::mt19937_64 rng((uint64_t) projectSeed_);
        std::shuffle(paletteOrder_.begin(), paletteOrder_.end(), rng);
        paletteCursor_ = 0;
    }

    juce::int64 projectSeed_;
    std::map<juce::String, Entry> entries_;
    std::vector<juce::Colour> paletteOrder_;
    size_t paletteCursor_ = 0;
    int generatedCount_ = 0;
};

} // namespace DAW

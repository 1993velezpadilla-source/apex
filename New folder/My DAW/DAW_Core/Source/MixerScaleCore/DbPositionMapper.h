#pragma once
#include <cmath>
#include <JuceHeader.h>
#include "FaderRangeCore.h"

namespace DAW {

/**
 * DbPositionMapper — THIN SHIM that delegates to FaderRangeCore.
 *
 * All dB↔position math now lives exclusively in FaderRangeCore (single source
 * of truth). This file exists only for call-site compatibility — it forwards
 * every call to the global FaderRangeCore instance.
 *
 * DO NOT add new constants here. Any value you need is in FaderRangeCore.
 */
namespace DbPositionMapper
{
    // ── Helpers that require a FaderRangeCore reference ──────────────────
    inline float dbToPos(float db, const FaderRangeCore& core) noexcept
    {
        return core.dbToNorm(db);
    }
    inline float posToDb(float pos, const FaderRangeCore& core) noexcept
    {
        return core.normToDb(pos);
    }
    inline float gainToPos(float gain, const FaderRangeCore& core) noexcept
    {
        return core.gainToNorm(gain);
    }
    inline float posToGain(float pos, const FaderRangeCore& core) noexcept
    {
        return core.normToGain(pos);
    }
    inline float posAt(float db, const FaderRangeCore& core) noexcept
    {
        return core.dbToNorm(db);
    }

    // ── Global-instance convenience overloads (uses FaderRangeCore::getGlobalInstance()) ──
    // Falls back gracefully if global instance not yet set (e.g. during static init).
    inline float dbToPos(float db) noexcept
    {
        if (auto* c = FaderRangeCore::getGlobalInstance()) return c->dbToNorm(db);
        // Fallback: Plus6 taper inline (avoids null-deref during early init)
        db = juce::jlimit(-96.0f, 6.0f, db);
        const float unity = 0.88f, exp = 2.5f, minDb = -96.0f;
        if (db <= 0.0f) { float t = (db - minDb) / (-minDb); return std::pow(t, exp) * unity; }
        return unity + (db / 6.0f) * (1.0f - unity);
    }
    inline float posToDb(float pos) noexcept
    {
        if (auto* c = FaderRangeCore::getGlobalInstance()) return c->normToDb(pos);
        pos = juce::jlimit(0.0f, 1.0f, pos);
        const float unity = 0.88f, exp = 2.5f, minDb = -96.0f;
        if (pos <= unity) { float t = std::pow(pos / unity, 1.0f / exp); return minDb + t * (-minDb); }
        return (pos - unity) / (1.0f - unity) * 6.0f;
    }
    inline float gainToPos(float gain) noexcept
    {
        if (auto* c = FaderRangeCore::getGlobalInstance()) return c->gainToNorm(gain);
        return dbToPos(gain <= 0.000001f ? -96.0f : 20.0f * std::log10(gain));
    }
    inline float posToGain(float pos) noexcept
    {
        float db = posToDb(pos);
        return db <= -96.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
    }
    inline float posAt(float db) noexcept { return dbToPos(db); }

    // ── dB ↔ gain (format-agnostic) ───────────────────────────────────────
    inline float dbToGain(float db) noexcept
    {
        return db <= -96.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
    }
    inline float gainToDb(float gain) noexcept
    {
        if (gain <= 0.000001f) return -96.0f;
        if (auto* c = FaderRangeCore::getGlobalInstance())
            return juce::jlimit(-96.0f, c->getMaxDb(), 20.0f * std::log10(gain));
        return juce::jlimit(-96.0f, 6.0f, 20.0f * std::log10(gain));
    }
}

} // namespace DAW

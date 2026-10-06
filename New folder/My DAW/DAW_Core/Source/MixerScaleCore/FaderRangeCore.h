#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace DAW {

/**
 * FaderRangeCore — SINGLE SOURCE OF TRUTH for all fader format, dB↔position
 * math, taper curves, tick geometry, and gain computation.
 *
 * Architecture: one instance per session (global). Every system — audio engine,
 * fader thumb, scale column, meter, automation — reads from this core ONLY.
 * No other module stores or computes format-dependent values independently.
 *
 * FORMAT SWITCH IS A TRUE SIGNAL-PATH CHANGE:
 *   Flipping Range::Plus6 ↔ Range::Plus12 changes:
 *     • maxDb ceiling (audio gain clamped to new max)
 *     • taper curve (power exponent changes per format)
 *     • unity visual position (0.88 vs 0.75)
 *     • tick geometry and amber zone bounds
 *   Stored fader values are in dB and survive format switches unchanged.
 *   Values above the new max are clamped (e.g. +10 dB → +6 dB on Plus6 flip).
 *
 * TAPER CURVES (per format, smooth across unity):
 *   Below unity: power curve  norm = (t)^exp  where t = normalised dB in [minDb..0]
 *   Above unity: linear       norm = unity + (db / maxDb) * (1 - unity)
 *   +6  format: exp = 2.5  — steeper, more resolution near unity
 *   +12 format: exp = 2.2  — gentler, SSL-style above-unity headroom
 */
class FaderRangeCore
{
public:
    // ── Format ─────────────────────────────────────────────────────────────
    enum class Range { Plus6, Plus12 };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void faderRangeChanged() = 0;
    };

    FaderRangeCore() = default;

    /** TRUE FORMAT SWITCH — changes audio gain ceiling, taper, and all geometry. */
    void setRange(Range r)
    {
        if (r == range_) return;
        range_ = r;
        listeners_.call([](Listener& l) { l.faderRangeChanged(); });
    }

    Range getRange()   const noexcept { return range_; }

    /** Maximum boost dB for this format (+6 or +12). */
    float getMaxDb()   const noexcept { return range_ == Range::Plus12 ? 12.0f : 6.0f; }

    /** Minimum dB — effective silence floor, format-agnostic. */
    float getMinDb()   const noexcept { return -96.0f; }

    /** Normalized position [0..1] where 0 dB (unity) sits on the taper.
     *  +6 format: 0.88 (unity sits high — more resolution below)
     *  +12 format: 0.75 (unity sits lower — SSL-style headroom above) */
    float getUnityNorm() const noexcept { return range_ == Range::Plus12 ? 0.75f : 0.88f; }

    /** Power exponent for below-unity taper. Higher = more resolution near unity. */
    float getTaperExponent() const noexcept { return range_ == Range::Plus12 ? 2.2f : 2.5f; }

    // ── SINGLE SOURCE OF TRUTH: dB ↔ normalized position (0=bottom, 1=top) ──
    //
    // Taper curve:
    //   Below unity: norm = pow(t, exp) * unityNorm
    //     where t = (db - minDb) / (0 - minDb)   [0..1]
    //   Above unity: norm = unityNorm + (db / maxDb) * (1 - unityNorm)
    //
    float dbToNorm(float db) const noexcept
    {
        const float maxDb  = getMaxDb();
        const float minDb  = getMinDb();
        const float unity  = getUnityNorm();
        const float exp    = getTaperExponent();
        db = juce::jlimit(minDb, maxDb, db);
        if (db <= 0.0f)
        {
            const float t = (db - minDb) / (-minDb);   // 0..1 across [minDb..0]
            return std::pow(t, exp) * unity;
        }
        return unity + (db / maxDb) * (1.0f - unity);
    }

    float normToDb(float norm) const noexcept
    {
        norm = juce::jlimit(0.0f, 1.0f, norm);
        const float maxDb = getMaxDb();
        const float minDb = getMinDb();
        const float unity = getUnityNorm();
        const float exp   = getTaperExponent();
        if (norm <= unity)
        {
            if (unity <= 0.0f) return minDb;
            const float t = std::pow(norm / unity, 1.0f / exp);  // inverse power
            return minDb + t * (-minDb);
        }
        return (norm - unity) / (1.0f - unity) * maxDb;
    }

    // ── dB ↔ linear gain (format-agnostic, uses minDb floor) ─────────────
    float dbToGain(float db) const noexcept
    {
        return db <= getMinDb() ? 0.0f : std::pow(10.0f, db / 20.0f);
    }
    static float gainToDb(float gain) noexcept
    {
        if (gain <= 0.000001f) return -96.0f;
        return 20.0f * std::log10(gain);
    }

    float gainToNorm(float gain) const noexcept { return dbToNorm(gainToDb(gain)); }
    float normToGain(float norm) const noexcept { return dbToGain(normToDb(norm)); }

    /** Clamp a dB value to the current format's range. Used when switching formats. */
    float clampDb(float db) const noexcept
    {
        return juce::jlimit(getMinDb(), getMaxDb(), db);
    }

    // ── Tick lists (filtered by current maxDb, greedy priority) ──────────
    struct TickEntry { float db; const char* label; bool isMajor; int priority; };

    /** All candidate ticks filtered by current maxDb. Y = dbToNorm(db)*height. */
    juce::Array<TickEntry> getAllTicks() const
    {
        const float maxDb = getMaxDb();
        static const TickEntry kAll[] = {
            {   0.0f, "0",    true,  0 },
            {  -3.0f, "-3",   true,  0 },
            { -96.0f, "-inf", true,  0 },
            {   3.0f, "+3",   true,  1 },
            {  -1.0f, "-1",   true,  1 },
            {  -2.0f, "-2",   true,  1 },
            {  -6.0f, "-6",   true,  1 },
            { -12.0f, "-12",  true,  1 },
            {   6.0f, "+6",   true,  2 },
            {  -4.0f, "-4",   true,  2 },
            {  -9.0f, "-9",   true,  2 },
            { -18.0f, "-18",  true,  2 },
            { -24.0f, "-24",  true,  2 },
            {  12.0f, "+12",  true,  3 },
            {   9.0f, "+9",   true,  3 },
            { -36.0f, "-36",  true,  3 },
            { -48.0f, "-48",  true,  3 },
            { -72.0f, "-72",  true,  4 },
            {   1.5f, "",     false, 5 },
            {  -5.0f, "",     false, 5 },
            { -15.0f, "",     false, 5 },
            { -21.0f, "",     false, 5 },
            { -30.0f, "",     false, 5 },
            { -42.0f, "",     false, 5 },
            { -60.0f, "",     false, 5 },
            { -84.0f, "",     false, 5 },
        };
        juce::Array<TickEntry> out;
        for (const auto& tick : kAll)
            if (tick.db <= maxDb + 0.01f)
                out.add(tick);
        return out;
    }

    /** Ticks selected for a specific pixel height, returned top-to-bottom for stable painting. */
    juce::Array<TickEntry> getDisplayTicks(float pixelHeight) const
    {
        auto all = getAllTicks();
        if (pixelHeight <= 0.0f)
            return all;

        const float majorSpacing = juce::jlimit(7.5f, 14.0f, pixelHeight / 24.0f);
        const float minorSpacing = juce::jlimit(5.0f, 9.0f, majorSpacing * 0.7f);

        std::vector<TickEntry> candidates;
        candidates.reserve((size_t)all.size());
        for (const auto& tick : all)
            candidates.push_back(tick);

        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const TickEntry& a, const TickEntry& b)
                         {
                             if (a.priority != b.priority) return a.priority < b.priority;
                             if (a.isMajor != b.isMajor)   return a.isMajor && !b.isMajor;
                             return a.db > b.db;
                         });

        std::vector<TickEntry> selected;
        selected.reserve(candidates.size());

        auto canPlace = [this, pixelHeight, majorSpacing, minorSpacing, &selected](const TickEntry& tick)
        {
            const float y = (1.0f - dbToNorm(tick.db)) * pixelHeight;
            for (const auto& existing : selected)
            {
                const float existingY = (1.0f - dbToNorm(existing.db)) * pixelHeight;
                const float spacing = (tick.isMajor || existing.isMajor) ? majorSpacing : minorSpacing;
                if (std::abs(y - existingY) < spacing)
                    return false;
            }

            return true;
        };

        for (const auto& tick : candidates)
            if (canPlace(tick))
                selected.push_back(tick);

        std::sort(selected.begin(), selected.end(),
                  [](const TickEntry& a, const TickEntry& b)
                  {
                      return a.db > b.db;
                  });

        juce::Array<TickEntry> out;
        for (const auto& tick : selected)
            out.add(tick);
        return out;
    }

    /** Major labeled ticks for the scale column. */
    juce::Array<float> getMajorTickDbs() const
    {
        juce::Array<float> out;
        for (const auto& t : getAllTicks())
            if (t.isMajor) out.add(t.db);
        return out;
    }

    /** Minor (unlabeled) tick dBs. */
    juce::Array<float> getMinorTickDbs() const
    {
        juce::Array<float> out;
        for (const auto& t : getAllTicks())
            if (!t.isMajor) out.add(t.db);
        return out;
    }

    // ── Persistence — serializes format choice to session ValueTree ───────
    juce::ValueTree toValueTree() const
    {
        juce::ValueTree vt("FaderRange");
        vt.setProperty("range", range_ == Range::Plus12 ? "plus12" : "plus6", nullptr);
        return vt;
    }
    /** Load format BEFORE any strips paint. Unknown/missing property defaults to Plus6. */
    void fromValueTree(const juce::ValueTree& vt)
    {
        if (vt.hasProperty("range"))
            setRange(vt["range"].toString() == "plus12" ? Range::Plus12 : Range::Plus6);
        else
            setRange(Range::Plus6);  // legacy sessions without property → Plus6 default
    }

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    // ── Global accessor — set once at app startup, read everywhere ────────
    static void setGlobalInstance(FaderRangeCore* instance) noexcept { sGlobal_ = instance; }
    static FaderRangeCore* getGlobalInstance() noexcept { return sGlobal_; }

private:
    Range range_ { Range::Plus6 };   // default +6 — spec requires Plus6 as default
    juce::ListenerList<Listener> listeners_;

    static inline FaderRangeCore* sGlobal_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE(FaderRangeCore)
};

} // namespace DAW

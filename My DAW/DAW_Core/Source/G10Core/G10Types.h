#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace G10 {

// ============================================================================
// APEX G10 — permanent product identity and parameter contract.
//
// Ten fixed musical bands. The frequency is the primary technical label; the
// musical identity is the primary workflow label. These names are permanent
// APEX product identities and must not be renamed without an explicit
// product-level decision.
// ============================================================================

static constexpr int kNumBands = 10;

// Stable persistent parameter IDs (compatibility ABI once projects store them).
static constexpr const char* kParamInput   = "g10.input";
static constexpr const char* kParamBand31  = "g10.band31";
static constexpr const char* kParamBand63  = "g10.band63";
static constexpr const char* kParamBand125 = "g10.band125";
static constexpr const char* kParamBand250 = "g10.band250";
static constexpr const char* kParamBand500 = "g10.band500";
static constexpr const char* kParamBand1k  = "g10.band1k";
static constexpr const char* kParamBand2k  = "g10.band2k";
static constexpr const char* kParamBand4k  = "g10.band4k";
static constexpr const char* kParamBand8k  = "g10.band8k";
static constexpr const char* kParamBand16k = "g10.band16k";
static constexpr const char* kParamOutputId = "g10.output";
static constexpr const char* kParamAnalogId  = "g10.analog";
static constexpr const char* kParamQualityId = "g10.quality";
static constexpr const char* kParamBypassId  = "g10.bypass";

// ---- Mini Clean EQ (clean digital correction layer, permanent IDs) --------
static constexpr const char* kParamHpfId = "g10.hpf";
static constexpr const char* kParamLpfId = "g10.lpf";
static constexpr const char* kParamBell1EnabledId = "g10.bell1.enabled";
static constexpr const char* kParamBell1FreqId    = "g10.bell1.freq";
static constexpr const char* kParamBell1GainId    = "g10.bell1.gain";
static constexpr const char* kParamBell1QId       = "g10.bell1.q";
static constexpr const char* kParamBell2EnabledId = "g10.bell2.enabled";
static constexpr const char* kParamBell2FreqId    = "g10.bell2.freq";
static constexpr const char* kParamBell2GainId    = "g10.bell2.gain";
static constexpr const char* kParamBell2QId       = "g10.bell2.q";
static constexpr const char* kParamBell3EnabledId = "g10.bell3.enabled";
static constexpr const char* kParamBell3FreqId    = "g10.bell3.freq";
static constexpr const char* kParamBell3GainId    = "g10.bell3.gain";
static constexpr const char* kParamBell3QId       = "g10.bell3.q";
// ---- Individual Bell bypass (v3; bypass != delete) -------------------------
static constexpr const char* kParamBell1BypassId = "g10.bell1.bypass";
static constexpr const char* kParamBell2BypassId = "g10.bell2.bypass";
static constexpr const char* kParamBell3BypassId = "g10.bell3.bypass";

// Public gain ranges (fixed product contract).
static constexpr float kBandMinDb = -12.0f;
static constexpr float kBandMaxDb = +12.0f;
static constexpr float kBandDefaultDb = 0.0f;
static constexpr float kTrimMinDb = -18.0f;
static constexpr float kTrimMaxDb = +18.0f;
static constexpr float kTrimDefaultDb = 0.0f;

// State schema version.
// v1 = the frozen 15-parameter musical G10 (legacy sessions).
// v2 = adds the 14 Mini Clean EQ parameters; missing v2 properties restore
//      to defaults (HPF OFF, LPF OFF, all Bells disabled) so old sessions
//      load exactly like old G10.
// v3 = adds the 3 individual Bell bypass parameters (g10.bellN.bypass) and
//      re-maps the Mini EQ control laws:
//        HPF 20..420 Hz        -> 20 Hz..20 kHz  (log)
//        LPF 8.5..16 kHz       -> 20 Hz..20 kHz  (log)
//        Bell Q 0.5..10        -> 0.10..40.0     (log)
//      v2 states are migrated SEMANTICALLY (old normalized value -> exact old
//      Hz/Q -> new normalized value) so old sessions keep their sound.
static constexpr int kStateVersion = 3;

// ============================================================================
// Band identity table.
// ============================================================================

struct G10BandInfo
{
    const char* paramId;      // stable persistent ID
    const char* musicalName; // permanent musical identity
    const char* frequencyLabel; // technical label
    float centerHz;          // nominal center / musical anchor
};

static constexpr G10BandInfo kBandInfos[kNumBands] =
{
    { kParamBand31,  "DEEP",     "31 Hz",  31.0f   },
    { kParamBand63,  "PUNCH",    "63 Hz",  63.0f   },
    { kParamBand125, "BODY",     "125 Hz", 125.0f  },
    { kParamBand250, "WARMTH",   "250 Hz", 250.0f  },
    { kParamBand500, "WOOD",     "500 Hz", 500.0f  },
    { kParamBand1k,  "FOCUS",    "1 kHz",  1000.0f },
    { kParamBand2k,  "ATTACK",   "2 kHz",  2000.0f },
    { kParamBand4k,  "PRESENCE", "4 kHz",  4000.0f },
    { kParamBand8k,  "SHINE",    "8 kHz",  8000.0f },
    { kParamBand16k, "AIR",      "16 kHz", 16000.0f },
};

// ============================================================================
// Curve family identity. Each family is a distinct musical design; shared
// low-level DSP primitives are allowed, shared musical behavior is not.
// ============================================================================

enum class G10CurveFamily
{
    Deep31,
    Punch63,
    Body125,
    Warmth250,
    Wood500,
    Focus1k,
    Attack2k,
    Presence4k,
    Shine8k,
    Air16k
};

// Internal research lineage (never exposed in the product UI).
//   Deep31    — Pultec EQP-1A philosophy
//   Punch63   — SSL 4000E Black + API philosophy
//   Body125   — Neve 1073 philosophy
//   Warmth250 — Harrison 32C philosophy
//   Wood500   — Harrison 32C philosophy
//   Focus1k   — API 560 philosophy
//   Attack2k  — API 560 + SSL E philosophy
//   Presence4k— SSL 4000E philosophy
//   Shine8k   — Trident A-Range philosophy
//   Air16k    — Maag Air Band philosophy

// ============================================================================
// Parameter index layout (stable ordering for the processor parameter list).
// ============================================================================

enum G10ParamIndex
{
    kInput = 0,
    kBand31 = 1,
    kBand63 = 2,
    kBand125 = 3,
    kBand250 = 4,
    kBand500 = 5,
    kBand1k = 6,
    kBand2k = 7,
    kBand4k = 8,
    kBand8k = 9,
    kBand16k = 10,
    kOutput = 11,
    kAnalog = 12,
    kQuality = 13,
    kBypass = 14,

    // ---- Mini Clean EQ (appended after the frozen 15) ---------------------
    kHpf = 15,
    kLpf = 16,
    kBell1Enabled = 17,
    kBell1Freq = 18,
    kBell1Gain = 19,
    kBell1Q = 20,
    kBell2Enabled = 21,
    kBell2Freq = 22,
    kBell2Gain = 23,
    kBell2Q = 24,
    kBell3Enabled = 25,
    kBell3Freq = 26,
    kBell3Gain = 27,
    kBell3Q = 28,

    // ---- Individual Bell bypass (v3; appended AFTER the existing IDs so the
    // stable ordering of every earlier parameter is untouched) --------------
    kBell1Bypass = 29,
    kBell2Bypass = 30,
    kBell3Bypass = 31,
    kNumParams = 32
};

// ============================================================================
// dB helpers (shared, allocation-free, realtime-safe).
// ============================================================================

inline float dbToGain (float db) noexcept
{
    // 10^(db/20) == 2^(db * log2(10)/20)
    return std::exp2 (db * 0.16609640474436813f);
}

inline float gainToDb (float gain) noexcept
{
    return 20.0f * std::log10 (juce::jmax (1e-30f, gain));
}

} // namespace G10
} // namespace APEX
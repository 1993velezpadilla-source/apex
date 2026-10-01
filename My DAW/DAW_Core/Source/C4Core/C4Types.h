#pragma once
#include <JuceHeader.h>

namespace APEX {
namespace C4 {

// ============================================================================
// APEX C4 — Console Character Equalizer (BLOOM CORE).
//
// Permanent product identity and parameter contract.
//
// Four musical zones, one instrument:
//   WEIGHT  — low-frequency size, foundation, chest, mass.       30..450 Hz
//   SCULPT  — body, warmth, wood, broad tonal shaping.          120 Hz..2.5 kHz
//   BITE    — presence, attack, articulation, aggression.       400 Hz..9 kHz
//   OPEN    — brightness, air, height, openness.    Bell 1.5..20 kHz,
//             HALO shelf control 2.5..40 kHz (realizable in-band mapping).
//
// These names are permanent APEX product identities. The parameter IDs below
// become a compatibility ABI once projects store them.
// ============================================================================

static constexpr int kNumBands = 4;

// Max channels supported by the C4 engine (shared constant; the engine
// clamps any larger host layout).
static constexpr int kMaxChannels = 2;

// ---- Stable persistent parameter IDs --------------------------------------
static constexpr const char* kParamWeightFreqId = "c4.weight.freq";
static constexpr const char* kParamWeightGainId = "c4.weight.gain";
static constexpr const char* kParamWeightModeId = "c4.weight.mode"; // 0=Bell 1=Shelf
static constexpr const char* kParamSculptFreqId = "c4.sculpt.freq";
static constexpr const char* kParamSculptGainId = "c4.sculpt.gain";
static constexpr const char* kParamSculptQId    = "c4.sculpt.q";
static constexpr const char* kParamBiteFreqId   = "c4.bite.freq";
static constexpr const char* kParamBiteGainId   = "c4.bite.gain";
static constexpr const char* kParamBiteQId      = "c4.bite.q";
static constexpr const char* kParamOpenFreqId   = "c4.open.freq";
static constexpr const char* kParamOpenGainId   = "c4.open.gain";
static constexpr const char* kParamOpenModeId   = "c4.open.mode";   // 0=Bell 1=HALO
static constexpr const char* kParamHpfId        = "c4.hpf";         // 0=OFF, (0,1] -> 20..1500 Hz
static constexpr const char* kParamLpfId        = "c4.lpf";         // 0=OFF, (0,1] -> 1.5..24 kHz
static constexpr const char* kParamBloomId      = "c4.bloom";       // 0..10 continuous
static constexpr const char* kParamInputId      = "c4.input";       // -18..+18 dB
static constexpr const char* kParamOutputId     = "c4.output";      // -18..+18 dB
static constexpr const char* kParamAutoGainId   = "c4.autogain";    // OFF by default
static constexpr const char* kParamBypassId     = "c4.bypass";

// ---- Public value ranges (fixed product contract) -------------------------
static constexpr float kGainMinDb  = -15.0f;
static constexpr float kGainMaxDb  = +15.0f;
static constexpr float kGainDefaultDb = 0.0f;
static constexpr float kTrimMinDb  = -18.0f;
static constexpr float kTrimMaxDb  = +18.0f;
static constexpr float kTrimDefaultDb = 0.0f;

// Band frequency ranges (Hz).
static constexpr float kWeightFreqMinHz = 30.0f;
static constexpr float kWeightFreqMaxHz = 450.0f;
static constexpr float kSculptFreqMinHz = 120.0f;
static constexpr float kSculptFreqMaxHz = 2500.0f;
static constexpr float kBiteFreqMinHz   = 400.0f;
static constexpr float kBiteFreqMaxHz   = 9000.0f;
static constexpr float kOpenFreqMinHz   = 1500.0f;
static constexpr float kOpenFreqMaxHz   = 20000.0f;

// HALO shelf: control range extends beyond the audio range on purpose.
// The C4HaloShelfMapper translates high control values into a realizable
// in-band response (see C4HaloShelfMapper.h). Never a raw 40 kHz filter.
static constexpr float kHaloFreqMinHz = 2500.0f;
static constexpr float kHaloFreqMaxHz = 40000.0f;

// User Q ranges.
static constexpr float kSculptQMin = 0.50f;
static constexpr float kSculptQMax = 2.00f;
static constexpr float kBiteQMin   = 0.60f;
static constexpr float kBiteQMax   = 3.00f;

// HPF / LPF ranges.
static constexpr float kHpfMinHz = 20.0f;
static constexpr float kHpfMaxHz = 1500.0f;
static constexpr float kLpfMinHz = 1500.0f;
static constexpr float kLpfMaxHz = 24000.0f;

// BLOOM control (user-facing 0..10, continuous; the production default is a
// Phase 5 FROZEN design decision — see C4TuningProfile::makeProfileProduction
// and docs/C4_PHASE5_PERCEPTUAL_TUNING.md. 4.0 was chosen, not 5.0: it lands
// the default coloration in the measured "subtle but recognizable" band on
// program material while leaving headroom; see the Phase 5 evidence).
static constexpr float kBloomMin = 0.0f;
static constexpr float kBloomMax = 10.0f;
static constexpr float kBloomDefault = 4.0f;

// State schema version.
// v1 = the initial 19-parameter C4. Missing properties restore to defaults.
static constexpr int kStateVersion = 1;

// ============================================================================
// Band identity table.
// ============================================================================

enum class C4BandId
{
    Weight = 0,
    Sculpt = 1,
    Bite   = 2,
    Open   = 3
};

enum class C4BandMode
{
    Bell  = 0,
    Shelf = 1
};

// Shelf polarity per band: WEIGHT uses a LOW shelf, OPEN/HALO a HIGH shelf.
enum class C4ShelfKind
{
    LowShelf,
    HighShelf
};

struct C4BandInfo
{
    const char* paramFreqId;
    const char* paramGainId;
    const char* musicalName;
    float freqMinHz;
    float freqMaxHz;
    float defaultFreqHz;
    C4ShelfKind shelfKind;
};

static constexpr C4BandInfo kBandInfos[kNumBands] =
{
    { kParamWeightFreqId, kParamWeightGainId, "WEIGHT", kWeightFreqMinHz, kWeightFreqMaxHz, 100.0f,  C4ShelfKind::LowShelf  },
    { kParamSculptFreqId, kParamSculptGainId, "SCULPT", kSculptFreqMinHz, kSculptFreqMaxHz, 400.0f,  C4ShelfKind::LowShelf  },
    { kParamBiteFreqId,   kParamBiteGainId,   "BITE",   kBiteFreqMinHz,   kBiteFreqMaxHz,   2500.0f, C4ShelfKind::LowShelf  },
    { kParamOpenFreqId,   kParamOpenGainId,   "OPEN",   kOpenFreqMinHz,   kOpenFreqMaxHz,   10000.0f,C4ShelfKind::HighShelf }
};

// ============================================================================
// Parameter index layout (stable ordering for the processor parameter list).
// ============================================================================

enum C4ParamIndex
{
    kWeightFreq = 0,
    kWeightGain = 1,
    kWeightMode = 2,
    kSculptFreq = 3,
    kSculptGain = 4,
    kSculptQ    = 5,
    kBiteFreq   = 6,
    kBiteGain   = 7,
    kBiteQ      = 8,
    kOpenFreq   = 9,
    kOpenGain   = 10,
    kOpenMode   = 11,
    kHpf        = 12,
    kLpf        = 13,
    kBloom      = 14,
    kInput      = 15,
    kOutput     = 16,
    kAutoGain   = 17,
    kBypass     = 18,
    kNumParams  = 19
};

// ============================================================================
// dB / level helpers (shared, allocation-free, realtime-safe).
// ============================================================================

inline float dbToGain (float db) noexcept
{
    return std::exp2 (db * 0.16609640474436813f); // 2^(db * log2(10)/20)
}

inline float gainToDb (float gain) noexcept
{
    return 20.0f * std::log10 (juce::jmax (1e-30f, gain));
}

inline float clampDb (float db) noexcept
{
    return juce::jlimit (kGainMinDb, kGainMaxDb, db);
}

} // namespace C4
} // namespace APEX

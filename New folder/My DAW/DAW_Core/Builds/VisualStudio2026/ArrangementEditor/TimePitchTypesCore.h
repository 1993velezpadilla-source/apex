// ===========================================================================
// TimePitchTypesCore.h
// Enums, structs y constantes del sistema Pitch / Time Stretch.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   Centraliza todos los tipos compartidos entre los otros núcleos.
//   Si necesitas agregar un modo nuevo, solo tocas este archivo.
//   Ningún otro núcleo necesita conocer los detalles de los demás.
// ===========================================================================
#pragma once
#include <cmath>
#include <string>
#include "VoiceTransformTypesCore.h"
#include "TimePitchQualityCore.h"

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// Modos de stretch/pitch — tal como lo hacen los DAWs profesionales
// ---------------------------------------------------------------------------
enum class TimePitchMode
{
    // RESAMPLE — tape/vinyl/DJ mode.
    // Pitch y tiempo están unidos: subir pitch acorta el clip.
    // playbackRate = pow(2, semitones/12)
    // Igual a cómo funciona un vinilo o cinta analógica.
    // CPU mínimo, sin artefactos de algoritmo, sonido crudo/natural.
    Resample = 0,

    // STRETCH — duración independiente.
    // Cambia duración sin afectar pitch.
    // Usa phase vocoder o similar internamente.
    // Ideal para loops de batería, samples de música.
    Stretch,

    // PITCH_ONLY — pitch independiente.
    // Cambia pitch sin afectar duración.
    // Conceptualmente: pitch-shift + stretch compensatorio.
    PitchOnly,

    // VOCAL — voces con preservación de formants.
    // Cambia pitch manteniendo el timbre humano.
    // Sin esto: pitch alto = ardilla, pitch bajo = demonio.
    // Con esto: la identidad vocal se mantiene.
    Vocal,

    // PERCUSSION — protege transients.
    // Para drums, kicks, snares.
    // Evita smear en los ataques.
    Percussion,

    // TEXTURE — granular / experimental.
    // Stretch extremo con artefactos creativos.
    // Divide audio en granos pequeños.
    Texture,

    // OFFLINE_HQ — render de alta calidad en background.
    // No se usa en tiempo real; se renderiza antes del playback.
    OfflineHQ
};

namespace TimePitchModeIds
{
    static constexpr int Resample        = static_cast<int>(TimePitchMode::Resample);
    static constexpr int Stretch         = static_cast<int>(TimePitchMode::Stretch);
    static constexpr int PitchOnly       = static_cast<int>(TimePitchMode::PitchOnly);
    static constexpr int Vocal           = static_cast<int>(TimePitchMode::Vocal);
    static constexpr int Percussion      = static_cast<int>(TimePitchMode::Percussion);
    static constexpr int Texture         = static_cast<int>(TimePitchMode::Texture);
    static constexpr int OfflineHQ       = static_cast<int>(TimePitchMode::OfflineHQ);
    static constexpr int DefaultUserMode = Vocal;
}

// ---------------------------------------------------------------------------
// Número de modos — útil para UI dropdowns y validación
// ---------------------------------------------------------------------------
static constexpr int kTimePitchModeCount = 7;

// ---------------------------------------------------------------------------
// Rangos y constantes de los knobs
// ---------------------------------------------------------------------------
struct TimePitchConstants
{
    // Pitch knob
    static constexpr double kPitchMinSemitones  = -36.0;
    static constexpr double kPitchMaxSemitones  = +36.0;
    static constexpr double kPitchAudibleMinSemitones = -48.0;
    static constexpr double kPitchAudibleMaxSemitones = +48.0;
    static constexpr double kPitchDefaultSt     =   0.0;
    static constexpr double kPitchStepFine      =   0.01;  // 1 cent
    static constexpr double kPitchStepNormal    =   0.1;
    static constexpr double kPitchStepMusical   =   1.0;   // 1 semitono

    // Fine tune knob (cents)
    static constexpr double kFineTuneMinCents   = -100.0;
    static constexpr double kFineTuneMaxCents   = +100.0;
    static constexpr double kFineTuneDefault    =    0.0;

    // Time stretch knob (porcentaje)
    static constexpr double kStretchMinPercent  =  25.0;   // 1/4 de duración
    static constexpr double kStretchMaxPercent  = 400.0;   // 4x duración
    static constexpr double kStretchDefault     = 100.0;   // original

    // Time stretch ratio equivalente
    static constexpr double kStretchMinRatio    =  0.25;
    static constexpr double kStretchMaxRatio    =  4.0;
    static constexpr double kStretchDefaultRatio = 1.0;

    // Formant knob (semitonos)
    static constexpr double kFormantMinSemitones = -12.0;
    static constexpr double kFormantMaxSemitones = +12.0;
    static constexpr double kFormantDefault      =   0.0;
};

// ---------------------------------------------------------------------------
// Estado completo de pitch/time de un clip
// ---------------------------------------------------------------------------
struct TimePitchState
{
    // --- Pitch ---
    // En semitonos: -36..+36 UI, con headroom interno -48..+48 para legacy/import.
    // 0 = sin cambio.
    double pitchSemitones = 0.0;

    // Serialization/DSP migration version. v0 absent legacy, v1 stores semitones.
    int pitchEngineVersion = 1;

    // Fine tune en cents: -100..+100.
    // 100 cents = 1 semitono.
    double fineTuneCents = 0.0;

    // --- Time stretch ---
    // 1.0 = duración original.
    // 2.0 = doble de duración.
    // 0.5 = mitad de duración.
    double stretchRatio = 1.0;

    // --- Formant ---
    // Solo activo en modo Vocal.
    // 0 = preservar formants originales.
    // Positivo = voz más pequeña/nasal.
    // Negativo = voz más gruesa/adulta.
    double formantSemitones = 0.0;

    // --- Modo activo ---
    // Default to Resample (basic mode): pitch and rate are linked.
    // This avoids accidental time-stretch on imported audio.
    // User can change to Vocal/Stretch/etc per-clip as needed.
    TimePitchMode mode = TimePitchMode::Resample;

    // --- Flags ---
    // En modo Vocal: true = mantener timbre, false = efecto ardilla/demonio
    bool preserveFormants = false;

    // En modo Percussion: protege ataques
    bool transientPreserve = false;
    bool transientLock = false;
    TimePitchQuality quality = TimePitchQuality::Balanced;

    // En modo OfflineHQ: render completo antes del playback
    bool highQuality = false;

    // --- Voice Transform ---
    // One-knob vocal character engine. Pitch remains duration-independent;
    // stretch remains pitch-independent; formant changes do not affect timing.
    VoiceTransformState voiceTransform;

    // --- Info del source ---
    int    sourceSampleRate = 44100;
    double sourceBPM        = 0.0;   // 0 = desconocido
    double projectBPM       = 120.0;

    // ---------------------------------------------------------------------------
    // Helpers de estado
    // ---------------------------------------------------------------------------

    bool isPitchActive() const
    {
        return std::abs(pitchSemitones) > 0.001
            || std::abs(fineTuneCents) > 0.1
            || voiceTransform.isActive();
    }

    bool isStretchActive() const
    {
        return std::abs(stretchRatio - 1.0) > 0.001;
    }

    bool isFormantActive() const
    {
        return std::abs(formantSemitones) > 0.001;
    }

    bool isIdentity() const
    {
        return !isPitchActive() && !isStretchActive() && !isFormantActive()
            && !voiceTransform.isActive();
    }

    // Devuelve el total de semitones incluyendo fine tune
    double totalPitchSemitones() const
    {
        return pitchSemitones + fineTuneCents / 100.0;
    }
};

// ---------------------------------------------------------------------------
// Cache key — identifica de forma única el estado procesado de un clip
// Usado por TimePitchRenderCacheCore para invalidar cuando algo cambia.
// ---------------------------------------------------------------------------
struct TimePitchCacheKey
{
    std::string  sourcePath;
    int64_t      sourceStartSample  = 0;
    int64_t      sourceEndSample    = 0;
    double       pitchSemitones     = 0.0;
    double       fineTuneCents      = 0.0;
    double       stretchRatio       = 1.0;
    double       formantSemitones   = 0.0;
    TimePitchMode mode              = TimePitchMode::Resample;
    bool         preserveFormants   = false;
    int          sampleRate         = 44100;

    bool operator==(const TimePitchCacheKey& o) const
    {
        return sourcePath       == o.sourcePath
            && sourceStartSample == o.sourceStartSample
            && sourceEndSample   == o.sourceEndSample
            && std::abs(pitchSemitones - o.pitchSemitones)   < 0.0001
            && std::abs(fineTuneCents  - o.fineTuneCents)    < 0.01
            && std::abs(stretchRatio   - o.stretchRatio)     < 0.0001
            && std::abs(formantSemitones - o.formantSemitones) < 0.0001
            && mode              == o.mode
            && preserveFormants  == o.preserveFormants
            && sampleRate        == o.sampleRate;
    }

    bool operator!=(const TimePitchCacheKey& o) const { return !(*this == o); }
};

} // namespace ArrangementEditor

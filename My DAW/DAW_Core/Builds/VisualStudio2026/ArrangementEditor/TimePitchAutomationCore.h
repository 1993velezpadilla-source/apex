// ===========================================================================
// TimePitchAutomationCore.h
// Parámetros automatizables de pitch / time stretch.
//
// POR QUÉ EXISTE:
//   DAWs pro permiten automatizar pitch, stretch, formant frame-by-frame.
//   Este núcleo define los parámetros como juce::AudioParameterFloat/Choice
//   para que el sistema de automatización del DAW los lea.
//
// ARQUITECTURA:
//   TimePitchAutomationCore no procesa audio.
//   Solo define y expone los parámetros.
//   El AudioEngine lee getAutomatedState(time) para cada bloque.
//
// FLUJO FUTURO:
//   1. User dibuja curva de pitch en automation lane.
//   2. AutomationEngine evalúa curva en tiempo de playback.
//   3. Llama timePitchAuto.setAutomatedPitch(time, value).
//   4. AudioEngine llama getAutomatedState() → DSP aplica valor.
//
// ESTADO ACTUAL:
//   Estructura lista. Automation lanes no implementadas todavía.
//   Los valores default pasan el estado estático normal.
//   Conectar AutomationEngine aquí cuando esté listo.
// ===========================================================================
#pragma once
#include "TimePitchTypesCore.h"
#include <JuceHeader.h>
#include <atomic>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// Parámetro ID constants — usar estos strings en el automation engine
// ---------------------------------------------------------------------------
namespace TimePitchParamIDs
{
    static const juce::String Pitch        = "timepitch_pitch_st";
    static const juce::String FineTune     = "timepitch_finetune_cents";
    static const juce::String Stretch      = "timepitch_stretch_ratio";
    static const juce::String Formant      = "timepitch_formant_st";
    static const juce::String Mode         = "timepitch_mode";
    static const juce::String PreserveForm = "timepitch_preserve_formants";
}

// ---------------------------------------------------------------------------
// TimePitchAutomationCore
// ---------------------------------------------------------------------------
class TimePitchAutomationCore
{
public:
    TimePitchAutomationCore() = default;

    // -----------------------------------------------------------------------
    // Rango de cada parámetro (para UI sliders, automation lanes)
    // -----------------------------------------------------------------------
    struct ParamRange
    {
        float minVal;
        float maxVal;
        float defaultVal;
        float step;       // 0 = continuous
        const char* unit;
        const char* name;
    };

    static ParamRange pitchRange()
    {
        return { -24.f, 24.f, 0.f, 0.f, "st", "Pitch" };
    }
    static ParamRange fineTuneRange()
    {
        return { -100.f, 100.f, 0.f, 0.f, "ct", "Fine Tune" };
    }
    static ParamRange stretchRange()
    {
        // 10% … 400%, default 100% = 1.0 ratio
        return { 0.1f, 4.f, 1.f, 0.f, "x", "Stretch" };
    }
    static ParamRange formantRange()
    {
        return { -12.f, 12.f, 0.f, 0.f, "st", "Formant" };
    }

    // -----------------------------------------------------------------------
    // Automated state override.
    // Called from automation playback engine (message thread) before each block.
    // AudioEngine reads this on audio thread via atomic snapshot.
    // -----------------------------------------------------------------------
    void setAutomatedPitch(double semitones)
    {
        automatedPitch_.store(semitones);
        hasAutomation_.store(true);
    }
    void setAutomatedStretch(double ratio)
    {
        automatedStretch_.store(juce::jlimit(0.1, 4.0, ratio));
        hasAutomation_.store(true);
    }
    void setAutomatedFormant(double semitones)
    {
        automatedFormant_.store(semitones);
        hasAutomation_.store(true);
    }
    void clearAutomation()
    {
        hasAutomation_.store(false);
    }

    // -----------------------------------------------------------------------
    // getAutomatedState — audio thread reads this.
    // Merges base state with any live automation values.
    // -----------------------------------------------------------------------
    TimePitchState getAutomatedState(const TimePitchState& baseState) const
    {
        if (!hasAutomation_.load()) return baseState;

        TimePitchState result = baseState;
        result.pitchSemitones  = automatedPitch_.load();
        result.stretchRatio    = automatedStretch_.load();
        result.formantSemitones= automatedFormant_.load();
        return result;
    }

    bool hasActiveAutomation() const { return hasAutomation_.load(); }

private:
    std::atomic<double> automatedPitch_   { 0.0 };
    std::atomic<double> automatedStretch_ { 1.0 };
    std::atomic<double> automatedFormant_ { 0.0 };
    std::atomic<bool>   hasAutomation_    { false };
};

} // namespace ArrangementEditor

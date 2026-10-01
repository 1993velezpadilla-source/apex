// ===========================================================================
// ClipTimePitchBridgeCore.h
// Sincroniza el TimePitchState de ArrangementClipModel con DAW::AudioClip.
//
// POR QUÉ EXISTE ESTE NÚCLEO:
//   El DAW tiene dos representaciones de un clip:
//
//   1. ArrangementClipModel (ArrangementEditor namespace)
//      El modelo UI: lo que el usuario ve, edita y guarda en el proyecto.
//      Contiene `TimePitchState timePitch` con todos los parámetros pro.
//
//   2. DAW::AudioClip (DAW namespace)
//      El modelo de audio engine: lo que renderClip() lee para playback.
//      Tiene campos planos: pitch_, timeStretch_, fineTuneCents_, etc.
//
//   Este núcleo propaga los cambios de uno al otro.
//   Se llama desde el UI/message thread cuando el usuario cambia un knob.
//   El AudioEngine lee los campos de DAW::AudioClip en el audio thread.
//
// THREAD SAFETY:
//   Llamar SOLO desde el message thread (nunca desde audio thread).
//   El audio thread lee los campos atómicos de AudioClip directamente.
//
// FLUJO COMPLETO:
//   User drags pitch knob
//     → ClipPropertiesPanelCore::sliderValueChanged()
//     → onModelChanged(ArrangementClipModel)
//     → ArrangementClipStateCore::updateClip()
//     → ClipTimePitchBridgeCore::pushToAudioClip()  ← aquí
//     → DAW::AudioClip::setPitch() etc.
//     → AudioEngine::renderClip() lee los nuevos valores
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "../Source/ClipCore/Clip.h"

namespace DAW { class ClipManager; }

namespace ArrangementEditor
{

class ClipTimePitchBridgeCore
{
public:
    // -----------------------------------------------------------------------
    // Aplica el TimePitchState de un ArrangementClipModel al DAW::AudioClip
    // correspondiente (buscado por nombre/ID en el ClipManager).
    //
    // Parámetros:
    //   model      — el ArrangementClipModel recién actualizado
    //   audioClip  — el DAW::AudioClip que renderClip() usa
    //
    // Solo propaga los campos que cambiaron; el resto queda intacto.
    // -----------------------------------------------------------------------
    static void pushToAudioClip(const ArrangementClipModel& model,
                                 DAW::AudioClip* audioClip)
    {
        if (!audioClip) return;

        const TimePitchState& tp = model.timePitch;
        DBG("[PITCH WRITE] source=LoadState value=" << (float) tp.pitchSemitones);

        // Pitch en semitonos → campo pitch_ de AudioClip (usado por renderClip)
        audioClip->setPitchTargetSemitones((float)tp.pitchSemitones);

        // Fine tune en cents
        audioClip->setFineTuneCents((float)tp.fineTuneCents);

        // Stretch ratio → campo timeStretch_
        audioClip->setTimeStretch((float)juce::jlimit(
            0.01, 4.0, tp.stretchRatio));

        // Formant
        audioClip->setFormantSemitones((float)tp.formantSemitones);

        // Preserve formants
        audioClip->setPreserveFormants(tp.preserveFormants);

        DBG("[DEMON SYNC] source=ClipTimePitchBridge value=" << tp.voiceTransform.demonAmount);

        // Modo (0=Resample … 6=OfflineHQ)
        audioClip->setTimePitchMode(static_cast<int>(tp.mode));

        // Source bounds en samples (para split/slip)
        audioClip->setSourceStartSample(model.sourceStartSample);
        audioClip->setSourceEndSample(model.sourceEndSample);
    }

    // -----------------------------------------------------------------------
    // Lee el estado actual de un DAW::AudioClip y lo escribe en el
    // TimePitchState del ArrangementClipModel.
    // Útil al cargar un proyecto o al crear un clip desde el audio engine.
    // -----------------------------------------------------------------------
    static void pullFromAudioClip(ArrangementClipModel& model,
                                   const DAW::AudioClip* audioClip)
    {
        if (!audioClip) return;

        TimePitchState& tp = model.timePitch;
        tp.pitchSemitones   = audioClip->getPitch();
        tp.fineTuneCents    = audioClip->getFineTuneCents();
        tp.stretchRatio     = audioClip->getTimeStretch();
        tp.formantSemitones = audioClip->getFormantSemitones();
        tp.preserveFormants = audioClip->getPreserveFormants();
        tp.mode             = static_cast<TimePitchMode>(audioClip->getTimePitchMode());

        model.sourceStartSample = audioClip->getSourceStartSample();
        model.sourceEndSample   = audioClip->getSourceEndSample();

        // Sync legacy fields para backward compat
        model.pitch = (float)tp.pitchSemitones;
        model.rate  = (float)tp.stretchRatio;
    }
};

} // namespace ArrangementEditor

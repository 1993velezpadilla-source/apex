#pragma once
#include <JuceHeader.h>
#include "BubblegumInfoPopupCore.h"

namespace DAW {

/**
 * MasterStripFeatureInfoCatalog
 *
 * Static catalog of "what is this and how does it work" info strings for
 * every feature exposed in the master strip. Used by the ? buttons in each
 * section to show a BubblegumInfoPopup with consistent bilingual text.
 */
class MasterStripFeatureInfoCatalog
{
public:
    static BubblegumInfoPopup::Info ceiling()
    {
        return {
            "Ceiling",
            "El Ceiling es el techo maximo que tu senal puede alcanzar antes de salir del DAW. "
            "Si el audio intenta pasar este limite, se aplica el modo seleccionado:\n\n"
            "- Off: sin limite, puede pasar de 0 dBFS y clipear.\n"
            "- Hard Clip: corta abruptamente al limite (distorsion digital).\n"
            "- Soft Clip: redondea suavemente con tanh (saturacion musical).\n"
            "- Lookahead: limitador transparente con 5ms de anticipacion.\n\n"
            "Recomendado: Lookahead a -0.3 dB para masters comerciales; evita intersample peaks.\n\n"
            "-----\n\n"
            "Ceiling is the maximum level your signal can reach before leaving the DAW. "
            "If audio tries to exceed this limit, the selected mode applies. Recommended: "
            "Lookahead at -0.3 dB for commercial masters.",
            true
        };
    }

    static BubblegumInfoPopup::Info dither()
    {
        return {
            "Dither",
            "Dither es ruido aleatorio muy bajo que se anade ANTES de reducir el bit depth. "
            "Sin dither, el redondeo crea distorsion correlacionada con la senal.\n\n"
            "- Off: no anadir dither.\n"
            "- TPDF: ruido triangular estandar, transparente.\n"
            "- Noise Shaped: empuja el ruido hacia frecuencias agudas.\n\n"
            "Recomendado: TPDF a 16-bit cuando exportes a CD/streaming. A 24-bit casi no hace diferencia audible.\n\n"
            "-----\n\n"
            "Dither is very low random noise added BEFORE reducing bit depth. Without dither, "
            "rounding creates distortion correlated with the signal. TPDF is the standard safe choice.",
            true
        };
    }

    static BubblegumInfoPopup::Info metering()
    {
        return {
            "Metering Suite",
            "Mediciones de nivel POST-fader del master. NO afectan el audio, solo lo miden:\n\n"
            "- PEAK: nivel pico instantaneo.\n"
            "- TRUE PK: pico real reconstruido, incluyendo intersample peaks.\n"
            "- RMS: nivel promedio de energia.\n\n"
            "Si TRUE PK pasa de 0 dB, tu master puede clipear al convertirse a MP3/AAC/streaming.\n\n"
            "-----\n\n"
            "POST-fader level metering of the master. It does NOT affect audio; it only measures it. "
            "True peak is important for codec and streaming safety.",
            false
        };
    }

    static BubblegumInfoPopup::Info lufs()
    {
        return {
            "LUFS",
            "LUFS mide la sonoridad PERCIBIDA segun el oido humano, no solo amplitud electrica.\n\n"
            "- M (Momentary): ultimos 400 ms.\n"
            "- S (Short-term): ultimos 3 segundos.\n"
            "- I (Integrated): toda la cancion, valor usado por plataformas.\n\n"
            "Targets tipicos: Spotify/YouTube -14 LUFS, Apple Music -16 LUFS, club/festival alrededor de -8 LUFS.\n\n"
            "-----\n\n"
            "LUFS measures perceived loudness. Integrated LUFS is the full-song value used by streaming platforms.",
            false
        };
    }

    static BubblegumInfoPopup::Info phaseWidth()
    {
        return {
            "Phase / Width",
            "Diagnostico estereo del master:\n\n"
            "Goniometro: muestra la relacion L/R en tiempo real. Inclinado a la izquierda indica problema de fase.\n\n"
            "Correlation: -1 a +1. Valores bajo 0 indican posible cancelacion en mono.\n\n"
            "Width: 0%-100%. 30-70% suele ser rango comercial sano; mas de 85% puede traducir mal.\n\n"
            "-----\n\n"
            "Stereo diagnosis of the master: goniometer, correlation and perceived width. "
            "Negative correlation means mono compatibility risk.",
            false
        };
    }

    static BubblegumInfoPopup::Info monoCheck()
    {
        return {
            "Mono Check",
            "Suma L+R audible para verificar como suena tu mix en sistemas MONO.\n\n"
            "Workflow:\n"
            "1. Pon tu mix sonando.\n"
            "2. Click MONO.\n"
            "3. Escucha que se pierde. Si algo desaparece, hay problema de fase.\n"
            "4. Click MONO otra vez para volver a estereo.\n\n"
            "IMPORTANTE: solo afecta lo que ESCUCHAS. El render exportado NO se modifica.\n\n"
            "-----\n\n"
            "Audible L+R sum for mono compatibility checking. It affects monitoring only, not export.",
            false
        };
    }

    static BubblegumInfoPopup::Info speakerSet()
    {
        return {
            "Speaker Set",
            "Selector de set de monitores activos. Util cuando tienes varios pares de monitores conectados.\n\n"
            "- A/B/C: presets configurados en el Control Room.\n"
            "- M: mono check global o monitor mono.\n\n"
            "Cambiar entre sets permite escuchar tu mix en distintos sistemas sin tocar conexiones fisicas.\n\n"
            "-----\n\n"
            "Active monitor speaker set selector. Useful for comparing the mix across different monitoring systems.",
            false
        };
    }

private:
    MasterStripFeatureInfoCatalog() = delete;
};

} // namespace DAW

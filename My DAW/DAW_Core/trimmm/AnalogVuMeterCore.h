#pragma once

/**
 * AnalogVuMeterCore — master include for the analog VU meter family.
 *
 * The VU meter is broken into nine focused nucleos. Three carry state /
 * logic; six are stateless paint helpers. The Component that hosts them all
 * is built in a separate batch (AnalogVuMeterComponent, delivered with the
 * panel rebuild).
 *
 * State / logic:
 *   AnalogVuBallisticsCore       — VU/PPM smoothing + peak hold
 *   AnalogVuScaleCore            — dB <-> angle math + tick table
 *   AnalogVuOverloadCore         — OL latch state + worst overshoot
 *
 * Renderers (each one paints exactly one visual concern):
 *   AnalogVuCreamFaceRenderer    — cream rectangle with bevel + highlight
 *   AnalogVuRedZoneRenderer      — red overload band arc
 *   AnalogVuScaleMarkingsRenderer — baseline arc + ticks + numeric labels
 *   AnalogVuLogoRenderer         — italic VU logo + mode subtitle + corner tag
 *   AnalogVuOverloadRenderer     — OL bulb + label, reads OverloadCore state
 *   AnalogVuNeedleRenderer       — tapered needle + pivot cap
 *
 * Per-track uniqueness: none of these cores own audio data. They process
 * whatever's fed in. The hosting Component holds a pointer to the track's
 * InputMeterCore and calls feed() every UI tick. When the panel rebinds to
 * a different track, the Component calls resetAll() / reset() on the
 * stateful cores so smoothing and latch state do not bleed across tracks.
 */

#include "AnalogVuBallisticsCore.h"
#include "AnalogVuScaleCore.h"
#include "AnalogVuOverloadCore.h"
#include "AnalogVuCreamFaceRenderer.h"
#include "AnalogVuRedZoneRenderer.h"
#include "AnalogVuScaleMarkingsRenderer.h"
#include "AnalogVuLogoRenderer.h"
#include "AnalogVuOverloadRenderer.h"
#include "AnalogVuNeedleRenderer.h"

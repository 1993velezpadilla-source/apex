#pragma once

/**
 * BubblegumKnobCore — master include for the Bubblegum knob family.
 *
 * The knob is broken into seven focused nucleos plus this aggregator:
 *
 * Logic / state:
 *   BubblegumKnobValueBinding      — std::function glue (per-track unique value)
 *   BubblegumKnobInteractionCore   — drag math, fine mode, snap, reset
 *   BubblegumKnobScaleCore         — value <-> angle (linear or bipolar-unity)
 *
 * Renderers (each paints exactly one visual concern):
 *   BubblegumKnobHaloRenderer      — pink glow rings behind the body
 *   BubblegumKnobTrackRenderer     — outer track + tick dots + active arc
 *   BubblegumKnobBodyRenderer      — metallic body, bezel ring, gloss, cap
 *   BubblegumKnobIndicatorRenderer — rotating pink pointer
 *
 * Per-track uniqueness: the knob never owns a value. It always reads through
 * the BubblegumKnobValueBinding supplied to it by the panel. Each panel
 * instance constructs a fresh binding whose lambdas close over a specific
 * track's parameter — track A and track B can never see each other's value.
 *
 * Painting order (in the hosting Component):
 *   1. Halo
 *   2. Track (incl. active arc)
 *   3. Body (incl. gloss, bezel, center cap)
 *   4. Indicator
 *
 * Interaction order (in the hosting Component):
 *   - mouseDown    -> InteractionCore.beginDrag(binding.read(), e.getScreenY())
 *   - mouseDrag    -> binding.write(InteractionCore.computeDragValue(...))
 *   - mouseUp      -> InteractionCore.endDrag()
 *   - mouseDoubleClick -> binding.write(binding.defaultValue)
 */

#include "BubblegumKnobValueBinding.h"
#include "BubblegumKnobInteractionCore.h"
#include "BubblegumKnobScaleCore.h"
#include "BubblegumKnobHaloRenderer.h"
#include "BubblegumKnobTrackRenderer.h"
#include "BubblegumKnobBodyRenderer.h"
#include "BubblegumKnobIndicatorRenderer.h"

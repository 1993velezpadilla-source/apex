#pragma once

/**
 * BubblegumFloatingPanelCore — master include for the reusable building
 * blocks of any Bubblegum-themed floating panel (header bar, toggle switch,
 * readout box).
 *
 * These widgets are intentionally small, single-concern, and panel-agnostic
 * so any future panel (EQ, compressor, send level editor, etc.) can drop
 * them in. Specific panels (e.g. InputTrimFloatingPanel) compose them with
 * domain-specific Components like AnalogVuMeterComponent and
 * BubblegumKnobComponent.
 *
 * Member files:
 *   BubblegumPanelHeaderCore     - title + subtitle + minimize + close bar
 *   BubblegumPanelToggleSwitch   - pill-shaped on/off toggle with label + dot
 *   BubblegumPanelReadoutBox     - label-on-top, big-value-below readout box
 */

#include "BubblegumPanelHeaderCore.h"
#include "BubblegumPanelToggleSwitch.h"
#include "BubblegumPanelReadoutBox.h"

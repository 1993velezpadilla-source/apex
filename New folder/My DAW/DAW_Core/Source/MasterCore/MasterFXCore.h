#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * MasterFXCore — master bus insert effects chain management.
 *
 * Manages the master bus FX chain state: slot count, bypass,
 * ordering. The actual DSP is in MasterInsertChain; this core
 * manages the logical state and slot bookkeeping.
 *
 * Master FX process the FULL final mix before fader and render tap.
 */
class MasterFXCore
{
public:
    int getSlotCount() const noexcept { return slotCount_; }
    void setSlotCount(int count) noexcept { slotCount_ = count; }

    bool isBypassed() const noexcept { return bypassed_; }
    void setBypassed(bool bypassed) noexcept { bypassed_ = bypassed; }

    bool isEmpty() const noexcept { return slotCount_ == 0; }

private:
    int slotCount_ = 0;
    bool bypassed_ = false;
};

} // namespace DAW

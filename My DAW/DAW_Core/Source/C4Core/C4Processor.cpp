#include "C4Processor.h"
#include "../C4UI/C4Editor.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4Processor::createEditor — implemented here (not in the header) so the
// header never needs to include GUI code, keeping the DSP translation unit
// free of GUI dependencies.
//
// Phase 7: the final APEX C4 console interface lives in Source/C4UI/C4Editor.h.
// ============================================================================

juce::AudioProcessorEditor* C4Processor::createEditor()
{
    return new APEX::C4::C4Editor (*this);
}

} // namespace C4
} // namespace APEX

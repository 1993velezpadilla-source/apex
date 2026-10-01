#include "G10Processor.h"
#include "../G10UI/G10Editor.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10Processor::createEditor — implemented here (not in the header) so the
// header never needs to include the editor, keeping the DSP translation unit
// free of GUI dependencies.
//
// The returned editor is owned by the host (PluginEditorWindow /
// SafeEditorHost) and is created/destroyed on the message thread only.
// ============================================================================

juce::AudioProcessorEditor* G10Processor::createEditor()
{
    // Visibility, not mere construction, owns analyzer production. The editor
    // resets analyzer-only FIFO/state before enabling this gate when shown.
    return new G10Editor (*this);
}

} // namespace G10
} // namespace APEX

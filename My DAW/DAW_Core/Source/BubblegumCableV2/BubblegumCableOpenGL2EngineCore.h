#pragma once
#include <JuceHeader.h>
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"
#include "BubblegumCableFallbackRenderCore.h"

namespace DAW::BgV2 {

class BubblegumCableOpenGL2EngineCore {
public:
    bool isReady() const noexcept { return false; }
    void renderCable(const CableGeometry& geo, const CableInput& in,
                     const MaterialPalette& pal, float time) {
        juce::ignoreUnused(geo, in, pal, time);
    }
    static constexpr const char* kVertSrc = "/* Phase 3 */";
    static constexpr const char* kFragSrc = "/* Phase 3 */";
};

inline void BubblegumCableFallbackRenderCore::paintViaGL(
    juce::Graphics& g, const CableGeometry& geo, const CableInput& in,
    const MaterialPalette& pal, float time) {
    if (gl_ != nullptr && gl_->isReady())
        gl_->renderCable(geo, in, pal, time);
    juce::ignoreUnused(g);
}

} // namespace DAW::BgV2
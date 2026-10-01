#pragma once
#include <JuceHeader.h>
#include "BubblegumCableTypesV2.h"
#include "BubblegumCableMaterialCore.h"

namespace DAW::BgV2 {

class BubblegumCableOpenGL2EngineCore;  // forward decl

class BubblegumCableFallbackRenderCore {
public:
    void setGLEngine(BubblegumCableOpenGL2EngineCore* gl) noexcept { gl_ = gl; }
    void setForceCPU(bool force) noexcept { forceCpu_ = force; }

    bool preferOpenGL() const noexcept {
        if (forceCpu_ || gl_ == nullptr) return false;
        // OpenGL context detection deferred to Phase 3
        return false;
    }

    void paintViaGL(juce::Graphics& g, const CableGeometry& geo,
                    const CableInput& in, const MaterialPalette& pal, float time);

private:
    BubblegumCableOpenGL2EngineCore* gl_ = nullptr;
    bool forceCpu_ = false;
};

} // namespace DAW::BgV2

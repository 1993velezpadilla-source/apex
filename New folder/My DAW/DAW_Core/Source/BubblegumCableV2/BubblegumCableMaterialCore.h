#pragma once
#include <JuceHeader.h>

namespace DAW::BgV2 {

struct MaterialPalette {
    juce::Colour albedo      { 0xFFEE4FA0 };
    juce::Colour specPeak    { 0xFFFFF8FC };
    juce::Colour specHalo    { 0xFFFFD9EC };
    juce::Colour sssWarm     { 0xFFFFB8DC };
    juce::Colour sssCool     { 0xFFF5E8FF };
    juce::Colour litTop      { 0xFFFFD6EC };
    juce::Colour midLit      { 0xFFFF9AC8 };
    juce::Colour shadowMid   { 0xFF8C1848 };
    juce::Colour shadowDeep  { 0xFF2A0612 };
    juce::Colour aoContact   { 0xFF120308 };
    juce::Colour sheenTop    { 0xFFFFEEF8 };
    juce::Colour sheenEdge   { 0xFFFFB0D8 };
    juce::Colour auraOuter   { 0xFFFF4FA0 };
};

struct MaterialPhysics {
    float roughness   = 0.18f;
    float thicknessPx = 6.0f;
    float ior         = 1.42f;
    float viscosity   = 0.85f;
    float density     = 1.12f;
    struct { float x, y, z; } lightDir { 0.45f, -0.78f, 0.43f };
};

class BubblegumCableMaterialCore {
public:
    const MaterialPalette&  palette() const noexcept { return palette_; }
    const MaterialPhysics&  physics() const noexcept { return physics_; }

    static float diffuseTerm(float nx, float ny, const MaterialPhysics& phys) noexcept {
        const float d = nx * phys.lightDir.x + ny * phys.lightDir.y;
        return juce::jlimit(0.f, 1.f, 0.5f + 0.5f * d);
    }

    static float fresnelTerm(float nx) noexcept {
        const float a = std::abs(nx);
        const float a2 = a * a;
        return a2 * a2 * a;
    }

    static float specularTerm(float nx, float ny, const MaterialPhysics& phys) noexcept {
        const float nl = nx * phys.lightDir.x + ny * phys.lightDir.y;
        const float r  = juce::jmax(0.f, nl);
        const float exponent = 2.f / juce::jmax(0.001f, phys.roughness * phys.roughness);
        return std::pow(r, exponent);
    }

private:
    MaterialPalette palette_;
    MaterialPhysics physics_;
};

} // namespace DAW::BgV2

#pragma once
#include <JuceHeader.h>
#include <array>

namespace DAW::BgV2 {

struct Endpoint { float x = 0.f, y = 0.f; };

struct CableInput {
    Endpoint source, target;
    float sendLevel  = 0.f;
    float sendEnergy = 0.f;
    bool  active     = false;
    float dragTension     = 0.f;
    float interactionGlow = 0.f;
    float activationPulse = 0.f;
    float deletePulse     = 0.f;
    float id() const noexcept { return source.x * 0.031f + target.x * 0.017f + target.y * 0.013f; }
};

struct CablePoint {
    float t  = 0.f;
    float x  = 0.f, y  = 0.f;
    float nx = 0.f, ny = -1.f;
    float halfW = 0.f;
};

struct CableGeometry {
    static constexpr int kSeg = 128;
    std::array<CablePoint, kSeg + 1> points;
    float sx = 0, sy = 0, tx = 0, ty = 0;
    float sag = 0;
    float thickness = 6.f;
    float seed = 0.f;
    juce::Path ribbon;
    juce::Path topContour;
    juce::Path bottomContour;
};

enum class RenderTier { Tier0Body, Tier1Material, Tier2Polish, Tier3Endpoints };

} // namespace DAW::BgV2

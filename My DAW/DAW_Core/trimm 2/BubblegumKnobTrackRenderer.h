#pragma once
#include <JuceHeader.h>
#include "BubblegumKnobScaleCore.h"

namespace DAW {

/**
 * BubblegumKnobTrackRenderer
 *
 * Paints the visual track outside the knob body:
 *   - dim track arc spanning the full rotation range
 *   - tick dots at evenly spaced angles (slightly larger at the ends and at unity)
 *   - bright pink "active arc" from unity to the current position
 *
 * The active arc is the visual hook — it tells the user how far they've moved
 * away from unity at a glance, which is the fundamental affordance of a
 * bipolar gain knob.
 *
 * Stateless. The current angle and unity angle are passed in by the caller
 * (the knob Component) which has already consulted the ScaleCore.
 */
class BubblegumKnobTrackRenderer
{
public:
    struct Config
    {
        juce::Colour trackColour       { 0x8022273A };
        juce::Colour tickColour        { 0xFF3B4156 };
        juce::Colour activeArcColour   { 0xFFFF4F8A };

        float trackRadiusMult   { 1.10f };
        float trackStrokeWidth  { 1.5f  };

        int   numTicks          { 11 };
        float tickRadiusMult    { 1.10f };
        float tickDotRadius     { 0.95f };
        float tickEndDotRadius  { 1.20f };
        float tickUnityDotRadius { 1.20f };

        float activeArcRadiusMult { 1.10f };
        float activeArcStrokeWidth { 2.0f };
        float activeArcAlpha       { 0.85f };
    };

    BubblegumKnobTrackRenderer() = default;

    void setConfig(const Config& c)        { config_ = c; }
    const Config& getConfig() const        { return config_; }

    void paint(juce::Graphics& g,
               juce::Point<float> centre,
               float bodyRadius,
               float minAngleDeg,
               float maxAngleDeg,
               float currentAngleDeg,
               float unityAngleDeg) const
    {
        paintTrack(g, centre, bodyRadius, minAngleDeg, maxAngleDeg);
        paintTicks(g, centre, bodyRadius, minAngleDeg, maxAngleDeg, unityAngleDeg);
        paintActiveArc(g, centre, bodyRadius, currentAngleDeg, unityAngleDeg);
    }

private:
    void paintTrack(juce::Graphics& g,
                    juce::Point<float> centre,
                    float bodyRadius,
                    float minAngleDeg,
                    float maxAngleDeg) const
    {
        const float r = bodyRadius * config_.trackRadiusMult;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, r, r, 0.0f,
                          juce::degreesToRadians(minAngleDeg),
                          juce::degreesToRadians(maxAngleDeg),
                          true);
        g.setColour(config_.trackColour);
        g.strokePath(arc, juce::PathStrokeType(config_.trackStrokeWidth));
    }

    void paintTicks(juce::Graphics& g,
                    juce::Point<float> centre,
                    float bodyRadius,
                    float minAngleDeg,
                    float maxAngleDeg,
                    float unityAngleDeg) const
    {
        if (config_.numTicks < 2) return;

        const float r = bodyRadius * config_.tickRadiusMult;
        g.setColour(config_.tickColour);

        for (int i = 0; i < config_.numTicks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(config_.numTicks - 1);
            const float angle = juce::jmap(t, 0.0f, 1.0f, minAngleDeg, maxAngleDeg);
            const auto p = BubblegumKnobScaleCore::pointOnArc(centre, r, angle);

            const bool isEnd   = (i == 0 || i == config_.numTicks - 1);
            const bool isUnity = std::abs(angle - unityAngleDeg) < 1.0f;
            const float dotR   = isUnity ? config_.tickUnityDotRadius
                              : isEnd    ? config_.tickEndDotRadius
                                         : config_.tickDotRadius;

            g.fillEllipse(p.x - dotR, p.y - dotR, dotR * 2.0f, dotR * 2.0f);
        }
    }

    void paintActiveArc(juce::Graphics& g,
                        juce::Point<float> centre,
                        float bodyRadius,
                        float currentAngleDeg,
                        float unityAngleDeg) const
    {
        if (std::abs(currentAngleDeg - unityAngleDeg) < 0.5f) return;

        const float r = bodyRadius * config_.activeArcRadiusMult;
        const float fromAngle = juce::jmin(unityAngleDeg, currentAngleDeg);
        const float toAngle   = juce::jmax(unityAngleDeg, currentAngleDeg);

        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, r, r, 0.0f,
                          juce::degreesToRadians(fromAngle),
                          juce::degreesToRadians(toAngle),
                          true);

        g.setColour(config_.activeArcColour.withAlpha(config_.activeArcAlpha));
        g.strokePath(arc, juce::PathStrokeType(config_.activeArcStrokeWidth,
                                               juce::PathStrokeType::beveled,
                                               juce::PathStrokeType::rounded));
    }

    Config config_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumKnobTrackRenderer)
};

} // namespace DAW

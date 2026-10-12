#pragma once

#include "AutomationLaneCore.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace DAW {

/**
 * Offline, bounded tempo-synchronised automation-pattern generator.
 *
 * This is a message-thread EDIT of points, not a signal-rate modulator.
 * No DSP allocations/locks: generated lanes are published using the existing
 * immutable APEX snapshot path after the edit. Direct editing remains
 * destructive only to the selected lane, with a single Undo transaction.
 */
struct AutomationMusicalLfoCore
{
    enum class Shape { Sine, Triangle, Square, Saw };

    static bool supports(const juce::String& parameterId) noexcept
    {
        return parameterId == AutomationLaneCore::trackVolumeParameterId
            || parameterId == AutomationLaneCore::trackPanParameterId
            || parameterId.startsWith("plugin.")
            || parameterId.startsWith("instrument.");
    }

    /**
     * Generate one musical cycle over beatsPerCycle beats.
     * A default 4-beat cycle equals one 4/4 bar at the supplied BPM.
     * The endpoint is explicit, to preserve the last value on playback.
     * 64 divisions per cycle, bounded at 65 points; square uses Hold.
     */
    static bool generate(const juce::String& parameterId, Shape shape,
                         double sampleRate, double tempoBpm,
                         std::vector<AutomationPoint>& destination,
                         double beatsPerCycle = 4.0)
    {
        constexpr int divisions = 64;
        if (!supports(parameterId) || !std::isfinite(sampleRate)
            || !std::isfinite(tempoBpm) || !std::isfinite(beatsPerCycle)
            || sampleRate <= 0.0 || tempoBpm <= 0.0
            || beatsPerCycle <= 0.0 || beatsPerCycle > 64.0)
            return false;

        const double totalSamples = sampleRate * 60.0 * beatsPerCycle / tempoBpm;
        if (!std::isfinite(totalSamples) || totalSamples < divisions * 2.0
            || totalSamples > static_cast<double>(std::numeric_limits<int64_t>::max() / 4))
            return false;

        const bool pan = parameterId == AutomationLaneCore::trackPanParameterId;
        const auto curve = shape == Shape::Square
            ? AutomationCurveType::Hold : AutomationCurveType::Linear;

        std::vector<AutomationPoint> generated;
        generated.reserve(divisions + 1);
        for (int i = 0; i <= divisions; ++i)
        {
            const double phase = static_cast<double>(i) / divisions;
            double u = 0.0;
            switch (shape)
            {
                case Shape::Sine:     u = 0.5 + 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * phase); break;
                case Shape::Triangle: u = 1.0 - std::abs(2.0 * phase - 1.0); break;
                case Shape::Square:   u = (phase < 0.5 || phase >= 1.0) ? 1.0 : 0.0; break;
                case Shape::Saw:      u = phase; break;
                default:             return false;
            }

            // Avoid extreme endpoint saw-wrap interpolation across an
            // entire subdivision. One sample down, like a reset transient.
            double samplePos = totalSamples * phase;
            if (shape == Shape::Saw && i == divisions - 1)
                samplePos = totalSamples - 1.0;

            AutomationPoint point;
            point.timeSamples = static_cast<int64_t>(std::llround(samplePos));
            point.value = AutomationLaneCore::clampValueForParameter(
                parameterId, static_cast<float>(pan ? (u * 2.0 - 1.0) : u));
            point.curveToNext = curve;
            point.tensionToNext = 0.0f;
            generated.push_back(point);
        }

        // The saw must reset only at the boundary, not glide 1/64 bar.
        if (shape == Shape::Saw)
        {
            generated.back().value = AutomationLaneCore::clampValueForParameter(
                parameterId, pan ? -1.0f : 0.0f);
            generated[generated.size() - 2].value = AutomationLaneCore::clampValueForParameter(
                parameterId, pan ? 1.0f : 1.0f);
        }

        destination = std::move(generated);
        return true;
    }
};

} // namespace DAW

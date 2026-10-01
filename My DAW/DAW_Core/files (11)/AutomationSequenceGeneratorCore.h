// ============================================================
// AutomationSequenceGeneratorCore.h
// APEX — Create Sequence system  |  Source/AutomationSequence/
//
// Pure stateless math.  NO JUCE UI, NO state, NO side effects.
// All functions are static.  Include anywhere safely.
// ============================================================
#pragma once
#include "AutomationSequenceTypes.h"
#include <cmath>
#include <random>
#include <algorithm>

namespace APEX {
namespace AutomationSeq {

class GeneratorCore {
public:
    GeneratorCore() = delete;  // Static-only class

    // ===========================================================
    // generate()
    //   Fills a GeneratedCurve from SequenceParams.
    //   No allocations inside — uses fixed-size arrays.
    // ===========================================================
    static GeneratedCurve generate(const SequenceParams& p) noexcept {
        GeneratedCurve out;
        out.samples.fill(0.0f);
        out.timeMulResolved = resolveTimeMul(p.timeMul);

        const int N = juce::jlimit(1, kMaxSteps, p.numSteps);
        const float samplesPerStep = static_cast<float>(kPreviewSamples) / N;

        std::mt19937 rng(p.seed);
        std::uniform_real_distribution<float> udist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> pdist(0.0f, 1.0f);

        int activeCount = 0;

        for (int step = 0; step < N; ++step) {
            const StepData& sd = p.steps[step];

            // ---- Probability gate ---
            bool fires = sd.active;
            if (p.humanize > 0.0f) {
                float threshold = sd.active ? sd.probability : 0.0f;
                fires = pdist(rng) < threshold;
            }
            if (!fires) { rng(); rng(); rng(); rng(); continue; }  // advance RNG consistently

            activeCount++;

            // ---- Resolve per-step overrides ----
            float aLvl  = resolveField(sd.localAttackLevel,  p.attackLevel);
            float dSlp  = resolveField(sd.localDecaySlope,   p.decaySlope);
            float sLvl  = resolveField(sd.localSustainLevel, p.sustainLevel);
            float rSlp  = resolveField(sd.localReleaseSlope, p.releaseSlope);
            float gateV = p.gate;

            // ---- Humanize perturbations ----
            if (p.humanize > 0.0f) {
                const float h = p.humanize * 0.12f;
                aLvl  = clamp01(aLvl  + h * udist(rng));
                sLvl  = clamp01(sLvl  + h * udist(rng));
                gateV = juce::jlimit(0.05f, 1.0f, gateV + h * 0.5f * udist(rng));
            } else {
                rng(); rng(); rng(); rng();
            }

            const int iStart = static_cast<int>(step * samplesPerStep);
            const int iEnd   = std::min(static_cast<int>((step + 1) * samplesPerStep), kPreviewSamples);
            const float span = std::max(1.0f, static_cast<float>(iEnd - iStart));

            // ---- Swing offset: push odd steps later ----
            const float swingShift = ((step & 1) == 1)
                ? p.swing * 0.18f * span
                : 0.0f;

            for (int s = iStart; s < iEnd; ++s) {
                float phase = ((s - iStart) - swingShift) / span;
                phase = juce::jlimit(0.0f, 1.0f, phase);

                float v = 0.0f;
                switch (p.mode) {
                    case ShapeMode::Normal:
                        v = evalNormal(phase, aLvl, p.attackSlope, dSlp, sLvl, rSlp, gateV);
                        break;

                    case ShapeMode::Saw:
                        v = (phase < gateV)
                            ? aLvl * (phase / std::max(gateV, 0.001f))
                            : 0.0f;
                        break;

                    case ShapeMode::RevSaw:
                        v = (phase < gateV)
                            ? aLvl * (1.0f - phase / std::max(gateV, 0.001f))
                            : 0.0f;
                        break;

                    case ShapeMode::Triangle: {
                        const float half = gateV * 0.5f;
                        if (phase < half)
                            v = aLvl * (phase / std::max(half, 0.001f));
                        else if (phase < gateV)
                            v = aLvl * (1.0f - (phase - half) / std::max(half, 0.001f));
                        break;
                    }

                    case ShapeMode::Gate:
                        v = (phase < gateV) ? aLvl : 0.0f;
                        break;

                    case ShapeMode::Sine:
                        if (phase < gateV)
                            v = aLvl * std::sin(juce::MathConstants<float>::pi
                                                * phase / std::max(gateV, 0.001f));
                        break;

                    case ShapeMode::Bounce:
                        v = evalBounce(phase, aLvl, gateV);
                        break;

                    default: break;
                }

                out.samples[s] = clamp01(v);
            }
        }

        out.numActiveSteps = activeCount;
        return out;
    }

    // ===========================================================
    // barValues()
    //   Returns one float per step for the editable bar view.
    // ===========================================================
    static std::array<float, kMaxSteps> barValues(
        const SequenceParams& p, BarViewMode mode) noexcept
    {
        std::array<float, kMaxSteps> out{};
        const int N = juce::jlimit(1, kMaxSteps, p.numSteps);
        for (int i = 0; i < N; ++i) {
            if (!p.steps[i].active) { out[i] = 0.0f; continue; }
            const StepData& sd = p.steps[i];
            switch (mode) {
                case BarViewMode::AttackLevel:  out[i] = resolveField(sd.localAttackLevel,  p.attackLevel);  break;
                case BarViewMode::DecaySlope:   out[i] = resolveField(sd.localDecaySlope,   p.decaySlope);   break;
                case BarViewMode::SustainLevel: out[i] = resolveField(sd.localSustainLevel, p.sustainLevel); break;
                case BarViewMode::ReleaseSlope: out[i] = resolveField(sd.localReleaseSlope, p.releaseSlope); break;
                default: break;
            }
        }
        return out;
    }

    // timeMul: 0 → 0.25x,  0.5 → 1x,  1 → 4x  (log2 interpolation)
    static float resolveTimeMul(float n) noexcept {
        constexpr float lo = -2.0f;   // log2(0.25)
        constexpr float hi =  2.0f;   // log2(4.0)
        return std::pow(2.0f, lo + n * (hi - lo));
    }

private:
    static inline float resolveField(float local, float global) noexcept {
        return (local >= 0.0f) ? local : global;
    }
    static inline float clamp01(float v) noexcept {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    // Full ADSR envelope, phase 0..1 within a single step.
    //   Segments:  0..atkEnd  →  atkEnd..decEnd  →  decEnd..gate  →  gate..1.0
    static float evalNormal(float phase, float aLvl, float aSlp,
                             float dSlp, float sLvl, float rSlp, float gate) noexcept
    {
        // Map normalised slope 0..1 → curve exponent  0.1..3.0
        auto toExp = [](float s) noexcept -> float { return 0.1f + s * 2.9f; };
        const float aExp = toExp(aSlp);
        const float dExp = toExp(dSlp);
        const float rExp = toExp(rSlp);

        const float eps    = 0.001f;
        const float atkEnd = gate * 0.22f;
        const float decEnd = gate * 0.50f;
        const float susEnd = gate;

        if (phase <= atkEnd) {
            float t = phase / std::max(atkEnd, eps);
            return aLvl * std::pow(t, aExp);
        }
        if (phase <= decEnd) {
            float t = (phase - atkEnd) / std::max(decEnd - atkEnd, eps);
            float target = aLvl * sLvl;
            return aLvl + (target - aLvl) * std::pow(t, dExp);
        }
        if (phase <= susEnd) {
            return aLvl * sLvl;
        }
        // Release
        float t = (phase - susEnd) / std::max(1.0f - susEnd, eps);
        return aLvl * sLvl * (1.0f - std::pow(t, rExp));
    }

    // Three decaying sinusoidal bounces.
    static float evalBounce(float phase, float aLvl, float gate) noexcept {
        if (phase >= gate) return 0.0f;
        const float t       = phase / std::max(gate, 0.001f);   // 0..1 within gate
        const float decay   = std::exp(-4.0f * t);
        const float period  = 0.33f;
        const float bPhase  = std::fmod(t, period) / period;
        return clamp01(aLvl * decay * std::sin(juce::MathConstants<float>::pi * bPhase));
    }
};

} // namespace AutomationSeq
} // namespace APEX

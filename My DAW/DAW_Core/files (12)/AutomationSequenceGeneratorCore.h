// ============================================================
// AutomationSequenceGeneratorCore.h  — v3 (Playground)
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// 70 shape implementations.  All pure stateless math.
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
    GeneratorCore() = delete;

    static GeneratedCurve generate(const SequenceParams& p) noexcept {
        GeneratedCurve out;
        out.samples.fill(0.0f);
        out.timeMulResolved = resolveTimeMul(p.timeMul);

        const int N = juce::jlimit(1, kMaxSteps, p.numSteps);
        const float samplesPerStep = static_cast<float>(kPreviewSamples) / N;

        std::array<bool, kMaxSteps> eucActive{};
        if (p.mode == ShapeMode::Euclid) {
            int hits = 0;
            for (int i = 0; i < N; ++i) hits += p.steps[i].active ? 1 : 0;
            eucActive = euclideanPattern(hits, N);
        }

        std::mt19937 rng(p.seed);
        std::uniform_real_distribution<float> udist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> pdist(0.0f, 1.0f);
        int activeCount = 0;

        for (int step = 0; step < N; ++step) {
            const bool isActive = (p.mode == ShapeMode::Euclid)
                                  ? eucActive[step] : p.steps[step].active;

            bool fires = isActive;
            if (p.humanize > 0.0f) {
                float threshold = isActive ? p.steps[step].probability : 0.0f;
                fires = pdist(rng) < threshold;
            }
            if (!fires) { (void)rng(); (void)rng(); (void)rng(); (void)rng(); continue; }
            activeCount++;

            const StepData& sd = p.steps[step];
            float aLvl  = resolveField(sd.localAttackLevel,  p.attackLevel);
            float dSlp  = resolveField(sd.localDecaySlope,   p.decaySlope);
            float sLvl  = resolveField(sd.localSustainLevel, p.sustainLevel);
            float rSlp  = resolveField(sd.localReleaseSlope, p.releaseSlope);
            float gateV = p.gate;

            if (p.humanize > 0.0f) {
                const float h = p.humanize * 0.12f;
                aLvl  = clamp01(aLvl  + h * udist(rng));
                sLvl  = clamp01(sLvl  + h * udist(rng));
                gateV = juce::jlimit(0.05f, 1.0f, gateV + h * 0.5f * udist(rng));
            } else {
                (void)rng(); (void)rng(); (void)rng(); (void)rng();
            }

            const int iStart = static_cast<int>(step * samplesPerStep);
            const int iEnd   = std::min(static_cast<int>((step + 1) * samplesPerStep),
                                        kPreviewSamples);
            const float span = std::max(1.0f, static_cast<float>(iEnd - iStart));
            const float swingShift = ((step & 1) == 1)
                ? p.swing * 0.18f * span : 0.0f;
            const int rep = juce::jlimit(1, 8, p.repeatCount);

            for (int s = iStart; s < iEnd; ++s) {
                float rawPhase = ((s - iStart) - swingShift) / span;
                rawPhase = juce::jlimit(0.0f, 1.0f, rawPhase);

                const float phase = (rep > 1)
                    ? std::fmod(rawPhase * static_cast<float>(rep), 1.0f)
                    : rawPhase;

                float v = evalShape(p.mode, phase, aLvl, p.attackSlope,
                                    dSlp, sLvl, rSlp, gateV,
                                    p.seed, step, s);

                if (p.chaos > 0.0f) {
                    const float noise = hashFloat(p.seed
                        + 3571u * static_cast<uint32_t>(step)
                        + 1009u * static_cast<uint32_t>(s));
                    v = clamp01(v + p.chaos * 0.22f * (noise * 2.0f - 1.0f));
                }
                out.samples[s] = clamp01(v);
            }
        }

        out.numActiveSteps = activeCount;
        return out;
    }

    static std::array<float, kMaxSteps> barValues(
        const SequenceParams& p, BarViewMode mode) noexcept
    {
        std::array<float, kMaxSteps> out{};
        const int N = juce::jlimit(1, kMaxSteps, p.numSteps);
        for (int i = 0; i < N; ++i) {
            if (!p.steps[i].active) { out[i] = 0.0f; continue; }
            switch (mode) {
                case BarViewMode::AttackLevel:  out[i] = resolveField(p.steps[i].localAttackLevel,  p.attackLevel);  break;
                case BarViewMode::DecaySlope:   out[i] = resolveField(p.steps[i].localDecaySlope,   p.decaySlope);   break;
                case BarViewMode::SustainLevel: out[i] = resolveField(p.steps[i].localSustainLevel, p.sustainLevel); break;
                case BarViewMode::ReleaseSlope: out[i] = resolveField(p.steps[i].localReleaseSlope, p.releaseSlope); break;
                default: break;
            }
        }
        return out;
    }

    static float resolveTimeMul(float n) noexcept {
        return std::pow(2.0f, -2.0f + n * 4.0f);
    }

    static std::array<bool, kMaxSteps> euclideanPattern(int hits, int steps) noexcept {
        std::array<bool, kMaxSteps> out{};
        hits  = juce::jlimit(0, steps, hits);
        steps = juce::jlimit(1, kMaxSteps, steps);
        if (hits == 0)     return out;
        if (hits >= steps) { for (int i = 0; i < steps; ++i) out[i] = true; return out; }
        for (int i = 0; i < steps; ++i)
            out[i] = ((i * hits) % steps) < hits;
        return out;
    }

private:
    static inline float clamp01(float v) noexcept {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    static inline float resolveField(float local, float global) noexcept {
        return (local >= 0.0f) ? local : global;
    }
    static inline float toExp(float s) noexcept { return 0.1f + s * 2.9f; }

    // Integer hash → float [0,1)
    static float hashFloat(uint32_t x) noexcept {
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = (x >> 16) ^ x;
        return (x & 0xFFFFu) / 65535.0f;
    }
    static uint32_t hash32(uint32_t x) noexcept {
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = ((x >> 16) ^ x) * 0x45d9f3bu;
        x = (x >> 16) ^ x;
        return x;
    }

    // ============================================================
    // evalShape — master dispatcher for all 70 shapes
    // ============================================================
    static float evalShape(ShapeMode mode, float phase,
                            float aLvl, float aSlp,
                            float dSlp, float sLvl, float rSlp, float gateV,
                            uint32_t seed, int step, int sampleIdx) noexcept
    {
        constexpr float eps = 0.001f;
        constexpr float pi  = juce::MathConstants<float>::pi;
        constexpr float tpi = juce::MathConstants<float>::twoPi;

        switch (mode) {
            // ── ENVELOPE ──────────────────────────────────────────
            case ShapeMode::Normal:
                return evalNormal(phase, aLvl, aSlp, dSlp, sLvl, rSlp, gateV);

            case ShapeMode::AD: {
                const float atkEnd = gateV * 0.45f;
                if (phase <= atkEnd)
                    return aLvl * std::pow(phase / std::max(atkEnd, eps), toExp(aSlp));
                if (phase <= gateV) {
                    float t = (phase - atkEnd) / std::max(gateV - atkEnd, eps);
                    return aLvl * (1.0f - std::pow(t, toExp(dSlp)));
                }
                return 0.0f;
            }

            case ShapeMode::AR: {
                const float atkEnd = gateV * 0.3f;
                if (phase <= atkEnd)
                    return aLvl * std::pow(phase / std::max(atkEnd, eps), toExp(aSlp));
                if (phase <= gateV) {
                    float t = (phase - atkEnd) / std::max(gateV - atkEnd, eps);
                    return aLvl * (1.0f - std::pow(t, toExp(rSlp)));
                }
                return 0.0f;
            }

            case ShapeMode::AHR: {
                const float atkEnd  = gateV * 0.20f;
                const float holdEnd = atkEnd + gateV * (0.10f + dSlp * 0.30f);
                if (phase <= atkEnd)
                    return aLvl * std::pow(phase / std::max(atkEnd, eps), toExp(aSlp));
                if (phase <= holdEnd) return aLvl;
                if (phase <= gateV) {
                    float t = (phase - holdEnd) / std::max(gateV - holdEnd, eps);
                    return aLvl * (1.0f - std::pow(t, toExp(rSlp)));
                }
                return 0.0f;
            }

            case ShapeMode::AHDSR: {
                const float holdFrac = dSlp * 0.30f;
                const float atkEnd  = gateV * 0.18f;
                const float holdEnd = atkEnd + gateV * holdFrac;
                const float decEnd  = holdEnd + gateV * 0.25f;
                const float susEnd  = gateV;
                if (phase <= atkEnd)
                    return aLvl * std::pow(phase / std::max(atkEnd, eps), toExp(aSlp));
                if (phase <= holdEnd) return aLvl;
                if (phase <= decEnd) {
                    float t = (phase - holdEnd) / std::max(decEnd - holdEnd, eps);
                    return aLvl + (aLvl * sLvl - aLvl) * std::pow(t, toExp(rSlp));
                }
                if (phase <= susEnd) return aLvl * sLvl;
                {
                    float t = (phase - susEnd) / std::max(1.0f - susEnd, eps);
                    return aLvl * sLvl * (1.0f - std::pow(t, toExp(rSlp)));
                }
            }

            case ShapeMode::Gate:
                return (phase < gateV) ? aLvl : 0.0f;

            case ShapeMode::GateInv:
                return (phase < gateV) ? 0.0f : aLvl;

            // ── RAMP ──────────────────────────────────────────────
            case ShapeMode::Saw:
                return (phase < gateV) ? aLvl * (phase / std::max(gateV, eps)) : 0.0f;

            case ShapeMode::RevSaw:
                return (phase < gateV) ? aLvl * (1.0f - phase / std::max(gateV, eps)) : 0.0f;

            case ShapeMode::Triangle: {
                const float half = gateV * 0.5f;
                if (phase < half)
                    return aLvl * (phase / std::max(half, eps));
                if (phase < gateV)
                    return aLvl * (1.0f - (phase - half) / std::max(half, eps));
                return 0.0f;
            }

            case ShapeMode::StairUp: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 6;
                const int stair = juce::jlimit(0, K - 1,
                    static_cast<int>(phase / gateV * K));
                return aLvl * (stair + 1) / static_cast<float>(K);
            }

            case ShapeMode::StairDn: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 6;
                const int stair = juce::jlimit(0, K - 1,
                    static_cast<int>(phase / gateV * K));
                return aLvl * (K - stair) / static_cast<float>(K);
            }

            case ShapeMode::ZigZag: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 4;
                const float t  = phase / std::max(gateV, eps) * K;
                const int   z  = static_cast<int>(t);
                const float sub = t - z;
                return aLvl * ((z & 1) ? (1.0f - sub) : sub);
            }

            // ── WAVE ──────────────────────────────────────────────
            case ShapeMode::Sine:
                return (phase < gateV)
                    ? aLvl * std::sin(pi * phase / std::max(gateV, eps)) : 0.0f;

            case ShapeMode::SineDbl:
                return (phase < gateV)
                    ? aLvl * std::max(0.0f, std::sin(tpi * phase / std::max(gateV, eps)))
                    : 0.0f;

            case ShapeMode::SineSq: {
                if (phase >= gateV) return 0.0f;
                const float s = std::sin(pi * phase / std::max(gateV, eps));
                return aLvl * s * s;
            }

            case ShapeMode::SineCube: {
                if (phase >= gateV) return 0.0f;
                const float s = std::sin(pi * phase / std::max(gateV, eps));
                return aLvl * s * s * s;
            }

            case ShapeMode::Tremolo:
                return (phase < gateV)
                    ? aLvl * (0.5f + 0.5f * std::sin(tpi * 8.0f * phase)) : 0.0f;

            case ShapeMode::Flutter: {
                if (phase >= gateV) return 0.0f;
                const float f1 = std::sin(tpi * 7.3f  * phase);
                const float f2 = std::sin(tpi * 13.7f * phase);
                const float f3 = std::sin(tpi * 4.9f  * phase);
                const float env = std::sin(pi * phase / std::max(gateV, eps));
                return clamp01(aLvl * env * (0.5f + 0.33f * (f1 * 0.5f + f2 * 0.3f + f3 * 0.2f)));
            }

            case ShapeMode::WaveFold: {
                if (phase >= gateV) return 0.0f;
                // sin then fold values > threshold back down
                float v = std::sin(tpi * 3.0f * phase / std::max(gateV, eps)) * 1.8f;
                while (v > 1.0f)  v = 2.0f - v;
                while (v < -1.0f) v = -2.0f - v;
                return aLvl * (0.5f + 0.5f * v);
            }

            // ── PHYSICS ───────────────────────────────────────────
            case ShapeMode::Bounce:
                return evalBounce(phase, aLvl, gateV);

            case ShapeMode::Spring: {
                if (phase >= gateV) return 0.0f;
                const float t    = phase / std::max(gateV, eps);
                const float damp = std::exp(-3.8f * t);
                const float osc  = std::cos(tpi * 3.5f * t);
                return clamp01(aLvl * (1.0f - damp * osc));
            }

            case ShapeMode::Pendulum: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                return aLvl * std::abs(std::cos(pi * t));
            }

            case ShapeMode::Heartbeat: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                auto gauss = [](float x, float mu, float sig) {
                    const float d = (x - mu) / sig;
                    return std::exp(-0.5f * d * d);
                };
                const float pw  = gauss(t, 0.18f, 0.06f) * 0.35f;
                const float qrs = gauss(t, 0.42f, 0.04f) * 1.0f;
                const float tw  = gauss(t, 0.72f, 0.08f) * 0.25f;
                return clamp01(aLvl * (pw + qrs + tw));
            }

            case ShapeMode::Drumroll: {
                // Accelerating hits — frequency increases exponentially
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float freq = 4.0f + 16.0f * t * t;
                const float trig = std::sin(tpi * freq * t);
                return (trig > 0.7f) ? aLvl * (1.0f - t * 0.5f) : 0.0f;
            }

            case ShapeMode::Pluck: {
                // Karplus-Strong style: noise burst at start + exponential decay
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float burst = (t < 0.04f)
                    ? hashFloat(seed + static_cast<uint32_t>(step) * 17u
                                     + static_cast<uint32_t>(sampleIdx))
                    : 0.5f;
                const float decay = std::exp(-5.0f * t);
                const float ring  = std::sin(tpi * 6.0f * t);
                return clamp01(aLvl * decay * (0.6f + 0.4f * burst * (0.5f + 0.5f * ring)));
            }

            // ── ORGANIC ───────────────────────────────────────────
            case ShapeMode::Breathe: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                auto sigm = [](float x) { return 1.0f / (1.0f + std::exp(-x)); };
                const float rise = sigm((t - 0.25f) * 18.0f);
                const float fall = 1.0f - sigm((t - 0.75f) * 18.0f);
                return aLvl * clamp01(rise * fall);
            }

            case ShapeMode::Wobble: {
                if (phase >= gateV) return 0.0f;
                const float t  = phase / std::max(gateV, eps);
                const float fm = std::sin(tpi * 1.8f * t) * 0.15f;
                const float v  = std::sin(tpi * 4.5f * (t + fm));
                return aLvl * (0.5f + 0.5f * v);
            }

            case ShapeMode::Stutter: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 8;
                const float sub = std::fmod(phase / gateV * K, 1.0f);
                return (sub < 0.48f) ? aLvl : 0.0f;
            }

            case ShapeMode::Ratchet: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 4;
                const float norm = phase / std::max(gateV, eps);
                const float sub  = std::fmod(norm * K, 1.0f);
                const float lvl  = std::floor(norm * K) / K;
                return aLvl * (lvl + sub / K);
            }

            case ShapeMode::Vinyl: {
                // Sparse seeded crackle: short impulses at pseudo-random positions
                if (phase >= gateV) return 0.0f;
                constexpr int K = 32;
                const int bin   = static_cast<int>(phase / gateV * K);
                const float h   = hashFloat(seed + static_cast<uint32_t>(step) * 991u
                                                + static_cast<uint32_t>(bin));
                if (h > 0.85f) {
                    const float subT = std::fmod(phase / gateV * K, 1.0f);
                    const float pulse = std::exp(-8.0f * subT);
                    return aLvl * pulse * (h);
                }
                return 0.0f;
            }

            case ShapeMode::Telegraph: {
                // Morse-like: dots and dashes seeded by step
                if (phase >= gateV) return 0.0f;
                constexpr int K = 12;
                const int bin = static_cast<int>(phase / gateV * K);
                const float h = hashFloat(seed + static_cast<uint32_t>(step) * 1097u
                                               + static_cast<uint32_t>(bin / 2));
                const bool isDash = ((bin % 4) == 0) && (h > 0.5f);
                const bool isDot  = ((bin % 4) == 2) && (h > 0.3f);
                return (isDash || isDot) ? aLvl : 0.0f;
            }

            // ── CURVE ─────────────────────────────────────────────
            case ShapeMode::ExpIn:
                return (phase < gateV)
                    ? aLvl * std::pow(phase / std::max(gateV, eps), 3.0f) : 0.0f;

            case ShapeMode::ExpOut: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                return aLvl * (1.0f - std::pow(1.0f - t, 3.0f));
            }

            case ShapeMode::SCurve: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                return aLvl * (t * t * (3.0f - 2.0f * t));
            }

            case ShapeMode::LogCurve:
                return (phase < gateV)
                    ? aLvl * std::sqrt(phase / std::max(gateV, eps)) : 0.0f;

            case ShapeMode::Convex:
                return (phase < gateV)
                    ? aLvl * std::pow(phase / std::max(gateV, eps), 0.5f) : 0.0f;

            case ShapeMode::Concave:
                return (phase < gateV)
                    ? aLvl * std::pow(phase / std::max(gateV, eps), 2.0f) : 0.0f;

            // ── PULSE ─────────────────────────────────────────────
            case ShapeMode::NarrowPulse:
                return (phase < gateV * 0.15f) ? aLvl : 0.0f;

            case ShapeMode::WidePulse:
                return (phase < gateV * 0.80f) ? aLvl : 0.0f;

            case ShapeMode::DoublePulse: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                return ((t < 0.20f) || (t > 0.45f && t < 0.65f)) ? aLvl : 0.0f;
            }

            case ShapeMode::TriplePulse: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                return ((t < 0.18f) || (t > 0.32f && t < 0.50f)
                                    || (t > 0.62f && t < 0.80f)) ? aLvl : 0.0f;
            }

            case ShapeMode::MorseRand: {
                // Seeded pseudo-random Morse code
                if (phase >= gateV) return 0.0f;
                constexpr int K = 16;
                const int bin = static_cast<int>(phase / gateV * K);
                const float h = hashFloat(seed + static_cast<uint32_t>(step) * 271u
                                               + static_cast<uint32_t>(bin));
                return (h > 0.5f) ? aLvl : 0.0f;
            }

            // ── MOD ───────────────────────────────────────────────
            case ShapeMode::AM: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float carrier   = 0.5f + 0.5f * std::sin(tpi * 6.0f * t);
                const float modulator = 0.5f + 0.5f * std::sin(tpi * 1.5f * t);
                return aLvl * carrier * modulator;
            }

            case ShapeMode::FM: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float modIdx = 2.5f;
                const float modulator = std::sin(tpi * 2.0f * t);
                return aLvl * (0.5f + 0.5f * std::sin(tpi * 5.0f * t + modIdx * modulator));
            }

            case ShapeMode::RingMod: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float a = std::sin(tpi * 4.0f * t);
                const float b = std::sin(tpi * 7.0f * t);
                return aLvl * (0.5f + 0.5f * a * b);
            }

            case ShapeMode::PWM: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                // Pulse width sweeps from 0.2 to 0.8 across the step
                const float pw = 0.2f + 0.6f * std::sin(tpi * 1.5f * t) * 0.5f + 0.3f;
                constexpr int K = 6;
                const float sub = std::fmod(t * K, 1.0f);
                return (sub < pw) ? aLvl : 0.0f;
            }

            case ShapeMode::Comb: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                // Comb peaks at evenly spaced positions
                float v = 0.0f;
                for (int k = 1; k <= 5; ++k) {
                    const float peak = std::exp(-pow((t * 5.0f - k) * 4.0f, 2.0f));
                    v += peak * (1.0f - k * 0.15f);
                }
                return clamp01(aLvl * v);
            }

            // ── RANDOM ────────────────────────────────────────────
            case ShapeMode::WhiteNs: {
                if (phase >= gateV) return 0.0f;
                const float n = hashFloat(seed
                    + static_cast<uint32_t>(step) * 7919u
                    + static_cast<uint32_t>(sampleIdx) * 31u);
                return aLvl * n;
            }

            case ShapeMode::Brownian: {
                if (phase >= gateV) return 0.0f;
                float val = 0.5f;
                const int sLen = juce::jlimit(1, kPreviewSamples,
                    static_cast<int>(phase * kPreviewSamples));
                for (int i = 0; i <= sLen; ++i) {
                    const float s = hashFloat(seed + static_cast<uint32_t>(step) * 997u
                                                   + static_cast<uint32_t>(i) * 53u);
                    val = clamp01(val + (s - 0.5f) * 0.12f);
                }
                return aLvl * val;
            }

            case ShapeMode::SmthRnd: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 5;
                const float t   = phase / std::max(gateV, eps);
                const float seg = t * (K - 1);
                const int   k0  = static_cast<int>(seg);
                const float mu  = seg - k0;
                auto key = [&](int ki) {
                    ki = juce::jlimit(0, K - 1, ki);
                    return hashFloat(seed + static_cast<uint32_t>(step) * 2053u
                                          + static_cast<uint32_t>(ki));
                };
                const float p0 = key(k0 - 1), p1 = key(k0);
                const float p2 = key(k0 + 1), p3 = key(k0 + 2);
                const float m1 = (p2 - p0) * 0.5f;
                const float m2 = (p3 - p1) * 0.5f;
                const float mu2 = mu * mu, mu3 = mu2 * mu;
                return clamp01(aLvl * (
                    (2*mu3 - 3*mu2 + 1) * p1 +
                    (-2*mu3 + 3*mu2)    * p2 +
                    (mu3 - 2*mu2 + mu)  * m1 +
                    (mu3 - mu2)         * m2));
            }

            case ShapeMode::DrunkWalk: {
                if (phase >= gateV) return 0.0f;
                // Bounded random walk that pulls back toward center
                float val = 0.5f;
                const int sLen = juce::jlimit(1, kPreviewSamples,
                    static_cast<int>(phase * kPreviewSamples));
                for (int i = 0; i <= sLen; ++i) {
                    const float r = hashFloat(seed + static_cast<uint32_t>(step) * 1373u
                                                   + static_cast<uint32_t>(i) * 41u);
                    val += (r - 0.5f) * 0.08f;
                    val += (0.5f - val) * 0.015f;   // pull toward center
                    val = clamp01(val);
                }
                return aLvl * val;
            }

            case ShapeMode::RndGate: {
                const float rndLen = 0.2f + hashFloat(seed
                    + static_cast<uint32_t>(step) * 8191u) * 0.75f;
                return (phase < rndLen) ? aLvl : 0.0f;
            }

            case ShapeMode::LFSR: {
                if (phase >= gateV) return 0.0f;
                // 8-bit Galois LFSR
                uint8_t state = static_cast<uint8_t>((seed + step * 71u) & 0xFFu);
                if (state == 0) state = 1;
                const int iters = static_cast<int>(phase / gateV * 32.0f);
                for (int i = 0; i < iters; ++i) {
                    const bool lsb = (state & 1u) != 0u;
                    state >>= 1;
                    if (lsb) state ^= 0xB8u;
                }
                return aLvl * (state / 255.0f);
            }

            // ── GLITCH ────────────────────────────────────────────
            case ShapeMode::BitCrsh: {
                if (phase >= gateV) return 0.0f;
                const float t = phase / std::max(gateV, eps);
                const float cont = std::sin(pi * t);
                constexpr int L = 8;
                const int lvl = juce::jlimit(0, L - 1, static_cast<int>(cont * L));
                return aLvl * static_cast<float>(lvl) / (L - 1);
            }

            case ShapeMode::ChaosMp: {
                if (phase >= gateV) return 0.0f;
                constexpr float r = 3.95f;
                float x = 0.3f + hashFloat(seed + static_cast<uint32_t>(step) * 6271u) * 0.4f;
                const int iters = static_cast<int>(phase / gateV * 48.0f);
                for (int i = 0; i < iters; ++i) x = r * x * (1.0f - x);
                return aLvl * clamp01(x);
            }

            case ShapeMode::Euclid:
                return (phase < gateV) ? aLvl : 0.0f;

            case ShapeMode::SampHold: {
                if (phase >= gateV) return 0.0f;
                constexpr int K = 8;
                const int bin = static_cast<int>(phase / gateV * K);
                return aLvl * hashFloat(seed + static_cast<uint32_t>(step) * 419u
                                             + static_cast<uint32_t>(bin) * 71u);
            }

            case ShapeMode::BitShift: {
                if (phase >= gateV) return 0.0f;
                const int bin = static_cast<int>(phase / gateV * 8.0f);
                return aLvl * static_cast<float>((1 << (bin & 7)) - 1) / 255.0f;
            }

            case ShapeMode::RobotStep: {
                if (phase >= gateV) return 0.0f;
                // Quantized to 16 levels with sharp transitions
                constexpr int L = 16;
                const float t = phase / std::max(gateV, eps);
                const float raw = std::sin(tpi * 1.5f * t) * 0.5f + 0.5f;
                return aLvl * std::floor(raw * L) / L;
            }

            // ── ALGORITHM ─────────────────────────────────────────
            case ShapeMode::Rule30: {
                if (phase >= gateV) return 0.0f;
                // 1D cellular automaton Rule 30, evolved over generations
                constexpr int W = 31;
                uint32_t cells = 1u << (W / 2);   // single center cell
                const int gens = static_cast<int>(phase / gateV * 12.0f) + 1;
                for (int g = 0; g < gens; ++g) {
                    uint32_t next = 0;
                    for (int i = 0; i < W; ++i) {
                        const int l = (i > 0)     ? ((cells >> (i - 1)) & 1u) : 0;
                        const int c = (cells >> i) & 1u;
                        const int rr= (i < W - 1) ? ((cells >> (i + 1)) & 1u) : 0;
                        const int idx = (l << 2) | (c << 1) | rr;
                        // Rule 30: 00011110b
                        const int rule = (30 >> idx) & 1;
                        if (rule) next |= (1u << i);
                    }
                    cells = next;
                }
                // Sample one cell at column matching seed+step
                const int col = (static_cast<int>(seed + step) & (W - 1));
                return aLvl * (((cells >> col) & 1u) ? 1.0f : 0.0f);
            }

            case ShapeMode::Rule110: {
                if (phase >= gateV) return 0.0f;
                constexpr int W = 31;
                uint32_t cells = 1u << (W / 2);
                const int gens = static_cast<int>(phase / gateV * 12.0f) + 1;
                for (int g = 0; g < gens; ++g) {
                    uint32_t next = 0;
                    for (int i = 0; i < W; ++i) {
                        const int l = (i > 0)     ? ((cells >> (i - 1)) & 1u) : 0;
                        const int c = (cells >> i) & 1u;
                        const int rr= (i < W - 1) ? ((cells >> (i + 1)) & 1u) : 0;
                        const int idx = (l << 2) | (c << 1) | rr;
                        const int rule = (110 >> idx) & 1;
                        if (rule) next |= (1u << i);
                    }
                    cells = next;
                }
                const int col = (static_cast<int>(seed + step) & (W - 1));
                return aLvl * (((cells >> col) & 1u) ? 1.0f : 0.0f);
            }

            case ShapeMode::PrimeSieve: {
                if (phase >= gateV) return 0.0f;
                // Position is "active" if its discrete index is prime
                constexpr int K = 24;
                const int n = juce::jlimit(2, K, 2 + static_cast<int>(phase / gateV * K));
                bool prime = true;
                for (int d = 2; d * d <= n; ++d)
                    if (n % d == 0) { prime = false; break; }
                return aLvl * (prime ? 1.0f : 0.0f);
            }

            case ShapeMode::Fibonacci: {
                if (phase >= gateV) return 0.0f;
                // Fib ratios mod 1 give a quasi-periodic sequence
                const float t = phase / std::max(gateV, eps);
                constexpr float phi = 1.6180339887f;
                const float v = std::fmod((t + step * 0.123f) * phi * 5.0f, 1.0f);
                return aLvl * v;
            }

            case ShapeMode::Markov: {
                if (phase >= gateV) return 0.0f;
                // 4-state Markov chain (states 0,1,2,3 → levels 0, 0.33, 0.66, 1.0)
                int state = static_cast<int>(hash32(seed + static_cast<uint32_t>(step) * 919u) % 4u);
                const int steps_taken = static_cast<int>(phase / gateV * 16.0f);
                for (int i = 0; i < steps_taken; ++i) {
                    const float r = hashFloat(seed + static_cast<uint32_t>(step) * 919u
                                                   + static_cast<uint32_t>(i) * 23u);
                    // Bias toward neighbouring states
                    if      (r < 0.4f) state = std::max(0, state - 1);
                    else if (r > 0.6f) state = std::min(3, state + 1);
                }
                return aLvl * state / 3.0f;
            }

            // ── EXOTIC ────────────────────────────────────────────
            case ShapeMode::Lorenz: {
                if (phase >= gateV) return 0.0f;
                // Integrate Lorenz attractor with Euler, project X
                float x = 1.0f + hashFloat(seed + static_cast<uint32_t>(step) * 53u) * 0.5f;
                float y = 1.0f, z = 1.0f;
                constexpr float sigma = 10.0f, rho = 28.0f, beta = 8.0f / 3.0f;
                constexpr float dt = 0.008f;
                const int iters = static_cast<int>(phase / gateV * 200.0f) + 1;
                for (int i = 0; i < iters; ++i) {
                    const float dx = sigma * (y - x);
                    const float dy = x * (rho - z) - y;
                    const float dz = x * y - beta * z;
                    x += dx * dt; y += dy * dt; z += dz * dt;
                }
                // Normalize x typically in [-25, 25]
                return clamp01(aLvl * (0.5f + x * 0.02f));
            }

            case ShapeMode::Henon: {
                if (phase >= gateV) return 0.0f;
                constexpr float a = 1.4f, b = 0.3f;
                float x = 0.1f + hashFloat(seed + static_cast<uint32_t>(step) * 89u) * 0.1f;
                float y = 0.1f;
                const int iters = static_cast<int>(phase / gateV * 48.0f) + 1;
                for (int i = 0; i < iters; ++i) {
                    const float xn = 1.0f - a * x * x + y;
                    y = b * x;
                    x = xn;
                }
                return clamp01(aLvl * (0.5f + x * 0.4f));
            }

            case ShapeMode::Mandelbrot: {
                if (phase >= gateV) return 0.0f;
                // Sample escape time on a horizontal line through the set
                const float t  = phase / std::max(gateV, eps);
                const float cx = -2.0f + t * 3.0f;
                const float cy = -1.0f + (step / 16.0f) * 2.0f
                                       + hashFloat(seed) * 0.1f;
                float zx = 0.0f, zy = 0.0f;
                int iter = 0;
                constexpr int maxIter = 32;
                while (zx*zx + zy*zy < 4.0f && iter < maxIter) {
                    const float t2 = zx*zx - zy*zy + cx;
                    zy = 2.0f * zx * zy + cy;
                    zx = t2;
                    ++iter;
                }
                return aLvl * (1.0f - static_cast<float>(iter) / maxIter);
            }

            case ShapeMode::Cantor: {
                if (phase >= gateV) return 0.0f;
                // Cantor set membership: repeatedly remove middle third
                float t = phase / std::max(gateV, eps);
                bool inSet = true;
                for (int d = 0; d < 6 && inSet; ++d) {
                    t *= 3.0f;
                    if (t >= 1.0f && t < 2.0f) inSet = false;
                    t = std::fmod(t, 1.0f);
                }
                return inSet ? aLvl : 0.0f;
            }

            case ShapeMode::Sierpinski: {
                if (phase >= gateV) return 0.0f;
                // Sierpinski triangle by Pascal's parity
                const int n = juce::jlimit(0, 64,
                    static_cast<int>(phase / gateV * 32.0f));
                const int k = step & n;
                // Binomial (n, k) is odd iff (k & n) == k (Lucas' theorem mod 2)
                return aLvl * ((k == (step & n)) ? 1.0f : 0.0f);
            }

            default: return 0.0f;
        }
    }

    static float evalNormal(float phase, float aLvl, float aSlp,
                             float dSlp, float sLvl, float rSlp, float gate) noexcept
    {
        constexpr float eps = 0.001f;
        const float atkEnd = gate * 0.22f;
        const float decEnd = gate * 0.50f;
        const float susEnd = gate;
        if (phase <= atkEnd)
            return aLvl * std::pow(phase / std::max(atkEnd, eps), toExp(aSlp));
        if (phase <= decEnd) {
            const float t = (phase - atkEnd) / std::max(decEnd - atkEnd, eps);
            return aLvl + (aLvl * sLvl - aLvl) * std::pow(t, toExp(dSlp));
        }
        if (phase <= susEnd) return aLvl * sLvl;
        {
            const float t = (phase - susEnd) / std::max(1.0f - susEnd, eps);
            return aLvl * sLvl * (1.0f - std::pow(t, toExp(rSlp)));
        }
    }

    static float evalBounce(float phase, float aLvl, float gate) noexcept {
        if (phase >= gate) return 0.0f;
        const float t      = phase / std::max(gate, 0.001f);
        const float decay  = std::exp(-4.0f * t);
        const float period = 0.33f;
        const float bPhase = std::fmod(t, period) / period;
        return clamp01(aLvl * decay
            * std::sin(juce::MathConstants<float>::pi * bPhase));
    }
};

} // namespace AutomationSeq
} // namespace APEX

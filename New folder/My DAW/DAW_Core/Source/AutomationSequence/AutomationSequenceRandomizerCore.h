// ============================================================
// AutomationSequenceRandomizerCore.h  — v3
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// 7 distinct random-pattern generators.  Each "flavor" produces
// a meaningfully different musical character — never duplicates
// because the seed is pulled from HistoryCore::getNextUnusedSeed().
//
// Stateless / header-only.
// ============================================================
#pragma once
#include "AutomationSequenceTypes.h"
#include "AutomationSequenceHistoryCore.h"
#include <random>

namespace APEX {
namespace AutomationSeq {

class RandomizerCore {
public:
    RandomizerCore() = delete;

    // Generate a new SequenceParams of the chosen flavor.
    // Always uses a fresh unused seed from HistoryCore — guarantees
    // the user never sees the same randomized pattern twice.
    static SequenceParams randomize(RandomFlavor flavor) {
        const uint32_t seed = HistoryCore::getNextUnusedSeed();
        SequenceParams p;
        p.seed = seed;
        std::mt19937 rng(seed);

        switch (flavor) {
            case RandomFlavor::Musical:      makeMelodic     (p, rng); break;
            case RandomFlavor::StepsOnly:     makeRhythmic    (p, rng); break;
            case RandomFlavor::ShapeOnly:       makeGlitch      (p, rng); break;
            case RandomFlavor::Mutate:      makeOrganic     (p, rng); break;
            case RandomFlavor::KnobsOnly: makeMathematical(p, rng); break;
            case RandomFlavor::Everything:     makeSurprise    (p, rng); break;
            case RandomFlavor::Wild:    makeFullChaos   (p, rng); break;
            default: makeSurprise(p, rng); break;
        }
        return p;
    }

private:
    // ---- per-flavor mode pools ---------------------------------
    static ShapeMode pickFromPool(const ShapeMode* pool, int count, std::mt19937& rng) {
        std::uniform_int_distribution<int> d(0, count - 1);
        return pool[d(rng)];
    }
    static float frand(std::mt19937& rng, float a, float b) {
        std::uniform_real_distribution<float> d(a, b);
        return d(rng);
    }
    static int irand(std::mt19937& rng, int a, int b) {
        std::uniform_int_distribution<int> d(a, b);
        return d(rng);
    }

    static void fillBaseParams(SequenceParams& p, std::mt19937& rng,
                                float minAct = 0.55f, float maxAct = 0.95f)
    {
        p.numSteps = 16;
        const float activeProb = frand(rng, minAct, maxAct);
        for (int i = 0; i < kMaxSteps; ++i) {
            p.steps[i] = {};
            p.steps[i].active            = frand(rng, 0.0f, 1.0f) < activeProb;
            p.steps[i].probability       = frand(rng, 0.7f, 1.0f);
            p.steps[i].localAttackLevel  = -1.0f;
            p.steps[i].localDecaySlope   = -1.0f;
            p.steps[i].localSustainLevel = -1.0f;
            p.steps[i].localReleaseSlope = -1.0f;
        }
        // Guarantee at least 3 active steps so the preview isn't empty
        int activeCount = 0;
        for (int i = 0; i < kMaxSteps; ++i)
            if (p.steps[i].active) activeCount++;
        while (activeCount < 3) {
            int i = irand(rng, 0, kMaxSteps - 1);
            if (!p.steps[i].active) {
                p.steps[i].active = true;
                activeCount++;
            }
        }
    }

    // ============================================================
    // Melodic — smooth, musical curves
    // ============================================================
    static void makeMelodic(SequenceParams& p, std::mt19937& rng) {
        static constexpr ShapeMode pool[] = {
            ShapeMode::Sine,    ShapeMode::SineSq,  ShapeMode::SineDbl,
            ShapeMode::StairUp,   ShapeMode::StairDn,   ShapeMode::ZigZag,
            ShapeMode::Fibonacci,   ShapeMode::Markov,   ShapeMode::SCurve,
            ShapeMode::ExpOut,  ShapeMode::Breathe, ShapeMode::Breathe,
        };
        p.mode         = pickFromPool(pool, sizeof(pool) / sizeof(pool[0]), rng);
        p.attackLevel  = frand(rng, 0.7f, 1.0f);
        p.attackSlope  = frand(rng, 0.3f, 0.6f);
        p.decaySlope   = frand(rng, 0.3f, 0.6f);
        p.sustainLevel = frand(rng, 0.4f, 0.8f);
        p.releaseSlope = frand(rng, 0.4f, 0.8f);
        p.gate         = frand(rng, 0.6f, 0.9f);
        p.swing        = frand(rng, 0.0f, 0.15f);
        p.timeMul      = frand(rng, 0.45f, 0.6f);
        p.chaos        = 0.0f;
        p.repeatCount  = (frand(rng, 0.0f, 1.0f) < 0.2f) ? 2 : 1;
        fillBaseParams(p, rng, 0.65f, 0.95f);
    }

    // ============================================================
    // Rhythmic — gate-heavy, square shapes, percussive
    // ============================================================
    static void makeRhythmic(SequenceParams& p, std::mt19937& rng) {
        static constexpr ShapeMode pool[] = {
            ShapeMode::Gate,    ShapeMode::Gate,  ShapeMode::NarrowPulse,
            ShapeMode::Euclid, ShapeMode::DoublePulse,  ShapeMode::TriplePulse,
            ShapeMode::WidePulse, ShapeMode::MorseRand,  ShapeMode::Telegraph,
            ShapeMode::Pluck,   ShapeMode::Ratchet, ShapeMode::AD,
            ShapeMode::Euclid,
        };
        p.mode         = pickFromPool(pool, sizeof(pool) / sizeof(pool[0]), rng);
        p.attackLevel  = frand(rng, 0.8f, 1.0f);
        p.attackSlope  = frand(rng, 0.0f, 0.2f);
        p.decaySlope   = frand(rng, 0.2f, 0.5f);
        p.sustainLevel = frand(rng, 0.0f, 0.3f);
        p.releaseSlope = frand(rng, 0.0f, 0.3f);
        p.gate         = frand(rng, 0.30f, 0.55f);
        p.swing        = frand(rng, 0.0f, 0.40f);
        p.timeMul      = frand(rng, 0.5f, 0.7f);
        p.chaos        = 0.0f;
        p.repeatCount  = irand(rng, 1, 3);
        fillBaseParams(p, rng, 0.50f, 0.85f);
    }

    // ============================================================
    // Glitch — chaotic, broken, digital
    // ============================================================
    static void makeGlitch(SequenceParams& p, std::mt19937& rng) {
        static constexpr ShapeMode pool[] = {
            ShapeMode::BitCrsh, ShapeMode::ChaosMp,  ShapeMode::Stutter,
            ShapeMode::RndGate, ShapeMode::RobotStep,  ShapeMode::RevSaw,
            ShapeMode::GateInv, ShapeMode::RevSaw, ShapeMode::ChaosMp,
            ShapeMode::WhiteNs, ShapeMode::LFSR,
        };
        p.mode         = pickFromPool(pool, sizeof(pool) / sizeof(pool[0]), rng);
        p.attackLevel  = frand(rng, 0.5f, 1.0f);
        p.attackSlope  = frand(rng, 0.0f, 1.0f);
        p.decaySlope   = frand(rng, 0.0f, 1.0f);
        p.sustainLevel = frand(rng, 0.0f, 0.9f);
        p.releaseSlope = frand(rng, 0.0f, 1.0f);
        p.gate         = frand(rng, 0.40f, 0.95f);
        p.swing        = frand(rng, 0.0f, 0.5f);
        p.timeMul      = frand(rng, 0.3f, 0.85f);
        p.chaos        = frand(rng, 0.25f, 0.65f);
        p.repeatCount  = irand(rng, 1, 6);
        fillBaseParams(p, rng, 0.40f, 0.90f);
    }

    // ============================================================
    // Organic — natural feel, physics/breathing
    // ============================================================
    static void makeOrganic(SequenceParams& p, std::mt19937& rng) {
        static constexpr ShapeMode pool[] = {
            ShapeMode::Breathe, ShapeMode::Breathe,    ShapeMode::Wobble,
            ShapeMode::Flutter,  ShapeMode::Spring,   ShapeMode::Heartbeat,
            ShapeMode::Bounce,  ShapeMode::Spring,  ShapeMode::Pendulum,
            ShapeMode::Pendulum, ShapeMode::Spring, ShapeMode::WaveFold,
            ShapeMode::Bounce, ShapeMode::Flutter, ShapeMode::SmthRnd,
        };
        p.mode         = pickFromPool(pool, sizeof(pool) / sizeof(pool[0]), rng);
        p.attackLevel  = frand(rng, 0.7f, 1.0f);
        p.attackSlope  = frand(rng, 0.4f, 0.85f);
        p.decaySlope   = frand(rng, 0.4f, 0.85f);
        p.sustainLevel = frand(rng, 0.3f, 0.75f);
        p.releaseSlope = frand(rng, 0.5f, 0.9f);
        p.gate         = frand(rng, 0.65f, 0.95f);
        p.swing        = frand(rng, 0.0f, 0.25f);
        p.timeMul      = frand(rng, 0.40f, 0.65f);
        p.chaos        = frand(rng, 0.05f, 0.20f);   // subtle organic noise
        p.repeatCount  = 1;
        fillBaseParams(p, rng, 0.70f, 0.95f);
    }

    // ============================================================
    // Mathematical — deterministic curves and LFOs
    // ============================================================
    static void makeMathematical(SequenceParams& p, std::mt19937& rng) {
        static constexpr ShapeMode pool[] = {
            ShapeMode::ExpIn,    ShapeMode::ExpOut,  ShapeMode::SCurve,
            ShapeMode::LogCurve, ShapeMode::Convex,   ShapeMode::Concave,
            ShapeMode::ExpIn,   ShapeMode::ExpOut, ShapeMode::Saw,
            ShapeMode::RevSaw,   ShapeMode::Triangle, ShapeMode::Triangle,
            ShapeMode::Triangle,   ShapeMode::Sine,  ShapeMode::Saw,
            ShapeMode::Gate,   ShapeMode::Comb,
        };
        p.mode         = pickFromPool(pool, sizeof(pool) / sizeof(pool[0]), rng);
        p.attackLevel  = frand(rng, 0.6f, 1.0f);
        p.attackSlope  = frand(rng, 0.0f, 1.0f);
        p.decaySlope   = frand(rng, 0.0f, 1.0f);
        p.sustainLevel = frand(rng, 0.0f, 1.0f);
        p.releaseSlope = frand(rng, 0.0f, 1.0f);
        p.gate         = frand(rng, 0.55f, 0.95f);
        p.swing        = 0.0f;
        p.timeMul      = frand(rng, 0.40f, 0.65f);
        p.chaos        = 0.0f;
        p.repeatCount  = irand(rng, 1, 4);
        fillBaseParams(p, rng, 0.70f, 1.0f);
    }

    // ============================================================
    // Surprise — any mode, modest params
    // ============================================================
    static void makeSurprise(SequenceParams& p, std::mt19937& rng) {
        const int idx = irand(rng, 0, static_cast<int>(ShapeMode::kCount) - 1);
        p.mode         = static_cast<ShapeMode>(idx);
        p.attackLevel  = frand(rng, 0.5f, 1.0f);
        p.attackSlope  = frand(rng, 0.1f, 0.8f);
        p.decaySlope   = frand(rng, 0.1f, 0.8f);
        p.sustainLevel = frand(rng, 0.2f, 0.8f);
        p.releaseSlope = frand(rng, 0.1f, 0.8f);
        p.gate         = frand(rng, 0.5f, 0.9f);
        p.swing        = frand(rng, 0.0f, 0.3f);
        p.timeMul      = frand(rng, 0.4f, 0.65f);
        p.chaos        = frand(rng, 0.0f, 0.25f);
        p.repeatCount  = irand(rng, 1, 4);
        fillBaseParams(p, rng, 0.55f, 0.90f);
    }

    // ============================================================
    // Full Chaos — every parameter random, anything goes
    // ============================================================
    static void makeFullChaos(SequenceParams& p, std::mt19937& rng) {
        const int idx = irand(rng, 0, static_cast<int>(ShapeMode::kCount) - 1);
        p.mode         = static_cast<ShapeMode>(idx);
        p.attackLevel  = frand(rng, 0.0f, 1.0f);
        p.attackSlope  = frand(rng, 0.0f, 1.0f);
        p.decaySlope   = frand(rng, 0.0f, 1.0f);
        p.sustainLevel = frand(rng, 0.0f, 1.0f);
        p.releaseSlope = frand(rng, 0.0f, 1.0f);
        p.gate         = frand(rng, 0.1f, 1.0f);
        p.swing        = frand(rng, 0.0f, 0.6f);
        p.timeMul      = frand(rng, 0.0f, 1.0f);
        p.chaos        = frand(rng, 0.0f, 0.8f);
        p.humanize     = frand(rng, 0.0f, 0.5f);
        p.repeatCount  = irand(rng, 1, 8);
        fillBaseParams(p, rng, 0.30f, 1.0f);
    }
};

} // namespace AutomationSeq
} // namespace APEX

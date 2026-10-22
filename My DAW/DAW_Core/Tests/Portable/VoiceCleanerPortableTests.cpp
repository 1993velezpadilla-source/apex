// Standalone C++17 regression tests for APEX Voice Cleaner.
// No DAW/JUCE dependency, so the spectral core is tested deterministically.
#include "../../Builds/VisualStudio2026/ArrangementEditor/VoiceCleanerDspCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static double energy(const std::vector<float>& v, int a, int b)
{
    double sum = 0;
    for (int i = a; i < b; ++i) sum += (double)v[(size_t)i] * v[(size_t)i];
    return std::sqrt(sum / (b - a));
}

int main()
{
    using ArrangementEditor::VoiceCleanerDspCore;
    constexpr int sampleRate = 48000;
    constexpr int count = sampleRate * 2;
    std::vector<float> dirty(count), clean(count), bypass(count);
    constexpr double tau = 6.28318530717958647692;
    for (int i = 0; i < count; ++i)
    {
        const double t = (double)i / sampleRate;
        const float voice = (t >= 0.5 && t < 1.5)
            ? 0.27f * (float)std::sin(tau * 220.0 * t) : 0.f;
        // Continuous high-frequency cricket / whistle, also under the voice.
        dirty[(size_t)i] = voice + 0.04f * (float)std::sin(tau * 4600.0 * t);
    }

    assert(VoiceCleanerDspCore::process(dirty.data(), bypass.data(), count, sampleRate, 0.f));
    assert(bypass == dirty);
    assert(VoiceCleanerDspCore::process(dirty.data(), clean.data(), count, sampleRate, 0.68f));
    for (float v : clean) assert(std::isfinite(v));
    // Quiet-region cricket must attenuate; no promise of perfect cancellation.
    const auto inputNoise = energy(dirty, count / 10, count / 5);
    const auto outputNoise = energy(clean, count / 10, count / 5);
    std::cout << "noiseRms " << inputNoise << " -> " << outputNoise << "\n";
    assert(outputNoise < inputNoise * 0.75);

    // Fundamental vocal frequency below 110? 220 Hz remains protected by
    // conservative spectral subtraction and never collapses entirely.
    const auto voicedInput = energy(dirty, sampleRate * 3 / 4, sampleRate * 5 / 4);
    const auto voicedOutput = energy(clean, sampleRate * 3 / 4, sampleRate * 5 / 4);
    assert(voicedOutput > voicedInput * 0.75);
    assert(voicedOutput < voicedInput * 1.25);

    // Focus mode must reduce persistent cricket tones, without introducing
    // non-finite samples, changing duration, or muting the sung fundamental.
    // Profile learning is intentionally challenged by a small chorus of
    // simultaneous high pitched tonal components beneath the same vocal.
    std::vector<float> mixed(count), focused(count), broad(count), untouched(count);
    for (int i = 0; i < count; ++i)
    {
        const double t = (double)i / sampleRate;
        const float vocal = t >= 0.4 && t < 1.7
            ? 0.20f * (float)std::sin(tau * 247.0 * t)
            : 0.0f;
        const float insects = 0.025f * (float)std::sin(tau * 4180.0 * t)
                            + 0.017f * (float)std::sin(tau * 6170.0 * t);
        mixed[(size_t)i] = vocal + insects;
    }
    VoiceCleanerDspCore::Options settings;
    settings.strength = 0.85f;
    settings.voiceProtection = 0.82f;
    settings.focus = VoiceCleanerDspCore::Focus::Crickets;
    assert(VoiceCleanerDspCore::process(mixed.data(), focused.data(), count,
                                       sampleRate, settings));
    settings.focus = VoiceCleanerDspCore::Focus::Broadband;
    assert(VoiceCleanerDspCore::process(mixed.data(), broad.data(), count,
                                       sampleRate, settings));
    settings.strength = 0.0f;
    assert(VoiceCleanerDspCore::process(mixed.data(), untouched.data(), count,
                                       sampleRate, settings));
    assert(untouched == mixed);
    for (const auto& sample : focused)
        assert(std::isfinite(sample));
    // Quiet gap: learned cricket profile suppresses the noise.
    const double dirtyGap = energy(mixed, 2000, 14500);
    const double focusGap = energy(focused, 2000, 14500);
    std::cout << "focused-crickets " << dirtyGap << " -> " << focusGap << "\\n";
    assert(focusGap < dirtyGap * 0.85);
    // Speech should remain readily audible even during strong cricket focus.
    assert(energy(focused, 26000, 50000) >
           energy(mixed, 26000, 50000) * 0.60);

    // Live-audition contract: the Amount knob uses dry/wet mixing of a
    // precomputed 100%-wet profile; it must match offline Apply at ANY amount
    // up to floating point rounding, including while a clip is playing.
    VoiceCleanerDspCore::Options full;
    full.strength = 1.0f;
    full.focus = VoiceCleanerDspCore::Focus::Crickets;
    full.voiceProtection = 0.85f;
    std::vector<float> fullyClean(count), fromKnob(count), fromApply(count);
    assert(VoiceCleanerDspCore::process(mixed.data(), fullyClean.data(),
                                       count, sampleRate, full));
    constexpr float kAmount = 0.37f;
    for (int i = 0; i < count; ++i)
        fromKnob[(size_t)i] = mixed[(size_t)i]
            + kAmount * (fullyClean[(size_t)i] - mixed[(size_t)i]);
    full.strength = kAmount;
    assert(VoiceCleanerDspCore::process(mixed.data(), fromApply.data(),
                                       count, sampleRate, full));
    double maxPreviewError = 0.0;
    for (int i = 0; i < count; ++i)
        maxPreviewError = std::max(maxPreviewError,
            std::abs((double)fromKnob[(size_t)i]
                     - (double)fromApply[(size_t)i]));
    std::cout << "preview_vs_apply_max_err=" << maxPreviewError << "\\n";
    assert(maxPreviewError < 1.0e-5);

    // Silence, one-sample input, bad args, and non-overrun tail frames.
    std::vector<float> silence(8192, 0.f), result(silence.size(), 99.f);
    assert(VoiceCleanerDspCore::process(silence.data(), result.data(), 8192, sampleRate));
    assert(std::all_of(result.begin(), result.end(), [](float x){return x == 0.f;}));
    assert(VoiceCleanerDspCore::process(dirty.data(), result.data(), 1, sampleRate));
    assert(result[0] == dirty[0]);
    assert(!VoiceCleanerDspCore::process(nullptr, result.data(), 1, sampleRate));
    assert(!VoiceCleanerDspCore::process(result.data(), result.data(), 1, 0));
    std::cout << "APEX Voice Cleaner portable DSP GREEN\n";
    return 0;
}

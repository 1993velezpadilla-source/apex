#pragma once
#include <atomic>

namespace DAW {

enum class SpeakerSetId { A = 0, B = 1, C = 2 };
enum class MonitorSourceId { MainMix = 0, ListenBus, Cue, Reference };

/**
 * MonitorStateModel — thread-safe monitor section state.
 *
 * All fields are std::atomic so the audio thread can read them lock-free
 * while the UI thread writes. This is the single source of truth for
 * every monitor-path parameter.
 *
 * Architectural rule: nothing in this model touches the render/export path.
 */
struct MonitorStateModel
{
    // ── Monitor level (linear gain: 0.0 = silence, 1.0 = unity) ─────────
    std::atomic<float> monitorGain { 1.0f };

    // ── Dim ──────────────────────────────────────────────────────────────
    std::atomic<bool>  dimActive   { false };
    std::atomic<float> dimAmountDb { -18.0f };

    // ── Mute ─────────────────────────────────────────────────────────────
    std::atomic<bool>  muteActive  { false };

    // ── Mono fold-down ───────────────────────────────────────────────────
    std::atomic<bool>  monoActive  { false };

    // ── Speaker select ───────────────────────────────────────────────────
    std::atomic<int>   activeSpeakerSet { (int)SpeakerSetId::A };

    // ── Monitor FX bypass ────────────────────────────────────────────────
    std::atomic<bool>  monitorFxBypassed { true };

    // ── Monitor source (future: listen bus, cue, reference) ──────────────
    std::atomic<int>   monitorSource { (int)MonitorSourceId::MainMix };

    // ── Monitor meter peaks (written by audio thread, read by UI) ────────
    mutable std::atomic<float> meterPeakL  { 0.0f };
    mutable std::atomic<float> meterPeakR  { 0.0f };

    // ── Helpers (UI thread convenience) ──────────────────────────────────
    float getMonitorGainDb() const noexcept
    {
        float g = monitorGain.load(std::memory_order_relaxed);
        return g <= 0.000001f ? -120.0f : 20.0f * std::log10(g);
    }

    void setMonitorGainDb(float db) noexcept
    {
        monitorGain.store(std::pow(10.0f, db / 20.0f), std::memory_order_relaxed);
    }
};

} // namespace DAW

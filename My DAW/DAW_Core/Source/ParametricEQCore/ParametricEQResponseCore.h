#pragma once

#include "ParametricEQEngine.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>

namespace APEX::ParametricEQ
{

// A full 2x2 complex transfer. Mixed placements create crossfeed, so a single
// scalar curve can no longer describe the processor; consumers choose a
// labelled projection (L->L, R->R, M->M, S->S) from this matrix.
struct StereoTransfer
{
    std::complex<double> ll { 1.0, 0.0 };
    std::complex<double> lr { 0.0, 0.0 };
    std::complex<double> rl { 0.0, 0.0 };
    std::complex<double> rr { 1.0, 0.0 };

    static StereoTransfer identity() noexcept { return {}; }

    static StereoTransfer composed (
        const StereoTransfer& next, const StereoTransfer& current) noexcept
    {
        return {
            next.ll * current.ll + next.lr * current.rl,
            next.ll * current.lr + next.lr * current.rr,
            next.rl * current.ll + next.rr * current.rl,
            next.rl * current.lr + next.rr * current.rr
        };
    }

    StereoTransfer lerp (const StereoTransfer& other, double mix) const noexcept
    {
        return {
            ll + mix * (other.ll - ll),
            lr + mix * (other.lr - lr),
            rl + mix * (other.rl - rl),
            rr + mix * (other.rr - rr)
        };
    }

    std::complex<double> midProjection() const noexcept
    {
        return 0.5 * (ll + lr + rl + rr);
    }

    std::complex<double> sideProjection() const noexcept
    {
        return 0.5 * (ll - lr - rl + rr);
    }
};

// A coherent description of the transfer that the processor is rendering.
// The audio thread publishes these fixed-size frames through ResponseCore;
// GUI/analysis code computes curves from the frame and never reads mutable
// Engine state directly.
struct ResponseFrame
{
    std::array<BandSettings, kMaxBands> currentBands {};
    std::array<BandSettings, kMaxBands> targetBands {};
    DesignMode currentMode = DesignMode::Realtime;
    DesignMode targetMode = DesignMode::Realtime;
    double sampleRate = 44100.0;
    int channelCount = 2;
    double transitionMix = 0.0; // parallel current -> target crossfade
    double bypassMix = 0.0;     // wet -> dry crossfade
    bool auditionActive = false;
    int auditionBand = -1;
    double auditionMix = 0.0;
    std::uint32_t dynamicActiveMask = 0;
    std::array<double, kMaxBands> dynamicGainDb {};
    std::uint64_t generation = 0;
    std::uint64_t prepareEpoch = 0;
};

class ResponseCore final
{
public:
    static constexpr int kCapacity = 32;

    static_assert ((kCapacity & (kCapacity - 1)) == 0,
                   "Response queue capacity must be a power of two");

    /** Control/lifecycle thread: invalidate frames from the old preparation.
        No payload is touched, so a concurrent consumer cannot race this call. */
    std::uint64_t beginPrepareEpoch() noexcept
    {
        return prepareEpoch_.fetch_add (1, std::memory_order_acq_rel) + 1;
    }

    std::uint64_t getPrepareEpoch() const noexcept
    {
        return prepareEpoch_.load (std::memory_order_acquire);
    }

    /** Audio thread only. Drops the newest visualization frame if the GUI has
        not drained the fixed queue; audio is never delayed or overwritten. */
    bool push (const ResponseFrame& frame) noexcept
    {
        const auto write = write_.load (std::memory_order_relaxed);
        const auto read = read_.load (std::memory_order_acquire);
        if (write - read >= static_cast<std::uint32_t> (kCapacity))
        {
            drops_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        frames_[write & (kCapacity - 1)] = frame;
        write_.store (write + 1, std::memory_order_release);
        return true;
    }

    /** GUI/analysis consumer only. Drains to the newest coherent frame and
        rejects frames from an earlier prepare epoch. */
    bool consumeLatest (ResponseFrame& destination) noexcept
    {
        auto read = read_.load (std::memory_order_relaxed);
        const auto write = write_.load (std::memory_order_acquire);
        if (read == write)
            return false;

        ResponseFrame latest;
        while (read != write)
        {
            latest = frames_[read & (kCapacity - 1)];
            ++read;
        }
        read_.store (read, std::memory_order_release);

        if (latest.prepareEpoch != getPrepareEpoch())
            return false;
        destination = latest;
        return true;
    }

    int getDropCount() const noexcept
    {
        return drops_.load (std::memory_order_relaxed);
    }

    static std::complex<double> bandResponse (const BandSettings& rawSettings,
                                               DesignMode mode,
                                               double sampleRate,
                                               double frequencyHz) noexcept
    {
        const auto settings = sanitise (rawSettings, sampleRate);
        if (! settings.enabled || settings.bypassed)
            return { 1.0, 0.0 };

        if (! isCutShape (settings.shape))
            return FilterDesigner::designBand (settings, sampleRate, mode)
                .response (frequencyHz, sampleRate);

        const auto exactOrder = settings.slopeDbPerOctave / 6.0;
        const auto lowerOrder = std::clamp (
            static_cast<int> (std::floor (exactOrder)), 0, kMaxCutOrder);
        const auto upperOrder = std::min (kMaxCutOrder, lowerOrder + 1);
        const auto mix = upperOrder == lowerOrder
                       ? 0.0 : std::clamp (exactOrder - lowerOrder, 0.0, 1.0);
        const bool highPass = settings.shape == FilterShape::LowCut;
        const auto lower = FilterDesigner::designButterworthCut (
            highPass, lowerOrder, settings.frequencyHz, sampleRate, mode)
                .response (frequencyHz, sampleRate);
        if (mix <= 0.0)
            return lower;
        const auto upper = FilterDesigner::designButterworthCut (
            highPass, upperOrder, settings.frequencyHz, sampleRate, mode)
                .response (frequencyHz, sampleRate);
        return lower + mix * (upper - lower);
    }

    // Per-band transfer matrix for a scalar component response H.
    //   Stereo: diag(H, H)   Left: diag(H, 1)   Right: diag(1, H)
    //   Mid:    0.5*[[H+1, H-1], [H-1, H+1]]
    //   Side:   0.5*[[H+1, 1-H], [1-H, H+1]]
    static StereoTransfer bandTransfer (const BandSettings& rawSettings,
                                        DesignMode mode,
                                        double sampleRate,
                                        double frequencyHz) noexcept
    {
        const auto settings = sanitise (rawSettings, sampleRate);
        const auto h = bandResponse (settings, mode, sampleRate, frequencyHz);
        const auto one = std::complex<double> { 1.0, 0.0 };
        switch (settings.placement)
        {
            case ChannelPlacement::Stereo:
                return { h, 0.0, 0.0, h };
            case ChannelPlacement::Left:
                return { h, 0.0, 0.0, one };
            case ChannelPlacement::Right:
                return { one, 0.0, 0.0, h };
            case ChannelPlacement::Mid:
            {
                const auto half = std::complex<double> { 0.5, 0.0 };
                return { half * (h + one), half * (h - one),
                         half * (h - one), half * (h + one) };
            }
            case ChannelPlacement::Side:
            {
                const auto half = std::complex<double> { 0.5, 0.0 };
                return { half * (h + one), half * (one - h),
                         half * (one - h), half * (h + one) };
            }
        }
        return StereoTransfer::identity();
    }

    static StereoTransfer wetTransfer (
        const std::array<BandSettings, kMaxBands>& bands,
        DesignMode mode,
        double sampleRate,
        double frequencyHz) noexcept
    {
        auto transfer = StereoTransfer::identity();
        for (int index = kMaxBands - 1; index >= 0; --index)
            transfer = StereoTransfer::composed (
                bandTransfer (bands[static_cast<std::size_t> (index)], mode,
                              sampleRate, frequencyHz),
                transfer);
        return transfer;
    }

    /** Exact stereo transfer of the processor's two nested parallel
        crossfades, composed in fixed slot order. */
    static StereoTransfer responseMatrix (const ResponseFrame& frame,
                                          double frequencyHz) noexcept
    {
        const auto rate = std::isfinite (frame.sampleRate) && frame.sampleRate > 1.0
                        ? frame.sampleRate : 44100.0;
        const auto transition = std::clamp (frame.transitionMix, 0.0, 1.0);
        const auto bypass = std::clamp (frame.bypassMix, 0.0, 1.0);
        auto current = wetTransfer (frame.currentBands, frame.currentMode,
                                    rate, frequencyHz);
        if (transition > 0.0)
        {
            const auto target = wetTransfer (frame.targetBands, frame.targetMode,
                                             rate, frequencyHz);
            current = current.lerp (target, transition);
        }
        return current.lerp (StereoTransfer::identity(), bypass);
    }

    static std::complex<double> response (const ResponseFrame& frame,
                                          double frequencyHz) noexcept
    {
        return responseMatrix (frame, frequencyHz).ll;
    }

    static double magnitudeDb (const ResponseFrame& frame,
                               double frequencyHz) noexcept
    {
        return 20.0 * std::log10 (std::max (1.0e-15,
                                            std::abs (response (frame,
                                                                frequencyHz))));
    }

private:
    std::array<ResponseFrame, kCapacity> frames_ {};
    alignas (64) std::atomic<std::uint32_t> write_ { 0 };
    alignas (64) std::atomic<std::uint32_t> read_ { 0 };
    alignas (64) std::atomic<int> drops_ { 0 };
    alignas (64) std::atomic<std::uint64_t> prepareEpoch_ { 0 };
};

} // namespace APEX::ParametricEQ

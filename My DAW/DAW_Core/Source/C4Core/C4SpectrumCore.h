#pragma once
#include <JuceHeader.h>

#include <atomic>
#include <cmath>

namespace APEX {
namespace C4 {

// ============================================================================
// C4SpectrumCore — Phase 6: the C4 SPECTRUM FLAG (observational analyzer).
//
// Architecture (per the C4 master specification):
//
//   audio thread:  realtime-safe tap (mono-mix copy, preallocated ring)
//        -> SPSC ring (PRE and/or POST, per the tap mode)
//        -> analyzer worker thread (low priority; dormant when Closed)
//        -> Hann-windowed FFT + smoothed magnitude snapshot (double-buffered,
//           published by index — the GUI reads the latest at 60 FPS while the
//           FFT updates at its own slower rate)
//        -> GUI (Phase 7) draws the spectra + the total response curve
//           (post - pre in BOTH mode)
//
// Hard rules:
//   - NO FFT in processBlock. The audio thread only copies.
//   - NO effect on the audio output: the tap is read-only, bit-identical
//     output with the flag on/off (pinned by tests).
//   - NO allocation, locks, waits, or GUI calls in the tap path.
//   - Closed (default): the tap and the worker are dormant — the flag costs
//     nothing until the user opens it.
//   - Observational only: no draggable EQ nodes, no spectrum grab, no
//     auto-EQ, no parametric editing (the G10 three-clean-bell system is a
//     G10 concept and is NOT part of C4).
//
// Tap modes: Closed=0, Pre=1, Post=2, Both=3 (persisted as processor state,
// NOT a hosted parameter — the 19-parameter ABI stays frozen).
//
// Thread contract:
//   setTapMode()      -> message/UI thread (starts/stops the worker)
//   prepare()         -> message thread (prepareToPlay; audio stopped)
//   pushPre/pushPost  -> audio thread only
//   worker thread     -> owns the FFT + smoothing; publishes snapshots
//   getSnapshot()     -> any thread (atomic index + plain float reads)
// ============================================================================

enum class C4SpectrumTapMode
{
    Closed = 0,   // flag hidden; analyzer dormant
    Pre    = 1,   // input spectrum only
    Post   = 2,   // output spectrum only
    Both   = 3    // input + output; the GUI derives the total response curve
};

// ---------------------------------------------------------------------------
// C4SpectrumFifo — lock-free SPSC ring (the G10 pattern, audio->worker).
// ---------------------------------------------------------------------------

class C4SpectrumFifo final
{
public:
    static constexpr int kCapacity = 16384; // ~170 ms at 48 kHz (mono-mixed)

    void reset() noexcept
    {
        read_.store (write_.load (std::memory_order_acquire), std::memory_order_release);
    }

    /** Audio thread: copy n mono-mixed samples. Drops the newest block when
        full — never overwrites unread data, never blocks. */
    void push (const float* data, int n) noexcept
    {
        if (data == nullptr || n <= 0)
            return;

        const uint32_t w = write_.load (std::memory_order_relaxed);
        const uint32_t r = read_.load (std::memory_order_acquire);

        if (w - r + (uint32_t) n > (uint32_t) kCapacity)
        {
            dropCount_.fetch_add (1, std::memory_order_relaxed);
            return; // full: drop the newest visualization block
        }

        for (int i = 0; i < n; ++i)
            data_[(w + (uint32_t) i) & (kCapacity - 1)] = data[i];

        write_.store (w + (uint32_t) n, std::memory_order_release);
        pushCount_.fetch_add (1, std::memory_order_relaxed);
    }

    /** Worker thread: copy up to maxN oldest samples; returns count. */
    int drain (float* dst, int maxN) noexcept
    {
        if (dst == nullptr || maxN <= 0)
            return 0;

        const uint32_t r = read_.load (std::memory_order_relaxed);
        const uint32_t w = write_.load (std::memory_order_acquire);
        const uint32_t available = w - r;
        if (available == 0)
            return 0;

        const int n = (int) juce::jmin ((uint32_t) maxN, available);
        for (int i = 0; i < n; ++i)
            dst[i] = data_[(r + (uint32_t) i) & (kCapacity - 1)];
        read_.store (r + (uint32_t) n, std::memory_order_release);
        return n;
    }

    int available() const noexcept
    {
        return (int) (write_.load (std::memory_order_acquire)
                    - read_.load (std::memory_order_acquire));
    }

    /** Diagnostics (relaxed; tests/forensics only, never the hot path). */
    int pushCount() const noexcept  { return pushCount_.load (std::memory_order_relaxed); }
    int dropCount() const noexcept  { return dropCount_.load (std::memory_order_relaxed); }

private:
    alignas (64) float data_[kCapacity] = {};
    alignas (64) std::atomic<uint32_t> write_ { 0 };
    alignas (64) std::atomic<uint32_t> read_  { 0 };
    alignas (64) std::atomic<int> pushCount_ { 0 };
    alignas (64) std::atomic<int> dropCount_ { 0 };
};

// ---------------------------------------------------------------------------
// C4SpectrumCore — tap plumbing + the analyzer worker + published snapshots.
// ---------------------------------------------------------------------------

class C4SpectrumCore final
{
public:
    static constexpr int kFftSize = 4096;             // power of two
    static constexpr int kSpectrumBins = kFftSize / 2 + 1;
    static constexpr int kMaxTapBlock = 8192;         // mono-mix scratch, fixed

    C4SpectrumCore()
        : worker_ ("C4 Spectrum Analyzer", *this)
    {
        buildTwiddles();
    }

    ~C4SpectrumCore()
    {
        worker_.stopThread (500);
    }

    // ---- Message/UI thread -----------------------------------------------

    void prepare (double sampleRate) noexcept
    {
        // Quiesce the worker BEFORE touching the worker-owned staging/FFT
        // state (the host has stopped the audio thread, but the worker is a
        // separate thread and must not race this reset).
        const C4SpectrumTapMode mode = (C4SpectrumTapMode) mode_.load (std::memory_order_acquire);
        worker_.stopThread (500);

        const float rate = (float) juce::jmax (1.0, sampleRate);
        rate_.store (rate, std::memory_order_release);
        binHz_.store (rate / kFftSize, std::memory_order_release);

        preFifo_.reset();
        postFifo_.reset();
        // Staging + snapshot buffers keep their memory; the contents are
        // discarded so a rate change cannot mix stale spectra. The snapshot
        // index resets too: old-rate spectra are invalid and must never be
        // presented by the GUI.
        resetWorkerState();
        snapshotIndex_.store (0, std::memory_order_release);

        if (mode != C4SpectrumTapMode::Closed)
            worker_.startThread (juce::Thread::Priority::low);
    }

    /** Start/stop the analyzer per the tap mode. Idempotent; never called
        on the audio thread. */
    void setTapMode (C4SpectrumTapMode mode) noexcept
    {
        mode_.store ((int) mode, std::memory_order_release);
        if (mode == C4SpectrumTapMode::Closed)
        {
            worker_.stopThread (500);
            preFifo_.reset();
            postFifo_.reset();
            resetWorkerState();
            snapshotIndex_.store (0, std::memory_order_release);
        }
        else
        {
            if (! worker_.isThreadRunning())
                worker_.startThread (juce::Thread::Priority::low);
        }
    }

    C4SpectrumTapMode getTapMode() const noexcept
    {
        return (C4SpectrumTapMode) mode_.load (std::memory_order_acquire);
    }

    // ---- Audio thread ------------------------------------------------------

    /** Read-only tap: mixes the input channels down (mono) and copies the
        block into the PRE ring. O(2N) adds; no allocation, no locks. */
    void pushPre (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        if ((mode_.load (std::memory_order_relaxed) & 1) == 0)
            return;
        mixAndPush (buffer, numChannels, numSamples, preFifo_);
    }

    /** Read-only tap: the true output (post engine, post bypass crossfade). */
    void pushPost (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        if ((mode_.load (std::memory_order_relaxed) & 2) == 0)
            return;
        mixAndPush (buffer, numChannels, numSamples, postFifo_);
    }

    // ---- Any thread ---------------------------------------------------------

    struct Snapshot
    {
        int index = -1;                 // monotonically increasing publish count
        bool preActive = false;         // pre spectrum updating under this mode
        bool postActive = false;
        const float* preDb = nullptr;   // kSpectrumBins dB magnitudes
        const float* postDb = nullptr;  // kSpectrumBins dB magnitudes
        float binHz = 0.0f;
        float sampleRate = 0.0f;
    };

    /** Cheap, lock-free snapshot read: the GUI polls this at 60 FPS. */
    Snapshot getSnapshot() const noexcept
    {
        Snapshot s;
        s.index = snapshotIndex_.load (std::memory_order_acquire);
        s.preActive = preActive_.load (std::memory_order_acquire);
        s.postActive = postActive_.load (std::memory_order_acquire);
        s.binHz = binHz_.load (std::memory_order_acquire);
        s.sampleRate = rate_.load (std::memory_order_acquire);
        if (s.index > 0)
        {
            const int buf = (s.index - 1) & 1;
            s.preDb = preDb_[buf];
            s.postDb = postDb_[buf];
        }
        return s;
    }

    /** Test hook: wait (up to timeoutMs) until the snapshot index advances
        past `after`. Returns the latest index. */
    int waitForSnapshot (int after, int timeoutMs = 2000) const
    {
        const juce::uint32 deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        int idx = snapshotIndex_.load (std::memory_order_acquire);
        while (idx <= after && juce::Time::getMillisecondCounter() < deadline)
        {
            juce::Thread::sleep (5);
            idx = snapshotIndex_.load (std::memory_order_acquire);
        }
        return idx;
    }

    /** Test introspection: bytes currently buffered per tap. */
    int availablePre() const noexcept  { return preFifo_.available(); }
    int availablePost() const noexcept { return postFifo_.available(); }
    bool isWorkerRunning() const noexcept { return worker_.isThreadRunning(); }

    /** Diagnostics (forensics; relaxed atomics, never the hot path). */
    int prePushCount() const noexcept  { return preFifo_.pushCount(); }
    int postPushCount() const noexcept { return postFifo_.pushCount(); }
    int preDropCount() const noexcept  { return preFifo_.dropCount(); }
    int postDropCount() const noexcept { return postFifo_.dropCount(); }
    int preConsumed() const noexcept   { return (int) consumedPre_.load (std::memory_order_relaxed); }
    int postConsumed() const noexcept  { return (int) consumedPost_.load (std::memory_order_relaxed); }
    int preWindowStart() const noexcept { return (int) preWindowStart_.load (std::memory_order_relaxed); }
    int postWindowStart() const noexcept { return (int) postWindowStart_.load (std::memory_order_relaxed); }

private:
    // ---- worker -------------------------------------------------------------

    class WorkerThread final : public juce::Thread
    {
    public:
        WorkerThread (const juce::String& name, C4SpectrumCore& owner)
            : juce::Thread (name), owner_ (owner) {}

        void run() override
        {
            while (! threadShouldExit())
            {
                juce::Thread::sleep (20);
                owner_.workerTick();
            }
        }

    private:
        C4SpectrumCore& owner_;
    };

    void workerTick() noexcept
    {
        const int mode = mode_.load (std::memory_order_acquire);
        const bool wantPre = (mode & 1) != 0;
        const bool wantPost = (mode & 2) != 0;
        if (! wantPre && ! wantPost)
            return;

        // Drain what is available into the staging buffers (bounded).
        int gotPre = 0, gotPost = 0;
        if (wantPre)
        {
            gotPre = preFifo_.drain (staging_, kMaxTapBlock);
            consumedPre_.fetch_add (gotPre, std::memory_order_relaxed);
        }
        if (wantPost)
        {
            gotPost = postFifo_.drain (stagingPost_, kMaxTapBlock);
            consumedPost_.fetch_add (gotPost, std::memory_order_relaxed);
        }

        appendStaging (preAcc_, preCount_, wantPre, staging_, gotPre);
        appendStaging (postAcc_, postCount_, wantPost, stagingPost_, gotPost);

        // FFT whenever a full window is staged; consume the LAST window.
        if (preCount_ >= kFftSize || postCount_ >= kFftSize)
        {
            const int nextBuf = snapshotIndex_.load (std::memory_order_relaxed) & 1;

            if (wantPre && preCount_ >= kFftSize)
            {
                computeSpectrum (preAcc_, preCount_, preDb_[nextBuf],
                                 preSmoothed_, preSmoothingInit_);
                preWindowStart_.store (
                    consumedPre_.load (std::memory_order_relaxed) - kFftSize,
                    std::memory_order_relaxed);
            }
            if (wantPost && postCount_ >= kFftSize)
            {
                computeSpectrum (postAcc_, postCount_, postDb_[nextBuf],
                                 postSmoothed_, postSmoothingInit_);
                postWindowStart_.store (
                    consumedPost_.load (std::memory_order_relaxed) - kFftSize,
                    std::memory_order_relaxed);
            }

            preActive_.store (wantPre, std::memory_order_release);
            postActive_.store (wantPost, std::memory_order_release);
            snapshotIndex_.fetch_add (1, std::memory_order_release);

            // Keep the newest leftover samples (continuity across windows).
            if (preCount_ > kFftSize)
            {
                const int keep = preCount_ - kFftSize;
                for (int i = 0; i < keep; ++i)
                    preAcc_[i] = preAcc_[kFftSize + i];
                preCount_ = keep;
            }
            else
                preCount_ = 0;

            if (postCount_ > kFftSize)
            {
                const int keep = postCount_ - kFftSize;
                for (int i = 0; i < keep; ++i)
                    postAcc_[i] = postAcc_[kFftSize + i];
                postCount_ = keep;
            }
            else
                postCount_ = 0;
        }
    }

    static void appendStaging (float* acc, int& count, bool want,
                               const float* src, int n) noexcept
    {
        if (! want)
        {
            count = 0;
            return;
        }
        const int space = (kFftSize * 2) - count;
        const int take = juce::jmin (space, n);
        for (int i = 0; i < take; ++i)
            acc[count + i] = src[i];
        count += take;
    }

    /** One FFT + smoothing into `outDb` using the stream's OWN smoothing
        history (`smoothed`) and init flag. The PRE and POST streams must
        NEVER share smoothing state: a shared one-pole couples the two
        histories and compresses the measured post-pre difference by the
        pole factor (measured: +1.74 dB instead of +6.0 dB at 0.55/0.45). */
    void computeSpectrum (const float* window, int count, float* outDb,
                          float* smoothed, bool& smoothingInit) noexcept
    {
        const int n = kFftSize;
        const int start = count - n; // newest full window
        for (int i = 0; i < n; ++i)
        {
            const float hann = (float) (0.5 - 0.5 * std::cos (
                2.0 * juce::MathConstants<double>::pi * i / (n - 1)));
            fftRe_[i] = window[start + i] * hann;
            fftIm_[i] = 0.0f;
        }
        fft (fftRe_, fftIm_);

        const float normDb = (float) (20.0 * std::log10 ((double) n * 0.25)); // Hann coherent gain 0.5
        for (int b = 0; b < kSpectrumBins; ++b)
        {
            const double mag = std::sqrt ((double) fftRe_[b] * fftRe_[b]
                                        + (double) fftIm_[b] * fftIm_[b]);
            float db = (float) (20.0 * std::log10 (std::max (1e-9, mag))) - normDb;
            db = juce::jlimit (-140.0f, 40.0f, db);
            // First frame after a reset copies directly (no cold-start pull
            // from the -140 dB initial state); afterwards one-pole smoothing.
            smoothed[b] = smoothingInit ? smoothed[b] * 0.55f + db * 0.45f : db;
            outDb[b] = smoothed[b];
        }
        smoothingInit = true;
    }

    void buildTwiddles() noexcept
    {
        for (int i = 0; i < kFftSize / 2; ++i)
        {
            const double angle = -2.0 * juce::MathConstants<double>::pi * i / kFftSize;
            twiddleCos_[i] = (float) std::cos (angle);
            twiddleSin_[i] = (float) std::sin (angle);
        }
    }

    void fft (float* re, float* im) const noexcept
    {
        // Radix-2 decimation-in-time, iterative.
        const int n = kFftSize;
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const int tw = j * step;
                    const float wr = twiddleCos_[tw];
                    const float wi = twiddleSin_[tw];
                    const int a = i + j;
                    const int b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

    void mixAndPush (const juce::AudioBuffer<float>& buffer, int numChannels,
                     int numSamples, C4SpectrumFifo& fifo) noexcept
    {
        int pos = 0;
        while (pos < numSamples)
        {
            const int n = juce::jmin (kMaxTapBlock, numSamples - pos);
            const float* ch0 = buffer.getReadPointer (0, pos);
            if (numChannels >= 2)
            {
                const float* ch1 = buffer.getReadPointer (1, pos);
                for (int i = 0; i < n; ++i)
                    tapScratch_[i] = 0.5f * (ch0[i] + ch1[i]);
            }
            else
            {
                for (int i = 0; i < n; ++i)
                    tapScratch_[i] = ch0[i];
            }
            fifo.push (tapScratch_, n);
            pos += n;
        }
    }

    void resetWorkerState() noexcept
    {
        preCount_ = 0;
        postCount_ = 0;
        preActive_.store (false, std::memory_order_release);
        postActive_.store (false, std::memory_order_release);
        preSmoothingInit_ = false;
        postSmoothingInit_ = false;
        for (float& v : preSmoothed_)
            v = -140.0f;
        for (float& v : postSmoothed_)
            v = -140.0f;
    }

    // ---- members -------------------------------------------------------------
    C4SpectrumFifo preFifo_, postFifo_;
    WorkerThread worker_;

    std::atomic<int> mode_ { (int) C4SpectrumTapMode::Closed };
    std::atomic<int> snapshotIndex_ { 0 };
    std::atomic<bool> preActive_ { false };
    std::atomic<bool> postActive_ { false };
    std::atomic<float> rate_ { 48000.0f };
    std::atomic<float> binHz_ { 48000.0f / kFftSize };

    // Diagnostics (relaxed; worker-owned writes, test reads).
    std::atomic<long> consumedPre_ { 0 };
    std::atomic<long> consumedPost_ { 0 };
    std::atomic<long> preWindowStart_ { 0 };
    std::atomic<long> postWindowStart_ { 0 };

    float preDb_[2][kSpectrumBins] = {};
    float postDb_[2][kSpectrumBins] = {};

    // Worker-owned staging + FFT state (only the worker thread touches these).
    float staging_[kMaxTapBlock] = {};
    float stagingPost_[kMaxTapBlock] = {};
    float preAcc_[kFftSize * 2] = {};
    float postAcc_[kFftSize * 2] = {};
    int preCount_ = 0, postCount_ = 0;
    float fftRe_[kFftSize] = {};
    float fftIm_[kFftSize] = {};
    float preSmoothed_[kSpectrumBins] = {};
    float postSmoothed_[kSpectrumBins] = {};
    bool preSmoothingInit_ = false;
    bool postSmoothingInit_ = false;
    float twiddleCos_[kFftSize / 2] = {};
    float twiddleSin_[kFftSize / 2] = {};

    // Audio-thread scratch (fixed; no allocation).
    float tapScratch_[kMaxTapBlock] = {};
};

} // namespace C4
} // namespace APEX

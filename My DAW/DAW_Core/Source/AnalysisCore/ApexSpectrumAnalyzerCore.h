#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>

namespace APEX::Analysis
{

enum class SpectrumTapMode : int
{
    Closed = 0,
    Pre = 1,
    Post = 2,
    Both = 3
};

// Fixed SPSC transport. The producer drops the newest visualization chunk on
// overflow; it never blocks, overwrites unread data, or allocates.
class SpectrumFifo final
{
public:
    static constexpr int kCapacity = 16384;

    static_assert ((kCapacity & (kCapacity - 1)) == 0,
                   "Spectrum FIFO capacity must be a power of two");

    void reset() noexcept
    {
        read_.store (write_.load (std::memory_order_acquire),
                     std::memory_order_release);
    }

    bool push (const float* left, const float* right,
               int numberOfSamples) noexcept
    {
        if (left == nullptr || numberOfSamples <= 0)
            return true;

        const auto write = write_.load (std::memory_order_relaxed);
        const auto read = read_.load (std::memory_order_acquire);
        if (write - read + static_cast<std::uint32_t> (numberOfSamples)
            > static_cast<std::uint32_t> (kCapacity))
        {
            drops_.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const auto slot = (write + static_cast<std::uint32_t> (sample))
                            & (kCapacity - 1);
            left_[slot] = left[sample];
            right_[slot] = right != nullptr ? right[sample] : left[sample];
        }
        write_.store (write + static_cast<std::uint32_t> (numberOfSamples),
                      std::memory_order_release);
        pushes_.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    bool push (const float* mono, int numberOfSamples) noexcept
    {
        return push (mono, mono, numberOfSamples);
    }

    // Consume through the producer boundary while retaining only the newest
    // bounded window. Analyzer freshness is more valuable than stale history.
    int drainNewest (float* destinationLeft, float* destinationRight,
                     int maximumSamples) noexcept
    {
        if (destinationLeft == nullptr || destinationRight == nullptr
            || maximumSamples <= 0)
            return 0;

        const auto read = read_.load (std::memory_order_relaxed);
        const auto write = write_.load (std::memory_order_acquire);
        const auto available = write - read;
        const auto count = static_cast<int> (std::min (
            available, static_cast<std::uint32_t> (maximumSamples)));
        const auto start = write - static_cast<std::uint32_t> (count);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto slot = (start + static_cast<std::uint32_t> (sample))
                            & (kCapacity - 1);
            destinationLeft[sample] = left_[slot];
            destinationRight[sample] = right_[slot];
        }
        read_.store (write, std::memory_order_release);
        return count;
    }

    int available() const noexcept
    {
        return static_cast<int> (write_.load (std::memory_order_acquire)
                               - read_.load (std::memory_order_acquire));
    }

    int pushCount() const noexcept
    {
        return pushes_.load (std::memory_order_relaxed);
    }

    int dropCount() const noexcept
    {
        return drops_.load (std::memory_order_relaxed);
    }

private:
    alignas (64) std::array<float, kCapacity> left_ {};
    alignas (64) std::array<float, kCapacity> right_ {};
    alignas (64) std::atomic<std::uint32_t> write_ { 0 };
    alignas (64) std::atomic<std::uint32_t> read_ { 0 };
    alignas (64) std::atomic<int> pushes_ { 0 };
    alignas (64) std::atomic<int> drops_ { 0 };
};

// Reusable observational analyzer:
// audio callback -> fixed SPSC tap -> low-priority FFT worker -> pinned
// immutable snapshot -> GUI. The callback performs only bounded copies.
class SpectrumAnalyzerCore final
{
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kSpectrumBins = kFftSize / 2 + 1;
    static constexpr int kMaximumTapChunk = 8192;
    static constexpr int kSnapshotSlots = 3;

    class Snapshot final
    {
    public:
        Snapshot() = default;
        Snapshot (const Snapshot&) = delete;
        Snapshot& operator= (const Snapshot&) = delete;

        Snapshot (Snapshot&& other) noexcept
        {
            moveFrom (other);
        }

        Snapshot& operator= (Snapshot&& other) noexcept
        {
            if (this != &other)
            {
                release();
                moveFrom (other);
            }
            return *this;
        }

        ~Snapshot() { release(); }

        bool isValid() const noexcept { return owner_ != nullptr; }
        int index() const noexcept { return index_; }
        bool preActive() const noexcept { return preActive_; }
        bool postActive() const noexcept { return postActive_; }
        bool discontinuous() const noexcept { return discontinuous_; }
        float binHz() const noexcept { return binHz_; }
        float sampleRate() const noexcept { return sampleRate_; }
        int preDropCount() const noexcept { return preDrops_; }
        int postDropCount() const noexcept { return postDrops_; }
        const float* preDb() const noexcept { return preDb_; }
        const float* postDb() const noexcept { return postDb_; }

    private:
        friend class SpectrumAnalyzerCore;

        Snapshot (const SpectrumAnalyzerCore* owner, int slot,
                  int index, bool preActive, bool postActive,
                  bool discontinuous, float binHz, float sampleRate,
                  int preDrops, int postDrops,
                  const float* preDb, const float* postDb) noexcept
            : owner_ (owner), slot_ (slot), index_ (index),
              preActive_ (preActive), postActive_ (postActive),
              discontinuous_ (discontinuous), binHz_ (binHz),
              sampleRate_ (sampleRate), preDrops_ (preDrops),
              postDrops_ (postDrops), preDb_ (preDb), postDb_ (postDb)
        {
        }

        void release() noexcept
        {
            if (owner_ != nullptr)
                owner_->releaseSnapshot (slot_);
            owner_ = nullptr;
            slot_ = -1;
            preDb_ = nullptr;
            postDb_ = nullptr;
        }

        void moveFrom (Snapshot& other) noexcept
        {
            owner_ = std::exchange (other.owner_, nullptr);
            slot_ = std::exchange (other.slot_, -1);
            index_ = other.index_;
            preActive_ = other.preActive_;
            postActive_ = other.postActive_;
            discontinuous_ = other.discontinuous_;
            binHz_ = other.binHz_;
            sampleRate_ = other.sampleRate_;
            preDrops_ = other.preDrops_;
            postDrops_ = other.postDrops_;
            preDb_ = std::exchange (other.preDb_, nullptr);
            postDb_ = std::exchange (other.postDb_, nullptr);
        }

        const SpectrumAnalyzerCore* owner_ = nullptr;
        int slot_ = -1;
        int index_ = -1;
        bool preActive_ = false;
        bool postActive_ = false;
        bool discontinuous_ = false;
        float binHz_ = 0.0f;
        float sampleRate_ = 0.0f;
        int preDrops_ = 0;
        int postDrops_ = 0;
        const float* preDb_ = nullptr;
        const float* postDb_ = nullptr;
    };

    explicit SpectrumAnalyzerCore (
        const juce::String& workerName = "APEX Spectrum Analyzer")
        : worker_ (workerName, *this)
    {
        buildTwiddles();
    }

    ~SpectrumAnalyzerCore()
    {
        mode_.store (static_cast<int> (SpectrumTapMode::Closed),
                     std::memory_order_release);
        worker_.stopThread (500);
    }

    // Lifecycle/control thread only; audio processing must be quiescent.
    void prepare (double sampleRate) noexcept
    {
        const auto mode = getTapMode();
        worker_.stopThread (500);
        rate_.store (static_cast<float> (std::max (1.0, sampleRate)),
                     std::memory_order_release);
        binHz_.store (rate_.load (std::memory_order_relaxed) / kFftSize,
                      std::memory_order_release);
        preFifo_.reset();
        postFifo_.reset();
        resetWorkerState (true);
        publishedSlot_.store (-1, std::memory_order_release);
        latestIndex_.store (-1, std::memory_order_release);
        if (mode != SpectrumTapMode::Closed)
            worker_.startThread (juce::Thread::Priority::low);
    }

    // Message/control thread only. Closing may wait for the worker; never call
    // this from processBlock.
    void setTapMode (SpectrumTapMode mode) noexcept
    {
        const auto raw = std::clamp (static_cast<int> (mode), 0, 3);
        const auto previous = mode_.load (std::memory_order_acquire);
        if (raw == previous)
            return;

        // Gate callback capture before quiescing the worker. This is a
        // control-thread wait only; processBlock observes one atomic scalar.
        mode_.store (static_cast<int> (SpectrumTapMode::Closed),
                     std::memory_order_release);
        worker_.stopThread (500);
        preFifo_.reset();
        postFifo_.reset();
        resetWorkerState (true);
        publishedSlot_.store (-1, std::memory_order_release);
        latestIndex_.store (-1, std::memory_order_release);

        mode_.store (raw, std::memory_order_release);
        if (raw != static_cast<int> (SpectrumTapMode::Closed))
            worker_.startThread (juce::Thread::Priority::low);
    }

    SpectrumTapMode getTapMode() const noexcept
    {
        return static_cast<SpectrumTapMode> (
            mode_.load (std::memory_order_acquire));
    }

    // Any thread may request a continuity reset. The worker observes this
    // scalar and performs all mutable analysis-state work itself.
    void signalDiscontinuity() noexcept
    {
        requestedDiscontinuities_.fetch_add (1, std::memory_order_release);
    }

    // Audio thread only.
    void pushPre (const juce::AudioBuffer<float>& buffer,
                  int numberOfChannels, int numberOfSamples) noexcept
    {
        if ((mode_.load (std::memory_order_relaxed) & 1) == 0)
            return;
        mixAndPush (buffer, numberOfChannels, numberOfSamples, preFifo_);
    }

    // Audio thread only.
    void pushPost (const juce::AudioBuffer<float>& buffer,
                   int numberOfChannels, int numberOfSamples) noexcept
    {
        if ((mode_.load (std::memory_order_relaxed) & 2) == 0)
            return;
        mixAndPush (buffer, numberOfChannels, numberOfSamples, postFifo_);
    }

    // Any consumer thread. The returned frame pins its fixed slot until the
    // Snapshot is destroyed; the worker drops a visual update rather than
    // overwriting a frame that a GUI is reading.
    Snapshot acquireSnapshot() const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const int slot = publishedSlot_.load (std::memory_order_acquire);
            if (slot < 0 || slot >= kSnapshotSlots)
                return {};

            slots_[static_cast<std::size_t> (slot)].readers.fetch_add (
                1, std::memory_order_acq_rel);
            if (publishedSlot_.load (std::memory_order_acquire) != slot)
            {
                slots_[static_cast<std::size_t> (slot)].readers.fetch_sub (
                    1, std::memory_order_release);
                continue;
            }

            const auto& frame = slots_[static_cast<std::size_t> (slot)];
            return Snapshot (this, slot, frame.index, frame.preActive,
                             frame.postActive, frame.discontinuous,
                             frame.binHz, frame.sampleRate, frame.preDrops,
                             frame.postDrops, frame.preDb.data(),
                             frame.postDb.data());
        }
        return {};
    }

    int waitForSnapshot (int afterIndex, int timeoutMs = 2000) const
    {
        const auto deadline = juce::Time::getMillisecondCounter()
                            + static_cast<juce::uint32> (std::max (0, timeoutMs));
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            auto snapshot = acquireSnapshot();
            if (snapshot.isValid() && snapshot.index() > afterIndex)
                return snapshot.index();
            juce::Thread::sleep (5);
        }
        return -1;
    }

    bool isWorkerRunning() const noexcept { return worker_.isThreadRunning(); }
    int availablePre() const noexcept { return preFifo_.available(); }
    int availablePost() const noexcept { return postFifo_.available(); }
    int prePushCount() const noexcept { return preFifo_.pushCount(); }
    int postPushCount() const noexcept { return postFifo_.pushCount(); }
    int preDropCount() const noexcept { return preFifo_.dropCount(); }
    int postDropCount() const noexcept { return postFifo_.dropCount(); }
    int snapshotDropCount() const noexcept
    {
        return snapshotDrops_.load (std::memory_order_relaxed);
    }

private:
    struct SnapshotSlot
    {
        mutable std::atomic<int> readers { 0 };
        int index = -1;
        bool preActive = false;
        bool postActive = false;
        bool discontinuous = false;
        float binHz = 0.0f;
        float sampleRate = 0.0f;
        int preDrops = 0;
        int postDrops = 0;
        std::array<float, kSpectrumBins> preDb {};
        std::array<float, kSpectrumBins> postDb {};
    };

    class Worker final : public juce::Thread
    {
    public:
        Worker (const juce::String& name, SpectrumAnalyzerCore& owner)
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
        SpectrumAnalyzerCore& owner_;
    };

    void releaseSnapshot (int slot) const noexcept
    {
        if (slot >= 0 && slot < kSnapshotSlots)
            slots_[static_cast<std::size_t> (slot)].readers.fetch_sub (
                1, std::memory_order_release);
    }

    void workerTick() noexcept
    {
        const int mode = mode_.load (std::memory_order_acquire);
        const bool wantPre = (mode & 1) != 0;
        const bool wantPost = (mode & 2) != 0;
        if (! wantPre && ! wantPost)
            return;

        const auto requested = requestedDiscontinuities_.load (
            std::memory_order_acquire);
        const int preDrops = preFifo_.dropCount();
        const int postDrops = postFifo_.dropCount();
        if (requested != observedDiscontinuities_
            || preDrops != observedPreDrops_
            || postDrops != observedPostDrops_)
        {
            // Any loss makes queued history continuity-ambiguous. Discard it
            // and wait for a complete fresh window before publication.
            preFifo_.reset();
            postFifo_.reset();
            preCount_ = 0;
            postCount_ = 0;
            preSmoothingInitialised_ = false;
            postSmoothingInitialised_ = false;
            observedDiscontinuities_ = requested;
            observedPreDrops_ = preDrops;
            observedPostDrops_ = postDrops;
            pendingDiscontinuity_ = true;
            publishedSlot_.store (-1, std::memory_order_release);
            latestIndex_.store (-1, std::memory_order_release);
            return;
        }

        const int gotPre = wantPre
                         ? preFifo_.drainNewest (stagingPreLeft_.data(),
                                                stagingPreRight_.data(),
                                                kMaximumTapChunk)
                         : 0;
        const int gotPost = wantPost
                           ? postFifo_.drainNewest (stagingPostLeft_.data(),
                                                   stagingPostRight_.data(),
                                                   kMaximumTapChunk)
                           : 0;
        appendNewest (preAccumulatorLeft_, preAccumulatorRight_, preCount_,
                      wantPre, stagingPreLeft_, stagingPreRight_, gotPre);
        appendNewest (postAccumulatorLeft_, postAccumulatorRight_, postCount_,
                      wantPost, stagingPostLeft_, stagingPostRight_, gotPost);

        const bool preReady = ! wantPre || preCount_ >= kFftSize;
        const bool postReady = ! wantPost || postCount_ >= kFftSize;
        if (! preReady || ! postReady)
            return;

        if (wantPre)
            computeSpectrum (preAccumulatorLeft_.data(),
                             preAccumulatorRight_.data(), preCount_,
                             candidatePreDb_.data(), preSmoothed_,
                             preSmoothingInitialised_);
        if (wantPost)
            computeSpectrum (postAccumulatorLeft_.data(),
                             postAccumulatorRight_.data(), postCount_,
                             candidatePostDb_.data(), postSmoothed_,
                             postSmoothingInitialised_);

        consumeWindow (preAccumulatorLeft_, preAccumulatorRight_,
                       preCount_, wantPre);
        consumeWindow (postAccumulatorLeft_, postAccumulatorRight_,
                       postCount_, wantPost);

        // A pinned GUI frame may suppress publication, but it must never
        // freeze analysis age or preserve stale queued samples.
        const int current = publishedSlot_.load (std::memory_order_acquire);
        int candidate = -1;
        for (int slot = 0; slot < kSnapshotSlots; ++slot)
        {
            if (slot != current
                && slots_[static_cast<std::size_t> (slot)].readers.load (
                       std::memory_order_acquire) == 0)
            {
                candidate = slot;
                break;
            }
        }
        if (candidate < 0)
        {
            snapshotDrops_.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        auto& frame = slots_[static_cast<std::size_t> (candidate)];
        if (wantPre)
            frame.preDb = candidatePreDb_;
        if (wantPost)
            frame.postDb = candidatePostDb_;

        frame.index = ++nextFrameIndex_;
        frame.preActive = wantPre;
        frame.postActive = wantPost;
        frame.discontinuous = pendingDiscontinuity_;
        frame.binHz = binHz_.load (std::memory_order_acquire);
        frame.sampleRate = rate_.load (std::memory_order_acquire);
        frame.preDrops = preDrops;
        frame.postDrops = postDrops;
        // Publish the fully written pinned slot before making its completion
        // sequence wait-visible.
        publishedSlot_.store (candidate, std::memory_order_release);
        latestIndex_.store (frame.index, std::memory_order_release);
        pendingDiscontinuity_ = false;
    }

    template <std::size_t AccumulatorSize, std::size_t StagingSize>
    static void appendNewest (
        std::array<float, AccumulatorSize>& accumulatorLeft,
        std::array<float, AccumulatorSize>& accumulatorRight,
        int& count, bool wanted,
        const std::array<float, StagingSize>& stagingLeft,
        const std::array<float, StagingSize>& stagingRight,
        int numberOfSamples) noexcept
    {
        if (! wanted)
        {
            count = 0;
            return;
        }
        const int incoming = std::clamp (numberOfSamples, 0,
                                         static_cast<int> (StagingSize));
        const int capacity = static_cast<int> (AccumulatorSize);
        if (incoming >= capacity)
        {
            const int start = incoming - capacity;
            for (int sample = 0; sample < capacity; ++sample)
            {
                accumulatorLeft[static_cast<std::size_t> (sample)]
                    = stagingLeft[static_cast<std::size_t> (start + sample)];
                accumulatorRight[static_cast<std::size_t> (sample)]
                    = stagingRight[static_cast<std::size_t> (start + sample)];
            }
            count = capacity;
            return;
        }

        const int discard = std::max (0, count + incoming - capacity);
        if (discard > 0)
        {
            const int keep = count - discard;
            for (int sample = 0; sample < keep; ++sample)
            {
                accumulatorLeft[static_cast<std::size_t> (sample)]
                    = accumulatorLeft[static_cast<std::size_t> (discard + sample)];
                accumulatorRight[static_cast<std::size_t> (sample)]
                    = accumulatorRight[static_cast<std::size_t> (discard + sample)];
            }
            count = keep;
        }

        for (int sample = 0; sample < incoming; ++sample)
        {
            accumulatorLeft[static_cast<std::size_t> (count + sample)]
                = stagingLeft[static_cast<std::size_t> (sample)];
            accumulatorRight[static_cast<std::size_t> (count + sample)]
                = stagingRight[static_cast<std::size_t> (sample)];
        }
        count += incoming;
    }

    template <std::size_t Size>
    static void consumeWindow (std::array<float, Size>& accumulatorLeft,
                               std::array<float, Size>& accumulatorRight,
                               int& count, bool wanted) noexcept
    {
        if (! wanted)
        {
            count = 0;
            return;
        }
        const int keep = std::max (0, count - kFftSize);
        for (int sample = 0; sample < keep; ++sample)
        {
            accumulatorLeft[static_cast<std::size_t> (sample)]
                = accumulatorLeft[static_cast<std::size_t> (kFftSize + sample)];
            accumulatorRight[static_cast<std::size_t> (sample)]
                = accumulatorRight[static_cast<std::size_t> (kFftSize + sample)];
        }
        count = keep;
    }

    template <std::size_t SmoothedSize>
    void computeSpectrum (const float* inputLeft, const float* inputRight,
                          int count, float* outputDb,
                          std::array<float, SmoothedSize>& smoothed,
                          bool& smoothingInitialised) noexcept
    {
        const int start = count - kFftSize;
        computePower (inputLeft + start, fftPowerLeft_);
        computePower (inputRight + start, fftPowerRight_);

        const auto normalisationDb = static_cast<float> (
            20.0 * std::log10 (static_cast<double> (kFftSize) * 0.25));
        for (int bin = 0; bin < kSpectrumBins; ++bin)
        {
            const auto power = 0.5 * (
                fftPowerLeft_[static_cast<std::size_t> (bin)]
              + fftPowerRight_[static_cast<std::size_t> (bin)]);
            auto db = static_cast<float> (10.0 * std::log10 (
                std::max (1.0e-18, power))) - normalisationDb;
            db = std::clamp (db, -140.0f, 40.0f);
            auto& history = smoothed[static_cast<std::size_t> (bin)];
            history = smoothingInitialised ? history * 0.55f + db * 0.45f : db;
            outputDb[bin] = history;
        }
        smoothingInitialised = true;
    }

    template <std::size_t PowerSize>
    void computePower (const float* input,
                       std::array<double, PowerSize>& power) noexcept
    {
        for (int sample = 0; sample < kFftSize; ++sample)
        {
            fftReal_[static_cast<std::size_t> (sample)]
                = input[sample] * hann_[static_cast<std::size_t> (sample)];
            fftImaginary_[static_cast<std::size_t> (sample)] = 0.0f;
        }
        fft (fftReal_.data(), fftImaginary_.data());

        for (int bin = 0; bin < kSpectrumBins; ++bin)
        {
            const auto real = fftReal_[static_cast<std::size_t> (bin)];
            const auto imaginary = fftImaginary_[static_cast<std::size_t> (bin)];
            power[static_cast<std::size_t> (bin)]
                = static_cast<double> (real) * real
                + static_cast<double> (imaginary) * imaginary;
        }
    }

    void buildTwiddles() noexcept
    {
        for (int index = 0; index < kFftSize / 2; ++index)
        {
            const auto angle = -2.0 * juce::MathConstants<double>::pi
                             * index / kFftSize;
            twiddleCos_[static_cast<std::size_t> (index)]
                = static_cast<float> (std::cos (angle));
            twiddleSin_[static_cast<std::size_t> (index)]
                = static_cast<float> (std::sin (angle));
        }
        for (int sample = 0; sample < kFftSize; ++sample)
            hann_[static_cast<std::size_t> (sample)] = static_cast<float> (
                0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                      * sample / (kFftSize - 1)));
    }

    void fft (float* real, float* imaginary) const noexcept
    {
        for (int index = 1, reversed = 0; index < kFftSize; ++index)
        {
            int bit = kFftSize >> 1;
            for (; reversed & bit; bit >>= 1)
                reversed ^= bit;
            reversed ^= bit;
            if (index < reversed)
            {
                std::swap (real[index], real[reversed]);
                std::swap (imaginary[index], imaginary[reversed]);
            }
        }

        for (int length = 2; length <= kFftSize; length <<= 1)
        {
            const int half = length >> 1;
            const int step = kFftSize / length;
            for (int base = 0; base < kFftSize; base += length)
            {
                for (int index = 0; index < half; ++index)
                {
                    const int twiddle = index * step;
                    const float wr = twiddleCos_[static_cast<std::size_t> (twiddle)];
                    const float wi = twiddleSin_[static_cast<std::size_t> (twiddle)];
                    const int a = base + index;
                    const int b = a + half;
                    const float tr = real[b] * wr - imaginary[b] * wi;
                    const float ti = real[b] * wi + imaginary[b] * wr;
                    real[b] = real[a] - tr;
                    imaginary[b] = imaginary[a] - ti;
                    real[a] += tr;
                    imaginary[a] += ti;
                }
            }
        }
    }

    void mixAndPush (const juce::AudioBuffer<float>& buffer,
                     int numberOfChannels, int numberOfSamples,
                     SpectrumFifo& fifo) noexcept
    {
        if (numberOfChannels <= 0 || numberOfSamples <= 0
            || buffer.getNumChannels() <= 0)
            return;

        const int channels = std::min (numberOfChannels,
                                       buffer.getNumChannels());
        int offset = 0;
        while (offset < numberOfSamples)
        {
            const int count = std::min (kMaximumTapChunk,
                                        numberOfSamples - offset);
            const auto* left = buffer.getReadPointer (0, offset);
            if (channels >= 2)
            {
                const auto* right = buffer.getReadPointer (1, offset);
                fifo.push (left, right, count);
            }
            else
            {
                fifo.push (left, left, count);
            }
            offset += count;
        }
    }

    void resetWorkerState (bool markDiscontinuous = false) noexcept
    {
        preCount_ = 0;
        postCount_ = 0;
        preSmoothingInitialised_ = false;
        postSmoothingInitialised_ = false;
        preSmoothed_.fill (-140.0f);
        postSmoothed_.fill (-140.0f);
        observedDiscontinuities_ = requestedDiscontinuities_.load (
            std::memory_order_acquire);
        observedPreDrops_ = preFifo_.dropCount();
        observedPostDrops_ = postFifo_.dropCount();
        pendingDiscontinuity_ = markDiscontinuous;
    }

    SpectrumFifo preFifo_;
    SpectrumFifo postFifo_;
    Worker worker_;

    std::atomic<int> mode_ { static_cast<int> (SpectrumTapMode::Closed) };
    std::atomic<float> rate_ { 48000.0f };
    std::atomic<float> binHz_ { 48000.0f / kFftSize };
    std::atomic<int> publishedSlot_ { -1 };
    std::atomic<int> latestIndex_ { -1 };
    std::atomic<int> snapshotDrops_ { 0 };
    std::atomic<std::uint64_t> requestedDiscontinuities_ { 0 };
    mutable std::array<SnapshotSlot, kSnapshotSlots> slots_ {};

    // Worker-owned memory.
    std::array<float, kMaximumTapChunk> stagingPreLeft_ {};
    std::array<float, kMaximumTapChunk> stagingPreRight_ {};
    std::array<float, kMaximumTapChunk> stagingPostLeft_ {};
    std::array<float, kMaximumTapChunk> stagingPostRight_ {};
    std::array<float, kFftSize * 2> preAccumulatorLeft_ {};
    std::array<float, kFftSize * 2> preAccumulatorRight_ {};
    std::array<float, kFftSize * 2> postAccumulatorLeft_ {};
    std::array<float, kFftSize * 2> postAccumulatorRight_ {};
    int preCount_ = 0;
    int postCount_ = 0;
    std::array<float, kFftSize> fftReal_ {};
    std::array<float, kFftSize> fftImaginary_ {};
    std::array<double, kSpectrumBins> fftPowerLeft_ {};
    std::array<double, kSpectrumBins> fftPowerRight_ {};
    std::array<float, kSpectrumBins> candidatePreDb_ {};
    std::array<float, kSpectrumBins> candidatePostDb_ {};
    std::array<float, kSpectrumBins> preSmoothed_ {};
    std::array<float, kSpectrumBins> postSmoothed_ {};
    bool preSmoothingInitialised_ = false;
    bool postSmoothingInitialised_ = false;
    std::uint64_t observedDiscontinuities_ = 0;
    int observedPreDrops_ = 0;
    int observedPostDrops_ = 0;
    bool pendingDiscontinuity_ = false;
    int nextFrameIndex_ = 0;
    std::array<float, kFftSize / 2> twiddleCos_ {};
    std::array<float, kFftSize / 2> twiddleSin_ {};
    std::array<float, kFftSize> hann_ {};
};

} // namespace APEX::Analysis

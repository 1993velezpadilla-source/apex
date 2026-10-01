#pragma once

#include "PluginSandboxAudioTransportCore.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

namespace DAW {

class PluginSandboxExactQuantumBackendCore
{
public:
    virtual ~PluginSandboxExactQuantumBackendCore() = default;

    virtual PluginSandboxAudioTransportCore::ExchangeResult exchangeQuantum(
        const float* const* input,
        std::uint32_t inputChannels,
        float* const* output,
        std::uint32_t outputChannels,
        std::uint32_t quantumSamples) noexcept = 0;
};

class PluginSandboxFixedQuantumReblockerCore
{
public:
    struct Configuration
    {
        int quantumSamples = 0;
        int maximumHostBlockSamples = 0;
        int inputChannels = 0;
        int outputChannels = 0;
        int pluginLatencySamples = 0;
        std::uint64_t transportGeneration = 0;
        bool initiallyBypassed = false;
    };

    enum class State
    {
        Unprepared,
        Priming,
        Running,
        Faulted
    };

    struct ProcessSummary
    {
        static constexpr std::uint32_t kMaxAutomationDeliveryReports = 5;

        struct AutomationDeliveryReport
        {
            // Parent-local E2B delivery identity. This is captured from the
            // transport exchange that produced the report; it is not a shared
            // memory field and must never be relabeled from currentGeneration
            // during report consumption.
            std::uint64_t workerGeneration = 0;
            std::uint64_t sequence = 0;
            bool submitted = false;
            bool automationBatchPublished = false;
            bool automationBatchValid = false;
        };

        int samplesProcessed = 0;
        int backendCalls = 0;
        int remoteQuantaCommitted = 0;
        int fallbackQuantaCommitted = 0;
        int bypassQuantaCommitted = 0;
        std::uint64_t lastSubmittedSequence = 0;
        bool contractFault = false;
        std::array<AutomationDeliveryReport,
                   kMaxAutomationDeliveryReports> automationDeliveryReports {};
        std::uint32_t automationDeliveryReportCount = 0;
    };

    struct Diagnostics
    {
        State state = State::Unprepared;
        std::uint64_t generation = 0;
        std::uint64_t inputSamplesAccepted = 0;
        std::uint64_t outputSamplesEmitted = 0;
        std::uint64_t backendCalls = 0;
        std::uint64_t committedQuanta = 0;
        std::uint64_t remoteQuanta = 0;
        std::uint64_t fallbackQuanta = 0;
        std::uint64_t bypassQuanta = 0;
        std::uint64_t contractFaults = 0;
        int inputFill = 0;
        int outputSamplesAvailable = 0;
        bool pendingQuantum = false;
        bool bypassRequested = false;
        int resolutionDepth = 0;
    };

    bool prepare(const Configuration& configuration)
    {
        release();
        if (! isValid(configuration))
            return false;

        configuration_ = configuration;
        depth_ = depthFor(configuration);
        inputAccumulator_.setSize(configuration.inputChannels,
                                  configuration.quantumSamples,
                                  false, true, false);
        backendOutput_.setSize(configuration.outputChannels,
                               configuration.quantumSamples,
                               false, true, false);
        dryDelay_.setSize(configuration.outputChannels,
                          juce::jmax(1, configuration.pluginLatencySamples),
                          false, true, false);
        for (auto& dry : dryQuanta_)
            dry.setSize(configuration.outputChannels,
                        configuration.quantumSamples,
                        false, true, false);

        // (depth_ + 1) quanta cover the deterministic pre-roll plus the
        // transient produced while ingesting one maximum host callback.
        const int requiredRingSamples =
            (depth_ + 1) * configuration.quantumSamples;
        if (! outputRing_.prepare(configuration.outputChannels,
                                  nextPowerOfTwo(requiredRingSamples)))
        {
            release();
            return false;
        }

        requestedBypass_.store(configuration.initiallyBypassed,
                               std::memory_order_relaxed);
        resetStorage(configuration.transportGeneration,
                     configuration.initiallyBypassed);
        return true;
    }

    bool reprepare(const Configuration& configuration)
    {
        return prepare(configuration);
    }

    void resetForGeneration(std::uint64_t generation, bool bypassed) noexcept
    {
        if (state_.load(std::memory_order_acquire) == State::Unprepared
            || generation == 0)
            return;

        configuration_.transportGeneration = generation;
        requestedBypass_.store(bypassed, std::memory_order_relaxed);
        resetStorage(generation, bypassed);
    }

    void release()
    {
        state_.store(State::Unprepared, std::memory_order_release);
        inputAccumulator_.setSize(0, 0);
        backendOutput_.setSize(0, 0);
        dryDelay_.setSize(0, 0);
        for (auto& dry : dryQuanta_)
            dry.setSize(0, 0);
        outputRing_.release();
        configuration_ = {};
        clearStreamState();
    }

    ProcessSummary processBlock(juce::AudioBuffer<float>& buffer,
                                int numSamples,
                                PluginSandboxExactQuantumBackendCore& backend) noexcept
    {
        ProcessSummary summary;
        const auto currentState = state_.load(std::memory_order_acquire);
        if (numSamples == 0)
            return summary;

        const int requiredChannels = juce::jmax(configuration_.inputChannels,
                                                configuration_.outputChannels);
        if (currentState == State::Unprepared || currentState == State::Faulted
            || numSamples < 0
            || numSamples > configuration_.maximumHostBlockSamples
            || numSamples > buffer.getNumSamples()
            || buffer.getNumChannels() < requiredChannels)
            return failWholeCallback(buffer, numSamples);

        int sourceOffset = 0;
        while (sourceOffset < numSamples)
        {
            if (inputFill_ == 0)
                currentQuantumBypassed_ = requestedBypass_.load(std::memory_order_relaxed);

            const int copyCount = juce::jmin(configuration_.quantumSamples - inputFill_,
                                             numSamples - sourceOffset);
            for (int channel = 0; channel < configuration_.inputChannels; ++channel)
                std::memcpy(inputAccumulator_.getWritePointer(channel, inputFill_),
                            buffer.getReadPointer(channel, sourceOffset),
                            sizeof(float) * static_cast<std::size_t>(copyCount));

            accumulateDelayedDry(buffer, sourceOffset, copyCount);
            inputFill_ += copyCount;
            sourceOffset += copyCount;
            inputSamplesAccepted_ += static_cast<std::uint64_t>(copyCount);

            if (inputFill_ != configuration_.quantumSamples)
                continue;

            const auto exchange = backend.exchangeQuantum(
                inputAccumulator_.getArrayOfReadPointers(),
                static_cast<std::uint32_t>(configuration_.inputChannels),
                backendOutput_.getArrayOfWritePointers(),
                static_cast<std::uint32_t>(configuration_.outputChannels),
                static_cast<std::uint32_t>(configuration_.quantumSamples));
            ++backendCalls_;
            ++summary.backendCalls;
            if (exchange.submitted)
                summary.lastSubmittedSequence = exchange.submittedSequence;

            // The largest accepted host callback is 2048 samples and a
            // callback may begin with a partially filled quantum. Therefore
            // at most five exact 512-sample exchanges can occur here. Keep
            // the per-exchange result local and fixed-capacity so the parent
            // can commit E2B delivery state against the actual submission.
            if (exchange.submittedSequence != 0
                && summary.automationDeliveryReportCount
                       < ProcessSummary::kMaxAutomationDeliveryReports)
            {
                auto& report = summary.automationDeliveryReports[
                    summary.automationDeliveryReportCount++];
                report.workerGeneration = exchange.submittedGeneration;
                report.sequence = exchange.submittedSequence;
                report.submitted = exchange.submitted;
                report.automationBatchPublished = exchange.automationBatchPublished;
                report.automationBatchValid = exchange.automationBatchValid;
            }

            // Depth-aware resolution: Phase B exchange k resolves quantum
            // k-depth. The pending window holds exactly the quanta
            // k-depth .. k-1 once full, so the OLDEST entry is quantum
            // k-depth and its deterministic resolution point has arrived.
            if (pendingCount_ == depth_)
            {
                auto& oldest = pending_[pendingHead_];

                const bool remoteValid = exchange.remoteOutput
                    && oldest.backendSequence != 0
                    && exchange.completedSequence == oldest.backendSequence
                    && exchange.completedGeneration == configuration_.transportGeneration
                    && exchange.outputSamples
                           == static_cast<std::uint32_t>(configuration_.quantumSamples)
                    && exchange.outputChannels
                           == static_cast<std::uint32_t>(configuration_.outputChannels);

                const juce::AudioBuffer<float>* selected;
                if (oldest.bypassed)
                {
                    selected = &dryQuanta_[oldest.dryIndex];
                    ++bypassQuanta_;
                    ++summary.bypassQuantaCommitted;
                }
                else if (remoteValid)
                {
                    selected = &backendOutput_;
                    ++remoteQuanta_;
                    ++summary.remoteQuantaCommitted;
                }
                else
                {
                    // Deadline miss at the deterministic resolution point:
                    // commit the SAME historical source quantum's retained,
                    // plugin-latency-aligned dry audio. Never current callback
                    // audio, never a partial quantum.
                    selected = &dryQuanta_[oldest.dryIndex];
                    ++fallbackQuanta_;
                    ++summary.fallbackQuantaCommitted;
                }

                if (! outputRing_.push(*selected, configuration_.quantumSamples))
                    return failWholeCallback(buffer, numSamples);
                ++committedQuanta_;
                state_.store(State::Running, std::memory_order_release);

                pendingHead_ = (pendingHead_ + 1) % kMaxPendingEntries;
                --pendingCount_;
            }

            // Enqueue the new quantum k together with its just-completed,
            // plugin-latency-aligned dry quantum.
            auto& entry = pending_[(pendingHead_ + pendingCount_) % kMaxPendingEntries];
            entry.active = true;
            entry.bypassed = currentQuantumBypassed_;
            entry.backendSequence = exchange.submittedSequence;
            entry.dryIndex = currentDryIndex_;
            ++pendingCount_;
            currentDryIndex_ = (currentDryIndex_ + 1) % kDryQuantumCount;
            inputFill_ = 0;
        }

        if (outputRing_.available() < numSamples
            || ! outputRing_.pop(buffer, numSamples))
            return failWholeCallback(buffer, numSamples);

        for (int channel = configuration_.outputChannels;
             channel < buffer.getNumChannels(); ++channel)
            buffer.clear(channel, 0, numSamples);

        outputSamplesEmitted_ += static_cast<std::uint64_t>(numSamples);
        summary.samplesProcessed = numSamples;
        return summary;
    }

    void requestBypass(bool bypassed) noexcept
    {
        requestedBypass_.store(bypassed, std::memory_order_relaxed);
    }

    int transportLatencySamples() const noexcept
    {
        // (depth + 1) * Q: prepared-stable, independent of the actual
        // callback partition sizes observed at runtime.
        return (depth_ + 1) * configuration_.quantumSamples;
    }

    int effectiveLatencySamples() const noexcept
    {
        return configuration_.pluginLatencySamples + transportLatencySamples();
    }

    int resolutionDepth() const noexcept { return depth_; }

    int quantumSamples() const noexcept { return configuration_.quantumSamples; }
    int maximumHostBlockSamples() const noexcept
    {
        return configuration_.maximumHostBlockSamples;
    }
    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    Diagnostics diagnostics() const noexcept
    {
        return { state(),
                 configuration_.transportGeneration,
                 inputSamplesAccepted_,
                 outputSamplesEmitted_,
                 backendCalls_,
                 committedQuanta_,
                 remoteQuanta_,
                 fallbackQuanta_,
                 bypassQuanta_,
                 contractFaults_,
                 inputFill_,
                 outputRing_.available(),
                 pendingCount_ > 0,
                 requestedBypass_.load(std::memory_order_relaxed),
                 depth_ };
    }

private:
    class PlanarRing
    {
    public:
        bool prepare(int channels, int capacity)
        {
            if (channels <= 0 || capacity <= 0)
                return false;
            storage_.setSize(channels, capacity, false, true, false);
            channels_ = channels;
            capacity_ = capacity;
            reset();
            return true;
        }

        void release()
        {
            storage_.setSize(0, 0);
            channels_ = 0;
            capacity_ = 0;
            reset();
        }

        void reset() noexcept
        {
            read_ = 0;
            write_ = 0;
            available_ = 0;
            if (storage_.getNumSamples() > 0)
                storage_.clear();
        }

        bool pushSilence(int samples) noexcept
        {
            if (samples < 0 || samples > capacity_ - available_)
                return false;
            int remaining = samples;
            while (remaining > 0)
            {
                const int contiguous = juce::jmin(remaining, capacity_ - write_);
                for (int channel = 0; channel < channels_; ++channel)
                    storage_.clear(channel, write_, contiguous);
                advanceWrite(contiguous);
                remaining -= contiguous;
            }
            return true;
        }

        bool push(const juce::AudioBuffer<float>& source, int samples) noexcept
        {
            if (samples < 0 || samples > capacity_ - available_
                || source.getNumChannels() < channels_
                || source.getNumSamples() < samples)
                return false;

            int sourceOffset = 0;
            int remaining = samples;
            while (remaining > 0)
            {
                const int contiguous = juce::jmin(remaining, capacity_ - write_);
                for (int channel = 0; channel < channels_; ++channel)
                    std::memcpy(storage_.getWritePointer(channel, write_),
                                source.getReadPointer(channel, sourceOffset),
                                sizeof(float) * static_cast<std::size_t>(contiguous));
                advanceWrite(contiguous);
                sourceOffset += contiguous;
                remaining -= contiguous;
            }
            return true;
        }

        bool pop(juce::AudioBuffer<float>& destination, int samples) noexcept
        {
            if (samples < 0 || samples > available_
                || destination.getNumChannels() < channels_
                || destination.getNumSamples() < samples)
                return false;

            int destinationOffset = 0;
            int remaining = samples;
            while (remaining > 0)
            {
                const int contiguous = juce::jmin(remaining, capacity_ - read_);
                for (int channel = 0; channel < channels_; ++channel)
                    std::memcpy(destination.getWritePointer(channel, destinationOffset),
                                storage_.getReadPointer(channel, read_),
                                sizeof(float) * static_cast<std::size_t>(contiguous));
                read_ = (read_ + contiguous) % capacity_;
                available_ -= contiguous;
                destinationOffset += contiguous;
                remaining -= contiguous;
            }
            return true;
        }

        int available() const noexcept { return available_; }

    private:
        void advanceWrite(int samples) noexcept
        {
            write_ = (write_ + samples) % capacity_;
            available_ += samples;
        }

        juce::AudioBuffer<float> storage_;
        int channels_ = 0;
        int capacity_ = 0;
        int read_ = 0;
        int write_ = 0;
        int available_ = 0;
    };

    static int depthFor(const Configuration& configuration) noexcept
    {
        return (configuration.maximumHostBlockSamples
                + configuration.quantumSamples - 1)
            / configuration.quantumSamples;
    }

    static bool isValid(const Configuration& configuration) noexcept
    {
        return configuration.quantumSamples > 0
            && configuration.maximumHostBlockSamples > 0
            && configuration.inputChannels > 0
            && configuration.outputChannels > 0
            && configuration.pluginLatencySamples >= 0
            && configuration.transportGeneration != 0
            && configuration.quantumSamples
                   <= (std::numeric_limits<int>::max)() / 4
            && configuration.maximumHostBlockSamples
                   <= (std::numeric_limits<int>::max)() - configuration.quantumSamples
            && configuration.pluginLatencySamples
                   <= (std::numeric_limits<int>::max)() - 2 * configuration.quantumSamples
            && depthFor(configuration)
                   <= static_cast<int>(PluginSandboxAudioShared::kMaxOutstandingDepth);
    }

    static int nextPowerOfTwo(int value) noexcept
    {
        std::uint32_t result = 1;
        const auto target = static_cast<std::uint32_t>(value);
        while (result < target && result <= (1u << 30))
            result <<= 1;
        return result >= target ? static_cast<int>(result) : 0;
    }

    void accumulateDelayedDry(const juce::AudioBuffer<float>& input,
                              int sourceOffset,
                              int samples) noexcept
    {
        auto& currentDry = dryQuanta_[currentDryIndex_];
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < configuration_.outputChannels; ++channel)
            {
                const float dryInput = channel < configuration_.inputChannels
                    ? input.getSample(channel, sourceOffset + sample) : 0.0f;
                float delayed = dryInput;
                if (configuration_.pluginLatencySamples > 0)
                {
                    delayed = dryDelay_.getSample(channel, dryDelayPosition_);
                    dryDelay_.setSample(channel, dryDelayPosition_, dryInput);
                }
                currentDry.setSample(channel, inputFill_ + sample, delayed);
            }
            if (configuration_.pluginLatencySamples > 0)
                dryDelayPosition_ = (dryDelayPosition_ + 1)
                                  % configuration_.pluginLatencySamples;
        }
    }

    void resetStorage(std::uint64_t generation, bool bypassed) noexcept
    {
        inputAccumulator_.clear();
        backendOutput_.clear();
        dryDelay_.clear();
        for (auto& dry : dryQuanta_)
            dry.clear();
        for (auto& pending : pending_)
            pending.active = false;
        outputRing_.reset();
        clearStreamState();
        configuration_.transportGeneration = generation;
        currentQuantumBypassed_ = bypassed;
        outputRing_.pushSilence((depth_ + 1) * configuration_.quantumSamples);
        state_.store(State::Priming, std::memory_order_release);
    }

    void clearStreamState() noexcept
    {
        inputFill_ = 0;
        dryDelayPosition_ = 0;
        currentDryIndex_ = 0;
        pendingHead_ = 0;
        pendingCount_ = 0;
        for (auto& pending : pending_)
        {
            pending.active = false;
            pending.bypassed = false;
            pending.backendSequence = 0;
            pending.dryIndex = 0;
        }
        currentQuantumBypassed_ = false;
        inputSamplesAccepted_ = 0;
        outputSamplesEmitted_ = 0;
        backendCalls_ = 0;
        committedQuanta_ = 0;
        remoteQuanta_ = 0;
        fallbackQuanta_ = 0;
        bypassQuanta_ = 0;
        contractFaults_ = 0;
    }

    ProcessSummary failWholeCallback(juce::AudioBuffer<float>& buffer,
                                     int numSamples) noexcept
    {
        const int safeSamples = juce::jlimit(0, buffer.getNumSamples(), numSamples);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.clear(channel, 0, safeSamples);
        state_.store(State::Faulted, std::memory_order_release);
        ++contractFaults_;
        ProcessSummary result;
        result.contractFault = true;
        return result;
    }

    Configuration configuration_;
    juce::AudioBuffer<float> inputAccumulator_;
    juce::AudioBuffer<float> backendOutput_;
    juce::AudioBuffer<float> dryDelay_;

    static constexpr int kMaxPendingEntries =
        static_cast<int>(PluginSandboxAudioShared::kMaxOutstandingDepth);
    static constexpr int kDryQuantumCount = kMaxPendingEntries + 1;

    struct PendingEntry
    {
        bool active = false;
        bool bypassed = false;
        std::uint64_t backendSequence = 0;
        int dryIndex = 0;
    };

    std::array<juce::AudioBuffer<float>, kDryQuantumCount> dryQuanta_;
    std::array<PendingEntry, kMaxPendingEntries> pending_;
    PlanarRing outputRing_;
    std::atomic<State> state_ { State::Unprepared };
    std::atomic<bool> requestedBypass_ { false };
    int inputFill_ = 0;
    int dryDelayPosition_ = 0;
    int currentDryIndex_ = 0;
    int pendingHead_ = 0;
    int pendingCount_ = 0;
    int depth_ = 1;
    bool currentQuantumBypassed_ = false;
    std::uint64_t inputSamplesAccepted_ = 0;
    std::uint64_t outputSamplesEmitted_ = 0;
    std::uint64_t backendCalls_ = 0;
    std::uint64_t committedQuanta_ = 0;
    std::uint64_t remoteQuanta_ = 0;
    std::uint64_t fallbackQuanta_ = 0;
    std::uint64_t bypassQuanta_ = 0;
    std::uint64_t contractFaults_ = 0;
};

} // namespace DAW

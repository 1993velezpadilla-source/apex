#pragma once
#include <JuceHeader.h>
#include <memory>
#include <atomic>

struct ScriptedWriterState
{
    juce::WaitableEvent allowWrite;
    std::atomic<int64_t> attemptedFrames { 0 };
    std::atomic<int64_t> confirmedFrames { 0 };
    std::atomic<int64_t> firstFailedFrame { -1 };
    std::atomic<int> writeCalls { 0 };
    std::atomic<int> flushCalls { 0 };
    std::atomic<bool> destroyed { false };
    int failWriteCall = -1;
    int failFlushCall = -1;
    bool blockWrites = false;
};

class ScriptedAudioFormatWriter final : public juce::AudioFormatWriter
{
public:
    explicit ScriptedAudioFormatWriter (std::shared_ptr<ScriptedWriterState> stateToUse)
        : AudioFormatWriter (nullptr, "ScriptedWriter", 44100.0, 2, 16),
          state (std::move (stateToUse))
    {}

    ~ScriptedAudioFormatWriter() override
    {
        if (state != nullptr)
            state->destroyed.store (true, std::memory_order_release);
    }

    bool write (const int** /*channels*/, int numSamples) override
    {
        if (state == nullptr)
            return false;

        const auto callIndex = state->writeCalls.fetch_add (1, std::memory_order_relaxed);

        if (state->blockWrites)
            state->allowWrite.wait();

        state->attemptedFrames.fetch_add (numSamples, std::memory_order_relaxed);

        if (state->failWriteCall >= 0 && callIndex >= state->failWriteCall)
        {
            const auto prevFailed = state->firstFailedFrame.load (std::memory_order_relaxed);
            if (prevFailed < 0)
                state->firstFailedFrame.store (state->attemptedFrames.load (std::memory_order_relaxed)
                                                    - numSamples,
                                                std::memory_order_relaxed);
            return false;
        }

        state->confirmedFrames.fetch_add (numSamples, std::memory_order_relaxed);
        return true;
    }

    bool flush() override
    {
        if (state == nullptr)
            return false;

        const auto callIndex = state->flushCalls.fetch_add (1, std::memory_order_relaxed);

        if (state->failFlushCall >= 0 && callIndex >= state->failFlushCall)
            return false;

        return true;
    }

private:
    std::shared_ptr<ScriptedWriterState> state;
};

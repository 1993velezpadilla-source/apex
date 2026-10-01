#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cstddef>
#include <memory>
#include <atomic>

struct StreamCounters
{
    std::atomic<int> flushCalls { 0 };
    std::atomic<int> seekCalls { 0 };
    std::atomic<juce::int64> firstFailedByte { -1 };
};

class ScriptedSeekableOutputStream final : public juce::OutputStream
{
public:
    explicit ScriptedSeekableOutputStream (size_t capacityBytes, juce::int64 failAtByte = -1,
                                           std::shared_ptr<StreamCounters> countersToUse = nullptr)
        : storage (capacityBytes, std::byte { 0 }),
          failAt (failAtByte),
          shared (countersToUse ? std::move (countersToUse) : std::make_shared<StreamCounters>())
    {}

    void flush() override
    {
        shared->flushCalls.fetch_add (1, std::memory_order_relaxed);
    }

    bool setPosition (juce::int64 newPosition) override
    {
        shared->seekCalls.fetch_add (1, std::memory_order_relaxed);
        if (newPosition >= 0 && newPosition <= static_cast<juce::int64> (storage.size()))
        {
            position = newPosition;
            return true;
        }
        return false;
    }

    juce::int64 getPosition() override
    {
        return position;
    }

    bool write (const void* data, size_t bytes) override
    {
        if (failAt >= 0 && position + static_cast<juce::int64> (bytes) > failAt)
        {
            const auto prev = shared->firstFailedByte.load (std::memory_order_relaxed);
            if (prev < 0)
                shared->firstFailedByte.store (juce::jmax (position, failAt),
                                               std::memory_order_relaxed);
            return false;
        }

        auto remaining = static_cast<size_t> (storage.size() - static_cast<size_t> (position));
        auto toWrite = juce::jmin (bytes, remaining);
        if (toWrite > 0)
        {
            std::memcpy (storage.data() + static_cast<size_t> (position), data, toWrite);
            position += static_cast<juce::int64> (toWrite);
        }
        return toWrite == bytes;
    }

    int getFlushCalls() const noexcept   { return shared->flushCalls.load (std::memory_order_relaxed); }
    int getSeekCalls() const noexcept    { return shared->seekCalls.load (std::memory_order_relaxed); }
    juce::int64 getFirstFailedByte() const noexcept { return shared->firstFailedByte.load (std::memory_order_relaxed); }

    std::shared_ptr<StreamCounters> getCounters() const noexcept { return shared; }

private:
    std::vector<std::byte> storage;
    juce::int64 position = 0;
    juce::int64 failAt = -1;
    std::shared_ptr<StreamCounters> shared;
};

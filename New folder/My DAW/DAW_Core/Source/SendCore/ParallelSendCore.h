#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * ParallelSendCore — manages parallel processing sends.
 *
 * A parallel send duplicates the source signal to a parallel
 * processing bus (e.g., parallel compression) while the original
 * signal continues to its main output.
 *
 * Both paths sum at the master for blending dry + processed.
 */
class ParallelSendCore
{
public:
    void setSourceId(const juce::String& id) { sourceId_ = id; }
    const juce::String& getSourceId() const { return sourceId_; }

    void setParallelBusId(const juce::String& id) { parallelBusId_ = id; }
    const juce::String& getParallelBusId() const { return parallelBusId_; }

    void setSendLevel(float level) noexcept { sendLevel_ = juce::jlimit(0.0f, 2.0f, level); }
    float getSendLevel() const noexcept { return sendLevel_; }

    void setMuted(bool muted) noexcept { muted_ = muted; }
    bool isMuted() const noexcept { return muted_; }

    float getEffectiveGain() const noexcept
    {
        return muted_ ? 0.0f : sendLevel_;
    }

private:
    juce::String sourceId_;
    juce::String parallelBusId_;
    float sendLevel_ = 1.0f;
    bool muted_ = false;
};

} // namespace DAW

#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * SendCore — represents a single audio send from a source to a destination.
 *
 * Each send has:
 *   - Source node ID
 *   - Destination node ID (typically a bus)
 *   - Send level (gain)
 *   - Pre/Post fader mode
 *   - Mute state
 *   - Pan (future)
 *
 * Sends are PARALLEL paths — they do not replace the main output.
 * A track's main output + all sends run simultaneously.
 */
class SendCore
{
public:
    enum class SendMode { PreFader, PostFader };

    SendCore() = default;
    SendCore(const juce::String& sourceId, const juce::String& destId,
             SendMode mode = SendMode::PostFader, float level = 1.0f)
        : sourceId_(sourceId), destId_(destId), mode_(mode), level_(level) {}

    const juce::String& getSourceId() const noexcept { return sourceId_; }
    const juce::String& getDestId() const noexcept { return destId_; }

    SendMode getMode() const noexcept { return mode_; }
    void setMode(SendMode m) noexcept { mode_ = m; }

    float getLevel() const noexcept { return level_; }
    void setLevel(float v) noexcept { level_ = juce::jlimit(0.0f, 2.0f, v); }

    bool isMuted() const noexcept { return muted_; }
    void setMuted(bool m) noexcept { muted_ = m; }

    /** Returns the effective send gain (0 if muted). */
    float getEffectiveGain() const noexcept
    {
        return muted_ ? 0.0f : level_;
    }

    juce::ValueTree getState() const
    {
        juce::ValueTree v("Send");
        v.setProperty("sourceId", sourceId_, nullptr);
        v.setProperty("destId", destId_, nullptr);
        v.setProperty("mode", (int)mode_, nullptr);
        v.setProperty("level", level_, nullptr);
        v.setProperty("muted", muted_, nullptr);
        return v;
    }

    void restoreState(const juce::ValueTree& v)
    {
        sourceId_ = v.getProperty("sourceId", "").toString();
        destId_ = v.getProperty("destId", "").toString();
        mode_ = (SendMode)(int)v.getProperty("mode", 0);
        level_ = (float)v.getProperty("level", 1.0f);
        muted_ = (bool)v.getProperty("muted", false);
    }

private:
    juce::String sourceId_;
    juce::String destId_;
    SendMode mode_ = SendMode::PostFader;
    float level_ = 1.0f;
    bool muted_ = false;
};

} // namespace DAW

#pragma once
#include <JuceHeader.h>
#include "StepSequencerTypes.h"
#include <memory>
#include <mutex>

namespace DAW {

class StepSequencerModel : public juce::ChangeBroadcaster {
public:
    StepSequencerModel();
    ~StepSequencerModel() override;

    int  addPattern(const juce::String& name = "");
    void removePattern(int index);
    void setCurrentPattern(int index);
    int  getCurrentPatternIndex() const noexcept;
    int  getPatternCount() const noexcept;
    PatternData& getPattern(int index);
    const PatternData& getPattern(int index) const;
    void clonePattern(int sourceIndex);
    StablePatternId clonePatternWithId(int sourceIndex);

    int  addChannel(const juce::String& name = "Channel");
    void removeChannel(int index);
    void moveChannel(int fromIndex, int toIndex);
    juce::Array<LaneData>& getChannels();
    LaneData& getChannel(int index);

    void toggleStep(int channelIndex, int stepIndex);
    void setStepVelocity(int channelIndex, int stepIndex, float velocity);
    void setStepProbability(int channelIndex, int stepIndex, float probability);
    void setStepPitchOffset(int channelIndex, int stepIndex, int8_t offset);
    void addStepAtPitch(int channelIndex, int stepIndex, int8_t pitchOffset);

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

    int  addLane(const juce::String& name = "Lane");
    void removeLane(int index);
    void moveLane(int fromIndex, int toIndex);
    LaneData& getLane(int index);
    const LaneData& getLane(int index) const;
    int  getLaneCount() const noexcept;

    struct Snapshot {
        struct LaneSnapshot {
            StableLaneId id;
            juce::String name;
            float volume;
            float pan;
            bool muted;
            bool soloed;
            int midiNote;
            int midiChannel;
            float swingAmount;
            int laneLength;
            juce::Array<StepEvent> steps;
        };
        StablePatternId patternId;
        uint32_t patternId_hash = 0;
        juce::String name;
        juce::Array<LaneSnapshot> lanes;
        int stepsPerBeat;
        int beatsPerBar;
        int barsPerPattern;
        float globalSwing;
        int totalSteps;
    };

    // ── RT-safe snapshot access (message thread publishes, audio thread reads) ──

    /** Build a fresh immutable snapshot and publish it.
     *  MUST be called from the message thread after any model mutation. */
    void publishSnapshot();

    /** Acquire the current immutable snapshot without allocation or locking.
     *  Safe to call from the audio thread. Returns nullptr if never published.
     *  The returned shared_ptr keeps the snapshot alive even if publishSnapshot()
     *  is called concurrently. Publication uses release/acquire shared_ptr
     *  semantics (same pattern as RoutingSnapshotPublisher). */
    std::shared_ptr<const Snapshot> getSnapshotRT() const noexcept
    {
        return std::atomic_load_explicit(&publishedSnapshot_, std::memory_order_acquire);
    }

    // Legacy lock-based access for message-thread callers only.
    Snapshot getSnapshot() const;

private:
    juce::Array<PatternData> patterns_;
    int currentPatternIndex_ = 0;
    mutable juce::CriticalSection lock_;

    // Immutable snapshot: message thread publishes with a release store,
    // audio thread acquires. Lock-free on both sides.
    std::shared_ptr<const Snapshot> publishedSnapshot_;
    std::shared_ptr<const Snapshot> retiredSnapshot_;   // message-thread retire slot

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerModel)
};

} // namespace DAW

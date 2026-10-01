#pragma once
#include <JuceHeader.h>
#include "StepSequencerModel.h"

namespace DAW {

class StepSequencerPlaybackCore {
public:
    StepSequencerPlaybackCore();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(juce::MidiBuffer& midiBuffer, int numSamples,
                      int64_t currentSamplePosition, double tempo,
                      const StepSequencerModel::Snapshot& snapshot);

    static uint32_t deterministicSeed(uint32_t patternId_hash, uint32_t laneIndex,
                                      int stepIndex, int64_t loopIteration);

private:
    double sampleRate_ = 44100.0;
    int samplesPerBlock_ = 512;
    int64_t lastStepSample_ = 0;
    int currentStep_ = 0;
    juce::Array<bool> previousStepTieState_;

    int64_t tickToSample(int64_t tick, double tempo) const;
    float applySwing(int stepIndex, float swingAmount, int stepsPerBeat) const;

    bool evaluateCondition(StepEvent::Condition condition, int stepIndex, int laneIndex,
                           const StepSequencerModel::Snapshot& snapshot, int64_t loopIteration,
                           int laneStepCount) const;
};

} // namespace DAW

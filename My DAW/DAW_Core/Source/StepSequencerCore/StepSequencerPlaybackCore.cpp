#include "StepSequencerPlaybackCore.h"

namespace DAW {

StepSequencerPlaybackCore::StepSequencerPlaybackCore() = default;

void StepSequencerPlaybackCore::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate;
    samplesPerBlock_ = samplesPerBlock;
}

int64_t StepSequencerPlaybackCore::tickToSample(int64_t tick, double tempo) const {
    const double beatsPerSecond = tempo / 60.0;
    const double samplesPerBeat = sampleRate_ / beatsPerSecond;
    return (int64_t)((double)tick / 960.0 * samplesPerBeat);
}

float StepSequencerPlaybackCore::applySwing(
    int stepIndex, float swingAmount, int stepsPerBeat) const
{
    if (stepsPerBeat <= 0) return 0.0f;
    const bool isOffbeat = ((stepIndex / stepsPerBeat) % 2 == 1)
                           && ((stepIndex % stepsPerBeat) != 0);
    if (!isOffbeat || swingAmount <= 0.0f) return 0.0f;
    const int64_t ticksPerStep = 960 / stepsPerBeat;
    return (float)ticksPerStep * swingAmount * 0.5f;
}

uint32_t StepSequencerPlaybackCore::deterministicSeed(
    uint32_t patternId_hash, uint32_t laneIndex, int stepIndex, int64_t loopIteration)
{
    uint32_t seed = patternId_hash;
    seed ^= laneIndex * 0x9E3779B9u;
    seed ^= (uint32_t)stepIndex * 0x85EBCA6Bu;
    seed ^= (uint32_t)loopIteration * 0xC2B2AE35u;
    seed ^= seed >> 16;
    seed *= 0x45D9F3Bu;
    seed ^= seed >> 16;
    seed *= 0x45D9F3Bu;
    seed ^= seed >> 16;
    return seed;
}

bool StepSequencerPlaybackCore::evaluateCondition(
    StepEvent::Condition condition, int stepIndex, int laneIndex,
    const StepSequencerModel::Snapshot& snapshot, int64_t loopIteration,
    int laneStepCount) const
{
    const int stepsPerBeat = snapshot.stepsPerBeat;

    switch (condition)
    {
        case StepEvent::Condition::Every:
            return true;

        case StepEvent::Condition::NotFirst:
        {
            for (int s = 0; s < stepIndex; ++s)
            {
                if (laneIndex < snapshot.lanes.size() && s < snapshot.lanes[laneIndex].steps.size()
                    && snapshot.lanes[laneIndex].steps[s].active)
                    return true;
            }
            return false;
        }

        case StepEvent::Condition::NotSecond:
        {
            int activeCount = 0;
            for (int s = 0; s < stepIndex; ++s)
            {
                if (laneIndex < snapshot.lanes.size() && s < snapshot.lanes[laneIndex].steps.size()
                    && snapshot.lanes[laneIndex].steps[s].active)
                    activeCount++;
            }
            return activeCount != 1;
        }

        case StepEvent::Condition::NotThird:
        {
            int activeCount = 0;
            for (int s = 0; s < stepIndex; ++s)
            {
                if (laneIndex < snapshot.lanes.size() && s < snapshot.lanes[laneIndex].steps.size()
                    && snapshot.lanes[laneIndex].steps[s].active)
                    activeCount++;
            }
            return activeCount != 2;
        }

        case StepEvent::Condition::FirstOf:
        {
            if (stepsPerBeat <= 0) return false;
            return (stepIndex % stepsPerBeat) == 0;
        }

        case StepEvent::Condition::Fill:
        {
            if (stepsPerBeat <= 0) return false;
            return stepIndex >= (laneStepCount - stepsPerBeat);
        }

        case StepEvent::Condition::NotFill:
        {
            if (stepsPerBeat <= 0) return true;
            return stepIndex < (laneStepCount - stepsPerBeat);
        }

        case StepEvent::Condition::PrevActive:
        {
            if (stepIndex <= 0) return false;
            const int prevIdx = stepIndex - 1;
            if (laneIndex < snapshot.lanes.size() && prevIdx < snapshot.lanes[laneIndex].steps.size())
                return snapshot.lanes[laneIndex].steps[prevIdx].active;
            return false;
        }

        case StepEvent::Condition::PrevInactive:
        {
            if (stepIndex <= 0) return true;
            const int prevIdx = stepIndex - 1;
            if (laneIndex < snapshot.lanes.size() && prevIdx < snapshot.lanes[laneIndex].steps.size())
                return !snapshot.lanes[laneIndex].steps[prevIdx].active;
            return true;
        }

        case StepEvent::Condition::First:
            return stepIndex == 0;

        case StepEvent::Condition::Last:
            return stepIndex == (laneStepCount - 1);

        case StepEvent::Condition::Random4:
        {
            const uint32_t seed = deterministicSeed(snapshot.patternId_hash, laneIndex, stepIndex, loopIteration);
            return (seed & 0x3u) == 0;
        }

        case StepEvent::Condition::Random8:
        {
            const uint32_t seed = deterministicSeed(snapshot.patternId_hash, laneIndex, stepIndex, loopIteration);
            return (seed & 0x7u) == 0;
        }

        case StepEvent::Condition::Random16:
        {
            const uint32_t seed = deterministicSeed(snapshot.patternId_hash, laneIndex, stepIndex, loopIteration);
            return (seed & 0xFu) == 0;
        }

        default:
            return true;
    }
}

void StepSequencerPlaybackCore::processBlock(
    juce::MidiBuffer& midiBuffer, int numSamples,
    int64_t currentSamplePosition, double tempo,
    const StepSequencerModel::Snapshot& snapshot)
{
    if (snapshot.totalSteps <= 0 || snapshot.lanes.isEmpty()) return;

    if (previousStepTieState_.size() != snapshot.lanes.size())
    {
        previousStepTieState_.clearQuick();
        for (int i = 0; i < snapshot.lanes.size(); ++i)
            previousStepTieState_.add(false);
    }

    const int64_t blockEndSample = currentSamplePosition + numSamples;
    const int totalSteps = snapshot.totalSteps;
    const int stepsPerBeat = snapshot.stepsPerBeat;
    const float globalSwing = snapshot.globalSwing;

    const double beatsPerSecond = tempo / 60.0;
    const double samplesPerBeat = sampleRate_ / beatsPerSecond;

    for (int ch = 0; ch < snapshot.lanes.size(); ++ch)
    {
        const auto& lane = snapshot.lanes[ch];

        const int laneStepCount = (lane.laneLength > 0) ? lane.laneLength : totalSteps;
        const float effectiveSwing = (lane.swingAmount != 0.0f) ? lane.swingAmount : globalSwing;

        const double samplesPerStep = samplesPerBeat / (double)stepsPerBeat;

        const int64_t lanePatternLengthSamples = tickToSample(
            (int64_t)laneStepCount * (960 / stepsPerBeat), tempo);

        for (int step = 0; step < laneStepCount; ++step)
        {
            const int64_t stepTick = (int64_t)step * (960 / stepsPerBeat);
            const float swingOffset = applySwing(step, effectiveSwing, stepsPerBeat);
            const int64_t adjustedTick = stepTick + (int64_t)swingOffset;
            const int64_t stepSample = tickToSample(adjustedTick, tempo);

            for (int64_t patternRepeat = 0; ; ++patternRepeat)
            {
                const int64_t absoluteStepSample = stepSample + patternRepeat * lanePatternLengthSamples;
                if (absoluteStepSample >= blockEndSample) break;
                if (absoluteStepSample < currentSamplePosition) continue;

                const int sampleOffset = (int)(absoluteStepSample - currentSamplePosition);
                if (sampleOffset < 0 || sampleOffset >= numSamples) continue;

                if (step >= lane.steps.size()) continue;

                const auto& event = lane.steps[step];

                if (!event.active && previousStepTieState_[ch])
                    continue;

                if (!event.active) continue;
                if (lane.muted) continue;

                bool anySoloed = false;
                for (const auto& l : snapshot.lanes)
                    if (l.soloed) { anySoloed = true; break; }
                if (anySoloed && !lane.soloed) continue;

                const int64_t loopIteration = (lanePatternLengthSamples > 0)
                    ? absoluteStepSample / lanePatternLengthSamples : 0;

                if (!evaluateCondition(event.condition, step, ch, snapshot, loopIteration, laneStepCount))
                    continue;

                if (event.probability < 1.0f)
                {
                    const uint32_t seed = deterministicSeed(snapshot.patternId_hash, ch, step, loopIteration);
                    const float roll = (float)(seed & 0xFFFF) / 65535.0f;
                    if (roll > event.probability) continue;
                }

                int midiNote = lane.midiNote + event.pitchOffset;
                midiNote = juce::jlimit(0, 127, midiNote);

                float vel = event.velocity * lane.volume;
                vel = juce::jlimit(0.0f, 1.0f, vel);
                const uint8_t velocity = (uint8_t)(vel * 127.0f);
                const int midiChannel = lane.midiChannel + 1;

                if (event.flamEnabled)
                {
                    const int64_t flamOffsetSamples = tickToSample(event.flamOffsetTicks, tempo);
                    const int flamSampleOffset = juce::jmax (0, sampleOffset + (int)flamOffsetSamples);
                    const uint8_t flamVelocity = (uint8_t)(velocity * 0.65f);

                    if (flamSampleOffset >= 0 && flamSampleOffset < numSamples)
                    {
                        midiBuffer.addEvent(
                            juce::MidiMessage::noteOn(midiChannel, midiNote, flamVelocity),
                            flamSampleOffset);

                        const int flamOff = flamSampleOffset + (int)(sampleRate_ * 0.02);
                        if (flamOff >= 0 && flamOff < numSamples)
                        {
                            midiBuffer.addEvent(
                                juce::MidiMessage::noteOff(midiChannel, midiNote, (uint8_t)0),
                                flamOff);
                        }
                    }
                }

                const int ratchetCount = juce::jlimit(1, 16, event.ratchetCount);

                if (ratchetCount > 1)
                {
                    const double subStepDuration = samplesPerStep / (double)ratchetCount;

                    for (int r = 0; r < ratchetCount; ++r)
                    {
                        double ratchetPosition;
                        const double t = (double)r / (double)(ratchetCount - 1);

                        switch (event.ratchetSpacing)
                        {
                            case 1:
                                ratchetPosition = t * t;
                                break;
                            case 2:
                                ratchetPosition = 1.0 - (1.0 - t) * (1.0 - t);
                                break;
                            default:
                                ratchetPosition = t;
                                break;
                        }

                        const int ratchetSampleOffset = sampleOffset
                            + (int)(ratchetPosition * samplesPerStep);
                        const int ratchetNoteOff = (r == ratchetCount - 1)
                            ? sampleOffset + (int)samplesPerStep
                            : ratchetSampleOffset + (int)(subStepDuration * 0.9);

                        if (ratchetSampleOffset >= 0 && ratchetSampleOffset < numSamples)
                        {
                            midiBuffer.addEvent(
                                juce::MidiMessage::noteOn(midiChannel, midiNote, velocity),
                                ratchetSampleOffset);
                        }

                        if (ratchetNoteOff >= 0 && ratchetNoteOff < numSamples)
                        {
                            midiBuffer.addEvent(
                                juce::MidiMessage::noteOff(midiChannel, midiNote, (uint8_t)0),
                                ratchetNoteOff);
                        }
                    }
                }
                else
                {
                    midiBuffer.addEvent(
                        juce::MidiMessage::noteOn(midiChannel, midiNote, velocity),
                        sampleOffset);

                    if (!event.tieToNext)
                    {
                        const int noteOffOffset = sampleOffset + (int)(samplesPerStep * 0.9);
                        if (noteOffOffset >= 0 && noteOffOffset < numSamples)
                        {
                            midiBuffer.addEvent(
                                juce::MidiMessage::noteOff(midiChannel, midiNote, (uint8_t)0),
                                noteOffOffset);
                        }
                    }
                }

                previousStepTieState_.set(ch, event.tieToNext);
            }
        }
    }
}

} // namespace DAW

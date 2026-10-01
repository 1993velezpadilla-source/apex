#include "DrumSamplerVoicePool.h"

namespace DAW {

DrumSamplerVoicePool::DrumSamplerVoicePool(int maxVoices) {
    for (int i = 0; i < maxVoices; ++i)
        voices_.add(new DrumSamplerVoice());
}

DrumSamplerVoice* DrumSamplerVoicePool::allocate() {
    // Single-threaded by design: allocate() and processAll() only ever run on
    // the audio thread (DrumSamplerEngine::processBlock), and voices_ is
    // mutated only in the constructor (message thread, before audio starts).
    // The previous CriticalSection was therefore an unneeded lock in the
    // realtime path and has been removed, not replaced.
    for (auto* v : voices_)
        if (!v->isActive()) return v;
    return voices_.getFirst();
}

void DrumSamplerVoicePool::release(DrumSamplerVoice* voice) {
    if (voice) voice->stop();
}

void DrumSamplerVoicePool::processAll(
    juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    for (auto* v : voices_)
        if (v->isActive())
            v->process(outputBuffer, startSample, numSamples);
}

void DrumSamplerVoicePool::setSampleRate (double engineRate) noexcept {
    for (auto* v : voices_)
        v->setSampleRate (engineRate);
}

} // namespace DAW

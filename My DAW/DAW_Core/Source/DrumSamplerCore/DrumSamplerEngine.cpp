#include "DrumSamplerEngine.h"

namespace DAW {

DrumSamplerEngine::DrumSamplerEngine(int numPads)
    : voicePool_(numPads * 4)
{
    formatManager_.registerBasicFormats();
    pads_.resize(numPads);
    for (int i = 0; i < numPads; ++i) {
        pads_[i].name = juce::String("Pad ") + juce::String(i + 1);
        pads_[i].midiNote = 36 + i;
    }
}

int DrumSamplerEngine::findPadByMidiNote(int note) const {
    for (int i = 0; i < (int)pads_.size(); ++i)
        if (pads_[i].midiNote == note) return i;
    return -1;
}

void DrumSamplerEngine::processBlock(
    juce::MidiBuffer& midiBuffer, juce::AudioBuffer<float>& audioBuffer,
    int startSample, int numSamples)
{
    for (const auto metadata : midiBuffer) {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn()) {
            const int padIdx = findPadByMidiNote(msg.getNoteNumber());
            if (padIdx >= 0) {
                auto* voice = voicePool_.allocate();
                if (voice)
                    voice->start(pads_[padIdx], msg.getFloatVelocity(), 0);
            }
        }
    }

    voicePool_.processAll(audioBuffer, startSample, numSamples);
}

DrumPadConfig& DrumSamplerEngine::getPad(int index) { return pads_[index]; }
const DrumPadConfig& DrumSamplerEngine::getPad(int index) const { return pads_[index]; }

void DrumSamplerEngine::loadSample(int padIndex, const juce::String& filePath) {
    if (padIndex < 0 || padIndex >= (int)pads_.size()) return;
    auto* reader = formatManager_.createReaderFor(filePath);
    if (!reader) return;

    auto& pad = pads_[padIndex];
    VelocityLayer layer;
    layer.filePath = filePath;
    layer.sampleRate = reader->sampleRate;
    layer.numSamples = reader->lengthInSamples;

    juce::AudioBuffer<float> fileBuffer(1, (int)layer.numSamples);
    reader->read(&fileBuffer, 0, (int)layer.numSamples, 0, true, true);
    delete reader;

    layer.audioData = std::move(fileBuffer);
    layer.velocityRange[0] = 0.0f;
    layer.velocityRange[1] = 1.0f;

    pad.layers.clear();
    pad.layers.push_back(std::move(layer));
}

juce::ValueTree DrumSamplerEngine::toValueTree() const {
    juce::ValueTree tree("DrumSampler");
    for (const auto& pad : pads_) {
        juce::ValueTree padTree("Pad");
        padTree.setProperty("name", pad.name, nullptr);
        padTree.setProperty("midiNote", pad.midiNote, nullptr);
        padTree.setProperty("gainDb", pad.gainDb, nullptr);
        padTree.setProperty("pan", pad.pan, nullptr);
        padTree.setProperty("tune", pad.tune, nullptr);
        padTree.setProperty("samplePath",
            pad.layers.empty() ? juce::String("") : pad.layers[0].filePath, nullptr);
        tree.addChild(padTree, -1, nullptr);
    }
    return tree;
}

void DrumSamplerEngine::fromValueTree(const juce::ValueTree& tree) {
    for (int i = 0; i < juce::jmin(tree.getNumChildren(), (int)pads_.size()); ++i) {
        const auto& padTree = tree.getChild(i);
        pads_[i].name = padTree.getProperty("name", "Pad").toString();
        pads_[i].midiNote = (int)(int64_t)padTree.getProperty("midiNote", 36);
        pads_[i].gainDb = (float)(double)padTree.getProperty("gainDb", 0.0);
        pads_[i].pan = (float)(double)padTree.getProperty("pan", 0.0);
        pads_[i].tune = (float)(double)padTree.getProperty("tune", 0.0);
        juce::String path = padTree.getProperty("samplePath", "").toString();
        if (path.isNotEmpty()) loadSample(i, path);
    }
}

} // namespace DAW

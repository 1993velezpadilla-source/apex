// APEX Voice Cleaner offline file renderer.
// Deliberately independent of playback: all disk I/O and FFT processing run
// on a worker before a message-thread transaction publishes new audio caches.
#pragma once
#include "VoiceCleanerDspCore.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include <JuceHeader.h>
#include <limits>
#include <memory>

namespace ArrangementEditor
{
class VoiceCleanerOfflineCore
{
public:
    struct RenderResult
    {
        bool success = false;
        juce::String error;
        DAW::AudioFileManager::PreparedAudio prepared;
    };

    static RenderResult render(const juce::File& source, const juce::File& destination,
                               float strength = 0.68f)
    {
        RenderResult result;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(source));
        if (reader == nullptr || reader->numChannels == 0 ||
            reader->lengthInSamples <= 0 ||
            reader->lengthInSamples > std::numeric_limits<int>::max())
        {
            result.error = "Could not decode source audio.";
            return result;
        }

        const int length = (int)reader->lengthInSamples;
        const int channels = (int)reader->numChannels;
        const double sampleRate = reader->sampleRate;
        juce::AudioBuffer<float> original(channels, length);
        if (!reader->read(&original, 0, length, 0, true, true))
        {
            result.error = "Source audio could not be read.";
            return result;
        }

        juce::AudioBuffer<float> cleaned(channels, length);
        for (int ch = 0; ch < channels; ++ch)
            if (!VoiceCleanerDspCore::process(original.getReadPointer(ch),
                                              cleaned.getWritePointer(ch),
                                              length, sampleRate, strength))
            {
                result.error = "Spectral cleaner failed.";
                return result;
            }

        const auto directory = destination.getParentDirectory();
        if (!directory.isDirectory() && !directory.createDirectory())
        {
            result.error = "Cannot create processed-audio directory.";
            return result;
        }

        // Only publish completely closed WAV files; a failing write never
        // changes the original or leaves a half-written referenced clip.
        const auto temp = destination.withFileExtension(".part");
        auto stream = std::unique_ptr<juce::OutputStream>(temp.createOutputStream());
        if (stream == nullptr)
        {
            result.error = "Cannot create processed WAV.";
            return result;
        }
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriter::Options{}
            .withSampleRate(sampleRate)
            .withNumChannels(channels)
            .withBitsPerSample(24);
        auto writer = wav.createWriterFor(stream, options);
        if (writer == nullptr)
        {
            temp.deleteFile();
            result.error = "Cannot initialize processed WAV encoder.";
            return result;
        }
        const bool wrote = writer->writeFromAudioSampleBuffer(cleaned, 0, length);
        writer.reset(); // closes the WAV header before rename
        if (!wrote || !temp.moveFileTo(destination))
        {
            temp.deleteFile();
            result.error = "Processed WAV could not be written atomically.";
            return result;
        }

        // Supply the fully decoded output to AudioFileManager on the UI thread.
        // Subsequent Undo/Redo swap retained immutable cache handles without I/O.
        auto prepared = std::make_shared<DAW::AudioFileManager::CachedAudio>();
        prepared->sampleRate = sampleRate;
        prepared->buffer = std::move(cleaned);
        result.prepared.decodedSource = std::move(prepared);
        result.prepared.result.success = true;
        result.prepared.result.sampleRate = sampleRate;
        result.prepared.result.numSamples = length;
        result.prepared.result.numChannels = channels;
        result.success = true;
        return result;
    }
};
} // namespace ArrangementEditor

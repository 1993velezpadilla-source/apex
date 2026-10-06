/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#if TRACKTION_UNIT_TESTS && ENGINE_UNIT_TESTS_AUDIO_FILE

#include <tracktion_engine/../3rd_party/doctest/tracktion_doctest.hpp>

namespace tracktion::inline engine
{

TEST_SUITE ("tracktion_engine")
{
    TEST_CASE("WavAudioFormat 7 hrs")
    {
        using namespace std::literals;
        auto numChannels = 2;
        auto duration = TimeDuration (7h);
        auto sampleRate = 44100.0;
        auto numSamples = toSamples (duration, sampleRate);

        auto headerSizeBytes = static_cast<size_t> (5'888);
        auto totalSizeBytes = headerSizeBytes + static_cast<size_t> (numChannels * numSamples) * sizeof (int16_t);
        auto mb = juce::MemoryBlock (totalSizeBytes);
        auto os = std::unique_ptr<juce::OutputStream> (std::make_unique<juce::MemoryOutputStream> (mb, false));
        auto w = juce::WavAudioFormat().createWriterFor (os,
                                                         juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                         .withNumChannels (numChannels));
        auto blockSize = 2048 * 4;
        juce::AudioBuffer<float> temp (numChannels, blockSize);
        temp.clear();

        for (int64_t i = 0; i < numSamples;)
        {
            auto numLeft = numSamples - i;
            auto numThisTime = std::min (static_cast<int64_t> (blockSize), numLeft);

            temp.setSize (numChannels, static_cast<int> (numThisTime), true, false, true);
            w->writeFromAudioSampleBuffer (temp, 0, temp.getNumSamples());

            i += numThisTime;
        }

        w.reset();
        auto is = std::make_unique<juce::MemoryInputStream> (mb, false);
        auto reader = std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (is.release(), true));
        CHECK_EQ(reader->lengthInSamples, numSamples);
        CHECK_EQ(reader->numChannels, numChannels);
        CHECK_EQ(reader->sampleRate, sampleRate);
        CHECK_EQ(reader->usesFloatingPointData, false);
        CHECK_EQ(reader->bitsPerSample, 16);
    }
}


//==============================================================================
//==============================================================================
class AudioFileTests    : public juce::UnitTest
{
public:
    AudioFileTests()
        : juce::UnitTest ("AudioFile", "tracktion_engine")
    {
    }

    void runTest() override
    {
        runFileInfoTest();
    }

private:
    void runFileInfoTest()
    {
        // Create a file
        // Get the info
        // Write to it
        // Get info again
        // Check length and sample rate

        beginTest ("AudioFileInfo update after writing");

        auto& engine = *Engine::getEngines().getFirst();

        juce::WavAudioFormat format;
        juce::TemporaryFile tempFile (format.getFileExtensions()[0]);

        AudioFile audioFile (engine, tempFile.getFile());
        const int numChannels = 2;
        const double sampleRate = 44100.0;
        const int bitDepth = 16;

        // Check file is invalid
        {
            auto info = audioFile.getInfo();
            expectEquals (info.sampleRate, 0.0);
            expectEquals<SampleCount> (info.lengthInSamples, 0);
            expectEquals (info.getLengthInSeconds(), 0.0);
        }

        // Write 1s silence
        {
            const auto numSamplesToWrite = static_cast<int> (sampleRate);

            AudioFileWriter writer (audioFile, &format, numChannels, sampleRate, bitDepth, {}, 0);
            expect (writer.isOpen());

            if (writer.isOpen())
            {
                juce::AudioBuffer<float> buffer (numChannels, numSamplesToWrite);
                buffer.clear();
                writer.appendBuffer (buffer, buffer.getNumSamples());
            }
        }

        // Check file is now valid
        {
            auto info = audioFile.getInfo();
            expectEquals (info.sampleRate, sampleRate);
            expectEquals (info.lengthInSamples, static_cast<SampleCount> (sampleRate));
            expectEquals (info.getLengthInSeconds(), 1.0);
        }
    }
};

static AudioFileTests audioFileTests;

} // namespace tracktion::inline engine

#endif

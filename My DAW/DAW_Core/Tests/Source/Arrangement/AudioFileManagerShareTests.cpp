#include <JuceHeader.h>
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include "../../../Source/UtilityCore/Types.h"
#include <cmath>
#include <vector>

using namespace DAW;

class AudioFileManagerShareTests final : public juce::UnitTest
{
public:
    AudioFileManagerShareTests() : juce::UnitTest ("audio-cache.shared-source-chain.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("shared cache chain A->B->C exposes equal nonzero frame counts and identical samples");
        {
            AudioFileManager mgr;

            const int numChannels = 2;
            const int numSamples = 4096;
            const double sr = 44100.0;

            juce::AudioBuffer<float> original (numChannels, numSamples);
            for (int ch = 0; ch < numChannels; ++ch)
                for (int s = 0; s < numSamples; ++s)
                    original.setSample (ch, s, (float) std::sin (2.0 * 3.14159265 * 440.0 * s / sr));

            ClipID clipA = "clip-a";
            {
                auto cached = std::make_shared<AudioFileManager::CachedAudio>();
                cached->sampleRate = sr;
                cached->buffer.setSize (numChannels, numSamples);
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int s = 0; s < numSamples; ++s)
                        cached->buffer.setSample (ch, s, original.getSample (ch, s));
                cached->rawMin.resize (100, 0.1f);
                cached->rawMax.resize (100, 0.9f);

                mgr.shareSnapshotForClip (cached, clipA);
            }

            ClipID clipB = "clip-b";
            auto snapA = mgr.getCachedAudioSnapshot (clipA);
            expect (snapA != nullptr, "clip A should have cached audio");
            mgr.shareSnapshotForClip (snapA, clipB);

            ClipID clipC = "clip-c";
            auto snapB = mgr.getCachedAudioSnapshot (clipB);
            expect (snapB != nullptr, "clip B should have cached audio");
            mgr.shareSnapshotForClip (snapB, clipC);

            auto snapC = mgr.getCachedAudioSnapshot (clipC);
            expect (snapC != nullptr, "clip C should have cached audio");

            const int countA = snapA->getBufferRef().getNumSamples();
            const int countB = snapB->getBufferRef().getNumSamples();
            const int countC = snapC->getBufferRef().getNumSamples();

            expectEquals (countA, numSamples);
            expectEquals (countB, numSamples);
            expectEquals (countC, numSamples);

            bool identical = true;
            for (int ch = 0; ch < numChannels && identical; ++ch)
                for (int s = 0; s < numSamples && identical; ++s)
                    if (std::abs (snapC->getBufferRef().getSample (ch, s) - original.getSample (ch, s)) > 1e-6f)
                        identical = false;

            expect (identical, "clip C buffer must match original source data");
        }

        beginTest ("change generation bumps on share, unload, and clear");
        {
            // The audio thread validates its per-clip resolution cache against
            // this generation (resolveClipAudio); every mutation must bump it.
            AudioFileManager mgr;
            expectEquals (mgr.getChangeGeneration(), (std::uint64_t) 0);

            auto cached = std::make_shared<AudioFileManager::CachedAudio>();
            cached->sampleRate = 44100.0;
            cached->buffer.setSize (1, 64);

            mgr.shareSnapshotForClip (cached, "clip-g1");
            const auto g1 = mgr.getChangeGeneration();
            expect (g1 > (std::uint64_t) 0);

            mgr.shareSnapshotForClip (cached, "clip-g2");
            const auto g2 = mgr.getChangeGeneration();
            expect (g2 > g1);

            mgr.unloadClip ("clip-g1");
            const auto g3 = mgr.getChangeGeneration();
            expect (g3 > g2);

            mgr.clearAll();
            expect (mgr.getChangeGeneration() > g3);
            expect (mgr.getCachedAudioSnapshot ("clip-g2") == nullptr);
        }

        beginTest ("prepared decode builds peaks before bounded clip-cache publication");
        {
            const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("apex_prepared_audio_" + juce::Uuid().toString());
            expect (tempDir.createDirectory());
            const auto sourceFile = tempDir.getChildFile ("prepared.wav");

            std::unique_ptr<juce::OutputStream> stream (sourceFile.createOutputStream());
            juce::WavAudioFormat wav;
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                wav.createWriterFor (stream,
                    juce::AudioFormatWriterOptions()
                        .withSampleRate (48000.0)
                        .withNumChannels (1)
                        .withBitsPerSample (16)));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> fixture (1, 2048);
                for (int sample = 0; sample < fixture.getNumSamples(); ++sample)
                    fixture.setSample (0, sample, std::sin ((float) sample * 0.05f));
                expect (writer->writeFromAudioSampleBuffer (fixture, 0, fixture.getNumSamples()));
                writer.reset();
            }

            AudioFileManager mgr;
            const auto beforePrepare = mgr.getChangeGeneration();
            auto prepared = mgr.prepareAudioFile (sourceFile, true);
            expect (prepared.result.success);
            expectEquals (prepared.result.numSamples, (SamplePosition) 2048);
            expectEquals (prepared.result.numChannels, 1);
            expect (prepared.decodedSource != nullptr);
            if (prepared.decodedSource != nullptr)
                expect (! prepared.decodedSource->getRawMinRef().empty(),
                        "peak extraction must finish off the message thread before publication");
            expectEquals (mgr.getChangeGeneration(), beforePrepare,
                          "preparation must not publish a realtime-visible clip cache generation");
            expect (mgr.getCachedAudioSnapshot ("prepared-clip") == nullptr);

            const auto result = mgr.publishPreparedForClip ("prepared-clip", prepared);
            expect (result.success);
            expect (mgr.getChangeGeneration() > beforePrepare);
            auto published = mgr.getCachedAudioSnapshot ("prepared-clip");
            expect (published != nullptr);
            if (published != nullptr)
            {
                expectEquals (published->getBufferRef().getNumSamples(), 2048);
                expect (! published->getRawMinRef().empty());
            }

            tempDir.deleteRecursively();
        }
    }
};

static AudioFileManagerShareTests audioFileManagerShareTests;

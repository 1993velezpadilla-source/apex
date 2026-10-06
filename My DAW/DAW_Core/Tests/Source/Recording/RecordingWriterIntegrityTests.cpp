#include <JuceHeader.h>
#include "Fakes/ScriptedAudioFormatWriter.h"
#include "Fakes/ScriptedSeekableOutputStream.h"
#include "../../../Source/AudioEngineCore/AudioEngine.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/RecordingCore/RecordingDiskWriterCore.h"
#include "../../../Source/RecordingCore/RecordingEngine.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/StateCore/ApplicationState.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/TransportCore/TransportController.h"
#include <cmath>

class RecordingWriterIntegrityTests final : public juce::UnitTest
{
public:
    RecordingWriterIntegrityTests()
        : juce::UnitTest ("recording.writer-integrity.v1", "APEX.Recording") {}

    void runTest() override
    {
        testFifoNotDurable();
        testQueueFullWholeBlock();
        testRuntimeWriteFailure();
        testFlushFailure();
        testStopDrainsBeforeDestroy();
        testValidPrefix();
        testCounterWidth();
        testNormalRoundtrip();
        testPostFaderFloatWavRetainsHeadroom();
        testPostFaderTakeWritesProcessedAudioAndPreservesClock();
        testTrackRecordModePersistence();
        testSeekableStreamFailure();
        testWavHeaderPrefix();
    }

private:
    static constexpr double kSampleRate = 44100.0;
    static constexpr int kNumChannels = 2;

    static juce::AudioBuffer<float> makeSineBlock (int numSamples, int numChannels, double phase)
    {
        juce::AudioBuffer<float> buf (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buf.setSample (ch, i, static_cast<float> (std::sin (phase + i * 0.1 + ch * 1.5)));
        return buf;
    }

    struct ChannelPtrs
    {
        std::vector<const float*> ptrs;
    };

    static ChannelPtrs getChannelPtrs (const juce::AudioBuffer<float>& buf)
    {
        ChannelPtrs cp;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            cp.ptrs.push_back (buf.getReadPointer (ch));
        return cp;
    }

    void testFifoNotDurable()
    {
        beginTest ("recording.writer.fifo-not-durable.v1");

        auto state = std::make_shared<ScriptedWriterState>();
        state->failWriteCall = 0;

        auto* writer = new ScriptedAudioFormatWriter (state);
        juce::TimeSliceThread diskThread ("TestDiskThread");
        diskThread.startThread();

        auto threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer, diskThread, 4096);

        auto block = makeSineBlock (256, kNumChannels, 0.0);
        auto cp = getChannelPtrs (block);
        const bool fifoAccepted = threadedWriter->write (cp.ptrs.data(), 256);

        expect (fifoAccepted, "ThreadedWriter::write returned true");

        juce::Thread::sleep (200);
        threadedWriter.reset();

        expect (state->attemptedFrames.load() >= 256, "underlying writer attempted the block");
        expect (state->confirmedFrames.load() == 0, "confirmedFrames remained zero");
        expect (state->destroyed.load(), "writer destruction occurred");

        diskThread.stopThread (2000);
    }

    void testQueueFullWholeBlock()
    {
        beginTest ("recording.writer.queue-full-whole-block.v1");

        auto state = std::make_shared<ScriptedWriterState>();
        state->blockWrites = true;

        auto* writer = new ScriptedAudioFormatWriter (state);
        juce::TimeSliceThread diskThread ("TestDiskThread2");
        diskThread.startThread();

        constexpr int fifoCapacity = 16;
        auto threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer, diskThread, fifoCapacity);

        auto filler = makeSineBlock (7, kNumChannels, 0.0);
        auto cp1 = getChannelPtrs (filler);
        expect (threadedWriter->write (cp1.ptrs.data(), 7), "first block accepted");

        auto filler2 = makeSineBlock (7, kNumChannels, 1.0);
        auto cp1b = getChannelPtrs (filler2);
        expect (threadedWriter->write (cp1b.ptrs.data(), 7), "second block accepted");

        auto overflow = makeSineBlock (8, kNumChannels, 2.0);
        auto cp2 = getChannelPtrs (overflow);
        const bool overflowAccepted = threadedWriter->write (cp2.ptrs.data(), 8);

        expect (! overflowAccepted, "overflow block rejected");

        state->blockWrites = false;
        for (int i = 0; i < 20; ++i)
            state->allowWrite.signal();
        threadedWriter.reset();

        expect (state->attemptedFrames.load() >= 14, "accepted blocks were attempted");
        expect (state->destroyed.load(), "writer destroyed");

        diskThread.stopThread (2000);
    }

    void testRuntimeWriteFailure()
    {
        beginTest ("recording.writer.runtime-write-failure.v1");

        auto state = std::make_shared<ScriptedWriterState>();
        state->failWriteCall = 0;

        auto* writer = new ScriptedAudioFormatWriter (state);
        juce::TimeSliceThread diskThread ("TestDiskThread3");
        diskThread.startThread();

        auto threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer, diskThread, 4096);

        auto block1 = makeSineBlock (128, kNumChannels, 0.0);
        auto cp1 = getChannelPtrs (block1);
        expect (threadedWriter->write (cp1.ptrs.data(), 128));

        auto block2 = makeSineBlock (128, kNumChannels, 1.0);
        auto cp2 = getChannelPtrs (block2);
        threadedWriter->write (cp2.ptrs.data(), 128);

        threadedWriter.reset();

        expect (state->confirmedFrames.load() == 0,
                "no frames confirmed when underlying writer fails");

        diskThread.stopThread (2000);
    }

    void testFlushFailure()
    {
        beginTest ("recording.writer.flush-failure.v1");

        auto state = std::make_shared<ScriptedWriterState>();
        state->failFlushCall = 0;

        auto* writer = new ScriptedAudioFormatWriter (state);

        expect (! writer->flush(), "first flush returns false");

        delete writer;
        expect (state->destroyed.load(), "writer destroyed after flush failure");
    }

    void testStopDrainsBeforeDestroy()
    {
        beginTest ("recording.writer.stop-drains-before-destroy.v1");

        auto state = std::make_shared<ScriptedWriterState>();

        auto* writer = new ScriptedAudioFormatWriter (state);
        juce::TimeSliceThread diskThread ("TestDiskThread4");
        diskThread.startThread();

        auto threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer, diskThread, 4096);

        auto block = makeSineBlock (256, kNumChannels, 0.0);
        auto cp = getChannelPtrs (block);
        threadedWriter->write (cp.ptrs.data(), 256);

        threadedWriter.reset();

        expect (state->attemptedFrames.load() >= 256,
                "all accepted blocks attempted before writer destruction");
        expect (state->confirmedFrames.load() == 256,
                "all frames confirmed before destruction");
        expect (state->destroyed.load(), "writer destruction occurred");

        diskThread.stopThread (2000);
    }

    void testValidPrefix()
    {
        beginTest ("recording.writer-prefix-after-failure.v1");

        auto state = std::make_shared<ScriptedWriterState>();
        state->failWriteCall = 2;

        auto* writer = new ScriptedAudioFormatWriter (state);

        constexpr int blockSize = 128;
        constexpr int numBlocks = 4;
        for (int i = 0; i < numBlocks; ++i)
        {
            auto block = makeSineBlock (blockSize, kNumChannels, i * 0.5);
            auto cp = getChannelPtrs (block);
            writer->write (reinterpret_cast<const int**> (cp.ptrs.data()), blockSize);
        }

        delete writer;

        const auto confirmed = state->confirmedFrames.load();
        const auto attempted = state->attemptedFrames.load();

        expect (attempted == numBlocks * blockSize,
                "all blocks were attempted despite middle failure");
        expect (confirmed == 2 * blockSize,
                "confirmed equals contiguous prefix before failure (2 blocks * 128)");
    }

    void testCounterWidth()
    {
        beginTest ("recording.writer.counter-width.v1");

        auto state = std::make_shared<ScriptedWriterState>();

        constexpr int64_t largeCount = int64_t (1) << 33;
        state->confirmedFrames.store (largeCount);
        state->attemptedFrames.store (largeCount);

        expect (state->confirmedFrames.load() == largeCount, "64-bit counter survives large value");
        expect (state->attemptedFrames.load() == largeCount, "attempted counter survives large value");

        state->confirmedFrames.fetch_add (1000000);
        expect (state->confirmedFrames.load() == largeCount + 1000000,
                "64-bit counter addition works");
    }

    void testNormalRoundtrip()
    {
        beginTest ("recording.writer.normal-roundtrip.v1");

        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("apex_writer_test");
        tempDir.createDirectory();

        auto wavFile = tempDir.getChildFile ("roundtrip_test.wav");
        wavFile.deleteFile();

        {
            std::unique_ptr<juce::OutputStream> outStream (wavFile.createOutputStream());
            expect (outStream != nullptr, "output stream created");

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> wavWriter (
                wav.createWriterFor (outStream,
                    juce::AudioFormatWriterOptions()
                        .withSampleRate (kSampleRate)
                        .withNumChannels (kNumChannels)
                        .withBitsPerSample (16)));
            expect (wavWriter != nullptr, "wav writer created");

            constexpr int blockSize = 512;
            for (int block = 0; block < 10; ++block)
            {
                auto buf = makeSineBlock (blockSize, kNumChannels, block * 0.25 + 0.1);
                wavWriter->writeFromAudioSampleBuffer (buf, 0, blockSize);
            }
        }

        {
            juce::AudioFormatManager mgr;
            mgr.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader (
                mgr.createReaderFor (wavFile));
            expect (reader != nullptr, "reader created");

            expect (reader->numChannels == (unsigned int) kNumChannels,
                    "channel count matches");
            expect (std::abs (reader->sampleRate - kSampleRate) < 1.0,
                    "sample rate matches");

            constexpr int totalFrames = 512 * 10;
            expectEquals (static_cast<int> (reader->lengthInSamples), totalFrames,
                          "frame count matches");

            juce::AudioBuffer<float> readBuf (kNumChannels, totalFrames);
            reader->read (&readBuf, 0, totalFrames, 0, true, true);

            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                const float firstSample = readBuf.getSample (ch, 0);
                expect (std::abs (firstSample) > 0.001f, "non-zero first sample");
            }
        }

        wavFile.deleteFile();
        tempDir.deleteRecursively();
    }

    void testSeekableStreamFailure()
    {
        beginTest ("recording.writer.seekable-stream-failure.v1");

        constexpr size_t capacity = 8192;
        constexpr juce::int64 failAtByte = 768;

        ScriptedSeekableOutputStream stream (capacity, failAtByte);

        std::vector<char> data (512, 'A');
        const bool writeOk1 = stream.write (data.data(), data.size());
        expect (writeOk1, "writes up to fail point succeed");

        std::vector<char> data2 (512, 'B');
        const bool writeOk2 = stream.write (data2.data(), data2.size());
        expect (! writeOk2, "write past fail point returns false");
        expect (stream.getFirstFailedByte() >= failAtByte,
                "first failed byte at or after declared boundary");

        auto counters = std::make_shared<StreamCounters>();
        auto* heapStream = new ScriptedSeekableOutputStream (capacity, failAtByte, counters);

        juce::WavAudioFormat wav;
        juce::AudioFormat* fmt = &wav;
        juce::AudioFormatWriter* rawWriter =
            fmt->createWriterFor (
                heapStream, kSampleRate, (unsigned int) kNumChannels, 16,
                {}, 0);

        if (rawWriter != nullptr)
        {
            auto block1 = makeSineBlock (64, kNumChannels, 0.0);
            const bool r1 = rawWriter->writeFromAudioSampleBuffer (block1, 0, 64);
            expect (r1, "small write succeeds within fail boundary");

            auto block2 = makeSineBlock (512, kNumChannels, 1.0);
            const bool r2 = rawWriter->writeFromAudioSampleBuffer (block2, 0, 512);
            expect (! r2, "write crossing fail boundary returns false");

            expect (counters->firstFailedByte.load() >= failAtByte,
                    "stream recorded failure at declared boundary");

            delete rawWriter;
        }
    }

    void testPostFaderFloatWavRetainsHeadroom()
    {
        beginTest ("recording.writer.post-fader-float-headroom.v1");

        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("apex_post_fader_float_test");
        tempDir.createDirectory();
        auto wavFile = tempDir.getChildFile ("post_fader_headroom.wav");
        wavFile.deleteFile();

        juce::TimeSliceThread diskThread ("PostFaderFloatWriterTest");
        diskThread.startThread();
        {
            DAW::RecordingDiskWriterCore writer;
            expect (writer.start (wavFile, kSampleRate, kNumChannels, 24,
                                  diskThread, true),
                    "post-fader writer starts as 32-bit float WAV");

            juce::AudioBuffer<float> block (kNumChannels, 256);
            block.clear();
            block.setSample (0, 0, 1.25f);
            block.setSample (1, 0, -1.5f);
            block.setSample (0, 1, 0.25f);
            block.setSample (1, 1, -0.25f);
            const float* channels[2] = { block.getReadPointer (0), block.getReadPointer (1) };
            expect (writer.pushSamples (channels, block.getNumSamples ()),
                    "post-fader block enters the threaded writer");
            writer.stop();
        }
        diskThread.stopThread (2000);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (wavFile.createInputStream().release(), true));
        expect (reader != nullptr, "post-fader WAV reopens successfully");
        if (reader != nullptr)
        {
            expect (reader->usesFloatingPointData,
                    "printed recording is stored as IEEE float");
            juce::AudioBuffer<float> decoded (kNumChannels, 2);
            expect (reader->read (&decoded, 0, 2, 0, true, true),
                    "headroom samples can be decoded");
            expectWithinAbsoluteError (decoded.getSample (0, 0), 1.25f, 1.0e-6f,
                                       "left sample above 0 dBFS is retained");
            expectWithinAbsoluteError (decoded.getSample (1, 0), -1.5f, 1.0e-6f,
                                       "right sample below -1.0 is retained");
        }

        wavFile.deleteFile();
        tempDir.deleteRecursively();
    }

    void testTrackRecordModePersistence()
    {
        beginTest ("recording.track-mode-persistence.v1");

        DAW::Track sourceTrack ("record-mode-source", "Source");
        expect (sourceTrack.getRecordMode() == DAW::TrackRecordMode::Dry,
                "legacy/default tracks remain dry");
        sourceTrack.setRecordMode (DAW::TrackRecordMode::PostFader);

        DAW::Track restoredTrack ("record-mode-restored", "Restored");
        restoredTrack.restoreState (sourceTrack.getState());
        expect (restoredTrack.getRecordMode() == DAW::TrackRecordMode::PostFader,
                "post-fader selection survives track save/restore");
    }

    void testPostFaderTakeWritesProcessedAudioAndPreservesClock()
    {
        beginTest ("recording.post-fader-take.wet-audio-and-sample-clock.v1");

        const int blockSize = 128;
        auto projectDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("apex_post_fader_take_"
                                  + juce::Uuid().toString().substring (0, 8));
        projectDir.createDirectory();

        auto recordDir = projectDir.getChildFile ("Recordings");
        juce::Array<juce::File> takeFiles;
        {
            DAW::ApplicationState appState;
            DAW::TransportController transport (appState);
            DAW::TrackManager tracks;
            DAW::ClipManager clips;
            DAW::RoutingGraph routing;
            DAW::AudioFileManager audioFiles;
            DAW::AudioEngine audioEngine;
            DAW::RecordingEngine recorder;
            tracks.createMasterTrack();
            audioEngine.setSubsystems (&tracks, &clips, &transport, &routing, &audioFiles);
            audioEngine.prepare (kSampleRate, blockSize);
            recorder.setSubsystems (&tracks, nullptr, &transport, nullptr);
            recorder.setProjectDirectory (projectDir);
            recorder.prepare (kSampleRate, blockSize);

            auto* track = tracks.createTrack ("Post-Fader Test");
            expect (track != nullptr, "post-fader track is created");
            if (track == nullptr)
                return;

            track->setArmed (true);
            track->setRecordMode (DAW::TrackRecordMode::PostFader);
            track->getMonitoringState().setMode (DAW::InputMonitorMode::Auto);
            track->setInputSource (0, false);
            track->setVolume (0.5f);
            routing.addNode (track->getName(), DAW::RoutingNodeType::Track, track->getID());

            juce::AudioBuffer<float> dryInput (2, blockSize);
            dryInput.clear();
            for (int sample = 0; sample < blockSize; ++sample)
            {
                dryInput.setSample (0, sample, 0.7f);
                dryInput.setSample (1, sample, -0.8f);
            }

            audioEngine.setLiveInputBuffer (&dryInput, blockSize, 2);
            for (int warmup = 0; warmup < 24; ++warmup)
            {
                juce::AudioBuffer<float> output (2, blockSize);
                juce::AudioSourceChannelInfo info (output);
                audioEngine.process (info);
            }

            audioEngine.setPostFaderRecordTap (
                &recorder,
                [] (void* context, const DAW::TrackID& id,
                    const juce::AudioBuffer<float>& processed, int samples,
                    bool capture) noexcept
                {
                    static_cast<DAW::RecordingEngine*> (context)->processPostFaderBlock (
                        id, processed, samples, capture);
                });
            transport.recordWithoutSafetyCheck();
            expect (recorder.isActivelyRecording(), "armed post-fader take starts");

            {
                DAW::RecordingEngine::AudioCallbackScope callback (recorder, blockSize);
                expect (callback.shouldCapture(), "recording callback admits the take");
                recorder.processBlock (dryInput, blockSize, 2, callback.shouldCapture());
                audioEngine.setPostFaderRecordCaptureEnabled (callback.shouldCapture());
                juce::AudioBuffer<float> output (2, blockSize);
                juce::AudioSourceChannelInfo info (output);
                audioEngine.process (info);
                audioEngine.setPostFaderRecordCaptureEnabled (false);
            }

            // With monitoring Off, recording must still capture the vocal
            // without making the live mic audible through the track.
            {
                track->getMonitoringState().setMode (DAW::InputMonitorMode::Off);
                DAW::RecordingEngine::AudioCallbackScope callback (recorder, blockSize);
                recorder.processBlock (dryInput, blockSize, 2, callback.shouldCapture());
                audioEngine.setPostFaderRecordCaptureEnabled (callback.shouldCapture());
                juce::AudioBuffer<float> output (2, blockSize);
                juce::AudioSourceChannelInfo info (output);
                audioEngine.process (info);
                audioEngine.setPostFaderRecordCaptureEnabled (false);
                expect (track->getMonitoringState().getMode() == DAW::InputMonitorMode::Off,
                        "recording fallback does not turn live monitoring back on");
            }

            // A missing post-fader tap while monitoring is active still keeps
            // the take's clock aligned without silently substituting dry input.
            {
                track->getMonitoringState().setMode (DAW::InputMonitorMode::Auto);
                DAW::RecordingEngine::AudioCallbackScope callback (recorder, blockSize);
                recorder.processBlock (dryInput, blockSize, 2, callback.shouldCapture());
            }

            transport.stopRecording();
            recordDir.findChildFiles (takeFiles, juce::File::findFiles, false, "*.wav");
            expectEquals (takeFiles.size(), 1, "one post-fader take is finalized");
        }

        if (!takeFiles.isEmpty())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (
                wav.createReaderFor (takeFiles.getReference (0).createInputStream().release(), true));
            expect (reader != nullptr, "post-fader take opens as WAV");
            if (reader != nullptr)
            {
                expect (reader->usesFloatingPointData, "post-fader take remains float WAV");
                expectEquals ((int) reader->lengthInSamples, 3 * blockSize,
                              "wet, monitor-off dry, and missing-tap callbacks are present");

                juce::AudioBuffer<float> decoded (2, 3 * blockSize);
                expect (reader->read (&decoded, 0, decoded.getNumSamples(), 0, true, true),
                        "post-fader take decodes");
                const float expectedLeft = 0.7f * 0.5f
                    * std::cos (juce::MathConstants<float>::pi * 0.25f);
                const float expectedRight = -0.8f * 0.5f
                    * std::sin (juce::MathConstants<float>::pi * 0.25f);
                expectWithinAbsoluteError (decoded.getSample (0, 0), expectedLeft, 0.002f,
                                           "live track signal is printed after its fader");
                expectWithinAbsoluteError (decoded.getSample (1, 0), expectedRight, 0.002f,
                                           "post-fader pan is printed to the right channel");
                expectWithinAbsoluteError (decoded.getSample (0, blockSize), 0.7f, 1.0e-6f,
                                           "monitor-off post-fader take preserves the vocal as dry audio");
                expectWithinAbsoluteError (decoded.getSample (1, blockSize), -0.8f, 1.0e-6f,
                                           "monitor-off dry fallback preserves the other input channel");
                expectWithinAbsoluteError (decoded.getSample (0, 2 * blockSize), 0.0f, 1.0e-7f,
                                           "missing active-monitor tap writes silence instead of dry input");
                expectWithinAbsoluteError (decoded.getSample (1, 2 * blockSize), 0.0f, 1.0e-7f,
                                           "silent fallback preserves stereo alignment");
            }
        }

        projectDir.deleteRecursively();
    }

    void testWavHeaderPrefix()
    {
        beginTest ("recording.writer.wav-header-prefix.v1");

        auto counters = std::make_shared<StreamCounters>();

        {
            auto* stream = new ScriptedSeekableOutputStream (16384, -1, counters);

            juce::WavAudioFormat wav;
            juce::AudioFormat* fmt = &wav;
            juce::AudioFormatWriter* rawWriter =
                fmt->createWriterFor (
                    stream, kSampleRate, (unsigned int) kNumChannels, 16,
                    {}, 0);
            expect (rawWriter != nullptr, "wav writer created over seekable stream");

            constexpr int blockSize = 256;
            constexpr int numBlocks = 4;
            for (int i = 0; i < numBlocks; ++i)
            {
                auto block = makeSineBlock (blockSize, kNumChannels, i * 0.3);
                expect (rawWriter->writeFromAudioSampleBuffer (block, 0, blockSize),
                        "block " + juce::String (i) + " wrote successfully");
            }

            delete rawWriter;
        }

        expect (counters->seekCalls.load() > 0, "header rewrite involves seeks");
        expect (counters->firstFailedByte.load() < 0, "no write failure in non-failing stream");
    }
};

static RecordingWriterIntegrityTests recordingWriterIntegrityTests;

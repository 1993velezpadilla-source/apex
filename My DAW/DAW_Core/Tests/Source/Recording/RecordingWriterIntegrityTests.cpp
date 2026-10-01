#include <JuceHeader.h>
#include "Fakes/ScriptedAudioFormatWriter.h"
#include "Fakes/ScriptedSeekableOutputStream.h"
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

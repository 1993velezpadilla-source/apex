#include <JuceHeader.h>

// ── Production subsystems under test ────────────────────────────────────────
#include "../../../Source/AudioEngineCore/AudioEngine.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/TransportCore/TransportController.h"
#include "../../../Source/StateCore/ApplicationState.h"
#include "../../../Source/PluginHostCore/PluginChainCore.h"
#include "../../../Source/MasterCore/MasterBusEngine.h"
#include "../../../Source/MonitorCore/ControlRoomEngine.h"
#include "../../../Source/DeviceCore/AudioDeviceBlockAdapterCore.h"
#include "../../../Source/PluginHostCore/HostedPluginIsolationCore.h"

//==============================================================================
/** Deterministic passthrough insert used by the mock plugin-chain variants.
    Passes audio untouched; probes the block-size contract of the host:
      - preparedBlockSize():  block size the chain prepared the plugin with
      - lastBlockSize():      block size the plugin actually received
      - lastInputRms():       RMS of the audio entering the insert
    In silenceOnViolation mode the plugin clears its output whenever the
    callback frame count exceeds the prepared maximum — the deterministic
    equivalent of a third-party plugin whose internal buffers are sized to the
    prepared block (a host contract violation then produces silence/corruption).
*/
class GainProbePlugin final : public juce::AudioPluginInstance
{
public:
    GainProbePlugin (bool silenceOnViolation,
                     bool silenceOnLifecycleViolation = false,
                     float gain = 1.0f,
                     int reportedLatencySamples = 0)
        : juce::AudioPluginInstance (BusesProperties()
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          silenceOnViolation_ (silenceOnViolation),
          silenceOnLifecycleViolation_ (silenceOnLifecycleViolation),
          gain_ (gain)
    {
        setLatencySamples (juce::jmax (0, reportedLatencySamples));
    }

    const juce::String getName() const override { return "APEX Gain Probe"; }

    void prepareToPlay (double, int maximumExpectedSamplesPerBlock) override
    {
        if (preparedWithoutRelease_)
        {
            if (processedSincePrepare_)
                ++hotPrepareWithoutReleaseCount_;
            else
                ++coldPrepareWithoutReleaseCount_;
            lifecycleViolationLatched_ = true;
        }

        preparedBlockSize_ = maximumExpectedSamplesPerBlock;
        preparedWithoutRelease_ = true;
        processedSincePrepare_ = false;
    }

    void releaseResources() override
    {
        ++releaseCount_;
        preparedWithoutRelease_ = false;
        processedSincePrepare_ = false;
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        processedSincePrepare_ = true;
        ++processCount_;
        lastBlockSize_ = buffer.getNumSamples();
        maximumBlockSizeSeen_ = juce::jmax (maximumBlockSizeSeen_, lastBlockSize_);
        lastInputRms_ = 0.0f;
        const int n = buffer.getNumSamples();
        const int chans = juce::jmax (1, buffer.getNumChannels());
        if (n > 0)
        {
            double sum = 0.0;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* d = buffer.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    sum += (double) d[i] * d[i];
            }
            lastInputRms_ = (float) std::sqrt (sum / ((double) n * (double) chans));
        }

        if (silenceOnViolation_ && n > preparedBlockSize_)
        {
            buffer.clear();
            ++violationCount_;
        }

        if (silenceOnLifecycleViolation_ && lifecycleViolationLatched_)
            buffer.clear();
        else if (gain_ != 1.0f)
            buffer.applyGain (gain_);

        lastOutputRms_ = 0.0f;
        if (n > 0)
        {
            double sum = 0.0;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* d = buffer.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    sum += (double) d[i] * d[i];
            }
            lastOutputRms_ = (float) std::sqrt (sum / ((double) n * (double) chans));
        }
    }

    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destinationData) override { destinationData.reset(); }
    void setStateInformation (const void*, int) override {}

    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = getName();
        description.descriptiveName = getName();
        description.pluginFormatName = "APEX Test";
        description.manufacturerName = "APEX";
        description.version = "1";
        description.uniqueId = 0x41504558;
        description.deprecatedUid = description.uniqueId;
        description.numInputChannels = 2;
        description.numOutputChannels = 2;
    }

    int  lastBlockSize() const noexcept     { return lastBlockSize_; }
    int  maximumBlockSizeSeen() const noexcept { return maximumBlockSizeSeen_; }
    int  preparedBlockSize() const noexcept { return preparedBlockSize_; }
    int  violationCount() const noexcept    { return violationCount_; }
    float lastInputRms() const noexcept     { return lastInputRms_; }
    float lastOutputRms() const noexcept    { return lastOutputRms_; }
    int coldPrepareWithoutReleaseCount() const noexcept { return coldPrepareWithoutReleaseCount_; }
    int hotPrepareWithoutReleaseCount() const noexcept  { return hotPrepareWithoutReleaseCount_; }
    int releaseCount() const noexcept { return releaseCount_; }
    int processCount() const noexcept { return processCount_; }

private:
    const bool silenceOnViolation_;
    const bool silenceOnLifecycleViolation_;
    const float gain_;
    int preparedBlockSize_ = 0;
    int lastBlockSize_ = 0;
    int maximumBlockSizeSeen_ = 0;
    int violationCount_ = 0;
    float lastInputRms_ = 0.0f;
    float lastOutputRms_ = 0.0f;
    bool preparedWithoutRelease_ = false;
    bool processedSincePrepare_ = false;
    bool lifecycleViolationLatched_ = false;
    int coldPrepareWithoutReleaseCount_ = 0;
    int hotPrepareWithoutReleaseCount_ = 0;
    int releaseCount_ = 0;
    int processCount_ = 0;
};

//==============================================================================
/** Test-only AudioPluginFormat that hands a GainProbePlugin to
    ClipRegionPluginCore::loadForClip() — deterministic, no third-party VSTs,
    no production code changes. */
class TestClipRegionFormat final : public juce::AudioPluginFormat
{
public:
    TestClipRegionFormat (bool silenceOnViolation, float gain = 1.0f)
        : silenceOnViolation_ (silenceOnViolation), gain_ (gain) {}

    juce::String getName() const override { return "APEX Test Format"; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&,
                              const juce::String&) override {}
    bool fileMightContainThisPluginType (const juce::String&) override { return true; }
    juce::String getNameOfPluginFromIdentifier (const juce::String&) override { return {}; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

    void createPluginInstance (const juce::PluginDescription&,
                               double, int, PluginCreationCallback callback) override
    {
        auto plugin = std::make_unique<GainProbePlugin> (silenceOnViolation_, false, gain_);
        lastCreated_ = plugin.get();
        callback (std::move (plugin), {});
    }

    /** Non-owning probe of the most recently created plugin (valid after
        ClipRegionPluginCore::loadForClip). */
    GainProbePlugin* lastCreated() const noexcept { return lastCreated_; }

private:
    const bool silenceOnViolation_;
    const float gain_;
    GainProbePlugin* lastCreated_ = nullptr;
};

//==============================================================================
/**
    @test    track.reprepare.survival.v1
    @verify  #2A: a track that plays before a runtime device reprepare must
             still contribute non-zero audio after reprepare.

             Lifecycle GOOD: prepare(1024) -> create tracks/clips -> play.
             Lifecycle BAD:  prepare(480)  -> create tracks/clips -> play ->
                             releaseResources() -> prepare(1024) -> play on.

             Three audio tracks carry sine tones at 440/550/660 Hz with
             distinct amplitudes so the master output can be decomposed per
             track (Goertzel) and the FIRST silent track detected.

             No third-party plugins are used. Mock plugin-chain variants use
             GainProbePlugin (deterministic passthrough + block-size probe).
*/
class TrackReprepareHarnessTests : public juce::UnitTest
{
public:
    TrackReprepareHarnessTests()
        : juce::UnitTest ("track.reprepare.survival.v1", "APEX.Diagnostics") {}

    // ── Helpers ────────────────────────────────────────────────────────────

    static void writeSineWav (const juce::File& file, double freq, float amp,
                              double durationSeconds = 4.0)
    {
        constexpr double sr = 44100.0;
        const int n = (int) (durationSeconds * sr);
        juce::AudioBuffer<float> buf (1, n);
        for (int i = 0; i < n; ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr));

        // FileOutputStream preserves an existing file unless it is explicitly
        // replaced. Tests reuse deterministic temp paths, so always publish a
        // fresh WAV with the requested duration instead of retaining an older
        // four-second fixture from a previous run.
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (new juce::FileOutputStream (file), sr, 1, 16, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buf, 0, n);
    }

    /** Goertzel-style single-bin magnitude of `freq` in `data[0..n)`. */
    static float measureTone (const float* data, int n, double sampleRate, double freq)
    {
        double sumCos = 0.0, sumSin = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate;
            sumCos += (double) data[i] * std::cos (phase);
            sumSin += (double) data[i] * std::sin (phase);
        }
        return (float) (2.0 * std::sqrt (sumCos * sumCos + sumSin * sumSin) / (double) n);
    }

    struct Harness
    {
        DAW::ApplicationState appState;
        DAW::TransportController transport { appState };
        DAW::TrackManager tracks;
        DAW::ClipManager clips;
        DAW::RoutingGraph routing;
        DAW::AudioFileManager audioFiles;
        DAW::AudioEngine engine;

        juce::File dir;
        juce::File wavA, wavB, wavC;
        DAW::AudioClip* clipA = nullptr;
        DAW::AudioClip* clipB = nullptr;
        DAW::AudioClip* clipC = nullptr;
        DAW::Track* trackA = nullptr;
        DAW::Track* trackB = nullptr;
        DAW::Track* trackC = nullptr;

        static constexpr double kSr = 44100.0;

        bool setup (juce::UnitTest& ut)
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("apex_track_harness");
            dir.createDirectory();

            wavA = dir.getChildFile ("tA_440.wav");
            wavB = dir.getChildFile ("tB_550.wav");
            wavC = dir.getChildFile ("tC_660.wav");
            writeSineWav (wavA, 440.0, 0.5f);
            writeSineWav (wavB, 550.0, 0.4f);
            writeSineWav (wavC, 660.0, 0.3f);

            tracks.createMasterTrack();
            trackA = tracks.createTrack ("TrackA");
            trackB = tracks.createTrack ("TrackB");
            trackC = tracks.createTrack ("TrackC");

            routing.addNode ("master", DAW::RoutingNodeType::Master);
            routing.addNode ("nA", DAW::RoutingNodeType::Track, trackA->getID());
            routing.addNode ("nB", DAW::RoutingNodeType::Track, trackB->getID());
            routing.addNode ("nC", DAW::RoutingNodeType::Track, trackC->getID());
            routing.publishSnapshotOnly();

            clipA = clips.createAudioClip ("CA", wavA);
            clipB = clips.createAudioClip ("CB", wavB);
            clipC = clips.createAudioClip ("CC", wavC);
            clipA->setTrackID (trackA->getID());
            clipB->setTrackID (trackB->getID());
            clipC->setTrackID (trackC->getID());
            clipA->setStartPosition (0);
            clipB->setStartPosition (0);
            clipC->setStartPosition (0);

            audioFiles.loadForClip (clipA->getID(), wavA);
            audioFiles.loadForClip (clipB->getID(), wavB);
            audioFiles.loadForClip (clipC->getID(), wavC);

            engine.setSubsystems (&tracks, &clips, &transport, &routing, &audioFiles);
            engine.setClipRegionPluginCore (&clipRegion);
            return true;
        }

        // ── N-track variant (track-count sweep) ───────────────────────────
        // Frequencies 440 + i*110 Hz, amplitude 0.4. Tracks 0..N-1 are
        // created in order; the first and last track are independently
        // identifiable in the master output.
        std::vector<DAW::Track*> xtracks;
        std::vector<DAW::AudioClip*> xclips;
        std::vector<juce::File> xwavs;

        bool setupN (juce::UnitTest& ut, int n)
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("apex_track_harness");
            dir.createDirectory();

            tracks.createMasterTrack();
            routing.addNode ("master", DAW::RoutingNodeType::Master);

            for (int i = 0; i < n; ++i)
            {
                const double freq = 440.0 + 110.0 * i;
                auto wav = dir.getChildFile (juce::String ("tN") + juce::String (i) + ".wav");
                writeSineWav (wav, freq, 0.4f);
                xwavs.push_back (wav);

                auto* track = tracks.createTrack (juce::String ("T") + juce::String (i));
                xtracks.push_back (track);
                routing.addNode (juce::String ("n") + juce::String (i),
                                 DAW::RoutingNodeType::Track, track->getID());

                auto* clip = clips.createAudioClip (juce::String ("C") + juce::String (i), wav);
                xclips.push_back (clip);
                clip->setTrackID (track->getID());
                clip->setStartPosition (0);
                audioFiles.loadForClip (clip->getID(), wav);
            }

            routing.publishSnapshotOnly();
            engine.setSubsystems (&tracks, &clips, &transport, &routing, &audioFiles);
            return true;
        }

        // ── Mock plugin chains ────────────────────────────────────────────
        // Mirrors ApplicationCore::getPluginChain() + prepareRenderGraph():
        // chain prepared with the current device rate/block, plugin appended,
        // chain re-prepared, then the immutable map published to the engine.
        std::map<DAW::TrackID, std::shared_ptr<DAW::PluginChainCore>> chains;
        std::map<DAW::TrackID, GainProbePlugin*> chainProbes;

        GainProbePlugin* attachChainProbe (juce::UnitTest& ut,
                                           DAW::Track* track,
                                           int blockSize,
                                           bool silenceOnViolation,
                                           bool silenceOnLifecycleViolation = false,
                                           float gain = 1.0f)
        {
            if (track == nullptr)
                return nullptr;

            const DAW::TrackID tid = track->getID();
            auto chain = std::make_shared<DAW::PluginChainCore>();
            chain->prepare (kSr, blockSize);
            auto plugin = std::make_unique<GainProbePlugin> (
                silenceOnViolation, silenceOnLifecycleViolation, gain);
            auto* probe = plugin.get();
            const int idx = chain->appendPluginInstanceForTesting (std::move (plugin));
            ut.expect (idx == 0, "mock plugin appended to chain");
            chains[tid] = chain;
            chainProbes[tid] = probe;
            publishChains();
            return probe;
        }

        void publishChains()
        {
            auto map = std::make_shared<DAW::AudioEngine::PluginChainSnapshotMap>();
            for (const auto& [tid, chain] : chains)
                (*map)[tid] = chain;
            engine.publishPluginChainsSnapshot (std::move (map));
        }

        void setupChains (juce::UnitTest& ut, bool allTracks, int blockSize,
                          bool silenceOnViolation)
        {
            const DAW::Track* tracksToUse[3] = { trackA, trackB, trackC };
            const int count = allTracks ? 3 : 1;
            for (int i = 0; i < count; ++i)
            {
                attachChainProbe (ut, const_cast<DAW::Track*> (tracksToUse[i]),
                                  blockSize, silenceOnViolation);
            }
        }

        /** Real production device-change lifecycle at engine level:
            audioDeviceStopped -> releaseResources; audioDeviceAboutToStart ->
            prepare; prepareRenderGraph -> chain->prepare for every chain. */
        void reprepare (double sr, int blockSize)
        {
            engine.releaseResources();
            engine.prepare (sr, blockSize);
            for (const auto& [tid, chain] : chains)
                chain->prepare (sr, blockSize);
            clipRegion.prepare (sr, blockSize);
        }

        // ── Clip-region plugins (per-clip insert) ─────────────────────────
        // Mirrors ApplicationCore::prepareRenderGraph: clipRegionPluginCore_
        // is prepared with the device rate/block, and entries persist across
        // releaseResources(). The internal JUCE Gain insert is deterministic
        // and requires no third-party VSTs.
        DAW::ClipRegionPluginCore clipRegion;
        std::vector<GainProbePlugin*> clipRegionProbes;   // non-owning

        void setupClipRegion (juce::UnitTest& ut, int blockSize, bool allClips,
                              float gain = 1.0f)
        {
            clipRegion.prepare (kSr, blockSize);

            juce::AudioPluginFormatManager fm;
            auto* testFormat = new TestClipRegionFormat (true, gain);
            fm.addFormat (testFormat);

            const DAW::AudioClip* clipsToUse[3] = { clipA, clipB, clipC };
            const int count = allClips ? 3 : 1;
            for (int i = 0; i < count; ++i)
            {
                juce::PluginDescription desc;
                desc.name = "APEX Gain Probe";
                desc.pluginFormatName = "APEX Test Format";
                auto res = clipRegion.loadForClip (clipsToUse[i]->getID(), desc, fm);
                ut.expect (res.success, "clip-region probe plugin loaded");
                if (testFormat->lastCreated() != nullptr)
                    clipRegionProbes.push_back (testFormat->lastCreated());
            }
        }

        /** Render exactly ONE block; returns the master left channel. */
        void renderOne (int blockSize, std::vector<float>& out)
        {
            const int n = blockSize;
            std::vector<float> buf (n * 2, 0.f);
            std::fill (buf.begin(), buf.end(), 0.f);
            float* ptrs[2] = { buf.data(), buf.data() + n };
            juce::AudioBuffer<float> ob (ptrs, 2, n);
            juce::AudioSourceChannelInfo info (&ob, 0, n);
            engine.process (info);
            out.assign (n, 0.f);
            for (int i = 0; i < n; ++i)
                out[i] = ob.getSample (0, i);
        }

        /** Render `numBlocks` blocks of `blockSize` from the current transport
            position; returns the LAST block's master output in `out`. */
        void render (int blockSize, int numBlocks, std::vector<float>& out)
        {
            const int n = blockSize;
            std::vector<float> buf (n * 2, 0.f);
            for (int b = 0; b < numBlocks; ++b)
            {
                std::fill (buf.begin(), buf.end(), 0.f);
                float* ptrs[2] = { buf.data(), buf.data() + n };
                juce::AudioBuffer<float> ob (ptrs, 2, n);
                juce::AudioSourceChannelInfo info (&ob, 0, n);
                engine.process (info);
                if (b == numBlocks - 1)
                {
                    out.assign (n, 0.f);
                    for (int i = 0; i < n; ++i)
                        out[i] = ob.getSample (0, i);   // left channel master
                }
            }
        }
    };

    // ── #2B full-stack, plugin-free playback fixture ─────────────────────
    // Extends the existing real AudioEngine fixture through the production
    // MasterBusEngine and ControlRoomEngine. The backing I/O storage is 8192
    // samples while AudioSourceChannelInfo exposes only the active callback
    // count, matching APEX's production capacity-versus-frames contract.
    struct FullStackResult
    {
        std::vector<float> engineOutput;
        std::vector<float> masterOutput;
        std::vector<float> controlRoomOutput;
        std::vector<double> callbackMs;
        int64_t finalTransportPosition = 0;
        bool activeSentinelOverwritten = true;
        bool inactiveTailPreserved = true;
        bool secondHalfValid = true;
        bool repeatedHalfDetected = false;
        bool repeatedCallbackDetected = false;
        bool allFinite = true;
        float minimumBlockRms = std::numeric_limits<float>::max();
        float maximumAdjacentJump = 0.0f;
    };

    struct FullStackHarness
    {
        Harness core;
        DAW::MasterBusEngine master;
        DAW::ControlRoomEngine controlRoom;
        std::shared_ptr<DAW::PluginChainCore> masterChain;
        GainProbePlugin* masterProbe = nullptr;
        DAW::Track* sendBusTrack = nullptr;
        GainProbePlugin* sendBusProbe = nullptr;

        static constexpr int kCapacity = 8192;
        static constexpr float kSentinel = 0.1234567f;

        bool setup (juce::UnitTest& ut, int blockSize)
        {
            core.dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("apex_fullstack_2048_harness");
            core.dir.createDirectory();
            core.wavA = core.dir.getChildFile ("fsA_440_12s.wav");
            core.wavB = core.dir.getChildFile ("fsB_550_12s.wav");
            core.wavC = core.dir.getChildFile ("fsC_660_12s.wav");
            writeSineWav (core.wavA, 440.0, 0.5f, 12.0);
            writeSineWav (core.wavB, 550.0, 0.4f, 12.0);
            writeSineWav (core.wavC, 660.0, 0.3f, 12.0);

            core.tracks.createMasterTrack();
            core.trackA = core.tracks.createTrack ("TrackA");
            core.trackB = core.tracks.createTrack ("TrackB");
            core.trackC = core.tracks.createTrack ("TrackC");
            core.routing.addNode ("master", DAW::RoutingNodeType::Master);
            core.routing.addNode ("nA", DAW::RoutingNodeType::Track, core.trackA->getID());
            core.routing.addNode ("nB", DAW::RoutingNodeType::Track, core.trackB->getID());
            core.routing.addNode ("nC", DAW::RoutingNodeType::Track, core.trackC->getID());
            core.routing.publishSnapshotOnly();

            core.clipA = core.clips.createAudioClip ("CA", core.wavA);
            core.clipB = core.clips.createAudioClip ("CB", core.wavB);
            core.clipC = core.clips.createAudioClip ("CC", core.wavC);
            if (core.clipA == nullptr || core.clipB == nullptr || core.clipC == nullptr)
                return false;
            core.clipA->setTrackID (core.trackA->getID());
            core.clipB->setTrackID (core.trackB->getID());
            core.clipC->setTrackID (core.trackC->getID());
            core.clipA->setStartPosition (0);
            core.clipB->setStartPosition (0);
            core.clipC->setStartPosition (0);
            const int64_t fullLength = (int64_t) (12.0 * Harness::kSr);
            core.clipA->setLength (fullLength);
            core.clipB->setLength (fullLength);
            core.clipC->setLength (fullLength);
            core.audioFiles.loadForClip (core.clipA->getID(), core.wavA);
            core.audioFiles.loadForClip (core.clipB->getID(), core.wavB);
            core.audioFiles.loadForClip (core.clipC->getID(), core.wavC);
            core.engine.setSubsystems (&core.tracks, &core.clips, &core.transport,
                                       &core.routing, &core.audioFiles);
            core.engine.setClipRegionPluginCore (&core.clipRegion);

            master.bindMasterTrack (core.tracks.getMasterTrack());
            core.engine.prepare (Harness::kSr, blockSize);
            master.prepare (Harness::kSr, blockSize);
            controlRoom.prepare (Harness::kSr, blockSize);
            return true;
        }

        void attachTrackGainProbes (juce::UnitTest& ut, int blockSize)
        {
            core.attachChainProbe (ut, core.trackA, blockSize, true, false, 0.50f);
            core.attachChainProbe (ut, core.trackB, blockSize, true, false, 0.75f);
            core.attachChainProbe (ut, core.trackC, blockSize, true, false, 1.00f);
        }

        void attachMasterGainProbe (juce::UnitTest& ut, int blockSize, float gain)
        {
            masterChain = std::make_shared<DAW::PluginChainCore>();
            masterChain->prepare (Harness::kSr, blockSize);
            auto plugin = std::make_unique<GainProbePlugin> (true, false, gain);
            masterProbe = plugin.get();
            ut.expectEquals (masterChain->appendPluginInstanceForTesting (std::move (plugin)), 0,
                             "master mock plugin appended");
            master.setPluginChain (masterChain.get());
        }

        void addSendBusPath (juce::UnitTest& ut, int blockSize, bool attachProbe,
                             float probeGain = 1.0f)
        {
            sendBusTrack = core.tracks.createTrack ("Isolation Send Bus");
            sendBusTrack->setRole (DAW::TrackRole::Bus);
            auto* busNode = core.routing.addNode (
                "isolationSendBus", DAW::RoutingNodeType::Bus, sendBusTrack->getID());
            auto* send = core.routing.connect (
                "nA", "isolationSendBus", DAW::ConnectionType::Send);
            ut.expect (busNode != nullptr && send != nullptr,
                       "isolation send/bus routing created");
            core.routing.publishSnapshotOnly();

            // Topology was prepared before this optional diagnostic route was
            // added. Reprepare on the control side before the first callback.
            core.engine.releaseResources();
            core.engine.prepare (Harness::kSr, blockSize);

            if (attachProbe)
                sendBusProbe = core.attachChainProbe (
                    ut, sendBusTrack, blockSize, true, false, probeGain);
        }

        FullStackResult render (int blockSize, int numBlocks)
        {
            FullStackResult result;
            const int totalSamples = blockSize * numBlocks;
            result.engineOutput.reserve ((size_t) totalSamples);
            result.masterOutput.reserve ((size_t) totalSamples);
            result.controlRoomOutput.reserve ((size_t) totalSamples);
            result.callbackMs.reserve ((size_t) numBlocks);

            std::vector<float> storage ((size_t) kCapacity * 2u, kSentinel);
            std::vector<float> previousBlock ((size_t) blockSize, 0.0f);
            bool havePreviousBlock = false;
            const double ticksPerSecond = (double) juce::Time::getHighResolutionTicksPerSecond();

            core.transport.setPosition (0);
            core.transport.play();

            for (int block = 0; block < numBlocks; ++block)
            {
                std::fill (storage.begin(), storage.end(), kSentinel);
                float* channels[2] = { storage.data(), storage.data() + kCapacity };
                juce::AudioBuffer<float> io (channels, 2, kCapacity);
                juce::AudioSourceChannelInfo active (&io, 0, blockSize);

                const int64_t startTicks = juce::Time::getHighResolutionTicks();
                core.engine.process (active);
                appendActive (result.engineOutput, io, blockSize);

                master.processBlock (io.getWritePointer (0), io.getWritePointer (1), blockSize);
                appendActive (result.masterOutput, io, blockSize);

                controlRoom.processBlock (active);
                const int64_t endTicks = juce::Time::getHighResolutionTicks();
                result.callbackMs.push_back (1000.0 * (double) (endTicks - startTicks) / ticksPerSecond);
                appendActive (result.controlRoomOutput, io, blockSize);

                double sumSquares = 0.0;
                float maxHalfDifference = 0.0f;
                for (int i = 0; i < blockSize; ++i)
                {
                    const float sample = io.getSample (0, i);
                    result.allFinite = result.allFinite && std::isfinite (sample)
                        && std::isfinite (io.getSample (1, i));
                    result.activeSentinelOverwritten = result.activeSentinelOverwritten
                        && sample != kSentinel && io.getSample (1, i) != kSentinel;
                    sumSquares += (double) sample * sample;
                    if (i > 0)
                        result.maximumAdjacentJump = juce::jmax (
                            result.maximumAdjacentJump,
                            std::abs (sample - io.getSample (0, i - 1)));
                }

                const int half = blockSize / 2;
                double firstHalfSquares = 0.0, secondHalfSquares = 0.0;
                for (int i = 0; i < half; ++i)
                {
                    const float first = io.getSample (0, i);
                    const float second = io.getSample (0, i + half);
                    firstHalfSquares += (double) first * first;
                    secondHalfSquares += (double) second * second;
                    maxHalfDifference = juce::jmax (maxHalfDifference, std::abs (first - second));
                }
                result.secondHalfValid = result.secondHalfValid
                    && firstHalfSquares > 1.0e-8 && secondHalfSquares > 1.0e-8;
                result.repeatedHalfDetected = result.repeatedHalfDetected
                    || maxHalfDifference < 1.0e-6f;

                if (havePreviousBlock)
                {
                    float maxBlockDifference = 0.0f;
                    for (int i = 0; i < blockSize; ++i)
                        maxBlockDifference = juce::jmax (
                            maxBlockDifference,
                            std::abs (io.getSample (0, i) - previousBlock[(size_t) i]));
                    result.repeatedCallbackDetected = result.repeatedCallbackDetected
                        || maxBlockDifference < 1.0e-6f;
                }
                for (int i = 0; i < blockSize; ++i)
                    previousBlock[(size_t) i] = io.getSample (0, i);
                havePreviousBlock = true;

                result.minimumBlockRms = juce::jmin (
                    result.minimumBlockRms,
                    (float) std::sqrt (sumSquares / (double) blockSize));

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = blockSize; i < kCapacity; ++i)
                        result.inactiveTailPreserved = result.inactiveTailPreserved
                            && io.getSample (ch, i) == kSentinel;
            }

            result.finalTransportPosition = (int64_t) core.transport.getPosition();
            return result;
        }

        /** Simulates the production device boundary when the backend delivers
            more frames than the graph's prepared maximum. The production
            AudioDeviceBlockAdapterCore owns the chunk schedule; all audio
            stages below it receive only the resulting bounded active views. */
        std::vector<float> renderThroughDeviceAdapter (int backendBlockSize,
                                                       int maximumPreparedSamples,
                                                       int numBackendCallbacks)
        {
            std::vector<float> output;
            output.reserve ((size_t) backendBlockSize * (size_t) numBackendCallbacks);
            std::vector<float> storage ((size_t) kCapacity * 2u, kSentinel);

            core.transport.setPosition (0);
            core.transport.play();
            for (int callback = 0; callback < numBackendCallbacks; ++callback)
            {
                std::fill (storage.begin(), storage.end(), kSentinel);
                float* channels[2] = { storage.data(), storage.data() + kCapacity };
                juce::AudioBuffer<float> io (channels, 2, kCapacity);

                DAW::AudioDeviceBlockAdapterCore::forEachPreparedChunk (
                    backendBlockSize, maximumPreparedSamples,
                    [this, &io] (int offset, int numSamples)
                    {
                        juce::AudioSourceChannelInfo active (&io, offset, numSamples);
                        core.engine.process (active);
                        master.processBlock (io.getWritePointer (0, offset),
                                             io.getWritePointer (1, offset),
                                             numSamples);
                        controlRoom.processBlock (active);
                    });

                const float* left = io.getReadPointer (0);
                output.insert (output.end(), left, left + backendBlockSize);
            }
            return output;
        }

        static void appendActive (std::vector<float>& destination,
                                  const juce::AudioBuffer<float>& source,
                                  int numSamples)
        {
            const float* data = source.getReadPointer (0);
            destination.insert (destination.end(), data, data + numSamples);
        }
    };

    static float correlation (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size() || a.empty())
            return 0.0f;
        double aa = 0.0, bb = 0.0, ab = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
            ab += (double) a[i] * b[i];
        }
        return aa > 0.0 && bb > 0.0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
    }

    static double percentileMs (std::vector<double> values, double percentile)
    {
        if (values.empty())
            return 0.0;
        std::sort (values.begin(), values.end());
        const size_t index = (size_t) juce::jlimit<int64_t> (
            0, (int64_t) values.size() - 1,
            (int64_t) std::floor (percentile * (double) (values.size() - 1)));
        return values[index];
    }

    void runTest() override
    {
        beginTest ("failure-2b.plugin-isolation.session-flag-and-effective-latency");
        {
            juce::StringArray exactFlag;
            exactFlag.add (DAW::HostedPluginIsolationCore::kCommandLineFlag);
            juce::StringArray similarButInvalid;
            similarButInvalid.add ("--apex-bypass-all-hosted-dsp-extra");
            expect (DAW::HostedPluginIsolationCore::commandLineRequestsIsolation (exactFlag),
                    "exact isolation command-line token enables the session mode");
            expect (! DAW::HostedPluginIsolationCore::commandLineRequestsIsolation (similarButInvalid),
                    "similar command-line text does not enable isolation");

            {
                DAW::HostedPluginIsolationCore::ScopedOverride normalMode (false);
                expectEquals (DAW::HostedPluginIsolationCore::effectiveHostedLatencySamples (256), 256,
                              "normal mode publishes the hosted latency");
            }
            {
                DAW::HostedPluginIsolationCore::ScopedOverride isolationMode (true);
                expectEquals (DAW::HostedPluginIsolationCore::effectiveHostedLatencySamples (256), 0,
                              "isolation mode publishes zero effective hosted latency");
            }
        }

        beginTest ("failure-2b.plugin-isolation.same-chain-instance-normal-vs-global-bypass");
        {
            DAW::PluginChainCore chain;
            chain.prepare (Harness::kSr, 2048);
            auto plugin = std::make_unique<GainProbePlugin> (true, false, 0.5f, 256);
            auto* probe = plugin.get();
            expectEquals (chain.appendPluginInstanceForTesting (std::move (plugin)), 0,
                          "isolation chain probe appended");
            expectEquals (chain.totalLatencySamples(), 256,
                          "raw plugin latency remains factual");

            juce::AudioBuffer<float> audio (2, 2048);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> sidechain (2, 2048);
            sidechain.clear();
            sidechain.applyGain (0.25f);

            {
                DAW::HostedPluginIsolationCore::ScopedOverride normalMode (false);
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, 2048);
                chain.processBlock (audio, 2048);
                expectEquals (probe->processCount(), 1,
                              "NORMAL processBlock called exactly once");
                expectWithinAbsoluteError (audio.getSample (0, 0), 0.5f, 1.0e-7f,
                                           "NORMAL gain probe produces 0.5 from 1.0");
            }

            {
                DAW::HostedPluginIsolationCore::ScopedOverride isolationMode (true);
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, 2048);
                chain.processBlock (audio, 2048);
                expectEquals (probe->processCount(), 1,
                              "BYPASS does not call the same hosted instance again");
                expectWithinAbsoluteError (audio.getSample (0, 0), 1.0f, 1.0e-7f,
                                           "BYPASS audio is transparent");

                chain.processBlockWithMidi (audio, midi, 2048);
                chain.processBlockWithSidechain (audio, sidechain, 2048);
                expectEquals (probe->processCount(), 1,
                              "MIDI and sidechain hosted dispatches are also gated");
                expectWithinAbsoluteError (audio.getSample (0, 2047), 1.0f, 1.0e-7f,
                                           "MIDI/sidechain bypass preserves the active tail");
                expectEquals (DAW::HostedPluginIsolationCore::effectiveHostedLatencySamples (
                                  chain.totalLatencySamples()), 0,
                              "same present instance contributes zero effective isolation latency");
            }
            expectEquals (chain.totalLatencySamples(), 256,
                          "isolation never mutates the plugin raw latency report");
        }

        beginTest ("failure-2b.plugin-isolation.clip-region-normal-vs-global-bypass");
        {
            DAW::ClipRegionPluginCore clipPlugins;
            clipPlugins.prepare (Harness::kSr, 2048);
            juce::AudioPluginFormatManager formats;
            auto* format = new TestClipRegionFormat (true, 0.5f);
            formats.addFormat (format);
            juce::PluginDescription description;
            description.name = "APEX Gain Probe";
            description.pluginFormatName = "APEX Test Format";
            const DAW::ClipID clipId = "plugin-isolation-clip";
            auto loaded = clipPlugins.loadForClip (clipId, description, formats);
            expect (loaded.success && format->lastCreated() != nullptr,
                    "clip-region isolation probe loaded and remains present");

            juce::AudioBuffer<float> audio (2, 2048);
            {
                DAW::HostedPluginIsolationCore::ScopedOverride normalMode (false);
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, 2048);
                clipPlugins.processClipBlock (clipId, audio, 2048);
                expectEquals (format->lastCreated()->processCount(), 1,
                              "NORMAL clip-region processBlock called");
                expectWithinAbsoluteError (audio.getSample (0, 1024), 0.5f, 1.0e-7f,
                                           "NORMAL clip-region gain is applied");
            }
            {
                DAW::HostedPluginIsolationCore::ScopedOverride isolationMode (true);
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 1.0f, 2048);
                clipPlugins.processClipBlock (clipId, audio, 2048);
                expectEquals (format->lastCreated()->processCount(), 1,
                              "BYPASS clip-region callback is not entered");
                expectWithinAbsoluteError (audio.getSample (0, 2047), 1.0f, 1.0e-7f,
                                           "BYPASS clip-region tail remains transparent");
            }
        }

        beginTest ("failure-2b.plugin-isolation.full-stack-2048-all-hosted-paths");
        {
            FullStackResult referenceResult;
            {
                DAW::HostedPluginIsolationCore::ScopedOverride normalMode (false);
                auto reference = std::make_unique<FullStackHarness>();
                expect (reference->setup (*this, 2048), "plugin-free isolation reference setup");
                reference->addSendBusPath (*this, 2048, false);
                reference->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
                referenceResult = reference->render (2048, 1);
            }

            {
                DAW::HostedPluginIsolationCore::ScopedOverride normalMode (false);
                auto normal = std::make_unique<FullStackHarness>();
                expect (normal->setup (*this, 2048), "NORMAL hosted-path setup");
                normal->core.attachChainProbe (*this, normal->core.trackA, 2048,
                                               true, false, 0.5f);
                normal->addSendBusPath (*this, 2048, true, 0.5f);
                normal->core.setupClipRegion (*this, 2048, false, 0.5f);
                normal->attachMasterGainProbe (*this, 2048, 0.5f);
                normal->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
                const auto normalResult = normal->render (2048, 1);

                expectEquals (normal->core.chainProbes[normal->core.trackA->getID()]->processCount(), 1,
                              "NORMAL track insert executes once");
                expect (normal->sendBusProbe != nullptr && normal->sendBusProbe->processCount() == 1,
                        "NORMAL send destination bus insert executes once");
                expect (! normal->core.clipRegionProbes.empty()
                            && normal->core.clipRegionProbes.front()->processCount() == 1,
                        "NORMAL clip-region insert executes once");
                expect (normal->masterProbe != nullptr && normal->masterProbe->processCount() == 1,
                        "NORMAL master insert executes once");
                expect (normalResult.minimumBlockRms > 1.0e-6f && normalResult.allFinite,
                        "NORMAL 2048 full stack remains finite and nonzero");
            }

            {
                DAW::HostedPluginIsolationCore::ScopedOverride isolationMode (true);
                auto bypass = std::make_unique<FullStackHarness>();
                expect (bypass->setup (*this, 2048), "BYPASS hosted-path setup");
                bypass->core.attachChainProbe (*this, bypass->core.trackA, 2048,
                                               true, false, 0.5f);
                bypass->addSendBusPath (*this, 2048, true, 0.5f);
                bypass->core.setupClipRegion (*this, 2048, false, 0.5f);
                bypass->attachMasterGainProbe (*this, 2048, 0.5f);
                bypass->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
                const auto bypassResult = bypass->render (2048, 1);

                for (const auto& [trackId, probe] : bypass->core.chainProbes)
                {
                    juce::ignoreUnused (trackId);
                    expectEquals (probe->processCount(), 0,
                                  "BYPASS track/bus hosted processBlock count is zero");
                }
                for (auto* probe : bypass->core.clipRegionProbes)
                    expectEquals (probe->processCount(), 0,
                                  "BYPASS clip-region hosted processBlock count is zero");
                expect (bypass->masterProbe != nullptr && bypass->masterProbe->processCount() == 0,
                        "BYPASS master hosted processBlock count is zero");

                expectEquals ((int) bypassResult.controlRoomOutput.size(), 2048,
                              "BYPASS conserves exactly one 2048-frame callback");
                expectEquals (bypassResult.finalTransportPosition, (int64_t) 2048,
                              "BYPASS transport advances exactly 2048 samples");
                expect (bypassResult.minimumBlockRms > 1.0e-6f,
                        "BYPASS track output remains nonzero");
                expect (bypassResult.allFinite && bypassResult.activeSentinelOverwritten
                            && bypassResult.inactiveTailPreserved && bypassResult.secondHalfValid
                            && ! bypassResult.repeatedHalfDetected,
                        "BYPASS 2048 output is finite, complete, non-stale, and tail-safe");
                expect (rmsOf (bypassResult.engineOutput) > 1.0e-6f
                            && rmsOf (bypassResult.masterOutput) > 1.0e-6f
                            && rmsOf (bypassResult.controlRoomOutput) > 1.0e-6f,
                        "routing, master, and native control-room stages remain active");
                expect (rmsOf (bypassResult.controlRoomOutput)
                            < rmsOf (bypassResult.masterOutput) * 0.95f,
                        "native control-room monitor gain remains active during hosted bypass");
                expect (correlation (referenceResult.controlRoomOutput,
                                     bypassResult.controlRoomOutput) > 0.9999f,
                        "BYPASS output matches the plugin-free 2048 reference");
            }
        }

        beginTest ("failure-2b.device-adapter.chunk-scheduling-matrix");
        {
            const auto expectSchedule = [this] (
                int backendSamples,
                int preparedMaximum,
                std::initializer_list<std::pair<int, int>> expected,
                const juce::String& label)
            {
                std::vector<std::pair<int, int>> chunks;
                DAW::AudioDeviceBlockAdapterCore::forEachPreparedChunk (
                    backendSamples, preparedMaximum,
                    [&chunks] (int offset, int numSamples)
                    {
                        chunks.emplace_back (offset, numSamples);
                    });

                const std::vector<std::pair<int, int>> expectedChunks (expected);
                expectEquals ((int) chunks.size(), (int) expectedChunks.size(),
                              label + " chunk count");

                int conservedSamples = 0;
                int expectedOffset = 0;
                for (size_t i = 0; i < chunks.size(); ++i)
                {
                    expectEquals (chunks[i].first, expectedOffset,
                                  label + " contiguous offset " + juce::String ((int) i));
                    expect (chunks[i].second > 0 && chunks[i].second <= preparedMaximum,
                            label + " bounded positive chunk " + juce::String ((int) i));
                    if (i < expectedChunks.size())
                    {
                        expectEquals (chunks[i].first, expectedChunks[i].first,
                                      label + " expected offset " + juce::String ((int) i));
                        expectEquals (chunks[i].second, expectedChunks[i].second,
                                      label + " expected size " + juce::String ((int) i));
                    }
                    expectedOffset += chunks[i].second;
                    conservedSamples += chunks[i].second;
                }

                const int expectedConservation = backendSamples > 0 && preparedMaximum > 0
                    ? backendSamples : 0;
                expectEquals (conservedSamples, expectedConservation,
                              label + " sample conservation");
            };

            expectSchedule (480, 1024, {{ 0, 480 }}, "480/1024");
            expectSchedule (1025, 1024, {{ 0, 1024 }, { 1024, 1 }}, "1025/1024");
            expectSchedule (2048, 1024, {{ 0, 1024 }, { 1024, 1024 }}, "2048/1024");
            expectSchedule (2048, 2048, {{ 0, 2048 }}, "2048/2048");
            expectSchedule (3000, 1024,
                            {{ 0, 1024 }, { 1024, 1024 }, { 2048, 952 }},
                            "3000/1024");
            expectSchedule (8192, 1024,
                            {{ 0, 1024 }, { 1024, 1024 }, { 2048, 1024 }, { 3072, 1024 },
                             { 4096, 1024 }, { 5120, 1024 }, { 6144, 1024 }, { 7168, 1024 }},
                            "8192/1024");
            expectSchedule (0, 1024, {}, "zero backend samples");
            expectSchedule (-1, 1024, {}, "negative backend samples");
            expectSchedule (2048, 0, {}, "zero prepared maximum");
            expectSchedule (2048, -1, {}, "negative prepared maximum");
        }

        beginTest ("failure-2b.device-adapter.multichannel-input-output-offsets");
        {
            constexpr int backendSamples = 2048;
            constexpr int preparedMaximum = 1024;
            constexpr float outputSentinel = -12345.0f;
            std::array<std::vector<float>, 2> input;
            std::array<std::vector<float>, 2> output;
            std::array<std::vector<float>, 2> scratch;
            for (int ch = 0; ch < 2; ++ch)
            {
                input[(size_t) ch].resize (backendSamples);
                output[(size_t) ch].assign (backendSamples, outputSentinel);
                scratch[(size_t) ch].resize (preparedMaximum);
                for (int i = 0; i < backendSamples; ++i)
                    input[(size_t) ch][(size_t) i] = (ch == 0 ? 0.0f : 100000.0f) + (float) i;
            }

            int chunkIndex = 0;
            DAW::AudioDeviceBlockAdapterCore::forEachPreparedChunk (
                backendSamples, preparedMaximum,
                [this, &input, &output, &scratch, &chunkIndex]
                (int frameOffset, int chunkSamples)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        std::copy_n (input[(size_t) ch].data() + frameOffset,
                                     chunkSamples,
                                     scratch[(size_t) ch].data());
                        std::copy_n (scratch[(size_t) ch].data(),
                                     chunkSamples,
                                     output[(size_t) ch].data() + frameOffset);
                    }

                    if (chunkIndex == 1)
                    {
                        expectEquals (scratch[0][0], 1024.0f,
                                      "second chunk channel 0 starts at input sample 1024");
                        expectEquals (scratch[1][0], 101024.0f,
                                      "second chunk channel 1 starts at input sample 101024");
                        expectEquals (frameOffset, 1024,
                                      "second chunk writes beginning at output frame 1024");
                    }
                    ++chunkIndex;
                });

            expectEquals (chunkIndex, 2, "multichannel callback uses exactly two chunks");
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < backendSamples; ++i)
                    expectEquals (output[(size_t) ch][(size_t) i],
                                  input[(size_t) ch][(size_t) i],
                                  "channel/output sample conserved ch=" + juce::String (ch)
                                      + " i=" + juce::String (i));
        }

        beginTest ("failure-2b.device-adapter.runtime-2048-on-prepared-1024.full-stack-equivalence");
        {
            constexpr int backendCallbacks = 64;
            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 1024), "adapter reference setup");
            reference->attachTrackGainProbes (*this, 1024);
            reference->attachMasterGainProbe (*this, 1024, 0.5f);
            reference->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
            const auto expected = reference->render (1024, backendCallbacks * 2).controlRoomOutput;

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 1024), "adapter candidate setup");
            candidate->attachTrackGainProbes (*this, 1024);
            candidate->attachMasterGainProbe (*this, 1024, 0.5f);
            candidate->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
            const auto actual = candidate->renderThroughDeviceAdapter (2048, 1024, backendCallbacks);

            const float corr = correlation (expected, actual);
            logMessage ("#2B DEVICE-ADAPTER prepared=1024 runtime=2048 rms="
                        + juce::String (rmsOf (expected), 6) + "/"
                        + juce::String (rmsOf (actual), 6) + " corr="
                        + juce::String (corr, 8));
            expectEquals ((int) actual.size(), backendCallbacks * 2048,
                          "adapter publishes every backend frame");
            expect (corr > 0.9999f,
                    "chunked runtime-2048 output equals native prepared-1024 timeline");
            expect (candidate->masterProbe != nullptr, "adapter master probe exists");
            if (candidate->masterProbe != nullptr)
            {
                expectEquals (candidate->masterProbe->lastBlockSize(), 1024,
                              "adapter never sends 2048 to master plugin prepared for 1024");
                expectEquals (candidate->masterProbe->maximumBlockSizeSeen(), 1024,
                              "master plugin maximum process block stays within prepared maximum");
                expectEquals (candidate->masterProbe->processCount(), backendCallbacks * 2,
                              "adapter invokes master plugin for both bounded chunks");
                expectEquals (candidate->masterProbe->violationCount(), 0,
                              "adapter causes no master plugin maximum violation");
            }
            for (const auto& [trackId, probe] : candidate->core.chainProbes)
            {
                expectEquals (probe->lastBlockSize(), 1024,
                              "adapter never sends 2048 to track plugin prepared for 1024");
                expectEquals (probe->maximumBlockSizeSeen(), 1024,
                              "track plugin maximum process block stays within prepared maximum");
                expectEquals (probe->processCount(), backendCallbacks * 2,
                              "adapter invokes every track plugin for both chunks");
                expectEquals (probe->violationCount(), 0,
                              "adapter causes no track plugin maximum violation");
            }
        }

        beginTest ("failure-2b.device-adapter.fresh-2048-remains-single-2048-process-call");
        {
            constexpr int backendCallbacks = 32;
            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 2048), "fresh adapter reference setup");
            reference->attachTrackGainProbes (*this, 2048);
            reference->attachMasterGainProbe (*this, 2048, 0.5f);
            const auto expected = reference->render (2048, backendCallbacks).controlRoomOutput;

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 2048), "fresh adapter candidate setup");
            candidate->attachTrackGainProbes (*this, 2048);
            candidate->attachMasterGainProbe (*this, 2048, 0.5f);
            const auto actual = candidate->renderThroughDeviceAdapter (
                2048, 2048, backendCallbacks);

            expect (correlation (expected, actual) > 0.9999f,
                    "fresh prepared-2048 adapter output remains equivalent");
            expect (candidate->masterProbe != nullptr, "fresh 2048 master probe exists");
            if (candidate->masterProbe != nullptr)
            {
                expectEquals (candidate->masterProbe->processCount(), backendCallbacks,
                              "fresh 2048 master plugin runs once per backend callback");
                expectEquals (candidate->masterProbe->maximumBlockSizeSeen(), 2048,
                              "fresh 2048 master plugin receives one 2048 block");
            }
            for (const auto& [trackId, probe] : candidate->core.chainProbes)
            {
                expectEquals (probe->processCount(), backendCallbacks,
                              "fresh 2048 track plugin runs once per backend callback");
                expectEquals (probe->maximumBlockSizeSeen(), 2048,
                              "fresh 2048 track plugin receives one 2048 block");
                expectEquals (probe->violationCount(), 0,
                              "fresh 2048 track plugin has no maximum violation");
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // FAILURE #2B — full production playback stack, fresh-state control.
        // No plugins. Compare the same 450560-sample (~10.22 s) timeline as
        // 440 callbacks at 1024 and 220 callbacks at 2048.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("failure-2b.full-stack.no-plugins.fresh-1024-vs-2048.long-run");
        {
            constexpr int blocks2048 = 220;
            constexpr int blocks1024 = blocks2048 * 2;

            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 1024), "fresh 1024 full-stack setup");
            const auto r1024 = reference->render (1024, blocks1024);

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 2048), "fresh 2048 full-stack setup");
            const auto r2048 = candidate->render (2048, blocks2048);

            const float engineCorrelation = correlation (r1024.engineOutput, r2048.engineOutput);
            const float masterCorrelation = correlation (r1024.masterOutput, r2048.masterOutput);
            const float controlCorrelation = correlation (r1024.controlRoomOutput, r2048.controlRoomOutput);
            const float engineRms1024 = rmsOf (r1024.engineOutput);
            const float engineRms2048 = rmsOf (r2048.engineOutput);
            const float masterRms1024 = rmsOf (r1024.masterOutput);
            const float masterRms2048 = rmsOf (r2048.masterOutput);
            const float outputRms1024 = rmsOf (r1024.controlRoomOutput);
            const float outputRms2048 = rmsOf (r2048.controlRoomOutput);

            juce::String tones;
            const double frequencies[3] = { 440.0, 550.0, 660.0 };
            for (double frequency : frequencies)
            {
                const float a = measureTone (r1024.controlRoomOutput.data(),
                                             (int) r1024.controlRoomOutput.size(),
                                             Harness::kSr, frequency);
                const float b = measureTone (r2048.controlRoomOutput.data(),
                                             (int) r2048.controlRoomOutput.size(),
                                             Harness::kSr, frequency);
                tones += " " + juce::String (frequency, 0) + "Hz="
                    + juce::String (a, 5) + "/" + juce::String (b, 5);
                expect (a > 0.03f && b > 0.03f,
                        "all source/track frequencies survive 1024 and 2048");
                expect (std::abs (a - b) < 0.002f,
                        "per-frequency magnitude is block-size invariant");
            }

            logMessage ("#2B NO-PLUGIN rms engine=" + juce::String (engineRms1024, 6)
                        + "/" + juce::String (engineRms2048, 6)
                        + " master=" + juce::String (masterRms1024, 6)
                        + "/" + juce::String (masterRms2048, 6)
                        + " control/application/device=" + juce::String (outputRms1024, 6)
                        + "/" + juce::String (outputRms2048, 6)
                        + " corr=" + juce::String (engineCorrelation, 8)
                        + "/" + juce::String (masterCorrelation, 8)
                        + "/" + juce::String (controlCorrelation, 8)
                        + tones);
            logMessage ("#2B LONG-RUN 1024 minBlockRms=" + juce::String (r1024.minimumBlockRms, 6)
                        + " maxJump=" + juce::String (r1024.maximumAdjacentJump, 6)
                        + " p99Ms=" + juce::String (percentileMs (r1024.callbackMs, 0.99), 4)
                        + " maxMs=" + juce::String (*std::max_element (r1024.callbackMs.begin(), r1024.callbackMs.end()), 4));
            logMessage ("#2B LONG-RUN 2048 minBlockRms=" + juce::String (r2048.minimumBlockRms, 6)
                        + " maxJump=" + juce::String (r2048.maximumAdjacentJump, 6)
                        + " p99Ms=" + juce::String (percentileMs (r2048.callbackMs, 0.99), 4)
                        + " maxMs=" + juce::String (*std::max_element (r2048.callbackMs.begin(), r2048.callbackMs.end()), 4));

            expectEquals ((int) r1024.engineOutput.size(), (int) r2048.engineOutput.size(),
                          "same logical engine sample count");
            expectEquals ((int) r1024.controlRoomOutput.size(), (int) r2048.controlRoomOutput.size(),
                          "same logical final sample count");
            expectEquals (r1024.finalTransportPosition, r2048.finalTransportPosition,
                          "same final timeline position");
            expect (r1024.allFinite && r2048.allFinite, "all full-stack outputs are finite");
            expect (r1024.activeSentinelOverwritten && r2048.activeSentinelOverwritten,
                    "every active output sample overwrites the sentinel");
            expect (r1024.inactiveTailPreserved && r2048.inactiveTailPreserved,
                    "processing does not touch inactive backing-buffer capacity");
            expect (r1024.secondHalfValid && r2048.secondHalfValid,
                    "both halves of every callback contain valid audio");
            expect (! r1024.repeatedHalfDetected && ! r2048.repeatedHalfDetected,
                    "no callback duplicates its first half into its second half");
            expect (! r1024.repeatedCallbackDetected && ! r2048.repeatedCallbackDetected,
                    "no stale callback is repeated");
            expect (r1024.minimumBlockRms > 0.03f && r2048.minimumBlockRms > 0.03f,
                    "no sustained or periodic zero/corrupt block");
            expect (engineCorrelation > 0.9999f, "AudioEngine output equivalent at 1024 and 2048");
            expect (masterCorrelation > 0.9999f, "master output equivalent at 1024 and 2048");
            expect (controlCorrelation > 0.9999f, "control-room/final output equivalent at 1024 and 2048");
            expect (std::abs (engineRms1024 - engineRms2048) < 0.001f,
                    "engine RMS equivalent");
            expect (std::abs (masterRms1024 - masterRms2048) < 0.001f,
                    "master RMS equivalent");
            expect (std::abs (outputRms1024 - outputRms2048) < 0.001f,
                    "final RMS equivalent");
        }

        // Track-host layer: deterministic JUCE AudioPluginInstance objects are
        // hosted by the real PluginChainCore/PluginInstanceCore path.
        beginTest ("failure-2b.full-stack.mock-track-plugins.fresh-1024-vs-2048");
        {
            constexpr int blocks2048 = 64;
            constexpr int blocks1024 = blocks2048 * 2;

            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 1024), "track-plugin 1024 setup");
            reference->attachTrackGainProbes (*this, 1024);
            const auto r1024 = reference->render (1024, blocks1024);

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 2048), "track-plugin 2048 setup");
            candidate->attachTrackGainProbes (*this, 2048);
            const auto r2048 = candidate->render (2048, blocks2048);

            for (const auto& [trackId, probe] : reference->core.chainProbes)
            {
                expectEquals (probe->preparedBlockSize(), 1024, "track plugin prepared at 1024");
                expectEquals (probe->lastBlockSize(), 1024, "track plugin receives exactly 1024");
                expectEquals (probe->processCount(), blocks1024, "track plugin processes every 1024 callback");
                expectEquals (probe->violationCount(), 0, "no track plugin size violation at 1024");
            }
            for (const auto& [trackId, probe] : candidate->core.chainProbes)
            {
                expectEquals (probe->preparedBlockSize(), 2048, "track plugin prepared at 2048");
                expectEquals (probe->lastBlockSize(), 2048, "track plugin receives exactly 2048");
                expectEquals (probe->processCount(), blocks2048, "track plugin processes every 2048 callback");
                expectEquals (probe->violationCount(), 0, "no track plugin size violation at 2048");
            }

            const float corr = correlation (r1024.controlRoomOutput, r2048.controlRoomOutput);
            logMessage ("#2B TRACK-PLUGIN rms=" + juce::String (rmsOf (r1024.controlRoomOutput), 6)
                        + "/" + juce::String (rmsOf (r2048.controlRoomOutput), 6)
                        + " corr=" + juce::String (corr, 8));
            expect (corr > 0.9999f, "mock track-plugin output equivalent at 1024 and 2048");
            expect (r1024.secondHalfValid && r2048.secondHalfValid,
                    "track-plugin path writes both callback halves");
            expect (r1024.inactiveTailPreserved && r2048.inactiveTailPreserved,
                    "track-plugin path preserves inactive capacity");
        }

        // Master-host layer, isolated after the track-host layer passes.
        beginTest ("failure-2b.full-stack.mock-master-plugin.fresh-1024-vs-2048");
        {
            constexpr int blocks2048 = 64;
            constexpr int blocks1024 = blocks2048 * 2;

            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 1024), "master-plugin 1024 setup");
            reference->attachMasterGainProbe (*this, 1024, 0.5f);
            const auto r1024 = reference->render (1024, blocks1024);

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 2048), "master-plugin 2048 setup");
            candidate->attachMasterGainProbe (*this, 2048, 0.5f);
            const auto r2048 = candidate->render (2048, blocks2048);

            expect (reference->masterProbe != nullptr && candidate->masterProbe != nullptr,
                    "master probes exist");
            if (reference->masterProbe != nullptr && candidate->masterProbe != nullptr)
            {
                expectEquals (reference->masterProbe->preparedBlockSize(), 1024,
                              "master plugin prepared at 1024");
                expectEquals (reference->masterProbe->lastBlockSize(), 1024,
                              "master plugin receives exactly 1024");
                expectEquals (reference->masterProbe->processCount(), blocks1024,
                              "master plugin processes every 1024 callback");
                expectEquals (candidate->masterProbe->preparedBlockSize(), 2048,
                              "master plugin prepared at 2048");
                expectEquals (candidate->masterProbe->lastBlockSize(), 2048,
                              "master plugin receives exactly 2048");
                expectEquals (candidate->masterProbe->processCount(), blocks2048,
                              "master plugin processes every 2048 callback");
            }

            const float corr = correlation (r1024.controlRoomOutput, r2048.controlRoomOutput);
            logMessage ("#2B MASTER-PLUGIN rms=" + juce::String (rmsOf (r1024.controlRoomOutput), 6)
                        + "/" + juce::String (rmsOf (r2048.controlRoomOutput), 6)
                        + " corr=" + juce::String (corr, 8));
            expect (corr > 0.9999f, "mock master-plugin output equivalent at 1024 and 2048");
            expect (r1024.secondHalfValid && r2048.secondHalfValid,
                    "master-plugin path writes both callback halves");
        }

        // Control-room has no hosted-plugin chain in the current repository.
        // Exercise its real deterministic monitor-gain path rather than invent
        // a nonexistent API.
        beginTest ("failure-2b.full-stack.control-room-gain.fresh-1024-vs-2048");
        {
            constexpr int blocks2048 = 64;
            constexpr int blocks1024 = blocks2048 * 2;
            auto reference = std::make_unique<FullStackHarness>();
            expect (reference->setup (*this, 1024), "control-room 1024 setup");
            reference->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
            const auto r1024 = reference->render (1024, blocks1024);

            auto candidate = std::make_unique<FullStackHarness>();
            expect (candidate->setup (*this, 2048), "control-room 2048 setup");
            candidate->controlRoom.getState().monitorGain.store (0.5f, std::memory_order_relaxed);
            const auto r2048 = candidate->render (2048, blocks2048);

            const float corr = correlation (r1024.controlRoomOutput, r2048.controlRoomOutput);
            logMessage ("#2B CONTROL-ROOM rms=" + juce::String (rmsOf (r1024.controlRoomOutput), 6)
                        + "/" + juce::String (rmsOf (r2048.controlRoomOutput), 6)
                        + " corr=" + juce::String (corr, 8));
            expect (corr > 0.9999f, "control-room output equivalent at 1024 and 2048");
            expect (r1024.secondHalfValid && r2048.secondHalfValid,
                    "control-room path writes both callback halves");
        }

        // ══════════════════════════════════════════════════════════════════
        // LIFECYCLE GOOD: fresh 1024 prepare, then play.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("good.fresh-1024");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");

            h.engine.prepare (Harness::kSr, 1024);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> out;
            h.render (1024, 5, out);

            const float toneA = measureTone (out.data(), (int) out.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (out.data(), (int) out.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (out.data(), (int) out.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("GOOD  tones  A=440Hz:") + juce::String (toneA, 4)
                        + "  B=550Hz:" + juce::String (toneB, 4)
                        + "  C=660Hz:" + juce::String (toneC, 4));

            expect (toneA > 0.05f, "GOOD track A non-zero");
            expect (toneB > 0.05f, "GOOD track B non-zero");
            expect (toneC > 0.05f, "GOOD track C non-zero");
        }

        // ══════════════════════════════════════════════════════════════════
        // LIFECYCLE BAD: 480 prepare, play forward, releaseResources,
        // reprepare 1024, continue from the same logical position.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.runtime-reprepare-480-to-1024");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");

            h.engine.prepare (Harness::kSr, 480);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);   // advance 3840 samples into the clips

            const int64_t posBefore = (int64_t) h.transport.getPosition();
            logMessage (juce::String ("BAD  transport position before reprepare: ") + juce::String (posBefore));

            // ── The REAL production device-change lifecycle ──────────────
            h.engine.releaseResources();          // audioDeviceStopped path
            h.engine.prepare (Harness::kSr, 1024); // audioDeviceAboutToStart path
            // (transport is NOT stopped by the device change — matching
            //  production, where no transport_->stop() exists in the path)

            std::vector<float> post;
            h.render (1024, 3, post);   // continue from the same position

            const float toneA = measureTone (post.data(), (int) post.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (post.data(), (int) post.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (post.data(), (int) post.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("BAD   tones  A=440Hz:") + juce::String (toneA, 4)
                        + "  B=550Hz:" + juce::String (toneB, 4)
                        + "  C=660Hz:" + juce::String (toneC, 4)
                        + "  posAfter=" + juce::String ((int64_t) h.transport.getPosition()));

            expect (toneA > 0.05f, "BAD track A non-zero after reprepare");
            expect (toneB > 0.05f, "BAD track B non-zero after reprepare");
            expect (toneC > 0.05f, "BAD track C non-zero after reprepare");
        }

        // ══════════════════════════════════════════════════════════════════
        // FAILURE #2A-CRASH — exact HOT lifecycle.
        // PLAY -> multiple callbacks -> STOP -> immediate device release ->
        // prepare(1024).  There is deliberately no callback between STOP and
        // release, matching the authoritative real-user sequence.
        //
        // Two independent lifecycle contracts are asserted:
        //   1. A prepared/processed plugin must be released before reprepare.
        //   2. A stopped engine must not synthesize the old session's stop-fade
        //      callback after the device execution lifetime was destroyed.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("failure-a.hot-play-stop-release-480-prepare-1024");
        {
            Harness h;
            expect (h.setup (*this), "hot lifecycle harness setup");
            h.engine.prepare (Harness::kSr, 480);
            auto* probe = h.attachChainProbe (*this, h.trackA, 480, false, true);
            expect (probe != nullptr, "hot lifecycle probe exists");
            const auto coldState = h.engine.getDeviceLifecycleSnapshotForTesting();

            h.transport.setPosition (0);
            h.transport.play();
            std::vector<float> playing;
            h.render (480, 8, playing);
            expect (rmsOf (playing) > 0.05f, "HOT project rendered before stop");
            const int processCountBeforeStop = probe != nullptr ? probe->processCount() : 0;
            expect (processCountBeforeStop >= 8, "HOT plugin processed real callbacks");

            h.transport.stop();
            expect (! h.transport.isPlaying(), "transport stopped before device release");
            const auto hotState = h.engine.getDeviceLifecycleSnapshotForTesting();
            logMessage (juce::String ("COLD/HOT state process=")
                        + juce::String ((int64_t) coldState.processCounter) + "/"
                        + juce::String ((int64_t) hotState.processCounter)
                        + " engineWasPlaying=" + juce::String ((int) coldState.engineWasPlaying)
                        + "/" + juce::String ((int) hotState.engineWasPlaying)
                        + " audioCache=" + juce::String ((int) coldState.clipAudioCacheCount)
                        + "/" + juce::String ((int) hotState.clipAudioCacheCount)
                        + " renderCursors=" + juce::String ((int) coldState.clipRenderCursorCount)
                        + "/" + juce::String ((int) hotState.clipRenderCursorCount)
                        + " trackRamps=" + juce::String ((int) coldState.trackVolumeRampCount)
                        + "/" + juce::String ((int) hotState.trackVolumeRampCount)
                        + " muteFades=" + juce::String ((int) coldState.trackMuteFadeCount)
                        + "/" + juce::String ((int) hotState.trackMuteFadeCount)
                        + " routeRamps=" + juce::String ((int) coldState.connectionRampCount)
                        + "/" + juce::String ((int) hotState.connectionRampCount));
            expect (! coldState.engineWasPlaying && hotState.engineWasPlaying,
                    "only HOT state retains the unconsumed play-to-stop engine edge");
            expectEquals ((int) coldState.clipAudioCacheCount, 0,
                          "COLD state has not resolved clip audio in a callback");
            expect (hotState.clipAudioCacheCount >= 3,
                    "HOT state resolved all clip audio snapshots");
            expectEquals ((int) coldState.clipRenderCursorCount, 0,
                          "COLD state has no render cursors");
            expectEquals ((int) hotState.clipRenderCursorCount, 0,
                          "identity render path retains no per-clip cursor state");

            h.reprepare (Harness::kSr, 1024);

            if (probe != nullptr)
            {
                logMessage (juce::String ("HOT lifecycle release=") + juce::String (probe->releaseCount())
                            + " hotPrepareWithoutRelease=" + juce::String (probe->hotPrepareWithoutReleaseCount())
                            + " processBeforeStop=" + juce::String (processCountBeforeStop));
                expectEquals (probe->hotPrepareWithoutReleaseCount(), 0,
                              "processed plugin released before 1024 reprepare");
                expect (probe->releaseCount() >= 1,
                        "device release reaches the processed track plugin");
            }

            std::vector<float> stoppedBlock;
            h.renderOne (1024, stoppedBlock); // first callback after device start
            const int processCountAfterRestart = probe != nullptr ? probe->processCount() : 0;
            logMessage (juce::String ("HOT first stopped callback rms=")
                        + juce::String (rmsOf (stoppedBlock), 8)
                        + " processAfterRestart=" + juce::String (processCountAfterRestart)
                        + " pluginInputRms="
                        + juce::String (probe != nullptr ? probe->lastInputRms() : 0.0f, 8));
            expectEquals (processCountAfterRestart, processCountBeforeStop + 1,
                          "stopped callback may service plugin tails exactly once");
            expect (probe != nullptr && probe->lastInputRms() < 0.000001f,
                    "stopped callback does not feed stale playback audio to plugins");
            expect (rmsOf (stoppedBlock) < 0.000001f,
                    "first stopped callback after reprepare is silent");
        }

        // ══════════════════════════════════════════════════════════════════
        // FAILURE #2A-SILENCE — exact COLD lifecycle.
        // prepare(480), NEVER PLAY, release, prepare(1024), first PLAY.
        // A strict lifecycle probe is placed on the selected track only.  Its
        // input is the exact post-clip/pre-track-FX boundary and its output is
        // the exact post-track-FX boundary. Distinct frequencies identify each
        // track downstream at routing/master output.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("failure-b.cold-release-480-prepare-1024-first-play.counts-1-2-3-7");
        {
            const int counts[] = { 1, 2, 3, 7 };
            for (int n : counts)
            {
                Harness h;
                expect (h.setupN (*this, n), "cold lifecycle setupN");
                h.engine.prepare (Harness::kSr, 480);
                auto* probe = h.attachChainProbe (*this, h.xtracks[0], 480, false, true);
                expect (probe != nullptr, "cold lifecycle probe exists");

                const auto source = h.audioFiles.getCachedAudioSnapshot (h.xclips[0]->getID());
                const auto& sourceBuffer = source != nullptr
                    ? source->getBufferRef() : emptyAudioBuffer();
                const float sourceRms = sourceBuffer.getNumSamples() > 0
                    ? sourceBuffer.getRMSLevel (0, 0, sourceBuffer.getNumSamples())
                    : 0.0f;
                expect (sourceRms > 0.05f, "SOURCE READ backing audio is non-zero");
                expectEquals (probe != nullptr ? probe->processCount() : -1, 0,
                              "COLD project has never processed playback");

                h.reprepare (Harness::kSr, 1024);
                if (probe != nullptr)
                {
                    expectEquals (probe->coldPrepareWithoutReleaseCount(), 0,
                                  "never-played plugin released before 1024 reprepare");
                    expect (probe->releaseCount() >= 1,
                            "device release reaches the never-played track plugin");
                }

                h.transport.setPosition (0);
                h.transport.play();
                std::vector<float> post;
                h.render (1024, 3, post);

                const float firstTone = measureTone (post.data(), (int) post.size(),
                                                     Harness::kSr, 440.0);
                const double lastFreq = 440.0 + 110.0 * (n - 1);
                const float lastTone = measureTone (post.data(), (int) post.size(),
                                                    Harness::kSr, lastFreq);
                const float preFxRms = probe != nullptr ? probe->lastInputRms() : 0.0f;
                const float postFxRms = probe != nullptr ? probe->lastOutputRms() : 0.0f;

                logMessage (juce::String ("COLD n=") + juce::String (n)
                            + " source=" + juce::String (sourceRms, 4)
                            + " clip/track/preFX=" + juce::String (preFxRms, 4)
                            + " postFX=" + juce::String (postFxRms, 4)
                            + " route/master.first=" + juce::String (firstTone, 4)
                            + " route/master.last=" + juce::String (lastTone, 4));

                expect (preFxRms > 0.05f,
                        "CLIP RENDER / CLIP REGION / TRACK BUFFER non-zero before track FX");
                expect (postFxRms > 0.05f, "POST TRACK FX non-zero");
                expect (firstTone > 0.03f,
                        "ROUTING SOURCE/DESTINATION/MASTER first-track contribution non-zero");
                if (n > 1)
                    expect (lastTone > 0.03f, "last track remains independently audible");
            }
        }

        beginTest ("failure-b.silence-follows-plugin-bearing-track-not-first-index");
        {
            Harness h;
            expect (h.setupN (*this, 3), "cold middle-track setup");
            h.engine.prepare (Harness::kSr, 480);
            auto* probe = h.attachChainProbe (*this, h.xtracks[1], 480, false, true);
            h.reprepare (Harness::kSr, 1024);
            h.transport.setPosition (0);
            h.transport.play();
            std::vector<float> out;
            h.render (1024, 3, out);

            const float a = measureTone (out.data(), (int) out.size(), Harness::kSr, 440.0);
            const float b = measureTone (out.data(), (int) out.size(), Harness::kSr, 550.0);
            const float c = measureTone (out.data(), (int) out.size(), Harness::kSr, 660.0);
            logMessage (juce::String ("COLD middle plugin tones A:") + juce::String (a, 4)
                        + " B:" + juce::String (b, 4) + " C:" + juce::String (c, 4)
                        + " preFX:" + juce::String (probe != nullptr ? probe->lastInputRms() : 0.0f, 4)
                        + " postFX:" + juce::String (probe != nullptr ? probe->lastOutputRms() : 0.0f, 4));
            expect (a > 0.10f, "first index audible when its plugin lifecycle is valid");
            expect (b > 0.10f, "middle plugin-bearing track audible after cold reprepare");
            expect (c > 0.10f, "last index audible");
        }

        // ══════════════════════════════════════════════════════════════════
        // Additional buffer transitions: track survival only.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("transitions.480-512-1024-512");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");

            const int seq[] = { 480, 512, 1024, 512 };
            h.engine.prepare (Harness::kSr, seq[0]);
            h.transport.setPosition (0);
            h.transport.play();

            for (int s = 1; s < 4; ++s)
            {
                std::vector<float> tmp;
                h.render (seq[s - 1], 2, tmp);
                h.engine.releaseResources();
                h.engine.prepare (Harness::kSr, seq[s]);
            }

            std::vector<float> out;
            h.render (512, 3, out);

            const float toneA = measureTone (out.data(), (int) out.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (out.data(), (int) out.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (out.data(), (int) out.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("TRANS tones  A:") + juce::String (toneA, 4)
                        + "  B:" + juce::String (toneB, 4)
                        + "  C:" + juce::String (toneC, 4));

            expect (toneA > 0.05f, "transition track A non-zero");
            expect (toneB > 0.05f, "transition track B non-zero");
            expect (toneC > 0.05f, "transition track C non-zero");
        }

        // ══════════════════════════════════════════════════════════════════
        // MOCK PLUGIN CHAIN — ALL TRACKS (Theory #4).
        // A passthrough insert on every track. The chain is re-prepared with
        // the new block size by the production prepareRenderGraph equivalent.
        // Silence-on-violation mode turns any host block-size contract breach
        // into deterministic track silence.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.chain-all.gain-probe");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");
            h.engine.prepare (Harness::kSr, 480);
            h.setupChains (*this, true, 480, true);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);
            const float preRms = rmsOf (pre);

            h.reprepare (Harness::kSr, 1024);

            std::vector<float> post;
            h.render (1024, 3, post);

            const float toneA = measureTone (post.data(), (int) post.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (post.data(), (int) post.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (post.data(), (int) post.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("CHAIN-ALL tones  A:") + juce::String (toneA, 4)
                        + "  B:" + juce::String (toneB, 4)
                        + "  C:" + juce::String (toneC, 4)
                        + "  preRms:" + juce::String (preRms, 4));

            for (const auto& [tid, probe] : h.chainProbes)
            {
                logMessage (juce::String ("CHAIN-ALL probe  prepared:") + juce::String (probe->preparedBlockSize())
                            + "  lastBlock:" + juce::String (probe->lastBlockSize())
                            + "  violations:" + juce::String (probe->violationCount())
                            + "  lastRms:" + juce::String (probe->lastInputRms(), 4));
                expectEquals (probe->lastBlockSize(), 1024, "chain receives the new device block size");
                expectEquals (probe->violationCount(), 0, "no block-size contract violation");
                expect (probe->lastInputRms() > 0.05f, "plugin input non-zero after reprepare");
            }

            expect (toneA > 0.05f, "chain-all track A non-zero after reprepare");
            expect (toneB > 0.05f, "chain-all track B non-zero after reprepare");
            expect (toneC > 0.05f, "chain-all track C non-zero after reprepare");
        }

        // ══════════════════════════════════════════════════════════════════
        // MOCK PLUGIN CHAIN — TRACK 1 ONLY.
        // Tests whether the failure follows the first-created track/chain.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.chain-track1.gain-probe");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");
            h.engine.prepare (Harness::kSr, 480);
            h.setupChains (*this, false, 480, true);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);

            h.reprepare (Harness::kSr, 1024);

            std::vector<float> post;
            h.render (1024, 3, post);

            const float toneA = measureTone (post.data(), (int) post.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (post.data(), (int) post.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (post.data(), (int) post.size(), Harness::kSr, 660.0);

            const auto it = h.chainProbes.find (h.trackA->getID());
            const GainProbePlugin* probe = it != h.chainProbes.end() ? it->second : nullptr;

            logMessage (juce::String ("CHAIN-T1 tones  A:") + juce::String (toneA, 4)
                        + "  B:" + juce::String (toneB, 4)
                        + "  C:" + juce::String (toneC, 4));

            if (probe != nullptr)
            {
                logMessage (juce::String ("CHAIN-T1 probe  prepared:") + juce::String (probe->preparedBlockSize())
                            + "  lastBlock:" + juce::String (probe->lastBlockSize())
                            + "  violations:" + juce::String (probe->violationCount())
                            + "  lastRms:" + juce::String (probe->lastInputRms(), 4));
                expectEquals (probe->lastBlockSize(), 1024, "track-1 chain receives the new device block size");
                expectEquals (probe->violationCount(), 0, "no block-size contract violation");
                expect (probe->lastInputRms() > 0.05f, "track-1 plugin input non-zero after reprepare");
            }

            expect (toneA > 0.05f, "chain-track1 track A non-zero after reprepare");
            expect (toneB > 0.05f, "chain-track1 track B non-zero after reprepare");
            expect (toneC > 0.05f, "chain-track1 track C non-zero after reprepare");
        }

        // A device buffer Apply is a cold device lifecycle, but ordinary
        // playback through a track insert must still be continuous on the
        // first 2048-sample callback after the device resumes.
        beginTest ("device.reprepare-2048.normal-clip-track-plugin-continuity");
        {
            Harness h;
            expect (h.setupN (*this, 1), "setupN");
            h.engine.prepare (Harness::kSr, 480);
            auto* probe = h.attachChainProbe (*this, h.xtracks[0], 480, true, true);
            expect (probe != nullptr, "track probe exists");

            h.transport.setPosition (0);
            h.transport.play();
            std::vector<float> prior;
            for (int b = 0; b < 8; ++b)
                h.renderOne (480, prior);
            const float beforeApplyRms = rmsOf (prior);

            h.reprepare (Harness::kSr, 2048);
            std::vector<float> firstAfterApply;
            std::vector<float> continuousAfterApply;
            for (int b = 0; b < 3; ++b)
            {
                h.renderOne (2048, firstAfterApply);
                continuousAfterApply.insert (continuousAfterApply.end(),
                                             firstAfterApply.begin(), firstAfterApply.end());
            }

            float maxAdjacentJump = 0.0f;
            for (size_t i = 1; i < continuousAfterApply.size(); ++i)
                maxAdjacentJump = juce::jmax (maxAdjacentJump,
                    std::abs (continuousAfterApply[i] - continuousAfterApply[i - 1]));
            const float outputRms = rmsOf (firstAfterApply);

            logMessage (juce::String ("REPREPARE-2048 normal clip RMS=")
                        + juce::String (outputRms, 6)
                        + " beforeApplyRms=" + juce::String (beforeApplyRms, 6)
                        + " maxPostApplyJump=" + juce::String (maxAdjacentJump, 6)
                        + " pluginBlock=" + juce::String (probe != nullptr ? probe->lastBlockSize() : 0)
                        + " pluginInputRms=" + juce::String (probe != nullptr ? probe->lastInputRms() : 0.0f, 6));

            expect (outputRms > 0.05f, "normal clip remains audible on first 2048 callback");
            expect (beforeApplyRms > 0.05f, "normal clip is audible before the device reprepare");
            expect (probe != nullptr && probe->lastBlockSize() == 2048,
                    "track plugin receives the applied 2048-sample block");
            expect (probe != nullptr && probe->violationCount() == 0,
                    "track plugin has no prepared-block contract violation");
            expect (probe != nullptr && probe->lastInputRms() > 0.05f,
                    "track plugin receives non-zero audio on first 2048 callback");
            expect (maxAdjacentJump < 0.08f,
                    "normal clip has no hard discontinuity within the resumed 2048 stream");
        }

        // Match the panel workflow more closely: the project is stopped when
        // its device closes and prepares at 2048, then the user presses Play
        // immediately after Apply completes.
        beginTest ("device.apply-2048.stopped-project-immediate-play");
        {
            Harness h;
            expect (h.setupN (*this, 1), "setupN");
            h.engine.prepare (Harness::kSr, 480);
            auto* probe = h.attachChainProbe (*this, h.xtracks[0], 480, true, true);
            expect (probe != nullptr, "track probe exists");
            expect (! h.transport.isPlaying(), "transport stays stopped during the device Apply");

            h.reprepare (Harness::kSr, 2048);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> firstAfterPlay;
            std::vector<float> continuousAfterPlay;
            for (int b = 0; b < 3; ++b)
            {
                h.renderOne (2048, firstAfterPlay);
                continuousAfterPlay.insert (continuousAfterPlay.end(),
                                            firstAfterPlay.begin(), firstAfterPlay.end());
            }

            float maxAdjacentJump = 0.0f;
            for (size_t i = 1; i < continuousAfterPlay.size(); ++i)
                maxAdjacentJump = juce::jmax (maxAdjacentJump,
                    std::abs (continuousAfterPlay[i] - continuousAfterPlay[i - 1]));
            const float firstBlockRms = rmsOf (std::vector<float>(continuousAfterPlay.begin(),
                                                                  continuousAfterPlay.begin() + 2048));

            logMessage (juce::String ("APPLY-2048 stopped->play firstBlockRms=")
                        + juce::String (firstBlockRms, 6)
                        + " maxPostApplyJump=" + juce::String (maxAdjacentJump, 6)
                        + " pluginBlock=" + juce::String (probe != nullptr ? probe->lastBlockSize() : 0)
                        + " pluginInputRms=" + juce::String (probe != nullptr ? probe->lastInputRms() : 0.0f, 6));

            expect (firstBlockRms > 0.05f, "normal clip is audible in the first block after Play");
            expect (probe != nullptr && probe->lastBlockSize() == 2048,
                    "track plugin receives the 2048-sample block after Play");
            expect (probe != nullptr && probe->violationCount() == 0,
                    "track plugin has no block-size or lifecycle violation");
            expect (probe != nullptr && probe->lastInputRms() > 0.05f,
                    "track plugin receives audio immediately after Play");
            expect (maxAdjacentJump < 0.08f,
                    "normal clip has no hard discontinuity after Apply then Play");
        }

        // ══════════════════════════════════════════════════════════════════
        // STRETCH — CLIP 1 (Critical Theory #1: clip DSP continuity after
        // reprepare). Clip A is time-stretched 1.5x. Per-block profile after
        // the reprepare shows exactly how many blocks the reset DSP core
        // needs before the track is audible again.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.stretch.clip1.block-profile");
        {
            // Single track only: the master output IS the stretched clip, so
            // master RMS is a direct per-clip survival measurement.
            Harness h;
            expect (h.setupN (*this, 1), "setupN");
            h.xclips[0]->setTimeStretch (1.5f);

            h.engine.prepare (Harness::kSr, 480);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);
            const float preRms = rmsOf (pre);
            logMessage (juce::String ("STRETCH-T1 pre-reprepare RMS: ") + juce::String (preRms, 4));

            h.reprepare (Harness::kSr, 1024);

            // Per-block profile of the first 8 blocks after reprepare.
            float rms[8] = { 0.f };
            for (int b = 0; b < 8; ++b)
            {
                std::vector<float> blk;
                h.renderOne (1024, blk);
                rms[b] = rmsOf (blk);
            }

            juce::String profile = "STRETCH-T1 post-reprepare blocks: ";
            for (int b = 0; b < 8; ++b)
                profile += juce::String (b) + ":" + juce::String (rms[b], 4) + " ";
            logMessage (profile);

            expect (rms[0] > 0.05f, "stretch clip audible in FIRST block after reprepare");
            expect (rms[7] > 0.05f, "stretch clip audible by block 7 after reprepare");
        }

        // Diagnostic 2048 block profile for the stateful stretch path after
        // a device restart. Keep the startup and recovery values in evidence;
        // passing this recovery check does not claim that startup latency is
        // inaudible or matches the user's reported project.
        beginTest ("bad.stretch.clip1.reprepare-2048.block-profile");
        {
            Harness h;
            expect (h.setupN (*this, 1), "setupN");
            h.xclips[0]->setTimeStretch (1.5f);

            h.engine.prepare (Harness::kSr, 480);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            for (int b = 0; b < 32; ++b)
                h.renderOne (480, pre);
            const float preRms = rmsOf (pre);
            logMessage (juce::String ("STRETCH-2048 last pre-reprepare block RMS: ") + juce::String (preRms, 4));

            h.reprepare (Harness::kSr, 2048);

            float rms[8] = { 0.f };
            float maxAdjacentJump = 0.f;
            float previous = 0.f;
            for (int b = 0; b < 8; ++b)
            {
                std::vector<float> blk;
                h.renderOne (2048, blk);
                rms[b] = rmsOf (blk);
                for (float sample : blk)
                {
                    maxAdjacentJump = juce::jmax (maxAdjacentJump, std::abs (sample - previous));
                    previous = sample;
                }
            }

            juce::String profile = "STRETCH-2048 post-reprepare blocks: ";
            for (int b = 0; b < 8; ++b)
                profile += juce::String (b) + ":" + juce::String (rms[b], 4) + " ";
            logMessage (profile + "maxAdjacentJump=" + juce::String (maxAdjacentJump, 4));

            expect (rms[0] > 0.05f, "stretch clip is audible in the first 2048 block after reprepare");
            expect (rms[7] > 0.05f, "stretch clip remains audible by block 7 after reprepare");
            expect (maxAdjacentJump < 0.35f, "2048 reprepare has no hard zipper-sized sample jump");
        }

        // ══════════════════════════════════════════════════════════════════
        // STRETCH — ALL TRACKS (BAD lifecycle).
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.stretch.all.survival");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");
            h.clipA->setTimeStretch (1.5f);
            h.clipB->setTimeStretch (0.75f);
            h.clipC->setTimeStretch (1.25f);

            h.engine.prepare (Harness::kSr, 480);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);

            h.reprepare (Harness::kSr, 1024);

            std::vector<float> post;
            h.render (1024, 8, post);

            const float preRms = rmsOf (pre);
            const float postRms = rmsOf (post);

            logMessage (juce::String ("STRETCH-ALL preRms:") + juce::String (preRms, 4)
                        + "  postRms:" + juce::String (postRms, 4));

            expect (postRms > 0.1f, "stretch-all master non-zero after reprepare");
            expect (postRms > preRms * 0.5f, "stretch-all no large level drop after reprepare");
        }

        // ══════════════════════════════════════════════════════════════════
        // STRETCH — GOOD lifecycle control (fresh 1024 -> play).
        // ══════════════════════════════════════════════════════════════════
        beginTest ("good.stretch.fresh-1024");
        {
            Harness h;
            expect (h.setupN (*this, 1), "setupN");
            h.xclips[0]->setTimeStretch (1.5f);

            h.engine.prepare (Harness::kSr, 1024);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> out;
            h.render (1024, 8, out);

            const float rms = rmsOf (out);
            logMessage (juce::String ("GOOD-STRETCH rms: ") + juce::String (rms, 4));
            expect (rms > 0.05f, "good-stretch single clip non-zero");
        }

        // ══════════════════════════════════════════════════════════════════
        // CLIP-REGION PLUGIN — CLIP A ONLY.
        // The per-clip insert path (AudioEngine 2563-2579) is the last
        // remaining per-track layer that can silence an individual clip.
        // The internal JUCE Gain insert must keep passing audio after the
        // reprepare.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.clipregion.clipA.gain");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");
            h.engine.prepare (Harness::kSr, 480);
            h.setupClipRegion (*this, 480, false);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);
            const float preToneA = measureTone (pre.data(), (int) pre.size(), Harness::kSr, 440.0);

            h.reprepare (Harness::kSr, 1024);

            std::vector<float> post;
            h.render (1024, 3, post);

            const float toneA = measureTone (post.data(), (int) post.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (post.data(), (int) post.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (post.data(), (int) post.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("CLIPREG-A tones  preA:") + juce::String (preToneA, 4)
                        + "  A:" + juce::String (toneA, 4)
                        + "  B:" + juce::String (toneB, 4)
                        + "  C:" + juce::String (toneC, 4));

            for (const auto* probe : h.clipRegionProbes)
            {
                logMessage (juce::String ("CLIPREG-A probe  prepared:") + juce::String (probe->preparedBlockSize())
                            + "  lastBlock:" + juce::String (probe->lastBlockSize())
                            + "  violations:" + juce::String (probe->violationCount())
                            + "  lastRms:" + juce::String (probe->lastInputRms(), 4));
            }

            expect (toneA > 0.05f, "clipregion clipA non-zero after reprepare");
            expect (toneB > 0.05f, "clipregion track B non-zero after reprepare");
            expect (toneC > 0.05f, "clipregion track C non-zero after reprepare");
        }

        // ══════════════════════════════════════════════════════════════════
        // CLIP-REGION PLUGIN — ALL CLIPS.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("bad.clipregion.all.gain");
        {
            Harness h;
            expect (h.setup (*this), "harness setup");
            h.engine.prepare (Harness::kSr, 480);
            h.setupClipRegion (*this, 480, true);
            h.transport.setPosition (0);
            h.transport.play();

            std::vector<float> pre;
            h.render (480, 8, pre);

            h.reprepare (Harness::kSr, 1024);

            std::vector<float> post;
            h.render (1024, 3, post);

            const float toneA = measureTone (post.data(), (int) post.size(), Harness::kSr, 440.0);
            const float toneB = measureTone (post.data(), (int) post.size(), Harness::kSr, 550.0);
            const float toneC = measureTone (post.data(), (int) post.size(), Harness::kSr, 660.0);

            logMessage (juce::String ("CLIPREG-ALL tones  A:") + juce::String (toneA, 4)
                        + "  B:" + juce::String (toneB, 4)
                        + "  C:" + juce::String (toneC, 4));

            for (const auto* probe : h.clipRegionProbes)
            {
                logMessage (juce::String ("CLIPREG-ALL probe  prepared:") + juce::String (probe->preparedBlockSize())
                            + "  lastBlock:" + juce::String (probe->lastBlockSize())
                            + "  violations:" + juce::String (probe->violationCount())
                            + "  lastRms:" + juce::String (probe->lastInputRms(), 4));
            }

            expect (toneA > 0.05f, "clipregion-all track A non-zero after reprepare");
            expect (toneB > 0.05f, "clipregion-all track B non-zero after reprepare");
            expect (toneC > 0.05f, "clipregion-all track C non-zero after reprepare");
        }

        // ══════════════════════════════════════════════════════════════════
        // TRACK-COUNT SWEEP (Theory #3: index/id/first-element assumptions).
        // BAD lifecycle with 1, 2 and 7 tracks.
        // ══════════════════════════════════════════════════════════════════
        beginTest ("counts.1-2-7");
        {
            const int counts[3] = { 1, 2, 7 };
            for (int c = 0; c < 3; ++c)
            {
                const int n = counts[c];
                Harness h;
                expect (h.setupN (*this, n), "setupN");
                h.engine.prepare (Harness::kSr, 480);
                h.transport.setPosition (0);
                h.transport.play();

                std::vector<float> pre;
                h.render (480, 8, pre);

                h.engine.releaseResources();
                h.engine.prepare (Harness::kSr, 1024);

                std::vector<float> post;
                h.render (1024, 3, post);

                const double firstFreq = 440.0;
                const double lastFreq  = 440.0 + 110.0 * (n - 1);
                const float firstTone = measureTone (post.data(), (int) post.size(), Harness::kSr, firstFreq);
                const float lastTone  = measureTone (post.data(), (int) post.size(), Harness::kSr, lastFreq);

                logMessage (juce::String ("COUNTS n=") + juce::String (n)
                            + "  first(" + juce::String (firstFreq) + "Hz):" + juce::String (firstTone, 4)
                            + "  last(" + juce::String (lastFreq) + "Hz):" + juce::String (lastTone, 4));

                expect (firstTone > 0.03f, "count sweep: first track non-zero after reprepare");
                if (n > 1)
                    expect (lastTone > 0.03f, "count sweep: last track non-zero after reprepare");
            }
        }
    }

private:
    static const juce::AudioBuffer<float>& emptyAudioBuffer()
    {
        static const juce::AudioBuffer<float> empty;
        return empty;
    }

    static float rmsOf (const std::vector<float>& data)
    {
        if (data.empty()) return 0.0f;
        double sum = 0.0;
        for (float v : data)
            sum += (double) v * v;
        return (float) std::sqrt (sum / (double) data.size());
    }

    // Adapter to allow render() without collecting output.
    static void discard (std::vector<float>&) {}
};

/**
    @test    track.project-replacement.reprepare.v1
    @verify  A callback-held plugin-chain snapshot from the previous project is
             retired on the drained control-plane prepare boundary, never by a
             later realtime callback after New -> Open -> 480 -> 2048.
*/
class ProjectReplacementReprepareTests final : public juce::UnitTest
{
public:
    ProjectReplacementReprepareTests()
        : juce::UnitTest ("track.project-replacement.reprepare.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("previous project chain ownership ends before the first 2048 callback");

        TrackReprepareHarnessTests::Harness h;
        expect (h.setup (*this), "harness setup");
        h.engine.prepare (TrackReprepareHarnessTests::Harness::kSr, 480);

        h.attachChainProbe (*this, h.trackA, 480, false);
        const auto previousTrackId = h.trackA->getID();
        std::weak_ptr<DAW::PluginChainCore> previousProjectChain = h.chains.at (previousTrackId);

        // A normal stopped device callback adopts project A's immutable chain
        // map even though transport has never played.
        h.transport.stop();
        std::vector<float> stoppedOutput;
        h.renderOne (480, stoppedOutput);
        expect (! previousProjectChain.expired(),
                "project A chain must be owned by the adopted callback snapshot");

        // Production-equivalent New Project publication. ApplicationCore has
        // already drained callbacks when endProjectStateRestore(true) invokes
        // AudioEngine::prepare at this boundary.
        h.chains.clear();
        h.chainProbes.clear();
        h.publishChains();
        h.engine.prepare (TrackReprepareHarnessTests::Harness::kSr, 480);

        // Production-equivalent Open Project publication and terminal prepare,
        // followed by the real engine-level 480 -> 2048 device lifecycle.
        auto targetProjectChain = std::make_shared<DAW::PluginChainCore>();
        targetProjectChain->prepare (TrackReprepareHarnessTests::Harness::kSr, 480);
        h.chains[previousTrackId] = targetProjectChain;
        h.publishChains();
        h.engine.prepare (TrackReprepareHarnessTests::Harness::kSr, 480);
        h.reprepare (TrackReprepareHarnessTests::Harness::kSr, 2048);

        expect (previousProjectChain.expired(),
                "project A chain must be reclaimed on a drained control-plane boundary, not a later callback");

        // Diagnostic confirmation: before the fix this callback performs the
        // forbidden final release of project A's chain snapshot.
        h.renderOne (2048, stoppedOutput);
        expect (previousProjectChain.expired(),
                "the first 2048 callback must not own or reclaim project A state");
    }
};

static TrackReprepareHarnessTests trackReprepareHarnessTests;
static ProjectReplacementReprepareTests projectReplacementReprepareTests;

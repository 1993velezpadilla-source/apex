#include <JuceHeader.h>
#include "G10TestUtils.h"
#include "../../../Source/G10UI/G10SpectrumAnalyzerComponent.h"
#include "../../../Source/G10UI/G10AnalyzerTimingProbe.h"
#include <cmath>

// ============================================================================
// G10AnalyzerTests — the analyzer is a MEASUREMENT INSTRUMENT, not decoration.
//
// The analyzer must report the true calibrated spectrum of the EXACT samples
// the host receives (post-EQ, post-analog, post-quality, post-trim,
// post-bypass), with real frequency positions and real dBFS amplitudes:
//
//   A. silence            -> every bar at the floor (no fake movement)
//   B. 100 Hz sine        -> peak lands at 100 Hz
//   C. 1 kHz sine         -> peak lands at 1 kHz
//   D. 10 kHz sine        -> peak lands at 10 kHz
//   E. amplitude scaling  -> -6/-12/-24/-48 dBFS track the real level
//   F. anti-phase stereo  -> L = -R still measures full energy (no mono-mix
//                            cancellation; power combination is phase-safe)
//   G. dual tone          -> 100 Hz + 4 kHz both appear
//   H. post-EQ            -> boosting/cutting a band moves the measured
//                            spectrum at that frequency
//   I. sample rates       -> 44.1 / 48 / 96 kHz map frequencies correctly
//                            (mono transport included)
//
// All measurements use the un-smoothed calibrated targets; smoothing is a
// display concern only.
// ============================================================================

namespace APEX {
namespace G10 {

class G10AnalyzerTests : public juce::UnitTest
{
public:
    G10AnalyzerTests() : juce::UnitTest ("G10.Analyzer", "APEX.G10") {}

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    static std::vector<float> sine (int frames, double sampleRate,
                                    double freqHz, float amp)
    {
        std::vector<float> s ((size_t) frames);
        for (int i = 0; i < frames; ++i)
            s[(size_t) i] = amp * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * freqHz * (double) i / sampleRate);
        return s;
    }

    /** Bar index whose log-frequency range contains `freqHz`. */
    static int barForFreq (double freqHz)
    {
        const float logMin = std::log (G10SpectrumAnalyzerComponent::kMinFreqHz);
        const float logMax = std::log (G10SpectrumAnalyzerComponent::kMaxFreqHz);
        const float t = (float) ((std::log (freqHz) - logMin) / (logMax - logMin));
        return juce::jlimit (0, G10SpectrumAnalyzerComponent::kNumBars - 1,
                             (int) (t * G10SpectrumAnalyzerComponent::kNumBars));
    }

    /** Push one full window and run one FFT cycle. */
    static void feedWindow (G10AnalyzerFifo& fifo, G10SpectrumAnalyzerComponent& analyzer,
                            const std::vector<float>& l, const std::vector<float>& r)
    {
        fifo.pushStereo (l.data(), r.data(), (int) l.size());
        analyzer.tick();
    }

    class RealtimeAnalyzerProducer final : public juce::Thread
    {
    public:
        RealtimeAnalyzerProducer (G10Processor& processor,
                                  G10AnalyzerTimingProbe& timingProbe,
                                  double sampleRate,
                                  int blockSize)
            : juce::Thread ("G10 analyzer latency producer"),
              processor_ (processor),
              timingProbe_ (timingProbe),
              sampleRate_ (sampleRate),
              blockSize_ (blockSize),
              block_ (2, blockSize)
        {
        }

        void run() override
        {
            started_.store (true, std::memory_order_release);
            constexpr double prefillMilliseconds = 1000.0;
            constexpr double signalFrequencyHz = 997.0;
            constexpr float signalAmplitude = 0.35f;
            const double blockMilliseconds = 1000.0 * (double) blockSize_ / sampleRate_;
            const double phaseStep = juce::MathConstants<double>::twoPi * signalFrequencyHz / sampleRate_;
            const double startMilliseconds = juce::Time::getMillisecondCounterHiRes();
            double nextBlockMilliseconds = startMilliseconds;
            double phase = 0.0;
            bool signalActive = false;
            juce::MidiBuffer midi;

            while (! threadShouldExit())
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (now + 0.10 < nextBlockMilliseconds)
                {
                    juce::Thread::sleep (1);
                    continue;
                }

                if (! signalActive && now - startMilliseconds >= prefillMilliseconds)
                {
                    timingProbe_.arm();
                    signalActive = true;
                    signalArmed_.store (true, std::memory_order_release);
                }

                if (signalActive)
                {
                    for (int sample = 0; sample < blockSize_; ++sample)
                    {
                        const float value = signalAmplitude * (float) std::sin (phase);
                        phase += phaseStep;
                        if (phase >= juce::MathConstants<double>::twoPi)
                            phase -= juce::MathConstants<double>::twoPi;
                        block_.setSample (0, sample, value);
                        block_.setSample (1, sample, value);
                    }
                }
                else
                {
                    block_.clear();
                }

                midi.clear();
                processor_.processBlock (block_, midi);
                blocksProcessed_.fetch_add (1, std::memory_order_relaxed);
                nextBlockMilliseconds += blockMilliseconds;

                // A heavily delayed test worker must not burst indefinitely.
                if (now - nextBlockMilliseconds > blockMilliseconds * 4.0)
                    nextBlockMilliseconds = now + blockMilliseconds;
            }
        }

        bool hasStarted() const noexcept { return started_.load (std::memory_order_acquire); }
        bool hasArmedSignal() const noexcept { return signalArmed_.load (std::memory_order_acquire); }
        int getBlocksProcessed() const noexcept { return blocksProcessed_.load (std::memory_order_relaxed); }

    private:
        G10Processor& processor_;
        G10AnalyzerTimingProbe& timingProbe_;
        const double sampleRate_;
        const int blockSize_;
        juce::AudioBuffer<float> block_;
        std::atomic<bool> started_ { false };
        std::atomic<bool> signalArmed_ { false };
        std::atomic<int> blocksProcessed_ { 0 };
    };

    /** Assert the measured peak is within 1.5 bins of the true frequency. */
    void expectPeakNear (G10SpectrumAnalyzerComponent& analyzer,
                         double freqHz, double sampleRate, const juce::String& label)
    {
        const double binHz = sampleRate / (double) analyzer.getWindowSize();
        const double tol = 1.5 * binHz;
        const double err = std::abs ((double) analyzer.getPeakBinHz() - freqHz);
        expect (err <= tol,
                juce::String (label) + ": peak " + juce::String (analyzer.getPeakBinHz(), 1)
                + " Hz within " + juce::String (tol, 1) + " Hz of " + juce::String (freqHz, 1)
                + " Hz (err " + juce::String (err, 1) + ")");

        // The bar containing the frequency must carry the peak energy.
        const float barDb = analyzer.getBarTargetDb (barForFreq (freqHz));
        expect (std::abs (barDb - analyzer.getPeakBinDb()) < 5.0f,
                juce::String (label) + ": bar at " + juce::String (freqHz, 0)
                + " Hz holds the peak (" + juce::String (barDb, 1) + " dBFS)");
    }

    // ------------------------------------------------------------------
    // A. Silence -> floor
    // ------------------------------------------------------------------

    void testSilenceFloor()
    {
        beginTest ("Silence -> floor");

        G10AnalyzerFifo fifo;
        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (&fifo);
        analyzer.setSampleRate (48000.0);

        const std::vector<float> z ((size_t) analyzer.getWindowSize(), 0.0f);
        feedWindow (fifo, analyzer, z, z);

        expect (analyzer.getPeakBinDb() < -90.0f, "silence peak below -90 dBFS");
        for (int b = 0; b < analyzer.getNumBars(); ++b)
            expect (analyzer.getBarTargetDb (b) < -90.0f,
                    "silence bar " + juce::String (b) + " below -90 dBFS");
    }

    // ------------------------------------------------------------------
    // B/C/D. Frequency positions
    // ------------------------------------------------------------------

    void testFrequencyPositions()
    {
        const double sampleRate = 48000.0;
        const float amp = 0.5f; // -6 dBFS

        struct Case { double freq; const char* label; };
        const Case cases[] = { { 100.0, "100 Hz" }, { 1000.0, "1 kHz" }, { 10000.0, "10 kHz" } };

        for (const auto& c : cases)
        {
            beginTest (juce::String ("Position: ") + c.label);

            G10AnalyzerFifo fifo;
            G10SpectrumAnalyzerComponent analyzer;
            analyzer.setFifo (&fifo);
            analyzer.setSampleRate (sampleRate);

            const auto s = sine (analyzer.getWindowSize(), sampleRate, c.freq, amp);
            feedWindow (fifo, analyzer, s, s);

            expectPeakNear (analyzer, c.freq, sampleRate, c.label);
        }
    }

    // ------------------------------------------------------------------
    // E. Amplitude scaling (dBFS linearity)
    // ------------------------------------------------------------------

    void testAmplitudeScaling()
    {
        beginTest ("Amplitude scaling (-6/-12/-24/-48 dBFS)");

        const double sampleRate = 48000.0;
        const double freq = 1000.0;

        const float levelsDb[] = { -6.0f, -12.0f, -24.0f, -48.0f };
        float measured[4] = {};

        for (int i = 0; i < 4; ++i)
        {
            G10AnalyzerFifo fifo;
            G10SpectrumAnalyzerComponent analyzer;
            analyzer.setFifo (&fifo);
            analyzer.setSampleRate (sampleRate);

            const float amp = std::pow (10.0f, levelsDb[i] / 20.0f);
            const auto s = sine (analyzer.getWindowSize(), sampleRate, freq, amp);
            feedWindow (fifo, analyzer, s, s);
            measured[i] = analyzer.getPeakBinDb();
        }

        // Absolute sanity: a -6 dBFS sine measures within Hann scalloping
        // loss of -6 dBFS (worst case ~1.4 dB at half-bin offset; the 1 kHz
        // bin at 48 kHz sits at bin 42.67, so allow a little more).
        expect (measured[0] > -10.0f && measured[0] < -4.0f,
                "absolute level: -6 dBFS sine measures " + juce::String (measured[0], 2) + " dBFS");

        // Relative linearity: a 6/18/42 dB reduction must measure as such.
        expect (std::abs ((measured[0] - measured[1]) - 6.0f) < 2.0f,
                "6 dB step measures " + juce::String (measured[0] - measured[1], 2) + " dB");
        expect (std::abs ((measured[0] - measured[2]) - 18.0f) < 2.0f,
                "18 dB step measures " + juce::String (measured[0] - measured[2], 2) + " dB");
        expect (std::abs ((measured[0] - measured[3]) - 42.0f) < 2.5f,
                "42 dB step measures " + juce::String (measured[0] - measured[3], 2) + " dB");
    }

    // ------------------------------------------------------------------
    // F. Anti-phase (no mono-mix cancellation)
    // ------------------------------------------------------------------

    void testAntiPhase()
    {
        beginTest ("Anti-phase stereo");

        const double sampleRate = 48000.0;
        const double freq = 1000.0;
        const float amp = 0.5f; // -6 dBFS

        // In-phase: L = R = A sin.
        G10AnalyzerFifo fifoIn;
        G10SpectrumAnalyzerComponent analyzerIn;
        analyzerIn.setFifo (&fifoIn);
        analyzerIn.setSampleRate (sampleRate);
        const auto s = sine (analyzerIn.getWindowSize(), sampleRate, freq, amp);
        feedWindow (fifoIn, analyzerIn, s, s);
        const float inPhaseDb = analyzerIn.getPeakBinDb();

        // Anti-phase: L = +A sin, R = -A sin. A mono mix would cancel this
        // completely; power combination must measure the same energy.
        G10AnalyzerFifo fifoAnti;
        G10SpectrumAnalyzerComponent analyzerAnti;
        analyzerAnti.setFifo (&fifoAnti);
        analyzerAnti.setSampleRate (sampleRate);
        std::vector<float> neg = s;
        for (auto& x : neg)
            x = -x;
        feedWindow (fifoAnti, analyzerAnti, s, neg);
        const float antiPhaseDb = analyzerAnti.getPeakBinDb();

        expect (antiPhaseDb > -10.0f,
                "anti-phase still measures energy (" + juce::String (antiPhaseDb, 1) + " dBFS)");
        expect (std::abs (antiPhaseDb - inPhaseDb) < 1.0f,
                "power combination is phase-independent (in " + juce::String (inPhaseDb, 1)
                + " dBFS vs anti " + juce::String (antiPhaseDb, 1) + " dBFS)");
    }

    // ------------------------------------------------------------------
    // G. Dual tone
    // ------------------------------------------------------------------

    void testDualTone()
    {
        beginTest ("Dual tone 100 Hz + 4 kHz");

        const double sampleRate = 48000.0;
        const float amp = 0.1f; // -20 dBFS each

        G10AnalyzerFifo fifo;
        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (&fifo);
        analyzer.setSampleRate (sampleRate);

        const int n = analyzer.getWindowSize();
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sampleRate;
            const float v = amp * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * t)
                                         + std::sin (2.0 * juce::MathConstants<double>::pi * 4000.0 * t));
            l[(size_t) i] = v;
            r[(size_t) i] = v;
        }
        feedWindow (fifo, analyzer, l, r);

        const float db100 = analyzer.getBarTargetDb (barForFreq (100.0));
        const float db4k  = analyzer.getBarTargetDb (barForFreq (4000.0));
        const float peak  = analyzer.getPeakBinDb();

        expect (db100 > -26.0f, "100 Hz tone present (" + juce::String (db100, 1) + " dBFS)");
        expect (db4k  > -26.0f, "4 kHz tone present (" + juce::String (db4k, 1) + " dBFS)");
        expect (std::abs (db100 - peak) < 6.0f || std::abs (db4k - peak) < 6.0f,
                "one of the tones is the global peak");
    }

    // ------------------------------------------------------------------
    // H. Post-EQ: the analyzer measures the REAL processed output
    // ------------------------------------------------------------------

    /** Process `blocks` blocks of a continuous sine through the processor,
        draining the FIFO periodically so it holds the most recent audio,
        then measure the calibrated bar level at freqHz. */
    float measurePostEq (G10Processor& proc, double sampleRate, int blockSize,
                         double freqHz, float amp, int blocks)
    {
        auto* fifo = proc.getAnalyzerFifo();
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        double phase = 0.0;
        const double step = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;

        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = amp * (float) std::sin (phase);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                phase += step;
            }
            proc.processBlock (buffer, midi);

            // Keep the FIFO holding recent audio (it drops the newest when
            // full, so drain periodically to discard the old tail).
            if (blk % 50 == 0)
            {
                float junk[256];
                while (fifo->available() > 0)
                    fifo->drain (junk, 256);
            }
        }

        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (fifo);
        analyzer.setSampleRate (sampleRate);
        analyzer.resetAnalysis();
        for (int i = 0; i < 8; ++i)
            analyzer.tick();

        return analyzer.getBarTargetDb (barForFreq (freqHz));
    }

    void testPostEq()
    {
        beginTest ("Post-EQ: band boost/cut moves the measured spectrum");

        const double sampleRate = 48000.0;
        const int blockSize = 512;
        const double freq = 2000.0;
        const float amp = 0.1f; // -20 dBFS

        G10Processor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        proc.setAnalyzerActive (true);

        auto* band2k = proc.getG10Parameter (G10ParamIndex::kBand2k);
        expect (band2k != nullptr, "2 kHz band parameter exists");
        if (band2k == nullptr)
            return;

        // Baseline: band at 0 dB (normalized 0.5).
        band2k->setValue (0.5f);
        const float base = measurePostEq (proc, sampleRate, blockSize, freq, amp, 300);

        // Boost +12 dB (normalized 1.0): the measured spectrum must rise.
        band2k->setValue (1.0f);
        const float boosted = measurePostEq (proc, sampleRate, blockSize, freq, amp, 300);
        expect (boosted - base > 6.0f,
                "boost +12 dB raises the measured spectrum ("
                + juce::String (base, 1) + " -> " + juce::String (boosted, 1) + " dBFS)");

        // Cut -12 dB (normalized 0.0): the measured spectrum must fall.
        band2k->setValue (0.0f);
        const float cut = measurePostEq (proc, sampleRate, blockSize, freq, amp, 300);
        expect (base - cut > 6.0f,
                "cut -12 dB lowers the measured spectrum ("
                + juce::String (base, 1) + " -> " + juce::String (cut, 1) + " dBFS)");
    }

    // ------------------------------------------------------------------
    // I. Sample-rate independence + mono transport
    // ------------------------------------------------------------------

    void testSampleRates()
    {
        beginTest ("Sample rates 44.1 / 48 / 96 kHz");

        const double rates[] = { 44100.0, 48000.0, 96000.0 };
        const double freq = 1000.0;
        const float amp = 0.5f;

        for (double sr : rates)
        {
            G10AnalyzerFifo fifo;
            G10SpectrumAnalyzerComponent analyzer;
            analyzer.setFifo (&fifo);
            analyzer.setSampleRate (sr);

            const auto s = sine (analyzer.getWindowSize(), sr, freq, amp);
            feedWindow (fifo, analyzer, s, s);

            expectPeakNear (analyzer, freq, sr, juce::String (sr / 1000.0, 1) + " kHz");
        }
    }

    void testMonoTransport()
    {
        beginTest ("Mono bus transport (duplicated by the FIFO)");

        const double sampleRate = 48000.0;
        const int blockSize = 512;
        const double freq = 1000.0;
        const float amp = 0.5f;

        G10Processor proc;
        proc.setPlayConfigDetails (1, 1, sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);
        proc.setAnalyzerActive (true);

        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const double step = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        for (int blk = 0; blk < 64; ++blk)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, amp * (float) std::sin (phase));
                phase += step;
            }
            proc.processBlock (buffer, midi);
        }

        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (proc.getAnalyzerFifo());
        analyzer.setSampleRate (sampleRate);
        analyzer.resetAnalysis();
        for (int i = 0; i < 8; ++i)
            analyzer.tick();

        expectPeakNear (analyzer, freq, sampleRate, "mono transport");
    }

    void testNewestWindowPolicy()
    {
        beginTest ("Freshness policy keeps newest 2048 frames and discards stale visualization history");

        constexpr double sampleRate = 48000.0;
        G10AnalyzerFifo fifo;
        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (&fifo);
        analyzer.setSampleRate (sampleRate);

        const auto stale = sine (analyzer.getWindowSize(), sampleRate, 100.0, 0.5f);
        const auto newest = sine (analyzer.getWindowSize(), sampleRate, 10000.0, 0.5f);

        // Exactly fill the 8192-frame stereo FIFO with three stale windows
        // followed by the newest one. One tick must consume through the whole
        // snapshot, run one FFT, and report the newest window.
        for (int window = 0; window < 3; ++window)
            fifo.pushStereo (stale.data(), stale.data(), analyzer.getWindowSize());
        fifo.pushStereo (newest.data(), newest.data(), analyzer.getWindowSize());

        expectEquals (fifo.available(), G10AnalyzerFifo::kCapacity,
                      "test backlog fills the complete stereo FIFO");
        analyzer.tick();

        expectEquals (fifo.available(), 0, "one tick consumes through the captured backlog");
        expectPeakNear (analyzer, 10000.0, sampleRate, "newest-window backlog selection");
    }

    void testVisibilityBoundaryReset()
    {
        beginTest ("Visibility boundary clears analyzer FIFO and local display state");

        constexpr double sampleRate = 48000.0;
        G10AnalyzerFifo fifo;
        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (&fifo);
        analyzer.setSampleRate (sampleRate);

        const auto signal = sine (analyzer.getWindowSize(), sampleRate, 1000.0, 0.5f);
        feedWindow (fifo, analyzer, signal, signal);
        expect (analyzer.getPeakBinDb() > -20.0f, "precondition: analyzer contains measured signal");

        fifo.pushStereo (signal.data(), signal.data(), analyzer.getWindowSize());
        expect (fifo.available() > 0, "precondition: FIFO contains queued visualization history");

        analyzer.resetForVisibilityBoundary();
        expectEquals (fifo.available(), 0, "visibility boundary discards queued visualization history");
        expect (analyzer.getPeakBinDb() < -90.0f, "visibility boundary clears measured peak");
        for (int bar = 0; bar < analyzer.getNumBars(); ++bar)
            expect (analyzer.getBarTargetDb (bar) <= -G10SpectrumAnalyzerComponent::kDisplayRangeDb,
                    "visibility boundary clears target bar " + juce::String (bar));
    }

    void testPresentationRangeHasNoHiddenDebt()
    {
        beginTest ("Presentation range does not accumulate invisible smoothing debt");

        constexpr double sampleRate = 48000.0;
        G10AnalyzerFifo fifo;
        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (&fifo);
        analyzer.setSampleRate (sampleRate);

        const std::vector<float> silence ((size_t) analyzer.getWindowSize(), 0.0f);
        feedWindow (fifo, analyzer, silence, silence);
        expect (analyzer.getPeakBinDb() < -90.0f,
                "raw calibrated silence remains below the visible floor");

        for (int tick = 0; tick < 10; ++tick)
            analyzer.onPresentationTick (0.1);

        for (int bar = 0; bar < analyzer.getNumBars(); ++bar)
            expectWithinAbsoluteError (analyzer.getBarLevelDb (bar),
                                       -G10SpectrumAnalyzerComponent::kDisplayRangeDb,
                                       1.0e-6f,
                                       "display state remains exactly at its truthful floor");

        const auto signal = sine (analyzer.getWindowSize(), sampleRate, 1000.0, 0.35f);
        feedWindow (fifo, analyzer, signal, signal);
        analyzer.onPresentationTick (1.0 / 60.0);
        expect (analyzer.getBarLevelDb (barForFreq (1000.0))
                    > -G10SpectrumAnalyzerComponent::kDisplayRangeDb,
                "first presentation step begins visible truthful attack");

        const auto overRange = sine (analyzer.getWindowSize(), sampleRate, 1000.0, 2.0f);
        feedWindow (fifo, analyzer, overRange, overRange);
        for (int tick = 0; tick < 10; ++tick)
            analyzer.onPresentationTick (0.1);
        for (int bar = 0; bar < analyzer.getNumBars(); ++bar)
            expect (analyzer.getBarLevelDb (bar) <= 0.0f,
                    "display state never accumulates hidden energy above 0 dBFS");
    }

    void testCorrelatedStartupLatency()
    {
        beginTest ("Correlated T0-T4 startup latency at 48 kHz, FFT 2048, analysis 30 Hz");

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;
        constexpr double analysisIntervalMilliseconds = 1000.0 / 30.0;
        constexpr double presentationIntervalMilliseconds = 1000.0 / 60.0;
        constexpr double timeoutMilliseconds = 8000.0;

        G10Processor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        processor.setAnalyzerActive (true);

        G10SpectrumAnalyzerComponent analyzer;
        analyzer.setFifo (processor.getAnalyzerFifo());
        analyzer.setSampleRate (sampleRate);
        analyzer.setSize (860, 176);

        G10AnalyzerTimingProbe timingProbe;
        processor.getAnalyzerFifo()->setTimingProbe (&timingProbe);
        analyzer.setTimingProbe (&timingProbe);

        RealtimeAnalyzerProducer producer (processor, timingProbe, sampleRate, blockSize);
        producer.startThread();

        const double producerStartDeadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
        while (! producer.hasStarted()
               && juce::Time::getMillisecondCounterHiRes() < producerStartDeadline)
            juce::Thread::sleep (1);

        expect (producer.hasStarted(), "Realtime analyzer producer thread must start");

        const double startMilliseconds = juce::Time::getMillisecondCounterHiRes();
        double nextAnalysisMilliseconds = startMilliseconds;
        double nextPresentationMilliseconds = startMilliseconds;
        double lastPresentationMilliseconds = startMilliseconds;
        juce::Image renderedFrame (juce::Image::ARGB, 860, 176, true);
        juce::Graphics frameGraphics (renderedFrame);
        G10AnalyzerTimingProbe::Snapshot snapshot;
        int analysisTicks = 0;
        int presentationTicks = 0;

        while (juce::Time::getMillisecondCounterHiRes() - startMilliseconds < timeoutMilliseconds)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();

            if (now >= nextAnalysisMilliseconds)
            {
                analyzer.tick();
                ++analysisTicks;
                nextAnalysisMilliseconds += analysisIntervalMilliseconds;
            }

            if (now >= nextPresentationMilliseconds)
            {
                const double elapsedSeconds = juce::jlimit (
                    0.0005, 0.1, (now - lastPresentationMilliseconds) / 1000.0);
                lastPresentationMilliseconds = now;
                analyzer.onPresentationTick (elapsedSeconds);
                ++presentationTicks;
                nextPresentationMilliseconds += presentationIntervalMilliseconds;

                // Production paints only after presentation invalidates the
                // analyzer. Reuse one image/context so test allocation and
                // unconditional invisible paints cannot block the 30/60 Hz
                // schedulers or masquerade as presentation latency.
                snapshot = timingProbe.snapshot();
                if (snapshot.repaintRequest > 0 && snapshot.t4 == 0)
                {
                    renderedFrame.clear (renderedFrame.getBounds(), juce::Colours::transparentBlack);
                    analyzer.paint (frameGraphics);
                }
            }

            snapshot = timingProbe.snapshot();
            if (snapshot.isComplete())
                break;

            juce::Thread::sleep (1);
        }

        producer.stopThread (1000);
        const int fifoAvailableAtStop = processor.getAnalyzerFifo()->available();
        const bool probeArmedAtStop = timingProbe.isArmedForTesting();
        processor.getAnalyzerFifo()->setTimingProbe (nullptr);
        analyzer.setTimingProbe (nullptr);
        processor.setAnalyzerActive (false);
        processor.releaseResources();

        snapshot = timingProbe.snapshot();
        const double t1Milliseconds = snapshot.millisecondsFromT0 (snapshot.t1);
        const double t2Milliseconds = snapshot.millisecondsFromT0 (snapshot.t2);
        const double t3Milliseconds = snapshot.millisecondsFromT0 (snapshot.t3);
        const double t4Milliseconds = snapshot.millisecondsFromT0 (snapshot.t4);
        const double visibleTargetMilliseconds = snapshot.millisecondsFromT0 (snapshot.visibleTarget);
        const double presentationEntryMilliseconds = snapshot.millisecondsFromT0 (snapshot.presentationEntry);
        const double visibleLevelMilliseconds = snapshot.millisecondsFromT0 (snapshot.visibleLevel);
        const double repaintRequestMilliseconds = snapshot.millisecondsFromT0 (snapshot.repaintRequest);
        const double paintEntryMilliseconds = snapshot.millisecondsFromT0 (snapshot.paintEntry);

        logMessage (juce::String ("[G10_ANALYZER_LATENCY] policy=")
                    + G10SpectrumAnalyzerComponent::getFreshnessPolicyName()
                    + juce::String::formatted (
                        " sampleRate=48000 fft=2048 analysisHz=30 "
                        "T1-T0=%.3fms T2-T0=%.3fms T3-T0=%.3fms T4-T0=%.3fms complete=%d",
                        t1Milliseconds, t2Milliseconds, t3Milliseconds, t4Milliseconds,
                        snapshot.isComplete() ? 1 : 0));

        logMessage (juce::String::formatted (
            "[G10_ANALYZER_PRESENTATION] visibleTarget-T0=%.3fms presentationEntry-T0=%.3fms "
            "visibleLevel-T0=%.3fms repaintRequest-T0=%.3fms paintEntry-T0=%.3fms",
            visibleTargetMilliseconds, presentationEntryMilliseconds,
            visibleLevelMilliseconds, repaintRequestMilliseconds, paintEntryMilliseconds));

        logMessage (juce::String::formatted (
            "[G10_ANALYZER_PRODUCER] started=%d signalArmed=%d probeArmed=%d blocks=%d "
            "analysisTicks=%d presentationTicks=%d fifoAvailable=%d",
            producer.hasStarted() ? 1 : 0,
            producer.hasArmedSignal() ? 1 : 0,
            probeArmedAtStop ? 1 : 0,
            producer.getBlocksProcessed(), analysisTicks, presentationTicks,
            fifoAvailableAtStop));

        expect (snapshot.isComplete(), "The correlated signal must reach the rendered spectrum within the timeout");
        expect (snapshot.t1 >= snapshot.t0 && snapshot.t0 > 0,
                "T1 must follow an accepted post-EQ T0 write");
        expect (snapshot.t2 >= snapshot.t1 && snapshot.t1 > 0,
                "T2 must follow the consumer range containing T0");
        expect (snapshot.t3 >= snapshot.t2 && snapshot.t2 > 0,
                "T3 must follow the correlated FFT");
        expect (snapshot.t4 >= snapshot.t3 && snapshot.t3 > 0,
                "T4 must follow the updated spectrum cache");
    }

    // ------------------------------------------------------------------

    void runTest() override
    {
        testSilenceFloor();
        testFrequencyPositions();
        testAmplitudeScaling();
        testAntiPhase();
        testDualTone();
        testPostEq();
        testSampleRates();
        testMonoTransport();
        testNewestWindowPolicy();
        testVisibilityBoundaryReset();
        testPresentationRangeHasNoHiddenDebt();
        testCorrelatedStartupLatency();
    }
};

static G10AnalyzerTests g10AnalyzerTests;

} // namespace G10
} // namespace APEX

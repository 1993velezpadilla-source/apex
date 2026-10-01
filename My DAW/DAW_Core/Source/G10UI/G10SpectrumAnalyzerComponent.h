#pragma once
#include <JuceHeader.h>
#include "G10AnalyzerFifo.h"
#include "../UICore/ApexPresentationClock.h"
#include "../EQCore/ApexInteractiveEQSurface.h"
#include "../ThirdParty/signalsmith-linear/fft.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10SpectrumAnalyzerComponent — the ONE living POST-EQ spectrum.
//
// Data flow:
//   audio thread  -> G10AnalyzerFifo::pushStereo() (minimal, analyzerActive-
//                    gated; the processor pushes the EXACT samples the host
//                    receives — post-EQ, post-analog, post-quality, post-trim,
//                    post-bypass)
//   GUI thread    -> tick() (editor's ~30 Hz measurement timer) -> stereo FFT
//                    -> calibrated bar TARGETS (cached, un-smoothed)
//   presentation  -> ApexPresentationClock::onPresentationTick() at the
//                    display-aware rate (60/120/144 Hz) interpolates the
//                    cached targets in the dB domain and repaints ONLY this
//                    component. Measurement and presentation are separate:
//                    the FFT never runs at the display rate, and the display
//                    never waits for a new FFT.
//
// Measurement contract (the analyzer is an INSTRUMENT, not decoration):
//   - Both channels are FFT'd separately and combined by POWER:
//     amp[k] = sqrt( (ampL[k]^2 + ampR[k]^2) / 2 ).
//     Anti-phase material therefore ADDS energy; the analyzer can never
//     cancel a signal the host is actually receiving.
//   - The FFT is the signalsmith RealFFT (unnormalized forward transform).
//     Single-sided amplitude calibration: A[k] = |X[k]| * 2 / (N * CG) for
//     bins 1..N/2-1 (DC/Nyquist use the un-doubled factor), where CG is the
//     Hann window's coherent gain computed exactly from the runtime window
//     coefficients (~0.5). A full-scale sine therefore measures ~0 dBFS.
//   - Bars: 140 log-spaced divisions, 20 Hz..20 kHz, real frequency bins
//     (binHz = sampleRate / N — sample-rate independent). Each bar shows the
//     peak dBFS of its bin range (a sine lands in its own bar).
//   - Smoothing happens AFTER calibration, in the dB domain only (fast
//     attack, slow release — classic analyzer feel), with TIME-based
//     constants on the presentation clock so the curve feels identical at
//     60, 120 or 144 Hz. Test reads use the un-smoothed targets.
//   - Silence measures the noise floor (<= -300 dBFS -> curve at rest).
//
// Thread contract: the FFT runs on the GUI thread only. No FFT, allocation,
// locking or GUI work ever runs on the audio thread. When no editor exists
// the processor does not push at all, so the analyzer cost is zero while the
// editor is closed. When the editor is hidden, setPresentationActive(false)
// releases the presentation-clock subscription, so a hidden G10 costs
// nothing (no FFT, no interpolation, no repaint, no blink).
//
// Render contract: maroon ONLY (#800000 core, #B03030 glow, #4A0000 depth).
// The spectrum is drawn as ONE continuous trace (juce::Path with quadratic
// midpoints) + a subtle fill + a 3-stroke glow — no bars, no PRE/POST/EQ
// triple-line clutter. Repaints happen only when a level actually moved.
// ============================================================================
// Measurement contract (the analyzer is an INSTRUMENT, not decoration):
//   - Both channels are FFT'd separately and combined by POWER:
//     amp[k] = sqrt( (ampL[k]^2 + ampR[k]^2) / 2 ).
//     Anti-phase material therefore ADDS energy; the analyzer can never
//     cancel a signal the host is actually receiving.
//   - The FFT is the signalsmith RealFFT (unnormalized forward transform).
//     Single-sided amplitude calibration: A[k] = |X[k]| * 2 / (N * CG) for
//     bins 1..N/2-1 (DC/Nyquist use the un-doubled factor), where CG is the
//     Hann window's coherent gain computed exactly from the runtime window
//     coefficients (~0.5). A full-scale sine therefore measures ~0 dBFS.
//   - Bars: 140 log-spaced divisions, 20 Hz..20 kHz, real frequency bins
//     (binHz = sampleRate / N — sample-rate independent). Each bar shows the
//     peak dBFS of its bin range (a sine lands in its own bar).
//   - Smoothing happens AFTER calibration, in the dB domain only (fast
//     attack, slow release — classic analyzer feel). Test reads use the
//     un-smoothed targets.
//   - Silence measures the noise floor (<= -300 dBFS -> curve at rest).
//
// Thread contract: the FFT runs on the GUI thread only. No FFT, allocation,
// locking or GUI work ever runs on the audio thread. When no editor exists
// the processor does not push at all, so the analyzer cost is zero while the
// editor is closed.
//
// Render contract: maroon ONLY (#800000 core, #B03030 glow, #4A0000 depth).
// ONE continuous trace (juce::Path, quadratic midpoints) + subtle fill +
// 3-stroke glow. No bars, no PRE/POST/EQ triple-line clutter.
// ============================================================================

class G10SpectrumAnalyzerComponent final : public juce::Component,
                                            public DAW::ApexPresentationClock::TickReceiver
{
public:
    static constexpr int kWindowSize = 2048;
    static constexpr int kNumBars = 140;
    static constexpr float kMinFreqHz = 20.0f;
    static constexpr float kMaxFreqHz = 20000.0f;
    // Display range: 0 dBFS (top of the frame) .. -60 dBFS (baseline).
    static constexpr float kDisplayRangeDb = 60.0f;

    G10SpectrumAnalyzerComponent();
    ~G10SpectrumAnalyzerComponent() override;

    /** GUI thread: set the processor's analyzer FIFO (once, at editor setup). */
    void setFifo (G10AnalyzerFifo* fifo) noexcept { fifo_ = fifo; }

    /** GUI thread: attach the interactive Mini EQ surface overlay. The
        surface is a transparent child filling the analyzer bounds; the
        existing Content transform scales both together. Non-owning. */
    void setInteractiveSurface (ApexInteractiveEQSurface* surface) noexcept
    {
        eqSurface_ = surface;
        if (eqSurface_ != nullptr)
        {
            addAndMakeVisible (eqSurface_);
            eqSurface_->setBounds (getLocalBounds());
        }
    }

    /** Focused latency-test hook; production leaves this null. */
    void setTimingProbe (G10AnalyzerTimingProbe* probe) noexcept { timingProbe_ = probe; }

    static constexpr const char* getFreshnessPolicyName() noexcept
    {
        return "latest-window";
    }

    /** GUI thread: sample rate (from the processor every tick). A rate change
        discards the partially accumulated window, so stale-rate windows are
        never shown. */
    void setSampleRate (double sampleRate) noexcept
    {
        if (sampleRate > 0.0 && sampleRate != sampleRate_)
        {
            sampleRate_ = sampleRate;
            resetAnalysis();
        }
    }

    double getSampleRate() const noexcept { return sampleRate_; }

    /** GUI thread: editor host scale (contentScale from G10Editor::resized).
        Used for responsive axis-label density: at small host scales the label
        SET is reduced (readable fewer labels beats microscopic many). */
    void setScaleHint (float scale) noexcept
    {
        scale = juce::jmax (0.0f, scale);
        if (std::abs (scale - scaleHint_) > 1.0e-4f)
        {
            scaleHint_ = scale;
            rebuildStaticCache();
            rebuildTraceCache();
        }
    }

    float getScaleHint() const noexcept { return scaleHint_; }

    /** GUI thread (~30 Hz measurement timer): drain FIFO, run the stereo FFT
        when a window is ready, compute calibrated bar targets. No repaint —
        the presentation clock renders the cached targets. */
    void tick();

    /** GUI thread: drop buffered audio (after sample-rate changes). */
    void resetAnalysis();

    /** GUI thread: discard FIFO history and all local analyzer presentation
        state at an editor visibility boundary. Audio processing is untouched. */
    void resetForVisibilityBoundary();

    /** GUI thread: join/leave the display-aware presentation clock. While
        the editor is visible the analyzer requests continuous updates so the
        smoothed spectrum renders at the display rate (60-144 Hz); the FFT
        measurement itself stays on the editor's ~30 Hz timer. */
    void setPresentationActive (bool active) noexcept;

    /** Presentation clock (message thread, display-aware rate): interpolate
        the cached measured targets in the dB domain and repaint ONLY this
        component when anything moved. No FFT, no FIFO, no allocation. */
    void onPresentationTick (double deltaSeconds) override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- Test introspection (headless measurement; no GUI required) -------
    int getWindowSize() const noexcept { return kWindowSize; }
    int getNumBars() const noexcept { return kNumBars; }

    /** Instantaneous calibrated dBFS of a bar (un-smoothed measurement). */
    float getBarTargetDb (int bar) const noexcept
    {
        return barTargetDb_[(size_t) juce::jlimit (0, kNumBars - 1, bar)];
    }

    /** Smoothed calibrated dBFS of a bar (what the display shows). */
    float getBarLevelDb (int bar) const noexcept
    {
        return barLevelDb_[(size_t) juce::jlimit (0, kNumBars - 1, bar)];
    }

    /** Geometric centre frequency (Hz) of a bar (log axis). */
    float getBarCenterHz (int bar) const noexcept;

    /** Calibrated magnitude (dBFS) of the strongest single bin. */
    float getPeakBinDb() const noexcept { return peakDb_; }

    /** Frequency (Hz) of the strongest single bin: k * (sampleRate / N). */
    float getPeakBinHz() const noexcept { return peakHz_; }

private:
    void computeBars();
    void rebuildStaticCache();
    void rebuildTraceCache();
    float frequencyToX (float frequencyHz) const noexcept;

    G10AnalyzerFifo* fifo_ = nullptr;
    G10AnalyzerTimingProbe* timingProbe_ = nullptr;
    ApexInteractiveEQSurface* eqSurface_ = nullptr;
    double sampleRate_ = 48000.0;
    bool presentationActive_ = false;
    float scaleHint_ = 1.0f; // editor content scale (for label density)

    // Preallocated, GUI-thread-owned (all sized in the constructor).
    std::vector<float> window_;    // Hann window, kWindowSize
    std::vector<float> accumL_;    // left accumulation buffer, kWindowSize
    std::vector<float> accumR_;    // right accumulation buffer, kWindowSize
    std::vector<float> fftInL_;    // windowed left input, kWindowSize
    std::vector<float> fftInR_;    // windowed right input, kWindowSize
    std::vector<std::complex<float>> fftOutL_; // kWindowSize/2 + 1
    std::vector<std::complex<float>> fftOutR_; // kWindowSize/2 + 1
    std::vector<float> binDb_;     // calibrated per-bin dBFS, kWindowSize/2 + 1
    std::vector<float> barLevels_; // kNumBars smoothed 0..1 (display)
    std::vector<float> barTargetDb_;// kNumBars instantaneous dBFS
    std::vector<float> barLevelDb_; // kNumBars smoothed dBFS
    int accumCount_ = 0;
    float coherentGain_ = 0.5f;    // Hann CG, computed from the runtime window
    float peakDb_ = -300.0f;       // strongest bin, dBFS (last window)
    float peakHz_ = 0.0f;          // strongest bin, Hz (last window)

    // GUI-only render caches. The chamber/guides are rasterized on resize;
    // spectrum geometry updates only when smoothed levels move. paint() merely
    // composites these bounded caches and never recomputes FFT-derived data.
    juce::Image chamberCache_;
    juce::Path chamberClip_;
    juce::Path spectrumPath_;
    juce::Path spectrumFillPath_;
    juce::ColourGradient spectrumFillGradient_;

    // Persistent 2048-point FFT: configured once, reused every tick.
    // Never constructed/destroyed per tick; never runs on the audio thread.
    signalsmith::linear::RealFFT<float> fft_;
};

} // namespace G10
} // namespace APEX

#include "G10SpectrumAnalyzerComponent.h"
#include "G10LookAndFeel.h"
#include <cstring>

namespace APEX {
namespace G10 {

namespace
{
    // Time-based dB-domain smoothing (fast attack, slow release). Applied on
    // the presentation clock with exponential time constants, so the visual
    // curve feels identical at 60, 120 or 144 Hz.
    constexpr float kAttackTau  = 0.05f;  // seconds
    constexpr float kReleaseTau = 0.40f;  // seconds
}

G10SpectrumAnalyzerComponent::G10SpectrumAnalyzerComponent()
    : fft_ (kWindowSize) // persistent 2048-point FFT, configured once
{
    setOpaque (false);
    // CRITICAL: the interactive Mini EQ surface is a CHILD of this analyzer.
    // (false, false) here would block mouse delivery to the whole subtree -
    // the surface would paint but never receive clicks (real-host dead-surface
    // bug). (true, true) lets this component AND its children intercept.
    setInterceptsMouseClicks (true, true);

    DAW::ApexPresentationClock::instance().addReceiver (this);

    window_.resize (kWindowSize);
    accumL_.resize (kWindowSize);
    accumR_.resize (kWindowSize);
    fftInL_.resize (kWindowSize);
    fftInR_.resize (kWindowSize);
    fftOutL_.resize (kWindowSize / 2 + 1);
    fftOutR_.resize (kWindowSize / 2 + 1);
    binDb_.resize (kWindowSize / 2 + 1);
    barLevels_.assign (kNumBars, 0.0f);
    barTargetDb_.assign (kNumBars, -kDisplayRangeDb);
    barLevelDb_.assign (kNumBars, -kDisplayRangeDb);
    chamberClip_.preallocateSpace (32);
    spectrumPath_.preallocateSpace (kNumBars * 8);
    spectrumFillPath_.preallocateSpace (kNumBars * 8 + 16);

    // Hann window (precomputed once; never on the audio thread). The
    // coherent gain is computed EXACTLY from the same coefficients used at
    // runtime, so the dBFS calibration can never drift from the window.
    for (int i = 0; i < kWindowSize; ++i)
    {
        const float phase = (float) i / (float) (kWindowSize - 1);
        window_[i] = 0.5f - 0.5f * std::cos (2.0f * (float) juce::MathConstants<float>::pi * phase);
    }
    float gainSum = 0.0f;
    for (int i = 0; i < kWindowSize; ++i)
        gainSum += window_[i];
    coherentGain_ = gainSum / (float) kWindowSize;
}

G10SpectrumAnalyzerComponent::~G10SpectrumAnalyzerComponent()
{
    // Message thread (editor lifecycle). removeReceiver tombstones the
    // registration and removes any active continuous-update contribution.
    DAW::ApexPresentationClock::instance().removeReceiver (this);
}

void G10SpectrumAnalyzerComponent::setPresentationActive (bool active) noexcept
{
    if (active == presentationActive_)
        return;
    presentationActive_ = active;

    auto& clock = DAW::ApexPresentationClock::instance();
    if (active)
        clock.requestContinuousUpdate (this);
    else
        clock.releaseContinuousUpdate (this);
}

void G10SpectrumAnalyzerComponent::resetAnalysis()
{
    accumCount_ = 0;
    std::fill (accumL_.begin(), accumL_.end(), 0.0f);
    std::fill (accumR_.begin(), accumR_.end(), 0.0f);
    std::fill (barLevels_.begin(), barLevels_.end(), 0.0f);
    std::fill (barTargetDb_.begin(), barTargetDb_.end(), -kDisplayRangeDb);
    std::fill (barLevelDb_.begin(), barLevelDb_.end(), -kDisplayRangeDb);
    peakDb_ = -300.0f;
    peakHz_ = 0.0f;
    rebuildTraceCache();
    repaint();
}

void G10SpectrumAnalyzerComponent::resetForVisibilityBoundary()
{
    if (fifo_ != nullptr)
        fifo_->reset();
    resetAnalysis();
}

void G10SpectrumAnalyzerComponent::tick()
{
    if (fifo_ == nullptr)
        return;

    // Consume through one FIFO snapshot every analysis tick, preserving only
    // its newest useful 2048 frames. fftInL/R are preallocated scratch here;
    // they are overwritten with windowed data below before the single FFT.
    const int freshFrames = fifo_->drainNewestFrames (fftInL_.data(),
                                                       fftInR_.data(),
                                                       kWindowSize);
    if (freshFrames <= 0)
        return;

    // Form a rolling latest-window view. If fewer than 2048 new frames arrived
    // (normally ~1600 at 48 kHz / 30 Hz), retain only the newest local tail
    // needed to complete the window and append every fresh frame.
    const int retainedFrames = juce::jmin (accumCount_, kWindowSize - freshFrames);
    if (retainedFrames > 0)
    {
        const int sourceOffset = accumCount_ - retainedFrames;
        std::memmove (accumL_.data(), accumL_.data() + sourceOffset,
                      (size_t) retainedFrames * sizeof (float));
        std::memmove (accumR_.data(), accumR_.data() + sourceOffset,
                      (size_t) retainedFrames * sizeof (float));
    }

    std::copy_n (fftInL_.data(), freshFrames, accumL_.data() + retainedFrames);
    std::copy_n (fftInR_.data(), freshFrames, accumR_.data() + retainedFrames);
    accumCount_ = retainedFrames + freshFrames;

    if (accumCount_ < kWindowSize)
        return; // not a full window yet

    // Window both channels (GUI thread only; the persistent FFT is reused).
    for (int i = 0; i < kWindowSize; ++i)
    {
        fftInL_[i] = accumL_[i] * window_[i];
        fftInR_[i] = accumR_[i] * window_[i];
    }

    // Separate transforms: the stereo combination happens AFTER the FFT by
    // power, so phase relationships can never cancel the measurement.
    fft_.fft (fftInL_.data(), fftOutL_.data());
    fft_.fft (fftInR_.data(), fftOutR_.data());
    if (timingProbe_ != nullptr)
        timingProbe_->recordFftComplete();

    // Keep this complete latest window. The next tick retains exactly the tail
    // required for all newly drained frames, so stale history cannot replay.
    // There is still at most one FFT in this tick.

    // Measurement only: compute calibrated targets. No repaint here — the
    // presentation clock interpolates the cached targets at the display
    // rate, so the FFT cadence (~30 Hz) never limits the visual cadence.
    computeBars();
    if (timingProbe_ != nullptr)
    {
        timingProbe_->recordSpectrumCacheUpdated();
        const float targetPeak = *std::max_element (barTargetDb_.begin(), barTargetDb_.end());
        if (targetPeak > -kDisplayRangeDb)
            timingProbe_->recordVisibleTargetPublished();
    }
}

void G10SpectrumAnalyzerComponent::computeBars()
{
    const float sampleRate = (float) juce::jmax (1.0, sampleRate_);
    const float binHz = sampleRate / (float) kWindowSize;
    const int halfN = kWindowSize / 2;

    // Single-sided amplitude calibration:
    //   A[k] = |X[k]| * 2 / (N * CG)   for bins 1..N/2-1
    //   A[k] = |X[k]| / (N * CG)       for DC (k=0) and Nyquist (k=N/2)
    // X[k] is the unnormalized forward transform of the windowed input; a
    // full-scale sine concentrates ~A*N*CG/2 into its bin, so A[k] is the
    // true per-channel amplitude and 20*log10(A) is dBFS.
    const float scaleFull = 2.0f / ((float) kWindowSize * coherentGain_);
    const float scaleEdge = scaleFull * 0.5f;

    peakDb_ = -300.0f;
    peakHz_ = 0.0f;

    for (int k = 0; k <= halfN; ++k)
    {
        const float scale = (k == 0 || k == halfN) ? scaleEdge : scaleFull;
        const float ampL = std::abs (fftOutL_[(size_t) k]) * scale;
        const float ampR = std::abs (fftOutR_[(size_t) k]) * scale;

        // Power combination (|L|^2 + |R|^2) / 2 — phase-safe by construction.
        const float combined = std::sqrt ((ampL * ampL + ampR * ampR) * 0.5f);
        const float db = 20.0f * std::log10 (juce::jmax (1e-12f, combined));
        binDb_[(size_t) k] = db;

        if (k > 0 && db > peakDb_)
        {
            peakDb_ = db;
            peakHz_ = (float) k * binHz;
        }
    }

    // Bars: 140 log-spaced divisions over 20 Hz..20 kHz. Each bar holds the
    // PEAK dBFS of its real bin range, so a sine lands in its own bar and
    // amplitude is never smeared across neighbors.
    const float logMin = std::log (kMinFreqHz);
    const float logMax = std::log (kMaxFreqHz);

    for (int b = 0; b < kNumBars; ++b)
    {
        const float f0 = std::exp (logMin + (logMax - logMin) * (float) b / (float) kNumBars);
        const float f1 = std::exp (logMin + (logMax - logMin) * (float) (b + 1) / (float) kNumBars);
        const int bin0 = juce::jmax (1, (int) std::floor (f0 / binHz));
        const int bin1 = juce::jmin (halfN, (int) std::ceil (f1 / binHz));

        float maxDb = -300.0f;
        for (int bin = bin0; bin <= bin1; ++bin)
            maxDb = juce::jmax (maxDb, binDb_[(size_t) bin]);

        // Un-smoothed measurement target. The presentation clock glides the
        // displayed level toward this target (dB domain only).
        barTargetDb_[b] = maxDb;
    }
}

void G10SpectrumAnalyzerComponent::onPresentationTick (double deltaSeconds)
{
    if (timingProbe_ != nullptr && timingProbe_->cacheHasUpdated())
        timingProbe_->recordPresentationEntry();

    // Presentation-only: glide the cached measured spectrum toward the
    // latest FFT targets with time-based constants, then repaint ONLY this
    // component when anything actually moved. No FFT, no FIFO access, no
    // allocation — pure display math at the display-aware rate.
    const float delta = (float) juce::jlimit (0.0005, 0.1, deltaSeconds);
    const float attackCoeff  = 1.0f - std::exp (-delta / kAttackTau);
    const float releaseCoeff = 1.0f - std::exp (-delta / kReleaseTau);

    bool changed = false;
    for (int b = 0; b < kNumBars; ++b)
    {
        // The raw calibrated target remains available below -60 dBFS for
        // measurement. Presentation cannot represent anything below its
        // declared floor, so do not accumulate invisible smoothing debt there.
        // This changes no spectrum data; it only makes equal visible states
        // share the same interpolation state.
        const float target = juce::jlimit (-kDisplayRangeDb, 0.0f,
                                           barTargetDb_[(size_t) b]);
        float& level = barLevelDb_[(size_t) b];
        level += (target - level) * (target > level ? attackCoeff : releaseCoeff);

        const float disp = juce::jlimit (0.0f, 1.0f,
                                         (level + kDisplayRangeDb) / kDisplayRangeDb);
        if (std::abs (disp - barLevels_[(size_t) b]) > 0.0005f)
        {
            barLevels_[(size_t) b] = disp;
            changed = true;
        }
    }

    if (timingProbe_ != nullptr && timingProbe_->cacheHasUpdated())
    {
        const float visiblePeak = *std::max_element (barLevels_.begin(), barLevels_.end());
        if (visiblePeak > 0.0005f)
            timingProbe_->recordVisibleLevelUpdated();
    }

    if (changed)
    {
        rebuildTraceCache();
        if (timingProbe_ != nullptr)
            timingProbe_->recordRepaintRequested();
        repaint();
    }
}

float G10SpectrumAnalyzerComponent::getBarCenterHz (int bar) const noexcept
{
    const int b = juce::jlimit (0, kNumBars - 1, bar);
    const float logMin = std::log (kMinFreqHz);
    const float logMax = std::log (kMaxFreqHz);
    const float f0 = std::exp (logMin + (logMax - logMin) * (float) b / (float) kNumBars);
    const float f1 = std::exp (logMin + (logMax - logMin) * (float) (b + 1) / (float) kNumBars);
    return std::sqrt (f0 * f1);
}

float G10SpectrumAnalyzerComponent::frequencyToX (float frequencyHz) const noexcept
{
    const float logMin = std::log (kMinFreqHz);
    const float logMax = std::log (kMaxFreqHz);
    const float clamped = juce::jlimit (kMinFreqHz, kMaxFreqHz, frequencyHz);
    return (std::log (clamped) - logMin) / (logMax - logMin) * (float) getWidth();
}

void G10SpectrumAnalyzerComponent::resized()
{
    if (eqSurface_ != nullptr)
        eqSurface_->setBounds (getLocalBounds());
    rebuildStaticCache();
    rebuildTraceCache();
}

void G10SpectrumAnalyzerComponent::rebuildStaticCache()
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    chamberClip_.clear();

    if (w <= 0.0f || h <= 0.0f)
    {
        chamberCache_ = {};
        return;
    }

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    constexpr float cornerRadius = 8.0f;
    chamberClip_.addRoundedRectangle (bounds, cornerRadius);

    chamberCache_ = juce::Image (juce::Image::ARGB, (int) w, (int) h, true);
    juce::Graphics g (chamberCache_);
    g.reduceClipRegion (chamberClip_);

    // ---- Chamber foundation ------------------------------------------------
    juce::ColourGradient base = juce::ColourGradient::vertical (
        juce::Colour (0xFF0A0710), G10Colors::analyzerVoid(), bounds);
    g.setGradientFill (base);
    g.fillRect (bounds);

    juce::ColourGradient violetWell (
        G10Colors::analyzerViolet().withAlpha (0.34f),
        w * 0.72f, h * 0.18f,
        G10Colors::analyzerViolet().withAlpha (0.0f),
        w * 1.18f, h * 0.18f, true);
    g.setGradientFill (violetWell);
    g.fillRect (bounds);

    juce::ColourGradient maroonWell (
        G10Colors::analyzerMist().withAlpha (0.28f),
        w * 0.34f, h * 0.92f,
        G10Colors::analyzerMist().withAlpha (0.0f),
        w * 0.78f, h * 0.92f, true);
    g.setGradientFill (maroonWell);
    g.fillRect (bounds);

    // ---- Plot geometry (ONE authority shared with the interactive surface) --
    // ApexEqPlotGeometry reserves axis-label space and defines the exact plot
    // rectangle; the grid, dB guides, labels, spectrum and the EQ surface all
    // use these same bounds, so visual and mouse coordinates never drift.
    const auto plot = ApexEqPlotGeometry::plotBounds (w, h);
    const float plotLeft   = plot.getX();
    const float plotRight  = plot.getRight();
    const float plotTop    = plot.getY();
    const float plotBottom = plot.getBottom();
    const float baseline   = plotBottom;
    const float plotHeight = juce::jmax (1.0f, plotBottom - plotTop);

    // Log-frequency → pixel, confined to the plot horizontal range.
    const float logMin = std::log (kMinFreqHz);
    const float logMax = std::log (kMaxFreqHz);
    auto freqToPlotX = [&] (float freqHz) -> float
    {
        const float clamped = juce::jlimit (kMinFreqHz, kMaxFreqHz, freqHz);
        const float t = (std::log (clamped) - logMin) / (logMax - logMin);
        return plotLeft + t * (plotRight - plotLeft);
    };

    // ---- Horizontal dB guide lines -----------------------------------------
    //  0, -6, -12, -18, -24, -30, -36, -42, -48, -54 dB
    constexpr int kNumDbLines = 10;
    constexpr float dbLevels[kNumDbLines] = {
        0.0f, -6.0f, -12.0f, -18.0f, -24.0f,
        -30.0f, -36.0f, -42.0f, -48.0f, -54.0f
    };
    for (int i = 0; i < kNumDbLines; ++i)
    {
        const float t = -dbLevels[i] / kDisplayRangeDb; // 0 = top, 1 = -60
        const float y = plotTop + t * plotHeight;
        const bool isMajor = (i == 0 || i == 2 || i == 4 || i == 6 || i == 8);
        const float alpha = isMajor ? 0.10f : 0.05f;
        g.setColour (G10Colors::analyzerGuide().withAlpha (alpha));
        g.drawLine (plotLeft, y, plotRight, y, 0.55f);
    }

    // ---- Vertical frequency grid lines (log-spaced) ------------------------
    // Decades plus intermediate markers — full height, very subtle.
    constexpr float freqGrid[] = {
        20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 80.0f,
        100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 800.0f,
        1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 8000.0f,
        10000.0f, 20000.0f
    };
    constexpr int kNumFreqGrid = sizeof (freqGrid) / sizeof (freqGrid[0]);
    for (int i = 0; i < kNumFreqGrid; ++i)
    {
        const float x = freqToPlotX (freqGrid[i]);
        // Decades (20, 100, 1k, 10k, 20k) get a touch more presence.
        const bool isDecade = (freqGrid[i] == 20.0f || freqGrid[i] == 100.0f
                            || freqGrid[i] == 1000.0f || freqGrid[i] == 10000.0f
                            || freqGrid[i] == 20000.0f);
        const float alpha = isDecade ? 0.08f : 0.04f;
        g.setColour (G10Colors::analyzerGuide().withAlpha (alpha));
        g.drawLine (x, plotTop, x, plotBottom, 0.45f);
    }

    // ---- Floor-level band markers (tie the graph to the faders below) ------
    constexpr float floorMarkers[10] {
        31.0f, 63.0f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };
    for (int index = 0; index < 10; ++index)
    {
        const float x = freqToPlotX (floorMarkers[index]);
        g.setColour (G10Colors::analyzerGuide().withAlpha (index == 5 ? 0.30f : 0.20f));
        g.drawLine (x, baseline - 4.0f, x, baseline + 0.5f, 0.8f);
    }

    // Baseline
    g.setColour (G10Colors::analyzerGuide().withAlpha (0.13f));
    g.drawLine (plotLeft, baseline + 0.5f, plotRight, baseline + 0.5f, 0.7f);

    // ---- Frequency labels (along the bottom) -------------------------------
    // Readable size (11 px logical); at small host scales reduce DENSITY
    // before shrinking fonts further.
    const bool smallScale = scaleHint_ < 0.75f;
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    struct FreqLabel { float hz; const char* label; };
    const FreqLabel fullFreqLabels[] {
        { 20.0f, "20 Hz" }, { 50.0f, "50" }, { 100.0f, "100" },
        { 200.0f, "200" }, { 500.0f, "500" }, { 1000.0f, "1k" },
        { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" },
        { 20000.0f, "20k" }
    };
    const FreqLabel smallFreqLabels[] {
        { 20.0f, "20" }, { 100.0f, "100" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 20000.0f, "20k" }
    };
    const auto* freqSet = smallScale ? smallFreqLabels : fullFreqLabels;
    const int freqCount = smallScale
        ? (int) (sizeof (smallFreqLabels) / sizeof (smallFreqLabels[0]))
        : (int) (sizeof (fullFreqLabels) / sizeof (fullFreqLabels[0]));
    for (int i = 0; i < freqCount; ++i)
    {
        const float x = freqToPlotX (freqSet[i].hz);
        g.setColour (G10Colors::textSecondary().withAlpha (smallScale ? 0.75f : 0.85f));
        const float tw = g.getCurrentFont().getStringWidth (freqSet[i].label);
        g.drawSingleLineText (freqSet[i].label, juce::roundToInt (x - tw * 0.5f),
                              juce::roundToInt (baseline + 8.0f));
    }

    // ---- dB labels (along the left edge) -----------------------------------
    // 10 px logical, stronger contrast, aligned with the guide lines.
    g.setFont (juce::Font (10.0f, juce::Font::plain));
    constexpr float dbLabels[] = { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f };
    constexpr int kNumDbLabels = sizeof (dbLabels) / sizeof (dbLabels[0]);
    const int dbLabelStep = smallScale ? 2 : 1; // omit -12/-36 at small scale
    for (int i = 0; i < kNumDbLabels; i += dbLabelStep)
    {
        const float db = dbLabels[i];
        const float t = -db / kDisplayRangeDb;
        const float y = plotTop + t * plotHeight;
        juce::String text = juce::String (juce::roundToInt (db));
        g.setColour (G10Colors::textSecondary().withAlpha (smallScale ? 0.65f : 0.75f));
        const float tw = g.getCurrentFont().getStringWidth (text);
        g.drawSingleLineText (text, juce::roundToInt (plotLeft - 8.0f - tw),
                              juce::roundToInt (y + 3.5f));
    }

    // ---- Shell rim + top-edge highlight ------------------------------------
    g.setColour (G10Colors::analyzerRim().withAlpha (0.64f));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    g.setColour (juce::Colour (0xFF8B627D).withAlpha (0.10f));
    g.drawLine (9.0f, 1.5f, w - 9.0f, 1.5f, 0.65f);

    // ---- Spectrum fill gradient (trace → floor, polished depth) ------------
    spectrumFillGradient_ = juce::ColourGradient (
        G10Colors::analyzerTrace().withAlpha (0.28f),
        0.0f, h * 0.18f,
        G10Colors::analyzerDeep().withAlpha (0.03f),
        0.0f, baseline, false);
}

void G10SpectrumAnalyzerComponent::rebuildTraceCache()
{
    spectrumPath_.clear();
    spectrumFillPath_.clear();

    const float w = (float) getWidth();
    const float h = (float) getHeight();
    if (w <= 0.0f || h <= 0.0f || barLevels_.empty())
        return;

    const auto plot = ApexEqPlotGeometry::plotBounds (w, h);
    const float baseline   = plot.getBottom();
    const float plotTop    = plot.getY();
    const float plotHeight = juce::jmax (1.0f, baseline - plotTop);

    const float logMin = std::log (kMinFreqHz);
    const float logMax = std::log (kMaxFreqHz);
    auto freqToPlotX = [&] (float freqHz) -> float
    {
        const float clamped = juce::jlimit (kMinFreqHz, kMaxFreqHz, freqHz);
        const float t = (std::log (clamped) - logMin) / (logMax - logMin);
        return plot.getX() + t * plot.getWidth();
    };

    float firstX = 0.0f;
    for (int bar = 0; bar < kNumBars; ++bar)
    {
        const float x = freqToPlotX (getBarCenterHz (bar));
        const float y = baseline
                      - juce::jlimit (0.0f, 1.0f, barLevels_[(size_t) bar]) * plotHeight;

        if (bar == 0)
        {
            firstX = x;
            spectrumPath_.startNewSubPath (x, y);
            spectrumFillPath_.startNewSubPath (x, baseline);
            spectrumFillPath_.lineTo (x, y);
        }
        else
        {
            spectrumPath_.lineTo (x, y);
            spectrumFillPath_.lineTo (x, y);
        }
    }

    const float lastX = freqToPlotX (getBarCenterHz (kNumBars - 1));
    spectrumFillPath_.lineTo (lastX, baseline);
    spectrumFillPath_.lineTo (firstX, baseline);
    spectrumFillPath_.closeSubPath();
}

void G10SpectrumAnalyzerComponent::paint (juce::Graphics& g)
{
    if (timingProbe_ != nullptr && timingProbe_->cacheHasUpdated())
        timingProbe_->recordPaintEntered();

    const auto bounds = getLocalBounds();
    if (chamberCache_.isValid())
        g.drawImageAt (chamberCache_, 0, 0);
    else
        g.fillAll (G10Colors::analyzerVoid());

    g.saveState();
    if (! chamberClip_.isEmpty())
        g.reduceClipRegion (chamberClip_);

    // ---- Refined spectrum: depth shadow → translucent fill → bright trace --
    // Shadow stroke (depth underneath the contour)
    g.setColour (G10Colors::analyzerDeep().withAlpha (0.38f));
    g.strokePath (spectrumPath_, juce::PathStrokeType (
        3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Translucent fill under the trace (depth, not decoration)
    g.setGradientFill (spectrumFillGradient_);
    g.fillPath (spectrumFillPath_);

    // Thin, sharp readable contour
    g.setColour (G10Colors::analyzerTrace().withAlpha (0.94f));
    g.strokePath (spectrumPath_, juce::PathStrokeType (
        1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Subtle luminous highlight at the very top edge of the trace
    g.setColour (juce::Colour (0xFFCC5A6A).withAlpha (0.12f));
    g.strokePath (spectrumPath_, juce::PathStrokeType (
        1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    if (timingProbe_ != nullptr && timingProbe_->cacheHasUpdated())
    {
        const float visiblePeak = *std::max_element (barLevels_.begin(), barLevels_.end());
        if (visiblePeak > 0.0005f)
            timingProbe_->recordRenderedSpectrum();
    }
}

} // namespace G10
} // namespace APEX

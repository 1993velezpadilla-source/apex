#include "G10Editor.h"
#include "G10LookAndFeel.h"
#include "../G10Core/G10Processor.h"

namespace APEX {
namespace G10 {

namespace
{
// Deliberately irregular, sparse lifetimes: no opening cascade and no visible
// marching pattern. Each eye blinks only every ~29-43 seconds; the first
// events are distributed over the first half-minute.
constexpr int kFirstBlinkMs[10] {
    4800, 17700, 9300, 26400, 13800, 32100, 21500, 6900, 28900, 15400
};
constexpr int kBlinkIntervalMs[10] {
    31000, 37000, 29000, 41000, 34000, 43000, 32000, 39000, 35000, 40000
};
}

// ============================================================================
// Content — the transformed child that owns the logical 1000x612 UI.
// It carries the uniform scale transform so the editor itself never needs one
// (JUCE owns the editor-level host scale transform — see
// AudioProcessorEditor::editorResized / setScaleFactor). Rendering AND mouse
// hit-testing scale together because JUCE maps input through the inverse
// transform.
// ============================================================================

class G10Editor::Content final : public juce::Component
{
public:
    explicit Content (G10Editor& owner) : owner_ (owner) {}

    void paint (juce::Graphics& g) override { owner_.paintContent (g); }

private:
    G10Editor& owner_;
};

juce::Component* G10Editor::getContent() noexcept { return content_.get(); }

G10Editor::G10Editor (G10Processor& processor)
    : juce::AudioProcessorEditor (processor),
      processor_ (processor),
      content_ (std::make_unique<Content> (*this))
{
    addAndMakeVisible (content_.get());

    // Host resize participation: the OUTER editor is host-resizable within
    // sensible min/max limits and accepts ANY host-negotiated aspect ratio —
    // the external host's plugin area must never be forced to 1000:612 (real
    // REAPER UX: the area fills whatever size the user chooses). The 1000:612
    // design aspect is preserved ONLY on the internal transformed Content
    // child (uniform min-fit scale, centered); the editor's own paint fills
    // the complete background with the G10 shell, so leftover letterbox space
    // belongs to G10, never to the host.
    setSize (G10Layout::kWidth, G10Layout::kHeight);
    setResizable (true, false);
    setResizeLimits (kMinEditorW, kMinEditorH, kMaxEditorW, kMaxEditorH);

    // ---- Analyzer -----------------------------------------------------------
    content_->addAndMakeVisible (analyzer_);
    analyzer_.setBounds (G10Layout::analyzer());
    analyzer_.setFifo (processor.getAnalyzerFifo());
    analyzer_.setSampleRate (processor.getSampleRate());

    // ---- Mini Clean EQ interactive surface (overlay on the analyzer) --------
    // The surface is parameter-agnostic; the model binds it to the canonical
    // G10Parameter objects through the host-automation gesture pipeline.
    eqSurface_ = std::make_unique<ApexInteractiveEQSurface>();
    {
        using Surface = ApexInteractiveEQSurface;
        using MiniEq = G10MiniEqCore;
        Surface::Model model;
        model.minFreqHz = G10SpectrumAnalyzerComponent::kMinFreqHz;
        model.maxFreqHz = G10SpectrumAnalyzerComponent::kMaxFreqHz;
        model.normToFreq = &MiniEq::bellHzFromNorm;
        model.freqToNorm = &MiniEq::bellNormFromHz;
        model.gainNormToDb = &MiniEq::bellGainDbFromNorm;
        model.dbToGainNorm = &MiniEq::bellNormFromGainDb;
        model.qNormToQ = &MiniEq::bellQFromNorm;
        model.qToQNorm = &MiniEq::bellNormFromQ;
        model.qMin = MiniEq::kBellMinQ;
        model.qMax = MiniEq::kBellMaxQ;
        model.hpfNormToHz = &MiniEq::hpfHzFromNorm;
        model.lpfNormToHz = &MiniEq::lpfHzFromNorm;
        model.maxBells = MiniEq::kMaxBells;
        model.defaultQNorm = MiniEq::bellNormFromQ (MiniEq::kBellDefaultQ);
        model.nominalMagnitude = [this] (float hz, float sr)
        {
            MiniEq::Targets t;
            t.hpfNorm = processor_.getG10Parameter (kHpf)->getValue();
            t.lpfNorm = processor_.getG10Parameter (kLpf)->getValue();
            t.bypass = false;
            for (int b = 0; b < MiniEq::kMaxBells; ++b)
            {
                const int base = kBell1Enabled + b * 4;
                t.bell[b].enabled = processor_.getG10Parameter (base)->getBool();
                t.bell[b].freqNorm = processor_.getG10Parameter (base + 1)->getValue();
                t.bell[b].gainDb = processor_.getG10Parameter (base + 2)->getUnitsValue();
                t.bell[b].qNorm = processor_.getG10Parameter (base + 3)->getValue();
            }
            return processor_.getMiniEq().getNominalMagnitude (hz, sr, t);
        };

        auto paramSetter = [] (G10Parameter* p, float value, bool start, bool end)
        {
            if (p == nullptr)
                return;
            if (start) p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            if (end) p->endChangeGesture();
        };

        model.getHpfNorm = [this] { return processor_.getG10Parameter (kHpf)->getValue(); };
        model.setHpfNorm = [this, paramSetter] (float v, bool s, bool e)
        { paramSetter (processor_.getG10Parameter (kHpf), v, s, e); };
        model.getLpfNorm = [this] { return processor_.getG10Parameter (kLpf)->getValue(); };
        model.setLpfNorm = [this, paramSetter] (float v, bool s, bool e)
        { paramSetter (processor_.getG10Parameter (kLpf), v, s, e); };

        for (int b = 0; b < MiniEq::kMaxBells; ++b)
        {
            const int base = kBell1Enabled + b * 4;
            const int bypassIdx = kBell1Bypass + b; // v3 individual bypass
            auto& bell = model.bells[b];
            bell.getEnabled  = [this, base] { return processor_.getG10Parameter (base)->getValue(); };
            bell.getFreqNorm = [this, base] { return processor_.getG10Parameter (base + 1)->getValue(); };
            bell.getGainNorm = [this, base] { return processor_.getG10Parameter (base + 2)->getValue(); };
            bell.getQNorm    = [this, base] { return processor_.getG10Parameter (base + 3)->getValue(); };
            bell.setEnabled  = [this, base, paramSetter] (float v, bool s, bool e)
            { paramSetter (processor_.getG10Parameter (base), v, s, e); };
            bell.setFreqNorm = [this, base, paramSetter] (float v, bool s, bool e)
            { paramSetter (processor_.getG10Parameter (base + 1), v, s, e); };
            bell.setGainNorm = [this, base, paramSetter] (float v, bool s, bool e)
            { paramSetter (processor_.getG10Parameter (base + 2), v, s, e); };
            bell.setQNorm    = [this, base, paramSetter] (float v, bool s, bool e)
            { paramSetter (processor_.getG10Parameter (base + 3), v, s, e); };
            bell.getBypassed = [this, bypassIdx] { return processor_.getG10Parameter (bypassIdx)->getValue(); };
            bell.setBypassed = [this, bypassIdx, paramSetter] (float v, bool s, bool e)
            { paramSetter (processor_.getG10Parameter (bypassIdx), v, s, e); };
        }

        model.enabledBellCount = [this]
        {
            int count = 0;
            for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
                if (processor_.getG10Parameter (kBell1Enabled + b * 4)->getBool())
                    ++count;
            return count;
        };
        model.createBellAt = [this] (float freqNorm, float gainNorm)
        {
            for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
            {
                const int base = kBell1Enabled + b * 4;
                auto* enabled = processor_.getG10Parameter (base);
                if (enabled->getBool())
                    continue;
                enabled->beginChangeGesture();
                enabled->setValueNotifyingHost (1.0f);
                enabled->endChangeGesture();
                auto* freq = processor_.getG10Parameter (base + 1);
                freq->beginChangeGesture();
                freq->setValueNotifyingHost (freqNorm);
                freq->endChangeGesture();
                // Gain from the double-click Y (X → Frequency, Y → Gain).
                auto* gain = processor_.getG10Parameter (base + 2);
                gain->beginChangeGesture();
                gain->setValueNotifyingHost (gainNorm);
                gain->endChangeGesture();
                auto* q = processor_.getG10Parameter (base + 3);
                q->beginChangeGesture();
                q->setValueNotifyingHost (G10MiniEqCore::bellNormFromQ (G10MiniEqCore::kBellDefaultQ));
                q->endChangeGesture();
                return true;
            }
            return false;
        };
        model.removeBell = [this] (int bellIndex)
        {
            const int base = kBell1Enabled + bellIndex * 4;
            if (auto* enabled = processor_.getG10Parameter (base))
            {
                enabled->beginChangeGesture();
                enabled->setValueNotifyingHost (0.0f);
                enabled->endChangeGesture();
            }
        };

        eqSurface_->setModel (std::move (model));
        eqSurface_->setSampleRate (processor.getSampleRate());
        analyzer_.setInteractiveSurface (eqSurface_.get());
    }

    // ---- Band faders ----------------------------------------------------------
    for (int b = 0; b < 10; ++b)
    {
        faders_[b] = std::make_unique<G10BandFaderComponent> (
            processor.getG10Parameter (kBand31 + b), b);
        content_->addAndMakeVisible (*faders_[b]);
        faders_[b]->setBounds (G10Layout::bandCenterX (b) - 46, G10Layout::kFaderTop - 34,
                               92, G10Layout::kGainTextY + 16 - (G10Layout::kFaderTop - 34));
    }

    // ---- Control strip ---------------------------------------------------------
    // A tighter, intentional cluster around BYPASS (symmetric 5-slot grid,
    // preparing for HPF / LPF later). Slots at logical centres 200 / 350 /
    // 500 / 650 / 800; only INPUT / BYPASS / OUTPUT are active today.
    inputKnob_ = std::make_unique<G10RotaryKnobComponent> (
        processor.getG10Parameter (kInput), "INPUT");
    outputKnob_ = std::make_unique<G10RotaryKnobComponent> (
        processor.getG10Parameter (kOutput), "OUTPUT");
    content_->addAndMakeVisible (*inputKnob_);
    content_->addAndMakeVisible (*outputKnob_);
    inputKnob_->setBounds (145, G10Layout::kStripTop, 110, G10Layout::kStripBottom - G10Layout::kStripTop);
    outputKnob_->setBounds (745, G10Layout::kStripTop, 110,
                            G10Layout::kStripBottom - G10Layout::kStripTop);

    // ---- Compact vertical level meters (INPUT left / OUTPUT right) ---------
    // Symmetric around the BYPASS centre, beside the knobs with breathing
    // room (7 px), clear of the analyzer / fader column / BYPASS.  The
    // instrument-style body (48x92) aligns to the strip top and extends a
    // little below the knob row; the fader column above blocks upward
    // extension, so the meters read as tall gain-staging instruments.
    inputMeter_ = std::make_unique<G10LevelMeterComponent>();
    outputMeter_ = std::make_unique<G10LevelMeterComponent>();
    content_->addAndMakeVisible (*inputMeter_);
    content_->addAndMakeVisible (*outputMeter_);
    inputMeter_->setBounds (90, G10Layout::kStripTop,
                            G10LevelMeterComponent::kMeterW,
                            G10LevelMeterComponent::kMeterH);
    outputMeter_->setBounds (862, G10Layout::kStripTop,
                             G10LevelMeterComponent::kMeterW,
                             G10LevelMeterComponent::kMeterH);

    // Toggle: BYPASS only (native button, parameter-driven). ANALOG and
    // QUALITY are canonical/internal and have no GUI controls.
    auto makeToggleButton = [this] (juce::TextButton& b, const juce::String& label)
    {
        b.setButtonText (label);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonColourId, G10Colors::ctlInactive());
        b.setColour (juce::TextButton::buttonOnColourId, G10Colors::ctlAccent());
        b.setLookAndFeel (&lookAndFeel_);
        content_->addAndMakeVisible (b);
    };
    makeToggleButton (bypassButton_, "BYPASS");

    const int stripY = G10Layout::kStripTop + 14;
    const int stripH = 34;
    bypassButton_.setBounds (455, stripY, 90, stripH);

    // Bind the bypass toggle to its parameter (canonical setValue path with
    // host gesture semantics: begin -> notify -> end, balanced).
    bypassButton_.onClick = [this]
    {
        if (auto* p = processor_.getG10Parameter (kBypass))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (bypassButton_.getToggleState() ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    };

    // ---- Parameter listener ----------------------------------------------------
    processor_.addListener (this);

    // ---- Blink scheduler: sparse, irregular first blink per eye -----------------
    for (int b = 0; b < 10; ++b)
        blinkNextMs_[b] = kFirstBlinkMs[b];

    refreshAllFromParameters();
    startSchedulerIfVisible();
}

G10Editor::~G10Editor()
{
    stopScheduler();
    processor_.setAnalyzerActive (false);
    analyzer_.setPresentationActive (false);
    analyzer_.resetForVisibilityBoundary();
    processor_.removeListener (this);
}

// ============================================================================
// Scheduler
// ============================================================================

void G10Editor::startSchedulerIfVisible()
{
    if (isVisible() && ! isTimerRunning())
        startTimerHz (30);
}

void G10Editor::stopScheduler()
{
    if (isTimerRunning())
        stopTimer();
}

void G10Editor::visibilityChanged()
{
    if (isVisible())
    {
        // Visible: enable the analyzer producer, the UI scheduler and the
        // display-aware presentation subscription (spectrum + blink squash
        // render at the display rate; the FFT stays at ~30 Hz).
        processor_.setAnalyzerActive (false);
        analyzer_.resetForVisibilityBoundary();
        processor_.setAnalyzerActive (true);
        analyzer_.setPresentationActive (true);
        startSchedulerIfVisible();
        refreshAllFromParameters();
    }
    else
    {
        // Hidden: suspend blinking/analyzer ticks, release the presentation
        // subscription (no interpolation, no repaints) AND stop the audio
        // thread from filling the analyzer FIFO (no editor visible -> no
        // pushes). A hidden G10 costs almost nothing.
        processor_.setAnalyzerActive (false);
        stopScheduler();
        analyzer_.setPresentationActive (false);
        analyzer_.resetForVisibilityBoundary();
        for (int b = 0; b < 10; ++b)
            if (faders_[b] != nullptr)
                faders_[b]->cancelBlink();
    }
}

void G10Editor::timerCallback()
{
    schedulerElapsedMs_ += 33;

    // Consume parameter-change dirty flags on the message thread.
    consumeDirtyFlags();

    // Analyzer tick (GUI thread FFT; audio thread only pushed samples).
    // Refresh the sample rate every tick: a host rate change discards the
    // partially accumulated window inside setSampleRate, so windows computed
    // with stale frequency mapping are never displayed.
    analyzer_.setSampleRate (processor_.getSampleRate());
    analyzer_.tick();

    // Mini Clean EQ surface: re-read the parameter model so host automation
    // moves nodes / the nominal curve without any mouse interaction, and the
    // curve tracks live edits at the measurement cadence.
    if (eqSurface_ != nullptr)
    {
        eqSurface_->setSampleRate (processor_.getSampleRate());
        eqSurface_->refresh();
    }

    // Level meters: read the audio-thread per-block peaks (relaxed atomic
    // handoff) and advance the display ballistics at the measurement cadence.
    if (inputMeter_ != nullptr)
        inputMeter_->setLevelDb (processor_.getInputMeterDb());
    if (outputMeter_ != nullptr)
        outputMeter_->setLevelDb (processor_.getOutputMeterDb());

    // Blink scheduler: sparse, staggered DECISIONS only. The ~220 ms squash
    // animation itself runs on the display-aware presentation clock inside
    // each eye (eye-local repaints), so blinks stay smooth at 60-144 Hz with
    // no permanent animation workload.
    for (int b = 0; b < 10; ++b)
    {
        if (schedulerElapsedMs_ < blinkNextMs_[b])
            continue;

        faders_[b]->startBlink();
        blinkNextMs_[b] = schedulerElapsedMs_ + kBlinkIntervalMs[b];
    }
}

// ============================================================================
// Parameter listener
// ============================================================================

void G10Editor::audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float)
{
    // AudioProcessorListener callbacks are NOT guaranteed to arrive on the
    // message thread. Only record the change atomically; the 30 Hz timer
    // consumes the flags and repaints on the GUI thread. No locks, no
    // allocation, no GUI calls here.
    const int semanticIndex = processor_.getSemanticParameterIndex (parameterIndex);
    if (semanticIndex >= 0 && semanticIndex < kNumParams)
        dirtyFlags_.fetch_or (1u << (uint32_t) semanticIndex, std::memory_order_relaxed);
}

void G10Editor::consumeDirtyFlags()
{
    const uint32_t dirty = dirtyFlags_.exchange (0, std::memory_order_relaxed);
    if (dirty == 0)
        return;

    for (int i = 0; i < kNumParams; ++i)
        if ((dirty & (1u << (uint32_t) i)) != 0)
            refreshControl (i);
}

void G10Editor::refreshControl (int parameterIndex)
{
    if (parameterIndex >= kBand31 && parameterIndex <= kBand16k)
    {
        const int b = parameterIndex - kBand31;
        if (faders_[b] != nullptr)
            faders_[b]->refreshFromParameter();
    }
    else if (parameterIndex == kInput)
    {
        if (inputKnob_ != nullptr)
            inputKnob_->refreshFromParameter();
    }
    else if (parameterIndex == kOutput)
    {
        if (outputKnob_ != nullptr)
            outputKnob_->refreshFromParameter();
    }
    else if (parameterIndex == kBypass)
    {
        bypassButton_.setToggleState (processor_.getG10Parameter (kBypass)->getBool(), juce::dontSendNotification);
    }
}

void G10Editor::refreshAllFromParameters()
{
    for (int b = 0; b < 10; ++b)
        if (faders_[b] != nullptr)
            faders_[b]->refreshFromParameter();

    if (inputKnob_ != nullptr)
        inputKnob_->refreshFromParameter();
    if (outputKnob_ != nullptr)
        outputKnob_->refreshFromParameter();

    bypassButton_.setToggleState (processor_.getG10Parameter (kBypass)->getBool(), juce::dontSendNotification);
}

// ============================================================================
// Painting / layout
// ============================================================================

void G10Editor::paint (juce::Graphics& g)
{
    // Letterbox shell: the full (possibly scaled) editor bounds get the shell
    // gradient so the area around the scaled content matches the design.
    const juce::Rectangle<int> bounds = getLocalBounds();
    juce::ColourGradient shell = juce::ColourGradient::vertical (
        G10Colors::shellInner(), G10Colors::shellOuter(), bounds.toFloat());
    g.setGradientFill (shell);
    g.fillRect (bounds);
}

void G10Editor::paintContent (juce::Graphics& g)
{
    // All geometry is in the logical 1000x612 space; the content child's
    // uniform transform scales it to the current editor size.
    constexpr int kLogicalWidth = G10Layout::kWidth;

    // Header: logo mark + title + subtitle.
    {
        g.setColour (G10Colors::ctlAccent());
        g.fillEllipse (juce::Rectangle<float> (27.0f, 21.0f, 18.0f, 18.0f));

        g.setFont (G10Fonts::title());
        g.setColour (G10Colors::textPrimary());
        g.drawText ("G10", juce::Rectangle<float> (70.0f, 26.0f, 120.0f, 30.0f),
                    juce::Justification::centredLeft);

        g.setFont (G10Fonts::subtitle());
        g.setColour (G10Colors::textSecondary());
        g.drawText ("APEX GRAPHIC EQUALIZER", juce::Rectangle<float> (70.0f, 50.0f, 300.0f, 12.0f),
                    juce::Justification::centredLeft);

        g.setFont (G10Fonts::micro());
        g.setColour (G10Colors::textDim());
        g.drawText ("10-BAND ANALOG-STYLE EQ  |  POST-EQ ANALYZER",
                    juce::Rectangle<float> (kLogicalWidth - 320.0f, 30.0f, 300.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // Panel separators.
    g.setColour (G10Colors::panelLine());
    g.drawHorizontalLine (G10Layout::headerBottom, 0.0f, (float) kLogicalWidth);
    g.drawHorizontalLine (G10Layout::kStripTop - 8, 0.0f, (float) kLogicalWidth);

    // Passive identity label (NOT interactive): the APEX color circuit is
    // canonical — always active, no user control.
    g.setFont (G10Fonts::micro());
    g.setColour (G10Colors::textDim());
    g.drawText ("DISCRETE \xE2\x80\xA2 IRON",
                juce::Rectangle<float> (400.0f, G10Layout::kStripTop + 52.0f, 200.0f, 12.0f),
                juce::Justification::centred);
}

void G10Editor::resized()
{
    if (content_ == nullptr)
        return;

    // Uniform min-fit scale around the logical 1000x612 design space.
    // The Content child keeps its LOGICAL bounds; only the transform scales —
    // this prevents the double-scale bug where scaled bounds × transform
    // squared produces a larger final area than the editor and overflows the
    // visible plugin viewport in DPI-scaled hosts. JUCE's getLocalArea and
    // hit-testing correctly account for the transform, so rendering, host
    // size negotiation, and mouse coordinates all agree.
    const float scale = juce::jmin (getWidth()  / (float) G10Layout::kWidth,
                                    getHeight() / (float) G10Layout::kHeight);
    const int renderedW = juce::jmax (1, juce::roundToInt (G10Layout::kWidth  * scale));
    const int renderedH = juce::jmax (1, juce::roundToInt (G10Layout::kHeight * scale));

    content_->setTransform (juce::AffineTransform::scale (scale));
    content_->setBounds ((getWidth()  - renderedW) / 2,
                         (getHeight() - renderedH) / 2,
                         G10Layout::kWidth, G10Layout::kHeight);

    contentScale_ = scale;
    // Responsive analyzer axis-label density (reduced set at small scales).
    analyzer_.setScaleHint (scale);
}

} // namespace G10
} // namespace APEX

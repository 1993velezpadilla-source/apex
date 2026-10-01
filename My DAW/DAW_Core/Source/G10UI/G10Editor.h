#pragma once
#include <JuceHeader.h>
#include "G10SpectrumAnalyzerComponent.h"
#include "G10BandFaderComponent.h"
#include "G10RotaryKnobComponent.h"
#include "G10LevelMeterComponent.h"
#include "../EQCore/ApexInteractiveEQSurface.h"

namespace APEX {
namespace G10 {

class G10Processor;

// ============================================================================
// G10Editor — the native APEX G10 plugin editor (1000x612 logical design).
//
// Layout (scaled 0.625 from the approved 1600x980 HTML blueprint):
//   header (logo/title) -> analyzer -> 10 band faders -> control strip.
//
// RESIZE PARTICIPATION (host contract, JUCE 8.0.12):
//   The OUTER editor is host-resizable (isResizable() == true) within
//   explicit min/max limits and accepts ANY host-negotiated aspect ratio —
//   the external host's plugin area is never forced to 1000:612. The
//   1000:612 design aspect is preserved ONLY on the internal Content child
//   via a uniform min-fit scale transform (rendering AND mouse hit-testing
//   scale together; JUCE maps input through the inverse transform), centered
//   inside the editor. The editor itself never carries a transform — JUCE
//   owns the editor-level hostScaleTransform (see
//   AudioProcessorEditor::editorResized and setScaleFactor). The editor's
//   paint fills the complete background with the G10 shell gradient, so all
//   leftover letterbox space belongs to G10, never to the host (no white or
//   unpainted host gaps).
//
// Scheduler contract: ONE editor-level Timer drives the analyzer MEASUREMENT
// (~30 Hz FFT) and the eye-blink DECISIONS. The visual PRESENTATION of both
// (spectrum interpolation + the ~220 ms blink squash) runs on APEX's
// display-aware ApexPresentationClock (60-144 Hz), so G10 never caps the DAW
// to 30 FPS and never creates a timer storm. Eyes blink sparsely and
// staggered; during a blink ONLY the blinking eye's local bounds are
// repainted. When the editor is hidden the timer is stopped, the analyzer
// releases its presentation subscription and the audio thread stops pushing
// (no analyzer work, no blinking, no repaints).
//
// Parameter contract: every control edits through G10Parameter::setValue()
// (the canonical JUCE pipeline). The editor implements AudioProcessorListener
// so automation / host changes repaint the matching control.
//
// Thread contract: everything here runs on the message thread. The audio
// thread only ever sees the analyzer FIFO push (gated by analyzerActive_).
// ============================================================================

class G10Editor final : public juce::AudioProcessorEditor,
                        public juce::Timer,
                        public juce::AudioProcessorListener
{
public:
    explicit G10Editor (G10Processor& processor);
    ~G10Editor() override;

    // ---- Resize participation (host contract) ------------------------------
    static constexpr int kMinEditorW = 500;
    static constexpr int kMinEditorH = 306;
    static constexpr int kMaxEditorW = 2000;
    static constexpr int kMaxEditorH = 1224;

    /** The uniform scale currently applied to the logical 1000x612 content. */
    float getContentScale() const noexcept { return contentScale_; }

    /** The transformed content child (logical design space). Defined in the
        .cpp after Content is complete. */
    juce::Component* getContent() noexcept;

    /** The interactive Mini Clean EQ surface (owned by this editor). */
    ApexInteractiveEQSurface* getAnalyzerSurface() noexcept { return eqSurface_.get(); }

    /** The band fader for a musical band (0..9 = DEEP..AIR). */
    G10BandFaderComponent* getBandFader (int bandIndex) noexcept
    {
        return (bandIndex >= 0 && bandIndex < 10) ? faders_[bandIndex].get() : nullptr;
    }

    /** INPUT level meter (beside the INPUT knob). */
    G10LevelMeterComponent* getInputMeter() noexcept { return inputMeter_.get(); }

    /** OUTPUT level meter (beside the OUTPUT knob). */
    G10LevelMeterComponent* getOutputMeter() noexcept { return outputMeter_.get(); }

    // ---- AudioProcessorEditor ---------------------------------------------
    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- Timer (single editor-level scheduler) -----------------------------
    void timerCallback() override;

    // ---- AudioProcessorListener --------------------------------------------
    void audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float newValue) override;
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}

    // ---- Visibility ----------------------------------------------------------
    void visibilityChanged() override;

private:
    class Content;
    friend class Content;

    void startSchedulerIfVisible();
    void stopScheduler();
    void updateBlinkScheduler();
    void refreshAllFromParameters();
    /** Paints the header/separators/identity in the logical 1000x612 space
        (called by the transformed content child). */
    void paintContent (juce::Graphics&);

    G10Processor& processor_;

    std::unique_ptr<Content> content_;
    float contentScale_ = 1.0f;

    G10SpectrumAnalyzerComponent analyzer_;
    std::unique_ptr<ApexInteractiveEQSurface> eqSurface_;
    std::unique_ptr<G10BandFaderComponent> faders_[10];
    std::unique_ptr<G10RotaryKnobComponent> inputKnob_;
    std::unique_ptr<G10RotaryKnobComponent> outputKnob_;
    std::unique_ptr<G10LevelMeterComponent> inputMeter_;
    std::unique_ptr<G10LevelMeterComponent> outputMeter_;

    // Control strip: INPUT knob | BYPASS toggle | OUTPUT knob. The APEX
    // color (D1B + I1) is canonical (always active) and quality is internal
    // (realtime 2x / offline 4x), so ANALOG/QUALITY have no GUI controls.
    juce::TextButton bypassButton_;
    juce::LookAndFeel_V4 lookAndFeel_;

    // Blink scheduler state (GUI thread only). The scheduler only DECIDES
    // when a blink starts; the ~220 ms animation runs on the presentation
    // clock inside G10EyeHandleComponent.
    int blinkNextMs_[10] = {};
    int schedulerElapsedMs_ = 0;

    // Parameter-change dirty flags. AudioProcessorListener callbacks are NOT
    // guaranteed to arrive on the message thread, so parameterChanged() only
    // sets an atomic bit; the 30 Hz timer consumes the flags and repaints on
    // the GUI thread. No locks, no allocation in the callback.
    std::atomic<uint32_t> dirtyFlags_ { 0 };
    void consumeDirtyFlags();
    void refreshControl (int parameterIndex);
};

} // namespace G10
} // namespace APEX

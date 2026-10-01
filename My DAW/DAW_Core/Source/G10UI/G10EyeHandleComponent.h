#pragma once
#include <JuceHeader.h>
#include "../UICore/ApexPresentationClock.h"

namespace APEX {
namespace G10 {

// ============================================================================
// G10EyeHandleComponent — the G10 fader handle: a stylized eye that blinks.
//
// The eye is a sharp-cornered, predatory almond rather than a circular knob.
// Its visible artwork stays compact inside a larger 24 px interaction corridor:
// restrained aura/lid, pale sclera, band-colored iris and slit pupil.
// Blinking is a vertical squash around the eye centre.
//
// Blink contract (sparse + display-aware):
//   - The editor's single ~30 Hz scheduler only DECIDES when a blink starts
//     (startBlink()). The ~220 ms squash animation itself runs on the
//     display-aware ApexPresentationClock (60-144 Hz), so blinks are smooth
//     on any display without any permanent animation workload.
//   - Between blinks the eye is completely static: no timer, no repaint.
//   - Repaint contract: this component repaints ONLY its own bounds.
// ============================================================================

class G10EyeHandleComponent final : public juce::Component,
                                    public DAW::ApexPresentationClock::TickReceiver
{
public:
    G10EyeHandleComponent (int bandIndex);
    ~G10EyeHandleComponent() override;

    /** Sparse blink trigger (editor scheduler, GUI thread only). Starts one
        ~220 ms blink animated by the presentation clock. */
    void startBlink() noexcept;

    /** Immediately end any in-progress blink (e.g. editor hidden). */
    void cancelBlink() noexcept;

    /** Manual override: 0 = fully open, 1 = fully closed. GUI thread only. */
    void setBlinkProgress (float p) noexcept
    {
        blink_ = juce::jlimit (0.0f, 1.0f, p);
        repaint();
    }

    float getBlinkProgress() const noexcept { return blink_; }

    void paint (juce::Graphics& g) override;

    /** Presentation clock (message thread, display rate): animates the blink
        while one is active; otherwise a cheap no-op. */
    void onPresentationTick (double deltaSeconds) override;

private:
    const int bandIndex_;
    float blink_ = 0.0f; // 0 open .. 1 closed
    bool blinking_ = false;
    double blinkStartMs_ = 0.0;
    static constexpr double kBlinkDurationMs = 220.0;
};

} // namespace G10
} // namespace APEX

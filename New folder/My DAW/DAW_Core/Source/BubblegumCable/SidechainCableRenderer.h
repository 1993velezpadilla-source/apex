#pragma once

#include <JuceHeader.h>
#include <vector>
#include <functional>

// =====================================================================================
// SidechainCableRenderer
//
// Draws an animated chain-style cable between two points (the bubblegum dots) and
// animates it with continuous link flow + per-trigger bright pulses.
//
// This file is the rendering nucleo only. State lives in SidechainCableAnimator.
// Trigger detection (calling animator.firePulse()) lives in SidechainTriggerDetector,
// which you feed with sample-rate level data from the source track's tap point.
//
// Integration sketch:
//
//   class RoutingCanvas : public juce::Component, public juce::Timer {
//       std::unordered_map<SendId, SidechainCableAnimator> animators;
//       SidechainTriggerDetector triggers;
//
//       void timerCallback() override {
//           const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
//           for (auto& [id, anim] : animators) anim.advance(now);
//
//           // pull current levels from your audio engine and fire pulses
//           for (auto& send : sidechainSends) {
//               float level = engine.getTapLevel(send.sourceTrackId, send.tapPoint);
//               if (triggers.process(send.id, level, now))
//                   animators[send.id].firePulse();
//           }
//           repaint();
//       }
//
//       void paint(juce::Graphics& g) override {
//           for (auto& send : sidechainSends) {
//               if (send.state == SendState::ACTIVE || send.state == SendState::INACTIVE) {
//                   auto path = buildCablePath(send.sourcePoint, send.destPoint);
//                   SidechainCableRenderer::draw(g, path, animators[send.id]);
//               }
//           }
//       }
//   };
//
// =====================================================================================

namespace bg::sidechain
{
    // -------------------------------------------------------------------------
    // Style — chain color is blue to distinguish sidechain cables from audio
    // sends. "bright" is the pulse highlight; "base" is the resting chain.
    // -------------------------------------------------------------------------
    struct Style
    {
        juce::Colour blueBase   { 0xFF3A7BD5 };  // resting chain
        juce::Colour blueBright { 0xFFB8D8FF };  // pulse highlight

        float thickness    = 1.5f;    // base cable thickness
        float flowSpeed    = 22.0f;   // px/sec along the cable when active
        float pulseDurationSec = 0.5f;
        float pulseGlowRadius  = 0.1f; // fraction of cable length the pulse illuminates

        // ── Dynamic chain properties (scale with thickness) ──────────────────
        // Scales maintain chain appearance across the full 0.3–4.0 px range.
        float getLinkSpacing() const noexcept
        {
            // Base: 12px at 1.5px thickness → ~8x thickness
            return juce::jmax(6.0f, thickness * 8.0f);
        }

        float getRingRx() const noexcept
        {
            // Base: 8px at 1.5px → ~5.3x thickness
            // Slight taper keeps large chains from looking bloated
            return juce::jmax(2.0f, thickness * 5.3f * (0.7f + 0.3f / (thickness + 0.5f)));
        }

        float getFaceRy() const noexcept
        {
            // Base: 4.5px at 1.5px → ~3x thickness
            return juce::jmax(1.0f, thickness * 3.0f);
        }

        float getEdgeRy() const noexcept
        {
            // Base: 1.4px at 1.5px → ~0.93x thickness
            // Edge links stay thinner to create depth illusion
            return juce::jmax(0.4f, thickness * 0.93f);
        }

        float getStrokeWidth() const noexcept
        {
            // Base: 1.6px at 1.5px → ~1.07x thickness
            // Stroke scales slightly to maintain definition
            return juce::jmax(0.5f, thickness * 1.07f);
        }
    };

    // -------------------------------------------------------------------------
    // Animator — per-cable mutable state. Cheap to construct, owned by whatever
    // owns the send (RoutingCanvas in the sketch above).
    // -------------------------------------------------------------------------
    class SidechainCableAnimator
    {
    public:
        void setActive (bool active) noexcept       { isActive = active; }
        void setVisible (bool visible) noexcept     { isVisible = visible; }
        bool active() const noexcept                { return isActive; }
        bool visible() const noexcept               { return isVisible; }

        // Call when the trigger detector says the source just hit.
        void firePulse() noexcept                   { lastPulseStart = lastAdvance; }

        // Call once per frame from your timer with monotonic seconds.
        void advance (double nowSeconds) noexcept
        {
            lastAdvance = nowSeconds;
            if (! isVisible) return;          // honors offscreenButtonsVisible rule

            if (startTime < 0.0) startTime = nowSeconds;
            elapsedSec = (float)(nowSeconds - startTime);
        }

        float getFlowOffsetPx (float spacing, int totalLinks, float flowSpeed) const noexcept
        {
            if (! isActive) return 0.0f;       // freeze flow when send is inactive
            return std::fmod (elapsedSec * flowSpeed, spacing * (float) totalLinks);
        }

        // Returns -1 if no pulse currently traveling, else 0..1 along cable.
        float getPulsePosition (float pulseDurationSec) const noexcept
        {
            if (lastPulseStart < 0.0) return -1.0f;
            float age = (float)(lastAdvance - lastPulseStart);
            if (age < 0.0f || age >= pulseDurationSec) return -1.0f;
            return age / pulseDurationSec;
        }

    private:
        bool   isActive       = true;
        bool   isVisible      = true;
        double startTime      = -1.0;
        double lastAdvance    = 0.0;
        double lastPulseStart = -1.0;
        float  elapsedSec     = 0.0f;
    };

    // -------------------------------------------------------------------------
    // Renderer — pure draw function. No state. Call from your component's paint().
    // -------------------------------------------------------------------------
    class SidechainCableRenderer
    {
    public:
        static void draw (juce::Graphics& g,
                          const juce::Path& cablePath,
                          const SidechainCableAnimator& anim,
                          const Style& style = {});

        // Helper: build the default Bezier path between two dots with a sag
        // proportional to distance. Replace with your routing graph's path
        // calculator if you already have one.
        static juce::Path buildDefaultPath (juce::Point<float> source,
                                            juce::Point<float> dest);
    };

    // -------------------------------------------------------------------------
    // Trigger detector — converts a stream of level samples into pulse events.
    // Per-send instance. Threshold + rate-of-rise + cooldown so it fires on
    // transients (kick hits) and not on sustained signal.
    // -------------------------------------------------------------------------
    class SidechainTriggerDetector
    {
    public:
        struct Config
        {
            float thresholdDb        = -40.0f;  // signal must exceed this
            float minRiseDbPerCall   = 6.0f;    // and rise this much vs. last call
            double minIntervalSec    = 0.08;    // ignore retrigger within this window
        };

        // Returns true if a pulse should fire for this send right now.
        // levelLinear is post-tap-point peak (pre-fader by default — matches
        // Ableton's "Pre FX" tap convention from your reference doc).
        bool process (uint64_t sendId,
                      float levelLinear,
                      double nowSeconds,
                      const Config& cfg = {});

        void reset (uint64_t sendId);

    private:
        struct State { float lastDb = -120.0f; double lastFireSec = -1e9; };
        std::unordered_map<uint64_t, State> states;
    };

} // namespace bg::sidechain

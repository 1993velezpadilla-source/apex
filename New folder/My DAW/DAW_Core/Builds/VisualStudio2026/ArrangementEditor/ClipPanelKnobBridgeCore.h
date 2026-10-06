// ===========================================================================
// ClipPanelKnobBridgeCore.h
// Adapter: ISampleItemBridge → ArrangementClipModel
// Lets the existing SampleKnobCore/SampleButtonCore talk to clip models
// instead of DAW takes. Zero code duplication.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "ArrangementClipStateCore.h"

// Forward declare to avoid circular dependency
namespace SampleSettings
{
    struct ItemSnapshot;
    class ISampleItemBridge;
}

// Minimal bridge interface (copied to avoid dependency)
namespace SampleSettings
{
    struct ItemSnapshot
    {
        bool  valid    = false;
        float volume   = 1.f;
        float pan      = 0.f;
        float pitch    = 0.f;
        float rate     = 1.f;
        float trim     = 0.f;
        float fadeIn   = 0.f;
        float fadeOut  = 0.f;
        float length   = 0.f;
        bool  reversed = false;
        std::string trackName;
        std::string sourceFile;
        double sampleRate = 44100.0;
    };

    class ISampleItemBridge
    {
    public:
        virtual ~ISampleItemBridge() = default;
        virtual bool readSnapshot(ItemSnapshot& out) = 0;
        virtual void setVolume(float linear) = 0;
        virtual void setPan(float pan) = 0;
        virtual void setPitch(float semitones) = 0;
        virtual void setRate(float rate) = 0;
        virtual void setTrim(float seconds) = 0;
        virtual void setFadeIn(float seconds) = 0;
        virtual void setFadeOut(float seconds) = 0;
        virtual void doReverse() = 0;
        virtual void doNormalize() = 0;
        virtual void undoLast() = 0;
        virtual void doRemoveDC() = 0;
        virtual void openItemProps() = 0;
        virtual void setStretchMode(int modeIndex) = 0;
        virtual int getStretchMode() = 0;
    };
}

namespace ArrangementEditor
{
    class ClipPanelKnobBridge : public SampleSettings::ISampleItemBridge
    {
    public:
        ClipPanelKnobBridge(ArrangementClipModel* clip,
                            ArrangementClipStateCore& state)
            : m_clip(clip), m_state(state)
        {}

        void setClip(ArrangementClipModel* clip) { m_clip = clip; }

        // -----------------------------------------------------------------------
        // Read current state from clip into snapshot
        // -----------------------------------------------------------------------
        bool readSnapshot(SampleSettings::ItemSnapshot& out) override
        {
            if (!m_clip)
                return false;

            out.valid = true;
            out.volume = m_clip->gain;
            out.pan = 0.f; // clips don't have pan (yet)
            out.pitch = m_clip->pitch;
            out.rate = m_clip->rate;
            out.trim = m_clip->sourceOffset;
            out.fadeIn = (float)m_clip->fadeInLength;
            out.fadeOut = (float)m_clip->fadeOutLength;
            out.length = (float)m_clip->length;
            out.reversed = m_clip->reversed;
            out.trackName = "Clip: " + m_clip->clipName;
            out.sourceFile = m_clip->sourcePath;
            out.sampleRate = 44100.0; // TODO: read from actual source file

            return true;
        }

        // -----------------------------------------------------------------------
        // Write individual parameters back to clip
        // -----------------------------------------------------------------------
        void setVolume(float linear) override
        {
            if (!m_clip) return;
            m_clip->gain = linear;
            m_state.updateClip(*m_clip);
        }

        void setPan(float pan) override
        {
            // Clips don't have pan (yet)
        }

        void setPitch(float semitones) override
        {
            if (!m_clip) return;
            DBG("[PITCH WRITE] source=UI value=" << semitones);
            // Keep both the legacy float field and the unified TimePitchState in sync.
            m_clip->pitch = semitones;
            m_clip->timePitch.pitchSemitones = (double) semitones;
            // Bug 50 fix: stamp pitchEngineVersion = 1 (semitones) whenever the
            // user writes a pitch value via the knob. Version 0 is the legacy
            // "stored in cents" format; leaving it at 0 caused the migration path
            // in UnifiedPitchStateCore::migrateLegacyPitchValue to divide the new
            // semitone value by 100, making +7 st heard as +0.07 st after the
            // first knob touch.
            m_clip->timePitch.pitchEngineVersion = 1;
            m_state.updateClip(*m_clip);
        }

        void setRate(float rate) override
        {
            if (!m_clip) return;
            m_clip->rate = rate;
            m_state.updateClip(*m_clip);
        }

        void setTrim(float seconds) override
        {
            if (!m_clip) return;
            m_clip->sourceOffset = seconds;
            m_state.updateClip(*m_clip);
        }

        void setFadeIn(float seconds) override
        {
            if (!m_clip) return;
            m_clip->fadeInLength = seconds;
            m_state.updateClip(*m_clip);
        }

        void setFadeOut(float seconds) override
        {
            if (!m_clip) return;
            m_clip->fadeOutLength = seconds;
            m_state.updateClip(*m_clip);
        }

        // -----------------------------------------------------------------------
        // Actions
        // -----------------------------------------------------------------------
        void doReverse() override
        {
            if (!m_clip) return;
            m_clip->reversed = !m_clip->reversed;
            m_state.updateClip(*m_clip);
        }

        void doNormalize() override
        {
            // TODO: Implement normalize (analyze peaks, set gain to 0dB max)
        }

        void undoLast() override
        {
            // TODO: Wire to undo stack
        }

        void doRemoveDC() override
        {
            // TODO: Implement DC offset removal
        }

        void openItemProps() override
        {
            // Already in the properties panel
        }

        // -----------------------------------------------------------------------
        // Stretch mode (map to clip's stretch mode)
        // -----------------------------------------------------------------------
        void setStretchMode(int modeIndex) override
        {
            if (!m_clip) return;
            DBG("[PITCH WRITE] source=ModeChange value=" << m_clip->timePitch.pitchSemitones);
            m_clip->stretchMode = modeIndex;
            m_clip->timePitch.mode = static_cast<TimePitchMode>(juce::jlimit(0, (int) TimePitchMode::OfflineHQ, modeIndex));
            m_state.updateClip(*m_clip);
        }

        int getStretchMode() override
        {
            return m_clip ? m_clip->stretchMode : 0;
        }

    private:
        ArrangementClipModel*      m_clip = nullptr;
        ArrangementClipStateCore&  m_state;
    };

} // namespace ArrangementEditor

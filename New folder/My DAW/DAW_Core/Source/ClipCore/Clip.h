#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../UICore/ForensicAuditWindow.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include "../../Builds/VisualStudio2026/ArrangementEditor/TimePitchQualityCore.h"

namespace DAW {

namespace PitchWriteAudit
{
    inline std::atomic<bool> playbackActive { false };
    inline thread_local const char* currentSource = "Unknown";

    struct ScopedSource
    {
        explicit ScopedSource(const char* source) noexcept
            : previous(currentSource)
        {
            currentSource = source;
        }

        ~ScopedSource() noexcept
        {
            currentSource = previous;
        }

    private:
        const char* previous;
    };
}

namespace TimePitchModeIds
{
    static constexpr int Resample        = 0;
    static constexpr int Stretch         = 1;
    static constexpr int PitchOnly       = 2;
    static constexpr int Vocal           = 3;
    static constexpr int Percussion      = 4;
    static constexpr int Texture         = 5;
    static constexpr int OfflineHQ       = 6;
    static constexpr int MaxMode         = OfflineHQ;
    // CRITICAL FIX: Default to Resample (0) instead of Vocal (3)
    // This prevents automatic time-stretch on imported/recorded clips
    static constexpr int DefaultUserMode = Resample;
}

// Clip types
enum class ClipType
{
    Audio,
    MIDI,
    Automation
};

// Forward declaration
class MidiClip;

// Base clip class - pure data model
class Clip
{
public:
    Clip(const ClipID& id, const juce::String& name, ClipType type);
    virtual ~Clip();
    
    // Identity
    ClipID getID() const { return id_; }
    juce::String getName() const { return name_; }
    void setName(const juce::String& newName);
    
    ClipType getType() const { return type_; }
    
    // Position (in samples)
    SamplePosition getStartPosition() const { return startPosition_; }
    void setStartPosition(SamplePosition pos);
    
    SamplePosition getLength() const { return length_; }
    void setLength(SamplePosition len);
    
    SamplePosition getEndPosition() const { return startPosition_ + length_; }
    
    // Track association
    TrackID getTrackID() const { return trackID_; }
    void setTrackID(const TrackID& id);
    
    // Visual properties
    juce::Colour getColor() const { return color_; }
    void setColor(const juce::Colour& newColor);
    
    bool isSelected() const { return selected_; }
    void setSelected(bool shouldBeSelected);
    
    bool isMuted() const { return muted_; }
    void setMuted(bool shouldBeMuted);
    
    // Offset within source file (for audio clips)
    SamplePosition getSourceOffset() const { return sourceOffset_; }
    void setSourceOffset(SamplePosition offset);
    
    // Serialization
    virtual juce::ValueTree getState() const;
    virtual void restoreState(const juce::ValueTree& state);

    /** Fire clipPropertyChanged after an external restoreState() so UI
     *  mirrors (timeline, mixer, editors) refresh. Used by undo/redo. */
    void notifyStateRestored() { notifyPropertyChanged(); }

    // Listener interface
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void clipPropertyChanged(Clip* clip) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    
protected:
    void notifyPropertyChanged();
    
private:
    ClipID id_;
    juce::String name_;
    ClipType type_;
    SamplePosition startPosition_{0};
    SamplePosition length_{44100}; // Default 1 second at 44.1kHz
    TrackID trackID_;
    juce::Colour color_;
    bool selected_{false};
    bool muted_{false};
    SamplePosition sourceOffset_{0};
    
    juce::ListenerList<Listener> listeners_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Clip)
};

// Audio clip - references audio file
class AudioClip : public Clip
{
public:
    AudioClip(const ClipID& id, const juce::String& name);
    ~AudioClip() override;
    
    // Audio file
    juce::File getSourceFile() const { return sourceFile_; }
    void setSourceFile(const juce::File& file);
    
    // Gain
    float getGain() const { return gain_; }
    void setGain(float newGain);

    float getClipPan() const { return clipPan_.load(std::memory_order_relaxed); }
    void  setClipPan(float newPan)
    {
        clipPan_.store(juce::jlimit(-1.0f, 1.0f, newPan), std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    bool isReversed() const noexcept { return reversed_; }
    void setReversed(bool shouldBeReversed)
    {
        if (reversed_ == shouldBeReversed)
            return;
        reversed_ = shouldBeReversed;
        notifyPropertyChanged();
    }

    // Pitch shift in semitones (-36..+36 UI)
    //
    // Threading contract:
    //   UI thread  → setPitchTargetFromUI() ONLY. No notify, no DSP reset.
    //   Audio thread → getPitch() reads atomic. Safe at any time.
    //   Serialization → getState()/restoreState() use pitch_ staging field.

    // Audio-thread-safe read of the current pitch target.
    float getPitch() const noexcept
    {
        return pitchTargetSemitones_.load(std::memory_order_relaxed);
    }

    // Alias kept for call-site compatibility. Writes atomic only — no notify.
    float getPitchTargetSemitones() const noexcept
    {
        return pitchTargetSemitones_.load(std::memory_order_relaxed);
    }

    // UI thread: write pitch target. Thread-safe. No notifyPropertyChanged.
    // No DSP reset. Audio thread picks up the new target on next renderClip().
    void setPitchTargetFromUI(float semitones) noexcept
    {
        const float clamped = juce::jlimit(-36.f, 36.f, semitones);
        pitchTargetSemitones_.store(clamped, std::memory_order_relaxed);
        DBG("[PITCH UI TARGET] " << clamped);
    }

    // Legacy name kept so existing call sites compile without changes.
    // Redirects to setPitchTargetFromUI — does NOT call notifyPropertyChanged.
    void setPitchTargetSemitones(float semitones) noexcept
    {
        setPitchTargetFromUI(semitones);
    }

    // Must only be called from the audio engine, never from the message thread.
    // Writes the atomic target; PitchSmootherCore in AudioEngine will smooth
    // toward it on the next renderClip() call.
    void applyPitchOnAudioThread_DoNotCallFromUI(float semitones) noexcept
    {
        jassert(!juce::MessageManager::existsAndIsCurrentThread());
        const float clamped = juce::jlimit(-36.f, 36.f, semitones);
        pitchTargetSemitones_.store(clamped, std::memory_order_relaxed);
        DBG("[PITCH AUDIO TARGET (direct)] " << clamped);
    }

    // Time stretch ratio (0.1..4.0, 1.0 = original speed)
    float getTimeStretch() const { return timeStretch_.load(std::memory_order_relaxed); }
    void  setTimeStretch(float ratio)
    {
        // Stack backtrace removed — getStackBacktrace() is a blocking OS symbol
        // resolution call (SymGetLineFromAddr64 on Windows). Calling it on every
        // knob drag event blocked the message thread long enough to starve the
        // audio callback, causing dropouts and xruns during UI interaction.
        FORENSIC_LOG("[FORENSIC TRAP setTimeStretch] value=" << juce::String(ratio));
        const auto clamped = juce::jlimit(0.1f, 4.f, ratio);
        if (std::abs(timeStretch_.load(std::memory_order_relaxed) - clamped) <= 0.0001f)
            return;

        timeStretch_.store(clamped, std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    // Fine tune in cents (-100..+100).  100 cents = 1 semitone.
    float getFineTuneCents() const { return fineTuneCents_.load(std::memory_order_relaxed); }
    float getClipFineTune() const { return getFineTuneCents(); }
    void  setFineTuneCents(float cents)
    {
        const auto clamped = juce::jlimit(-100.f, 100.f, cents);
        if (std::abs(fineTuneCents_.load(std::memory_order_relaxed) - clamped) <= 0.0001f)
            return;

        fineTuneCents_.store(clamped, std::memory_order_relaxed);
        notifyPropertyChanged();
    }
    void setClipFineTune(float cents) { setFineTuneCents(cents); }

    // Formant shift in semitones (-12..+12).  0 = no shift.
    float getFormantSemitones() const { return formantSemitones_.load(std::memory_order_relaxed); }
    void  setFormantSemitones(float st)
    {
        formantSemitones_.store(juce::jlimit(-12.f, 12.f, st), std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    // Whether to preserve vocal formants when pitch-shifting.
    bool getPreserveFormants() const { return preserveFormants_.load(std::memory_order_relaxed); }
    void setPreserveFormants(bool v) { preserveFormants_.store(v, std::memory_order_relaxed); notifyPropertyChanged(); }

    // Pitch/time mode.
    // 0=Resample 1=Stretch 2=PitchOnly 3=Vocal 4=Percussion 5=Texture 6=OfflineHQ
    int  getTimePitchMode() const { return timePitchMode_.load(std::memory_order_relaxed); }
    void setTimePitchMode(int mode)
    {
        // Stack backtrace removed — same reason as setTimeStretch above.
        FORENSIC_LOG("[FORENSIC TRAP setTimePitchMode] value=" << juce::String(mode));
        const auto clamped = juce::jlimit(TimePitchModeIds::Resample, TimePitchModeIds::MaxMode, mode);
        if (timePitchMode_.load(std::memory_order_relaxed) == clamped)
            return;

        timePitchMode_.store(clamped, std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    bool getTransientLock() const noexcept { return transientLock_.load(std::memory_order_relaxed); }
    void setTransientLock(bool enabled) { transientLock_.store(enabled, std::memory_order_relaxed); notifyPropertyChanged(); }

    int getTimePitchQuality() const noexcept { return timePitchQuality_.load(std::memory_order_relaxed); }
    void setTimePitchQuality(int quality)
    {
        timePitchQuality_.store(juce::jlimit(0, 4, quality), std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    SamplePosition getProcessedTimelineLength() const
    {
        // Pitch NEVER affects timeline length. Stretch is the only modifier.
        // Removing pitch from this formula was the fix for clip visually
        // stretching/shrinking when the pitch knob moved.
        const auto baseLen        = juce::jmax((SamplePosition) 1, getLength());
        const double stretchRatio = juce::jmax(0.01, (double) timeStretch_.load(std::memory_order_relaxed));

        return (SamplePosition) juce::jmax<int64_t>(1,
            (int64_t) std::llround((double) baseLen * stretchRatio));
    }

    bool isDurationPreservingPitchMode() const noexcept
    {
        const int mode = timePitchMode_.load(std::memory_order_relaxed);
        return mode == TimePitchModeIds::PitchOnly
            || mode == TimePitchModeIds::Vocal
            || mode == TimePitchModeIds::OfflineHQ;
    }

    // Source sample bounds (in source-file samples).
    // Used by renderClip() for slip/split correctness.
    int64_t getSourceStartSample() const  { return sourceStartSample_; }
    void    setSourceStartSample(int64_t s)
    {
        // Stack backtrace removed — getStackBacktrace() is a blocking OS symbol
        // resolution call (Bug 52). Calling it here blocked the message thread
        // on every clip split and slip operation.
        DBG("[FORENSIC TRAP setSourceStartSample] value=" << s);
        sourceStartSample_ = juce::jmax((int64_t)0, s);
    }
    int64_t getSourceEndSample() const    { return sourceEndSample_; }
    void    setSourceEndSample(int64_t s)
    {
        // Stack backtrace removed — same reason as setSourceStartSample (Bug 52).
        DBG("[FORENSIC TRAP setSourceEndSample] value=" << s);
        sourceEndSample_ = s;
    }

    // Fade in/out lengths (in samples)
    SamplePosition getFadeInLength() const { return fadeInLength_; }
    void setFadeInLength(SamplePosition length);

    SamplePosition getFadeOutLength() const { return fadeOutLength_; }
    void setFadeOutLength(SamplePosition length);

    int getFadeInCurve() const noexcept { return fadeInCurve_.load(std::memory_order_relaxed); }
    int getFadeOutCurve() const noexcept { return fadeOutCurve_.load(std::memory_order_relaxed); }
    void setFadeInCurve(int curve)
    {
        fadeInCurve_.store(juce::jlimit(0, 3, curve), std::memory_order_relaxed);
        notifyPropertyChanged();
    }
    void setFadeOutCurve(int curve)
    {
        fadeOutCurve_.store(juce::jlimit(0, 3, curve), std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    // Serialization
    juce::ValueTree getState() const override;
    void restoreState(const juce::ValueTree& state) override;

private:
    juce::File sourceFile_;
    float gain_{1.0f};
    std::atomic<float> clipPan_{0.0f};
    bool reversed_{false};
    // pitchTargetSemitones_ is the single authoritative pitch value.
    // Written atomically by UI thread; read atomically by audio thread.
    // pitch_ is kept only for getState()/restoreState() staging.
    std::atomic<float> pitchTargetSemitones_ { 0.0f };
    std::atomic<float> timeStretch_{1.0f};
    std::atomic<float> fineTuneCents_{0.0f};
    std::atomic<float> formantSemitones_{0.0f};
    std::atomic<bool>  preserveFormants_{false};
    std::atomic<int>   timePitchMode_{TimePitchModeIds::DefaultUserMode};
    std::atomic<bool>  transientLock_{false};
    std::atomic<int>   timePitchQuality_{(int)ArrangementEditor::TimePitchQuality::Balanced};
    int64_t sourceStartSample_{0};
    int64_t sourceEndSample_{0};
    SamplePosition fadeInLength_{0};
    SamplePosition fadeOutLength_{0};
    std::atomic<int> fadeInCurve_{0};
    std::atomic<int> fadeOutCurve_{0};
};

// Clip manager
class ClipManager
{
public:
    ClipManager();
    ~ClipManager();
    
    // Clip creation
    AudioClip* createAudioClip(const juce::String& name, const juce::File& audioFile);
    MidiClip* createMIDIClip(const juce::String& name);
    Clip* recreateClipFromState(const juce::ValueTree& state);

    // Create a generic placeholder clip placed on a track (used by the UI)
    Clip* createEmptyClip(const TrackID& trackId,
                          const juce::String& name,
                          SamplePosition startPos,
                          SamplePosition length);

    // Duplicate an existing clip (copy placed immediately after the original)
    Clip* duplicateClip(const ClipID& originalId);
    
    // Clip deletion
    bool deleteClip(const ClipID& id);
    void deleteAllClips();
    
    // Clip access
    Clip* getClip(const ClipID& id) const;
    const juce::OwnedArray<Clip>& getAllClips() const { return clips_; }
    
    // Get clips on a specific track
    juce::Array<Clip*> getClipsOnTrack(const TrackID& trackID) const;
    
    // Serialization
    juce::ValueTree getState() const;
    void restoreState(const juce::ValueTree& state);
    
    // Listener interface
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void clipAdded(Clip* clip) {}
        virtual void clipRemoved(const ClipID& clipID) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    std::function<void(const ClipID&)> onClipRemoved;
    
   /** Returns the lock that guards clips_.
    *  Audio thread: ScopedTryLock around getClipsOnTrack() + renderClip() loop.
    *  Message thread: ScopedLock in every create / delete mutation. */
   juce::CriticalSection& getLock() const { return clipLock_; }

private:
    juce::OwnedArray<Clip> clips_;
    juce::ListenerList<Listener> listeners_;
   mutable juce::CriticalSection clipLock_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipManager)
};

} // namespace DAW

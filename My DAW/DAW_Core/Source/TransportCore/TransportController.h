#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../StateCore/ApplicationState.h"

namespace DAW {

// Transport controller - handles play/stop/record logic
// Separated from UI and audio engine
class TransportController
{
public:
    TransportController(ApplicationState& appState);
    ~TransportController();
    
    // Transport control
    void play();
    void stop();
    void returnToZero();
    void pause();
    void togglePlayPause();
    void record();
    void recordWithoutSafetyCheck();
    void stopRecording();
    void toggleRecord();
    
    // Loop control
    void setLooping(bool shouldLoop);
    bool isLooping() const;
    void setLoopRange(SamplePosition start, SamplePosition end);
    std::pair<SamplePosition, SamplePosition> getLoopRange() const;

    // Return to last position control
    void setReturnToLastPosition(bool enabled);
    bool shouldReturnToLastPosition() const;
    
    // Tempo control
    void setTempo(double bpm);
    double getTempo() const;
    
    // Position control
    void setPosition(SamplePosition samples);
    void setPositionFromAudioThread(SamplePosition samples) noexcept;
    void setPositionBarBeat(BarBeatPosition barBeat);
    SamplePosition getPosition() const;
    BarBeatPosition getPositionBarBeat() const;
    
    // Time conversion
    SamplePosition barBeatToSamples(BarBeatPosition barBeat, double sampleRate) const;
    BarBeatPosition samplesToBarBeat(SamplePosition samples, double sampleRate) const;
    
    // State queries
    bool isPlaying() const;
    bool isRecording() const;
    bool isPaused() const;
    
    // Listeners for transport state changes
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void transportStateChanged() {}
        virtual void tempoChanged(double newTempo) {}
        virtual void positionChanged(SamplePosition newPosition) {}
        virtual void loopRangeChanged(SamplePosition start, SamplePosition end) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    // ── RT-safe state application (audio thread — atomic stores only) ──────
    // These methods perform ONLY atomic state changes. They do NOT:
    //   - acquire locks
    //   - allocate heap memory
    //   - call Logger
    //   - notify listeners
    //   - trigger UI updates
    //   - touch ApplicationState Value objects
    // Safe to call from flushPendingTransportControlRequests() on the audio thread.
    void applyPlayRT(SamplePosition startPos) noexcept;
    void applyPauseRT() noexcept;
    void applyRecordRT(SamplePosition startPos) noexcept;
    void applyStopRecordingRT() noexcept;
    
private:
    ApplicationState& state_;
    juce::ListenerList<Listener> listeners_;
    std::atomic<SamplePosition> currentPosition_{0};
    std::atomic<bool> isPlayingAtomic_{false};
    std::atomic<bool> isRecordingAtomic_{false};
    std::atomic<bool> isPausedAtomic_{false};
    std::atomic<bool> isLoopingAtomic_{false};
    std::atomic<double> tempoAtomic_{120.0};
    SamplePosition playStartPosition_{0};
    SamplePosition userCursorPosition_{0};  // last position explicitly set by the user via timeline click

    std::atomic<SamplePosition> loopStart_{0};
    std::atomic<SamplePosition> loopEnd_{0};
    bool safetyCheckEnabled_ { true };
    bool returnToLastPosition_{false};
    
    void notifyTransportStateChanged();
    void notifyTempoChanged();
    void notifyPositionChanged();
    void notifyLoopRangeChanged();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportController)
};

} // namespace DAW

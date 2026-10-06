#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../InputMonitorCore/TrackMonitoringStateModel.h"
#include "../InputMonitorCore/InputFxChain.h"
#include "../InputMonitorCore/InputTrimCore.h"
#include "../InputMonitorCore/InputMeterCore.h"
#include "../RecordingCore/LiveRecordWaveformCore.h"
#include <atomic>

namespace DAW {

// Universal track role — any track can change behavior
enum class TrackRole
{
    Audio,
    Aux,
    Bus,
    FolderBus,
    MIDI,
    Instrument,
    Utility,
    Print
};

// Track model - pure data, no UI
class Track
{
public:
    Track(const TrackID& id, const juce::String& name);
    virtual ~Track();
    
    // Identity
    TrackID getID() const { return id_; }
    juce::String getName() const { return name_; }
    void setName(const juce::String& newName);
    
    // State
    bool isMuted() const { return muted_.load(std::memory_order_relaxed); }
    void setMuted(bool shouldBeMuted);
    
    bool isSoloed() const { return soloed_.load(std::memory_order_relaxed); }
    void setSoloed(bool shouldBeSoloed);
    
    bool isArmed() const { return armed_.load(std::memory_order_relaxed); }
    void setArmed(bool shouldBeArmed);

    bool isMonitoring() const { return monitoring_.load(std::memory_order_relaxed); }
    void setMonitoring(bool shouldBeMonitoring);
    
    // Level
    float getVolume() const { return volume_.load(std::memory_order_relaxed); }
    void setVolume(float newVolume);
    
    float getPan() const { return pan_.load(std::memory_order_relaxed); }
    void setPan(float newPan); // -1.0 (left) to +1.0 (right)

    bool isAutomationVisible() const { return automationVisible_.load(std::memory_order_relaxed); }
    void setAutomationVisible(bool visible) { automationVisible_.store(visible, std::memory_order_relaxed); notifyPropertyChanged(); }
    bool isAutomationMuted() const { return automationMuted_.load(std::memory_order_relaxed); }
    void setAutomationMuted(bool muted) { automationMuted_.store(muted, std::memory_order_relaxed); notifyPropertyChanged(); }
    bool isAutomationSnapToGrid() const { return automationSnapToGrid_.load(std::memory_order_relaxed); }
    void setAutomationSnapToGrid(bool snap) { automationSnapToGrid_.store(snap, std::memory_order_relaxed); notifyPropertyChanged(); }
    juce::String getActiveAutomationParameterId() const { return activeAutomationParameterId_; }
    void setActiveAutomationParameterId(const juce::String& parameterId);
    juce::StringArray getAutomationParameterOrder() const { return automationParameterOrder_; }
    void setAutomationParameterOrder(const juce::StringArray& order);
    
    // Color
    juce::Colour getColor() const { return color_; }
    void setColor(const juce::Colour& newColor);

    // Peak level metering (written by audio thread, read by UI — atomic)
    float getPeakLevel() const
    {
        return juce::jmax(getPeakLevelLeft(), getPeakLevelRight());
    }
    float getPeakLevelLeft() const  { return peakLevelLeft_.load(std::memory_order_relaxed); }
    float getPeakLevelRight() const { return peakLevelRight_.load(std::memory_order_relaxed); }
    void  setPeakLevel(float v)
    {
        peakLevelLeft_.store(v, std::memory_order_relaxed);
        peakLevelRight_.store(v, std::memory_order_relaxed);
    }
    void setPeakLevels(float left, float right)
    {
        peakLevelLeft_.store(left, std::memory_order_relaxed);
        peakLevelRight_.store(right, std::memory_order_relaxed);
    }

    // Role
    TrackRole getRole() const { return (TrackRole) role_.load(std::memory_order_relaxed); }
    void setRole(TrackRole r) { role_.store((int) r, std::memory_order_relaxed); notifyPropertyChanged(); }

    // Master bus flag — set by TrackManager::createMasterTrack()
    bool isMaster() const  { return isMaster_; }
    void setMaster(bool m) { isMaster_ = m; }

    // Parent/child hierarchy (for folder buses)
    TrackID getParentTrackID() const { return parentTrackID_; }
    void setParentTrackID(const TrackID& id) { parentTrackID_ = id; notifyPropertyChanged(); }

    // Ordering
    int getIndex() const { return index_; }
    void setIndex(int newIndex);

    // Plugin chain (optional — set by ApplicationCore after Track creation)
    void setPluginChain(class PluginChainCore* chain) { pluginChain_ = chain; }
    class PluginChainCore* getPluginChain() const { return pluginChain_; }

    TrackMonitoringStateModel& getMonitoringState() noexcept { return monitoringState_; }
    const TrackMonitoringStateModel& getMonitoringState() const noexcept { return monitoringState_; }
    InputFxChain& getInputFxChain() noexcept { return inputFxChain_; }
    InputTrimCore& getInputTrim() noexcept { return inputTrim_; }
    InputMeterCore& getInputMeter() noexcept { return inputMeter_; }
    LiveRecordWaveformCore& getLiveRecordWaveform() noexcept { return liveRecordWaveform_; }

    // Per-track hardware input source (pro-DAW input routing).
    // firstChannel = 0-based hardware input channel; mono = record/monitor a
    // single channel instead of the [first, first+1] stereo pair.
    int  getInputFirstChannel() const noexcept { return inputFirstChannel_.load(std::memory_order_relaxed); }
    bool isInputMono() const noexcept          { return inputMono_.load(std::memory_order_relaxed); }
    void setInputSource(int firstChannel, bool mono)
    {
        inputFirstChannel_.store(juce::jmax(0, firstChannel), std::memory_order_relaxed);
        inputMono_.store(mono, std::memory_order_relaxed);
        notifyPropertyChanged();
    }

    // Serialization
    virtual juce::ValueTree getState() const;
    virtual void restoreState(const juce::ValueTree& state);
    
    // Listener interface
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void trackPropertyChanged(Track* track) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

protected:
    void notifyPropertyChanged();
    
private:
    TrackID id_;
    juce::String name_;
    std::atomic<bool> muted_{false};
    std::atomic<bool> soloed_{false};
    std::atomic<bool> armed_{false};
    std::atomic<bool> monitoring_{false};
    std::atomic<float> volume_{1.0f}; // 0.0 to ~+6 dB
    std::atomic<float> pan_{0.0f};    // -1.0 to 1.0
    std::atomic<bool> automationVisible_{false};
    std::atomic<bool> automationMuted_{false};
    std::atomic<bool> automationSnapToGrid_{true};
    juce::String activeAutomationParameterId_ { "track.volume" };
    juce::StringArray automationParameterOrder_;
    juce::Colour color_;
    std::atomic<int> role_ { (int) TrackRole::Audio };
    TrackID parentTrackID_;  // empty = top-level
    int index_{0};
    bool isMaster_ = false;
    std::atomic<float> peakLevelLeft_{0.f};
    std::atomic<float> peakLevelRight_{0.f};
    class PluginChainCore* pluginChain_ = nullptr; // Non-owning pointer — owned by ApplicationCore
    TrackMonitoringStateModel monitoringState_;
    InputFxChain              inputFxChain_;
    InputTrimCore             inputTrim_;
    InputMeterCore            inputMeter_;
    LiveRecordWaveformCore    liveRecordWaveform_;
    std::atomic<int>          inputFirstChannel_{0};
    std::atomic<bool>         inputMono_{false};

    juce::ListenerList<Listener> listeners_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};

// Track manager - owns all tracks
class TrackManager
{
public:
    TrackManager();
    ~TrackManager();
    
    // Track creation/deletion
    Track* createTrack(const juce::String& name);
    Track* createMidiTrack(const juce::String& name);
    Track* recreateTrackFromState(const juce::ValueTree& state);
    bool deleteTrack(const TrackID& id);
    void deleteAllTracks();
    
    // Track access
    Track* getTrack(const TrackID& id) const;
    Track* getTrack(int index) const;
    int getNumTracks() const { return tracks_.size(); }
    
    const juce::OwnedArray<Track>& getAllTracks() const { return tracks_; }
    
    // Track ordering
    void moveTrack(const TrackID& id, int newIndex);
    int  getTrackIndex(const TrackID& id) const;
    /** Fire trackOrderChanged on all listeners without moving any track.
     *  Use this to force a UI rebuild after topology-only changes (e.g. folder
     *  adoption where no physical reorder was required). */
    void notifyOrderChanged();

    // Master track — the final mix bus, always exists, never in the regular tracks_ list
    Track*  createMasterTrack();
    Track*  getMasterTrack() const { return masterTrack_.get(); }
    bool    hasMasterTrack()  const { return masterTrack_ != nullptr; }

    // Serialization
    juce::ValueTree getState() const;
    void restoreState(const juce::ValueTree& state);
    
    // Listener interface
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void trackAdded(Track* track) {}
        virtual void trackRemoved(const TrackID& trackID) {}
        virtual void trackOrderChanged() {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    
private:
    juce::OwnedArray<Track> tracks_;
    std::unique_ptr<Track>  masterTrack_;
    juce::ListenerList<Listener> listeners_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackManager)
};

} // namespace DAW

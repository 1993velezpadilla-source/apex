#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include "../InputMonitorCore/TrackMonitoringStateModel.h"
#include "../InputMonitorCore/InputFxChain.h"
#include "../InputMonitorCore/InputTrimCore.h"
#include "../InputMonitorCore/InputMeterCore.h"
#include "../RecordingCore/LiveRecordWaveformCore.h"
#include <atomic>
#include <cmath>

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

/** Signal point written by an armed track when recording. */
enum class TrackRecordMode : int
{
    Dry = 0,
    PostFader = 1
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

    TrackRecordMode getRecordMode() const noexcept
    {
        return static_cast<TrackRecordMode>(recordMode_.load(std::memory_order_relaxed));
    }
    void setRecordMode(TrackRecordMode mode)
    {
        const auto safeMode = mode == TrackRecordMode::PostFader
            ? TrackRecordMode::PostFader : TrackRecordMode::Dry;
        if (getRecordMode() == safeMode)
            return;
        recordMode_.store((int) safeMode, std::memory_order_relaxed);
        notifyPropertyChanged();
    }

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
    float getPendingPeakLevel() const noexcept
    {
        return juce::jmax(pendingPeakLevelLeft_.load(std::memory_order_relaxed),
                          pendingPeakLevelRight_.load(std::memory_order_relaxed));
    }
    bool hasPendingPeakLevel() const noexcept { return getPendingPeakLevel() > 0.0f; }
    void consumePeakLevels(float& left, float& right) noexcept
    {
        left = pendingPeakLevelLeft_.exchange(0.0f, std::memory_order_relaxed);
        right = pendingPeakLevelRight_.exchange(0.0f, std::memory_order_relaxed);
    }
    void  setPeakLevel(float v)
    {
        setPeakLevels(v, v);
    }
    void setPeakLevels(float left, float right)
    {
        const float safeLeft = std::isfinite(left) && left > 0.0f ? left : 0.0f;
        const float safeRight = std::isfinite(right) && right > 0.0f ? right : 0.0f;
        peakLevelLeft_.store(safeLeft, std::memory_order_relaxed);
        peakLevelRight_.store(safeRight, std::memory_order_relaxed);
        publishPeak(pendingPeakLevelLeft_, safeLeft);
        publishPeak(pendingPeakLevelRight_, safeRight);
        const float over = juce::jmax(safeLeft, safeRight);
        if (std::isfinite(over) && over > 1.0f)
        {
            const float overDb = 20.0f * std::log10(over);
            float previous = clipOverDb_.load(std::memory_order_relaxed);
            while (overDb > previous
                   && !clipOverDb_.compare_exchange_weak(previous, overDb,
                                                         std::memory_order_relaxed)) {}
        }
    }

    bool isClipLatched() const noexcept
    {
        return clipOverDb_.load(std::memory_order_relaxed) > 0.0f;
    }
    float getClipOverDb() const noexcept
    {
        return clipOverDb_.load(std::memory_order_relaxed);
    }
    void clearClipPeak() noexcept
    {
        clipOverDb_.store(0.0f, std::memory_order_relaxed);
    }

    /** Set the pre-fader input trim and notify project listeners for save/undo. */
    void setInputTrimDb(float db)
    {
        if (!std::isfinite(db)) db = 0.0f;
        db = juce::jlimit(-120.0f, 24.0f, db);
        if (std::abs(inputTrim_.getTargetGainDb() - db) < 0.0001f)
            return;
        inputTrim_.setTargetGainDb(db);
        notifyPropertyChanged();
    }

    void setTrimVuReferenceDb(float db)
    {
        const auto previous = inputMeter_.getVuReferenceDb();
        inputMeter_.setVuReferenceDb(db);
        if (previous != inputMeter_.getVuReferenceDb()) notifyPropertyChanged();
    }

    void setTrimVuChannelMode(VuChannelMode mode)
    {
        const auto previous = inputMeter_.getVuChannelMode();
        inputMeter_.setVuChannelMode(mode);
        if (previous != inputMeter_.getVuChannelMode()) notifyPropertyChanged();
    }

    // Role
    TrackRole getRole() const { return (TrackRole) role_.load(std::memory_order_relaxed); }
    void setRole(TrackRole r) { role_.store((int) r, std::memory_order_relaxed); notifyPropertyChanged(); }

    // Optional semantic role assigned by the Quick Workflow catalog.  This is
    // deliberately separate from the generic TrackRole enum and from the
    // display name: stable catalog IDs are the only authority for Quick Track
    // grouping and routing.
    juce::String getQuickRoleId() const { return quickRoleId_; }
    void setQuickRoleId(const juce::String& roleId)
    {
        if (quickRoleId_ == roleId)
            return;
        quickRoleId_ = roleId;
        notifyPropertyChanged();
    }

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
    static void publishPeak(std::atomic<float>& destination, float value) noexcept
    {
        if (!std::isfinite(value) || value <= 0.0f)
            return;
        float previous = destination.load(std::memory_order_relaxed);
        while (value > previous
               && !destination.compare_exchange_weak(previous, value,
                                                     std::memory_order_relaxed)) {}
    }

    TrackID id_;
    juce::String name_;
    std::atomic<bool> muted_{false};
    std::atomic<bool> soloed_{false};
    std::atomic<bool> armed_{false};
    std::atomic<int> recordMode_ { (int) TrackRecordMode::Dry };
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
    juce::String quickRoleId_;
    TrackID parentTrackID_;  // empty = top-level
    int index_{0};
    bool isMaster_ = false;
    std::atomic<float> peakLevelLeft_{0.f};
    std::atomic<float> peakLevelRight_{0.f};
    std::atomic<float> pendingPeakLevelLeft_{0.f};
    std::atomic<float> pendingPeakLevelRight_{0.f};
    std::atomic<float> clipOverDb_{0.f};
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
    std::vector<TrackID> deleteTracks(const std::vector<TrackID>& ids);
    void deleteAllTracks();

    /** True when the caller is applying one logical message-thread topology
     * mutation. Listeners still receive per-track lifecycle notifications, but
     * dirty/autosave owners can coalesce them into the enclosing transaction. */
    void beginTopologyMutation() noexcept { ++topologyMutationDepth_; }
    void endTopologyMutation() noexcept
    {
        if (topologyMutationDepth_ > 0)
            --topologyMutationDepth_;
    }
    bool isTopologyMutationActive() const noexcept { return topologyMutationDepth_ > 0; }

    bool canDeleteTrack(const TrackID& id) const;

    /** C11: optional policy gate consulted before any track deletion.
     *  Return false to refuse (e.g. while the track is actively recording).
     *  Installed by ApplicationCore; message thread only. */
    std::function<bool(const TrackID&)> trackDeletionGate;
    
    // Track access
    Track* getTrack(const TrackID& id) const;
    Track* getTrack(int index) const;
    int getNumTracks() const { return tracks_.size(); }
    
    const juce::OwnedArray<Track>& getAllTracks() const { return tracks_; }
    
    // Track ordering
    void moveTrack(const TrackID& id, int newIndex);
    /** Reorder the whole track list to match the given ID order (atomic).
     *  Every current track ID must appear exactly once; otherwise the call is
     *  ignored. Fires a single trackOrderChanged notification. Used by the
     *  multi-track reorder so a whole selection moves as one block while
     *  routing IDs stay stable. */
    void applyTrackOrder(const std::vector<TrackID>& orderedIds);
    int  getTrackIndex(const TrackID& id) const;
    /** Fire trackOrderChanged on all listeners without moving any track.
     *  Use this to force a UI rebuild after topology-only changes (e.g. folder
     *  adoption where no physical reorder was required). */
    void notifyOrderChanged();
    bool isRestoringState() const noexcept { return restoringState_; }

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
    bool restoringState_ = false;
    int topologyMutationDepth_ = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackManager)
};

} // namespace DAW

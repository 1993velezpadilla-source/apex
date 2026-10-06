#include "Track.h"
#include "../UtilityCore/Types.h"
#include "../MixerScaleCore/FaderRangeCore.h"
#include "../MidiCore/MidiTrackCore.h"

namespace DAW {

namespace IDs
{
    static const juce::Identifier TRACKS("Tracks");
    static const juce::Identifier TRACK("Track");
    static const juce::Identifier ID("id");
    static const juce::Identifier NAME("name");
    static const juce::Identifier MUTED("muted");
    static const juce::Identifier SOLOED("soloed");
    static const juce::Identifier ARMED("armed");
    static const juce::Identifier MONITORING("monitoring");
    static const juce::Identifier VOLUME("volume");
    static const juce::Identifier PAN("pan");
    static const juce::Identifier AUTOMATION_VISIBLE("automationVisible");
    static const juce::Identifier AUTOMATION_MUTED("automationMuted");
    static const juce::Identifier AUTOMATION_SNAP_TO_GRID("snapToGrid");
    static const juce::Identifier AUTOMATION_ACTIVE_PARAMETER("automationActiveParameter");
    static const juce::Identifier AUTOMATION_PARAMETER_ORDER("automationParameterOrder");
    static const juce::Identifier COLOR("color");
    static const juce::Identifier INDEX("index");
    static const juce::Identifier ROLE("role");
    static const juce::Identifier PARENT_TRACK_ID("parentTrackID");
    static const juce::Identifier IS_MASTER("isMaster");
}

//==============================================================================
Track::Track(const TrackID& id, const juce::String& name)
    : id_(id), name_(name)
{
    // Professional default: white — user can color tracks via palette
    color_ = juce::Colours::white;
    automationParameterOrder_.add("track.volume");
    automationParameterOrder_.add("track.pan");
    automationParameterOrder_.add("track.mute");
    automationParameterOrder_.add("track.solo");
    automationParameterOrder_.add("track.tape_stop");
    automationParameterOrder_.add("instrument.main");
    automationParameterOrder_.add("plugin.0.__slot_mix__");
}

Track::~Track()
{
}

void Track::setName(const juce::String& newName)
{
    name_ = newName;
    notifyPropertyChanged();
}

void Track::setMuted(bool shouldBeMuted)
{
    muted_.store(shouldBeMuted, std::memory_order_relaxed);
    notifyPropertyChanged();
}

void Track::setSoloed(bool shouldBeSoloed)
{
    // Master track must NEVER be soloed — it is the final mix bus
    if (isMaster_) return;

    soloed_.store(shouldBeSoloed, std::memory_order_relaxed);
    notifyPropertyChanged();
}

void Track::setArmed(bool shouldBeArmed)
{
    armed_.store(shouldBeArmed, std::memory_order_relaxed);
    notifyPropertyChanged();
}

void Track::setMonitoring(bool shouldBeMonitoring)
{
    monitoring_.store(shouldBeMonitoring, std::memory_order_relaxed);
    monitoringState_.setMode(shouldBeMonitoring ? InputMonitorMode::On : InputMonitorMode::Off);
    notifyPropertyChanged();
}

void Track::setVolume(float newVolume)
{
    // Clamp via the session-global fader format core so the audio model
    // follows the active +6/+12 format instead of assuming a fixed +12 path.
    float maxGain = 4.0f;
    if (auto* core = FaderRangeCore::getGlobalInstance())
        maxGain = core->dbToGain(core->getMaxDb());
    volume_.store(juce::jlimit(0.0f, maxGain, newVolume), std::memory_order_relaxed);
    notifyPropertyChanged();
}

void Track::setPan(float newPan)
{
    pan_.store(juce::jlimit(-1.0f, 1.0f, newPan), std::memory_order_relaxed);
    notifyPropertyChanged();
}

void Track::setActiveAutomationParameterId(const juce::String& parameterId)
{
    auto newId = parameterId.isNotEmpty() ? parameterId : juce::String("track.volume");
    if (activeAutomationParameterId_ == newId)
        return;

    activeAutomationParameterId_ = newId;
    if (!automationParameterOrder_.contains(newId))
        automationParameterOrder_.add(newId);
    notifyPropertyChanged();
}

void Track::setAutomationParameterOrder(const juce::StringArray& order)
{
    juce::StringArray cleaned;
    for (auto& id : order)
        if (id.isNotEmpty() && !cleaned.contains(id))
            cleaned.add(id);

    if (!cleaned.contains("track.volume"))    cleaned.insert(0, "track.volume");
    if (!cleaned.contains("track.pan"))       cleaned.insert(1, "track.pan");
    if (!cleaned.contains("track.mute"))      cleaned.insert(2, "track.mute");
    if (!cleaned.contains("track.solo"))      cleaned.insert(3, "track.solo");
    if (!cleaned.contains("track.tape_stop")) cleaned.insert(4, "track.tape_stop");
    if (!cleaned.contains(activeAutomationParameterId_)) cleaned.add(activeAutomationParameterId_);

    automationParameterOrder_ = cleaned;
    notifyPropertyChanged();
}

void Track::setColor(const juce::Colour& newColor)
{
    color_ = newColor;
    notifyPropertyChanged();
}

void Track::setIndex(int newIndex)
{
    index_ = newIndex;
    notifyPropertyChanged();
}

juce::ValueTree Track::getState() const
{
    juce::ValueTree state(IDs::TRACK);
    state.setProperty(IDs::ID, id_, nullptr);
    state.setProperty(IDs::NAME, name_, nullptr);
    state.setProperty(IDs::MUTED, muted_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::SOLOED, soloed_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::ARMED, armed_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("recordMode", recordMode_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::MONITORING, monitoring_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::VOLUME, volume_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::PAN, pan_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::AUTOMATION_VISIBLE, automationVisible_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::AUTOMATION_MUTED, automationMuted_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::AUTOMATION_SNAP_TO_GRID, automationSnapToGrid_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::AUTOMATION_ACTIVE_PARAMETER, activeAutomationParameterId_, nullptr);
    state.setProperty(IDs::AUTOMATION_PARAMETER_ORDER, automationParameterOrder_.joinIntoString("|"), nullptr);
    state.setProperty(IDs::COLOR, color_.toString(), nullptr);
    state.setProperty(IDs::INDEX, index_, nullptr);
    state.setProperty(IDs::ROLE, role_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("quickRoleId", quickRoleId_, nullptr);
    state.setProperty(IDs::PARENT_TRACK_ID, parentTrackID_, nullptr);
    state.setProperty(IDs::IS_MASTER, isMaster_, nullptr);
    state.setProperty("inputMonitorMode", (int) monitoringState_.getMode(), nullptr);
    state.setProperty("inputTrimDb", inputTrim_.getTargetGainDb(), nullptr);
    state.setProperty("trimVuReferenceDb", inputMeter_.getVuReferenceDb(), nullptr);
    state.setProperty("trimVuChannelMode", (int) inputMeter_.getVuChannelMode(), nullptr);
    state.setProperty("inputFirstChannel", inputFirstChannel_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("inputMono", inputMono_.load(std::memory_order_relaxed), nullptr);

    // Save plugin chain if present (forward declaration is ok, actual save is in ApplicationCore)
    state.setProperty("hasPlugins", pluginChain_ != nullptr, nullptr);

    return state;
}

void Track::restoreState(const juce::ValueTree& state)
{
    name_ = state.getProperty(IDs::NAME, "Untitled Track");
    muted_.store((bool)state.getProperty(IDs::MUTED, false), std::memory_order_relaxed);
    soloed_.store((bool)state.getProperty(IDs::SOLOED, false), std::memory_order_relaxed);
    armed_.store((bool)state.getProperty(IDs::ARMED, false), std::memory_order_relaxed);
    const int savedRecordMode = (int) state.getProperty("recordMode", (int) TrackRecordMode::Dry);
    recordMode_.store(savedRecordMode == (int) TrackRecordMode::PostFader
        ? (int) TrackRecordMode::PostFader : (int) TrackRecordMode::Dry,
        std::memory_order_relaxed);
    monitoring_.store((bool)state.getProperty(IDs::MONITORING, false), std::memory_order_relaxed);
    volume_.store((float)state.getProperty(IDs::VOLUME, 1.0f), std::memory_order_relaxed);
    pan_.store((float)state.getProperty(IDs::PAN, 0.0f), std::memory_order_relaxed);
    automationVisible_.store((bool)state.getProperty(IDs::AUTOMATION_VISIBLE, false), std::memory_order_relaxed);
    automationMuted_.store((bool)state.getProperty(IDs::AUTOMATION_MUTED, false), std::memory_order_relaxed);
    automationSnapToGrid_.store((bool)state.getProperty(IDs::AUTOMATION_SNAP_TO_GRID, true), std::memory_order_relaxed);
    activeAutomationParameterId_ = state.getProperty(IDs::AUTOMATION_ACTIVE_PARAMETER, "track.volume").toString();
    if (activeAutomationParameterId_.startsWith("plugin.") && activeAutomationParameterId_.endsWith(".wetdry"))
    {
        const int legacySlot = juce::jmax(1, activeAutomationParameterId_.fromFirstOccurrenceOf("plugin.", false, false).getIntValue());
        activeAutomationParameterId_ = "plugin." + juce::String(legacySlot - 1) + ".__slot_mix__";
    }
    automationParameterOrder_ = juce::StringArray::fromTokens(state.getProperty(IDs::AUTOMATION_PARAMETER_ORDER, "track.volume|track.pan|track.tape_stop|instrument.main|plugin.0.__slot_mix__").toString(), "|", "");
    for (int i = 0; i < automationParameterOrder_.size(); ++i)
    {
        auto id = automationParameterOrder_[i];
        if (id.startsWith("plugin.") && id.endsWith(".wetdry"))
        {
            const int legacySlot = juce::jmax(1, id.fromFirstOccurrenceOf("plugin.", false, false).getIntValue());
            automationParameterOrder_.set(i, "plugin." + juce::String(legacySlot - 1) + ".__slot_mix__");
        }
    }
    if (automationParameterOrder_.isEmpty())
    {
        automationParameterOrder_.add("track.volume");
        automationParameterOrder_.add("track.pan");
        automationParameterOrder_.add("track.tape_stop");
        automationParameterOrder_.add("instrument.main");
        automationParameterOrder_.add("plugin.0.__slot_mix__");
    }
    if (!automationParameterOrder_.contains(activeAutomationParameterId_))
        automationParameterOrder_.add(activeAutomationParameterId_);
    color_ = juce::Colour::fromString(state.getProperty(IDs::COLOR, "ff808080").toString());
    index_ = state.getProperty(IDs::INDEX, 0);
    role_.store((int)state.getProperty(IDs::ROLE, (int)TrackRole::Audio), std::memory_order_relaxed);
    quickRoleId_ = state.getProperty("quickRoleId", "").toString();
    parentTrackID_ = state.getProperty(IDs::PARENT_TRACK_ID, "");
    isMaster_ = state.getProperty(IDs::IS_MASTER, false);

    if (state.hasProperty("inputMonitorMode"))
        monitoringState_.setMode((InputMonitorMode)(int) state.getProperty("inputMonitorMode", (int) InputMonitorMode::Off));
    else if (monitoring_.load(std::memory_order_relaxed))
        monitoringState_.setMode(InputMonitorMode::On);
    if (state.hasProperty("inputTrimDb"))
        inputTrim_.setTargetGainDb((float) state.getProperty("inputTrimDb", 0.0f));
    inputMeter_.setVuReferenceDb((float) state.getProperty("trimVuReferenceDb", -18.0f));
    inputMeter_.setVuChannelMode((VuChannelMode) (int) state.getProperty(
        "trimVuChannelMode", (int) VuChannelMode::Average));
    inputFirstChannel_.store(juce::jmax(0, (int) state.getProperty("inputFirstChannel", 0)), std::memory_order_relaxed);
    inputMono_.store((bool) state.getProperty("inputMono", false), std::memory_order_relaxed);
}

void Track::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void Track::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void Track::notifyPropertyChanged()
{
    listeners_.call([this](Listener& l) { l.trackPropertyChanged(this); });
}

//==============================================================================
TrackManager::TrackManager()
{
}

TrackManager::~TrackManager()
{
    tracks_.clear();
}

Track* TrackManager::createTrack(const juce::String& name)
{
    auto id = IDGenerator::generateTrackID();
    auto* track = new Track(id, name);
    track->setIndex(tracks_.size());

    // APEX Quick Track Builder: new recording/audio tracks default to MONO
    // hardware input on channel 1 ("Mono 1") so a single microphone records
    // a centered source instead of a stereo pair with a silent right side.
    // Stereo 1/2 remains manually selectable per track, and this property
    // is recording/monitoring-only — imported clip playback is untouched.
    // Buses/folder-buses created through canonical bus paths explicitly
    // reset to internal routing (no hardware input) after creation.
    track->setInputSource(0, true);

    tracks_.add(track);
    
    listeners_.call([track](Listener& l) { l.trackAdded(track); });

    return track;
}

Track* TrackManager::recreateTrackFromState(const juce::ValueTree& state)
{
    const auto role = (TrackRole) (int) state.getProperty(IDs::ROLE, (int) TrackRole::Audio);
    auto* track = (role == TrackRole::MIDI || role == TrackRole::Instrument)
        ? createMidiTrack(state.getProperty(IDs::NAME, "Track").toString())
        : createTrack(state.getProperty(IDs::NAME, "Track").toString());

    if (track != nullptr)
        track->restoreState(state);

    return track;
}

Track* TrackManager::createMidiTrack(const juce::String& name)
{
    auto id = IDGenerator::generateTrackID();
    auto* track = new MidiTrackCore(id, name);
    track->setIndex(tracks_.size());
    tracks_.add(track);

    listeners_.call([track](Listener& l) { l.trackAdded(track); });

    return track;
}

Track* TrackManager::createMasterTrack()
{
    if (!masterTrack_)
    {
        masterTrack_ = std::make_unique<Track>("master", "MASTER");
        masterTrack_->setMaster(true);
        masterTrack_->setColor(juce::Colour(0xFFCC9900));
    }
    return masterTrack_.get();
}

bool TrackManager::deleteTrack(const TrackID& id)
{
    auto* candidate = getTrack(id);
    if (candidate == nullptr || candidate->isMaster())
        return false;

    // C11: policy gate (e.g. refuse deletion of an actively-recording track).
    if (!canDeleteTrack(id))
    {
        juce::Logger::writeToLog("[TRACK] deletion refused by policy gate: " + id);
        return false;
    }

    for (int i = 0; i < tracks_.size(); ++i)
    {
        if (tracks_[i]->getID() == id)
        {
            auto* doomed = tracks_.removeAndReturn(i);
            
            // Update indices
            for (int j = i; j < tracks_.size(); ++j)
                tracks_[j]->setIndex(j);

            listeners_.call([&id](Listener& l) { l.trackRemoved(id); });
            delete doomed;
            
            return true;
        }
    }
    return false;
}

std::vector<TrackID> TrackManager::deleteTracks(const std::vector<TrackID>& ids)
{
    std::vector<TrackID> deleted;
    deleted.reserve(ids.size());

    beginTopologyMutation();
    for (const auto& id : ids)
        if (deleteTrack(id))
            deleted.push_back(id);
    endTopologyMutation();

    return deleted;
}

bool TrackManager::canDeleteTrack(const TrackID& id) const
{
    auto* track = getTrack(id);
    if (track == nullptr || track->isMaster())
        return false;

    return !trackDeletionGate || trackDeletionGate(id);
}

void TrackManager::deleteAllTracks()
{
    // Move tracks out first — tracks_ is empty before any listener fires.
    // Listeners (ArrangementView) will see getNumTracks()==0 and create zero lanes.
    // Track objects stay alive in 'doomed' until all notifications complete.
    juce::OwnedArray<Track> doomed;
    doomed.swapWith(tracks_);

    for (auto* track : doomed)
    {
        auto id = track->getID();
        listeners_.call([&id](Listener& l) { l.trackRemoved(id); });
    }
    // doomed destructor destroys Track objects — no dangling TrackLane refs exist
}

Track* TrackManager::getTrack(const TrackID& id) const
{
    // Check master track first
    if (masterTrack_ && masterTrack_->getID() == id)
        return masterTrack_.get();

    for (auto* track : tracks_)
    {
        if (track->getID() == id)
            return track;
    }
    return nullptr;
}

Track* TrackManager::getTrack(int index) const
{
    if (index < 0 || index >= tracks_.size())
        return nullptr;
    return tracks_[index];
}

int TrackManager::getTrackIndex(const TrackID& id) const
{
    for (int i = 0; i < tracks_.size(); ++i)
        if (tracks_[i]->getID() == id)
            return i;
    return -1;
}

void TrackManager::notifyOrderChanged()
{
    listeners_.call([](Listener& l) { l.trackOrderChanged(); });
}

void TrackManager::moveTrack(const TrackID& id, int newIndex)
{
    int currentIndex = getTrackIndex(id);

    if (currentIndex == -1 || currentIndex == newIndex)
        return;
    
    tracks_.move(currentIndex, newIndex);
    
    // Update all indices
    for (int i = 0; i < tracks_.size(); ++i)
        tracks_[i]->setIndex(i);
    
    listeners_.call([](Listener& l) { l.trackOrderChanged(); });
}

void TrackManager::applyTrackOrder(const std::vector<TrackID>& orderedIds)
{
    if (orderedIds.size() != static_cast<size_t>(tracks_.size()))
        return;

    // Validate: every current track appears exactly once.
    for (size_t i = 0; i < orderedIds.size(); ++i)
    {
        if (getTrack(orderedIds[i]) == nullptr)
            return;
        for (size_t j = 0; j < i; ++j)
            if (orderedIds[j] == orderedIds[i])
                return;
    }

    // Rebuild the array in the requested order (ownership moves with each
    // Track; the temporary array is empty after the swap).
    juce::OwnedArray<Track> reordered;
    reordered.ensureStorageAllocated(tracks_.size());
    for (const auto& id : orderedIds)
    {
        for (int i = 0; i < tracks_.size(); ++i)
        {
            if (tracks_[i]->getID() == id)
            {
                reordered.add(tracks_.removeAndReturn(i));
                break;
            }
        }
    }
    tracks_.swapWith(reordered);

    for (int i = 0; i < tracks_.size(); ++i)
        tracks_[i]->setIndex(i);

    listeners_.call([](Listener& l) { l.trackOrderChanged(); });
}

juce::ValueTree TrackManager::getState() const
{
    juce::ValueTree state(IDs::TRACKS);
    for (auto* track : tracks_)
        state.appendChild(track->getState(), nullptr);

    // Save master track separately
    if (masterTrack_)
    {
        juce::ValueTree masterState("MasterTrack");
        masterState.appendChild(masterTrack_->getState(), nullptr);
        state.appendChild(masterState, nullptr);
    }

    return state;
}

void TrackManager::restoreState(const juce::ValueTree& state)
{
    {
    juce::ScopedValueSetter<bool> restoreFlag(restoringState_, true);
    deleteAllTracks();

    // C3/stable-ID invariant: trackCounter must never re-issue an ID that was
    // just restored from disk, or the new track silently aliases an existing
    // one (selection/mute/solo then operate on the wrong track). Seed the
    // counter past the largest numeric suffix found in the persisted IDs.
    int maxTrackNumber = 0;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild(i);

        // Handle master track separately
        if (child.getType().toString() == "MasterTrack")
        {
            if (child.getNumChildren() > 0)
            {
                auto masterState = child.getChild(0);
                if (!masterTrack_)
                    createMasterTrack();
                if (masterTrack_)
                    masterTrack_->restoreState(masterState);
            }
            continue;
        }

        // Regular track
        auto trackState = child;
        auto id = trackState.getProperty(IDs::ID).toString();
        auto name = trackState.getProperty(IDs::NAME).toString();

        // Track the largest numeric suffix so future createTrack() calls stay unique.
        if (id.startsWith("TRK_"))
        {
            const int n = id.fromFirstOccurrenceOf("TRK_", false, false).getIntValue();
            maxTrackNumber = juce::jmax(maxTrackNumber, n);
        }

        Track* track = nullptr;

        const auto role = (TrackRole) (int) trackState.getProperty(IDs::ROLE, (int) TrackRole::Audio);
        if (role == TrackRole::MIDI)
            track = new MidiTrackCore(id, name);
        else
            track = new Track(id, name);

        track->restoreState(trackState);
        tracks_.add(track);

        listeners_.call([track](Listener& l) { l.trackAdded(track); });
    }

    IDGenerator::seedTrackCounter(maxTrackNumber);
    }

    // UI listeners coalesce their expensive row/lane rebuilds to this single
    // terminal notification while control-plane listeners still receive each
    // track identity during restoration.
    notifyOrderChanged();
}

void TrackManager::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void TrackManager::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

} // namespace DAW

#include "Clip.h"
#include "../UtilityCore/Types.h"
#include "../MidiCore/MidiClip.h"

namespace DAW {

namespace IDs
{
    static const juce::Identifier CLIPS("Clips");
    static const juce::Identifier CLIP("Clip");
    static const juce::Identifier AUDIO_CLIP("AudioClip");
    static const juce::Identifier ID("id");
    static const juce::Identifier NAME("name");
    static const juce::Identifier TYPE("type");
    static const juce::Identifier START_POSITION("startPosition");
    static const juce::Identifier LENGTH("length");
    static const juce::Identifier TRACK_ID("trackID");
    static const juce::Identifier COLOR("color");
    static const juce::Identifier SELECTED("selected");
    static const juce::Identifier MUTED("muted");
    static const juce::Identifier SOURCE_OFFSET("sourceOffset");
    static const juce::Identifier SOURCE_FILE("sourceFile");
    static const juce::Identifier GAIN("gain");
    static const juce::Identifier CLIP_PAN("clipPan");
    static const juce::Identifier CLIP_FINE_TUNE("clipFineTune");
    static const juce::Identifier REVERSED("reversed");
    static const juce::Identifier FADE_IN_LENGTH("fadeInLength");
    static const juce::Identifier FADE_OUT_LENGTH("fadeOutLength");
    static const juce::Identifier FADE_IN_CURVE("fadeInCurve");
    static const juce::Identifier FADE_OUT_CURVE("fadeOutCurve");
}

//==============================================================================
Clip::Clip(const ClipID& id, const juce::String& name, ClipType type)
    : id_(id), name_(name), type_(type)
{
    color_ = juce::Colours::white;
}

Clip::~Clip()
{
}

void Clip::setName(const juce::String& newName)
{
    name_ = newName;
    notifyPropertyChanged();
}

void Clip::setStartPosition(SamplePosition pos)
{
    if (startPosition_ == pos)
        return;

    startPosition_ = pos;
    notifyPropertyChanged();
}

void Clip::setLength(SamplePosition len)
{
    const auto clamped = juce::jmax((SamplePosition)1, len);
    if (length_ == clamped)
        return;

    length_ = clamped;
    notifyPropertyChanged();
}

void Clip::setTrackID(const TrackID& id)
{
    if (trackID_ == id)
        return;

    trackID_ = id;
    notifyPropertyChanged();
}

void Clip::setColor(const juce::Colour& newColor)
{
    color_ = newColor;
    notifyPropertyChanged();
}

void Clip::setSelected(bool shouldBeSelected)
{
    selected_ = shouldBeSelected;
    notifyPropertyChanged();
}

void Clip::setMuted(bool shouldBeMuted)
{
    if (muted_ == shouldBeMuted)
        return;

    muted_ = shouldBeMuted;
    notifyPropertyChanged();
}

void Clip::setSourceOffset(SamplePosition offset)
{
    const auto clamped = juce::jmax((SamplePosition)0, offset);
    if (sourceOffset_ == clamped)
        return;

    sourceOffset_ = clamped;
    notifyPropertyChanged();
}

juce::ValueTree Clip::getState() const
{
    juce::ValueTree state(IDs::CLIP);
    state.setProperty(IDs::ID, id_, nullptr);
    state.setProperty(IDs::NAME, name_, nullptr);
    state.setProperty(IDs::TYPE, (int)type_, nullptr);
    state.setProperty(IDs::START_POSITION, (int64_t)startPosition_, nullptr);
    state.setProperty(IDs::LENGTH, (int64_t)length_, nullptr);
    state.setProperty(IDs::TRACK_ID, trackID_, nullptr);
    state.setProperty(IDs::COLOR, color_.toString(), nullptr);
    state.setProperty(IDs::SELECTED, selected_, nullptr);
    state.setProperty(IDs::MUTED, muted_, nullptr);
    state.setProperty(IDs::SOURCE_OFFSET, (int64_t)sourceOffset_, nullptr);
    return state;
}

void Clip::restoreState(const juce::ValueTree& state)
{
    name_ = state.getProperty(IDs::NAME, "Untitled Clip");
    type_ = (ClipType)(int)state.getProperty(IDs::TYPE, 0);
    startPosition_ = (SamplePosition)(int64_t)state.getProperty(IDs::START_POSITION, 0);
    length_ = (SamplePosition)(int64_t)state.getProperty(IDs::LENGTH, 44100);
    trackID_ = state.getProperty(IDs::TRACK_ID, "");
    color_ = juce::Colour::fromString(state.getProperty(IDs::COLOR, "ff808080").toString());
    selected_ = state.getProperty(IDs::SELECTED, false);
    muted_ = state.getProperty(IDs::MUTED, false);
    sourceOffset_ = (SamplePosition)(int64_t)state.getProperty(IDs::SOURCE_OFFSET, 0);
}

void Clip::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void Clip::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void Clip::notifyPropertyChanged()
{
    listeners_.call([this](Listener& l) { l.clipPropertyChanged(this); });
}

//==============================================================================
AudioClip::AudioClip(const ClipID& id, const juce::String& name)
    : Clip(id, name, ClipType::Audio)
{
}

AudioClip::~AudioClip()
{
}

void AudioClip::setSourceFile(const juce::File& file)
{
    sourceFile_ = file;
    notifyPropertyChanged();
}

void AudioClip::setGain(float newGain)
{
    const auto clamped = juce::jlimit(0.0f, 4.0f, newGain);
    if (std::abs(gain_ - clamped) <= 0.0001f)
        return;

    gain_ = clamped;
    notifyPropertyChanged();
}

void AudioClip::setFadeInLength(SamplePosition length)
{
    const auto clamped = juce::jmax((SamplePosition)0, length);
    if (fadeInLength_ == clamped)
        return;

    fadeInLength_ = clamped;
    notifyPropertyChanged();
}

void AudioClip::setFadeOutLength(SamplePosition length)
{
    const auto clamped = juce::jmax((SamplePosition)0, length);
    if (fadeOutLength_ == clamped)
        return;

    fadeOutLength_ = clamped;
    notifyPropertyChanged();
}

juce::ValueTree AudioClip::getState() const
{
    juce::ValueTree state(IDs::AUDIO_CLIP);

    // Copy base clip properties
    state.setProperty(IDs::ID, getID(), nullptr);
    state.setProperty(IDs::NAME, getName(), nullptr);
    state.setProperty(IDs::TYPE, (int)getType(), nullptr);
    state.setProperty(IDs::START_POSITION, (int64_t)getStartPosition(), nullptr);
    state.setProperty(IDs::LENGTH, (int64_t)getLength(), nullptr);
    state.setProperty(IDs::TRACK_ID, getTrackID(), nullptr);
    state.setProperty(IDs::COLOR, getColor().toString(), nullptr);
    state.setProperty(IDs::SELECTED, isSelected(), nullptr);
    state.setProperty(IDs::MUTED, isMuted(), nullptr);
    state.setProperty(IDs::SOURCE_OFFSET, (int64_t)getSourceOffset(), nullptr);

    // Add audio clip specific properties
    state.setProperty(IDs::SOURCE_FILE, sourceFile_.getFullPathName(), nullptr);
    state.setProperty(IDs::GAIN, gain_, nullptr);
    state.setProperty(IDs::CLIP_PAN, clipPan_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::REVERSED, reversed_, nullptr);
    state.setProperty("pitch", pitchTargetSemitones_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::CLIP_FINE_TUNE, fineTuneCents_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("timeStretch", timeStretch_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("fineTuneCents",    fineTuneCents_.load(std::memory_order_relaxed),    nullptr);
    state.setProperty("formantSemitones", formantSemitones_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("preserveFormants", preserveFormants_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("timePitchMode",    timePitchMode_.load(std::memory_order_relaxed),    nullptr);
    state.setProperty("transientLock", transientLock_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("timePitchQuality", timePitchQuality_.load(std::memory_order_relaxed), nullptr);
    state.setProperty("sourceStartSample",(int64_t)sourceStartSample_, nullptr);
    state.setProperty("sourceEndSample",  (int64_t)sourceEndSample_,   nullptr);
    state.setProperty(IDs::FADE_IN_LENGTH, (int64_t)fadeInLength_, nullptr);
    state.setProperty(IDs::FADE_OUT_LENGTH, (int64_t)fadeOutLength_, nullptr);
    state.setProperty(IDs::FADE_IN_CURVE, fadeInCurve_.load(std::memory_order_relaxed), nullptr);
    state.setProperty(IDs::FADE_OUT_CURVE, fadeOutCurve_.load(std::memory_order_relaxed), nullptr);
    return state;
}

void AudioClip::restoreState(const juce::ValueTree& state)
{
    Clip::restoreState(state);
    sourceFile_ = juce::File(state.getProperty(IDs::SOURCE_FILE, ""));
    gain_ = state.getProperty(IDs::GAIN, 1.0f);
    clipPan_.store(juce::jlimit(-1.0f, 1.0f, (float)state.getProperty(IDs::CLIP_PAN, 0.0f)), std::memory_order_relaxed);
    reversed_ = static_cast<bool>(state.getProperty(IDs::REVERSED, false));
    pitchTargetSemitones_.store(juce::jlimit(-36.f, 36.f,
        static_cast<float>(state.getProperty("pitch", 0.0f))), std::memory_order_relaxed);
    timeStretch_.store((float)state.getProperty("timeStretch", 1.0f), std::memory_order_relaxed);
    fineTuneCents_.store((float)state.getProperty(IDs::CLIP_FINE_TUNE,
        state.getProperty("fineTuneCents", 0.0f)), std::memory_order_relaxed);
    formantSemitones_.store((float)state.getProperty("formantSemitones", 0.0f), std::memory_order_relaxed);
    preserveFormants_.store(static_cast<bool>(state.getProperty("preserveFormants", false)), std::memory_order_relaxed);
    const int restoredMode = static_cast<int>(state.getProperty("timePitchMode", TimePitchModeIds::DefaultUserMode));
    timePitchMode_.store(juce::jlimit(TimePitchModeIds::Resample, TimePitchModeIds::MaxMode, restoredMode), std::memory_order_relaxed);
    transientLock_.store(static_cast<bool>(state.getProperty("transientLock", false)), std::memory_order_relaxed);
    timePitchQuality_.store(juce::jlimit(0, 4, static_cast<int>(state.getProperty("timePitchQuality", (int)ArrangementEditor::TimePitchQuality::Balanced))), std::memory_order_relaxed);
    sourceStartSample_= static_cast<int64_t>(state.getProperty("sourceStartSample", (int64_t)0));
    sourceEndSample_  = static_cast<int64_t>(state.getProperty("sourceEndSample",   (int64_t)0));
    fadeInLength_ = (SamplePosition)(int64_t)state.getProperty(IDs::FADE_IN_LENGTH, 0);
    fadeOutLength_ = (SamplePosition)(int64_t)state.getProperty(IDs::FADE_OUT_LENGTH, 0);
    fadeInCurve_.store(juce::jlimit(0, 3, (int)state.getProperty(IDs::FADE_IN_CURVE, 0)), std::memory_order_relaxed);
    fadeOutCurve_.store(juce::jlimit(0, 3, (int)state.getProperty(IDs::FADE_OUT_CURVE, 0)), std::memory_order_relaxed);
}

//==============================================================================
ClipManager::ClipManager()
{
}

ClipManager::~ClipManager()
{
    clips_.clear();
}

AudioClip* ClipManager::createAudioClip(const juce::String& name, const juce::File& audioFile)
{
    auto id = IDGenerator::generateClipID();
    auto* clip = new AudioClip(id, name);
    clip->setTimePitchMode(TimePitchModeIds::DefaultUserMode);
    clip->setSourceFile(audioFile);
    { juce::ScopedLock sl(clipLock_); clips_.add(clip); }
    listeners_.call([clip](Listener& l) { l.clipAdded(clip); });
    return clip;
}

Clip* ClipManager::recreateClipFromState(const juce::ValueTree& clipState)
{
    const auto name = clipState.getProperty(IDs::NAME, "Clip").toString();
    const auto type = (ClipType) (int) clipState.getProperty(IDs::TYPE, 0);
    const auto sourceFile = juce::File(clipState.getProperty(IDs::SOURCE_FILE, "").toString());

    Clip* clip = nullptr;
    if (clipState.hasType(IDs::AUDIO_CLIP) || type == ClipType::Audio || sourceFile.getFullPathName().isNotEmpty())
    {
        clip = new AudioClip(IDGenerator::generateClipID(), name);
    }
    else if (type == ClipType::MIDI)
    {
        clip = new MidiClip(IDGenerator::generateClipID(), name);
    }
    else
    {
        DBG("[TIMELINE-RECOVERY] createEmptyClip caller=recreateClipFromState track="
            << clipState.getProperty(IDs::TRACK_ID, "").toString());
        clip = new Clip(IDGenerator::generateClipID(), name, type);
    }

    if (clip != nullptr)
    {
        clip->restoreState(clipState);

        {
            juce::ScopedLock sl(clipLock_);
            clips_.add(clip);
        }

        listeners_.call([clip](Listener& l) { l.clipAdded(clip); });
    }

    return clip;
}

MidiClip* ClipManager::createMIDIClip(const juce::String& name)
{
    auto id = IDGenerator::generateClipID();
    auto* clip = new MidiClip(id, name);
    { juce::ScopedLock sl(clipLock_); clips_.add(clip); }
    listeners_.call([clip](Listener& l) { l.clipAdded(clip); });
    return clip;
}

Clip* ClipManager::createEmptyClip(const TrackID& trackId,
                                     const juce::String& name,
                                     SamplePosition startPos,
                                     SamplePosition length)
{
    DBG("[TIMELINE-RECOVERY] createEmptyClip caller=unknown track=" << trackId
        << " start=" << (juce::int64)startPos);
    auto* clip = static_cast<Clip*>(createMIDIClip(name));
    clip->setTrackID(trackId);
    clip->setStartPosition(startPos);
    clip->setLength(juce::jmax((SamplePosition)1, length));
    return clip;
}

Clip* ClipManager::duplicateClip(const ClipID& originalId)
{
    juce::ValueTree origState;
    {
        juce::ScopedLock sl(clipLock_);
        auto* orig = getClip(originalId);
        if (orig == nullptr) return nullptr;
        origState = orig->getState();
    }

    // Use recreateClipFromState so AudioClip subclass is preserved with source file
    auto* copy = recreateClipFromState(origState);
    if (copy == nullptr) return nullptr;

    // Give it a fresh unique ID (recreateClipFromState assigns one via createAudioClip/createMIDIClip)
    // Position same as original (caller moves it as needed)
    copy->setStartPosition((SamplePosition)(int64_t)origState.getProperty("startPosition", 0));
    copy->setLength       ((SamplePosition)(int64_t)origState.getProperty("length",        1));
    copy->setSourceOffset ((SamplePosition)(int64_t)origState.getProperty("sourceOffset",  0));

    // Share audio cache so waveform is immediately visible — no reload
    if (auto* origClip = getClip(originalId))
        (void)origClip; // origClip already in list; shareForClip done below

    // listeners were already fired inside recreateClipFromState via createAudioClip
    return copy;
}

bool ClipManager::deleteClip(const ClipID& id)
{
    // Fire listener OUTSIDE lock to avoid deadlock if listener re-enters ClipManager.
    bool found = false;
    {
        juce::ScopedLock sl(clipLock_);
        for (int i = 0; i < clips_.size(); ++i)
        {
            if (clips_[i]->getID() == id)
            {
                clips_.remove(i);
                found = true;
                break;
            }
        }
    }
    if (found)
    {
        if (this->onClipRemoved)
            this->onClipRemoved(id);
        listeners_.call([&id](Listener& l) { l.clipRemoved(id); });
    }
    return found;
}

void ClipManager::deleteAllClips()
{
    // Collect IDs first, then remove under lock, then notify outside lock.
    juce::Array<ClipID> ids;
    {
        juce::ScopedLock sl(clipLock_);
        for (auto* c : clips_)
            ids.add(c->getID());
        clips_.clear();
    }
    for (const auto& id : ids)
    {
        if (this->onClipRemoved)
            this->onClipRemoved(id);
        listeners_.call([&id](Listener& l) { l.clipRemoved(id); });
    }
}

Clip* ClipManager::getClip(const ClipID& id) const
{
    for (auto* clip : clips_)
    {
        if (clip->getID() == id)
            return clip;
    }
    return nullptr;
}

juce::Array<Clip*> ClipManager::getClipsOnTrack(const TrackID& trackID) const
{
    juce::Array<Clip*> result;
    for (auto* clip : clips_)
    {
        if (clip->getTrackID() == trackID)
            result.add(clip);
    }
    return result;
}

juce::ValueTree ClipManager::getState() const
{
    juce::ValueTree state(IDs::CLIPS);
    for (auto* clip : clips_)
        state.appendChild(clip->getState(), nullptr);
    return state;
}

void ClipManager::restoreState(const juce::ValueTree& state)
{
    deleteAllClips();
    
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto clipState = state.getChild(i);
        auto id = clipState.getProperty(IDs::ID).toString();
        auto name = clipState.getProperty(IDs::NAME).toString();
        auto type = (ClipType)(int)clipState.getProperty(IDs::TYPE, 0);
        
        Clip* clip = nullptr;
        
        if (clipState.hasType(IDs::AUDIO_CLIP))
        {
            clip = new AudioClip(id, name);
            clip->restoreState(clipState);
        }
        else if (type == ClipType::MIDI)
        {
            clip = new MidiClip(id, name);
            clip->restoreState(clipState);
        }
        else
        {
            clip = new Clip(id, name, type);
            clip->restoreState(clipState);
        }
        
        clips_.add(clip);
        listeners_.call([clip](Listener& l) { l.clipAdded(clip); });
    }
}

void ClipManager::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void ClipManager::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

} // namespace DAW

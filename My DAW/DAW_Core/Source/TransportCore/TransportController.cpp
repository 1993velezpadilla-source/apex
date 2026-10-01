#include "TransportController.h"

namespace DAW {

TransportController::TransportController(ApplicationState& appState)
    : state_(appState)
{
    currentPosition_.store((SamplePosition)(juce::int64)state_.transportPosition.getValue(), std::memory_order_relaxed);
    isPlayingAtomic_.store((bool) state_.isPlaying.getValue(), std::memory_order_relaxed);
    isRecordingAtomic_.store((bool) state_.isRecording.getValue(), std::memory_order_relaxed);
    isPausedAtomic_.store((bool) state_.isPaused.getValue(), std::memory_order_relaxed);
    isLoopingAtomic_.store((bool) state_.isLooping.getValue(), std::memory_order_relaxed);
    tempoAtomic_.store((double) state_.tempo.getValue(), std::memory_order_relaxed);
}

TransportController::~TransportController()
{
}

// ── RT-safe state application (audio thread — atomic stores only) ──────

void TransportController::applyPlayRT(SamplePosition startPos) noexcept
{
    currentPosition_.store(startPos, std::memory_order_relaxed);
    isPlayingAtomic_.store(true, std::memory_order_relaxed);
    isPausedAtomic_.store(false, std::memory_order_relaxed);
}

void TransportController::applyPauseRT() noexcept
{
    isPausedAtomic_.store(true, std::memory_order_relaxed);
    isPlayingAtomic_.store(false, std::memory_order_relaxed);
}

void TransportController::applyRecordRT(SamplePosition startPos) noexcept
{
    currentPosition_.store(startPos, std::memory_order_relaxed);
    isRecordingAtomic_.store(true, std::memory_order_relaxed);
    isPlayingAtomic_.store(true, std::memory_order_relaxed);
    isPausedAtomic_.store(false, std::memory_order_relaxed);
}

void TransportController::applyStopRecordingRT() noexcept
{
    isRecordingAtomic_.store(false, std::memory_order_relaxed);
}

// ── Message-thread transport control (full path: state + Logger + notify) ──

void TransportController::play()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::play() called");

    if (!isPlaying())
    {
        playStartPosition_ = userCursorPosition_;
        state_.transportPosition.setValue((int64_t)userCursorPosition_);
    }
    applyPlayRT(isPlaying() ? currentPosition_.load(std::memory_order_relaxed) : userCursorPosition_);
    state_.isPlaying.setValue(true);
    state_.isPaused.setValue(false);
    notifyTransportStateChanged();
}

void TransportController::stop()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::stop() called");

    SamplePosition stoppedAt = currentPosition_.load(std::memory_order_relaxed);

    isPlayingAtomic_.store(false, std::memory_order_relaxed);
    isPausedAtomic_.store(false, std::memory_order_relaxed);
    isRecordingAtomic_.store(false, std::memory_order_relaxed);
    state_.isPlaying.setValue(false);
    state_.isPaused.setValue(false);
    state_.isRecording.setValue(false);

    if (returnToLastPosition_)
    {
        setPosition(playStartPosition_);
    }
    else
    {
        userCursorPosition_ = stoppedAt;
        setPosition(stoppedAt);
    }

    notifyTransportStateChanged();
}

void TransportController::returnToZero()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::returnToZero() called");

    userCursorPosition_ = 0;
    setPosition(0);
}

void TransportController::pause()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::pause() called");

    applyPauseRT();
    state_.isPaused.setValue(true);
    state_.isPlaying.setValue(false);
    notifyTransportStateChanged();
}

void TransportController::togglePlayPause()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::togglePlayPause() called");

    if (isPlaying())
    {
        stop();
    }
    else
    {
        play();
    }
}

void TransportController::record()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::record() called");

    if (!isPlaying())
    {
        playStartPosition_ = userCursorPosition_;
        state_.transportPosition.setValue((int64_t)userCursorPosition_);
    }
    applyRecordRT(isPlaying() ? currentPosition_.load(std::memory_order_relaxed) : userCursorPosition_);
    state_.isRecording.setValue(true);
    state_.isPlaying.setValue(true);
    state_.isPaused.setValue(false);
    notifyTransportStateChanged();
}

void TransportController::recordWithoutSafetyCheck()
{
    record();
}

void TransportController::stopRecording()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::stopRecording() called");

    applyStopRecordingRT();
    state_.isRecording.setValue(false);
    notifyTransportStateChanged();
}

void TransportController::toggleRecord()
{
    juce::Logger::writeToLog ("[AUTO-TRANSPORT] TransportController::toggleRecord() called");

    if (isRecording())
        stopRecording();
    else
        record();
}

void TransportController::setLooping(bool shouldLoop)
{
    isLoopingAtomic_.store(shouldLoop, std::memory_order_relaxed);
    state_.isLooping.setValue(shouldLoop);
    notifyTransportStateChanged();
}

bool TransportController::isLooping() const
{
    return isLoopingAtomic_.load(std::memory_order_relaxed);
}

void TransportController::setLoopRange(SamplePosition start, SamplePosition end)
{
    loopStart_.store(start, std::memory_order_relaxed);
    loopEnd_.store(end, std::memory_order_relaxed);
    notifyLoopRangeChanged();
}

std::pair<SamplePosition, SamplePosition> TransportController::getLoopRange() const
{
    return {loopStart_.load(std::memory_order_relaxed), loopEnd_.load(std::memory_order_relaxed)};
}

void TransportController::setTempo(double bpm)
{
    const auto clamped = juce::jlimit(20.0, 999.0, bpm);
    tempoAtomic_.store(clamped, std::memory_order_relaxed);
    state_.tempo.setValue(clamped);
    notifyTempoChanged();
}

double TransportController::getTempo() const
{
    return tempoAtomic_.load(std::memory_order_relaxed);
}

void TransportController::setPosition(SamplePosition samples)
{
    currentPosition_.store(samples, std::memory_order_relaxed);
    state_.transportPosition.setValue((int64_t)samples);
    // Track the user-chosen cursor position so stop() can always return
    // here. Only update when not playing — during playback this is called
    // by ruler scrubbing and we do NOT want to change the return point.
    if (!isPlaying())
    {
        userCursorPosition_ = samples;
        playStartPosition_  = samples;
    }
    notifyPositionChanged();
}

void TransportController::setPositionFromAudioThread(SamplePosition samples) noexcept
{
    currentPosition_.store(samples, std::memory_order_relaxed);
}

void TransportController::setPositionBarBeat(BarBeatPosition barBeat)
{
    state_.barBeatPosition.setValue(barBeat);
    // Convert to samples would happen here with sample rate
    notifyPositionChanged();
}

SamplePosition TransportController::getPosition() const
{
    return currentPosition_.load(std::memory_order_relaxed);
}

BarBeatPosition TransportController::getPositionBarBeat() const
{
    return state_.barBeatPosition.getValue();
}

SamplePosition TransportController::barBeatToSamples(BarBeatPosition barBeat, double sampleRate) const
{
    double secondsPerBeat = 60.0 / getTempo();
    double beatsPerBar = 4.0; // Could be configurable time signature
    double totalBeats = barBeat * beatsPerBar;
    double totalSeconds = totalBeats * secondsPerBeat;
    return static_cast<SamplePosition>(totalSeconds * sampleRate);
}

BarBeatPosition TransportController::samplesToBarBeat(SamplePosition samples, double sampleRate) const
{
    double totalSeconds = samples / sampleRate;
    double secondsPerBeat = 60.0 / getTempo();
    double totalBeats = totalSeconds / secondsPerBeat;
    double beatsPerBar = 4.0;
    return totalBeats / beatsPerBar;
}

bool TransportController::isPlaying() const
{
    return isPlayingAtomic_.load(std::memory_order_relaxed);
}

bool TransportController::isRecording() const
{
    return isRecordingAtomic_.load(std::memory_order_relaxed);
}

bool TransportController::isPaused() const
{
    return isPausedAtomic_.load(std::memory_order_relaxed);
}

void TransportController::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void TransportController::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

void TransportController::notifyTransportStateChanged()
{
    listeners_.call([this](Listener& l) { l.transportStateChanged(); });
}

void TransportController::notifyTempoChanged()
{
    listeners_.call([this](Listener& l) { l.tempoChanged(getTempo()); });
}

void TransportController::notifyPositionChanged()
{
    listeners_.call([this](Listener& l) { l.positionChanged(getPosition()); });
}

void TransportController::setReturnToLastPosition(bool enabled)
{
    returnToLastPosition_ = enabled;

    // Enabling return-to-start while stopped anchors the home at the current
    // playhead position immediately, so the return target is exactly where
    // the playhead was last placed — no stop press is required to apply it.
    if (enabled && !isPlaying())
    {
        const auto pos = currentPosition_.load(std::memory_order_relaxed);
        playStartPosition_  = pos;
        userCursorPosition_ = pos;
    }
}

bool TransportController::shouldReturnToLastPosition() const
{
    return returnToLastPosition_;
}

void TransportController::notifyLoopRangeChanged()
{
    listeners_.call([this](Listener& l)
    {
        l.loopRangeChanged(loopStart_.load(std::memory_order_relaxed),
                           loopEnd_.load(std::memory_order_relaxed));
    });
}

} // namespace DAW

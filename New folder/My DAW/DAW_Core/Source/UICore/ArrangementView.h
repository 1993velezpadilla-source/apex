#pragma once

#include <JuceHeader.h>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../TrackCore/Track.h"
#include "../ClipCore/Clip.h"
#include "../MidiCore/MidiClip.h"
#include "../StateCore/ApplicationState.h"
#include "../MarkerCore/MarkerManager.h"
#include "../TransportCore/TransportController.h"
#include "../AudioEngineCore/AudioFileManager.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../PluginHostCore/PluginChainCore.h"
#include "../RoutingCore/RoutingGraph.h"
#include "../../Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.h"

namespace DAW {

enum class SnapMode { Free, Note_1_32, Note_1_16, Note_1_8, Note_1_4, Note_1_2, Bar_1, Bar_2, Bar_4 };
enum class EditTool { Select, Blade, RazorEdit, Eraser, Mute };

class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private Track::Listener,
                        private TrackManager::Listener,
                        private ClipManager::Listener,
                        private Clip::Listener,
                        private TransportController::Listener,
                        private juce::Timer
{
    class ImportOverlayComponent : public juce::Component
    {
    public:
        ImportOverlayComponent()
        {
            setInterceptsMouseClicks(false, false);
            setAlwaysOnTop(true);
        }

        void setState(const juce::String& title, const juce::String& detail, double progress)
        {
            title_ = title;
            detail_ = detail;
            progress_ = juce::jlimit(0.0, 1.0, progress);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            auto box = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(juce::Colour(0xF20D0D10));
            g.fillRoundedRectangle(box, 10.0f);
            g.setColour(juce::Colours::white.withAlpha(0.16f));
            g.drawRoundedRectangle(box, 10.0f, 1.0f);

            auto content = getLocalBounds().reduced(14, 10);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(15.0f, juce::Font::bold));
            g.drawText(title_, content.removeFromTop(24), juce::Justification::centredLeft, true);

            g.setFont(juce::Font(12.0f));
            g.setColour(juce::Colours::white.withAlpha(0.86f));
            g.drawText(detail_, content.removeFromTop(22), juce::Justification::centredLeft, true);

            auto barArea = content.removeFromTop(12).reduced(0, 1);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.fillRoundedRectangle(barArea.toFloat(), 5.0f);

            auto fillArea = barArea.withWidth((int) std::round((double) barArea.getWidth() * progress_));
            if (fillArea.getWidth() > 0)
            {
                g.setColour(juce::Colour(0xFF8E5CFF));
                g.fillRoundedRectangle(fillArea.toFloat(), 5.0f);
            }
        }

    private:
        juce::String title_;
        juce::String detail_;
        double progress_ = 0.0;
    };

public:
    ArrangementView(TrackManager& trackManager,
                    ClipManager& clipManager,
                    ApplicationState& state,
                    MarkerManager& markers,
                    TransportController& transport,
                    AudioFileManager* audioFiles = nullptr,
                    AutomationManagerCore* automation = nullptr)
        : trackManager_(trackManager), clipManager_(clipManager), state_(state),
          markers_(markers), transport_(transport), audioFiles_(audioFiles), automation_(automation)
    {
        core_ = std::make_unique<ArrangementEditor::ArrangementViewCore>();
        core_->setAudioEngineBridge(&clipManager_, audioFiles_, &trackManager_, 44100.0);
        core_->setAutomationManager(automation_);
        core_->onPlayheadMoved = [this](double timeSeconds)
        {
            const auto samples = (SamplePosition)std::llround(timeSeconds * juce::jmax(1.0, core_->m_engineSampleRate));
            transport_.setPosition(samples);
        };
        core_->onEngineClipSelected = [this](Clip& clip)
        {
            state_.selectedClipID.setValue(clip.getID());
            state_.selectedTrackID.setValue(clip.getTrackID());
            if (onBoxSelectionCommitted)
                onBoxSelectionCommitted({ clip.getID() }, false);
        };
        core_->onEngineClipSelectionCleared = [this]()
        {
            state_.selectedClipID.setValue({});
            if (onClipSelectionCleared)
                onClipSelectionCleared();
        };
        core_->onEngineClipDoubleClicked = [this](Clip& clip)
        {
            if (onOpenClipProperties)
                onOpenClipProperties(clip);
        };
        core_->onEngineClipAutomationRequested = [this](Clip& clip)
        {
            if (onOpenClipAutomation)
                onOpenClipAutomation(clip);
            else if (onOpenClipProperties)
                onOpenClipProperties(clip);
        };
        core_->onEngineClipVocalTuneRequested = [this](Clip& clip)
        {
            state_.selectedClipID.setValue(clip.getID());
            state_.selectedTrackID.setValue(clip.getTrackID());
            if (onOpenClipVocalTune)
                onOpenClipVocalTune(clip);
        };
        core_->onEngineClipVocalTuneBypassToggled = [this](Clip& clip)
        {
            if (onToggleClipVocalTuneBypass)
                onToggleClipVocalTuneBypass(clip);
        };
        core_->onEngineClipIsVocalTuneBypassed = [this](const Clip& clip) -> bool
        {
            if (onIsClipVocalTuneBypassed)
                return onIsClipVocalTuneBypassed(clip);
            return false;
        };
        core_->onEngineMidiClipDoubleClicked = [this](MidiClip& clip)
        {
            if (onOpenMidiClip)
                onOpenMidiClip(clip);
        };

        // Wire zoom changes → header sync so TrackList row heights stay
        // in sync with ArrangementViewCore track height at all times.
        core_->getZoom().onZoomChanged = [this]()
        {
            defaultLaneHeight_ = (int)core_->getZoom().getTrackHeightPx();
            if (core_ != nullptr)
                core_->resized();
            if (onLaneLayoutChanged)
                onLaneLayoutChanged();
            repaint();
        };

        addAndMakeVisible(*core_);
        addAndMakeVisible(importOverlay_);
        importOverlay_.setVisible(false);
        importOverlay_.toFront(false);
        trackManager_.addListener(this);
        attachTrackListeners();
        clipManager_.addListener(this);
        attachClipListeners();
        transport_.addListener(this);
        mirrorAllEngineClipsIntoArrangement();
        syncAutomationLanesFromTracks();
        // Timer is for playhead repaint only — clip mirroring is event-driven
        // via clipAdded/clipRemoved to avoid the 30Hz repaint storm.
        startTimerHz(30);
    }

    void setMetronomeBindings(std::function<bool()> isEnabled, std::function<void()> toggle)
    {
        if (core_ != nullptr)
        {
            core_->getToolbar().setMetronomeEnabledQuery(std::move(isEnabled));
            core_->getToolbar().setMetronomeToggleAction(std::move(toggle));
        }
    }

    ~ArrangementView() override
    {
        stopTimer();
        detachTrackListeners();
        trackManager_.removeListener(this);
        detachClipListeners();
        clipManager_.removeListener(this);
        transport_.removeListener(this);
        if (core_ != nullptr)
        {
            core_->onEngineClipSelected = nullptr;
            core_->onEngineClipSelectionCleared = nullptr;
            core_->onEngineClipDoubleClicked = nullptr;
            core_->onEngineClipAutomationRequested = nullptr;
            core_->onEngineClipVocalTuneRequested = nullptr;
            core_->onEngineClipVocalTuneBypassToggled = nullptr;
            core_->onEngineClipIsVocalTuneBypassed = nullptr;
            core_->onEngineMidiClipDoubleClicked = nullptr;
            core_->onPlayheadMoved = nullptr;
        }
    }

    void setPluginChainProvider(std::function<PluginChainCore*(const TrackID&)> provider) { pluginChainProvider_ = std::move(provider); }

    void requestClipRemirror()
    {
        mirrorAllEngineClipsIntoArrangement();
        repaint();
    }

    void setRoutingGraphProvider(std::function<RoutingGraph*()> provider)
    {
        routingGraphProvider_ = std::move(provider);
        if (core_ != nullptr)
            core_->setRoutingGraph(routingGraphProvider_ ? routingGraphProvider_() : nullptr);
    }

    void setEngineSampleRate(double sampleRate)
    {
        sampleRate = juce::jmax(1.0, sampleRate);
        currentEngineSampleRate_ = sampleRate;

        if (core_ != nullptr)
            core_->setEngineSampleRate(sampleRate);
        mirrorAllEngineClipsIntoArrangement();
    }

    // Transport controller listener - update UI tempo when transport tempo changes
    void tempoChanged(double newTempo) override
    {
        if (core_ != nullptr)
            core_->setTempoBpm(newTempo);
    }

    int getVerticalScrollOffset() const noexcept { return verticalScrollOffset_; }

    int getNumLanes() const noexcept
    {
        return trackManager_.getNumTracks() + (trackManager_.hasMasterTrack() ? 1 : 0);
    }

    int getLaneHeight(int /*index*/) const
    {
        // ArrangementViewCore is the single source of truth for lane height.
        // All lanes share one track height so headers and timeline always agree.
        if (core_ != nullptr)
            return juce::jmax(24, (int)core_->getZoom().getTrackHeightPx());
        return defaultLaneHeight_;
    }

    double getPixelsPerSecond() const noexcept
    {
        return core_ != nullptr ? core_->getZoom().getPixelsPerSecond() : 100.0;
    }

    void setPixelsPerSecond(double pixelsPerSecond)
    {
        if (core_ != nullptr)
            core_->getZoom().setPixelsPerSecond(pixelsPerSecond);
        resized();
        repaint();
    }

    void setViewportScrollOffsets(int scrollX, int scrollY)
    {
        horizontalScrollOffset_ = juce::jmax(0, scrollX);
        verticalScrollOffset_ = juce::jmax(0, scrollY);
        if (core_ != nullptr)
            core_->setViewportScrollOffset(scrollX, scrollY);
        updateImportOverlayBounds();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& path : files)
            if (isSupportedAudioFileForDrop(juce::File(path)))
                return true;

        return false;
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        beginQueuedFileDropImport(files, x, y);
    }

    void setSnapMode(SnapMode mode)
    {
        if (core_ == nullptr)
            return;

        using CoreSnapMode = ArrangementEditor::SnapMode;
        switch (mode)
        {
        case SnapMode::Note_1_32: core_->setSnapMode(CoreSnapMode::ThirtySecond); break;
        case SnapMode::Note_1_16: core_->setSnapMode(CoreSnapMode::Sixteenth); break;
        case SnapMode::Note_1_8:  core_->setSnapMode(CoreSnapMode::Eighth); break;
        case SnapMode::Note_1_4:  core_->setSnapMode(CoreSnapMode::Quarter); break;
        case SnapMode::Note_1_2:  core_->setSnapMode(CoreSnapMode::Half); break;
        case SnapMode::Bar_1:     core_->setSnapMode(CoreSnapMode::Bar); break;
        case SnapMode::Bar_2:     core_->setSnapMode(CoreSnapMode::Bar2); break;
        case SnapMode::Bar_4:     core_->setSnapMode(CoreSnapMode::Bar4); break;
        default:                  core_->setSnapMode(CoreSnapMode::Free); break;
        }
    }

    SnapMode getSnapMode() const noexcept
    {
        if (core_ == nullptr)
            return SnapMode::Free;

        using CoreSnapMode = ArrangementEditor::SnapMode;
        switch (core_->getSnapMode())
        {
        case CoreSnapMode::ThirtySecond: return SnapMode::Note_1_32;
        case CoreSnapMode::Sixteenth:    return SnapMode::Note_1_16;
        case CoreSnapMode::Eighth:       return SnapMode::Note_1_8;
        case CoreSnapMode::Quarter:      return SnapMode::Note_1_4;
        case CoreSnapMode::Half:         return SnapMode::Note_1_2;
        case CoreSnapMode::Bar:          return SnapMode::Bar_1;
        case CoreSnapMode::Bar2:         return SnapMode::Bar_2;
        case CoreSnapMode::Bar4:         return SnapMode::Bar_4;
        default:                         return SnapMode::Free;
        }
    }

    void setLaneHeight(int /*index*/, int newHeight)
    {
        newHeight = juce::jlimit(24, 180, newHeight);
        defaultLaneHeight_ = newHeight;
        if (core_ != nullptr)
            core_->getZoom().setTrackHeightPx((double)newHeight);
        // onLaneLayoutChanged fired by zoom callback wired in constructor
        resized();
        repaint();
    }

    void setMultiSelectedClips(const std::vector<juce::String>& ids)
    {
        multiSelectedClipIDs_.clear();
        multiSelectedClipIDs_.insert(ids.begin(), ids.end());
        repaint();
    }

    void setMultiSelectedClips(const std::unordered_set<juce::String>& ids)
    {
        multiSelectedClipIDs_ = ids;
        repaint();
    }

    void setCollapsedFolders(const std::unordered_set<TrackID>& collapsed)
    {
        collapsedFolders_ = collapsed;
        repaint();
    }

    void setKeepWaveformsVisibleWhileScrolling(bool enabled)
    {
        keepWaveformsVisibleWhileScrolling_ = enabled;
        repaint();
    }

    juce::Rectangle<int> getTrackLaneBounds(const TrackID& trackId) const
    {
        return core_ != nullptr ? core_->getTrackLaneBounds(trackId) : juce::Rectangle<int>();
    }

    juce::Rectangle<int> getClipBounds(const ClipID& clipId) const
    {
        if (core_ == nullptr)
            return {};

        auto it = engineClipIdsToArrangementIds_.find(clipId);
        return it != engineClipIdsToArrangementIds_.end() ? core_->getClipBounds(it->second) : juce::Rectangle<int>();
    }

    TrackID getTrackAtY(int y) const
    {
        return core_ != nullptr ? core_->getTrackAtY(y) : TrackID();
    }

    int getYForTrack(const TrackID& trackId) const
    {
        return core_ != nullptr ? core_->getYForTrack(trackId) : -1;
    }

    std::vector<TrackID> getVisibleTrackIds() const
    {
        return core_ != nullptr ? core_->getVisibleTrackIds() : std::vector<TrackID>();
    }

    TrackID resolveSelectedTrack() const
    {
        auto selected = state_.selectedTrackID.getValue().toString();
        return selected.isNotEmpty() ? selected : resolveArmedTrack();
    }

    TrackID resolveArmedTrack() const
    {
        for (int i = 0; i < trackManager_.getNumTracks(); ++i)
            if (auto* track = trackManager_.getTrack(i); track != nullptr && track->isArmed())
                return track->getID();

        return {};
    }

    AudioClip* placeRecordedAudioClip(const TrackID& trackId,
                                      const juce::File& file,
                                      SamplePosition startSample,
                                      SamplePosition lengthSamples)
    {
        if (!isValidAudioDestinationTrack(trackId) || !file.existsAsFile())
            return nullptr;

        auto* clip = clipManager_.createAudioClip(file.getFileNameWithoutExtension(), file);
        if (clip == nullptr)
            return nullptr;

        bool audioLoaded = audioFiles_ == nullptr;
        SamplePosition resolvedLengthSamples = juce::jmax((SamplePosition)0, lengthSamples);

        {
            juce::ScopedValueSetter<bool> mirrorGuard(isMirroringToArrangement_, true);
            clip->setTrackID(trackId);
            clip->setStartPosition(startSample);
            clip->setLength(juce::jmax((SamplePosition)1, resolvedLengthSamples > 0 ? resolvedLengthSamples : (SamplePosition)1));
            clip->setSourceOffset(0);
            if (audioFiles_ != nullptr)
            {
                auto res = audioFiles_->loadForClip(clip->getID(), file, !isImportingDroppedFiles_);
                if (res.success)
                {
                    audioLoaded = true;
                    if (resolvedLengthSamples <= 0)
                        resolvedLengthSamples = (SamplePosition) std::llround((double) res.numSamples * currentEngineSampleRate_ / juce::jmax(1.0, res.sampleRate));
                    clip->setLength(juce::jmax((SamplePosition)1, resolvedLengthSamples));
                    clip->setSourceStartSample(0);
                    clip->setSourceEndSample((int64_t)res.numSamples);
                }
            }
            else if (resolvedLengthSamples <= 0)
            {
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file)))
                {
                    resolvedLengthSamples = (SamplePosition) std::llround((double) reader->lengthInSamples * currentEngineSampleRate_ / juce::jmax(1.0, reader->sampleRate));
                    clip->setLength(juce::jmax((SamplePosition)1, resolvedLengthSamples));
                }
                else
                {
                    audioLoaded = false;
                }
            }
        }

        if (!audioLoaded)
        {
            clipManager_.deleteClip(clip->getID());
            return nullptr;
        }

        DBG("[TIMELINE-RECOVERY] record stop track=" << trackId
            << " clipType=Audio start=" << (juce::int64)startSample);
        if (core_ != nullptr)
            core_->setEngineSampleRate(currentEngineSampleRate_);
        mirrorEngineClipIntoArrangement(clip);
        return clip;
    }

    void repaintAutomationLane(const TrackID& trackId, const juce::String& target)
    {
        if (core_ != nullptr)
            core_->repaintAutomationLane(trackId, target);
    }

    void showAutomationLane(const TrackID& trackId, const juce::String& parameterId)
    {
        if (core_ != nullptr)
            core_->showAutomationLane(trackId, parameterId);
    }

    void hideAutomationLane(const TrackID& trackId, const juce::String& parameterId)
    {
        if (core_ != nullptr)
            core_->hideAutomationLane(trackId, parameterId);
    }

    /** Call this whenever track automation visibility or active parameter changes
        so the next timer tick syncs lanes immediately instead of polling every tick. */
    void requestAutomationSync() { automationSyncDirty_ = true; }

    void repaintClip(const ClipID& clipId)
    {
        if (core_ == nullptr)
            return;

        auto it = engineClipIdsToArrangementIds_.find(clipId);
        if (it != engineClipIdsToArrangementIds_.end())
            core_->repaintClip(it->second);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF1E1E1E));
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (core_ != nullptr && transport_.isRecording())
        {
            const int trackH = getLaneHeight(0);
            const int contentTop = 48 + 32;
            const double sr = juce::jmax(1.0, core_->m_engineSampleRate);
            const double pxPerSecond = core_->getZoom().getPixelsPerSecond();
            const auto currentSamples = transport_.getPosition();

            for (int i = 0; i < trackManager_.getNumTracks(); ++i)
            {
                auto* track = trackManager_.getTrack(i);
                if (track == nullptr || !track->isArmed())
                    continue;

                auto it = liveRecordingStartSamples_.find(track->getID());
                if (it == liveRecordingStartSamples_.end())
                    continue;

                const auto startSamples = it->second;
                const auto recordedSamples = juce::jmax<SamplePosition>(0, currentSamples - startSamples);
                const float x = (float)(((double)startSamples / sr) * pxPerSecond);
                const float w = (float)juce::jmax(2.0, ((double)recordedSamples / sr) * pxPerSecond);
                const int laneIndex = (trackManager_.hasMasterTrack() ? 1 : 0) + i;
                const float y = (float)(contentTop + laneIndex * trackH);
                juce::Rectangle<float> clipBounds(x, y, w, (float)trackH - 2.0f);

                g.setColour(track->getColor().darker(0.45f).withAlpha(0.92f));
                g.fillRoundedRectangle(clipBounds, 4.0f);

                const auto header = juce::Rectangle<float>(clipBounds.getX(), clipBounds.getY(), clipBounds.getWidth(), 18.0f);
                g.setColour(track->getColor());
                g.fillRoundedRectangle(header, 4.0f);
                g.fillRect(header.withTrimmedTop(4.0f));

                const auto waveformArea = clipBounds.withTrimmedTop(18.0f).reduced(2.0f, 2.0f);
                track->getLiveRecordWaveform().draw(g,
                                                    waveformArea,
                                                    juce::Colours::white.withAlpha(0.72f),
                                                    (float)(pxPerSecond / sr),
                                                    (double)recordedSamples);

                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText("Recording...", header.reduced(6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

                g.setColour(juce::Colour(0xFFFF4D4D).withAlpha(0.95f));
                g.fillEllipse(header.getRight() - 14.0f, header.getCentreY() - 4.0f, 8.0f, 8.0f);

                g.setColour(juce::Colour(0xFF111111));
                g.drawRoundedRectangle(clipBounds.reduced(0.5f), 4.0f, 1.0f);
            }
        }

    }

    void resized() override
    {
        if (core_ != nullptr)
            core_->setBounds(getLocalBounds());
        updateImportOverlayBounds();
    }

    std::function<void(int,int)> onLaneHeightChanged;
    std::function<void(int)> onVerticalScrollChanged;
    std::function<void()> onLaneLayoutChanged;
    std::function<void()> onSendToBubble;
    std::function<void(MidiClip&)> onOpenMidiClip;
    std::function<void(const std::vector<juce::String>&, bool)> onBoxSelectionCommitted;
    std::function<void()> onClipSelectionCleared;
    std::function<void(Clip&)> onOpenClipProperties;
    std::function<void(Clip&)> onOpenClipAutomation;
    std::function<void(Clip&)> onOpenClipVocalTune;
    std::function<void(Clip&)> onToggleClipVocalTuneBypass;
    std::function<bool(const Clip&)> onIsClipVocalTuneBypassed;
    std::function<void(const ClipID&)> onClipDeleted;
    std::function<void()> onOpenSelectedClipProperties;
    std::function<void()> onOpenSelectedMidiClip;
    std::function<void()> onQuickAccessSidePanel;
    std::function<void()> onQuickAccessFxBrowser;

private:
    bool isSupportedAudioFileForDrop(const juce::File& file) const
    {
        if (!file.existsAsFile())
            return false;

        const auto extension = file.getFileExtension().fromFirstOccurrenceOf(".", false, false).toLowerCase();
        if (extension.isEmpty())
            return false;

        if (audioFiles_ != nullptr)
            return audioFiles_->getFormatManager().findFormatForFileExtension(extension) != nullptr;

        static const juce::StringArray supportedExtensions { "wav", "wave", "aif", "aiff", "flac", "ogg", "mp3" };
        return supportedExtensions.contains(extension, true);
    }

    SamplePosition getDroppedAudioFileLengthSamples(const juce::File& file) const
    {
        if (!isSupportedAudioFileForDrop(file))
            return 0;

        if (audioFiles_ != nullptr)
        {
            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(audioFiles_->getFormatManager().createReaderFor(file)))
                return juce::jmax((SamplePosition) 1, (SamplePosition) reader->lengthInSamples);
            return 0;
        }

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(file)))
            return juce::jmax((SamplePosition) 1, (SamplePosition) reader->lengthInSamples);

        return 0;
    }

    SamplePosition dropXToTimelineSample(int x) const
    {
        const double pixelsPerSecond = juce::jmax(1.0, getPixelsPerSecond());
        const double timeSeconds = juce::jmax(0.0, (double) (horizontalScrollOffset_ + juce::jmax(0, x)) / pixelsPerSecond);
        return (SamplePosition) std::llround(timeSeconds * juce::jmax(1.0, currentEngineSampleRate_));
    }

    TrackID resolveTargetTrackForFileDrop(int y) const
    {
        const auto droppedTrack = getTrackAtY(y);
        if (isValidAudioDestinationTrack(droppedTrack))
            return droppedTrack;

        const auto selectedTrack = resolveSelectedTrack();
        if (isValidAudioDestinationTrack(selectedTrack))
            return selectedTrack;

        const auto armedTrack = resolveArmedTrack();
        if (isValidAudioDestinationTrack(armedTrack))
            return armedTrack;

        if (auto* firstTrack = trackManager_.getTrack(0))
            return firstTrack->getID();

        return {};
    }

    bool isValidAudioDestinationTrack(const TrackID& trackId) const
    {
        if (trackId.isEmpty())
            return false;

        if (trackManager_.hasMasterTrack())
            if (auto* masterTrack = trackManager_.getMasterTrack(); masterTrack != nullptr && trackId == masterTrack->getID())
                return false;

        return trackManager_.getTrack(trackId) != nullptr;
    }

    struct PendingDroppedFileImport
    {
        juce::File file;
        TrackID trackId;
        SamplePosition startSample = 0;
        SamplePosition lengthSamples = 0;
    };

    void beginQueuedFileDropImport(const juce::StringArray& files, int x, int y)
    {
        pendingDroppedFileImports_.clear();
        pendingDroppedFileImportCursor_ = 0;
        importTotalDroppedFileCount_ = 0;
        importedDroppedFileCount_ = 0;
        failedDroppedFileCount_ = 0;
        importCompletionTicksRemaining_ = 0;

        const auto startTrackId = resolveTargetTrackForFileDrop(y);
        const int startTrackIndex = getOrCreateDropStartTrackIndex(startTrackId);
        if (startTrackIndex < 0)
            return;

        const auto insertSample = dropXToTimelineSample(x);
        int trackIndex = startTrackIndex;

        for (const auto& path : files)
        {
            const juce::File file(path);
            if (!isSupportedAudioFileForDrop(file))
                continue;

            if (auto* track = ensureAudioTrackExistsAtIndex(trackIndex, file.getFileNameWithoutExtension()))
            {
                pendingDroppedFileImports_.push_back({ file, track->getID(), insertSample, 0 });
                ++trackIndex;
            }
        }

        importTotalDroppedFileCount_ = (int) pendingDroppedFileImports_.size();

        if (pendingDroppedFileImports_.empty())
        {
            isImportingDroppedFiles_ = false;
            importOverlayTitle_.clear();
            importOverlayDetail_.clear();
            importOverlayProgress_ = 0.0;
            importOverlay_.setState({}, {}, 0.0);
            importOverlay_.setVisible(false);
            repaint();
            return;
        }

        isImportingDroppedFiles_ = true;
        importOverlayTitle_ = "Loading dropped files...";
        importOverlayProgress_ = 0.0;
        updateDroppedFileImportOverlay();
        updateImportOverlayBounds();
        importOverlay_.setVisible(true);
        importOverlay_.toFront(false);
        state_.selectedTrackID.setValue(pendingDroppedFileImports_.front().trackId);
        if (onLaneLayoutChanged)
            onLaneLayoutChanged();
        repaint();
    }

    int getOrCreateDropStartTrackIndex(const TrackID& trackId)
    {
        if (isValidAudioDestinationTrack(trackId))
            return trackManager_.getTrackIndex(trackId);

        if (auto* track = ensureAudioTrackExistsAtIndex(0, "Audio 1"))
            return trackManager_.getTrackIndex(track->getID());

        return -1;
    }

    Track* ensureAudioTrackExistsAtIndex(int trackIndex, const juce::String& preferredName)
    {
        trackIndex = juce::jmax(0, trackIndex);
        while (trackManager_.getNumTracks() <= trackIndex)
        {
            auto newTrackName = preferredName.isNotEmpty()
                ? preferredName
                : juce::String("Audio ") + juce::String(trackManager_.getNumTracks() + 1);
            if (trackManager_.createTrack(newTrackName) == nullptr)
                return nullptr;
        }

        return trackManager_.getTrack(trackIndex);
    }

    void processNextPendingDroppedFileImport()
    {
        if (pendingDroppedFileImportCursor_ >= pendingDroppedFileImports_.size())
        {
            finishDroppedFileImport();
            return;
        }

        constexpr int maxFilesPerTick = 4;
        constexpr double maxMsPerTick = 45.0;
        const double startedMs = juce::Time::getMillisecondCounterHiRes();
        int processedThisTick = 0;

        while (pendingDroppedFileImportCursor_ < pendingDroppedFileImports_.size()
               && processedThisTick < maxFilesPerTick
               && (juce::Time::getMillisecondCounterHiRes() - startedMs) < maxMsPerTick)
        {
            auto nextImport = pendingDroppedFileImports_[pendingDroppedFileImportCursor_++];

            if (placeRecordedAudioClip(nextImport.trackId, nextImport.file, nextImport.startSample, nextImport.lengthSamples) != nullptr)
                ++importedDroppedFileCount_;
            else
                ++failedDroppedFileCount_;

            updateDroppedFileImportOverlay(nextImport.file.getFileName());
            ++processedThisTick;
        }

        importOverlay_.toFront(false);
        repaint();

        if (pendingDroppedFileImportCursor_ >= pendingDroppedFileImports_.size())
            finishDroppedFileImport();
    }

    void updateDroppedFileImportOverlay(const juce::String& currentFileName = {})
    {
        const int totalCount = importTotalDroppedFileCount_;
        const int processedCount = importedDroppedFileCount_ + failedDroppedFileCount_;

        importOverlayProgress_ = totalCount > 0
            ? juce::jlimit(0.0, 1.0, (double) processedCount / (double) totalCount)
            : 0.0;

        if (totalCount <= 0)
        {
            importOverlayDetail_.clear();
            return;
        }

        const auto nextName = currentFileName.isNotEmpty()
            ? currentFileName
            : (pendingDroppedFileImportCursor_ < pendingDroppedFileImports_.size()
                ? pendingDroppedFileImports_[pendingDroppedFileImportCursor_].file.getFileName()
                : juce::String());

        importOverlayDetail_ = "Loaded " + juce::String(processedCount) + " / " + juce::String(totalCount)
            + "  •  " + nextName;
        importOverlay_.setState(importOverlayTitle_, importOverlayDetail_, importOverlayProgress_);
    }

    void finishDroppedFileImport()
    {
        const int totalImported = importedDroppedFileCount_;
        const int totalFailed = failedDroppedFileCount_;

        isImportingDroppedFiles_ = false;
        pendingDroppedFileImports_.clear();
        pendingDroppedFileImportCursor_ = 0;
        importTotalDroppedFileCount_ = 0;
        importOverlayProgress_ = 1.0;
        importOverlayTitle_ = totalFailed > 0
            ? "Some dropped files could not be loaded"
            : "Dropped files loaded";
        importOverlayDetail_ = juce::String(totalImported) + " imported"
            + (totalFailed > 0 ? "  •  " + juce::String(totalFailed) + " failed" : juce::String());
        importOverlay_.setState(importOverlayTitle_, importOverlayDetail_, importOverlayProgress_);
        importOverlay_.setVisible(true);
        importOverlay_.toFront(false);
        importCompletionTicksRemaining_ = 45;

        if (onLaneLayoutChanged)
            onLaneLayoutChanged();

        if (totalImported > 0)
            repaint();

        importedDroppedFileCount_ = 0;
        failedDroppedFileCount_ = 0;
    }

    void updateImportOverlayBounds()
    {
        const int viewportW = [this]()
        {
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
                return viewport->getViewWidth();
            return getWidth();
        }();

        const int boxW = juce::jlimit(260, 460, viewportW - 24);
        importOverlay_.setBounds(horizontalScrollOffset_ + juce::jmax(12, (viewportW - boxW) / 2),
                                 verticalScrollOffset_ + 16,
                                 boxW,
                                 84);
    }

    void clipAdded(Clip* clip) override
    {
        if (clip != nullptr)
            clip->addListener(this);

        if (core_ != nullptr && core_->isSyncingArrangementToEngine())
            return;

        if (clip != nullptr && clip->getTrackID().isNotEmpty())
            mirrorEngineClipIntoArrangement(clip);
    }

    void clipRemoved(const ClipID& clipID) override
    {
        if (onClipDeleted)
            onClipDeleted(clipID);

        if (core_ != nullptr && core_->isSyncingArrangementToEngine())
        {
            engineClipIdsToArrangementIds_.erase(clipID);
            repaint();
            return;
        }

        if (auto* clip = clipManager_.getClip(clipID))
            clip->removeListener(this);

        auto it = engineClipIdsToArrangementIds_.find(clipID);
        if (it != engineClipIdsToArrangementIds_.end())
        {
            const auto arrangementId = it->second;
            DBG("[TIMELINE-RECOVERY] clipRemoved engine=" << clipID
                << " arrangement=" << arrangementId.toString());

            if (core_ != nullptr && !arrangementId.isNull())
            {
                core_->m_uuidToEngineId.erase(arrangementId);
                core_->m_selection.deselectClip(arrangementId);
                core_->m_clipState.removeClip(arrangementId);
            }

            engineClipIdsToArrangementIds_.erase(it);
        }

        repaint();
    }

    void clipPropertyChanged(Clip* clip) override
    {
        if (isMirroringToArrangement_)
            return;
        if (core_ != nullptr && core_->isSyncingArrangementToEngine())
            return;

        mirrorEngineClipIntoArrangement(clip);
    }

    void trackAdded(Track* track) override
    {
        if (track != nullptr)
        {
            track->addListener(this);
            syncTrackColourToClips(*track);
        }

        if (onLaneLayoutChanged)
            onLaneLayoutChanged();

        repaint();
    }

    void trackRemoved(const TrackID& trackID) override
    {
        visibleAutomationTargets_.erase(trackID);

        if (onLaneLayoutChanged)
            onLaneLayoutChanged();

        repaint();
    }

    void trackOrderChanged() override
    {
        if (onLaneLayoutChanged)
            onLaneLayoutChanged();

        mirrorAllEngineClipsIntoArrangement();
        repaint();
    }

    void trackPropertyChanged(Track* track) override
    {
        if (track == nullptr)
            return;

        syncTrackColourToClips(*track);
        requestAutomationSync();
        repaint();
    }

    void timerCallback() override
    {
        if (isImportingDroppedFiles_)
            processNextPendingDroppedFileImport();
        else if (importCompletionTicksRemaining_ > 0)
        {
            --importCompletionTicksRemaining_;
            if (importCompletionTicksRemaining_ == 0)
            {
                importOverlay_.setState({}, {}, 0.0);
                importOverlay_.setVisible(false);
            }
        }

        const bool isRecordingNow = transport_.isRecording();
        if (isRecordingNow && !wasRecording_)
        {
            liveRecordingStartSamples_.clear();
            const auto startSample = transport_.getPosition();
            for (int i = 0; i < trackManager_.getNumTracks(); ++i)
                if (auto* track = trackManager_.getTrack(i); track != nullptr && track->isArmed())
                    liveRecordingStartSamples_[track->getID()] = startSample;
        }
        else if (!isRecordingNow && wasRecording_)
        {
            liveRecordingStartSamples_.clear();
            mirrorAllEngineClipsIntoArrangement();
        }
        wasRecording_ = isRecordingNow;

        if (core_ != nullptr)
        {
            core_->setTempoBpm(transport_.getTempo());
            const double sr = juce::jmax(1.0, core_->m_engineSampleRate);
            core_->setPlayheadPosition((double)transport_.getPosition() / sr);
        }

        if (automationSyncDirty_)
        {
            syncAutomationLanesFromTracks();
            automationSyncDirty_ = false;
        }

        // Only repaint the arrangement when the playhead has actually moved.
        const double newPos = transport_.getPosition();
        if (newPos != lastTimerPlayheadPos_)
        {
            lastTimerPlayheadPos_ = newPos;
            repaint();
        }
    }

    int getTrackIndexForClip(const Clip& clip) const
    {
        const int index = trackManager_.getTrackIndex(clip.getTrackID());
        const int masterOffset = trackManager_.hasMasterTrack() ? 1 : 0;
        if (index >= 0)
            return index + masterOffset;

        const auto it = engineClipIdsToArrangementIds_.find(clip.getID());
        if (it != engineClipIdsToArrangementIds_.end() && core_ != nullptr)
            if (const auto* existing = core_->m_clipState.findClip(it->second))
                return existing->trackIndex;

        return masterOffset;
    }

    void mirrorEngineClipIntoArrangement(Clip* clip)
    {
        if (clip == nullptr || core_ == nullptr)
            return;

        juce::ScopedValueSetter<bool> mirrorGuard(isMirroringToArrangement_, true);
        ArrangementEditor::ArrangementClipModel model;
        auto& uuid = engineClipIdsToArrangementIds_[clip->getID()];
        if (uuid.isNull())
            uuid = juce::Uuid();

        model.id = uuid;
        model.trackIndex = getTrackIndexForClip(*clip);
        model.startTime = (double)clip->getStartPosition() / juce::jmax(1.0, core_->m_engineSampleRate);
        model.length = (double)clip->getLength() / juce::jmax(1.0, core_->m_engineSampleRate);
        model.sourceOffset = (double)clip->getSourceOffset() / juce::jmax(1.0, core_->m_engineSampleRate);
        model.clipName = clip->getName().toStdString();
        model.colour = clip->getColor();
        model.muted = clip->isMuted();

        if (auto* audioClip = dynamic_cast<AudioClip*>(clip))
        {
            auto sourceRate = audioFiles_ != nullptr ? audioFiles_->getSourceSampleRate(audioClip->getID()) : core_->m_engineSampleRate;
            if (std::abs(sourceRate - 44100.0) < 0.001 && audioFiles_ != nullptr && !audioFiles_->hasAudio(audioClip->getID()))
            {
                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(audioClip->getSourceFile())))
                    if (reader->sampleRate > 0.0)
                        sourceRate = reader->sampleRate;
            }
            model.sourcePath = audioClip->getSourceFile().getFullPathName().toStdString();
            model.gain = audioClip->getGain();
            model.sourceOffset = (double)audioClip->getSourceOffset() / juce::jmax(1.0, sourceRate);
            model.sourceStartSample = audioClip->getSourceStartSample();
            model.sourceEndSample = audioClip->getSourceEndSample();

            // Resolve the true decoded length of the underlying file. This is the
            // hard upper bound used by edge-trim so dragging the right edge stops
            // at the real audio end instead of silently time-stretching, and so
            // left-edge head-trim has a valid sourceEndSample to anchor against.
            int64_t totalSourceSamples = 0;
            if (audioFiles_ != nullptr)
                totalSourceSamples = (int64_t)audioFiles_->getSourceNumSamples(audioClip->getID());
            if (totalSourceSamples <= 0)
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                if (auto r = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(audioClip->getSourceFile())))
                    totalSourceSamples = (int64_t)r->lengthInSamples;
            }
            model.sourceTotalSamples = totalSourceSamples;
            model.sourceSampleRate = sourceRate;

            // When the engine never assigned an explicit source window (the common
            // case for freshly imported, un-split clips), default it to the full
            // decoded range so trim math has valid start/end bounds.
            if (model.sourceEndSample <= model.sourceStartSample && totalSourceSamples > 0)
            {
                model.sourceStartSample = juce::jmax((int64_t)0,
                    (int64_t)std::llround(model.sourceOffset * sourceRate));
                model.sourceEndSample = totalSourceSamples;
            }

            model.timePitch.pitchSemitones = audioClip->getPitchTargetSemitones();
            model.timePitch.stretchRatio = audioClip->getTimeStretch();
            model.timePitch.fineTuneCents = audioClip->getFineTuneCents();
            model.timePitch.mode = static_cast<ArrangementEditor::TimePitchMode>(audioClip->getTimePitchMode());
            model.reversed = audioClip->isReversed();
            model.fadeInLength = (float)((double)audioClip->getFadeInLength() / juce::jmax(1.0, core_->m_engineSampleRate));
            model.fadeOutLength = (float)((double)audioClip->getFadeOutLength() / juce::jmax(1.0, core_->m_engineSampleRate));
        }

        core_->addOrUpdateEngineClipModel(model, clip->getID());
    }

    void syncAutomationLanesFromTracks()
    {
        if (core_ == nullptr || automation_ == nullptr)
            return;

        auto syncTrack = [this](Track* track)
        {
            if (track == nullptr)
                return;

            const auto trackId = track->getID();
            const auto activeParameterId = track->getActiveAutomationParameterId().isNotEmpty()
                ? track->getActiveAutomationParameterId()
                : juce::String(AutomationLaneCore::trackVolumeParameterId);

            auto& last = visibleAutomationTargets_[trackId];
            if (track->isAutomationVisible())
            {
                if (last != activeParameterId)
                {
                    if (last.isNotEmpty())
                        core_->hideAutomationLane(trackId, last);

                    core_->showAutomationLane(trackId, activeParameterId);
                    last = activeParameterId;
                }
            }
            else if (last.isNotEmpty())
            {
                core_->hideAutomationLane(trackId, last);
                last.clear();
            }
        };

        if (trackManager_.hasMasterTrack())
            syncTrack(trackManager_.getMasterTrack());

        for (int i = 0; i < trackManager_.getNumTracks(); ++i)
            syncTrack(trackManager_.getTrack(i));
    }

    void mirrorAllEngineClipsIntoArrangement()
    {
        juce::ScopedLock sl(clipManager_.getLock());
        for (auto* clip : clipManager_.getAllClips())
            mirrorEngineClipIntoArrangement(clip);
    }

    void syncTrackColourToClips(Track& track)
    {
        const auto trackId = track.getID();
        const auto colour = track.getColor();

        juce::ScopedLock sl(clipManager_.getLock());
        for (auto* clip : clipManager_.getAllClips())
        {
            if (clip == nullptr || clip->getTrackID() != trackId || clip->getColor() == colour)
                continue;

            clip->setColor(colour);
        }
    }

    void attachTrackListeners()
    {
        if (trackManager_.hasMasterTrack())
            if (auto* masterTrack = trackManager_.getMasterTrack())
                masterTrack->addListener(this);

        for (auto* track : trackManager_.getAllTracks())
            if (track != nullptr)
                track->addListener(this);
    }

    void detachTrackListeners()
    {
        if (trackManager_.hasMasterTrack())
            if (auto* masterTrack = trackManager_.getMasterTrack())
                masterTrack->removeListener(this);

        for (auto* track : trackManager_.getAllTracks())
            if (track != nullptr)
                track->removeListener(this);
    }

    void attachClipListeners()
    {
        juce::ScopedLock sl(clipManager_.getLock());
        for (auto* clip : clipManager_.getAllClips())
            if (clip != nullptr)
                clip->addListener(this);
    }

    void detachClipListeners()
    {
        juce::ScopedLock sl(clipManager_.getLock());
        for (auto* clip : clipManager_.getAllClips())
            if (clip != nullptr)
                clip->removeListener(this);
    }

    TrackManager& trackManager_;
    ClipManager& clipManager_;
    ApplicationState& state_;
    MarkerManager& markers_;
    TransportController& transport_;
    AudioFileManager* audioFiles_ = nullptr;
    AutomationManagerCore* automation_ = nullptr;

    std::unique_ptr<ArrangementEditor::ArrangementViewCore> core_;
    ImportOverlayComponent importOverlay_;
    std::function<PluginChainCore*(const TrackID&)> pluginChainProvider_;
    std::function<RoutingGraph*()> routingGraphProvider_;

    std::unordered_map<juce::String, juce::Uuid> engineClipIdsToArrangementIds_;
    std::unordered_set<juce::String> multiSelectedClipIDs_;
    std::unordered_set<TrackID> collapsedFolders_;
    std::unordered_map<TrackID, SamplePosition> liveRecordingStartSamples_;
    std::unordered_map<TrackID, juce::String> visibleAutomationTargets_;
    std::vector<PendingDroppedFileImport> pendingDroppedFileImports_;
    size_t pendingDroppedFileImportCursor_ = 0;
    juce::String importOverlayTitle_;
    juce::String importOverlayDetail_;
    double importOverlayProgress_ = 0.0;
    int importTotalDroppedFileCount_ = 0;
    int importedDroppedFileCount_ = 0;
    int failedDroppedFileCount_ = 0;
    int importCompletionTicksRemaining_ = 0;
    int horizontalScrollOffset_ = 0;
    int verticalScrollOffset_ = 0;
    int defaultLaneHeight_ = 80;
    bool keepWaveformsVisibleWhileScrolling_ = false;
    bool isImportingDroppedFiles_ = false;
    bool wasRecording_ = false;
    bool isMirroringToArrangement_ = false;
    double currentEngineSampleRate_ = 44100.0;
    double lastTimerPlayheadPos_ = -1.0;
    bool automationSyncDirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangementView)
};

} // namespace DAW

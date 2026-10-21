#pragma once
#include <JuceHeader.h>
#include <map>
#include <cmath>
#include "../TrackCore/TrackPlaylistCompCore.h"
#include "../CommandCore/CommandManager.h"

namespace DAW
{
// APEX Playlist View: one selected track, expandable take lanes underneath the
// active main/target strip. Selection drag + arrow promotes by sample location.
// Docked beneath the native arranger, with the same project-sample timeline.
// A dedicated take workspace preserves existing track-drag/layout authority
// until true per-track expandable lanes are implemented in ArrangementCore.
class TrackPlaylistPanel final : public juce::Component
{
public:
    TrackPlaylistPanel(TrackManager& tracks, ClipManager& clips,
                       AudioFileManager& audioFiles)
        : tracks_(tracks), clips_(clips), audioFiles_(audioFiles)
    {
        formats_.registerBasicFormats();
        setOpaque(true);
        setWantsKeyboardFocus(true);
        addAndMakeVisible(newButton_);
        addAndMakeVisible(duplicateButton_);
        addAndMakeVisible(closeButton_);
        addChildComponent(renameEditor_);
        renameEditor_.setSelectAllWhenFocused(true);
        renameEditor_.onReturnKey = [this] { commitRename(); };
        renameEditor_.onEscapeKey = [this] { renamePlaylistId_.clear(); renameEditor_.setVisible(false); };
        renameEditor_.onFocusLost = [this] { commitRename(); };
        newButton_.setButtonText("+ New Playlist");
        duplicateButton_.setButtonText("Duplicate Main");
        closeButton_.setButtonText("Close");
        newButton_.onClick = [this] { createNew(false); };
        duplicateButton_.onClick = [this] { createNew(true); };
        closeButton_.onClick = [this] { if (onClose) onClose(); };
    }

    void setInlineMode(bool inlineMode)
    {
        inlineMode_ = inlineMode;
        newButton_.setVisible(!inlineMode_);
        duplicateButton_.setVisible(!inlineMode_);
        closeButton_.setVisible(!inlineMode_);
        scrollY_ = 0;
        repaint();
    }

    void setInlineTakeHeight(int pixels)
    {
        const int height = juce::jlimit(40, 180, pixels);
        if (inlineTakeHeight_ == height) return;
        inlineTakeHeight_ = height;
        repaint();
    }

    void createForTrack(const TrackID& id, bool duplicate, double engineRate)
    {
        showForTrack(id, engineRate);
        createNew(duplicate);
    }

    std::function<bool()> canModify;
    std::function<void()> onChanged;
    std::function<void()> onClose;
    std::function<void(bool)> onTimelineZoom;
    std::function<void(float)> onTimelinePan;
    std::function<void(float)> onTimelineVerticalScroll;
    std::function<void()> onSelectionChanged;

    // Clicking the always-visible arrow in the owning TrackList header must
    // promote only a real phrase selection from THAT take, never stale ranges
    // belonging to a different playlist.
    const TrackID& getCurrentTrackID() const noexcept { return trackId_; }

    bool canPromoteForLane(const TrackID& id, const juce::String& laneId) const
    {
        if (trackId_ != id || laneId.isEmpty() || selectedPlaylist_ != laneId
            || selectionStart_ < 0 || selectionEnd_ <= selectionStart_)
            return false;
        auto* track = tracks_.getTrack(trackId_);
        return track != nullptr && laneId != track->getTargetPlaylistId();
    }

    void promoteFromTrackHeader(const TrackID& id, const juce::String& laneId)
    {
        if (canPromoteForLane(id, laneId))
            promote();
    }

    void showForTrack(const TrackID& id, double engineRate)
    {
        trackId_ = id;
        sampleRate_ = engineRate > 0 ? engineRate : 44100.0;
        selectedPlaylist_.clear();
        selectionStart_ = selectionEnd_ = -1;
        thumbs_.clear();
        thumbnailSampleRates_.clear();
        recalculateView();
        preflightThumbnails();
        repaint();
    }

    // View origin is the arranger's first visible project sample.
    // x=161 is the plot origin; below it are playlist name/solo controls.
    // Pixel scaling MUST follow the arranger (no independent timebase).
    void followArrangementTimeline(double firstVisibleSample, double pixelsPerSecond)
    {
        if (pixelsPerSecond <= 0.0 || sampleRate_ <= 0.0)
            return;
        followingArrangement_ = true;
        // The inline panel is a CHILD of the horizontally scrolled arranger
        // content (not a pinned viewport widget). Its x=161 is content x=0.
        // Subtracting the viewport's first-visible sample here would scroll
        // alternate takes TWICE and make comp boundaries drift from Main.
        const double start = inlineMode_ ? 0.0
                                         : juce::jmax(0.0, firstVisibleSample);
        const double end = start + (double)juce::jmax(40, getWidth() - 222)
                           * sampleRate_ / pixelsPerSecond;
        if (std::abs(viewStart_ - start) < 0.5 && std::abs(viewEnd_ - end) < 0.5)
            return;
        viewStart_ = start;
        viewEnd_ = end;
        viewMaxEnd_ = juce::jmax(viewMaxEnd_, end);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff110f1d));
        if (!inlineMode_)
        {
            g.setColour(juce::Colour(0xffb98bff));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 10.f, 1.5f);
        }
        auto* track = tracks_.getTrack(trackId_);
        if (!inlineMode_)
        {
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(16.f, juce::Font::bold));
            g.drawText(track ? track->getName() + "  /  PLAYLISTS" : juce::String("PLAYLISTS"),
                       18, 6, juce::jmax(0, getWidth() - 335), 37, juce::Justification::centredLeft);
            g.setFont(juce::Font(11.f));
            g.setColour(juce::Colour(0xffa9a1be));
            g.drawText("Drag a phrase in a take lane; press the up arrow to copy it to the target.",
                       18, 43, juce::jmax(0, getWidth() - 40), 23, juce::Justification::centredLeft);
        }
        if (!track) return;
        const auto& playlists = track->getPlaylists();
        if (!inlineMode_)
        {
            g.setColour(juce::Colour(0xff5f5974));
            g.fillRect(10, 74, getWidth() - 20, 1);
        }
        const int rowHeight = inlineMode_ ? inlineTakeHeight_ : 66;
        int alternateRow = 0;
        for (int i = 0; i < (int)playlists.size(); ++i)
        {
            const auto& p = playlists[(size_t)i];
            if (inlineMode_ && p.id == track->getMainPlaylistId())
                continue; // Main is the owning track's normal clip row
            if (inlineMode_ && alternateRow >= 128) break;
            const int y = inlineMode_ ? alternateRow++ * rowHeight
                                      : 81 + i * rowHeight - scrollY_;
            if (y + rowHeight < (inlineMode_ ? 0 : 76)
                || y >= getHeight() - (inlineMode_ ? 0 : 22)) continue;
            const bool main = p.id == track->getMainPlaylistId();
            const bool target = p.id == track->getTargetPlaylistId();
            const bool solo = p.id == track->getAuditionPlaylistId();
            const bool selected = p.id == selectedPlaylist_;
            auto row = juce::Rectangle<int>(10, y, getWidth() - 20,
                                            juce::jmax(1, rowHeight - 4));
            g.setColour(main ? juce::Colour(0xff31204c) :
                        selected ? juce::Colour(0xff23233a) : juce::Colour(0xff191829));
            g.fillRoundedRectangle(row.toFloat(), 6.f);
            g.setColour(target ? juce::Colour(0xffb985ff) : juce::Colour(0xff413b5c));
            g.drawRoundedRectangle(row.toFloat(), 6.f, target ? 1.5f : 0.8f);
            g.setFont(juce::Font(12.f, juce::Font::bold));
            g.setColour(target ? juce::Colours::white : juce::Colour(0xffbbb5d5));
            g.drawText(p.name, 18, y + juce::jmax(3, rowHeight / 10), 105,
                       juce::jmin(23, rowHeight / 2),
                       juce::Justification::centredLeft, true);
            g.setFont(juce::Font(10.f));
            g.setColour(juce::Colour(0xffa393c9));
            if (rowHeight >= 54)
                g.drawText(main ? "MAIN" : target ? "TARGET" : "TAKE",
                           18, y + rowHeight - 27, 62, 16,
                           juce::Justification::centredLeft);
            auto soloRect = juce::Rectangle<int>(126,
                y + juce::jmax(0, (rowHeight - 25) / 2), 27, 25);
            g.setColour(solo ? juce::Colour(0xff33db9a) : juce::Colour(0xff3c3456));
            g.fillRoundedRectangle(soloRect.toFloat(), 4.f);
            g.setColour(solo ? juce::Colours::black : juce::Colours::white);
            g.drawText("S", soloRect, juce::Justification::centred);

            const auto timeline = getTimelineBounds(y);
            g.setColour(juce::Colour(0xff0e111c));
            g.fillRoundedRectangle(timeline.toFloat(), 3.f);
            g.saveState();
            g.reduceClipRegion(timeline);
            for (auto* clip : clips_.getClipsOnTrack(trackId_))
            {
                if (!clip || clip->getPlaylistId() != p.id) continue;
                auto* audio = dynamic_cast<AudioClip*>(clip);
                if (!audio) continue;
                const double start = (double)clip->getStartPosition();
                const double end = (double)clip->getEndPosition();
                auto x1 = xForSample(start), x2 = xForSample(end);
                auto body = juce::Rectangle<int>((int)x1, y + 8,
                    juce::jmax(1, (int)(x2 - x1)),
                    juce::jmax(10, rowHeight - 16));
                g.setColour(main ? juce::Colour(0xff6642b6) : juce::Colour(0xff315977));
                g.fillRoundedRectangle(body.toFloat(), 3.f);
                if (auto* thumb = getThumbnail(audio->getSourceFile()))
                {
                    // The file's duration / clip's sourceEndSample is NOT a
                    // sample-rate conversion: cropped takes have a sourceEnd
                    // before EOF, which shifts and stretches their waveform.
                    const auto rateIt = thumbnailSampleRates_.find(
                        audio->getSourceFile().getFullPathName());
                    const double fileRate = rateIt != thumbnailSampleRates_.end()
                        ? rateIt->second : sampleRate_;
                    const double beginSample = juce::jmax<int64_t>(
                        0, audio->getSourceStartSample());
                    const double endSample = audio->getSourceEndSample() > beginSample
                        ? (double)audio->getSourceEndSample()
                        : beginSample + (end - start) * fileRate / sampleRate_;
                    const double sec = beginSample / juce::jmax(1.0, fileRate);
                    const double secEnd = endSample / juce::jmax(1.0, fileRate);
                    g.setColour(juce::Colour(0xff99e8ff).withAlpha(0.85f));
                    thumb->drawChannels(g, body.reduced(1), sec,
                                        juce::jmax(sec + 0.001, secEnd), 0.82f);
                }
            }
            if (selected && selectionStart_ >= 0 && selectionEnd_ > selectionStart_)
            {
                auto xa = xForSample((double)selectionStart_);
                auto xb = xForSample((double)selectionEnd_);
                g.setColour(juce::Colour(0x7055eaff));
                g.fillRect(juce::Rectangle<float>(xa, (float)y + 3.f,
                    juce::jmax(1.f, xb - xa), (float)juce::jmax(1, rowHeight - 6)));
                g.setColour(juce::Colour(0xff90eeff));
                g.drawLine(xa, (float)y + 2.f,
                    xa, (float)y + rowHeight - 4.f, 1.5f);
                g.drawLine(xb, (float)y + 2.f,
                    xb, (float)y + rowHeight - 4.f, 1.5f);
            }
            g.restoreState();

            auto arrow = getArrowBounds(y);
            bool canPromote = selected && selectionStart_ >= 0
                && selectionEnd_ > selectionStart_ && p.id != track->getTargetPlaylistId();
            g.setColour(canPromote ? juce::Colour(0xffb88cff) : juce::Colour(0xff413b59));
            g.fillRoundedRectangle(arrow.toFloat(), 4.f);
            g.setColour(canPromote ? juce::Colours::black : juce::Colour(0xffaaa0c3));
            g.setFont(juce::Font(17.f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("↑"), arrow, juce::Justification::centred);
        }
        if (!inlineMode_)
        {
            g.setColour(juce::Colour(0xff87829c));
            g.setFont(juce::Font(10.f));
            g.drawText("Source WAVs and alternate takes are preserved when the project closes.",
                       18, getHeight() - 22, getWidth() - 30, 19, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        closeButton_.setBounds(getWidth() - 82, 12, 70, 28);
        duplicateButton_.setBounds(getWidth() - 235, 12, 145, 28);
        newButton_.setBounds(getWidth() - 367, 12, 125, 28);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            if (selectionDragging_)
                selectionDragging_ = false;
            if (auto* track = tracks_.getTrack(trackId_))
            {
                if (track->getAuditionPlaylistId().isNotEmpty())
                {
                    track->soloPlaylistLane({});
                    notifyMutation();
                    return true;
                }
            }
            // Escape leaves the clip and playlist metadata untouched.
            if (onClose) onClose();
            return true;
        }
        if (mods.isShiftDown() && mods.isAltDown()
            && key.getKeyCode() == juce::KeyPress::upKey)
        {
            promote(); // Windows Pro Tools comp-promotion shortcut
            return true;
        }
        if (mods.isShiftDown() && !mods.isAltDown()
            && key.getKeyCode() == 'S' && selectedPlaylist_.isNotEmpty())
        {
            if (auto* track = tracks_.getTrack(trackId_))
                if (track->soloPlaylistLane(selectedPlaylist_))
                    notifyMutation();
            return true;
        }
        if (mods.isShiftDown() && key.getKeyCode() == juce::KeyPress::rightKey)
        {
            if (auto* track = tracks_.getTrack(trackId_))
                if (track->showPlaylistAsMain(track->getTargetPlaylistId()))
                    notifyMutation();
            return true;
        }
        return false;
    }

    const Track::Playlist* playlistAtY(int y, int& rowTop) const
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return nullptr;
        const int firstY = inlineMode_ ? 0 : 81;
        const int rowHeight = inlineMode_ ? inlineTakeHeight_ : 66;
        const int contentY = y + (inlineMode_ ? 0 : scrollY_) - firstY;
        if (contentY < 0) return nullptr;
        int targetRow = contentY / rowHeight;
        for (const auto& p : track->getPlaylists())
        {
            if (inlineMode_ && p.id == track->getMainPlaylistId())
                continue;
            if (targetRow-- == 0)
            {
                rowTop = firstY + contentY / rowHeight * rowHeight
                         - (inlineMode_ ? 0 : scrollY_);
                return &p;
            }
        }
        return nullptr;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.x < 12 || e.x > 126) return;
        int y = 0;
        if (const auto* playlist = playlistAtY(e.y, y))
            beginRename(playlist->id, playlist->name, y);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return;
        int y = 0;
        const auto* playlist = playlistAtY(e.y, y);
        if (!playlist) return;
        const auto playlistId = playlist->id;
        if (e.mods.isPopupMenu())
        {
            openPlaylistMenu(playlistId);
            return;
        }
        if (juce::Rectangle<int>(126, y + juce::jmax(0,
                ((inlineMode_ ? inlineTakeHeight_ : 66) - 25) / 2), 27, 25)
                .contains(e.getPosition()))
        {
            track->soloPlaylistLane(playlistId);
            notifyMutation();
            return;
        }
        if (getArrowBounds(y).contains(e.getPosition()))
        {
            if (playlistId == selectedPlaylist_) promote();
            return;
        }
        if (e.x < 158)
        {
            selectedPlaylist_ = playlistId;
            selectionStart_ = selectionEnd_ = -1;
            if (onSelectionChanged) onSelectionChanged();
            repaint();
            return;
        }
        if (getTimelineBounds(y).contains(e.getPosition()))
        {
            selectedPlaylist_ = playlistId;
            dragAnchor_ = sampleForX(e.x);
            selectionStart_ = selectionEnd_ = dragAnchor_;
            selectionDragging_ = true;
            grabKeyboardFocus();
            if (onSelectionChanged) onSelectionChanged();
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!selectionDragging_) return;
        const auto current = sampleForX(e.x);
        selectionStart_ = juce::jmin(dragAnchor_, current);
        selectionEnd_ = juce::jmax(dragAnchor_, current);
        if (onSelectionChanged) onSelectionChanged();
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { selectionDragging_ = false; }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& w) override
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return;
        // The entire song cannot be a single fixed-width timeline: phrases
        // become only a few pixels in a long session. Ctrl+wheel zooms at
        // the pointer, Shift+wheel pans, plain wheel scrolls take lanes.
        if (event.mods.isCtrlDown() || event.mods.isCommandDown())
        {
            if (followingArrangement_ && onTimelineZoom)
            {
                onTimelineZoom(w.deltaY > 0.0f);
                return;
            }
            const double previousSpan = juce::jmax(1.0, viewEnd_ - viewStart_);
            const double span = juce::jlimit(
                juce::jmax(64.0, sampleRate_ * 0.05), viewMaxEnd_,
                previousSpan * std::pow(1.5, -(double)w.deltaY * 3.0));
            const double fraction = juce::jlimit(0.0, 1.0,
                ((double)event.x - 161.0) / (double)juce::jmax(40, getWidth() - 222));
            const double sampleUnderPointer = viewStart_ + previousSpan * fraction;
            viewStart_ = juce::jlimit(0.0, juce::jmax(0.0, viewMaxEnd_ - span),
                                      sampleUnderPointer - fraction * span);
            viewEnd_ = viewStart_ + span;
        }
        else if (event.mods.isShiftDown())
        {
            if (followingArrangement_ && onTimelinePan)
            {
                onTimelinePan(w.deltaY);
                return;
            }
            const double span = viewEnd_ - viewStart_;
            viewStart_ = juce::jlimit(0.0, juce::jmax(0.0, viewMaxEnd_ - span),
                                      viewStart_ - (double)w.deltaY * span * 0.3);
            viewEnd_ = viewStart_ + span;
        }
        else if (!inlineMode_)
        {
            const int available = juce::jmax(0, (int)track->getPlaylists().size() * 66
                                                 - (getHeight() - 105));
            scrollY_ = juce::jlimit(0, available, scrollY_ - juce::roundToInt(w.deltaY * 120.f));
        }
        else if (onTimelineVerticalScroll)
        {
            onTimelineVerticalScroll(w.deltaY);
            return;
        }
        repaint();
    }

private:
    void beginRename(const juce::String& id, const juce::String& name, int y)
    {
        if (!modificationAllowed()) return;
        renamePlaylistId_ = id;
        renameEditor_.setText(name, false);
        renameEditor_.setBounds(18, y + 4, 106, 28);
        renameEditor_.setVisible(true);
        renameEditor_.toFront(true);
        renameEditor_.grabKeyboardFocus();
        renameEditor_.selectAll();
    }
    void commitRename()
    {
        if (renamePlaylistId_.isEmpty()) return;
        const auto id = renamePlaylistId_;
        renamePlaylistId_.clear();
        const auto text = renameEditor_.getText().trim();
        renameEditor_.setVisible(false);
        if (auto* track = tracks_.getTrack(trackId_))
            if (track->renamePlaylist(id, text))
                notifyMutation();
    }
    void recalculateView(bool resetViewport = true)
    {
        double fullExtent = sampleRate_ * 12.0;
        for (auto* clip : clips_.getClipsOnTrack(trackId_))
            if (clip)
                fullExtent = juce::jmax(fullExtent,
                    (double)clip->getEndPosition() + sampleRate_ * 0.5);
        viewMaxEnd_ = fullExtent;
        if (resetViewport)
        {
            viewStart_ = 0.0;
            viewEnd_ = fullExtent;
            scrollY_ = 0;
        }
        else
        {
            // Promoting or renaming a take must not destroy the current
            // zoom/scroll position just as the user checks the comp boundary.
            const double span = juce::jlimit(1.0, viewMaxEnd_, viewEnd_ - viewStart_);
            viewStart_ = juce::jlimit(0.0, juce::jmax(0.0, viewMaxEnd_ - span), viewStart_);
            viewEnd_ = viewStart_ + span;
        }
    }
    juce::Rectangle<int> getTimelineBounds(int y) const
    {
        const int h = inlineMode_ ? inlineTakeHeight_ : 66;
        return { 161, y + 5, juce::jmax(40, getWidth() - 222),
                 juce::jmax(20, h - 14) };
    }
    juce::Rectangle<int> getArrowBounds(int y) const
    {
        const int h = inlineMode_ ? inlineTakeHeight_ : 66;
        return { getWidth() - 49, y + juce::jmax(0, (h - 30) / 2), 34, 30 };
    }
    float xForSample(double sample) const
    {
        return (float)(161.0 + (sample - viewStart_) /
            juce::jmax(1.0, viewEnd_ - viewStart_) * (double)juce::jmax(40, getWidth() - 222));
    }
    int64_t sampleForX(float x) const
    {
        const auto region = getTimelineBounds(0);
        const double fraction = juce::jlimit(0.0, 1.0,
            (double)(x - region.getX()) / juce::jmax(1, region.getWidth()));
        return (int64_t)std::llround(viewStart_ + (viewEnd_ - viewStart_) * fraction);
    }
    // Building thumbnails can open WAV files. Only call it on the message
    // thread when the visible lane set changes, never from paint().
    void preflightThumbnails()
    {
        for (auto* clip : clips_.getClipsOnTrack(trackId_))
        {
            auto* audio = dynamic_cast<AudioClip*>(clip);
            if (!audio) continue;
            const auto source = audio->getSourceFile();
            const auto key = source.getFullPathName();
            if (!source.existsAsFile() || thumbs_.find(key) != thumbs_.end())
                continue;
            // Cache the WAV's *actual* sample rate on the UI thread. Paint()
            // must never open a reader or derive a rate from a cropped clip.
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                formats_.createReaderFor(source));
            thumbnailSampleRates_[key] = reader && reader->sampleRate > 0.0
                ? reader->sampleRate : sampleRate_;
            auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, formats_, thumbnailCache_);
            thumbnail->setSource(new juce::FileInputSource(source));
            thumbs_.emplace(key, std::move(thumbnail));
        }
    }
    juce::AudioThumbnail* getThumbnail(const juce::File& source) const
    {
        auto it = thumbs_.find(source.getFullPathName());
        return it == thumbs_.end() ? nullptr : it->second.get();
    }
    bool modificationAllowed()
    {
        if (!canModify || canModify()) return true;
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Playlists",
            "Stop recording or exporting before changing playlist structure.");
        return false;
    }
    void notifyMutation()
    {
        TrackPlaylistCompCore::publishPlaybackVisibility(tracks_, clips_);
        recalculateView(false);
        preflightThumbnails();
        if (onChanged) onChanged();
        repaint();
    }
    void createNew(bool duplicate)
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track || !modificationAllowed()) return;
        const auto previous = track->getMainPlaylistId();
        const auto oldClips = clips_.getClipsOnTrack(trackId_);
        std::vector<juce::ValueTree> copied;
        if (duplicate)
            for (auto* clip : oldClips)
                if (clip && clip->getPlaylistId() == previous)
                    copied.push_back(clip->getState().createCopy());
        const auto newId = track->createPlaylist();
        for (auto& state : copied)
        {
            state.setProperty("playlistId", newId, nullptr);
            auto oldId = state.getProperty("id").toString();
            if (auto* clone = clips_.recreateClipFromState(state))
            {
                audioFiles_.shareForClip(oldId, clone->getID());
                if (auto* audio = dynamic_cast<AudioClip*>(clone))
                    if (!audioFiles_.hasAudio(clone->getID()))
                        audioFiles_.loadForClip(clone->getID(), audio->getSourceFile(), false);
            }
        }
        selectedPlaylist_ = previous;
        selectionStart_ = selectionEnd_ = -1;
        notifyMutation();
    }
    void promote()
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track || !modificationAllowed() || selectedPlaylist_.isEmpty()
            || selectedPlaylist_ == track->getTargetPlaylistId()
            || selectionStart_ < 0 || selectionEnd_ <= selectionStart_) return;
        auto action = std::make_unique<PromotePlaylistSelectionCommand>(
            trackId_, selectedPlaylist_, track->getTargetPlaylistId(),
            selectionStart_, selectionEnd_, sampleRate_, tracks_, clips_, audioFiles_);
        if (!action->isValid()) return;
        CommandManager::getInstance().execute(std::move(action));
        notifyMutation();
    }
    void openPlaylistMenu(const juce::String& id)
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return;
        juce::PopupMenu menu;
        // PopupMenu callbacks run asynchronously, and the user can close the
        // project (destroying the panel) while a menu is still open.
        juce::Component::SafePointer<TrackPlaylistPanel> weakThis(this);
        const bool isMain = id == track->getMainPlaylistId();
        const bool isTarget = id == track->getTargetPlaylistId();
        menu.addItem("Show as Main", true, isMain, [weakThis, id]
        {
            if (!weakThis) return;
            if (auto* t = weakThis->tracks_.getTrack(weakThis->trackId_))
                if (weakThis->modificationAllowed() && t->showPlaylistAsMain(id))
                    weakThis->notifyMutation();
        });
        menu.addItem("Set as Comp Target", true, isTarget, [weakThis, id]
        {
            if (!weakThis) return;
            if (auto* t = weakThis->tracks_.getTrack(weakThis->trackId_))
                if (weakThis->modificationAllowed() && t->setTargetPlaylist(id))
                    weakThis->notifyMutation();
        });
        menu.addItem("Solo / Audition", true, false, [weakThis, id]
        {
            if (!weakThis) return;
            if (auto* t = weakThis->tracks_.getTrack(weakThis->trackId_))
                if (t->soloPlaylistLane(id)) weakThis->notifyMutation();
        });
        menu.addSeparator();
        menu.addItem("Promote Highlight to Target", id != track->getTargetPlaylistId()
            && selectedPlaylist_ == id && selectionEnd_ > selectionStart_,
            false, [weakThis, id]
            {
                if (!weakThis) return;
                weakThis->selectedPlaylist_ = id;
                weakThis->promote();
            });
        menu.addItem("New Playlist", true, false, [weakThis]
        {
            if (weakThis) weakThis->createNew(false);
        });
        menu.addItem("Duplicate Main", true, false, [weakThis]
        {
            if (weakThis) weakThis->createNew(true);
        });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [weakThis](int) { juce::ignoreUnused(weakThis); });
    }

    TrackManager& tracks_;
    ClipManager& clips_;
    AudioFileManager& audioFiles_;
    TrackID trackId_;
    double sampleRate_ = 44100.0, viewStart_ = 0.0, viewEnd_ = 44100.0 * 12.0;
    double viewMaxEnd_ = 44100.0 * 12.0;
    juce::String selectedPlaylist_;
    int64_t selectionStart_ = -1, selectionEnd_ = -1, dragAnchor_ = -1;
    bool selectionDragging_ = false;
    bool followingArrangement_ = false;
    bool inlineMode_ = false;
    int inlineTakeHeight_ = 66;
    int scrollY_ = 0;
    juce::TextButton newButton_, duplicateButton_, closeButton_;
    juce::TextEditor renameEditor_;
    juce::String renamePlaylistId_;
    juce::AudioFormatManager formats_;
    juce::AudioThumbnailCache thumbnailCache_ { 128 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs_;
    std::map<juce::String, double> thumbnailSampleRates_;
};
} // namespace DAW

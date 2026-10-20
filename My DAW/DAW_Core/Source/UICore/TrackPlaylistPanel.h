#pragma once
#include <JuceHeader.h>
#include <map>
#include "../TrackCore/TrackPlaylistCompCore.h"
#include "../CommandCore/CommandManager.h"

namespace DAW
{
// APEX Playlist View: one selected track, expandable take lanes underneath the
// active main/target strip. Selection drag + arrow promotes by sample location.
// A separate compact panel avoids mutating the established arranger's track-
// height/scroll authority while sharing the same canonical ClipManager.
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
        newButton_.setButtonText("+ New Playlist");
        duplicateButton_.setButtonText("Duplicate Main");
        closeButton_.setButtonText("Close");
        newButton_.onClick = [this] { createNew(false); };
        duplicateButton_.onClick = [this] { createNew(true); };
        closeButton_.onClick = [this] { if (onClose) onClose(); };
    }

    void createForTrack(const TrackID& id, bool duplicate, double engineRate)
    {
        showForTrack(id, engineRate);
        createNew(duplicate);
    }

    std::function<bool()> canModify;
    std::function<void()> onChanged;
    std::function<void()> onClose;

    void showForTrack(const TrackID& id, double engineRate)
    {
        trackId_ = id;
        sampleRate_ = engineRate > 0 ? engineRate : 44100.0;
        selectedPlaylist_.clear();
        selectionStart_ = selectionEnd_ = -1;
        thumbs_.clear();
        recalculateView();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff110f1d));
        g.setColour(juce::Colour(0xffb98bff));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 10.f, 1.5f);
        auto* track = tracks_.getTrack(trackId_);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.f, juce::Font::bold));
        g.drawText(track ? track->getName() + "  /  PLAYLISTS" : juce::String("PLAYLISTS"),
                   18, 6, juce::jmax(0, getWidth() - 335), 37, juce::Justification::centredLeft);
        g.setFont(juce::Font(11.f));
        g.setColour(juce::Colour(0xffa9a1be));
        g.drawText("Drag a phrase in a take lane; press the up arrow to copy it to the target.",
                   18, 43, juce::jmax(0, getWidth() - 40), 23, juce::Justification::centredLeft);
        if (!track) return;
        const auto& playlists = track->getPlaylists();
        g.setColour(juce::Colour(0xff5f5974));
        g.fillRect(10, 74, getWidth() - 20, 1);
        for (int i = 0; i < (int)playlists.size(); ++i)
        {
            const auto& p = playlists[(size_t)i];
            const int y = 81 + i * 66 - scrollY_;
            if (y + 66 < 76 || y >= getHeight() - 22) continue;
            const bool main = p.id == track->getMainPlaylistId();
            const bool target = p.id == track->getTargetPlaylistId();
            const bool solo = p.id == track->getAuditionPlaylistId();
            const bool selected = p.id == selectedPlaylist_;
            auto row = juce::Rectangle<int>(10, y, getWidth() - 20, 62);
            g.setColour(main ? juce::Colour(0xff31204c) :
                        selected ? juce::Colour(0xff23233a) : juce::Colour(0xff191829));
            g.fillRoundedRectangle(row.toFloat(), 6.f);
            g.setColour(target ? juce::Colour(0xffb985ff) : juce::Colour(0xff413b5c));
            g.drawRoundedRectangle(row.toFloat(), 6.f, target ? 1.5f : 0.8f);
            g.setFont(juce::Font(12.f, juce::Font::bold));
            g.setColour(target ? juce::Colours::white : juce::Colour(0xffbbb5d5));
            g.drawText(p.name, 18, y + 8, 105, 23, juce::Justification::centredLeft, true);
            g.setFont(juce::Font(10.f));
            g.setColour(juce::Colour(0xffa393c9));
            g.drawText(main ? "MAIN" : target ? "TARGET" : "TAKE",
                       18, y + 34, 62, 16, juce::Justification::centredLeft);
            auto soloRect = juce::Rectangle<int>(126, y + 19, 27, 25);
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
                auto body = juce::Rectangle<int>((int)x1, y + 10,
                                        juce::jmax(1, (int)(x2 - x1)), 42);
                g.setColour(main ? juce::Colour(0xff6642b6) : juce::Colour(0xff315977));
                g.fillRoundedRectangle(body.toFloat(), 3.f);
                if (auto* thumb = getThumbnail(audio->getSourceFile()))
                {
                    const double sr = thumb->getNumChannels() > 0 && thumb->getTotalLength() > 0
                        ? thumb->getTotalLength() /
                            juce::jmax(1.0, (double)audio->getSourceEndSample())
                        : 1.0 / sampleRate_;
                    const double sec = (double)audio->getSourceStartSample() * sr;
                    const double duration = (end - start) / sampleRate_;
                    g.setColour(juce::Colour(0xff99e8ff).withAlpha(0.85f));
                    thumb->drawChannels(g, body.reduced(1), sec,
                                         sec + juce::jmax(0.001, duration), 0.82f);
                }
            }
            if (selected && selectionStart_ >= 0 && selectionEnd_ > selectionStart_)
            {
                auto xa = xForSample((double)selectionStart_);
                auto xb = xForSample((double)selectionEnd_);
                g.setColour(juce::Colour(0x7055eaff));
                g.fillRect(juce::Rectangle<float>(xa, (float)y + 3.f,
                                                 juce::jmax(1.f, xb - xa), 56.f));
                g.setColour(juce::Colour(0xff90eeff));
                g.drawLine(xa, (float)y + 2.f, xa, (float)y + 59.f, 1.5f);
                g.drawLine(xb, (float)y + 2.f, xb, (float)y + 59.f, 1.5f);
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
        g.setColour(juce::Colour(0xff87829c));
        g.setFont(juce::Font(10.f));
        g.drawText("Source WAVs and alternate takes are preserved when the project closes.",
                   18, getHeight() - 22, getWidth() - 30, 19, juce::Justification::centredLeft);
    }

    void resized() override
    {
        closeButton_.setBounds(getWidth() - 82, 12, 70, 28);
        duplicateButton_.setBounds(getWidth() - 235, 12, 145, 28);
        newButton_.setBounds(getWidth() - 367, 12, 125, 28);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track || e.y < 81) return;
        const int i = (e.y + scrollY_ - 81) / 66;
        if (i < 0 || i >= (int)track->getPlaylists().size()) return;
        const auto playlistId = track->getPlaylists()[(size_t)i].id;
        const int y = 81 + i * 66 - scrollY_;
        if (e.mods.isPopupMenu())
        {
            openPlaylistMenu(playlistId);
            return;
        }
        if (juce::Rectangle<int>(126, y + 19, 27, 25).contains(e.getPosition()))
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
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!selectionDragging_) return;
        const auto current = sampleForX(e.x);
        selectionStart_ = juce::jmin(dragAnchor_, current);
        selectionEnd_ = juce::jmax(dragAnchor_, current);
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { selectionDragging_ = false; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        auto* track = tracks_.getTrack(trackId_);
        if (!track) return;
        const int available = juce::jmax(0, (int)track->getPlaylists().size() * 66
                                             - (getHeight() - 105));
        scrollY_ = juce::jlimit(0, available, scrollY_ - juce::roundToInt(w.deltaY * 120.f));
        repaint();
    }

private:
    void recalculateView()
    {
        viewStart_ = 0;
        viewEnd_ = sampleRate_ * 12.0;
        for (auto* clip : clips_.getClipsOnTrack(trackId_))
            if (clip)
                viewEnd_ = juce::jmax(viewEnd_, (double)clip->getEndPosition() + sampleRate_ * 0.5);
        scrollY_ = 0;
    }
    juce::Rectangle<int> getTimelineBounds(int y) const
    {
        return { 161, y + 5, juce::jmax(40, getWidth() - 222), 52 };
    }
    juce::Rectangle<int> getArrowBounds(int y) const
    {
        return { getWidth() - 49, y + 16, 34, 30 };
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
    juce::AudioThumbnail* getThumbnail(const juce::File& source)
    {
        if (!source.existsAsFile()) return nullptr;
        const auto key = source.getFullPathName();
        auto it = thumbs_.find(key);
        if (it != thumbs_.end()) return it->second.get();
        auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, formats_, thumbnailCache_);
        thumbnail->setSource(new juce::FileInputSource(source));
        auto* result = thumbnail.get();
        thumbs_.emplace(key, std::move(thumbnail));
        return result;
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
        recalculateView();
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
        const bool isMain = id == track->getMainPlaylistId();
        const bool isTarget = id == track->getTargetPlaylistId();
        menu.addItem("Show as Main", true, isMain, [this, id]
        {
            if (auto* t = tracks_.getTrack(trackId_))
                if (modificationAllowed() && t->showPlaylistAsMain(id)) notifyMutation();
        });
        menu.addItem("Set as Comp Target", true, isTarget, [this, id]
        {
            if (auto* t = tracks_.getTrack(trackId_))
                if (modificationAllowed() && t->setTargetPlaylist(id)) notifyMutation();
        });
        menu.addItem("Solo / Audition", true, false, [this, id]
        {
            if (auto* t = tracks_.getTrack(trackId_))
                if (t->soloPlaylistLane(id)) notifyMutation();
        });
        menu.addSeparator();
        menu.addItem("Promote Highlight to Target", id != track->getTargetPlaylistId()
            && selectedPlaylist_ == id && selectionEnd_ > selectionStart_,
            false, [this, id]
            {
                selectedPlaylist_ = id;
                promote();
            });
        menu.addItem("New Playlist", true, false, [this] { createNew(false); });
        menu.addItem("Duplicate Main", true, false, [this] { createNew(true); });
        juce::Component::SafePointer<TrackPlaylistPanel> weakThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [weakThis](int) { juce::ignoreUnused(weakThis); });
    }

    TrackManager& tracks_;
    ClipManager& clips_;
    AudioFileManager& audioFiles_;
    TrackID trackId_;
    double sampleRate_ = 44100.0, viewStart_ = 0.0, viewEnd_ = 44100.0 * 12.0;
    juce::String selectedPlaylist_;
    int64_t selectionStart_ = -1, selectionEnd_ = -1, dragAnchor_ = -1;
    bool selectionDragging_ = false;
    int scrollY_ = 0;
    juce::TextButton newButton_, duplicateButton_, closeButton_;
    juce::AudioFormatManager formats_;
    juce::AudioThumbnailCache thumbnailCache_ { 128 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs_;
};
} // namespace DAW

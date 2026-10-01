#pragma once
#include <JuceHeader.h>
#include "../FloatingWindowCore/FloatingWindowBase.h"
#include "../MidiCore/PianoRollComponent.h"
#include "../MidiCore/PianoRollToolbarComponent.h"
#include "../MidiCore/MidiClip.h"
#include "../MidiCore/PlayableMidiKeyboardComponent.h"
#include "../MidiCore/VirtualMidiKeyboardCore.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

class TransportController;

class PianoRollVelocityLane : public juce::Component
{
public:
    void setClipModel(PianoRollClipModel* model)   { model_ = model; repaint(); }
    void setPixelsPerTick(float ppt)               { ppt_ = ppt; repaint(); }
    void setScrollOffset(float x)                  { scrollX_ = x; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.setColour(theme.colors.background.darker(0.2f));
        g.fillRect(getLocalBounds());

        g.setColour(theme.colors.separatorSoft.withAlpha(0.4f));
        g.drawHorizontalLine(0, 0.f, (float)getWidth());

        if (!model_) return;

        const float h = (float)getHeight();
        for (const auto& note : model_->getAllNotes())
        {
            float x = (float)note.startTick * ppt_ - scrollX_;
            float barH = h * ((float)note.velocity / 127.0f);
            float barW = juce::jmax(2.0f, 6.0f);

            auto baseCol = note.selected ? juce::Colour(0xFF8888FF)
                                         : juce::Colour(0xFF4FC3F7);
            g.setColour(baseCol.withAlpha(0.85f));
            g.fillRect(x, h - barH, barW, barH);
        }

        g.setColour(theme.colors.textSecondary);
        g.setFont(theme.fonts.small);
        g.drawText("VEL", 4, 2, 30, 14, juce::Justification::centredLeft, false);
    }

    void mouseDown(const juce::MouseEvent& e) override { dragVelocity(e); }
    void mouseDrag(const juce::MouseEvent& e) override { dragVelocity(e); }

private:
    void dragVelocity(const juce::MouseEvent& e)
    {
        if (!model_) return;
        const float h = (float)getHeight();
        const float newVelNorm = juce::jlimit(0.0f, 1.0f, 1.0f - e.position.y / h);
        const int newVel = juce::jlimit(1, 127, (int)(newVelNorm * 127.0f));

        float bestDist = 999999.f;
        juce::int64 bestId = 0;
        for (const auto& note : model_->getAllNotes())
        {
            float nx = (float)note.startTick * ppt_ - scrollX_;
            float dist = std::abs(e.position.x - nx);
            if (dist < bestDist) { bestDist = dist; bestId = note.id; }
        }
        if (bestId != 0 && bestDist < 16.f)
        {
            if (auto* n = model_->findNote(bestId))
            {
                auto edited = *n;
                edited.velocity = (juce::uint8)newVel;
                model_->modifyNote(bestId, edited);
            }
        }
        repaint();
    }

    PianoRollClipModel* model_ = nullptr;
    float ppt_ = 0.1f;
    float scrollX_ = 0.f;
};

class PianoRollWindow : public FloatingWindowBase,
                        private juce::Timer
{
public:
    PianoRollWindow() : FloatingWindowBase("PIANO ROLL")
    {
        setMinimumSize(520, 360);
        setSize(920, 620);
        addAndMakeVisible(toolbar_);
        addAndMakeVisible(pianoRollComponent_);
        addAndMakeVisible(velocityLane_);
        addAndMakeVisible(playableKeyboard_);
        toolbar_.bind(&pianoRollComponent_);
        startTimerHz(60);
    }

    void setMidiClip(MidiClip* clip)
    {
        clip_ = clip;
        auto* model = clip_ != nullptr ? &clip_->getPianoRollModel() : nullptr;
        pianoRollComponent_.setClipModel(model);
        if (virtualKeyboardCore_ != nullptr)
            virtualKeyboardCore_->setActiveTrackId(clip_ != nullptr ? clip_->getTrackID() : juce::String());
        velocityLane_.setClipModel(model);
        setWindowTitle(clip_ != nullptr ? "PIANO ROLL - " + clip_->getName() : "PIANO ROLL");
    }

    void setTransport(TransportController* transport) noexcept { transport_ = transport; }
    void setVirtualKeyboardCore(VirtualMidiKeyboardCore* keyboard) noexcept
    {
        virtualKeyboardCore_ = keyboard;
        playableKeyboard_.setKeyboardCore(keyboard);
        playableKeyboard_.setLayout(VirtualMidiKeyboardCore::Layout::PianoStandard);
        playableKeyboard_.setBaseOctave(2);
        playableKeyboard_.setVelocityMode(VirtualMidiKeyboardCore::VelocityMode::YPosition);
    }

    MidiClip* getMidiClip() const noexcept { return clip_; }
    PianoRollComponent& getPianoRollComponent() noexcept { return pianoRollComponent_; }

protected:
    void layoutContent() override
    {
        const bool hidden = isMinimized();
        toolbar_.setVisible(!hidden);
        pianoRollComponent_.setVisible(!hidden);
        velocityLane_.setVisible(!hidden);
        playableKeyboard_.setVisible(!hidden);
        if (!hidden)
        {
            auto area = getContentArea();
            toolbar_.setBounds(area.removeFromTop(toolbarHeight_));
            playableKeyboard_.setBounds(area.removeFromBottom(playableKeyboardHeight_));
            velocityLane_.setBounds(area.removeFromBottom(velocityLaneH_));
            pianoRollComponent_.setBounds(area);
        }
    }

private:
    void timerCallback() override
    {
        if (!transport_ || !clip_) return;
        const auto pos = transport_->getPosition();
        const double bpm  = clip_->getTempo();
        const double sr   = clip_->getSampleRate();
        const int    ppq  = clip_->getPianoRollModel().getPPQ();
        const double beats = (double)pos / sr * (bpm / 60.0);
        const auto tick   = (juce::int64)(beats * (double)ppq);
        pianoRollComponent_.setPlayheadTick(tick);

        velocityLane_.setPixelsPerTick(pianoRollComponent_.getPixelsPerTick());
        velocityLane_.repaint();
    }

    MidiClip* clip_ = nullptr;
    TransportController* transport_ = nullptr;
    VirtualMidiKeyboardCore* virtualKeyboardCore_ = nullptr;
    PianoRollToolbarComponent toolbar_;
    PianoRollComponent pianoRollComponent_;
    PianoRollVelocityLane velocityLane_;
    PlayableMidiKeyboardComponent playableKeyboard_;
    static constexpr int toolbarHeight_   = 36;
    static constexpr int playableKeyboardHeight_ = 112;
    static constexpr int velocityLaneH_   = 80;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWindow)
};

} // namespace DAW

// ===========================================================================
// ClipPropertiesWindowCore.h
// Floating clip properties panel — opened on double-click in the arrangement.
// Incluye knobs de Pitch / Time Stretch / Formant del sistema pro.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include "ArrangementClipModel.h"
#include "ClipAutomationPanel.h"
#include "TimePitchTypesCore.h"
#include "TimePitchUICore.h"
#include "TimePitchKnobMappingCore.h"
#include "VoiceTransformUICore.h"

namespace ArrangementEditor
{
    // -----------------------------------------------------------------------
    // Inner panel (the actual UI content)
    // -----------------------------------------------------------------------
    class ClipPropertiesPanelCore : public juce::Component,
                                    private juce::Slider::Listener,
                                    private juce::Button::Listener
    {
    public:
        // Called whenever the user changes a value so the model + view stay in sync
        std::function<void(ArrangementClipModel&)> onModelChanged;

        // Called when pitch/stretch/mode changes (requires processed waveform regen)
        std::function<void(ArrangementClipModel&)> onProcessedWaveformInvalidated;

        void activateAutomationMode(const DAW::TrackID& trackId,
                                    const ClipAutomationPanelCallbacks& callbacks,
                                    const juce::String& explicitClipId = {})
        {
            automationTrackId_   = trackId;
            automationCallbacks_ = callbacks;
            automationExplicitClipId_ = explicitClipId;
            automationMode_ = true;
            rebuildAutomationPanel();
            setSize(420, 670);
            if (auto* parent = getParentComponent())
                parent->resized();
            resized();
            repaint();
        }

        void deactivateAutomationMode()
        {
            automationMode_ = false;
            automationPanel_.reset();
            setSize(420, 450);
            if (auto* parent = getParentComponent())
                parent->resized();
            resized();
            repaint();
        }

        ClipPropertiesPanelCore()
        {
            auto setupSlider = [this](juce::Slider& s, juce::Label& lbl,
                                      const juce::String& text,
                                      double lo, double hi, double def)
            {
                lbl.setText(text, juce::dontSendNotification);
                lbl.setFont(juce::Font(11.f));
                lbl.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(lbl);

                s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
                s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
                s.setRange(lo, hi, 0.0);
                s.setValue(def, juce::dontSendNotification);
                s.addListener(this);
                addAndMakeVisible(s);
            };

            // --- Controles originales ---
            setupSlider(gainSlider_,    gainLbl_,    "Gain (dB)",  -60.0, 12.0,  0.0);
            setupSlider(fadeInSlider_,  fadeInLbl_,  "Fade In",      0.0, 10.0,  0.0);
            setupSlider(fadeOutSlider_, fadeOutLbl_, "Fade Out",     0.0, 10.0,  0.0);

            // --- Sistema pro pitch/time ---
            // Pitch: -2400..+24 semitonos. Vertical drag arriba = sube pitch.
            setupSlider(pitchSlider_,   pitchLbl_,   "Pitch (st)",
                        TimePitchConstants::kPitchMinSemitones,
                        TimePitchConstants::kPitchMaxSemitones, 0.0);
            pitchSlider_.setSkewFactorFromMidPoint(0.0);

            // Fine tune: -100..+100 cents
            setupSlider(fineTuneSlider_, fineTuneLbl_, "Fine (ct)",
                        TimePitchConstants::kFineTuneMinCents,
                        TimePitchConstants::kFineTuneMaxCents, 0.0);

            // Stretch: ratio 0.25..4.0 (mostrado como porcentaje)
            stretchSlider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            stretchSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
            stretchSlider_.setRange(TimePitchConstants::kStretchMinRatio,
                                    TimePitchConstants::kStretchMaxRatio, 0.0);
            stretchSlider_.setValue(1.0, juce::dontSendNotification);
            stretchSlider_.setSkewFactorFromMidPoint(1.0); // log scale: centro = 100%
            stretchSlider_.addListener(this);
            stretchLbl_.setText("Stretch", juce::dontSendNotification);
            stretchLbl_.setFont(juce::Font(11.f));
            stretchLbl_.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(stretchLbl_);
            addAndMakeVisible(stretchSlider_);
            stretchSlider_.textFromValueFunction = [](double v) {
                char buf[16]; snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0);
                return juce::String(buf);
            };

            // Formant: -12..+12 st (solo activo en modo Vocal/PitchOnly)
            setupSlider(formantSlider_, formantLbl_, "Formant",
                        TimePitchConstants::kFormantMinSemitones,
                        TimePitchConstants::kFormantMaxSemitones, 0.0);

            // Mode dropdown
            modeLbl_.setText("Mode", juce::dontSendNotification);
            modeLbl_.setFont(juce::Font(11.f));
            addAndMakeVisible(modeLbl_);

            for (int i = 0; i < kTimePitchModeCount; ++i)
                modeBox_.addItem(TimePitchUICore::modeName(static_cast<TimePitchMode>(i)), i + 1);
            modeBox_.setSelectedId(static_cast<int>(TimePitchMode::Vocal) + 1,
                                   juce::dontSendNotification);
            modeBox_.onChange = [this] { onModeChanged(); };
            addAndMakeVisible(modeBox_);

            // Preserve formants toggle (solo visible en modo Vocal)
            preserveFormantBtn_.setButtonText("Preserve Formants");
            preserveFormantBtn_.setClickingTogglesState(true);
            preserveFormantBtn_.addListener(this);
            addAndMakeVisible(preserveFormantBtn_);

            voiceTransformLbl_.setText("VOICE TRANSFORM", juce::dontSendNotification);
            voiceTransformLbl_.setFont(juce::Font(12.f, juce::Font::bold));
            addAndMakeVisible(voiceTransformLbl_);

            voicePresetLbl_.setText("Preset", juce::dontSendNotification);
            voicePresetLbl_.setFont(juce::Font(11.f));
            addAndMakeVisible(voicePresetLbl_);
            for (int i = 0; i < kVoiceTransformPresetCount; ++i)
                voicePresetBox_.addItem(VoiceTransformUICore::presetName(static_cast<VoiceTransformPreset>(i)), i + 1);
            voicePresetBox_.setSelectedId(static_cast<int>(VoiceTransformPreset::Demon) + 1, juce::dontSendNotification);
            voicePresetBox_.onChange = [this] { onVoicePresetChanged(); };
            addAndMakeVisible(voicePresetBox_);

            setupSlider(ultraDemonSlider_, ultraDemonLbl_, "ULTRA DEMON", 0.0, 100.0, 0.0);
            ultraDemonSlider_.textFromValueFunction = [](double v) {
                char buf[16]; snprintf(buf, sizeof(buf), "%.0f%%", v);
                return juce::String(buf);
            };

            reactiveBtn_.setButtonText("Reactive Mode");
            reactiveBtn_.setClickingTogglesState(true);
            reactiveBtn_.setToggleState(true, juce::dontSendNotification);
            reactiveBtn_.addListener(this);
            addAndMakeVisible(reactiveBtn_);

            muteBtn_.setButtonText("Mute");
            muteBtn_.setClickingTogglesState(true);
            muteBtn_.addListener(this);
            addAndMakeVisible(muteBtn_);

            nameLabel_.setFont(juce::Font(13.f, juce::Font::bold));
            nameLabel_.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(nameLabel_);

            setSize(420, 450);
            updateControlStates();
        }

        void setClip(ArrangementClipModel* clip)
        {
            m_clip = clip;
            if (clip == nullptr)
            {
                nameLabel_.setText({}, juce::dontSendNotification);
                automationPanel_.reset();
                return;
            }

            nameLabel_.setText(juce::String(clip->clipName), juce::dontSendNotification);

            // gain: stored as linear, show as dB
            gainSlider_.setValue(juce::Decibels::gainToDecibels(clip->gain, -60.f),
                                 juce::dontSendNotification);
            fadeInSlider_.setValue(clip->fadeInLength,   juce::dontSendNotification);
            fadeOutSlider_.setValue(clip->fadeOutLength, juce::dontSendNotification);
            muteBtn_.setToggleState(clip->muted,         juce::dontSendNotification);

            // --- Sistema pro pitch/time ---
            const TimePitchState& tp = clip->timePitch;
            pitchSlider_.setValue(tp.pitchSemitones,   juce::dontSendNotification);
            fineTuneSlider_.setValue(tp.fineTuneCents, juce::dontSendNotification);
            stretchSlider_.setValue(tp.stretchRatio,   juce::dontSendNotification);
            formantSlider_.setValue(tp.formantSemitones, juce::dontSendNotification);
            modeBox_.setSelectedId(static_cast<int>(tp.mode) + 1, juce::dontSendNotification);
            preserveFormantBtn_.setToggleState(tp.preserveFormants, juce::dontSendNotification);
            voicePresetBox_.setSelectedId(static_cast<int>(tp.voiceTransform.preset) + 1, juce::dontSendNotification);
            ultraDemonSlider_.setValue(tp.voiceTransform.demonAmount * 100.0, juce::dontSendNotification);
            reactiveBtn_.setToggleState(tp.voiceTransform.reactiveMode, juce::dontSendNotification);

            updateControlStates();

            if (automationMode_)
                rebuildAutomationPanel();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(10);

            juce::Rectangle<int> automationArea;
            if (automationMode_ && automationPanel_ != nullptr)
            {
                const int reservedH = juce::jmin(210, juce::jmax(150, automationPanel_->getPreferredHeight()));
                automationArea = area.removeFromBottom(reservedH);
                area.removeFromBottom(10);
            }

            nameLabel_.setBounds(area.removeFromTop(22));
            area.removeFromTop(4);

            // Mode row
            auto modeRow = area.removeFromTop(20);
            modeLbl_.setBounds(modeRow.removeFromLeft(40));
            modeBox_.setBounds(modeRow.removeFromLeft(140));
            area.removeFromTop(6);

            // Row 1: Gain | Pitch | Fine Tune
            const int knobW = 90, knobH = 80, lblH = 16, gap = 4;
            auto row1 = area.removeFromTop(lblH + knobH);
            placeKnob(row1, gainLbl_,     gainSlider_,    knobW, knobH, lblH, gap);
            placeKnob(row1, pitchLbl_,    pitchSlider_,   knobW, knobH, lblH, gap);
            placeKnob(row1, fineTuneLbl_, fineTuneSlider_,knobW, knobH, lblH, gap);

            area.removeFromTop(8);

            // Row 2: Stretch | Formant | Fade In
            auto row2 = area.removeFromTop(lblH + knobH);
            placeKnob(row2, stretchLbl_,  stretchSlider_, knobW, knobH, lblH, gap);
            placeKnob(row2, formantLbl_,  formantSlider_, knobW, knobH, lblH, gap);
            placeKnob(row2, fadeInLbl_,   fadeInSlider_,  knobW, knobH, lblH, gap);

            area.removeFromTop(8);

            // Row 3: Fade Out | buttons
            auto row3 = area.removeFromTop(lblH + knobH);
            placeKnob(row3, fadeOutLbl_, fadeOutSlider_, knobW, knobH, lblH, gap);

            area.removeFromTop(8);
            auto btnRow = area.removeFromTop(28);
            muteBtn_.setBounds(btnRow.removeFromLeft(70));
            btnRow.removeFromLeft(8);
            preserveFormantBtn_.setBounds(btnRow.removeFromLeft(160));

            area.removeFromTop(10);
            voiceTransformLbl_.setBounds(area.removeFromTop(18));
            auto voiceRow = area.removeFromTop(24);
            voicePresetLbl_.setBounds(voiceRow.removeFromLeft(46));
            voicePresetBox_.setBounds(voiceRow.removeFromLeft(150));
            voiceRow.removeFromLeft(10);
            reactiveBtn_.setBounds(voiceRow.removeFromLeft(130));

            area.removeFromTop(4);
            auto demonRow = area.removeFromTop(lblH + knobH);
            placeKnob(demonRow, ultraDemonLbl_, ultraDemonSlider_, 112, knobH, lblH, gap);

            if (automationMode_ && automationPanel_ != nullptr)
            {
                automationPanel_->setBounds(automationArea);
                automationPanel_->setVisible(true);
            }
        }

    private:
        void rebuildAutomationPanel()
        {
            automationPanel_.reset();

            if (!automationMode_ || m_clip == nullptr)
                return;

            automationPanel_ = std::make_unique<ClipAutomationPanel>(*m_clip, automationTrackId_, automationCallbacks_, true, automationExplicitClipId_);
            automationPanel_->onClose = [this] { deactivateAutomationMode(); };
            addAndMakeVisible(*automationPanel_);
            automationPanel_->toFront(false);
        }

        static bool isPitchActiveValue(double semitones, double cents)
        {
            return std::abs(semitones) > 0.001 || std::abs(cents) > 0.1;
        }

        void setModeWithoutFeedback(TimePitchMode newMode)
        {
            if (m_clip == nullptr)
                return;

            m_clip->timePitch.mode = newMode;

            const int modeId = static_cast<int>(newMode) + 1;
            juce::ScopedValueSetter<bool> svs(ignoreCallbacks_, true);
            modeBox_.setSelectedId(modeId, juce::dontSendNotification);

            updateControlStates();

            DBG("[ClipProps] Mode changed to " << TimePitchUICore::modeName(newMode));
        }

        void autoSwitchModeForPitchEdit()
        {
            if (m_clip == nullptr || !autoSwitchOutOfTapeMode_)
                return;

            if (m_clip->timePitch.mode == TimePitchMode::Resample)
                setModeWithoutFeedback(TimePitchMode::Vocal);
        }

        void autoSwitchModeForStretchEdit()
        {
            if (m_clip == nullptr || !autoSwitchOutOfTapeMode_)
                return;

            if (m_clip->timePitch.mode == TimePitchMode::Resample)
            {
                if (isPitchActiveValue(m_clip->timePitch.pitchSemitones,
                                       m_clip->timePitch.fineTuneCents))
                    setModeWithoutFeedback(TimePitchMode::Vocal);
                else
                    setModeWithoutFeedback(TimePitchMode::Stretch);
            }
        }

        void placeKnob(juce::Rectangle<int>& row,
                       juce::Label& lbl, juce::Slider& s,
                       int w, int kh, int lh, int gap)
        {
            auto cell = row.removeFromLeft(w + gap);
            lbl.setBounds(cell.removeFromTop(lh));
            s.setBounds(cell.removeFromTop(kh).withWidth(w));
            row.removeFromLeft(gap);
        }

        void sliderValueChanged(juce::Slider* s) override
        {
            if (!m_clip || ignoreCallbacks_) return;

            bool needsProcessedInvalidation = false;

            // Controles originales
            if (s == &gainSlider_)
            {
                const float newGain = juce::Decibels::decibelsToGain((float)s->getValue(), -60.f);
                DBG("[ClipProperties] Gain changed: " << m_clip->gain << " -> " << newGain << " (dB: " << s->getValue() << ")");
                m_clip->gain = newGain;
            }
            else if (s == &fadeInSlider_)
            {
                DBG("[ClipProperties] FadeIn changed: " << m_clip->fadeInLength << " -> " << s->getValue());
                m_clip->fadeInLength = (float)s->getValue();
            }
            else if (s == &fadeOutSlider_)
            {
                DBG("[ClipProperties] FadeOut changed: " << m_clip->fadeOutLength << " -> " << s->getValue());
                m_clip->fadeOutLength = (float)s->getValue();
            }

            // Sistema pro pitch/time — escribe en timePitch Y en los campos legacy
            else if (s == &pitchSlider_)
            {
                autoSwitchModeForPitchEdit();

                const double v = s->getValue();

                m_clip->timePitch.pitchSemitones = v;
                m_clip->pitch = (float) v;
                // Bug 51 fix: stamp version = 1 (semitones) whenever the user
                // edits pitch. A clip with pitchEngineVersion = 0 (legacy cents
                // format) would have the new semitone value misread as cents
                // by UnifiedPitchStateCore::migrateLegacyPitchValue on the next
                // audio block, making +7 st heard as +0.07 st.
                m_clip->timePitch.pitchEngineVersion = 1;
                needsProcessedInvalidation = true;

                DBG("[ClipProps] Pitch changed. pitch=" << v
                    << " stretch=" << m_clip->timePitch.stretchRatio
                    << " mode=" << (int) m_clip->timePitch.mode);
            }
            else if (s == &fineTuneSlider_)
            {
                m_clip->timePitch.fineTuneCents = s->getValue();
                needsProcessedInvalidation = true;
            }
            else if (s == &stretchSlider_)
            {
                autoSwitchModeForStretchEdit();

                const double v = s->getValue();

                m_clip->timePitch.stretchRatio = v;
                m_clip->rate = (float) v;
                needsProcessedInvalidation = true;

                DBG("[ClipProps] Stretch changed. stretch=" << v
                    << " pitch=" << m_clip->timePitch.pitchSemitones
                    << " mode=" << (int) m_clip->timePitch.mode);
            }
            else if (s == &formantSlider_)
            {
                m_clip->timePitch.formantSemitones = s->getValue();
                needsProcessedInvalidation = m_clip->needsProcessedWaveform();
            }
            else if (s == &ultraDemonSlider_)
            {
                m_clip->timePitch.voiceTransform.demonAmount = juce::jlimit(0.f, 1.f, (float) s->getValue() / 100.f);
                needsProcessedInvalidation = true;
            }

            // Fire appropriate callback
            if (needsProcessedInvalidation && onProcessedWaveformInvalidated)
            {
                DBG("[ClipProperties] Calling onProcessedWaveformInvalidated");
                onProcessedWaveformInvalidated(*m_clip);
            }
            else if (onModelChanged)
            {
                DBG("[ClipProperties] Calling onModelChanged (visual-only)");
                onModelChanged(*m_clip);
            }
        }

        void buttonClicked(juce::Button* b) override
        {
            if (!m_clip) return;
            if (b == &muteBtn_)
                m_clip->muted = muteBtn_.getToggleState();
            else if (b == &preserveFormantBtn_)
                m_clip->timePitch.preserveFormants = preserveFormantBtn_.getToggleState();
            else if (b == &reactiveBtn_)
                m_clip->timePitch.voiceTransform.reactiveMode = reactiveBtn_.getToggleState();

            if (b == &reactiveBtn_ && onProcessedWaveformInvalidated)
                onProcessedWaveformInvalidated(*m_clip);
            else if (onModelChanged) onModelChanged(*m_clip);
        }

        void onVoicePresetChanged()
        {
            if (ignoreCallbacks_ || !m_clip) return;

            const int id = voicePresetBox_.getSelectedId() - 1;
            m_clip->timePitch.voiceTransform.preset = static_cast<VoiceTransformPreset>(
                juce::jlimit(0, kVoiceTransformPresetCount - 1, id));

            if (onProcessedWaveformInvalidated)
                onProcessedWaveformInvalidated(*m_clip);
        }

        void onModeChanged()
        {
            if (ignoreCallbacks_ || !m_clip) return;

            const int id = modeBox_.getSelectedId() - 1;
            auto newMode = static_cast<TimePitchMode>(juce::jlimit(0, kTimePitchModeCount - 1, id));
            m_clip->timePitch.mode = newMode;
            updateControlStates();

            // Mode change may require processed waveform regen
            if (onProcessedWaveformInvalidated)
                onProcessedWaveformInvalidated(*m_clip);

            DBG("[ClipProps] User selected mode " << TimePitchUICore::modeName(newMode));
        }

        // Habilita/deshabilita controles según el modo activo
        void updateControlStates()
        {
            const TimePitchMode mode = m_clip
                ? m_clip->timePitch.mode
                : TimePitchMode::Vocal;

            const bool isTape      = (mode == TimePitchMode::Resample);
            const bool isStretch   = (mode == TimePitchMode::Stretch);
            const bool isPitchOnly = (mode == TimePitchMode::PitchOnly);
            const bool isVocal     = (mode == TimePitchMode::Vocal);
            const bool isPerc      = (mode == TimePitchMode::Percussion);
            const bool isTexture   = (mode == TimePitchMode::Texture);
            const bool isHQ        = (mode == TimePitchMode::OfflineHQ);

            const bool pitchEnabled =
                isTape || isPitchOnly || isVocal || isTexture || isHQ;

            const bool stretchEnabled =
                isTape || isStretch || isVocal || isPerc || isTexture || isHQ;

            const bool formantEnabled =
                isVocal || isPitchOnly;

            pitchSlider_.setEnabled(pitchEnabled);
            fineTuneSlider_.setEnabled(pitchEnabled);
            stretchSlider_.setEnabled(stretchEnabled);
            formantSlider_.setEnabled(formantEnabled);
            preserveFormantBtn_.setEnabled(isVocal);

            if (isTape)
            {
                pitchSlider_.setTooltip("Tape mode: pitch and time are linked.");
                fineTuneSlider_.setTooltip("Tape mode: fine tune also affects playback speed.");
                stretchSlider_.setTooltip("Tape mode: stretch changes playback speed.");
            }
            else if (isStretch)
            {
                pitchSlider_.setTooltip("Stretch mode changes duration while preserving pitch.");
                fineTuneSlider_.setTooltip("Stretch mode changes duration while preserving pitch.");
                stretchSlider_.setTooltip("Independent time stretch.");
            }
            else
            {
                pitchSlider_.setTooltip("Independent pitch shift.");
                fineTuneSlider_.setTooltip("Fine tune for independent pitch shift.");
                stretchSlider_.setTooltip("Independent time stretch.");
            }

            formantSlider_.setTooltip(formantEnabled ? "Formant shift." : "Formant control is unavailable in this mode.");
            preserveFormantBtn_.setTooltip(isVocal ? "Preserve vocal formants while pitch shifting." : "Preserve formants is only available in Vocal / Independent mode.");
            modeBox_.setTooltip(juce::String(TimePitchUICore::modeTooltip(mode)));
        }

        ArrangementClipModel* m_clip = nullptr;

        juce::Label  nameLabel_;

        // Controles originales
        juce::Label  gainLbl_, fadeInLbl_, fadeOutLbl_;
        juce::Slider gainSlider_, fadeInSlider_, fadeOutSlider_;

        // Sistema pro pitch/time
        juce::Label  pitchLbl_, fineTuneLbl_, stretchLbl_, formantLbl_, modeLbl_;
        juce::Slider pitchSlider_, fineTuneSlider_, stretchSlider_, formantSlider_;
        juce::ComboBox   modeBox_;
        juce::TextButton preserveFormantBtn_;

        juce::Label voiceTransformLbl_, voicePresetLbl_, ultraDemonLbl_;
        juce::ComboBox voicePresetBox_;
        juce::Slider ultraDemonSlider_;
        juce::TextButton reactiveBtn_;

        juce::TextButton muteBtn_;
        bool ignoreCallbacks_ = false;
        bool autoSwitchOutOfTapeMode_ = true;
        bool automationMode_ = false;
        DAW::TrackID automationTrackId_;
        juce::String automationExplicitClipId_;
        ClipAutomationPanelCallbacks automationCallbacks_;
        std::unique_ptr<ClipAutomationPanel> automationPanel_;
    };

    // -----------------------------------------------------------------------
    // Floating window wrapper
    // -----------------------------------------------------------------------
    class ClipPropertiesWindowCore : public juce::DocumentWindow
    {
    public:
        std::function<void(ArrangementClipModel&)> onModelChanged;
        std::function<void(ArrangementClipModel&)> onProcessedWaveformInvalidated;

        ClipPropertiesWindowCore()
            : juce::DocumentWindow("Clip Properties",
                                   juce::Colour(0xFF1E1E1E),
                                   juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(false);
            setResizable(true, false);
            panel_ = std::make_unique<ClipPropertiesPanelCore>();

            panel_->onModelChanged = [this](ArrangementClipModel& m)
            {
                if (onModelChanged) onModelChanged(m);
            };

            panel_->onProcessedWaveformInvalidated = [this](ArrangementClipModel& m)
            {
                if (onProcessedWaveformInvalidated) onProcessedWaveformInvalidated(m);
            };

            setContentNonOwned(panel_.get(), true);
            centreWithSize(420, 470);
            setVisible(false);
            addToDesktop();
        }

        void openForClip(ArrangementClipModel* clip)
        {
            panel_->setClip(clip);
            currentClipId_ = clip != nullptr ? clip->id : juce::Uuid();
            setName("Clip Properties: " + juce::String(clip ? clip->clipName : ""));
            setSize(420, panel_->getHeight() > 0 ? panel_->getHeight() + 20 : 470);
            setVisible(true);
            toFront(true);
        }

        void closeIfShowingClip(const juce::Uuid& clipId)
        {
            if (currentClipId_ != clipId)
                return;

            currentClipId_ = juce::Uuid();
            panel_->setClip(nullptr);
            setVisible(false);
        }

        void activateAutomationMode(ArrangementClipModel* clip,
                                    const DAW::TrackID& trackId,
                                    const ClipAutomationPanelCallbacks& callbacks,
                                    const juce::String& explicitClipId = {})
        {
            openForClip(clip);
            panel_->activateAutomationMode(trackId, callbacks, explicitClipId);
            toFront(true);
        }

        void deactivateAutomationMode()
        {
            panel_->deactivateAutomationMode();
            setSize(420, 470);
        }

        void closeButtonPressed() override
        {
            currentClipId_ = juce::Uuid();
            panel_->setClip(nullptr);
            setVisible(false);
        }

    private:
        std::unique_ptr<ClipPropertiesPanelCore> panel_;
        juce::Uuid currentClipId_;
    };

} // namespace ArrangementEditor

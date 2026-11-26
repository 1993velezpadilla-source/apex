#pragma once
// APEX native dark editor; included at the END of ApexTuneLiveProcessor.h.
// Uses hosted JUCE parameter gestures, preserving APEX automation Write/Read.
// No timers, component mutations or heap work on the audio callback.
#include "../TuneLiveCore/ApexTuneLivePresets.h"
#include "ApexTuneLiveVisualTheme.h"
#include <array>
#include <cmath>

namespace APEX { namespace TuneLive {

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& processor)
      : juce::AudioProcessorEditor(processor), processor_(processor)
    {
        setOpaque(true);
        setLookAndFeel(&studioLook_);
        setResizable(true, true);
#if JUCE_ANDROID
        setResizeLimits(280, 360, 1600, 1600);
        setSize(360, processor_.usesSpectralTracking() ? 390 : 610);
#else
        // Controls/latency readout occupy rows through y=516; a 480px
        // editor cut off the last row. Enforce a true touch-safe minimum.
        setResizeLimits(960, 600, 1600, 1000);
        setSize(1200, 750);
#endif

        constexpr std::array<const char*,7> names{{
            "CORRECTION", "RETUNE SPEED", "HUMANIZE", "VIBRATO", "FLEX TUNE", "STABILITY", "WET"
        }};
        for (std::size_t i=0; i<knobs_.size(); ++i) {
            auto& knob=knobs_[i];
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
#if JUCE_ANDROID
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,60,20);
#else
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,88,27);
#endif
            knob.setRange(0.0,1.0,0.001);
            // User-facing engineering units; normalized parameter values
            // remain stable for existing DAW host automation and state.
            knob.textFromValueFunction=[i](double normalized) {
                if(i==1)
                    return juce::String(1.0+149.0*normalized,1)+" ms";
                return juce::String(juce::roundToInt(normalized*100.0))+" %";
            };
            knob.valueFromTextFunction=[i](const juce::String& value) {
                const double parsed=value.getDoubleValue();
                return juce::jlimit(0.0,1.0,i==1?(parsed-1.0)/149.0:parsed/100.0);
            };
            knob.setColour(juce::Slider::rotarySliderFillColourId,
                           juce::Colour::fromRGB(238,173,107));
            knob.setColour(juce::Slider::rotarySliderOutlineColourId,
                           juce::Colour::fromRGB(58,54,67));
            knob.setColour(juce::Slider::thumbColourId,
                           juce::Colour::fromRGB(250,211,142));
            knob.setColour(juce::Slider::textBoxTextColourId,juce::Colours::white);
            knob.setColour(juce::Slider::textBoxOutlineColourId,
                           Visual::cyan().withAlpha(0.23f));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,
                           Visual::midnight());
            knob.setValue(parameter(kKnobParameters[i]).getValue(), juce::dontSendNotification);
            if (kKnobParameters[i] == Processor::kVibratoPreserve)
                knob.setTooltip("Preserve sustained vibrato while the note center is corrected.");
            addAndMakeVisible(knob);

            auto& title=knobLabels_[i];
            title.setText(names[i],juce::dontSendNotification);
            title.setJustificationType(juce::Justification::centred);
            title.setColour(juce::Label::textColourId,
                            juce::Colour::fromRGB(224,214,228));
            title.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
            addAndMakeVisible(title);
            if (processor_.usesSpectralTracking()) {
                const bool visible = i==1 || i==2 || i==4;
                knob.setVisible(visible);
                title.setVisible(visible);
                if(i==1)knob.setTooltip("How quickly pitch moves toward the selected note (milliseconds).");
                if(i==2)knob.setTooltip("Keep more expression in sustained notes.");
                if(i==4)knob.setTooltip("Allow small natural pitch deviations around the note.");
            }

            auto* bound=&parameter(kKnobParameters[i]);
            knob.onDragStart=[bound](){bound->beginChangeGesture();};
            knob.onValueChange=[bound,&knob](){
                bound->setValueNotifyingHost(static_cast<float>(knob.getValue()));
            };
            knob.onDragEnd=[bound](){bound->endChangeGesture();};
        }

        // Both enharmonic spellings are shown explicitly (e.g. Ab and G#).
        // They intentionally map to the SAME existing 12 semitone values:
        // zero new parameter IDs, no automation or serialized-state change.
        for(std::size_t k=0;k<keyOptions_.size();++k)
            keys_.addItem(keyOptions_[k].name,static_cast<int>(k)+1);
        keys_.setSelectedId(1, juce::dontSendNotification);
        reference_.setSliderStyle(juce::Slider::LinearHorizontal);
        reference_.setRange(0.0,1.0,0.001);
        reference_.setTextBoxStyle(juce::Slider::TextBoxRight,false,75,24);
        reference_.textFromValueFunction=[](double normalized){
            return juce::String(430.0+normalized*20.0,1)+" Hz";
        };
        reference_.valueFromTextFunction=[](const juce::String& str){
            return juce::jlimit(0.0,1.0,(str.getDoubleValue()-430.0)/20.0);
        };
        reference_.setColour(juce::Slider::trackColourId,Visual::cyan());
        reference_.setColour(juce::Slider::thumbColourId,juce::Colour::fromRGB(255,223,161));
        reference_.setColour(juce::Slider::textBoxTextColourId,juce::Colours::white);
        reference_.setColour(juce::Slider::textBoxBackgroundColourId,Visual::midnight());
        reference_.setColour(juce::Slider::textBoxOutlineColourId,Visual::cyan().withAlpha(0.34f));
        reference_.setValue(parameter(Processor::kA4Reference).getValue(),juce::dontSendNotification);
        reference_.onDragStart=[this](){parameter(Processor::kA4Reference).beginChangeGesture();};
        reference_.onValueChange=[this](){
            parameter(Processor::kA4Reference).setValueNotifyingHost(static_cast<float>(reference_.getValue()));
        };
        reference_.onDragEnd=[this](){parameter(Processor::kA4Reference).endChangeGesture();};
        addAndMakeVisible(reference_);
        // Keep legacy concert-pitch automation/state intact; the standard
        // editor needs no A4 slider (factory default 440 Hz).
        reference_.setVisible(!processor_.usesSpectralTracking());
        keys_.onChange=[this](){
            const int option=keys_.getSelectedId()-1;
            if(option>=0 && option<static_cast<int>(keyOptions_.size()))
                setDiscrete(Processor::kKey,keyOptions_[static_cast<std::size_t>(option)].semitone,11);
        };
        addAndMakeVisible(keys_);

        for(int i=0;i<3;++i)
            scales_.addItem(juce::StringArray{"Chromatic","Major","Minor"}[i],i+1);
        scales_.setSelectedId(1,juce::dontSendNotification);
        scales_.onChange=[this](){setDiscrete(Processor::kScale,scales_.getSelectedId()-1,2);};
        addAndMakeVisible(scales_);

        constexpr std::array<const char*,4> singerLabels{{
            "Automatic", "Baritone / Bass", "Alto / Tenor", "Soprano"
        }};
        for (std::size_t i=0; i<kVocalRanges.size(); ++i)
            ranges_.addItem(processor_.usesSpectralTracking()?singerLabels[i]:kVocalRanges[i].label,
                            static_cast<int>(i)+1);
        ranges_.setTooltip("Automatic covers all vocal registers. Choose Baritone / Bass for low notes, Alto / Tenor for mid-range, or Soprano for high notes. This does not transpose your voice.");
        ranges_.setSelectedId(1,juce::dontSendNotification);
        ranges_.onChange=[this](){
            setDiscrete(Processor::kVocalRange,ranges_.getSelectedId()-1,static_cast<int>(kVocalRanges.size())-1);
        };
        addAndMakeVisible(ranges_);

        formants_.setButtonText("PRESERVE FORMANTS");
        formants_.setColour(juce::ToggleButton::textColourId,juce::Colours::white);
        formants_.setToggleState(true,juce::dontSendNotification);
        formants_.onClick=[this](){setParameter(Processor::kPreserveFormants,
                             formants_.getToggleState()?1.0f:0.0f);};
        addAndMakeVisible(formants_);

        bypass_.setButtonText("BYPASS");
        bypass_.setColour(juce::ToggleButton::textColourId,juce::Colours::white);
        bypass_.onClick=[this](){setParameter(Processor::kBypass,
                           bypass_.getToggleState()?1.0f:0.0f);};
        addAndMakeVisible(bypass_);

        if (processor_.isTracking() && !processor_.usesSpectralTracking()) {
            experimentalDeclick_.setButtonText("ANTI-CLICK A/B (EXPERIMENTAL)");
            experimentalDeclick_.setTooltip(
                "Compare original Tracking (Off) against the phase-declick research variant (On). "
                "For clean A/B, stop playback before changing this setting.");
            experimentalDeclick_.setColour(juce::ToggleButton::textColourId,
                                          juce::Colour::fromRGB(255, 211, 142));
            experimentalDeclick_.onClick=[this](){
                setParameter(Processor::kExperimentalDeclick,
                             experimentalDeclick_.getToggleState()?1.0f:0.0f);
            };
            addAndMakeVisible(experimentalDeclick_);
        }

        if (!processor_.usesSpectralTracking()) for(int i=0;i<3;++i) {
            auto& button=styles_[static_cast<std::size_t>(i)];
            button.setButtonText(kVocalStyles[static_cast<std::size_t>(i)].name);
            button.setColour(juce::TextButton::buttonColourId,
                             juce::Colour::fromRGB(56,48,67));
            button.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colour::fromRGB(144,91,60));
            button.setColour(juce::TextButton::textColourOffId,juce::Colours::white);
            // No fictional active preset on open: the timer recognises the
            // profile only when ALL hosted values match its saved recipe.
            button.setToggleState(false,juce::dontSendNotification);
            button.onClick=[this,i](){
                for(int j=0;j<3;++j)
                    styles_[static_cast<std::size_t>(j)].setToggleState(j==i,juce::dontSendNotification);
                applyStyle(static_cast<VocalStyle>(i));
            };
            addAndMakeVisible(button);
        }
        for(auto* label : {&pitchLabel_, &pitchCaption_, &latencyLabel_}) {
            label->setJustificationType(juce::Justification::centredLeft);
            label->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*label);
        }
        pitchLabel_.setFont(juce::Font(juce::FontOptions(72.0f).withStyle("Bold")));
#if JUCE_ANDROID
        pitchLabel_.setFont(juce::Font(juce::FontOptions(34.0f).withStyle("Bold")));
#endif
        pitchLabel_.setJustificationType(juce::Justification::centred);
        pitchLabel_.setColour(juce::Label::textColourId,
                              juce::Colour::fromRGB(250,205,151));
        pitchCaption_.setFont(juce::Font(juce::FontOptions(12.0f)));
        pitchCaption_.setJustificationType(juce::Justification::centred);
        latencyLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
        startTimerHz(25); // GUI-only controls and pitch meter.
        timerCallback();
    }

    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    // All coordinates are expressed in the approved 1200x750 artboard.
    // Child controls are repositioned with the same transform in resized().
    // Every element is real JUCE vector paint, not a flattened screenshot.
    void paint(juce::Graphics& g) override {
        if (processor_.usesSpectralTracking()) {
            g.fillAll(Visual::midnight());
            const auto w=getWidth();
            g.setColour(Visual::ivory());
            g.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
            g.drawText("APEX TUNE LIVE",12,7,w-24,27,juce::Justification::centredLeft);
            const auto h=getHeight();
            const int half=w/2;
            const int top=std::max(74,h*20/100);
            g.setColour(Visual::muted());
            g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
            g.drawText("KEY",12,top,half-20,16,juce::Justification::centredLeft);
            g.drawText("SCALE",half+5,top,half-16,16,juce::Justification::centredLeft);
            g.drawText("VOCAL RANGE",12,top+58,w-24,16,juce::Justification::centredLeft);
            return;
        }
#if JUCE_ANDROID
        g.fillAll(Visual::midnight());
        g.setColour(Visual::ivory());
        g.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
        g.drawText(processor_.isTracking() ? "APEX TUNE LIVE TRACKING" : "APEX TUNE LIVE",
                   10, 5, getWidth() - 20, 26, juce::Justification::centredLeft);
        g.setColour(Visual::muted());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("KEY", 12, 94, getWidth() / 2 - 16, 16, juce::Justification::centredLeft);
        g.drawText("SCALE", getWidth() / 2 + 4, 94, getWidth() / 2 - 16, 16, juce::Justification::centredLeft);
        g.drawText("VOCAL RANGE", 12, 154, getWidth() / 2 - 16, 16, juce::Justification::centredLeft);
        g.drawText("REFERENCE", getWidth() / 2 + 4, 154, getWidth() / 2 - 16, 16, juce::Justification::centredLeft);
        return;
#endif
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::scale(getWidth()/1200.0f,getHeight()/750.0f));
        g.setColour(Visual::midnight());
        g.fillRect(juce::Rectangle<float>(0.0f,0.0f,1200.0f,750.0f));

        Visual::panel(g,{13.0f,11.0f,1174.0f,86.0f},14.0f);
        // Original APEX warm metallic identity, laid out like the reference.
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(255,207,142),
                           32.0f,26.0f,juce::Colour::fromRGB(227,139,75),178.0f,74.0f,false));
        g.setFont(juce::Font(juce::FontOptions(35.0f).withStyle("Bold")));
        g.drawText("A P E X",34,23,214,41,juce::Justification::centredLeft);
        g.setColour(Visual::muted().withAlpha(0.7f));
        g.fillRect(239.0f,30.0f,1.0f,37.0f);
        g.setColour(Visual::ivory());
        g.setFont(juce::Font(juce::FontOptions(35.0f)));
        g.drawText(processor_.isTracking() ? "Tune Live Tracking" : "Tune Live",
                   254,23,350,43,juce::Justification::centredLeft);
        g.setColour(Visual::muted());
        g.setFont(juce::Font(juce::FontOptions(9.4f).withStyle("Bold")));
        g.drawText(processor_.usesSpectralTracking() ? "SPECTRAL CORRECTION"
                   : (processor_.isTracking() ? "RECORDING & MONITORING" : "STUDIO CORRECTION"),
                   37,71,360,16,juce::Justification::centredLeft);
        // Decorative amplitude-free arc: NOT a fabricated live spectrum.
        for(int i=0;i<4;++i) {
            juce::Path wave;
            const float offset=static_cast<float>(i)*4.0f;
            for(int x=610;x<=1095;x+=5) {
                const float t=(x-610)/485.0f;
                const float y=55.0f+22.0f*std::sin(t*9.0f+offset*0.10f)
                       *std::sin(t*3.14159265f)+offset*0.40f;
                if(x==610)wave.startNewSubPath(static_cast<float>(x),y);
                else wave.lineTo(static_cast<float>(x),y);
            }
            g.setColour((i%2?Visual::gold():Visual::cyan()).withAlpha(0.32f));
            g.strokePath(wave,juce::PathStrokeType(1.15f));
        }
        g.setColour(Visual::gold());
        g.setFont(juce::Font(juce::FontOptions(39.0f).withStyle("Bold")));
        g.drawText("A",1125,23,50,47,juce::Justification::centred);

        Visual::panel(g,{16.0f,104.0f,1168.0f,210.0f});
        // APEX lighting uses real radial gradients under the hosted controls.
        // The spectral columns in idle mode remain explicitly decorative;
        // only frequencyHz/cents come from the live pitch tracker.
        g.setGradientFill(juce::ColourGradient(Visual::gold().withAlpha(0.12f),
                           400.0f,181.0f,Visual::gold().withAlpha(0.0f),
                           400.0f,279.0f,true));
        g.fillEllipse(240.0f,107.0f,320.0f,204.0f);
        // Reference concept's characteristic low cyan illumination. Applied
        // beneath live note/controls, so contrast of the text remains safe.
        g.setGradientFill(juce::ColourGradient(Visual::cyan().withAlpha(0.15f),
                           550.0f,313.0f,Visual::cyan().withAlpha(0.0f),
                           550.0f,215.0f,false));
        g.fillRoundedRectangle(juce::Rectangle<float>(16.0f,104.0f,1168.0f,210.0f),18.0f);
        Visual::sectionTitle(g,"T R A C K E D   P I T C H",35.0f,116.0f);
        // An illuminated glass well behind the actual note label, with a
        // softer neutral standby appearance when the tracker has no F0.
        const bool hasVoice=std::isfinite(trackedHz_) && trackedHz_>=55.0f;
        const auto noteGlow=hasVoice?Visual::gold():Visual::cyan();
        g.setGradientFill(juce::ColourGradient(noteGlow.withAlpha(hasVoice?0.24f:0.10f),
             407.0f,186.0f,noteGlow.withAlpha(0.0f),407.0f,285.0f,true));
        g.fillEllipse(220.0f,119.0f,370.0f,181.0f);
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawRoundedRectangle(juce::Rectangle<float>(228.0f,144.0f,361.0f,94.0f),
                               16.0f,1.0f);
        // Layered vector aurora is a purely decorative brand motif; the
        // numbered +/- 100-cent scale and live pointer are real values.
        for(int lane=0;lane<5;++lane) {
            juce::Path aurora;
            for(int px=66;px<=752;px+=7) {
                const float t=(px-66)/686.0f;
                const float slope=static_cast<float>(lane)*3.5f;
                const float yy=216.0f-slope-12.0f*std::sin(12.6f*t+lane*0.21f)
                                     *std::sin(t*3.14159265f);
                if(px==66)aurora.startNewSubPath(static_cast<float>(px),yy);
                else aurora.lineTo(static_cast<float>(px),yy);
            }
            g.setColour(Visual::cyan().withAlpha(0.035f+lane*0.008f));
            g.strokePath(aurora,juce::PathStrokeType(1.0f));
        }
        // Pitch grid and cents meter are real calibrated MIDI-cents readouts.
        constexpr float gridL=58.0f,gridR=754.0f,gridY=243.0f;
        for(int n=0;n<=20;++n) {
            const float x=gridL+(gridR-gridL)*n/20.0f;
            g.setColour(Visual::cyan().withAlpha(n%5==0?0.18f:0.08f));
            g.drawVerticalLine(static_cast<int>(x),165.0f,258.0f);
        }
        for(int n=0;n<77;++n) {
            const float x=gridL+9.0f*n;
            const float envelope=std::exp(-std::pow((n-39.0f)/30.0f,2.0f));
            const float wave=std::abs(std::sin(n*0.34f)*std::cos(n*0.19f));
            const float h=5.0f+42.0f*envelope*(0.28f+wave);
            const auto shade=n<26?Visual::gold():n<59?Visual::cyan():Visual::gold();
            // In standby this is intentionally decorative, not an analyzer.
            const float alpha=trackedHz_>=55.0f?0.72f:0.45f;
            g.setGradientFill(juce::ColourGradient(shade.withAlpha(alpha),
                    x,gridY,shade.withAlpha(alpha*0.12f),
                    x,gridY-h,false));
            g.fillRoundedRectangle(x,gridY-h,4.4f,h,1.1f);
        }
        g.setColour(Visual::cyan().withAlpha(0.52f));
        g.drawHorizontalLine(static_cast<int>(gridY),gridL,gridR);
        // Calibrated 0-cent centre line and endpoint ticks.
        g.setColour(Visual::gold().withAlpha(0.51f));
        g.fillRoundedRectangle((gridL+gridR)*0.5f-0.7f,185.0f,
                               1.4f,59.0f,0.7f);
        for(int n=0;n<=20;++n) {
            const float x=gridL+(gridR-gridL)*n/20.0f;
            g.setColour(Visual::muted().withAlpha(n%5==0?0.65f:0.23f));
            g.fillRect(x,gridY+3.0f,1.0f,n%5==0?10.0f:5.0f);
        }
        for(int n=0;n<5;++n) {
            const int cents=(n-2)*50;
            const float x=gridL+(gridR-gridL)*n/4.0f;
            g.setColour(Visual::muted());
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText((cents>0?"+":"")+juce::String(cents),static_cast<int>(x)-24,
                       263,48,16,juce::Justification::centred);
        }
        const float indicator=juce::jlimit(-100.0f,100.0f,trackedCents_);
        const float markerX=gridL+(indicator+100.0f)*(gridR-gridL)/200.0f;
        if(hasVoice) {
            g.setColour(Visual::gold().withAlpha(0.15f));
            g.fillRoundedRectangle(markerX-4.0f,184.0f,8.0f,60.0f,3.0f);
            g.setColour(Visual::gold());
            g.fillRoundedRectangle(markerX-1.0f,194.0f,2.0f,50.0f,1.0f);
            juce::Path point;
            point.addTriangle(markerX-7.0f,187.0f,markerX+7.0f,187.0f,
                              markerX,196.0f);
            g.setColour(Visual::gold());
            g.fillPath(point);
        } else {
            g.setColour(Visual::cyan().withAlpha(0.36f));
            g.fillRect((gridL+gridR)*0.5f,220.0f,1.0f,24.0f);
        }
        g.setColour(Visual::cyan().withAlpha(0.35f));
        g.fillRoundedRectangle(268.0f,285.0f,278.0f,21.0f,8.0f);
        g.setColour(Visual::midnight());
        g.fillRoundedRectangle(269.0f,286.0f,276.0f,19.0f,8.0f);
        // Explicit label avoids passing decorative spectrum lines off as
        // a live signal analyser; only the needle/note/cents are telemetry.
        g.setColour(Visual::muted().withAlpha(0.88f));
        g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
        g.drawText("CENTS DEVIATION",58,285,176,20,
                   juce::Justification::centredLeft);
        g.setColour(hasVoice?Visual::gold():Visual::cyan());
        g.fillEllipse(584.0f,290.0f,8.0f,8.0f);
        g.setColour(Visual::muted());
        g.drawText(hasVoice?"VOICE DETECTED":"AWAITING VOICE",602,284,156,21,
                   juce::Justification::centredLeft);

        g.setColour(Visual::cyan().withAlpha(0.25f));
        g.fillRect(770.0f,126.0f,1.0f,166.0f);
        g.setColour(Visual::muted());
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        g.drawText("K E Y",800,130,130,20,juce::Justification::centredLeft);
        g.drawText("S C A L E",974,130,150,20,juce::Justification::centredLeft);
        g.drawText("V O C A L   R A N G E",800,212,163,18,juce::Justification::centredLeft);
        g.drawText("C O N C E R T   A 4",974,212,177,18,juce::Justification::centredLeft);

        Visual::panel(g,{16.0f,323.0f,1168.0f,258.0f});
        Visual::sectionTitle(g,"M U S I C A L   C O N T R O L",35.0f,335.0f);
        for (std::size_t k=0; k<knobs_.size(); ++k) {
            const float x=static_cast<float>(knobX(k));
            const juce::Rectangle<float> well(x,390.0f,145.0f,181.0f);
            g.setGradientFill(juce::ColourGradient(Visual::surface().brighter(0.32f),
                 well.getTopLeft(),Visual::midnight(),well.getBottomRight(),false));
            g.fillRoundedRectangle(well,12.0f);
            g.setColour(Visual::cyan().withAlpha(0.13f));
            g.drawRoundedRectangle(well.reduced(0.75f),11.5f,1.0f);
            g.setGradientFill(juce::ColourGradient(Visual::gold().withAlpha(0.075f),
                 x+72.0f,420.0f,Visual::gold().withAlpha(0.0f),
                 x+72.0f,538.0f,false));
            g.fillEllipse(x+12.0f,398.0f,120.0f,128.0f);
        }

        Visual::panel(g,{16.0f,590.0f,1168.0f,146.0f});
        Visual::sectionTitle(g,"V O C A L   S T Y L E",35.0f,601.0f);
        g.setColour(Visual::cyan().withAlpha(0.35f));
        g.fillRect(749.0f,621.0f,1.0f,98.0f);
        g.setColour(Visual::cyan().withAlpha(0.22f));
        g.fillRect(777.0f,686.0f,374.0f,1.0f);
        // Move the highlighted glow with the actual matching preset.
        // After a host automation edit the profile becomes CUSTOM, and
        // none of the three preset buttons falsely stays highlighted.
        g.setColour(selectedStyle_>=0?Visual::gold():Visual::cyan());
        g.setFont(juce::Font(juce::FontOptions(10.5f).withStyle("Bold")));
        g.drawText(selectedStyle_>=0?"PROFILE ACTIVE":"CUSTOM PARAMETERS",
                   530,600,199,22,juce::Justification::centredRight);
        for(int i=0;i<3;++i) {
            const float x=49.0f+static_cast<float>(i)*228.0f;
            const bool active=(i==selectedStyle_);
            g.setGradientFill(juce::ColourGradient(
                (active?Visual::gold():Visual::cyan()).withAlpha(active?0.21f:0.045f),
                x+105.0f,685.0f,Visual::midnight().withAlpha(0.0f),
                x+105.0f,617.0f,true));
            g.fillRoundedRectangle(juce::Rectangle<float>(x,626.0f,211.0f,76.0f),
                                   11.0f);
            if(active) {
                g.setColour(Visual::gold().withAlpha(0.92f));
                g.fillRoundedRectangle(x+33.0f,633.0f,145.0f,2.0f,1.0f);
            }
        }
        // No clipped footer underneath the lower panel; the real latency
        // is explicitly visible as plugin PDC inside the options area.
    }

    void resized() override {
        if (processor_.usesSpectralTracking()) {
            const int w=getWidth(),h=getHeight(),half=w/2;
            const int top=std::max(74,h*20/100);
            pitchLabel_.setBounds(12,34,half-18,std::max(36,top-38));
            pitchCaption_.setBounds(half,34,half-12,std::max(36,top-38));
            keys_.setBounds(12,top+17,half-18,37);
            scales_.setBounds(half+5,top+17,half-17,37);
            ranges_.setBounds(12,top+75,w-24,37);
            constexpr int indices[]{1,2,4};
            const int cw=(w-24)/3, y=top+119;
            const int kh=std::max(60,h-y-68);
            for(int j=0;j<3;++j) {
                const int i=indices[j],x=12+j*cw;
                knobLabels_[i].setBounds(x,y,cw,20);
                knobs_[i].setBounds(x,y+20,cw,kh-20);
            }
            const int footer=y+kh;
            formants_.setBounds(12,footer+3,half+24,32);
            bypass_.setBounds(half+36,footer+3,half-48,32);
            latencyLabel_.setBounds(12,footer+36,w-24,25);
            return;
        }
#if JUCE_ANDROID
        const int width = getWidth();
        const int half = width / 2;
        pitchLabel_.setBounds(10, 30, half - 20, 53);
        pitchCaption_.setBounds(half, 42, half - 10, 24);
        keys_.setBounds(10, 110, half - 18, 38);
        scales_.setBounds(half + 2, 110, half - 12, 38);
        ranges_.setBounds(10, 170, half - 18, 38);
        reference_.setBounds(half + 2, 170, half - 12, 38);
        const int cellWidth = (width - 20) / 4;
        for (std::size_t i = 0; i < knobs_.size(); ++i) {
            const int x = 10 + static_cast<int>(i % 4) * cellWidth;
            const int y = 218 + static_cast<int>(i / 4) * 100;
            knobLabels_[i].setBounds(x, y, cellWidth, 20);
            knobs_[i].setBounds(x, y + 20, cellWidth, 77);
        }
        for (int i = 0; i < 3; ++i)
            styles_[static_cast<std::size_t>(i)].setBounds(10 + i * (width - 20) / 3,
                                                         424, (width - 26) / 3, 38);
        formants_.setBounds(10, 472, half + 20, 38);
        bypass_.setBounds(half + 26, 472, half - 36, 38);
        latencyLabel_.setBounds(10, 520, width - 20, 26);
        if (processor_.isTracking())
            experimentalDeclick_.setBounds(10, 550, width - 20, 40);
        return;
#endif
        const float sx=getWidth()/1200.0f,sy=getHeight()/750.0f;
        const auto box=[sx,sy](int x,int y,int w,int h){
            return juce::Rectangle<int>(juce::roundToInt(x*sx),juce::roundToInt(y*sy),
                                         std::max(1,juce::roundToInt(w*sx)),
                                         std::max(1,juce::roundToInt(h*sy)));
        };
        pitchLabel_.setBounds(box(220,138,374,102));
        pitchCaption_.setBounds(box(268,284,278,23));
        keys_.setBounds(box(797,159,163,41));
        scales_.setBounds(box(973,159,177,41));
        ranges_.setBounds(box(797,231,163,41));
        reference_.setBounds(box(973,231,177,41));
        for(std::size_t i=0;i<knobs_.size();++i) {
            knobLabels_[i].setBounds(box(knobX(i),365,145,23));
            knobs_[i].setBounds(box(knobX(i)+6,392,133,169));
        }
        for(int i=0;i<3;++i)
            styles_[static_cast<std::size_t>(i)].setBounds(box(49+i*228,640,211,57));
        formants_.setBounds(box(788,640,246,40));
        bypass_.setBounds(box(1037,640,137,40));
        latencyLabel_.setBounds(box(792,688,360,26));
        if (processor_.isTracking())
            experimentalDeclick_.setBounds(box(792,715,370,26));
    }

private:
    static int knobX(std::size_t i) noexcept { return 38 + 163*static_cast<int>(i); }

    juce::AudioProcessorParameter& parameter(int index) const {
        return *processor_.getParameters()[index];
    }
    void setParameter(int index,float normalised) {
        auto& p=parameter(index);
        p.beginChangeGesture();
        p.setValueNotifyingHost(juce::jlimit(0.0f,1.0f,normalised));
        p.endChangeGesture();
    }
    void setDiscrete(int index,int value,int max) {
        if(value < 0 || value > max)return;
        setParameter(index,static_cast<float>(value)/static_cast<float>(max));
    }
    void applyStyle(VocalStyle style) {
        const auto& profile=preset(style);
        setParameter(Processor::kAmount,processor_.usesSpectralTracking()?1.0f:profile.amount);
        setParameter(Processor::kSpeed,profile.retune);
        setParameter(Processor::kHumanize,profile.humanize);
        setParameter(Processor::kVibratoPreserve,styleVibrato(style));
        setParameter(Processor::kFlexTune,profile.flexTune);
        setParameter(Processor::kNoteStability,processor_.usesSpectralTracking()?0.24f:profile.stability);
        setParameter(Processor::kMix,profile.mix);
        setParameter(Processor::kPreserveFormants,profile.preserveFormants?1.0f:0.0f);
        // Key + scale remain under the artist's control when switching styles.
        timerCallback();
    }

    float styleVibrato(VocalStyle style) const noexcept {
        constexpr std::array<float,3> vibrato{1.0f,0.70f,0.0f};
        return processor_.isTracking() && !processor_.usesSpectralTracking() ? vibrato[static_cast<std::size_t>(style)] : 0.0f;
    }

    void timerCallback() override {
        // A style is a collection of REAL automation-safe parameter values,
        // not a separate hidden preset flag. Host recall or manual tweaking
        // can therefore never display a misleading selected profile.
        const auto close=[this](int id,float expected) {
            return std::abs(parameter(id).getValue()-expected)<0.0015f;
        };
        int matchingStyle=-1;
        if (!processor_.usesSpectralTracking()) for(int i=0;i<3;++i) {
            const auto& p=kVocalStyles[static_cast<std::size_t>(i)];
            if((processor_.usesSpectralTracking() || close(Processor::kAmount,p.amount))
                && close(Processor::kSpeed,p.retune)
                && close(Processor::kHumanize,p.humanize)
                && (processor_.usesSpectralTracking() || close(Processor::kVibratoPreserve,styleVibrato(static_cast<VocalStyle>(i))))
                && close(Processor::kFlexTune,p.flexTune)
                && (processor_.usesSpectralTracking() || close(Processor::kNoteStability,p.stability))
                && (processor_.usesSpectralTracking() || close(Processor::kMix,p.mix))
                && close(Processor::kPreserveFormants,p.preserveFormants?1.0f:0.0f)) {
                matchingStyle=i;
                break;
            }
        }
        if(selectedStyle_!=matchingStyle) {
            selectedStyle_=matchingStyle;
            for(int i=0;i<3;++i)
                styles_[static_cast<std::size_t>(i)]
                    .setToggleState(i==selectedStyle_,juce::dontSendNotification);
        }
        for(std::size_t i=0;i<kKnobParameters.size();++i)
            if(!knobs_[i].isMouseButtonDown())
                knobs_[i].setValue(parameter(kKnobParameters[i]).getValue(),juce::dontSendNotification);
        const int activeKey=juce::jlimit(0,11,juce::roundToInt(parameter(Processor::kKey).getValue()*11));
        const int chosenIndex=keys_.getSelectedId()-1;
        // Keep the artist's Ab/Bb spelling if the parameter pitch class is
        // unchanged. Automation/state recall may choose the canonical sharp
        // option because a 12-tone numeric key cannot persist spelling.
        if(chosenIndex<0 || chosenIndex>=static_cast<int>(keyOptions_.size())
           || keyOptions_[static_cast<std::size_t>(chosenIndex)].semitone!=activeKey) {
            for(std::size_t k=0;k<keyOptions_.size();++k)
                if(keyOptions_[k].semitone==activeKey) {
                    keys_.setSelectedId(static_cast<int>(k)+1,juce::dontSendNotification);
                    break;
                }
        }
        scales_.setSelectedId(1+juce::jlimit(0,2,juce::roundToInt(parameter(Processor::kScale).getValue()*2)),
                              juce::dontSendNotification);
        const auto rangeMax=static_cast<int>(kVocalRanges.size())-1;
        ranges_.setSelectedId(1+juce::roundToInt(parameter(Processor::kVocalRange).getValue()*rangeMax),
                              juce::dontSendNotification);
        if(!reference_.isMouseButtonDown())
            reference_.setValue(parameter(Processor::kA4Reference).getValue(),juce::dontSendNotification);
        formants_.setToggleState(parameter(Processor::kPreserveFormants).getValue()>=0.5f,
                                 juce::dontSendNotification);
        bypass_.setToggleState(parameter(Processor::kBypass).getValue()>=0.5f,
                               juce::dontSendNotification);
        if (processor_.isTracking())
            experimentalDeclick_.setToggleState(
                parameter(Processor::kExperimentalDeclick).getValue()>=0.5f,
                juce::dontSendNotification);
        const auto hz=processor_.detectedFrequencyHz();
        trackedHz_=hz;
        trackedCents_=0.0f;
        if(hz>=50.0f && std::isfinite(hz)) {
            const float reference=430.0f+20.0f*parameter(Processor::kA4Reference).getValue();
            const float midi=69.0f+12.0f*std::log2(hz/reference);
            const int note=juce::jlimit(0,127,juce::roundToInt(midi));
            trackedCents_=(midi-static_cast<float>(note))*100.0f;
            // Honor the spelling currently selected by the artist: Bb/Ab
            // modes show Bb/Ab on the tracked display instead of A#/G#.
            // This only affects text, never pitch classes, DSP or state.
            constexpr std::array<const char*,12> sharpNames{{
                "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
            }};
            constexpr std::array<const char*,12> flatNames{{
                "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"
            }};
            const int selected=keys_.getSelectedId();
            const bool useFlats=selected==3 || selected==6 || selected==10
                                 || selected==13 || selected==16;
            const auto& names=useFlats?flatNames:sharpNames;
            pitchLabel_.setText(juce::String(names[static_cast<std::size_t>(note%12)])
                +juce::String(note/12-1),juce::dontSendNotification);
            pitchCaption_.setText(juce::String((midi-note)*100.0f,1)
                +" cents   /   "+juce::String(hz,1)+" Hz"
                +"   /   "+juce::String(processor_.appliedShiftSemitones()*100.0f,1)+" cents shift",
                juce::dontSendNotification);
        } else {
            pitchLabel_.setText("--",juce::dontSendNotification);
            pitchCaption_.setText("Waiting for monophonic voice",juce::dontSendNotification);
        }
        const auto rate=processor_.getSampleRate();
        const int samples=processor_.getLatencyForDisplay();
        const auto latencyText=rate>0.0
            ? juce::String(1000.0*samples/rate,2)+" ms"
            : juce::String(samples)+" samples";
        latencyLabel_.setText("PLUGIN LATENCY   "+latencyText,juce::dontSendNotification);
        repaint(); // GUI thread only: update live pitch pointer and note styling.
    }

    struct KeyOption { const char* name; int semitone; };
    inline static constexpr std::array<KeyOption,17> keyOptions_{{
        {"C",0},{"C#",1},{"Db",1},{"D",2},{"D#",3},{"Eb",3},
        {"E",4},{"F",5},{"F#",6},{"Gb",6},{"G",7},{"G#",8},
        {"Ab",8},{"A",9},{"A#",10},{"Bb",10},{"B",11}
    }};
    Processor& processor_;
    Visual::StudioLookAndFeel studioLook_;
    float trackedHz_=0.0f;
    float trackedCents_=0.0f;
    int selectedStyle_=-1; // GUI-only; inferred from hosted values.
    inline static constexpr std::array<int,7> kKnobParameters{{
        Processor::kAmount, Processor::kSpeed, Processor::kHumanize, Processor::kVibratoPreserve,
        Processor::kFlexTune, Processor::kNoteStability, Processor::kMix
    }};
    std::array<juce::Slider,kKnobParameters.size()> knobs_;
    std::array<juce::Label,kKnobParameters.size()> knobLabels_;
    std::array<juce::TextButton,3> styles_;
    juce::ComboBox keys_,scales_,ranges_;
    juce::Slider reference_;
    juce::ToggleButton formants_,bypass_,experimentalDeclick_;
    juce::Label pitchLabel_,pitchCaption_,latencyLabel_;
};

}} // namespace APEX::TuneLive

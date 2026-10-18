#pragma once
// Original APEX Bubblegum GUI: dark glass + magenta/teal; no Waves assets.
#include <JuceHeader.h>
#include "ApexBubblegumDelayProcessor.h"
#include <array>
#include <cmath>

namespace APEX::Delay
{
class DelayLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override
    {
        const float cx=x+w*0.5f, cy=y+h*0.49f;
        const float r=0.405f*static_cast<float>(juce::jmin(w,h));
        const auto pink=juce::Colour(0xffff55bd);
        const auto cyan=juce::Colour(0xff37f3fa);
        // Subtle shadow, rim light and glossy original vector dial.
        g.setColour(juce::Colour(0x65000000));
        g.fillEllipse(cx-r-2,cy-r+5,2*r+8,2*r+8);
        g.setColour(juce::Colour(0xff18283d));
        g.fillEllipse(cx-r-5,cy-r-5,2*r+10,2*r+10);
        g.setColour(juce::Colour(0xff497394));
        g.drawEllipse(cx-r-4,cy-r-4,2*r+8,2*r+8,1.0f);
        const float angle=start+(end-start)*juce::jlimit(0.0f,1.0f,pos);
        juce::Path arc;
        arc.addCentredArc(cx,cy,r+5,r+5,0,start,angle,true);
        g.setColour(pink.withAlpha(0.16f));
        g.strokePath(arc,juce::PathStrokeType(10.0f));
        g.setColour(pink);
        g.strokePath(arc,juce::PathStrokeType(4.0f,juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff465976),cx-r,cy-r,
            juce::Colour(0xff080f20),cx+r,cy+r,false));
        g.fillEllipse(cx-r,cy-r,2*r,2*r);
        g.setColour(juce::Colour(0xff7793ae));
        g.drawEllipse(cx-r+1,cy-r+1,2*r-2,2*r-2,1.25f);
        g.setColour(pink.withAlpha(0.45f));
        juce::Path rimShine;
        rimShine.addCentredArc(cx,cy,r-3,r-3,0,0.28f,1.95f,true);
        g.strokePath(rimShine,juce::PathStrokeType(1.0f));
        const float vx=std::sin(angle), vy=-std::cos(angle);
        g.setColour(cyan.withAlpha(0.2f));
        g.drawLine(cx+vx*r*0.30f,cy+vy*r*0.30f,
                   cx+vx*r*0.79f,cy+vy*r*0.79f,6.0f);
        g.setColour(juce::Colour(0xffccfaff));
        g.drawLine(cx+vx*r*0.32f,cy+vy*r*0.32f,
                   cx+vx*r*0.76f,cy+vy*r*0.76f,2.8f);
        g.setColour(cyan.withAlpha(0.19f));
        g.fillEllipse(cx-5,cy-5,10,10);
        g.setColour(cyan);
        g.fillEllipse(cx-2.7f,cy-2.7f,5.4f,5.4f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto box=juce::Rectangle<float>(0,0,(float)w,(float)h).reduced(1.0f);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff1a324b),0,0,juce::Colour(0xff071221),0,(float)h,false));
        g.fillRoundedRectangle(box,5.5f);
        g.setColour(juce::Colour(0xff477da6));
        g.drawRoundedRectangle(box,5.5f,1.1f);
        g.setColour(juce::Colour(0xff35e6ef));
        juce::Path arrow;
        const float ax=(float)w-16.0f,ay=(float)h*0.50f;
        arrow.addTriangle(ax-6,ay-3,ax+6,ay-3,ax,ay+4);
        g.fillPath(arrow);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                          bool over, bool down) override
    {
        const auto rect=b.getLocalBounds().toFloat().reduced(1.0f);
        const bool on=b.getToggleState();
        g.setGradientFill(juce::ColourGradient(
            on?juce::Colour(0xff37213e):juce::Colour(0xff14283d),0,0,
            juce::Colour(0xff091424),0,rect.getBottom(),false));
        g.fillRoundedRectangle(rect,6.0f);
        g.setColour(on?juce::Colour(0xfffa65c0):juce::Colour(0xff396b91));
        g.drawRoundedRectangle(rect,6.0f,1.0f);
        auto cb=juce::Rectangle<float>(16.0f,rect.getCentreY()-8.0f,16.0f,16.0f);
        g.setColour(on?juce::Colour(0xffff5cbf):juce::Colour(0xff081728));
        g.fillRoundedRectangle(cb,2.0f);
        g.setColour(on?juce::Colour(0xffff86d5):juce::Colour(0xff5791ae));
        g.drawRoundedRectangle(cb,2.0f,1.3f);
        if(on)
        {
            juce::Path tick;tick.startNewSubPath(cb.getX()+4,cb.getY()+8);
            tick.lineTo(cb.getX()+7,cb.getY()+12);tick.lineTo(cb.getX()+13,cb.getY()+4);
            g.setColour(juce::Colours::white);
            g.strokePath(tick,juce::PathStrokeType(2.0f));
        }
        g.setColour(b.isEnabled()?juce::Colour(0xffe8f0fb):juce::Colour(0xff697a90));
        g.setFont(juce::Font(juce::FontOptions(13.0f,juce::Font::bold)));
        g.drawText(b.getButtonText(),38,0,b.getWidth()-44,b.getHeight(),
                   juce::Justification::centredLeft);
        if(over||down)
        {
            g.setColour(juce::Colour(0x3339f7ff));
            g.drawRoundedRectangle(rect.reduced(2),5.0f,1.0f);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool over, bool down) override
    {
        auto r=b.getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0x24ff4fbd));
        g.fillRoundedRectangle(r.expanded(1),7.0f);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(down?0xff71204c:0xff54203f),0,0,
            juce::Colour(over?0xff462044:0xff21132b),0,r.getBottom(),false));
        g.fillRoundedRectangle(r,7.0f);
        g.setColour(juce::Colour(0xffff62c4));
        g.drawRoundedRectangle(r,7.0f,1.5f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                        bool, bool) override
    {
        g.setColour(juce::Colour(0xfffce9f6));
        g.setFont(juce::Font(juce::FontOptions(15.0f,juce::Font::bold)));
        g.drawFittedText(b.getButtonText(),b.getLocalBounds().reduced(7),
                         juce::Justification::centred,1);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& p):juce::AudioProcessorEditor(&p),processor_(p)
    {
        setOpaque(true);
        setLookAndFeel(&skin_);
        // Text boxes provide permanent exact values and touchscreen entry.
        makeKnob(0,TimeMs,1,3500,375,1," ms");
        makeKnob(1,Feedback,0,200,45,0.1," %");
        makeKnob(2,ModDepth,0,100,0,0.1," %");
        makeKnob(3,ModRate,0.1,12,0.4,0.01," Hz");
        makeKnob(4,HiPass,20,20000,20,1," Hz");
        makeKnob(5,LowPass,20,20000,20000,1," Hz");
        makeKnob(6,Mix,0,100,35,0.1," %");
        makeKnob(7,OutputDb,-18,18,0,0.1," dB");
        makeKnob(8,ManualBpm,30,300,120,0.1," BPM");

        sync_.addItemList({"MS","HOST","BPM"},1);
        sync_.setTooltip("Manual milliseconds / host tempo / manual BPM");
        addAndMakeVisible(sync_);
        sync_.onChange=[this]{setChoice(SyncMode,sync_.getSelectedId()-1);};

        note_.addItemList({"1/32T","1/32","1/16T","1/16","1/8T",
                           "1/8.","1/8","1/4T","1/4","1/4.",
                           "1/2","1 Bar","2 Bars"},1);
        note_.setTooltip("Musical note division for host and manual tempo sync");
        addAndMakeVisible(note_);
        note_.onChange=[this]{setChoice(Note,note_.getSelectedId()-1);};

        colour_.addItemList({"CLEAN","TAPE I","TAPE II","DARK","HOT"},1);
        colour_.setTooltip("Four APEX-original saturation characters");
        addAndMakeVisible(colour_);
        colour_.onChange=[this]{setChoice(Analog,colour_.getSelectedId()-1);};

        makeToggle(0,PingPong,"PING PONG");
        makeToggle(1,LoFi,"LO-FI");
        makeToggle(2,PhaseLeft,"PHASE L");
        makeToggle(3,PhaseRight,"PHASE R");
        makeToggle(4,Bypass,"BYPASS");
        tap_.setButtonText("TAP TEMPO");
        tap_.setTooltip("Tap at least twice to set manual tempo (quarter-note BPM)");
        tap_.onClick=[this]
        {
            const double now=juce::Time::getMillisecondCounterHiRes();
            if(lastTap_>0 && now-lastTap_>=150 && now-lastTap_<=2000)
            {
                const double bpm=juce::jlimit(30.0,300.0,60000.0/(now-lastTap_));
                setChoice(ManualBpm,static_cast<float>(bpm));
                setChoice(Note,8.0f);
                setChoice(SyncMode,2.0f);
                lastTap_=now;
            }
            else lastTap_=now;
        };
        addAndMakeVisible(tap_);
        readout_.setName("ACTIVE CONTROL VALUE");
        readout_.setJustificationType(juce::Justification::centred);
        readout_.setInterceptsMouseClicks(false,false);
        readout_.setColour(juce::Label::textColourId,juce::Colour(0xfffbdaf5));
        readout_.setFont(juce::Font(juce::FontOptions(19.0f,juce::Font::bold)));
        addAndMakeVisible(readout_);
        readout_.setVisible(false);
        setResizable(true,true);
        setResizeLimits(760,400,1560,750);
        setSize(1060,510);
        syncFromParameters();
        startTimerHz(20);
    }
    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        // Draw at the fixed design grid and scale it with the live JUCE
        // controls. No flattened screenshot: every dial remains interactive.
        g.fillAll(juce::Colour(0xff050c19));
        g.addTransform(juce::AffineTransform::scale(
            getWidth()/1060.0f,getHeight()/510.0f));

        const auto pink=juce::Colour(0xffff5abf);
        const auto cyan=juce::Colour(0xff35f1f3);
        const auto ivory=juce::Colour(0xffeaf1fb);
        const auto muted=juce::Colour(0xffa3b5ce);

        // The reference's continuous rounded dark-blue shell and inner card.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff152039),0,0,juce::Colour(0xff050e1c),1050,505,false));
        g.fillRoundedRectangle(12.0f,10.0f,1036.0f,488.0f,20.0f);
        g.setColour(juce::Colour(0xff44749b));
        g.drawRoundedRectangle(12.0f,10.0f,1036.0f,488.0f,20.0f,0.9f);
        g.setColour(juce::Colour(0xff192a43));
        g.drawRoundedRectangle(20.0f,77.0f,1020.0f,413.0f,13.0f,1.0f);

        // APEX neon brand, deliberately anchored to the reference.
        g.setColour(pink.withAlpha(0.24f));
        g.fillRoundedRectangle(25.0f,23.0f,9.0f,55.0f,5.0f);
        g.setColour(pink);
        g.fillRoundedRectangle(27.0f,24.0f,5.0f,52.0f,3.0f);
        g.setColour(pink.withAlpha(0.18f));
        g.setFont(juce::Font(juce::FontOptions(31.0f,juce::Font::bold)));
        g.drawText("APEX",45,22,139,43,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffff7fcf));
        g.drawText("APEX",44,21,139,43,juce::Justification::centredLeft);
        g.setColour(ivory);
        g.setFont(juce::Font(juce::FontOptions(21.0f)));
        g.drawText("BUBBLEGUM DELAY",188,25,385,37,
                   juce::Justification::centredLeft);
        g.setColour(cyan);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("ORIGINAL NATIVE DSP  •  STEREO  •  ZERO PDC",
                   710,27,306,31,juce::Justification::centredRight);

        // Dark central tempo glass; thin cyan edge, no obstruction of the
        // combo boxes, touch regions or popup lists.
        auto panel=juce::Rectangle<float>(314,93,433,171);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff1a324c),panel.getX(),panel.getY(),
            juce::Colour(0xff08162a),panel.getRight(),panel.getBottom(),false));
        g.fillRoundedRectangle(panel,12.0f);
        g.setColour(juce::Colour(0xff3b789f));
        g.drawRoundedRectangle(panel,12.0f,1.4f);
        g.setColour(cyan.withAlpha(0.11f));
        g.drawRoundedRectangle(panel.reduced(2),11.0f,1.0f);
        g.setColour(cyan.withAlpha(0.05f));
        for(int iy=0;iy<6;++iy)
            for(int ix=0;ix<25;++ix)
                g.fillEllipse(339.0f+ix*15.2f,110.0f+iy*9.0f,1.0f,1.0f);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(13.0f,juce::Font::bold)));
        g.drawText("TEMPO / DELAY",335,102,392,22,juce::Justification::centred);

        const auto sync=static_cast<int>(processor_.getParameterAt(SyncMode).getUnits());
        const double ms=processor_.getEffectiveDelayMsForDisplay();
        const juce::String shown=sync==0?juce::String(ms,1)+" ms"
                                              :juce::String(juce::roundToInt(ms))+" ms";
        g.setColour(cyan.withAlpha(0.13f));
        g.setFont(juce::Font(juce::FontOptions(51.0f,juce::Font::bold)));
        g.drawText(shown,337,126,387,55,juce::Justification::centred);
        g.setColour(cyan);
        g.drawText(shown,335,124,387,55,juce::Justification::centred);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        const auto hostBpm=processor_.getHostTempoForDisplay();
        juce::String tempoSub=sync==0?"Free-running millisecond timing"
            :sync==2?"Manual tempo: "+juce::String(
                processor_.getParameterAt(ManualBpm).getUnits(),1)+" BPM"
            :processor_.hasHostTempo()?"Host tempo drives timing • "
                +juce::String(hostBpm,1)+" BPM"
            :"Host sync: awaiting project tempo (120 BPM fallback)";
        g.drawText(tempoSub,338,183,387,18,juce::Justification::centred);

        // Left/right hero knobs.
        g.setColour(pink);
        g.setFont(juce::Font(juce::FontOptions(16.0f,juce::Font::bold)));
        g.drawText("DELAY",75,100,178,25,juce::Justification::centred);
        g.drawText("FEEDBACK",807,100,178,25,juce::Justification::centred);

        // Clearly separate the lower parameter band. Labels are ONLY 6 px
        // above their associated rotary surfaces, not inside the hero row.
        g.setColour(juce::Colour(0xff346080));
        g.drawLine(31.0f,278.0f,1029.0f,278.0f,0.85f);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0x132e5675),28,288,
            juce::Colour(0x00071122),28,424,false));
        g.fillRoundedRectangle(28.0f,284.0f,1004.0f,140.0f,6.0f);
        const char* captions[]={"MOD DEPTH","MOD RATE","HIGH PASS","LOW PASS",
                                 "DRY / WET","OUTPUT","BPM"};
        const int centers[]={99,234,369,503,638,773,957};
        g.setColour(ivory);
        g.setFont(juce::Font(juce::FontOptions(12.2f,juce::Font::bold)));
        for(int i=0;i<7;++i)
            g.drawText(captions[i],centers[i]-66,288,132,22,
                       juce::Justification::centred);
        g.setColour(juce::Colour(0xff3d6b8c));
        g.drawLine(861.0f,294.0f,861.0f,416.0f,1.0f);

        // Touch-safe footer; no text drawn underneath its actual buttons.
        g.setColour(juce::Colour(0xff325c7a));
        g.drawLine(31.0f,425.0f,1029.0f,425.0f,1.0f);
        g.setColour(juce::Colour(0x330d1a30));
        g.fillRoundedRectangle(24.0f,434.0f,1012.0f,55.0f,8.0f);
    }
    void resized() override
    {
        const float sx=getWidth()/1060.0f,sy=getHeight()/510.0f;
        auto r=[sx,sy](int x,int y,int w,int h)
        {
            return juce::Rectangle<int>(juce::roundToInt(x*sx),
                juce::roundToInt(y*sy),juce::roundToInt(w*sx),
                juce::roundToInt(h*sy));
        };
        // Exactly two larger dials; readouts remain inside slider bounds.
        knobs_[0].setBounds(r(92,128,138,138));
        knobs_[1].setBounds(r(829,128,138,138));
        // Layout taken from approved APEX GUI reference, not from a bitmap.
        constexpr int x[]={47,182,317,451,586,721,905};
        for(int i=2;i<9;++i)
            knobs_[i].setBounds(r(x[i-2],307,104,111));

        sync_.setBounds(r(333,210,119,40));
        note_.setBounds(r(465,210,120,40));
        colour_.setBounds(r(598,210,121,40));
        tap_.setBounds(r(31,437,161,48));
        constexpr int tx[]={210,378,538,703,868};
        constexpr int tw[]={157,151,154,155,162};
        for(int i=0;i<5;++i) toggles_[i].setBounds(r(tx[i],437,tw[i],48));
        // A value popup only during interaction; unobstructed default GUI.
        readout_.setBounds(r(360,71,340,26));
    }

private:
    void makeKnob(int slot,int parameter,double low,double high,double initial,
                  double step,const char* unit)
    {
        auto& s=knobs_[slot]; auto& p=processor_.getParameterAt(parameter);
        s.setName(p.getName(128));
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);
        s.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffedf6ff));
        s.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff0a1526));
        s.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff467fac));
        s.setColour(juce::Slider::textBoxHighlightColourId,juce::Colour(0xff315d78));
        s.setRange(low,high,step);
        s.setValue(initial,juce::dontSendNotification);
        s.setTextValueSuffix(unit);
        s.setTooltip(p.getName(128));
        s.onDragStart=[this,slot,parameter]
        {
            // H-Delay-style workflow: while HOST sync is active, the BPM
            // dial displays project BPM. Touching it takes control and
            // switches to manual BPM, never silently edits an inaudible value.
            if(slot==8 && syncModeForKnob_==1)
            {
                setChoice(ManualBpm,static_cast<float>(
                    juce::jlimit(30.0,300.0,processor_.getHostTempoForDisplay())));
                setChoice(SyncMode,2.0f);
            }
            dragging_[slot]=true;
            const int target=(slot==0 && syncModeForKnob_!=0)?Note:parameter;
            dragTarget_[slot]=target;
            processor_.getParameterAt(target).beginChangeGesture();
            showValue(target);
        };
        s.onDragEnd=[this,slot]
        {
            processor_.getParameterAt(dragTarget_[slot]).endChangeGesture();
            dragging_[slot]=false;
            hideReadoutAfter_=juce::Time::getMillisecondCounterHiRes()+1500.0;
        };
        s.onValueChange=[this,slot,parameter]
        {
            if(updating_) return;
            if(slot==8 && processor_.getParameterAt(SyncMode).getUnits()==1.0f)
                setChoice(SyncMode,2.0f);
            // The large delay dial must actually change the audible delay in
            // factory HOST mode: control rhythmic division while synced and
            // exact milliseconds only in MS mode. Gesture ID is stable for
            // the entire drag, even if transport/UI sync changes in flight.
            const int target=dragging_[slot]?dragTarget_[slot]:
                ((slot==0 && syncModeForKnob_!=0)?Note:parameter);
            auto& p=processor_.getParameterAt(target);
            const bool needGesture=!dragging_[slot];
            if(needGesture) p.beginChangeGesture();
            p.setValueNotifyingHost(p.toNormalised(
                static_cast<float>(knobs_[slot].getValue())));
            if(needGesture) p.endChangeGesture();
            showValue(target);
        };
        addAndMakeVisible(s);
    }
    void makeToggle(int slot,int parameter,const char* label)
    {
        auto& b=toggles_[slot];
        b.setButtonText(label);
        b.setTooltip(label);
        b.onClick=[this,slot,parameter]
        {
            if(updating_)return;
            setChoice(parameter,toggles_[slot].getToggleState()?1.0f:0.0f);
        };
        addAndMakeVisible(b);
    }
    void setChoice(int index,float units)
    {
        if(updating_)return;
        auto& p=processor_.getParameterAt(index);
        p.beginChangeGesture();
        p.setValueNotifyingHost(p.toNormalised(units));
        p.endChangeGesture();
        showValue(index);
    }
    static juce::String divisionName(int index)
    {
        static const char* labels[]={
            "1/32T","1/32","1/16T","1/16","1/8T","1/8.",
            "1/8","1/4T","1/4","1/4.","1/2","1 BAR","2 BARS"
        };
        return labels[juce::jlimit(0,12,index)];
    }
    void showValue(int index)
    {
        const auto& p=processor_.getParameterAt(index);
        const auto value=index==Note
            ? divisionName(static_cast<int>(std::lround(p.getUnits())))
            : p.getText(p.getValue(),128);
        readout_.setVisible(true);
        readout_.setText(p.getName(128).toUpperCase()+": "+value,
                         juce::dontSendNotification);
        hideReadoutAfter_=juce::Time::getMillisecondCounterHiRes()+1600.0;
    }
    void syncFromParameters()
    {
        updating_=true;
        const int selectedSync=static_cast<int>(
            processor_.getParameterAt(SyncMode).getUnits());
        // Do not switch slider ranges mid-drag: that would change the
        // automatable target before a JUCE gesture has ended.
        if(!dragging_[0] && selectedSync!=syncModeForKnob_)
        {
            syncModeForKnob_=selectedSync;
            if(selectedSync!=0)
            {
                knobs_[0].setRange(0.0,12.0,1.0);
                knobs_[0].setTextValueSuffix({});
                knobs_[0].setName("SYNC DIVISION");
                knobs_[0].setTooltip("Tempo-synced subdivision: turn the delay dial");
                knobs_[0].textFromValueFunction=[](double value)
                {
                    return divisionName(static_cast<int>(std::lround(value)));
                };
                knobs_[0].valueFromTextFunction=[](const juce::String& value)
                {
                    for(int i=0;i<13;++i)
                        if(value.trim().equalsIgnoreCase(divisionName(i)))
                            return static_cast<double>(i);
                    return value.getDoubleValue();
                };
            }
            else
            {
                knobs_[0].setRange(1.0,3500.0,1.0);
                knobs_[0].setTextValueSuffix(" ms");
                knobs_[0].setName("DELAY (MS)");
                knobs_[0].setTooltip("Delay duration in milliseconds");
                knobs_[0].textFromValueFunction={};
                knobs_[0].valueFromTextFunction={};
            }
        }
        constexpr int which[]={TimeMs,Feedback,ModDepth,ModRate,HiPass,LowPass,
                               Mix,OutputDb,ManualBpm};
        for(int i=0;i<9;++i)
            if(!dragging_[i])
            {
                const double display=(i==8 && selectedSync==1)
                    ?juce::jlimit(30.0,300.0,processor_.getHostTempoForDisplay())
                    :processor_.getParameterAt(
                        i==0 && selectedSync!=0?Note:which[i]).getUnits();
                knobs_[i].setValue(display,juce::dontSendNotification);
            }
        knobs_[8].setTooltip(selectedSync==1
            ?"Project BPM (auto-synced); move to switch to manual BPM"
            :"Manual BPM for custom tempo");
        sync_.setSelectedId(static_cast<int>(processor_.getParameterAt(SyncMode).getUnits())+1,
                            juce::dontSendNotification);
        note_.setSelectedId(static_cast<int>(processor_.getParameterAt(Note).getUnits())+1,
                            juce::dontSendNotification);
        colour_.setSelectedId(static_cast<int>(processor_.getParameterAt(Analog).getUnits())+1,
                              juce::dontSendNotification);
        constexpr int toggleIds[]={PingPong,LoFi,PhaseLeft,PhaseRight,Bypass};
        for(int i=0;i<5;++i)
            toggles_[i].setToggleState(
                processor_.getParameterAt(toggleIds[i]).getUnits()>0.5f,
                juce::dontSendNotification);
        updating_=false;
    }
    void timerCallback() override
    {
        syncFromParameters();
        if(juce::Time::getMillisecondCounterHiRes()>hideReadoutAfter_)
        {
            readout_.setText({},juce::dontSendNotification);
            readout_.setVisible(false);
        }
        repaint();
    }
    Processor& processor_;
    DelayLookAndFeel skin_;
    std::array<juce::Slider,9> knobs_;
    std::array<bool,9> dragging_ {};
    std::array<int,9> dragTarget_ {};
    // -1 forces first construction to select a real source for the knob.
    int syncModeForKnob_=-1;
    std::array<juce::ToggleButton,5> toggles_;
    juce::ComboBox sync_,note_,colour_;
    juce::TextButton tap_;
    juce::Label readout_;
    bool updating_=false;
    double lastTap_=0,hideReadoutAfter_=0;
};
inline juce::AudioProcessorEditor* Processor::createEditor() {return new Editor(*this);}
} // namespace APEX::Delay

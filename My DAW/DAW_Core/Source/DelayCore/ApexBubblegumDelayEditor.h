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
        g.drawArc(cx-r+3,cy-r+3,2*r-6,2*r-6,0.28f,1.95f,1.0f);
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
        setResizable(true,true);
        setResizeLimits(760,400,1560,750);
        setSize(1060,510);
        syncFromParameters();
        startTimerHz(30);
    }
    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101522));
        g.addTransform(juce::AffineTransform::scale(
            getWidth()/1060.0f,getHeight()/510.0f));
        const auto background=juce::Colour(0xff151b2b);
        const auto accent=juce::Colour(0xffff53ae);
        const auto teal=juce::Colour(0xff56eade);
        g.setColour(background);
        g.fillRoundedRectangle(16,12,1028,483,18);
        g.setColour(juce::Colour(0xff303c54));
        g.drawRoundedRectangle(16,12,1028,483,18,1.5f);
        g.setColour(accent);g.fillRoundedRectangle(29,24,7,52,3);
        g.setFont(juce::Font(juce::FontOptions(28.0f,juce::Font::bold)));
        g.drawText("APEX",49,23,100,37,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffe7e8f2));
        g.setFont(juce::Font(juce::FontOptions(21.0f)));
        g.drawText("BUBBLEGUM DELAY",150,26,380,32,juce::Justification::centredLeft);
        g.setColour(teal);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("ORIGINAL NATIVE DSP  •  STEREO  •  ZERO PDC",620,29,394,32,
                   juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff222e40));
        g.fillRoundedRectangle(325,123,403,180,15);
        g.setColour(juce::Colour(0xff364d62));
        g.drawRoundedRectangle(325,123,403,180,15,1.4f);
        g.setColour(juce::Colour(0xffb1bccd));
        g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
        g.drawText("TEMPO / DELAY",341,135,367,18,juce::Justification::centred);
        const auto sync=static_cast<int>(processor_.getParameterAt(SyncMode).getUnits());
        const auto note=static_cast<int>(processor_.getParameterAt(Note).getUnits());
        const auto ms=processor_.getParameterAt(TimeMs).getUnits();
        const auto bpm=processor_.getParameterAt(ManualBpm).getUnits();
        const auto activeBpm=sync==2?bpm:processor_.getHostTempoForDisplay();
        const auto beats=Engine::kNoteBeats[static_cast<std::size_t>(juce::jlimit(0,12,note))];
        juce::String shown=sync==0?juce::String(ms,1)+" ms"
            :juce::String(static_cast<int>(std::lround(juce::jlimit(
                          1.0,3500.0,60000.0*beats/juce::jmax(1.0,activeBpm)))))+" ms";
        g.setColour(teal);
        g.setFont(juce::Font(juce::FontOptions(43.0f,juce::Font::bold)));
        g.drawText(shown,345,157,363,53,juce::Justification::centred);
        g.setColour(juce::Colour(0xff9eabbf));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(sync==1?"Host tempo drives timing"
                 :sync==2?"Manual BPM drives timing":"Free-running millisecond timing",
                   346,207,360,16,juce::Justification::centred);
        const char* captions[]={"DELAY","FEEDBACK","MOD DEPTH","MOD RATE",
                                 "HIGH PASS","LOW PASS","DRY / WET","OUTPUT","BPM"};
        const float cx[]={165,891,90,207,328,449,577,697,891};
        const float cy[]={127,127,313,313,313,313,313,313,313};
        for(int k=0;k<9;++k)
        {
            g.setColour(k<2?accent:juce::Colour(0xffaeb8cb));
            g.setFont(juce::Font(juce::FontOptions(k<2?16.0f:12.0f,juce::Font::bold)));
            g.drawText(captions[k],cx[k]-(k<2?89:55),cy[k]-22,
                k<2?178:110,22,juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff293649));
        g.drawLine(30,436,1030,436,1);
        g.setColour(juce::Colour(0xff7687a5));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("BUBBLEGUM ENGINE • PARAMETERS AUTOMATABLE • LIVE VALUES ON TOUCH",
                   32,468,996,20,juce::Justification::centred);
    }
    void resized() override
    {
        auto r=[this](int x,int y,int w,int h)
        {
            const float sx=getWidth()/1060.0f,sy=getHeight()/510.0f;
            return juce::Rectangle<int>(juce::roundToInt(x*sx),
                juce::roundToInt(y*sy),juce::roundToInt(w*sx),juce::roundToInt(h*sy));
        };
        knobs_[0].setBounds(r(92,149,150,148));
        knobs_[1].setBounds(r(816,149,150,148));
        const int x[]={40,158,279,400,522,642,840};
        for(int i=2;i<9;++i) knobs_[i].setBounds(r(x[i-2],341,104,89));
        sync_.setBounds(r(344,243,109,36));
        note_.setBounds(r(466,243,115,36));
        colour_.setBounds(r(594,243,115,36));
        tap_.setBounds(r(45,445,121,40));
        const int tx[]={197,349,487,624,761};
        for(int i=0;i<5;++i) toggles_[i].setBounds(r(tx[i],443,130,43));
        readout_.setBounds(r(370,74,324,42));
    }
private:
    void makeKnob(int slot,int parameter,double low,double high,double initial,
                  double step,const char* unit)
    {
        auto& s=knobs_[slot]; auto& p=processor_.getParameterAt(parameter);
        s.setName(p.getName(128));
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,100,24);
        s.setRange(low,high,step);
        s.setValue(initial,juce::dontSendNotification);
        s.setTextValueSuffix(unit);
        s.setTooltip(p.getName(128));
        s.onDragStart=[this,slot,parameter]
        {
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
                knobs_[i].setValue(processor_.getParameterAt(
                    i==0 && selectedSync!=0?Note:which[i]).getUnits(),
                    juce::dontSendNotification);
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
            readout_.setText({},juce::dontSendNotification);
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

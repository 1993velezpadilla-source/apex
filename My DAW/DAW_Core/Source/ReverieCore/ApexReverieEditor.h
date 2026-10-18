#pragma once
// APEX Reverie: original APEX/Bubblegum JUCE GUI, never copied artwork.
// Every rotary knob has an always-visible value and host automation gestures.
#include <JuceHeader.h>
#include <array>
#include <algorithm>
#include <cmath>
#include "ApexReverieProcessor.h"
#include "../PluginHostCore/ApexEvoEyeStamp.h"

namespace APEX::Reverie
{
class ReverieLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Native JUCE widgets, NOT a background screenshot. Gestures and
    // editable parameter values continue to use the original processor IDs.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float position, float start, float end,
                          juce::Slider&) override
    {
        const float cx=x+w*0.5f, cy=y+h*0.50f;
        const float radius=std::min(w,h)*0.365f;
        const auto pink=juce::Colour(0xffff50b8);
        const auto cyan=juce::Colour(0xff41d9ff);
        const float angle=start+(end-start)*juce::jlimit(0.0f,1.0f,position);
        g.setColour(juce::Colour(0x99000000));
        g.fillEllipse(cx-radius-9.0f,cy-radius-4.0f,
                      2*radius+18.0f,2*radius+22.0f);
        g.setColour(juce::Colour(0xff24364d));
        g.fillEllipse(cx-radius-5,cy-radius-5,2*radius+10,2*radius+10);
        g.setColour(juce::Colour(0xff5b7197));
        g.drawEllipse(cx-radius-4,cy-radius-4,2*radius+8,2*radius+8,1.0f);

        // Graduated 270-degree track and restrained neon progress, from the
        // real slider value. Tick marks remain clear at reduced plugin sizes.
        juce::Path track, lit, cyanRim;
        track.addCentredArc(cx,cy,radius+8,radius+8,0,start,end,true);
        lit.addCentredArc(cx,cy,radius+8,radius+8,0,start,angle,true);
        cyanRim.addCentredArc(cx,cy,radius+1,radius+1,0,0.5f,2.2f,true);
        g.setColour(juce::Colour(0xff2a4969));
        g.strokePath(track,juce::PathStrokeType(4.0f));
        g.setColour(pink.withAlpha(0.22f));
        g.strokePath(lit,juce::PathStrokeType(9.0f));
        g.setColour(pink);
        g.strokePath(lit,juce::PathStrokeType(4.3f,juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
        for(int i=0;i<=18;++i)
        {
            const float a=start+(end-start)*i/18.0f;
            const float inner=radius+15.0f,outer=inner+(i%3==0?5.2f:2.8f);
            g.setColour(juce::Colour(0xff65809b).withAlpha(i%3==0?0.82f:0.38f));
            g.drawLine(cx+std::sin(a)*inner,cy-std::cos(a)*inner,
                       cx+std::sin(a)*outer,cy-std::cos(a)*outer,0.95f);
        }

        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff273e56),cx-radius,cy-radius,
            juce::Colour(0xff020a17),cx+radius,cy+radius,false));
        g.fillEllipse(cx-radius,cy-radius,2*radius,2*radius);
        g.setColour(juce::Colour(0xff4b7390));
        g.drawEllipse(cx-radius+2,cy-radius+2,2*radius-4,2*radius-4,1.4f);
        g.setColour(cyan.withAlpha(0.34f));
        g.strokePath(cyanRim,juce::PathStrokeType(1.5f));
        g.setColour(juce::Colour(0x552b4e72));
        g.fillEllipse(cx-radius*0.76f,cy-radius*0.79f,
                      radius*1.52f,radius*1.52f);

        const float vx=std::sin(angle),vy=-std::cos(angle);
        g.setColour(cyan.withAlpha(0.28f));
        g.drawLine(cx+vx*radius*0.22f,cy+vy*radius*0.22f,
                   cx+vx*radius*0.80f,cy+vy*radius*0.80f,6.0f);
        g.setColour(juce::Colour(0xffe3f5ff));
        g.drawLine(cx+vx*radius*0.24f,cy+vy*radius*0.24f,
                   cx+vx*radius*0.77f,cy+vy*radius*0.77f,3.0f);
    }

    void drawComboBox(juce::Graphics& g,int width,int height,bool,
                      int,int,int,int,juce::ComboBox&) override
    {
        const auto r=juce::Rectangle<float>(0,0,(float)width,(float)height)
                          .reduced(2.0f);
        g.setColour(juce::Colour(0x33ff50b8));
        g.fillRoundedRectangle(r.expanded(2),9.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff20314b),0,0,
             juce::Colour(0xff061122),(float)width,(float)height,false));
        g.fillRoundedRectangle(r,8.0f);
        g.setColour(juce::Colour(0xff3c9fc5));
        g.drawRoundedRectangle(r,8.0f,1.25f);
        g.setColour(juce::Colour(0xffff61c5).withAlpha(0.85f));
        g.drawLine(r.getX()+2,r.getY()+8,r.getX()+2,r.getBottom()-8,1.8f);
        juce::Path chevron;
        const float ax=(float)width-20.0f,ay=(float)height*0.53f;
        chevron.addTriangle(ax-7,ay-3,ax+7,ay-3,ax,ay+4);
        g.setColour(juce::Colour(0xff4feaff));
        g.fillPath(chevron);
    }

    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,
                          bool highlighted,bool down) override
    {
        // Photo-approved single circular BYPASS toggle; text is a fixed label
        // above it, and the whole 58x58 disk is touch/click accessible.
        const auto area=button.getLocalBounds().toFloat();
        const float side=std::min(area.getWidth(),area.getHeight())-6.0f;
        const float x=area.getCentreX()-side*0.5f;
        const float y=area.getCentreY()-side*0.5f;
        const bool on=button.getToggleState();
        g.setColour((on?juce::Colour(0xffff58bc):juce::Colour(0xff64a7cd))
                        .withAlpha(highlighted||down?0.33f:0.15f));
        g.fillEllipse(x-3,y-3,side+6,side+6);
        g.setGradientFill(juce::ColourGradient(
            on?juce::Colour(0xff532446):juce::Colour(0xff172c42),x,y,
            juce::Colour(0xff050b17),x+side,y+side,false));
        g.fillEllipse(x,y,side,side);
        g.setColour(on?juce::Colour(0xffff73d3):juce::Colour(0xff6389b6));
        g.drawEllipse(x+2,y+2,side-4,side-4,2.2f);
        if(on)
        {
            g.setColour(juce::Colour(0xffff78d2).withAlpha(0.42f));
            g.fillEllipse(x+side*0.37f,y+side*0.37f,side*0.26f,side*0.26f);
        }
    }
};
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& processor)
        : juce::AudioProcessorEditor(&processor),processor_(processor)
    {
        setLookAndFeel(&look_);
        setOpaque(true);
        setResizable(true,true);
        setResizeLimits(990,550,1760,990);

        mode_.setName("REVERB MODE");
        mode_.addItemList({"PURE PLATE","VINTAGE PLATE","HALL",
                           "CHAMBER","ROOM","AMBIENT"},1);
        mode_.onChange=[this]
        {
            auto& p=processor_.modeParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(static_cast<float>(mode_.getSelectedId()-1)/5.0f);
            p.endChangeGesture();
        };
        mode_.setTooltip("Choose the reverb algorithm");
        mode_.setColour(juce::ComboBox::textColourId,juce::Colour(0xffeaf5ff));
        mode_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(mode_);

        era_.setName("COLOR ERA");
        era_.addItemList({"1970s","1980s","MODERN"},1);
        era_.onChange=[this]
        {
            auto& p=processor_.eraParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(static_cast<float>(era_.getSelectedId()-1)/2.0f);
            p.endChangeGesture();
        };
        era_.setTooltip("Choose 1970s, 1980s or modern reverb color");
        era_.setColour(juce::ComboBox::textColourId,juce::Colour(0xffeaf5ff));
        era_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(era_);

        bypass_.setButtonText({});
        bypass_.setName("BYPASS");
        bypass_.onClick=[this]
        {
            auto& p=processor_.bypassParameter();
            p.beginChangeGesture();
            p.setValueNotifyingHost(bypass_.getToggleState()?1.0f:0.0f);
            p.endChangeGesture();
        };
        bypass_.setColour(juce::ToggleButton::textColourId,juce::Colour(0xffc8dcea));
        bypass_.setTooltip("Bypass the Reverie reverb");
        addAndMakeVisible(bypass_);

        static constexpr const char* captions[10]={
            "PRE-DELAY","DECAY","SIZE","DAMPING","LOW CUT",
            "HIGH CUT","MODULATION","WIDTH","MIX","VOCAL DUCK"
        };
        static constexpr double minValue[10]={
            0.0,0.2,0.0,750.0,20.0,1500.0,0.0,0.0,0.0,0.0
        };
        static constexpr double maxValue[10]={
            250.0,18.0,100.0,20000.0,1500.0,20000.0,100.0,200.0,100.0,100.0
        };
        for(int i=0;i<10;++i)
        {
            auto& knob=knobs_[static_cast<size_t>(i)];
            knob.setName(captions[i]);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,132,28);
            knob.setRange(minValue[i],maxValue[i],i==1?0.01:(i==0?0.1:1.0));
            if(i==0) knob.setTextValueSuffix(" ms");
            if(i==1) knob.setTextValueSuffix(" s");
            if(i>=3 && i<=5) knob.setTextValueSuffix(" Hz");
            if(i>=7) knob.setTextValueSuffix(" %");
            knob.setTooltip(captions[i]);
            knob.setValue(processor_.floatAt(i+2).get(),juce::dontSendNotification);
            knob.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffdef5f8));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff0b1627));
            knob.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff56718d));
            knob.onDragStart=[this,i]
            {
                active_=i;
                processor_.floatAt(i+2).beginChangeGesture();
            };
            knob.onDragEnd=[this,i]
            {
                processor_.floatAt(i+2).endChangeGesture();
                if(active_==i)active_=-1;
            };
            knob.onValueChange=[this,i]
            {
                auto& p=processor_.floatAt(i+2);
                p.setValueNotifyingHost(p.convertTo0to1(
                    static_cast<float>(knobs_[static_cast<size_t>(i)].getValue())));
            };
            addAndMakeVisible(knob);
            auto& label=labels_[static_cast<size_t>(i)];
            label.setText(captions[i],juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId,juce::Colour(0xffbdd3df));
            label.setFont(juce::Font(juce::FontOptions(14.0f,juce::Font::bold)));
            addAndMakeVisible(label);
        }
        mode_.setSelectedId(processor_.modeParameter().getIndex()+1,
                            juce::dontSendNotification);
        era_.setSelectedId(processor_.eraParameter().getIndex()+1,
                           juce::dontSendNotification);
        bypass_.setToggleState(processor_.bypassParameter().get(),
                               juce::dontSendNotification);
        setSize(1260,700);
        startTimerHz(20);
    }
    ~Editor() override { stopTimer(); setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        // Match the approved 5-by-2 photo composition with original JUCE
        // drawing. The central EYE is sourced from the EXACT boot renderer
        // through EvoEyeStamp: OPEN, static, never a recreated neon icon.
        const float sx=getWidth()/1440.0f, sy=getHeight()/800.0f;
        g.fillAll(juce::Colour(0xff040914));
        juce::Graphics::ScopedSaveState design(g);
        g.addTransform(juce::AffineTransform::scale(sx,sy));
        const auto pink=juce::Colour(0xffff50b8);
        const auto cyan=juce::Colour(0xff4cddff);
        const auto text=juce::Colour(0xffe9f3ff);
        const auto muted=juce::Colour(0xff9bb3c9);

        const auto glass=[&g](juce::Rectangle<float> r,float round,
                              juce::Colour top,juce::Colour bottom,
                              juce::Colour edge)
        {
            g.setGradientFill(juce::ColourGradient(top,r.getX(),r.getY(),
                bottom,r.getRight(),r.getBottom(),false));
            g.fillRoundedRectangle(r,round);
            g.setColour(edge);
            g.drawRoundedRectangle(r.reduced(0.7f),round,1.1f);
        };
        const auto caption=[&g](const juce::String& value,
                                float x,float y,float w,float h,
                                float size,juce::Colour color,
                                juce::Justification justification=
                                    juce::Justification::centredLeft)
        {
            g.setColour(color);
            g.setFont(juce::Font(juce::FontOptions(size)));
            g.drawText(value,juce::roundToInt(x),juce::roundToInt(y),
                juce::roundToInt(w),juce::roundToInt(h),justification);
        };

        // Perimeter: restrained pink left accent, cyan right accent.
        glass({12,10,1416,778},19,juce::Colour(0xff15263b),
            juce::Colour(0xff060f20),juce::Colour(0xff355679));
        g.setColour(pink.withAlpha(0.74f));
        g.drawLine(14,42,14,742,2.0f);
        g.setColour(cyan.withAlpha(0.73f));
        g.drawLine(1426,46,1426,740,1.5f);

        // Header stays unobstructed on every supported window size.
        glass({18,18,1404,116},14,juce::Colour(0xff19253b),
              juce::Colour(0xff070f1c),juce::Colour(0xff3e5d7b));
        g.setColour(pink.withAlpha(0.20f));
        g.fillRoundedRectangle(53,43,10,66,5);
        g.setColour(pink);
        g.fillRoundedRectangle(56,48,5,56,3);
        caption("A P E X",92,37,210,49,36,text);
        caption("/",290,40,30,45,35,pink);
        caption("R E V E R I E",325,37,355,49,34,text);
        caption("PLATE FIRST  /  VINTAGE DIGITAL  /  MODERN SPACES",
                92,93,580,25,12,muted);
        caption("B R E A T H E",1050,51,200,20,14,muted);
        caption("S P A C E",1050,72,200,20,14,muted);
        caption("I N T O   S O U N D",1050,93,200,20,14,muted);
        g.setColour(juce::Colour(0xff52718d));
        g.drawLine(1265,43,1265,109,1.0f);
        // All native plugins share this miniature boot-eye-only corner stamp.
        APEX::Branding::EvoEyeStamp::paint(g,{1276.0f,37.0f,128.0f,70.0f});

        // Hero: dark domed glass recess with one static, OPEN, original
        // procedural boot eye, not a spectrum visualizer and not animation.
        glass({470,137,500,162},18,juce::Colour(0xff0a1831),
              juce::Colour(0xff071426),juce::Colour(0xff345676));
        juce::Path dome;
        dome.startNewSubPath(487,291);
        dome.cubicTo(505,195,579,143,720,141);
        dome.cubicTo(861,143,935,195,953,291);
        dome.closeSubPath();
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff1c274c),720,144,
            juce::Colour(0xff060e20),720,296,false));
        g.fillPath(dome);
        g.setColour(pink.withAlpha(0.39f));
        g.strokePath(dome,juce::PathStrokeType(1.9f));
        {
            juce::Graphics::ScopedSaveState domeClip(g);
            g.reduceClipRegion(dome);
            // SAME BOOT GEOMETRY, brows, metal armour, iris, z-order and color.
            // Sized down from its 600x220 once-cached procedural GDI+ frame.
            APEX::Branding::EvoEyeStamp::paint(g,{525.0f,165.0f,390.0f,136.0f});
        }
        caption("ORIGINAL APEX DSP  \xE2\x80\xA2  ZERO-SAMPLE PLUGIN LATENCY",
                510,282,420,14,10,muted,juce::Justification::centred);

        // Selectors: no redundant rows of mode/era buttons; menus do the work.
        glass({35,143,423,154},19,juce::Colour(0xff101f35),
            juce::Colour(0xff071121),juce::Colour(0xff344d70));
        glass({982,143,423,154},19,juce::Colour(0xff101f35),
            juce::Colour(0xff071121),juce::Colour(0xff344d70));
        caption("R E V E R B   M O D E",83,166,340,28,15,text);
        caption("C O L O R   /   E R A",1024,166,265,28,15,text);
        caption("B Y P A S S",1312,166,86,28,13,text,
                juce::Justification::centred);
        caption("PLATE FIRST / VINTAGE DIGITAL / MODERN SPACES",
                83,264,340,20,10,muted);

        // Two precisely aligned 5-column parameter banks.
        glass({35,310,1370,220},16,juce::Colour(0xff0f2035),
            juce::Colour(0xff071223),juce::Colour(0xff386785));
        glass({35,537,1370,220},16,juce::Colour(0xff0e1b30),
            juce::Colour(0xff071122),juce::Colour(0xff386785));
        g.setColour(pink.withAlpha(0.75f));
        g.drawLine(53,311,405,311,1.25f);
        g.drawLine(53,538,405,538,1.25f);
        g.setColour(cyan.withAlpha(0.46f));
        g.drawLine(1030,311,1392,311,1.0f);
        g.drawLine(1030,538,1392,538,1.0f);
        for(int row=0;row<2;++row)
        {
            const float top=row==0?310.0f:537.0f;
            g.setColour(juce::Colour(0xff344c65).withAlpha(0.54f));
            for(int col=1;col<5;++col)
            {
                const float x=35.0f+col*274.0f;
                g.drawLine(x,top+29,x,top+199,0.8f);
            }
        }
        // Eyelid and knob accents are purely GUI drawing; the processor's
        // existing DSP, parameter identity, automation and presets are intact.
        g.setColour(pink.withAlpha(0.68f));
        g.drawLine(525,768,915,768,1.7f);
    }

    void resized() override
    {
        const float sx=getWidth()/1440.0f,sy=getHeight()/800.0f;
        const auto rect=[sx,sy](int x,int y,int w,int h)
        {
            return juce::Rectangle<int>(
                juce::roundToInt(x*sx),juce::roundToInt(y*sy),
                juce::roundToInt(w*sx),juce::roundToInt(h*sy));
        };
        mode_.setBounds(rect(79,201,351,57));
        era_.setBounds(rect(1020,201,263,57));
        bypass_.setBounds(rect(1324,201,65,65));
        for(int i=0;i<10;++i)
        {
            const int col=i%5,row=i/5;
            const int x=35+col*274+44;
            const int top=row==0?310:537;
            labels_[static_cast<size_t>(i)].setBounds(
                rect(x-15,top+11,216,33));
            knobs_[static_cast<size_t>(i)].setBounds(
                rect(x,top+43,187,166));
        }
    }
private:
    void timerCallback() override
    {
        for(int i=0;i<10;++i)
            if(i!=active_)
                knobs_[static_cast<size_t>(i)].setValue(
                    processor_.floatAt(i+2).get(),juce::dontSendNotification);
        const int nextMode=processor_.modeParameter().getIndex()+1;
        const int nextEra=processor_.eraParameter().getIndex()+1;
        if(mode_.getSelectedId()!=nextMode)
            mode_.setSelectedId(nextMode,juce::dontSendNotification);
        if(era_.getSelectedId()!=nextEra)
            era_.setSelectedId(nextEra,juce::dontSendNotification);
        bypass_.setToggleState(processor_.bypassParameter().get(),
                               juce::dontSendNotification);
    }
    Processor& processor_;
    ReverieLookAndFeel look_;
    juce::ComboBox mode_,era_;
    juce::ToggleButton bypass_;
    std::array<juce::Slider,10> knobs_{};
    std::array<juce::Label,10> labels_{};
    int active_=-1;
};

inline juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
} // namespace APEX::Reverie

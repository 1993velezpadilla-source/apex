#pragma once
// APEX Opto 2A — tactile rack editor, drawn entirely in JUCE (no embedded
// Teletronix/UA artwork). Inspired by classic LA-2A hardware control layout.
// User supplied a photograph as the visual reference. Brand and graphics are
// original APEX. UI provides real hosted automation, meter source and state.
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "ApexOptoLevelerProcessor.h"
#include "../PluginHostCore/ApexEvoEyeStamp.h"

namespace APEX::Compressor
{

class OptoRackLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);
        const auto cx = static_cast<float>(x + width / 2);
        const auto cy = static_cast<float>(y + height / 2);
        const float radius = std::min(width,height) * 0.40f;
        const float angle = startAngle + sliderPos * (endAngle-startAngle);
        // Dial shadow, knurled black bakelite rim, and satin hub.
        g.setColour(juce::Colour(0x66000000));
        g.fillEllipse(cx-radius+3,cy-radius+5,radius*2.0f,radius*2.0f);
        g.setColour(juce::Colour(0xff07080b));
        g.fillEllipse(cx-radius,cy-radius,radius*2.0f,radius*2.0f);
        for(int k=0;k<40;++k)
        {
            const float a=juce::MathConstants<float>::twoPi*k/40.0f;
            const float r0=radius*0.91f, r1=radius*0.985f;
            g.setColour(k%4==0 ? juce::Colour(0xff626671) : juce::Colour(0xff24272e));
            g.drawLine(cx+std::cos(a)*r0,cy+std::sin(a)*r0,
                       cx+std::cos(a)*r1,cy+std::sin(a)*r1,1.1f);
        }
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff3c4049),cx-radius,cy-radius,
            juce::Colour(0xff090b11),cx+radius,cy+radius,false));
        g.fillEllipse(cx-radius*0.78f,cy-radius*0.78f,radius*1.56f,radius*1.56f);
        g.setColour(juce::Colour(0xff7a7c86));
        g.drawEllipse(cx-radius*0.73f,cy-radius*0.73f,radius*1.46f,radius*1.46f,0.8f);
        g.setColour(juce::Colour(0xffeae8e1));
        const float mark=radius*0.70f;
        g.drawLine(cx+std::sin(angle)*mark*0.35f,cy-std::cos(angle)*mark*0.35f,
                   cx+std::sin(angle)*mark,cy-std::cos(angle)*mark,3.0f);
    }

    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& button,
                          bool highlighted,bool pressed) override
    {
        juce::ignoreUnused(highlighted,pressed);
        const auto r=button.getLocalBounds().toFloat().reduced(2.0f);
        const auto center=r.getCentre();
        // Preserve a generous invisible tap target on mobile while the
        // mechanical toggle drawn in its centre stays hardware-sized.
        const float metal=std::min(20.0f,std::min(r.getWidth(),r.getHeight())*0.30f);
        g.setColour(juce::Colour(0xff393a40));
        g.fillEllipse(center.x-metal,center.y-metal,2.0f*metal,2.0f*metal);
        g.setColour(juce::Colour(0xffced0d2));
        g.fillEllipse(center.x-metal*0.80f,center.y-metal*0.80f,1.60f*metal,1.60f*metal);
        g.setColour(juce::Colour(0xff555960));
        g.drawEllipse(center.x-metal*0.75f,center.y-metal*0.75f,1.50f*metal,1.50f*metal,1.4f);
        const float offset=button.getToggleState() ? -metal*0.43f : metal*0.43f;
        g.setColour(juce::Colour(0xff101215));
        g.drawLine(center.x,center.y+2,center.x,center.y+offset,5.0f);
        g.setColour(juce::Colour(0xffe1e2e3));
        g.fillEllipse(center.x-metal*0.23f,center.y+offset-metal*0.24f,
                      metal*0.46f,metal*0.46f);
    }
};

class OptoEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OptoEditor(OptoProcessor& processor)
        : juce::AudioProcessorEditor(&processor), processor_(processor)
    {
        setLookAndFeel(&lookAndFeel_);
        configureRotary(gain_,Gain,"GAIN",0.0,40.0);
        configureRotary(peak_,PeakReduction,"PEAK REDUCTION",0.0,100.0);
        configureRotary(mix_,Mix,"PARALLEL MIX",0.0,100.0);
        configureRotary(emphasis_,Emphasis,"EMPHASIS / KEY",0.0,100.0);
        configureRotary(drive_,ColorDrive,"COLOR DRIVE",0.25,4.0);
        configureSwitch(limit_,Limit,false,"COMPRESS / LIMIT");
        configureSwitch(power_,Bypass,true,"POWER");
        configureSwitch(color_,ColorEnabled,false,"TUBE COLOR EXPERIMENTAL");
        meterMode_.setName("METER MODE");
        meterMode_.setTooltip("Meter mode: Gain Reduction, Output +4, Output +10");
        meterMode_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        meterMode_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        meterMode_.setRange(0.0,2.0,1.0);
        meterMode_.setValue(processor_.getMeterMode(),juce::dontSendNotification);
        meterMode_.onDragStart=[this] {
            activeValueControl_=&meterMode_;
            showControlValue(meterMode_,-1);
        };
        meterMode_.onDragEnd=[this] {
            activeValueControl_=nullptr;
            valueReadoutHideAtMs_=juce::Time::getMillisecondCounterHiRes()+1500.0;
        };
        meterMode_.onValueChange=[this] {
            processor_.setMeterMode(static_cast<int>(meterMode_.getValue()));
            if(activeValueControl_==&meterMode_)
                showControlValue(meterMode_,-1);
        };
        addAndMakeVisible(meterMode_);

        // Three APEX-original optical responses inside ONE processor. Host
        // automation and saved sessions own this choice, not a GUI-only flag.
        voice_.setName("OPTICAL VOICE");
        voice_.setTooltip("Optical response: FAST / CLASSIC / SLOW");
        voice_.addItem("FAST",1);
        voice_.addItem("CLASSIC",2);
        voice_.addItem("SLOW",3);
        voice_.setSelectedId(static_cast<int>(
            processor_.getParameterAt(Voicing).getUnits())+1,
            juce::dontSendNotification);
        voice_.onChange=[this] {
            auto& parameter=processor_.getParameterAt(Voicing);
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost(parameter.toNormalised(
                static_cast<float>(juce::jlimit(1,3,voice_.getSelectedId())-1)));
            parameter.endChangeGesture();
        };
        addAndMakeVisible(voice_);

        // One real, touch-safe value readout shared by all six rotary knobs.
        // It is a non-intercepting child (not a desktop tooltip), so the
        // performer can read the value on touchscreens as well as with a mouse
        // without losing slider drag capture or automation gestures.
        valueReadout_.setName("LIVE VALUE READOUT");
        valueReadout_.setInterceptsMouseClicks(false,false);
        valueReadout_.setJustificationType(juce::Justification::centred);
        valueReadout_.setColour(juce::Label::backgroundColourId,
                                juce::Colour(0xee182331));
        valueReadout_.setColour(juce::Label::textColourId,
                                juce::Colour(0xffffe4f3));
        valueReadout_.setColour(juce::Label::outlineColourId,
                                juce::Colour(0xffff56af));
        valueReadout_.setFont(juce::Font(juce::FontOptions(15.0f,juce::Font::bold)));
        addAndMakeVisible(valueReadout_);
        valueReadout_.setVisible(false);

        setOpaque(true);
        setResizable(true,true);
        // The approved rack presentation is 4:3. Keep the analogue controls
        // in their native proportions instead of stretching the faceplate.
        getConstrainer()->setFixedAspectRatio(4.0 / 3.0);
        setResizeLimits(700,525,1560,1170);
        setSize(1040,780);
        startTimerHz(30);
    }

    ~OptoEditor() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111923));
        g.saveState();
        const auto scale=faceScale();
        g.addTransform(juce::AffineTransform::scale(scale).translated(
            (getWidth()-kFaceWidth*scale)*0.5f,
            (getHeight()-kFaceHeight*scale)*0.5f));
        drawChassis(g);
        drawMeter(g);
        drawBubblegumStrip(g);
        // Canonical open boot-eye: a SMALL, STATIC branding stamp directly
        // right of the APEX wordmark in the bottom-left strip. It never
        // intersects the GR/+4/+10 VU selector or any analogue controls.
        APEX::Branding::EvoEyeStamp::paint(g, {143.0f, 292.0f, 128.0f, 38.0f});
        g.restoreState();
    }

    void resized() override
    {
        // Match every JUCE hit target to the same uniform transform used by
        // paint(); visual positions, gestures and labels remain aligned.
        gain_.setBounds(faceRect(243,101,119,119));
        peak_.setBounds(faceRect(684,101,119,119));
        limit_.setBounds(faceRect(106,158,71,69));
        power_.setBounds(faceRect(876,164,71,69));
        meterMode_.setBounds(faceRect(883,65,75,75));
        voice_.setBounds(faceRect(322,303,144,31));

        mix_.setBounds(faceRect(499,310,66,55));
        emphasis_.setBounds(faceRect(663,310,66,55));
        drive_.setBounds(faceRect(822,310,66,55));
        color_.setBounds(faceRect(907,300,77,68));
        if(valueReadout_.isVisible() && activeValueControl_!=nullptr)
            positionValueReadout(*activeValueControl_);
    }

private:
    static constexpr float kFaceWidth=1040.0f;
    static constexpr float kFaceHeight=380.0f;

    float faceScale() const noexcept
    {
        return juce::jmin(getWidth()/kFaceWidth,getHeight()/kFaceHeight);
    }

    juce::Rectangle<int> faceRect(float x,float y,float width,float height) const
    {
        const auto scale=faceScale();
        const auto offsetX=(getWidth()-kFaceWidth*scale)*0.5f;
        const auto offsetY=(getHeight()-kFaceHeight*scale)*0.5f;
        return {juce::roundToInt(offsetX+x*scale),
                juce::roundToInt(offsetY+y*scale),
                juce::jmax(10,juce::roundToInt(width*scale)),
                juce::jmax(10,juce::roundToInt(height*scale))};
    }

    static void label(juce::Graphics& g,const juce::String& t,float x,float y,float w,
                      float h,float size,juce::Colour color,bool bold=false,
                      juce::Justification alignment=juce::Justification::centred)
    {
        g.setColour(color);
        g.setFont(juce::Font(juce::FontOptions(size,
            bold ? juce::Font::bold : juce::Font::plain)));
        g.drawText(t,juce::Rectangle<float>(x,y,w,h),alignment);
    }

    static void screw(juce::Graphics& g,float cx,float cy)
    {
        g.setColour(juce::Colour(0x55000000));g.fillEllipse(cx-7,cy-6,14,14);
        g.setColour(juce::Colour(0xffb3b6b7));g.fillEllipse(cx-6,cy-6,12,12);
        g.setColour(juce::Colour(0xff545861));
        g.drawLine(cx-3.1f,cy-2.2f,cx+3.1f,cy+2.2f,1.5f);
    }

    static void dialTicks(juce::Graphics& g,float cx,float cy,float radius)
    {
        for(int i=0;i<=10;++i)
        {
            const float a=(225.0f+i*27.0f)*juce::MathConstants<float>::pi/180.0f;
            // JUCE rotary controls move from ~225 degrees to ~495 degrees.
            const float x0=cx+std::sin(a)*radius, y0=cy-std::cos(a)*radius;
            const float x1=cx+std::sin(a)*(radius+6),y1=cy-std::cos(a)*(radius+6);
            g.setColour(juce::Colour(0xff343b42));
            g.drawLine(x0,y0,x1,y1,i%2==0?1.5f:1.0f);
            if(i%2==0)
                label(g,juce::String(i*10),cx+std::sin(a)*(radius+18)-10,
                      cy-std::cos(a)*(radius+18)-7,20,14,10,juce::Colour(0xff273039));
        }
    }

    void drawChassis(juce::Graphics& g)
    {
        // Rack ears and lip: geometry follows user-supplied Teletronix photo,
        // but marks, typography and materials are APEX originals.
        g.setColour(juce::Colour(0xffaeb3b7));
        g.fillRoundedRectangle(5,9,1030,270,6);
        g.setColour(juce::Colour(0xffe3e7e7));
        g.fillRect(35,12,971,263);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xfff0f1ed),35,14,
            juce::Colour(0xffb9c0c3),950,270,false));
        g.fillRect(51,15,937,257);
        g.setColour(juce::Colour(0xff828c91));
        g.drawRect(50,14,939,259,1);
        // mounting slots, rails, and subtle lower cast shadow
        for(const auto x : {14.0f,1014.0f})
        {
            g.setColour(juce::Colour(0xff202733));
            g.fillRoundedRectangle(x,68,12,22,6);
            g.fillRoundedRectangle(x,195,12,22,6);
        }
        g.setColour(juce::Colour(0xff8b9398));
        g.fillRect(53,266,932,3);
        g.setColour(juce::Colour(0xffeef0f0));
        g.fillRect(53,269,932,2);
        for (const auto x : {86.0f,392.0f,649.0f,979.0f})
        {
            screw(g,x,30);
            screw(g,x,254);
        }

        // Custom APEX wordmark — do not copy Teletronix / UAD trademarks.
        label(g,"APEX",120,48,156,33,28,juce::Colour(0xffa12069),true,
              juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffa12069));
        g.fillRect(122,83,96,2);
        label(g,"OPTO 2A",120,89,106,19,12,juce::Colour(0xff3c4650),true,
              juce::Justification::centredLeft);
        label(g,"OPTICAL LEVELER",120,109,125,14,
              9,juce::Colour(0xff626c72),false,juce::Justification::centredLeft);

        dialTicks(g,302,161,64);
        dialTicks(g,744,161,64);
        label(g,"GAIN",249,227,105,24,13,juce::Colour(0xff333b41),true);
        label(g,"PEAK REDUCTION",667,227,164,24,13,juce::Colour(0xff333b41),true);

        label(g,"LIMIT",103,154,69,16,11,juce::Colour(0xff394148),true);
        label(g,"COMPRESS",98,215,81,16,10,juce::Colour(0xff394148));
        label(g,"GAIN REDUCTION",853,28,142,21,10,juce::Colour(0xff3c454b),true);
        label(g,"GR    +4    +10",853,148,143,19,12,juce::Colour(0xff364047));
        label(g,"POWER",869,219,81,19,11,juce::Colour(0xff343a40),true);
        label(g,"ON",887,161,43,15,10,juce::Colour(0xff40474e),true);

        // Tiny jewel lamp responds to actual host bypass.
        const bool active=processor_.getParameterAt(Bypass).getUnits()<0.5f;
        g.setColour(active ? juce::Colour(0xfff6928c) : juce::Colour(0xff67666a));
        g.fillEllipse(962,205,11,11);
    }

    void drawMeter(juce::Graphics& g)
    {
        constexpr float x=405, y=45,w=231,h=153;
        // Rectangular VU housing, amber glass and dark edge.
        g.setColour(juce::Colour(0xff343e43));
        g.fillRoundedRectangle(x-6,y-6,w+12,h+12,6.0f);
        g.setColour(juce::Colour(0xff718087));
        g.drawRoundedRectangle(x-6,y-6,w+12,h+12,6.0f,2.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffce9b5),x,y,
                                               juce::Colour(0xffbba276),x+w,y+h,false));
        g.fillRect(x,y,w,h);
        g.setColour(juce::Colour(0x66a18055));
        g.fillRoundedRectangle(x+9,y+8,w-18,h-35,8);

        // Keep the engraved scale and moving needle unobscured by branding.
        label(g,"APEX",x+79,y+112,w-158,14,11,juce::Colour(0xff7a553a),true);
        const float pivotX=x+w/2,pivotY=y+132;
        const float radius=105.0f;
        for(int i=0;i<=10;++i)
        {
            const float a=(-0.94f+1.88f*i/10.0f);
            const float x0=pivotX+std::sin(a)*radius;
            const float y0=pivotY-std::cos(a)*radius;
            const float x1=pivotX+std::sin(a)*(radius-12.0f);
            const float y1=pivotY-std::cos(a)*(radius-12.0f);
            g.setColour(i>7?juce::Colour(0xffb43835):juce::Colour(0xff433d36));
            g.drawLine(x0,y0,x1,y1,i%2==0?1.6f:0.9f);
            if(i%2==0)
            {
                const auto num=juce::String(-20+i*2);
                label(g,num,x0-15,y0-18,30,14,10,juce::Colour(0xff4b423a));
            }
        }

        // Anti-phase needle shadows then needle, fully live, no fake motion.
        const float angle=-0.94f+1.88f*needle_;
        const float ex=pivotX+std::sin(angle)*101.0f;
        const float ey=pivotY-std::cos(angle)*101.0f;
        g.setColour(juce::Colour(0x55000000));
        g.drawLine(pivotX+1,pivotY+2,ex+1,ey+2,3.2f);
        g.setColour(juce::Colour(0xff3c3530));
        g.drawLine(pivotX,pivotY,ex,ey,1.8f);
        g.setColour(juce::Colour(0xff4e5351));
        g.fillEllipse(pivotX-6,pivotY-6,12,12);
        label(g,meterLabel(),x+24,y+169,w-48,20,11,juce::Colour(0xff38454c),true);
    }

    void drawBubblegumStrip(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff101723));
        g.fillRoundedRectangle(5,283,1030,92,8);
        g.setColour(juce::Colour(0xffde348f));
        g.fillRoundedRectangle(12,288,7,81,2);
        label(g,"APEX",34,295,100,35,29,juce::Colour(0xfffa4baf),true,
              juce::Justification::centredLeft);
        label(g,"ADVANCED CONTROLS",34,333,239,17,11,juce::Colour(0xffc7ced9),true,
              juce::Justification::centredLeft);
        label(g,"Vintage face / Modern workflow",34,350,244,14,10,
              juce::Colour(0xff8997ac),false,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff363d4d));
        g.drawVerticalLine(310,301,361);
        label(g,"PARALLEL MIX",478,289,110,15,10,juce::Colour(0xffd3dae5),true);
        label(g,"KEY EMPHASIS",636,289,120,15,10,juce::Colour(0xffd3dae5),true);
        label(g,"COLOR DRIVE",797,289,117,15,10,juce::Colour(0xffd3dae5),true);
        label(g,"COLOR",906,351,101,15,10,juce::Colour(0xffd3dae5),true);
        label(g,"VOICE",318,289,150,14,10,juce::Colour(0xffd3dae5),true);
        label(g,"ZERO LATENCY / APEX NATIVE",314,334,158,30,9,
              juce::Colour(0xffb2a4ba));
    }

    juce::String meterLabel() const
    {
        switch(processor_.getMeterMode())
        {
            case 1: return "OUTPUT +4";
            case 2: return "OUTPUT +10";
            default: return "GAIN REDUCTION";
        }
    }

    void configureRotary(juce::Slider& slider,int index,const juce::String& name,
                         double low,double high)
    {
        slider.setName(name);
        slider.setTooltip(name+" (APEX automation)");
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        slider.setRange(low,high,index==ColorDrive ? 0.01 : 0.1);
        slider.setValue(processor_.getParameterAt(index).getUnits(),
                        juce::dontSendNotification);
        auto& param=processor_.getParameterAt(index);
        slider.onDragStart=[this,&slider,&param,index] {
            param.beginChangeGesture();
            activeValueControl_=&slider;
            showControlValue(slider,index);
        };
        slider.onDragEnd=[this,&slider,&param] {
            param.endChangeGesture();
            if(activeValueControl_==&slider)
            {
                activeValueControl_=nullptr;
                valueReadoutHideAtMs_=juce::Time::getMillisecondCounterHiRes()+1500.0;
            }
        };
        slider.onValueChange=[this,&slider,&param,index] {
            param.setValueNotifyingHost(
                param.toNormalised(static_cast<float>(slider.getValue())));
            if(activeValueControl_==&slider)
                showControlValue(slider,index);
        };
        addAndMakeVisible(slider);
    }

    void showControlValue(juce::Slider& slider,int index)
    {
        juce::String content;
        if(index<0)
        {
            const int mode=static_cast<int>(slider.getValue());
            content=mode==0 ? "VU  GR" : (mode==1 ? "VU  OUTPUT +4" : "VU  OUTPUT +10");
        }
        else
        {
            const auto number=juce::String(slider.getValue(),
                                            index==ColorDrive ? 2 : 1);
            switch(index)
            {
                case Gain:          content="GAIN  "+number+" dB";break;
                case PeakReduction: content="PEAK  "+number;break;
                case Mix:           content="MIX  "+number+" %";break;
                case Emphasis:      content="KEY  "+number+" %";break;
                case ColorDrive:    content="DRIVE  "+number+" x";break;
                default:           content=number;break;
            }
        }
        valueReadout_.setText(content,juce::dontSendNotification);
        positionValueReadout(slider);
        valueReadout_.setVisible(true);
        valueReadout_.toFront(false);
        valueReadoutHideAtMs_=0.0; // no timeout while actively dragging
    }

    void positionValueReadout(const juce::Slider& slider)
    {
        constexpr int width=164, height=34, margin=8;
        const auto rect=slider.getBounds();
        const int x=juce::jlimit(margin,
            std::max(margin,getWidth()-width-margin),
            rect.getCentreX()-width/2);
        const int above=rect.getY()-height-6;
        const int y=above>=margin ? above
            : std::min(getHeight()-height-margin,rect.getBottom()+6);
        valueReadout_.setBounds(x,y,width,height);
    }

    void configureSwitch(juce::ToggleButton& button,int index,bool invert,
                         const juce::String& name)
    {
        button.setName(name);
        button.setTooltip(name);
        auto& param=processor_.getParameterAt(index);
        button.setToggleState((param.getUnits()>0.5f)!=invert,
                              juce::dontSendNotification);
        button.onClick=[&button,&param,invert]{
            param.beginChangeGesture();
            const bool active=button.getToggleState()!=invert;
            param.setValueNotifyingHost(active?1.0f:0.0f);
            param.endChangeGesture();
        };
        addAndMakeVisible(button);
    }

    void timerCallback() override
    {
        if(activeValueControl_==nullptr && valueReadout_.isVisible()
           && valueReadoutHideAtMs_>0.0
           && juce::Time::getMillisecondCounterHiRes()>=valueReadoutHideAtMs_)
        {
            valueReadout_.setVisible(false);
            valueReadoutHideAtMs_=0.0;
        }
        const std::array<std::pair<juce::Slider*,int>,5> pairs {{
            {&gain_,Gain},{&peak_,PeakReduction},{&mix_,Mix},
            {&emphasis_,Emphasis},{&drive_,ColorDrive}
        }};
        for (const auto& p:pairs)
            if(!p.first->isMouseButtonDown())
                p.first->setValue(processor_.getParameterAt(p.second).getUnits(),
                                  juce::dontSendNotification);
        const auto value=[this](int index) {
            return processor_.getParameterAt(index).getUnits()>=0.5f;
        };
        limit_.setToggleState(value(Limit),juce::dontSendNotification);
        power_.setToggleState(!value(Bypass),juce::dontSendNotification);
        color_.setToggleState(value(ColorEnabled),juce::dontSendNotification);
        if(!meterMode_.isMouseButtonDown())
            meterMode_.setValue(processor_.getMeterMode(),juce::dontSendNotification);
        if(!voice_.isMouseButtonDown())
            voice_.setSelectedId(static_cast<int>(
                processor_.getParameterAt(Voicing).getUnits())+1,
                juce::dontSendNotification);

        const int mode=processor_.getMeterMode();
        float target=1.0f;
        if(mode==0)
        {
            const auto reduction=processor_.getGainReductionMeterDb();
            target=1.0f-juce::jlimit(0.0f,1.0f,reduction/20.0f);
        }
        else
        {
            // UAD-style nominal calibration +4 dBu=-12 dBFS; +10 is 6 dB
            // higher. This is a level display, NOT a calibrated true VU
            // ballistics/circuit model. Exact analog VU match remains a gate.
            const float zero=mode==1 ? -12.0f : -6.0f;
            const float vu=processor_.getOutputRmsDbfs()-zero;
            target=juce::jlimit(0.0f,1.0f,(vu+20.0f)/23.0f);
        }
        needle_+=(target-needle_)*0.18f;
        repaint(faceRect(392,31,258,189));
        repaint(faceRect(948,192,34,40));
    }

    OptoProcessor& processor_;
    OptoRackLookAndFeel lookAndFeel_;
    juce::Slider gain_,peak_,mix_,emphasis_,drive_,meterMode_;
    juce::Label valueReadout_;
    juce::Slider* activeValueControl_ = nullptr; // GUI/message thread only
    double valueReadoutHideAtMs_ = 0.0;
    juce::ComboBox voice_;
    juce::ToggleButton limit_,power_,color_;
    float needle_=1.0f;
};

inline juce::AudioProcessorEditor* OptoProcessor::createEditor()
{
    return new OptoEditor(*this);
}

} // namespace APEX::Compressor

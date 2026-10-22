#pragma once
// APEX Precision C3. Reference-driven hierarchy based on Pro-C 3's published
// manual (NOT FabFilter source/artwork). These are real JUCE controls connected
// to APEX host parameters. The meters draw only live DSP data. Unimplemented
// oversampling, lookahead and 6-band SC are deliberately NOT misrepresented.
#include <JuceHeader.h>
#include "../PluginHostCore/ApexEvoEyeStamp.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>

namespace APEX::PrecisionC3
{
class PrecisionLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float start, float end,
                          juce::Slider& s) override
    {
        const float side=static_cast<float>(std::min(w,h));
        const float cx=x+w*0.5f, cy=y+h*0.5f;
        const float radius=side*0.46f;
        const auto gold=juce::Colour(0xffffc44d);
        // Warm graphite disk, hairline metal ring, short amber stroke.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff343137),cx,cy-radius,
            juce::Colour(0xff18171b),cx,cy+radius,true));
        g.fillEllipse(cx-radius,cy-radius,radius*2.0f,radius*2.0f);
        g.setColour(juce::Colour(0xff686366).withAlpha(0.70f));
        g.drawEllipse(cx-radius,cy-radius,radius*2.0f,radius*2.0f,1.1f);
        const auto initial=start+0.07f, finish=end-0.07f;
        juce::Path rail, lit;
        rail.addCentredArc(cx,cy,radius-3.3f,radius-3.3f,0.0f,
                            initial,finish,true);
        lit.addCentredArc(cx,cy,radius-3.3f,radius-3.3f,0.0f,
                           initial,initial+(finish-initial)*pos,true);
        g.setColour(juce::Colour(0xff535057));
        g.strokePath(rail,juce::PathStrokeType(3.5f,
                juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.setColour(gold);
        g.strokePath(lit,juce::PathStrokeType(3.6f,
                juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        const float a=initial+(finish-initial)*pos;
        g.setColour(gold.brighter(0.20f));
        g.fillEllipse(cx+std::sin(a)*(radius-3.3f)-2.1f,
                      cy-std::cos(a)*(radius-3.3f)-2.1f,4.2f,4.2f);
        g.setColour(juce::Colour(0xff252227));
        g.fillEllipse(cx-radius*0.67f,cy-radius*0.67f,
                       radius*1.34f,radius*1.34f);
        // Distinct thumb tick keeps values readable in compact screenshots.
        g.setColour(juce::Colour(0xffcfc5a9));
        g.drawLine(cx+std::sin(a)*radius*0.28f,
                   cy-std::cos(a)*radius*0.28f,
                   cx+std::sin(a)*radius*0.54f,
                   cy-std::cos(a)*radius*0.54f,1.8f);
        juce::ignoreUnused(s);
    }

    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                          float sliderPos,float,float,
                          juce::Slider::SliderStyle,
                          juce::Slider&) override
    {
        const float cy=y+height*0.53f, from=static_cast<float>(x)+8.0f;
        const float to=static_cast<float>(x+width)-8.0f;
        g.setColour(juce::Colour(0xff514951));
        g.drawLine(from,cy,to,cy,1.4f);
        g.setColour(juce::Colour(0xffdfb852));
        g.drawLine(from,cy,sliderPos,cy,2.4f);
        g.fillEllipse(sliderPos-3.2f,cy-3.2f,6.4f,6.4f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour& background,
                              bool hovered, bool down) override
    {
        auto box=b.getLocalBounds().toFloat().reduced(0.6f);
        const auto base=background.isTransparent()
            ? juce::Colour(0xff2a272b) : background;
        g.setColour(hovered ? base.brighter(0.13f) : base);
        g.fillRoundedRectangle(box,3.0f);
        g.setColour(down ? juce::Colour(0xffffca65)
                         : juce::Colour(0xff51494e));
        g.drawRoundedRectangle(box,3.0f,1.0f);
    }
    void drawComboBox(juce::Graphics& g,int width,int height,bool isDown,
                      int,int,int,int,juce::ComboBox&) override
    {
        g.setColour(juce::Colour(0xff302c30));
        g.fillRoundedRectangle(0.5f,0.5f,(float)width-1.0f,
                                (float)height-1.0f,4.0f);
        g.setColour(isDown ? juce::Colour(0xffffca65)
                           : juce::Colour(0xff5c5154));
        g.drawRoundedRectangle(0.5f,0.5f,(float)width-1.0f,
                                (float)height-1.0f,4.0f,1.0f);
        const float ax=(float)width-19.0f, ay=height*0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(ax-5.0f,ay-2.0f);
        arrow.lineTo(ax,ay+3.0f);
        arrow.lineTo(ax+5.0f,ay-2.0f);
        g.setColour(juce::Colour(0xffe4b94e));
        g.strokePath(arrow,juce::PathStrokeType(1.5f));
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& processor)
        : juce::AudioProcessorEditor(&processor), processor_(processor)
    {
        setOpaque(true);
        setLookAndFeel(&lookAndFeel_);
        for (int i=0;i<6;++i)
        {
            const int idx=kKnobParams[static_cast<std::size_t>(i)];
            auto& knob=knobs_[static_cast<std::size_t>(i)];
            setupSlider(knob,idx,i<4);
        }
        for(int i=0;i<4;++i)
            setupSlider(minis_[static_cast<std::size_t>(i)],
                        kMiniParams[static_cast<std::size_t>(i)],false,true);
        for(int i=0;i<5;++i)
            setupSlider(utilities_[static_cast<std::size_t>(i)],
                        kUtilityParams[static_cast<std::size_t>(i)],false,true);

        for(auto* combo : {&style_,&character_,&preset_,&triggerSource_})
        {
            combo->setColour(juce::ComboBox::textColourId,juce::Colour(0xffebdfc3));
            combo->setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff302c30));
            combo->setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff585155));
            combo->setColour(juce::ComboBox::arrowColourId,juce::Colour(0xffecc15e));
        }
        style_.setName("C3 STYLE");
        character_.setName("C3 CHARACTER");
        preset_.setName("C3 PRESET");
        triggerSource_.setName("C3 TRIGGER SOURCE");
        style_.setTooltip("Compression topology and voicing");
        for(int i=0;i<14;++i)
            style_.addItem(kStyleNames[static_cast<std::size_t>(i)],i+1);
        style_.onChange=[this] { setDiscrete(Style,style_.getSelectedId()-1); };
        addAndMakeVisible(style_);
        constexpr std::array<const char*,4> characters {"Off","Tube","Diode","Bright"};
        for (int i=0;i<4;++i) character_.addItem(characters[i],i+1);
        character_.onChange=[this] { setDiscrete(Character,character_.getSelectedId()-1); };
        addAndMakeVisible(character_);

        triggerSource_.addItem("Internal",1);
        triggerSource_.addItem("External",2);
        triggerSource_.setTooltip("Trigger from main input or host-routed external sidechain");
        triggerSource_.onChange=[this] {
            setDiscrete(ExternalSidechain,triggerSource_.getSelectedId()-1);
        };
        addAndMakeVisible(triggerSource_);

        charPrePost_.setName("C3 CHARACTER ROUTING");
        charPrePost_.setTooltip("Route original character saturation before or after compression");
        charPrePost_.onClick=[this] {
            rememberUndo();
            auto& p=processor_.getC3Parameter(CharacterPre);
            p.beginChangeGesture();
            p.setValueNotifyingHost(p.getValue() >= 0.5f ? 0.0f : 1.0f);
            p.endChangeGesture();
            updateCharacterRouting();
        };
        addAndMakeVisible(charPrePost_);

        constexpr std::array<const char*,4> buttonNames {
            "AUTO THRESHOLD","AUTO RELEASE","AUTO GAIN","BYPASS"
        };
        for(int i=0;i<4;++i)
        {
            auto& b=toggles_[static_cast<std::size_t>(i)];
            const int parameter=kToggleParams[static_cast<std::size_t>(i)];
            b.setButtonText(buttonNames[static_cast<std::size_t>(i)]);
            b.onClick=[this,parameter] {
                rememberUndo();
                auto& p=processor_.getC3Parameter(parameter);
                p.beginChangeGesture();
                p.setValueNotifyingHost(p.getValue() >= 0.5f ? 0.0f : 1.0f);
                p.endChangeGesture();
                refreshToggleColours();
            };
            addAndMakeVisible(b);
        }

        scToggle_.setName("C3 SIDECHAIN PANEL");
        scToggle_.setButtonText("SIDE CHAIN   ^");
        scToggle_.setTooltip("Open or close detector EQ and stereo linking");
        scToggle_.onClick=[this] {
            sidechainOpen_=!sidechainOpen_;
            scToggle_.setButtonText(sidechainOpen_ ? "SIDE CHAIN   ^" : "SIDE CHAIN   v");
            resized(); repaint();
        };
        addAndMakeVisible(scToggle_);
        sidechainOpen_=true;

        aB_.setName("C3 A/B");
        aB_.setButtonText("A / B    A");
        aB_.setTooltip("Switch between two editable compressor snapshots");
        aB_.onClick=[this] { swapAB(); };
        addAndMakeVisible(aB_);
        copy_.setName("C3 COPY");
        copy_.setButtonText("COPY");
        copy_.setTooltip("Copy current A/B state into the other slot");
        copy_.onClick=[this] { snapshots_[1-activeSnapshot_]=snapshot(); };
        addAndMakeVisible(copy_);
        undo_.setName("C3 UNDO");
        undo_.setButtonText("UNDO");
        undo_.setTooltip("Undo the most recent panel parameter gesture");
        undo_.onClick=[this] { if(hasUndo_) { auto now=snapshot(); apply(undoState_);
            redoState_=now;hasRedo_=true;hasUndo_=false; } };
        addAndMakeVisible(undo_);
        redo_.setName("C3 REDO");
        redo_.setButtonText("REDO");
        redo_.onClick=[this] { if(hasRedo_) { undoState_=snapshot();apply(redoState_);
            hasUndo_=true;hasRedo_=false; } };
        addAndMakeVisible(redo_);

        preset_.addItem("Default",1);
        preset_.addItem("Vocal Control",2);
        preset_.addItem("Punchy Drums",3);
        preset_.addItem("Gentle Bus",4);
        preset_.onChange=[this] { loadPreset(preset_.getSelectedId()); };
        addAndMakeVisible(preset_);
        preset_.setSelectedId(1,juce::dontSendNotification);

        snapshots_[0]=snapshot();
        snapshots_[1]=snapshots_[0];
        syncControls();
        setResizable(true,true);
        setResizeLimits(780,534,1880,1288);
        getConstrainer()->setFixedAspectRatio(1080.0/740.0);
        setSize(1080,740);
        startTimerHz(30);
    }

    ~Editor() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff161518));
        juce::Graphics::ScopedSaveState state(g);
        const float sc=scale();
        g.addTransform(juce::AffineTransform::scale(sc).translated(
            (getWidth()-1080.0f*sc)*0.5f,
            (getHeight()-740.0f*sc)*0.5f));

        // Full-bleed graphite surface, NO disconnected rounded control panels.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff343035),540.0f,0.0f,
            juce::Colour(0xff19191d),540.0f,730.0f,false));
        g.fillRect(0,0,1080,740);
        g.setColour(juce::Colour(0xff111116)); g.fillRect(0,0,1080,50);
        g.setColour(juce::Colour(0xff403c3e)); g.drawHorizontalLine(50,0,1080);

        g.setColour(juce::Colour(0xffe7ba58));
        g.setFont(juce::Font(juce::FontOptions(28.0f,juce::Font::bold)));
        g.drawText("APEX",21,7,115,36,juce::Justification::centredLeft);
        APEX::Branding::EvoEyeStamp::paint(g,{18.0f,35.0f,30.0f,14.0f});
        g.setColour(juce::Colour(0xffe2d0a0));
        g.setFont(juce::Font(juce::FontOptions(24.0f,juce::Font::bold)));
        g.drawText("Precision C3",129,8,220,34,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff787177));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("BUBBLEGUM ENGINE",54,39,188,12,juce::Justification::left);

        paintScope(g);
        paintKnobLabels(g);
        paintMiniLabels(g);
        paintMeter(g);
        if(sidechainOpen_) paintSidechain(g);
        else
        {
            g.setColour(juce::Colour(0xff1a191d));
            g.fillRect(0,519,1080,185);
            g.setColour(juce::Colour(0xff615a59));
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText("Detector controls hidden. Dynamics processing remains active.",
                       175,605,760,26,juce::Justification::centred);
        }
        paintFooter(g);
    }

    void resized() override
    {
        const std::array<juce::Rectangle<int>,6> mainBounds {{
            {77,315,106,106},{251,315,106,106},{578,315,106,106},
            {752,315,106,106},{952,325,62,69},{952,407,62,69}
        }};
        for(int i=0;i<6;++i)
        {
            const auto& b=mainBounds[static_cast<std::size_t>(i)];
            knobs_[static_cast<std::size_t>(i)].setBounds(rect(b.getX(),b.getY(),b.getWidth(),b.getHeight()));
        }
        const std::array<juce::Rectangle<int>,4> miniBounds {{
            {72,442,122,36},{241,442,122,36},{580,442,122,36},{749,442,122,36}
        }};
        for(int i=0;i<4;++i)
        {
            const auto& b=miniBounds[static_cast<std::size_t>(i)];
            minis_[static_cast<std::size_t>(i)].setBounds(rect(b.getX(),b.getY(),b.getWidth(),b.getHeight()));
        }
        toggles_[0].setBounds(rect(76,296,123,21));
        toggles_[1].setBounds(rect(753,296,128,21));
        toggles_[2].setBounds(rect(937,300,95,21));
        toggles_[3].setBounds(rect(980,708,93,25));

        scToggle_.setBounds(rect(447,489,189,25));
        for(int i=0;i<5;++i)
        {
            const std::array<juce::Rectangle<int>,5> utilityBounds {{
                {27,598,115,27},{27,656,115,27},{228,654,170,28},
                {750,654,145,28},{898,654,111,28}
            }};
            const auto& b=utilityBounds[static_cast<std::size_t>(i)];
            utilities_[static_cast<std::size_t>(i)].setBounds(
                rect(b.getX(),b.getY(),b.getWidth(),b.getHeight()));
            utilities_[static_cast<std::size_t>(i)].setVisible(sidechainOpen_||i>=3);
        }
        style_.setBounds(rect(407,365,128,30));
        triggerSource_.setBounds(rect(16,542,125,28));
        triggerSource_.setVisible(sidechainOpen_);
        // Style and Character belong to the center Dynamics control cluster,
        // not inside the detector EQ. Both remain usable if SC is collapsed.
        character_.setBounds(rect(407,420,128,29));
        charPrePost_.setBounds(rect(407,455,128,26));
        aB_.setBounds(rect(455,9,91,29));
        copy_.setBounds(rect(551,9,59,29));
        undo_.setBounds(rect(349,9,58,29));
        redo_.setBounds(rect(409,9,45,29));
        preset_.setBounds(rect(639,9,253,29));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if(!sidechainOpen_)return;
        const float x=(event.position.x-shiftX())/scale();
        const float y=(event.position.y-shiftY())/scale();
        if(y<539.0f||y>640.0f||x<173.0f||x>1010.0f) return;
        // Adversarial constraint: clicking an empty EQ region MUST NOT
        // unexpectedly move an arbitrary HP/LP point. Select an actual node.
        const int picked=closestDetectorNode(x,y,22.0f);
        if(picked<0)return;
        activeEqNode_=picked;
        rememberUndo();
        processor_.getC3Parameter(activeEqNode_).beginChangeGesture();
        if(activeEqNode_>=SC1Freq && activeEqNode_<=SC4Freq)
            processor_.getC3Parameter(activeEqNode_+1).beginChangeGesture();
        dragEqNode(x,y);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if(activeEqNode_<0)return;
        dragEqNode((event.position.x-shiftX())/scale(),
                   (event.position.y-shiftY())/scale());
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(activeEqNode_>=0)
        {
            processor_.getC3Parameter(activeEqNode_).endChangeGesture();
            if(activeEqNode_>=SC1Freq && activeEqNode_<=SC4Freq)
                processor_.getC3Parameter(activeEqNode_+1).endChangeGesture();
            activeEqNode_=-1;
        }
    }
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override
    {
        if(!sidechainOpen_||wheel.deltaY==0.0f)return;
        const float x=(event.position.x-shiftX())/scale();
        const float y=(event.position.y-shiftY())/scale();
        const int picked=closestDetectorNode(x,y,21.0f);
        if(picked<SC1Freq||picked>SC4Freq)return;
        const int qIndex=picked+2;
        auto& param=processor_.getC3Parameter(qIndex);
        rememberUndo();
        const float value=param.getUnits();
        const float next=juce::jlimit(0.15f,18.0f,
                         value*std::pow(1.14f,wheel.deltaY*5.0f));
        param.beginChangeGesture();
        param.setValueNotifyingHost(param.toNormalised(next));
        param.endChangeGesture();
        selectedBell_=(picked-SC1Freq)/3;
        repaint(rect(155,519,851,183));
    }

private:
    static constexpr std::array<int,6> kKnobParams {{
        Threshold,Ratio,Attack,Release,Wet,Dry
    }};
    static constexpr std::array<int,4> kMiniParams {{
        Knee,Range,Hold,Drive
    }};
    static constexpr std::array<int,5> kUtilityParams {{
        StereoLink,SidechainHP,SidechainLP,Input,Output
    }};
    static constexpr std::array<int,4> kToggleParams {{
        AutoThreshold,AutoRelease,AutoGain,Bypass
    }};

    float scale() const noexcept
    {
        return std::min(getWidth()/1080.0f,getHeight()/740.0f);
    }
    float shiftX() const noexcept {return (getWidth()-1080.0f*scale())*0.5f;}
    float shiftY() const noexcept {return (getHeight()-740.0f*scale())*0.5f;}
    juce::Rectangle<int> rect(int x,int y,int width,int height) const
    {
        return {juce::roundToInt(shiftX()+x*scale()),
                juce::roundToInt(shiftY()+y*scale()),
                juce::jmax(1,juce::roundToInt(width*scale())),
                juce::jmax(1,juce::roundToInt(height*scale()))};
    }
    void setupSlider(juce::Slider& slider,int index,bool main,bool linear=false)
    {
        auto& p=processor_.getC3Parameter(index);
        slider.setName(p.getName(80));
        slider.setTooltip(p.getName(80));
        slider.setSliderStyle(linear
            ? juce::Slider::LinearHorizontal : juce::Slider::RotaryVerticalDrag);
        slider.setRange(p.fromNormalised(0.0f),p.fromNormalised(1.0f),0.0);
        if(main)
        {
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,92,20);
            slider.setColour(juce::Slider::textBoxBackgroundColourId,
                             juce::Colour(0xff262326));
            slider.setColour(juce::Slider::textBoxOutlineColourId,
                             juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxTextColourId,
                             juce::Colour(0xfff3d685));
        }
        else slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        slider.setValue(p.getUnits(),juce::dontSendNotification);
        slider.setDoubleClickReturnValue(true,p.fromNormalised(p.getDefaultValue()));
        slider.onDragStart=[this,index] {
            rememberUndo();
            processor_.getC3Parameter(index).beginChangeGesture();
        };
        slider.onValueChange=[this,&slider,index] {
            auto& target=processor_.getC3Parameter(index);
            target.setValueNotifyingHost(target.toNormalised((float)slider.getValue()));
        };
        slider.onDragEnd=[this,index] { processor_.getC3Parameter(index).endChangeGesture(); };
        addAndMakeVisible(slider);
    }

    void paintScope(juce::Graphics& g)
    {
        // Main production-style level history, display at 50..490 with
        // current controls floating on the graph (as in manual p.6).
        const float top=53.0f,bottom=304.0f,left=20.0f,right=1007.0f;
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff29272c),left,top,
            juce::Colour(0xff1c1b20),left,bottom,false));
        g.fillRect(left,top,right-left,437.0f);
        g.setColour(juce::Colour(0xff3c383c));
        for(int db=0;db>=-60;db-=6)
        {
            float y=top+(0-db)/66.0f*(bottom-top);
            g.drawHorizontalLine(juce::roundToInt(y),left,right);
            g.setColour(juce::Colour(0xff797277));
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(juce::String(db),984,juce::roundToInt(y)-8,23,16,
                       juce::Justification::centredRight);
            g.setColour(juce::Colour(0xff38343a));
        }
        for(int i=1;i<=9;++i)
            g.drawVerticalLine((int)(left+i*(right-left)/10),top,bottom);

        // Underlay is based on actual DSP processed level history, not noise
        // waveforms invented for visual decoration.
        const auto count=processor_.meterHistoryCount();
        const auto frames=std::min<std::uint32_t>(
            count,(std::uint32_t)Processor::kMeterHistoryLength);
        juce::Path inPath,outPath,grPath;
        if(frames>1)
        {
            for(std::uint32_t i=0;i<frames;++i)
            {
                float in=-120.0f,out=-120.0f,gr=0.0f;
                processor_.meterHistoryFrame(count-frames+i,in,out,gr);
                const float x=left+(right-left)*i/(float)(frames-1);
                const auto levelY=[top,bottom](float db) {
                    return juce::jlimit(top,bottom,
                        top+(0.0f-db)/66.0f*(bottom-top));
                };
                const float iy=levelY(in),oy=levelY(out);
                const float gy=top+8.0f+juce::jlimit(0.0f,36.0f,gr)*2.6f;
                if (i==0) {inPath.startNewSubPath(x,iy);outPath.startNewSubPath(x,oy);
                            grPath.startNewSubPath(x,gy);}
                else {inPath.lineTo(x,iy);outPath.lineTo(x,oy);grPath.lineTo(x,gy);}
            }
            g.setColour(juce::Colour(0xff66666b).withAlpha(0.55f));
            g.strokePath(inPath,juce::PathStrokeType(1.4f));
            g.setColour(juce::Colour(0xffc4c2c2).withAlpha(0.87f));
            g.strokePath(outPath,juce::PathStrokeType(1.65f));
            g.setColour(juce::Colour(0xfff07171));
            g.strokePath(grPath,juce::PathStrokeType(2.0f));
        }
        g.setColour(juce::Colour(0xff878188));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("LEVEL HISTORY   /   INPUT + OUTPUT + RED GAIN REDUCTION",34,62,610,18,
                   juce::Justification::left);

        // Real threshold/ratio/knee transfer computed from hosted parameters.
        // This overlay corresponds to input vs processed level, not an EQ.
        const float thresh=processor_.getC3Parameter(Threshold).getUnits();
        const float ratio=processor_.getC3Parameter(Ratio).getUnits();
        const float knee=processor_.getC3Parameter(Knee).getUnits();
        const float range=processor_.getC3Parameter(Range).getUnits();
        juce::Path transfer;
        for(int i=0;i<=120;++i)
        {
            float db=-60.0f+60.0f*i/120.0f;
            float over=db-thresh;
            float slope=1.0f-1.0f/juce::jmax(1.0f,ratio);
            float reduce=0.0f;
            if(knee<0.01f)reduce=slope*juce::jmax(0.0f,over);
            else if(over>=knee*0.5f) reduce=slope*over;
            else if(over>-knee*0.5f)
                reduce=slope*(over+knee*0.5f)*(over+knee*0.5f)/(2.0f*knee);
            reduce=juce::jlimit(0.0f,range,reduce);
            const float x=28.0f+(520.0f*i/120.0f);
            const float y=bottom-(db-reduce+60.0f)/60.0f*208.0f;
            if(i==0)transfer.startNewSubPath(x,y);
            else transfer.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xffd0d74c).withAlpha(0.78f));
        g.strokePath(transfer,juce::PathStrokeType(1.7f));

        // Main control floor: one integrated surface, not a bank of cards.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0x00201f23),20,306,
            juce::Colour(0xee201e23),20,490,false));
        g.fillRect(20,303,987,187);
        g.setColour(juce::Colour(0xff3d3639));
        g.drawHorizontalLine(485,20,1078);
    }

    void paintKnobLabels(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xffd6cbc0));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        constexpr std::array<const char*,4> labels {{
            "THRESHOLD","RATIO","ATTACK","RELEASE"
        }};
        constexpr std::array<int,4> x {{68,242,569,746}};
        for(int i=0;i<4;++i)
            g.drawText(labels[static_cast<std::size_t>(i)],x[static_cast<std::size_t>(i)],
                       422,125,17,juce::Justification::centred);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("STYLE",409,337,123,18,juce::Justification::centred);
        g.drawText("WET GAIN",939,394,93,16,juce::Justification::centred);
        g.drawText("DRY",953,473,65,16,juce::Justification::centred);
        g.setColour(juce::Colour(0xffe3c16a));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("CHARACTER",416,403,113,14,juce::Justification::centred);
    }

    void paintMiniLabels(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xffa6a0a0));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        constexpr std::array<const char*,4> labels {{
            "KNEE","RANGE","HOLD","DRIVE"
        }};
        constexpr std::array<int,4> xs {{73,243,582,752}};
        for(int i=0;i<4;++i)
        {
            const int p=kMiniParams[static_cast<std::size_t>(i)];
            const float val=processor_.getC3Parameter(p).getUnits();
            g.drawText(labels[static_cast<std::size_t>(i)]+
                       juce::String("   ")+juce::String(val,1),
                       xs[static_cast<std::size_t>(i)],475,125,15,
                       juce::Justification::centred);
        }
    }

    void paintMeter(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff151418));g.fillRect(1010,51,70,438);
        const float output=processor_.outputLevel(), gr=processor_.gainReduction();
        const float level=juce::jlimit(0.0f,1.0f,(output+66.0f)/72.0f);
        const float reduction=juce::jlimit(0.0f,1.0f,gr/30.0f);
        g.setColour(juce::Colour(0xff4e494c));
        g.fillRect(1022,84,11,350);g.fillRect(1048,84,11,350);
        g.setColour(juce::Colour(0xffa5ac50));
        g.fillRect(1048.0f,84+350*(1-level),11.0f,350*level);
        g.setColour(juce::Colour(0xffe77470));
        g.fillRect(1022.0f,84.0f,11.0f,350*reduction);
        g.setColour(juce::Colour(0xffbcb2b3));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("GR",1018,62,22,17,juce::Justification::centred);
        g.drawText("OUT",1042,62,30,17,juce::Justification::centred);
        g.drawText(juce::String(gr,1),1015,444,27,18,juce::Justification::centred);
        g.drawText(juce::String(output,1),1044,444,32,18,juce::Justification::centred);
        g.drawText("dB",1032,462,22,16,juce::Justification::centred);
    }

    static float freqX(float frequency)
    {
        const float f=juce::jlimit(20.0f,20000.0f,frequency);
        return 178.0f+826.0f*(std::log(f/20.0f)/std::log(1000.0f));
    }
    static float freqAtX(float x)
    {
        return 20.0f*std::pow(1000.0f,juce::jlimit(0.0f,1.0f,(x-178)/826.0f));
    }
    // Complex z^-1 frequency response matches the *actual* RBJ detector
    // coefficients and existing one-pole HP/LP stages. GUI cannot substitute
    // an invented decorative EQ shape for these six real DSP filters.
    float detectorDbAt(float frequency) const
    {
        const double sampleRate=std::max(8000.0,processor_.getSampleRate());
        const double f=std::clamp((double)frequency,20.0,sampleRate*0.45);
        const double theta=6.283185307179586*f/sampleRate;
        const std::complex<double> z=std::polar(1.0,-theta);
        std::complex<double> response(1.0,0.0);
        const double hp=processor_.getC3Parameter(SidechainHP).getUnits();
        const double lp=processor_.getC3Parameter(SidechainLP).getUnits();
        if(hp>0.0)
        {
            const double c=std::exp(-6.283185307179586
                *std::min(hp,sampleRate*0.499)/sampleRate);
            response*=c*(1.0-z)/(1.0-c*z);
        }
        if(lp<19999.0)
        {
            const double c=std::exp(-6.283185307179586
                *std::min(lp,sampleRate*0.499)/sampleRate);
            response*=(1.0-c)/(1.0-c*z);
        }
        for(int band=0;band<4;++band)
        {
            const int start=SC1Freq+3*band;
            const double gain=processor_.getC3Parameter(start+1).getUnits();
            if(std::abs(gain)<1.0e-6)continue;
            const double center=std::clamp(
                (double)processor_.getC3Parameter(start).getUnits(),
                20.0,sampleRate*0.45);
            const double q=processor_.getC3Parameter(start+2).getUnits();
            const double w=6.283185307179586*center/sampleRate;
            const double cs=std::cos(w),a=std::pow(10.0,gain/40.0);
            const double alpha=std::sin(w)/(2.0*q);
            const double a0=1.0+alpha/a;
            const double b0=(1.0+alpha*a)/a0;
            const double b1=(-2.0*cs)/a0;
            const double b2=(1.0-alpha*a)/a0;
            const double a1=(-2.0*cs)/a0;
            const double a2=(1.0-alpha/a)/a0;
            response*=(b0+b1*z+b2*z*z)/(1.0+a1*z+a2*z*z);
        }
        return (float)(20.0*std::log10(std::max(1.0e-12,std::abs(response))));
    }
    float nodeY(int index) const
    {
        const float middle=589.0f;
        if(index>=SC1Freq&&index<=SC4Freq)
            return juce::jlimit(539.0f,639.0f,
                middle-processor_.getC3Parameter(index+1).getUnits()*2.6f);
        const float f=index==SidechainHP
            ? std::max(20.0f,processor_.getC3Parameter(index).getUnits())
            : processor_.getC3Parameter(index).getUnits();
        return juce::jlimit(539.0f,639.0f,
            middle-detectorDbAt(f)*2.8f);
    }
    int closestDetectorNode(float x,float y,float radius) const
    {
        constexpr std::array<int,6> params {{
            SidechainHP,SC1Freq,SC2Freq,SC3Freq,SC4Freq,SidechainLP
        }};
        int nearest=-1;
        float best=radius*radius;
        for(int idx:params)
        {
            float f=processor_.getC3Parameter(idx).getUnits();
            if(idx==SidechainHP&&f<=0.0f)f=20.0f;
            const float dx=x-freqX(f),dy=y-nodeY(idx);
            const float d=dx*dx+dy*dy;
            if(d<=best){best=d;nearest=idx;}
        }
        return nearest;
    }
    void dragEqNode(float x,float y)
    {
        if(activeEqNode_<0)return;
        float f=freqAtX(x);
        if(activeEqNode_==SidechainHP)
            f=juce::jlimit(0.0f,2000.0f,f);
        else if(activeEqNode_==SidechainLP)
            f=juce::jlimit(100.0f,20000.0f,f);
        auto& p=processor_.getC3Parameter(activeEqNode_);
        p.setValueNotifyingHost(p.toNormalised(f));
        if(activeEqNode_>=SC1Freq && activeEqNode_<=SC4Freq)
        {
            const int band=(activeEqNode_-SC1Freq)/3;
            selectedBell_=band;
            const float db=juce::jlimit(-18.0f,18.0f,
                (589.0f-y)/2.6f);
            auto& gain=processor_.getC3Parameter(activeEqNode_+1);
            gain.setValueNotifyingHost(gain.toNormalised(db));
        }
        else
        {
            for(int i=0;i<5;++i)
                if(kUtilityParams[static_cast<std::size_t>(i)]==activeEqNode_)
                    utilities_[static_cast<std::size_t>(i)].setValue(
                        f,juce::dontSendNotification);
        }
        repaint(rect(155,519,851,184));
    }

    void paintSidechain(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff151419));g.fillRect(0,519,1080,184);
        g.setColour(juce::Colour(0xff4c474c));g.drawHorizontalLine(518,0,1080);
        g.drawVerticalLine(154,525,692);
        g.setColour(juce::Colour(0xff9b9291));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("TRIGGER INPUT",20,521,130,17,juce::Justification::centred);
        g.drawText("STEREO LINK",20,575,130,18,juce::Justification::centred);
        g.drawText("HP FREQUENCY",20,631,130,17,juce::Justification::centred);
        g.drawText("LOW-PASS",230,635,160,14,juce::Justification::centred);
        g.drawText("INPUT TRIM",742,635,145,14,juce::Justification::centred);
        g.drawText("OUTPUT TRIM",891,635,120,14,juce::Justification::centred);

        const float top=539.0f,bottom=639.0f;
        g.setColour(juce::Colour(0xff343037));
        for(int i=0;i<=5;++i)
            g.drawHorizontalLine((int)(top+i*(bottom-top)/5),178,1005);
        constexpr std::array<float,4> freqs {{100.0f,1000.0f,10000.0f,20000.0f}};
        for(auto f:freqs)
        {
            float x=freqX(f);
            g.drawVerticalLine((int)x,top,bottom);
            g.setFont(juce::Font(juce::FontOptions(9.0f)));
            g.setColour(juce::Colour(0xff777074));
            g.drawText(juce::String((int)f)+" Hz",(int)x-35,641,70,15,
                       juce::Justification::centred);
            g.setColour(juce::Colour(0xff343037));
        }
        // Zero dB is the true center line. Gain boosts extend above it
        // and cuts below it. All six filters affect the live detector.
        constexpr float baseline=589.0f;
        g.setColour(juce::Colour(0xff565054));
        g.drawHorizontalLine((int)baseline,178,1005);
        juce::Path response;
        for(int i=0;i<=260;++i)
        {
            const float x=178.0f+i*826.0f/260.0f;
            const float f=freqAtX(x);
            const float db=detectorDbAt(f);
            const float y=juce::jlimit(top,bottom,baseline-db*2.6f);
            if(i==0)response.startNewSubPath(x,y);
            else response.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xfff4bc4c));
        g.strokePath(response,juce::PathStrokeType(2.6f));

        // Genuine draggable high-pass, four parametric bells and low-pass.
        // Unlike a decorative Pro-Q clone, every circle is tied to real DSP.
        constexpr std::array<int,6> nodeParams {{
            SidechainHP,SC1Freq,SC2Freq,SC3Freq,SC4Freq,SidechainLP
        }};
        const std::array<juce::Colour,6> nodeColors {{
            juce::Colour(0xffd87983),juce::Colour(0xff71c884),
            juce::Colour(0xff65bed5),juce::Colour(0xffda83ce),
            juce::Colour(0xffe8ba60),juce::Colour(0xff8c9af0)
        }};
        for(int i=0;i<6;++i)
        {
            const int idx=nodeParams[static_cast<std::size_t>(i)];
            float f=processor_.getC3Parameter(idx).getUnits();
            if(idx==SidechainHP&&f<=0.0f)f=20.0f;
            const float x=freqX(f), y=nodeY(idx);
            const bool selected=i>=1&&i<=4&&(i-1)==selectedBell_;
            const float r=selected ? 8.5f : 7.0f;
            g.setColour(juce::Colour(0xff111116));
            g.fillEllipse(x-r-2,y-r-2,2*(r+2),2*(r+2));
            g.setColour(nodeColors[static_cast<std::size_t>(i)]);
            g.fillEllipse(x-r,y-r,2*r,2*r);
            g.setColour(juce::Colour(0xff19191d));
            g.setFont(juce::Font(juce::FontOptions(10.0f,juce::Font::bold)));
            g.drawText(i==0 ? "H" : i==5 ? "L" : juce::String(i),
                (int)(x-r),(int)(y-r), (int)(2*r),(int)(2*r),
                juce::Justification::centred);
        }
        const int selected=SC1Freq+3*selectedBell_;
        const auto freq=processor_.getC3Parameter(selected).getUnits();
        const auto gain=processor_.getC3Parameter(selected+1).getUnits();
        const auto q=processor_.getC3Parameter(selected+2).getUnits();
        g.setColour(juce::Colour(0xffc5b9a9));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("SIX DETECTOR FILTERS   /   DRAG: FREQ + GAIN   /   WHEEL: Q",
                   170,523,670,16,juce::Justification::centredLeft);
        g.drawText("BAND "+juce::String(selectedBell_+1)+"   "
                   +juce::String(freq,0)+" Hz    "
                   +juce::String(gain,1)+" dB    Q "
                   +juce::String(q,2),
                   492,671,516,18,juce::Justification::centredRight);
        const bool wantsExternal=processor_.getC3Parameter(ExternalSidechain).getUnits()>0.5f;
        g.setColour(wantsExternal
            ? (processor_.externalSidechainRouted()
                ? juce::Colour(0xff99daab) : juce::Colour(0xffffa369))
            : juce::Colour(0xffb0a3a6));
        g.drawText(wantsExternal
            ? (processor_.externalSidechainRouted() ? "EXT ROUTED" : "EXT NOT ROUTED")
            : "INTERNAL",
            795,523,208,17,juce::Justification::centredRight);
    }

    void paintFooter(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff111014)); g.fillRect(0,703,1080,37);
        g.setColour(juce::Colour(0xff3b353b));g.drawHorizontalLine(703,0,1080);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xffa59aa0));
        g.drawText("HOST PARAMETER AUTOMATION",16,710,205,21,juce::Justification::left);
        g.drawText("Oversampling: Off (pending)",242,710,180,21,juce::Justification::left);
        g.drawText("Lookahead: Off (pending)",458,710,183,21,juce::Justification::left);
        g.drawText("PRECISION  /  APEX NATIVE",654,710,236,21,juce::Justification::right);
    }

    void setDiscrete(int idx,int value)
    {
        if(value<0)return;
        rememberUndo();
        auto& p=processor_.getC3Parameter(idx);
        p.beginChangeGesture();p.setValueNotifyingHost(p.toNormalised((float)value));p.endChangeGesture();
    }
    using Snapshot=std::array<float,ParameterCount>;
    Snapshot snapshot() const
    {
        Snapshot result{};
        for(int i=0;i<ParameterCount;++i)
            result[static_cast<std::size_t>(i)]=const_cast<Processor&>(processor_).getC3Parameter(i).getValue();
        return result;
    }
    void apply(const Snapshot& data)
    {
        for(int i=0;i<ParameterCount;++i)
        {
            auto& p=processor_.getC3Parameter(i);
            if(std::abs(p.getValue()-data[static_cast<std::size_t>(i)])<1.0e-7f)continue;
            p.beginChangeGesture();p.setValueNotifyingHost(data[static_cast<std::size_t>(i)]);
            p.endChangeGesture();
        }
        syncControls();repaint();
    }
    void rememberUndo() {undoState_=snapshot();hasUndo_=true;hasRedo_=false;}
    void swapAB()
    {
        snapshots_[activeSnapshot_]=snapshot();
        activeSnapshot_=1-activeSnapshot_;
        apply(snapshots_[activeSnapshot_]);
        aB_.setButtonText(activeSnapshot_==0?"A / B    A":"A / B    B");
    }
    void loadPreset(int id)
    {
        if(id<1)return;
        rememberUndo();
        auto data=snapshot();
        auto set=[&](int index,float v)
        {
            auto& p=processor_.getC3Parameter(index);
            data[static_cast<std::size_t>(index)]=p.toNormalised(v);
        };
        if(id==1) {set(Threshold,-18);set(Ratio,4);set(Attack,10);set(Release,120);
                   set(Knee,12);set(Hold,0);set(Style,0);}
        if(id==2) {set(Threshold,-22);set(Ratio,3);set(Attack,12);set(Release,100);
                   set(Knee,16);set(Style,10);}
        if(id==3) {set(Threshold,-16);set(Ratio,6);set(Attack,17);set(Release,80);
                   set(Knee,8);set(Style,3);}
        if(id==4) {set(Threshold,-10);set(Ratio,2);set(Attack,30);set(Release,200);
                   set(Knee,24);set(Style,12);}
        apply(data);
    }
    void refreshToggleColours()
    {
        for(int i=0;i<4;++i)
        {
            auto& b=toggles_[static_cast<std::size_t>(i)];
            const bool on=processor_.getC3Parameter(kToggleParams[static_cast<std::size_t>(i)]).getUnits()>=0.5f;
            b.setColour(juce::TextButton::buttonColourId,
                        on ? juce::Colour(0xff706039) : juce::Colour(0xff29262a));
            b.setColour(juce::TextButton::textColourOffId,
                        on ? juce::Colour(0xffffda7a) : juce::Colour(0xffb6adb0));
        }
    }
    void updateCharacterRouting()
    {
        const bool pre=processor_.getC3Parameter(CharacterPre).getUnits() >= 0.5f;
        charPrePost_.setButtonText(pre ? "CHARACTER   PRE" : "CHARACTER   POST");
        charPrePost_.setColour(juce::TextButton::buttonColourId,
            pre ? juce::Colour(0xff564425) : juce::Colour(0xff302a30));
        charPrePost_.setColour(juce::TextButton::textColourOffId,
            pre ? juce::Colour(0xffffd169) : juce::Colour(0xffd2b879));
    }
    void syncControls()
    {
        for(int i=0;i<6;++i)
            knobs_[static_cast<std::size_t>(i)].setValue(
                processor_.getC3Parameter(kKnobParams[static_cast<std::size_t>(i)]).getUnits(),
                juce::dontSendNotification);
        for(int i=0;i<4;++i)
            minis_[static_cast<std::size_t>(i)].setValue(
                processor_.getC3Parameter(kMiniParams[static_cast<std::size_t>(i)]).getUnits(),
                juce::dontSendNotification);
        for(int i=0;i<5;++i)
            utilities_[static_cast<std::size_t>(i)].setValue(
                processor_.getC3Parameter(kUtilityParams[static_cast<std::size_t>(i)]).getUnits(),
                juce::dontSendNotification);
        style_.setSelectedId(1+(int)std::lround(processor_.getC3Parameter(Style).getUnits()),
                             juce::dontSendNotification);
        character_.setSelectedId(1+(int)std::lround(processor_.getC3Parameter(Character).getUnits()),
                                 juce::dontSendNotification);
        triggerSource_.setSelectedId(1+(int)std::lround(
            processor_.getC3Parameter(ExternalSidechain).getUnits()),
            juce::dontSendNotification);
        refreshToggleColours();
        updateCharacterRouting();
    }
    void timerCallback() override
    {
        for(int i=0;i<6;++i)
            if(!knobs_[static_cast<std::size_t>(i)].isMouseButtonDown())
                knobs_[static_cast<std::size_t>(i)].setValue(
                    processor_.getC3Parameter(kKnobParams[static_cast<std::size_t>(i)]).getUnits(),
                    juce::dontSendNotification);
        for(int i=0;i<4;++i)
            if(!minis_[static_cast<std::size_t>(i)].isMouseButtonDown())
                minis_[static_cast<std::size_t>(i)].setValue(
                    processor_.getC3Parameter(kMiniParams[static_cast<std::size_t>(i)]).getUnits(),
                    juce::dontSendNotification);
        for(int i=0;i<5;++i)
            if(!utilities_[static_cast<std::size_t>(i)].isMouseButtonDown())
                utilities_[static_cast<std::size_t>(i)].setValue(
                    processor_.getC3Parameter(kUtilityParams[static_cast<std::size_t>(i)]).getUnits(),
                    juce::dontSendNotification);
        style_.setSelectedId(1+(int)std::lround(processor_.getC3Parameter(Style).getUnits()),
                             juce::dontSendNotification);
        character_.setSelectedId(1+(int)std::lround(processor_.getC3Parameter(Character).getUnits()),
                                 juce::dontSendNotification);
        triggerSource_.setSelectedId(1+(int)std::lround(
            processor_.getC3Parameter(ExternalSidechain).getUnits()),
            juce::dontSendNotification);
        refreshToggleColours();
        updateCharacterRouting();
        repaint();
    }

    Processor& processor_;
    PrecisionLookAndFeel lookAndFeel_;
    std::array<juce::Slider,6> knobs_{};
    std::array<juce::Slider,4> minis_{};
    std::array<juce::Slider,5> utilities_{};
    std::array<juce::TextButton,4> toggles_{};
    juce::TextButton scToggle_,aB_,copy_,undo_,redo_,charPrePost_;
    juce::ComboBox style_,character_,preset_,triggerSource_;
    std::array<Snapshot,2> snapshots_{};
    Snapshot undoState_{},redoState_{};
    bool hasUndo_=false,hasRedo_=false;
    int activeSnapshot_=0,activeEqNode_=-1,selectedBell_=1;
    bool sidechainOpen_=true;
};

inline juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor(*this);
}
} // namespace APEX::PrecisionC3

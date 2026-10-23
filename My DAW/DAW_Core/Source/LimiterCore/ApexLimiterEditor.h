#pragma once
// APEX Bubblegum Limiter. Original JUCE implementation, visually inspired by
// the professional limiter WORKFLOW, not FabFilter branding or proprietary art.
// The UI never enters the realtime audio callback. All graph data comes from
// atomically published DSP readings; there are no simulated meter values.
#include <JuceHeader.h>
#include "ApexLimiterProcessor.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <utility>

namespace APEX { namespace Limiter {

class LimiterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LimiterLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff252a38));
        setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff414756));
        setColour(juce::ComboBox::textColourId,juce::Colour(0xffe3e7f2));
        setColour(juce::TextButton::buttonColourId,juce::Colour(0xff282e3b));
        setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff95527d));
        setColour(juce::TextButton::textColourOffId,juce::Colour(0xffdee1eb));
        setColour(juce::TextButton::textColourOnId,juce::Colours::white);
        setColour(juce::PopupMenu::backgroundColourId,juce::Colour(0xff1f2530));
        setColour(juce::PopupMenu::textColourId,juce::Colour(0xffe5e8f0));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float position,float start,float end,juce::Slider&) override
    {
        const float size=static_cast<float>(juce::jmin(width,height));
        const float radius=size*0.35f;
        const float cx=x+width*0.5f;
        const float cy=y+height*0.42f;
        g.setColour(juce::Colour(0xff121722));
        g.fillEllipse(cx-radius-4,cy-radius-4,2*(radius+4),2*(radius+4));
        g.setColour(juce::Colour(0xff3d475b));
        g.fillEllipse(cx-radius,cy-radius,2*radius,2*radius);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff9ea8c4),
            cx-radius,cy-radius,juce::Colour(0xff4e5972),cx+radius,cy+radius,false));
        g.fillEllipse(cx-radius+4,cy-radius+4,2*(radius-4),2*(radius-4));
        juce::Path rail, active;
        const float arcRadius=radius+3;
        rail.addCentredArc(cx,cy,arcRadius,arcRadius,0,start,end,true);
        active.addCentredArc(cx,cy,arcRadius,arcRadius,0,start,
                             start+position*(end-start),true);
        g.setColour(juce::Colour(0xff171d2a));
        g.strokePath(rail,juce::PathStrokeType(4.5f));
        g.setColour(juce::Colour(0xfff078b1));
        g.strokePath(active,juce::PathStrokeType(3.1f));
        const float angle=start+position*(end-start);
        const float tipX=cx+std::sin(angle)*radius*0.68f;
        const float tipY=cy-std::cos(angle)*radius*0.68f;
        g.setColour(juce::Colour(0xfff4f2f7));
        g.drawLine(cx,cy,tipX,tipY,2.7f);
        g.fillEllipse(cx-2,cy-2,4,4);
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor& p) : juce::AudioProcessorEditor(&p), processor_(p)
    {
        setOpaque(true);
        setLookAndFeel(&skin_);
        setup(gain_,Gain,"GAIN",0,30,0.01,true);
        setup(ceiling_,Ceiling,"OUTPUT",-30,0,0.01,false);
        setup(lookahead_,Lookahead,"LOOKAHEAD",0.1,20,0.1,false);
        setup(attack_,Attack,"ATTACK",0.01,1000,0.01,false);
        setup(release_,Release,"RELEASE",10,3000,1,false);
        setup(link_,ChannelLink,"TRANSIENT LINK",0,100,1,false);
        setup(releaseLink_,ChannelLinkRelease,"RELEASE LINK",0,100,1,false);

        style_.addItemList({"Transparent","Punchy","Dynamic","Allround",
                            "Aggressive","Modern","Bus","Safe"},1);
        style_.setTooltip("Eight distinct APEX limiting profiles.");
        style_.setSelectedId(static_cast<int>(param(LimitingStyle).units())+1,
                             juce::dontSendNotification);
        style_.onChange=[this] { selectParameter(LimitingStyle,style_.getSelectedId()-1); };
        addAndMakeVisible(style_);

        // This is inter-sample DETECTOR quality, not full audio oversampling.
        detector_.addItemList({"1x","2x","4x","8x"},1);
        detector_.setTooltip("ISP detector only (1x–8x). AUDIO OS is a separate experimental high-rate DSP path.");
        detector_.setSelectedId(static_cast<int>(param(DetectorQuality).units())+1,
                                juce::dontSendNotification);
        detector_.onChange=[this] { selectParameter(DetectorQuality,detector_.getSelectedId()-1); };
        addAndMakeVisible(detector_);
        audioOs_.addItemList({"1x","2x","4x","8x","16x","32x"},1);
        audioOs_.setTooltip("Experimental REAL audio oversampling. Requires audio engine/plugin restart to apply (PDC cannot change inside playback). At >1x, dither is applied at host sample rate after decimation. Mode takes effect at audio engine restart.");
        audioOs_.setSelectedId(static_cast<int>(param(AudioOversampling).units())+1,
                               juce::dontSendNotification);
        audioOs_.onChange=[this] {selectParameter(AudioOversampling,audioOs_.getSelectedId()-1);};
        addAndMakeVisible(audioOs_);
        dither_.addItemList({"Off","TPDF","Shaped"},1);
        dither_.setTooltip("Final 16/24-bit triangular dither or bounded first-order noise shaping.");
        dither_.setSelectedId(static_cast<int>(param(DitherMode).units())+1,juce::dontSendNotification);
        dither_.onChange=[this] {selectParameter(DitherMode,dither_.getSelectedId()-1);};
        addAndMakeVisible(dither_);
        bits_.addItemList({"16","24"},1);
        bits_.setTooltip("Target word length for optional final dither.");
        bits_.setSelectedId(static_cast<int>(param(DitherBits).units())+1,juce::dontSendNotification);
        bits_.onChange=[this]{selectParameter(DitherBits,bits_.getSelectedId()-1);};
        addAndMakeVisible(bits_);

        setupToggle(tp_,TruePeak,"ISP","Inter-sample peak estimator; not certified true peak.");
        setupToggle(bypass_,Bypass,"BYPASS","Click-free latency-compensated bypass.");
        setupToggle(unity_,UnityGain,"UNITY","Audition with gain-compensation.");
        setupToggle(audition_,Audition,"DELTA","Listen to the difference signal.");
        setupToggle(dc_,DcFilter,"DC","High-pass DC-offset filter.");

        configureButton(bankA_,"A",[this]{ switchBank(0); });
        configureButton(bankB_,"B",[this]{ switchBank(1); });
        configureButton(copy_,"COPY",[this]{
            banks_[static_cast<size_t>(1-activeBank_)]=snapshot();
            updateBankButtons();
        });
        configureButton(resetMeter_,"RESET",[this]{
            heldInput_=heldOutput_=heldReduction_=0;
            repaint();
        });
        configureButton(advanced_,"ADVANCED",[this]{
            expanded_=!expanded_;
            resized();
            repaint();
        });
        for(int b=0;b<2;++b) banks_[static_cast<size_t>(b)]=snapshot();
        updateBankButtons();
        setResizable(true,true);
        setResizeLimits(820,520,1840,1080);
        setSize(1060,640);
        startTimerHz(30);
    }
    ~Editor() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    juce::Rectangle<int> getGraphRegionForTest() const noexcept { return chartBounds(); }
    juce::Rectangle<int> getMeterRegionForTest() const noexcept { return meterBounds(); }
    juce::Rectangle<int> getAdvancedRegionForTest() const noexcept { return advancedBounds(); }
    // Exact pixels occupied by live peak values and the GR/LUFS summary.
    // Used by Windows JUCE capture tests to prevent label overlap at 820px.
    juce::Rectangle<int> getPeakReadoutRegionForTest() const noexcept
    {
        const auto m=meterBounds();
        return {m.getX()+17,m.getBottom()-111,m.getWidth()-20,18};
    }
    juce::Rectangle<int> getLoudnessReadoutRegionForTest() const noexcept
    {
        const auto m=meterBounds();
        return {m.getX()+6,m.getBottom()-80,m.getWidth()-10,80};
    }

    void paint(juce::Graphics& g) override
    {
        const int w=getWidth(),h=getHeight();
        const auto chart=chartBounds().toFloat();
        const auto meter=meterBounds().toFloat();
        const auto advance=advancedBounds().toFloat();
        g.fillAll(juce::Colour(0xff131722));
        g.setColour(juce::Colour(0xff20232c));
        g.fillRect(0,0,w,56);
        g.setColour(juce::Colour(0xff2e313d));
        g.drawLine(0,55,(float)w,55,1.0f);
        g.setColour(juce::Colour(0xfff164a8));
        g.fillRoundedRectangle(19,16,5,25,2.0f);
        label(g,"APEX",31,10,95,29,23,0xfff2eef3,true);
        label(g,"/ BUBBLEGUM LIMITER",118,12,270,26,19,0xffe474a8,true);
        label(g,"NATIVE MASTERING",31,36,230,15,10,0xff8c95a9,false);
        label(g,"DITHER",w-323,3,87,18,9,0xffabb5c8,true);
        label(g,"A/B",w-262,13,41,30,12,0xff9fa9b9,true);

        // Pro-L-like information hierarchy: gain rail at left, full-height
        // scrolling INPUT / OUTPUT / GR history, metering stack at right,
        // the optional advanced controls underneath, and utility footer.
        g.setColour(juce::Colour(0xff1d2230));
        g.fillRoundedRectangle(chart,5);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff272a36),chart.getX(),chart.getY(),
                         juce::Colour(0xff565d72),chart.getX(),chart.getBottom(),false));
        g.fillRoundedRectangle(chart.reduced(2),3);
        g.setColour(juce::Colour(0xff68718a).withAlpha(0.33f));
        g.drawRoundedRectangle(chart,5,0.7f);

        for(int row=0;row<=6;++row)
        {
            const float yy=chart.getY()+2+row*(chart.getHeight()-4)/6.0f;
            g.setColour(juce::Colour(0xffd3dae9).withAlpha(row==0?0.23f:0.14f));
            g.drawHorizontalLine(juce::roundToInt(yy),chart.getX()+3,chart.getRight()-3);
            const int db=-row*6;
            label(g,juce::String(db)+" dB",chart.getRight()-52,yy+2,48,15,10,0xffd3d6e2,false);
        }
        for(int col=1;col<10;++col)
        {
            const float xx=chart.getX()+col*chart.getWidth()/10.0f;
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawVerticalLine(juce::roundToInt(xx),chart.getY()+4,chart.getBottom()-4);
        }

        // LIVE history from the audio callback, not a fabricated waveform.
        // Overlaid pale-blue input, blue output, magenta gain reduction.
        const size_t steps=juce::jmin(static_cast<int>(Processor::kDisplayHistory),
                                      juce::jmax(1,juce::roundToInt(chart.getWidth())));
        // Two truthful *envelope curves*, not hundreds of bottom-anchored
        // opaque bars. Adjacent block peaks are joined with JUCE paths and
        // reduction is drawn separately along the top. All samples are from
        // processBlock()'s atomic telemetry.
        juce::Path inputCurve,outputCurve,gainReduction;
        bool started=false;
        for(size_t i=0;i<steps;++i)
        {
            const auto frame=processor_.displayFrame(steps-i-1);
            const float x=chart.getX()+(static_cast<float>(i)/(steps-1))*(chart.getWidth()-1);
            auto levelY=[&](float peak)
            {
                const float db=20.0f*std::log10(juce::jmax(1.0e-6f,peak));
                const float fraction=juce::jlimit(0.0f,1.0f,(db+42.0f)/48.0f);
                return chart.getBottom()-4-fraction*(chart.getHeight()-10);
            };
            if(frame.input<=0.00001f) { started=false; continue; }
            const float inY=levelY(frame.input);
            const float outY=levelY(frame.output);
            const float redY=chart.getY()+4+
                juce::jlimit(0.0f,1.0f,frame.reduction/18.0f)*(chart.getHeight()*0.23f);
            if(!started)
            {
                inputCurve.startNewSubPath(x,inY);
                outputCurve.startNewSubPath(x,outY);
                gainReduction.startNewSubPath(x,redY);
                started=true;
            }
            else
            {
                inputCurve.lineTo(x,inY);
                outputCurve.lineTo(x,outY);
                gainReduction.lineTo(x,redY);
            }
        }
        g.setColour(juce::Colour(0xffb7c9e9).withAlpha(0.90f));
        g.strokePath(inputCurve,juce::PathStrokeType(1.7f));
        g.setColour(juce::Colour(0xff72a5cc).withAlpha(0.95f));
        g.strokePath(outputCurve,juce::PathStrokeType(2.0f));
        g.setColour(juce::Colour(0xfff65393));
        g.strokePath(gainReduction,juce::PathStrokeType(1.8f));
        label(g,"INPUT / OUTPUT / GAIN REDUCTION",chart.getX()+11,chart.getY()+7,
              chart.getWidth()-85,16,10,0xffe3e0ea,true);
        label(g,"Recent DSP history",chart.getX()+11,chart.getBottom()-24,
              chart.getWidth()-105,17,10,0xffd0c5ce,false);

        // The gain slider is deliberately outside the graph and labelled.
        label(g,"GAIN",25,64,66,19,13,0xffdce3ec,true);
        const float inGain=param(Gain).units();
        label(g,"+"+juce::String(inGain,2)+" dB",16,chart.getBottom()-22,
              78,20,12,0xffeeb2d2,true);

        // Output and gain-reduction meters, separate visible numeric scale.
        const float peakIn=processor_.meterInput();
        const float peakOut=processor_.meterOutput();
        const float reduction=processor_.meterReduction();
        const float dbIn=toDb(peakIn),dbOut=toDb(peakOut);
        heldInput_=juce::jmax(heldInput_,peakIn);
        heldOutput_=juce::jmax(heldOutput_,peakOut);
        heldReduction_=juce::jmax(heldReduction_,reduction);
        g.setColour(juce::Colour(0xff222634));
        g.fillRoundedRectangle(meter,5);
        label(g,"PEAK METER",meter.getX()+10,meter.getY()+7,meter.getWidth()-12,18,
              11,0xffced4e3,true);
        label(g,"INPUT",meter.getX()+10,meter.getY()+31,60,16,10,0xffafb9d4,true);
        label(g,"OUTPUT",meter.getX()+76,meter.getY()+31,61,16,10,0xffafb9d4,true);
        // Reserve separate rows for peak numbers, then GR/M/S/I/LRA.
        // Earlier versions placed peak numbers at precisely the GR baseline
        // and overprinted them at every supported window size.
        const float barTop=meter.getY()+78,barBottom=meter.getBottom()-114;
        const float barHeight=juce::jmax(25.0f,barBottom-barTop);
        const float barW=23.0f;
        for(int k=0;k<2;++k)
        {
            const float px=meter.getX()+32+k*65;
            g.setColour(juce::Colour(0xff111520));
            g.fillRoundedRectangle(px,barTop,barW,barHeight,2);
            const float value=k==0?dbIn:dbOut;
            const float fraction=juce::jlimit(0.0f,1.0f,(value+48.0f)/48.0f);
            const float y=barTop+barHeight*(1-fraction);
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(k==0?0xffef869f:0xffd7d9f4),px,y,
                juce::Colour(k==0?0xffb54870:0xff818eaf),px,barBottom,false));
            g.fillRoundedRectangle(px,y,barW,barHeight*fraction,2);
            // A peak-hold marker is independent of the moving live meter.
            // RESET clears both held peaks; the next live frame then starts
            // tracking a new peak rather than leaving a decorative button.
            const float held=k==0?heldInput_:heldOutput_;
            if(held>0.0f)
            {
                const float heldFraction=juce::jlimit(0.0f,1.0f,(toDb(held)+48.0f)/48.0f);
                const float heldY=barTop+barHeight*(1.0f-heldFraction);
                g.setColour(juce::Colour(k==0?0xfff9c0d3:0xfff3f4fe));
                g.drawLine(px-2.0f,heldY,px+barW+2.0f,heldY,2.0f);
            }
            label(g,juce::String(value,1),px-10,barBottom+3,47,18,11,0xffe8e5f1,true);
        }
        // Five truthful level/loudness summaries fit without covering the
        // lower advanced controls even at the touch-safe 820x520 minimum.
        const float lineStep=16.0f;
        const float topLine=meter.getBottom()-80.0f;
        g.setColour(juce::Colour(0xff697084).withAlpha(0.40f));
        g.drawLine(meter.getX()+9,topLine-8,meter.getRight()-9,topLine-8,0.75f);
        label(g,"GR",meter.getX()+8,topLine,30,15,10,0xffe799ad,true);
        label(g,juce::String(reduction,1)+" dB",
              meter.getX()+41,topLine,104,15,12,0xfff68f9c,true);
        label(g,"M",meter.getX()+8,topLine+lineStep,22,15,10,0xffd4d2e4,true);
        label(g,juce::String(processor_.meterMomentaryLufs(),1)+" LUFS",
              meter.getX()+32,topLine+lineStep,112,15,11,0xffc1cae7,true);
        label(g,"S",meter.getX()+8,topLine+2*lineStep,22,15,10,0xffd4d2e4,true);
        label(g,juce::String(processor_.meterShortTermLufs(),1)+" LUFS",
              meter.getX()+32,topLine+2*lineStep,112,15,11,0xffd2dfec,true);
        label(g,"I",meter.getX()+8,topLine+3*lineStep,22,15,10,0xffd4d2e4,true);
        label(g,juce::String(processor_.meterIntegratedLufs(),1)+" LUFS",
              meter.getX()+32,topLine+3*lineStep,112,15,11,0xffe7dce6,true);
        label(g,"LRA",meter.getX()+8,topLine+4*lineStep,28,15,10,0xffd4d2e4,true);
        label(g,juce::String(processor_.meterLoudnessRange(),1)+" LU",
              meter.getX()+41,topLine+4*lineStep,100,15,11,0xffe7dce6,true);

        g.setColour(juce::Colour(0xff23252c));
        g.fillRoundedRectangle(advance,5);
        g.setColour(juce::Colour(0xff3b3e49));
        g.drawRoundedRectangle(advance,5,0.7f);
        label(g,"STYLE",advance.getX()+16,advance.getY()+11,121,16,
              11,0xffadb4c3,true);
        label(g,"LOOKAHEAD",advance.getX()+164,advance.getY()+11,110,16,
              10,0xffc0c8d8,true);
        label(g,"ATTACK",advance.getX()+164+columnWidth(),advance.getY()+11,110,16,
              10,0xffc0c8d8,true);
        label(g,"RELEASE",advance.getX()+164+2*columnWidth(),advance.getY()+11,110,16,
              10,0xffc0c8d8,true);
        label(g,"TRANSIENT LINK",advance.getX()+164+3*columnWidth(),advance.getY()+11,
              columnWidth(),16,10,0xffc0c8d8,true);
        label(g,"RELEASE LINK",advance.getX()+164+4*columnWidth(),advance.getY()+11,
              columnWidth(),16,10,0xffc0c8d8,true);
        label(g,"APEX / eight styles",advance.getX()+15,advance.getBottom()-23,
              143,16,10,0xff8b98a9,false);

        // Keep footer independent of the advanced knobs and level meters.
        const int footerY=h-37;
        g.setColour(juce::Colour(0xff1d2029));
        g.fillRect(0,footerY,w,37);
        g.setColour(juce::Colour(0xff3f4554));
        g.drawLine(0,(float)footerY,(float)w,(float)footerY,1);
        label(g,"Powered by Bubblegum Engine",13,footerY+7,176,19,
              10,0xffb3a2b1,false);
        label(g,"ISP DETECTOR",w-302,footerY+8,93,19,10,0xffb9bdc9,true);
        label(g,"OS",w-145,footerY+8,35,19,10,0xffb9bdc9,true);
        // The ceiling readout is already attached to the top-right slider.
        // Duplicating it here covers the detector selector at 820px wide.
        const auto osFactor=processor_.activeAudioOversampling();
        juce::String osStatus="AUDIO OS "+juce::String(osFactor)+"x EXPERIMENTAL";
        if(processor_.audioOversamplingChangePending())
            osStatus+="  |  restart audio engine to apply";
        else if(osFactor>1)
            osStatus+="  |  post-OS dither  |  TP/PDC not certified";
        else
            osStatus+="  |  TP not certified";
        label(g,osStatus,chart.getX()+13,advance.getY()-19,chart.getWidth()-10,15,
              10,0xffddbac2,false);
    }

    void resized() override
    {
        const auto chart=chartBounds();
        const auto advance=advancedBounds();
        const int x=advance.getX(),y=advance.getY(),c=columnWidth();
        gain_.setBounds(17,chart.getY()+24,74,chart.getHeight()-32);
        // Output ceiling has its own clearly visible large numeric control:
        // place above the right meter where it cannot cover the graph.
        ceiling_.setBounds(getWidth()-166,54,152,27);
        ceiling_.setSliderStyle(juce::Slider::LinearHorizontal);
        ceiling_.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,25);

        const int comboW=juce::jmin(137,c+14);
        style_.setBounds(x+14,y+34,comboW,30);
        const int dialY=y+28;
        auto dial=[&](juce::Slider& slider,int col) {
            slider.setBounds(x+160+c*col,dialY,c-6,88);
        };
        dial(lookahead_,0);dial(attack_,1);dial(release_,2);
        dial(link_,3);dial(releaseLink_,4);

        const int footerY=getHeight()-37;
        detector_.setBounds(getWidth()-206,footerY+4,53,27);
        audioOs_.setBounds(getWidth()-104,footerY+4,85,27);
        dither_.setBounds(getWidth()-326,22,94,26);
        bits_.setBounds(getWidth()-354,footerY+4,46,27);
        tp_.setBounds(195,footerY+3,59,29);
        unity_.setBounds(256,footerY+3,74,29);
        audition_.setBounds(333,footerY+3,73,29);
        dc_.setBounds(408,footerY+3,52,29);
        bypass_.setBounds(getWidth()-423,8,93,35);
        bankA_.setBounds(getWidth()-215,12,27,26);
        bankB_.setBounds(getWidth()-185,12,27,26);
        copy_.setBounds(getWidth()-153,12,53,26);
        resetMeter_.setBounds(getWidth()-95,12,79,26);
        advanced_.setBounds(x+18,y+73,132,28);
        advanced_.setButtonText(expanded_?"COMPACT":"ADVANCED");
        const bool showAdvanced=expanded_;
        // Core mastering controls are always displayed; ADVANCED is a local
        // reveal that exposes the detailed DC and delta switches in the footer.
        dc_.setVisible(showAdvanced);
        audition_.setVisible(showAdvanced);
    }

private:
    using Snapshot=std::array<float,Count>;
    static float toDb(float value) noexcept
    {
        return 20.0f*std::log10(juce::jmax(1.0e-8f,value));
    }
    static void label(juce::Graphics& g,const juce::String& s,float x,float y,
                      float w,float h,float font,juce::uint32 colour,bool bold)
    {
        g.setColour(juce::Colour(colour));
        g.setFont(juce::Font(juce::FontOptions(font,
                     bold?juce::Font::bold:juce::Font::plain)));
        g.drawText(s,juce::Rectangle<float>(x,y,w,h),
                   juce::Justification::centredLeft,true);
    }
    juce::Rectangle<int> advancedBounds() const noexcept
    {
        return {106,getHeight()-177,getWidth()-116,139};
    }
    juce::Rectangle<int> chartBounds() const noexcept
    {
        return {106,87,getWidth()-283,getHeight()-287};
    }
    juce::Rectangle<int> meterBounds() const noexcept
    {
        return {getWidth()-166,88,153,getHeight()-277};
    }
    int columnWidth() const noexcept
    {
        return juce::jmax(80,(advancedBounds().getWidth()-172)/5);
    }
    LimiterHostedParameter& param(int i) noexcept { return processor_.getLimiterParameter(i); }

    void setup(juce::Slider& slider,int index,const juce::String& name,
               double low,double high,double interval,bool vertical)
    {
        slider.setName(name);
        slider.setTooltip(name+" (double-click value to type)");
        slider.setSliderStyle(vertical?juce::Slider::LinearVertical
                                      :juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,73,23);
        slider.setRange(low,high,interval);
        slider.setColour(juce::Slider::thumbColourId,juce::Colour(0xffee87bb));
        slider.setColour(juce::Slider::trackColourId,juce::Colour(0xffed70ac));
        slider.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff373e51));
        slider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xfff6eff5));
        slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff2c303d));
        slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff505467));
        slider.setValue(param(index).units(),juce::dontSendNotification);
        slider.onDragStart=[this,index]{param(index).beginChangeGesture();};
        slider.onValueChange=[this,index,&slider]{
            param(index).setValueNotifyingHost(
                 param(index).toNormal(static_cast<float>(slider.getValue())));
        };
        slider.onDragEnd=[this,index]{param(index).endChangeGesture();};
        addAndMakeVisible(slider);
    }
    void setupToggle(juce::ToggleButton& button,int index,
                     const juce::String& title,const juce::String& hint)
    {
        button.setButtonText(title);
        button.setTooltip(hint);
        button.setColour(juce::ToggleButton::textColourId,juce::Colour(0xffd2d9e7));
        button.setColour(juce::ToggleButton::tickColourId,juce::Colour(0xfff27bad));
        button.setToggleState(param(index).units()>0.5f,juce::dontSendNotification);
        button.onClick=[this,index,&button]{
            param(index).beginChangeGesture();
            param(index).setValueNotifyingHost(button.getToggleState()?1.0f:0.0f);
            param(index).endChangeGesture();
        };
        addAndMakeVisible(button);
    }
    void configureButton(juce::TextButton& b,const char* name,
                         std::function<void()> callback)
    {
        b.setButtonText(name);
        b.onClick=std::move(callback);
        addAndMakeVisible(b);
    }
    Snapshot snapshot() noexcept
    {
        Snapshot state{};
        for(int i=0;i<Count;++i)state[static_cast<size_t>(i)]=param(i).getValue();
        return state;
    }
    void recall(const Snapshot& state)
    {
        for(int i=0;i<Count;++i)
        {
            auto& p=param(i);
            p.beginChangeGesture();
            p.setValueNotifyingHost(state[static_cast<size_t>(i)]);
            p.endChangeGesture();
        }
    }
    void switchBank(int next)
    {
        if(next==activeBank_)return;
        banks_[static_cast<size_t>(activeBank_)]=snapshot();
        activeBank_=next;
        recall(banks_[static_cast<size_t>(next)]);
        updateBankButtons();
    }
    void updateBankButtons()
    {
        bankA_.setToggleState(activeBank_==0,juce::dontSendNotification);
        bankB_.setToggleState(activeBank_==1,juce::dontSendNotification);
    }
    void selectParameter(int index,int value)
    {
        auto& p=param(index);
        p.beginChangeGesture();
        p.setValueNotifyingHost(p.toNormal(static_cast<float>(value)));
        p.endChangeGesture();
    }
    void timerCallback() override
    {
        auto reflect=[this](juce::Slider& slider,int i){
            if(!slider.isMouseButtonDown())
                slider.setValue(param(i).units(),juce::dontSendNotification);
        };
        reflect(gain_,Gain);reflect(ceiling_,Ceiling);
        reflect(lookahead_,Lookahead);reflect(attack_,Attack);
        reflect(release_,Release);reflect(link_,ChannelLink);
        reflect(releaseLink_,ChannelLinkRelease);
        style_.setSelectedId(static_cast<int>(param(LimitingStyle).units())+1,
                             juce::dontSendNotification);
        dither_.setSelectedId(static_cast<int>(param(DitherMode).units())+1,
                              juce::dontSendNotification);
        bits_.setSelectedId(static_cast<int>(param(DitherBits).units())+1,
                            juce::dontSendNotification);
        detector_.setSelectedId(static_cast<int>(param(DetectorQuality).units())+1,
                                juce::dontSendNotification);
        audioOs_.setSelectedId(static_cast<int>(param(AudioOversampling).units())+1,
                               juce::dontSendNotification);
        auto reflectToggle=[this](juce::ToggleButton& b,int i){
            b.setToggleState(param(i).units()>0.5f,juce::dontSendNotification);
        };
        reflectToggle(tp_,TruePeak);reflectToggle(bypass_,Bypass);
        reflectToggle(unity_,UnityGain);reflectToggle(audition_,Audition);
        reflectToggle(dc_,DcFilter);
        repaint();
    }

    Processor& processor_;
    LimiterLookAndFeel skin_;
    juce::Slider gain_,ceiling_,lookahead_,attack_,release_,link_,releaseLink_;
    juce::ComboBox style_,detector_,audioOs_,dither_,bits_;
    juce::ToggleButton tp_,bypass_,unity_,audition_,dc_;
    juce::TextButton bankA_,bankB_,copy_,resetMeter_,advanced_;
    std::array<Snapshot,2> banks_{};
    int activeBank_=0;
    bool expanded_=true;
    float heldInput_=0,heldOutput_=0,heldReduction_=0;
};

inline juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}

}} // namespace APEX::Limiter

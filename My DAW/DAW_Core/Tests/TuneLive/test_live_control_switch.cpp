// Real APEX 40ms DSP: protect a singer when automation toggles Tune Live
// and ensure that returning to 0% correction becomes PDC-aligned dry again.
// No host/GUI, no ASIO claim, no allocations inside process(); strict C++17.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
constexpr int kRate=48000,kFrames=3*kRate,kPrepareBlock=128;
constexpr double kPi=3.14159265358979323846;
bool probe(int channels) {
    using namespace APEX::TuneLive;
    DspCore effect;
    effect.prepare(kRate,kPrepareBlock,channels);
    std::vector<float> left(kFrames),right(kFrames),copyLeft,copyRight;
    double phase=0.0;
    for(int i=0;i<kFrames;++i) {
        const double t=static_cast<double>(i)/kRate;
        const double hz=t<1.0?219.0:(t<2.0?332.0:443.0);
        const double vibrato=1.0+0.003*std::sin(2*kPi*4.9*t);
        phase+=2*kPi*hz*vibrato/kRate;
        const double base=0.24*std::sin(phase)+0.065*std::sin(2*phase);
        left[static_cast<std::size_t>(i)]=static_cast<float>(base);
        right[static_cast<std::size_t>(i)]=static_cast<float>(0.84*base);
    }
    copyLeft=left;
    copyRight=right;
    const std::array<int,5> blocks{{1,7,64,128,257}};
    double maximumAdjacent=0.0, peak=0.0;
    int processed=0,chunk=0;
    for(int i=0;i<kFrames;) {
        const int count=std::min(blocks[static_cast<std::size_t>(chunk%blocks.size())],kFrames-i);
        // Don't span the three stage boundaries; automation changes only
        // between host callbacks, as expected from JUCE parameter snapshots.
        const int stageEnd=std::min(kFrames,(i/kRate+1)*kRate);
        const int n=std::min(count,stageEnd-i);
        DspSettings control;
        control.wet=1.0f;
        control.retune=0.0f;
        control.flexTune=0.0f;
        control.humanize=0.0f;
        control.correction=1.0f;
        if(i>=kRate && i<2*kRate) {
            const int selection=(i/1024)%7;
            control.key=selection%12;
            control.scale=selection%3;
            control.retune=selection%2?0.0f:0.35f;
            control.humanize=selection%3==0?0.9f:0.05f;
            control.stability=selection%4==0?0.9f:0.2f;
            control.wet=selection%5==0?0.0f:1.0f;
            control.bypass=selection%6==0;
            control.preserveFormants=selection%2==0;
            control.referenceHz=selection%2?443.0f:438.0f;
        } else if(i>=2*kRate) {
            // Finish with 100% wet and 0% correction; the shifter wet gate
            // must move smoothly to exact PDC dry, even after hot automation.
            control.correction=0.0f;
            control.wet=1.0f;
            control.bypass=false;
            control.retune=0.0f;
        }
        float* data[2]={left.data()+i,channels==2?right.data()+i:nullptr};
        effect.process(data,channels,n,control);
        for(int k=0;k<n;++k) {
            const float a=left[static_cast<std::size_t>(i+k)];
            const float b=channels==2?right[static_cast<std::size_t>(i+k)]:a;
            if(!std::isfinite(a)||!std::isfinite(b))
                return false;
            peak=std::max(peak,std::max(std::abs(static_cast<double>(a)),
                                        std::abs(static_cast<double>(b))));
            if(i+k>0) {
                maximumAdjacent=std::max(maximumAdjacent,std::abs(static_cast<double>(a)
                    -left[static_cast<std::size_t>(i+k-1)]));
                if(channels==2)
                    maximumAdjacent=std::max(maximumAdjacent,std::abs(static_cast<double>(b)
                        -right[static_cast<std::size_t>(i+k-1)]));
            }
        }
        processed+=n;
        i+=n;
        ++chunk;
    }
    const int latency=effect.latencySamples();
    double worstDryDifference=0.0;
    // Only check the final 250ms after the 1s neutral ramp; if any spectral
    // coloration lingers, returning to no correction is no longer transparent.
    for(int i=kFrames-kRate/4;i<kFrames;++i) {
        if(i<latency)return false;
        worstDryDifference=std::max(worstDryDifference,
             std::abs(static_cast<double>(left[static_cast<std::size_t>(i)])
                      -copyLeft[static_cast<std::size_t>(i-latency)]));
        if(channels==2)
            worstDryDifference=std::max(worstDryDifference,
                std::abs(static_cast<double>(right[static_cast<std::size_t>(i)])
                         -copyRight[static_cast<std::size_t>(i-latency)]));
    }
    std::cout<<"TUNE_LIVE_AUTOMATION_STRESS channels="<<channels
             <<" processed_samples="<<processed
             <<" PDC="<<latency
             <<" final_neutral_max_error="<<worstDryDifference
             <<" max_adjacent_step_observed="<<maximumAdjacent
             <<" output_peak="<<peak<<"\\n";
    return processed==kFrames && latency==1920 && peak>0 && peak<8.0 &&
           worstDryDifference<1.0e-5;
}
}
int main() {
    const bool mono=probe(1);
    const bool stereo=probe(2);
    if(!mono || !stereo) {
        std::cerr<<"FAIL automation hot-switch or final neutral transparency\\n";
        return 1;
    }
    std::cout<<"PASS mono+stereo runtime automation switches, short/oversized callbacks, "
                "and exact final bypassed-PDC dry sound\\n";
    return 0;
}

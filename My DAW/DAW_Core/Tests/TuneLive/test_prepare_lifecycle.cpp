// APEX Tune Live — reusable production DSP lifetime and sample-rate changes.
// Host may stop audio, reopen 44.1/48/96 kHz, change mono/stereo bus,
 // resize callback boundaries, release resources and restore from reset.
// Compare PDC-bypassed audio sample-for-sample across repeated prepares;
// detects stale stretch/clock/delay state leaking between sessions.
// Strict portable C++17. Never prepare/reset on the audio callback.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
constexpr double kPi=3.1415926535897932384626433832795;
struct Case { int rate, channels, prepared; };
bool exercise(APEX::TuneLive::DspCore& dsp,const Case& c,int cycle) {
    dsp.prepare(c.rate,c.prepared,c.channels);
    const int latency=dsp.latencySamples();
    if(latency != static_cast<int>(std::lround(c.rate*0.040)))
        return false;
    const int frames=static_cast<int>(c.rate*0.31);
    std::vector<float> left(frames),right(frames),originalLeft,originalRight;
    for(int i=0;i<frames;++i) {
        const double t=static_cast<double>(i)/c.rate;
        const double sig=0.23*std::sin(2*kPi*221.0*t)+
                         0.058*std::sin(2*kPi*661.0*t);
        left[static_cast<std::size_t>(i)]=static_cast<float>(sig);
        right[static_cast<std::size_t>(i)]=static_cast<float>(0.71*sig);
    }
    originalLeft=left;
    originalRight=right;
    APEX::TuneLive::DspSettings s;
    s.bypass=false;
    s.wet=1.0f;
    s.correction=1.0f;
    s.retune=0.0f;
    s.humanize=0.0f;
    s.flexTune=0.0f;
    // Exercise active pitch processing prior to reset, so tests catch
    // partially initialized shifter state after a bus or rate change.
    const int wetFrames=std::min(frames,c.rate/6);
    int begin=0;
    int waveIndex=cycle;
    while(begin<wetFrames) {
        const int nominal=std::array<int,5>{{1,7,63,129,257}}
            [static_cast<std::size_t>(waveIndex%5)];
        const int n=std::min(nominal,wetFrames-begin);
        float* audio[2]={left.data()+begin,c.channels==2?right.data()+begin:nullptr};
        dsp.process(audio,c.channels,n,s);
        for(int i=0;i<n;++i) {
            if(!std::isfinite(left[static_cast<std::size_t>(begin+i)]) ||
               (c.channels==2 && !std::isfinite(right[static_cast<std::size_t>(begin+i)])))
                return false;
        }
        begin+=n;
        ++waveIndex;
    }
    // DAW stop/start reset is on non-realtime control thread.
    dsp.reset();
    left=originalLeft;
    right=originalRight;
    s.bypass=true;
    double worstError=0.0;
    begin=0;
    while(begin<frames) {
        const int n=std::min(std::array<int,6>{{1,3,64,128,256,513}}
            [static_cast<std::size_t>(waveIndex%6)],frames-begin);
        float* audio[2]={left.data()+begin,c.channels==2?right.data()+begin:nullptr};
        dsp.process(audio,c.channels,n,s);
        begin+=n;
        ++waveIndex;
    }
    for(int i=latency+128;i<frames;++i) {
        worstError=std::max(worstError,std::abs(static_cast<double>(
             left[static_cast<std::size_t>(i)])-
             originalLeft[static_cast<std::size_t>(i-latency)]));
        if(c.channels==2)
            worstError=std::max(worstError,std::abs(static_cast<double>(
                 right[static_cast<std::size_t>(i)])-
                 originalRight[static_cast<std::size_t>(i-latency)]));
    }
    std::cout<<"TUNE_LIVE_REPREPARE_CYCLE index="<<cycle
             <<" rate="<<c.rate<<" channels="<<c.channels
             <<" prepared_block="<<c.prepared<<" PDC="<<latency
             <<" worst_bypass_sample_error="<<worstError<<"\n";
    return std::isfinite(worstError) && worstError<1.e-5 &&
           dsp.profile()==APEX::TuneLive::ShifterProfile::HighQuality40ms;
}
}
int main() {
    using namespace APEX::TuneLive;
    DspCore shared;
    constexpr std::array<Case,8> cases{{
        {48000,1,128},{44100,2,64},{96000,1,256},
        {48000,2,32},{44100,1,257},{96000,2,128},
        {48000,1,32},{48000,2,128}
    }};
    bool pass=true;
    for(std::size_t i=0;i<cases.size();++i)
        pass=exercise(shared,cases[i],static_cast<int>(i)) && pass;
    if(!pass) {
        std::cerr<<"FAIL: reused real production shifter across rate, "
                   "channel, buffer or session reset\n";
        return 1;
    }
    std::cout<<"TUNE_LIVE_PREPARE_LIFECYCLE_PASS 8 successive producer workflows "
                "44.1/48/96k mono+stereo PDC exact\n";
}

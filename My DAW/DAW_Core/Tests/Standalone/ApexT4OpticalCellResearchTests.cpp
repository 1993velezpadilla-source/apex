// Independent T4 physics-structure tests. No JUCE.
#include "../../Source/CompressorCore/ApexT4OpticalCellCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace APEX::Compressor;
static void near(double a,double b,double eps=1.e-7) {
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>eps) std::abort();
}
int main() {
    // Static: illuminated CdS conducts, voltage divider attenuates.
    ApexT4OpticalCellCore t;t.prepare(48000);
    near(t.getGainReductionDb(),0);
    const auto dark=t.getAudioResistanceOhms();
    for(int i=0;i<96000;++i)t.tick(1.0);
    const auto compressed=t.getGainReductionDb();
    assert(t.getAudioResistanceOhms()<dark && compressed>6.0);
    assert(t.getMeterResistanceOhms()>t.getAudioResistanceOhms());
    assert(t.getExposureMemory()>0.0 && t.getExposureMemory()<1.0);

    // Two time scales: fast 60ms recovery, then long history-dependent tail.
    for(int i=0;i<2880;++i)t.tick(0.0);
    const auto after60=t.getGainReductionDb();
    assert(after60<compressed && after60>0);
    for(int i=0;i<96000;++i)t.tick(0.0);
    assert(t.getGainReductionDb()<after60);

    // No dependence on callback chunk sizes, even when slow coeff refreshes.
    ApexT4OpticalCellCore whole,sliced;whole.prepare(48000);sliced.prepare(48000);
    std::vector<float> excite(8192),out1(8192),out2(8192);
    for(int n=0;n<8192;++n) excite[n]=n<1800 ? .82f:n<5800 ? .36f:0.f;
    whole.process(excite.data(),out1.data(),8192);
    for(int i=0;i<8192;i+=17)
        sliced.process(excite.data()+i,out2.data()+i,std::min(17,8192-i));
    for(int n=0;n<8192;++n)near(out1[n],out2[n]);
    near(whole.getGainReductionDb(),sliced.getGainReductionDb());

    // Fixed real-time exposure durations yield nearly identical responses
    // across supported sample rates (coefficients are seconds, not samples).
    const auto atRate=[](double sampleRate) {
        ApexT4OpticalCellCore c;c.prepare(sampleRate);
        for(int n=0;n<(int)(sampleRate*.2);++n)c.tick(.75);
        for(int n=0;n<(int)(sampleRate*.5);++n)c.tick(0);
        return c.getGainReductionDb();
    };
    const auto reference=atRate(48000);
    for(double sr: {44100.0,96000.0,192000.0})
        near(atRate(sr),reference,0.015);

    // Sanitize extreme input and invalid calibration; no NaNs propagate.
    ApexT4OpticalCellCore safety;T4Calibration c;
    c.seriesOhms=NAN;c.brightOhms=-1;c.elReleaseSec=INFINITY;
    safety.prepare(NAN,c);
    for(int i=0;i<400;++i)safety.tick(NAN);
    assert(std::isfinite(safety.getGainReductionDb()));
    safety.reset();near(safety.getGainReductionDb(),0);

    std::cout << "APEX T4 research gates PASS (NOT calibrated or 1:1)\n";
}

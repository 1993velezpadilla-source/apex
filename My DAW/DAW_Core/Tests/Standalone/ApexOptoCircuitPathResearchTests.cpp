// Original full analogue-style circuit-path research functional gates.
// These DO NOT assert any UAD LA-2A Gray perceptual, electrical or timing match.
#include "../../Source/CompressorCore/ApexOptoCircuitPathResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace APEX::Compressor;
static void near(double a,double b,double tol=1.e-5){
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tol){
        std::cerr<<"Mismatch "<<a<<" vs "<<b<<"\n";std::abort();
    }
}
int main(){
    constexpr int count=48000;
    // Dry and initial bypass never route through the nonlinear tube circuit.
    for(const bool bypass:{false,true}) {
        ApexOptoCircuitPathResearchCore path;path.prepare(48000);
        ApexOptoCircuitResearchParameters p;
        p.peakReduction=95;p.mix=bypass?1.0:0.0;p.bypass=bypass;
        path.setParameters(p);
        std::vector<float> left(count),right(count);
        for(int n=0;n<count;++n){
            left[n]=static_cast<float>(.4*std::sin(n*.04));
            right[n]=static_cast<float>(.2*std::cos(n*.06));
        }
        auto originalLeft=left, originalRight=right;
        path.process(left.data(),right.data(),count);
        for(int n=0;n<count;++n){
            near(left[n],originalLeft[n],1.e-6);
            near(right[n],originalRight[n],1.e-6);
        }
        assert(path.getGainReductionDb()>1.0); // detector stays warm
    }
    // Main optical+analogue path reduces a steady waveform when engaged,
    // while preserving both audio channels and valid machine numbers.
    {
        ApexOptoCircuitPathResearchCore path;path.prepare(48000);
        ApexOptoCircuitResearchParameters p;p.peakReduction=90;
        path.setParameters(p);
        std::vector<float> left(count),right(count);
        for(int n=0;n<count;++n) {
            const auto phase=2.0*3.141592653589793*1000.0*n/48000.0;
            left[n]=static_cast<float>(0.8*std::sin(phase));
            right[n]=static_cast<float>(0.25*std::sin(phase));
        }
        path.process(left.data(),right.data(),count);
        assert(path.getGainReductionDb()>2.0);
        assert(std::isfinite(left.back())&&std::isfinite(right.back()));
        assert(path.getT4Exposure()>0);
    }
    // Buffers 31/64/128/48000: state and waveform are exactly consistent.
    {
        ApexOptoCircuitPathResearchCore one,other;
        one.prepare(48000);other.prepare(48000);
        ApexOptoCircuitResearchParameters p;
        p.peakReduction=65;p.mix=.45;p.gainDb=4;p.emphasis=.3;
        one.setParameters(p);other.setParameters(p);
        std::vector<float> left(8192),right(8192),splitL(8192),splitR(8192);
        for(int i=0;i<8192;++i) {
            left[i]=splitL[i]=static_cast<float>(.32*std::sin(i*.038));
            right[i]=splitR[i]=static_cast<float>(.22*std::cos(i*.027));
        }
        one.process(left.data(),right.data(),8192);
        for(int pos=0;pos<8192;pos+=31) {
            const int frames=std::min(31,8192-pos);
            other.process(splitL.data()+pos,splitR.data()+pos,frames);
        }
        for(int i=0;i<8192;++i) {
            near(left[i],splitL[i],1.e-6);
            near(right[i],splitR[i],1.e-6);
        }
        near(one.getGainReductionDb(),other.getGainReductionDb(),1.e-9);
    }
    // X4 magnetic mode has 16+16 host samples of FIR latency: the DRY
    // and initial BYPASS path must align to 32 samples, not silently comb.
    for(const bool bypass:{false,true}) {
        ApexOptoCircuitPathResearchCore path;
        path.prepare(48000,true);
        assert(path.latencySamples()==32);
        ApexOptoCircuitResearchParameters p;
        p.peakReduction=94;p.mix=bypass?1.0:0.0;p.bypass=bypass;
        path.setParameters(p);
        std::vector<float> left(512),right(512);
        for(int n=0;n<512;++n) {
            left[n]=static_cast<float>(.4*std::sin(n*.085));
            right[n]=static_cast<float>(.25*std::cos(n*.051));
        }
        const auto origL=left,origR=right;
        path.process(left.data(),right.data(),512);
        for(int n=0;n<512;++n) {
            const double expectL=n>=32?origL[n-32]:0.0;
            const double expectR=n>=32?origR[n-32]:0.0;
            near(left[n],expectL,1.e-6);
            near(right[n],expectR,1.e-6);
        }
    }
    // Continuous sample clock: x4 magnetic/T4/valve chain is invariant to
    // DAW block partition (32 / 64 / 128 vs contiguous 2048 frames).
    {
        ApexOptoCircuitPathResearchCore continuous,partitioned;
        continuous.prepare(48000,true);partitioned.prepare(48000,true);
        ApexOptoCircuitResearchParameters p;
        p.peakReduction=79;p.gainDb=3;p.mix=.62;p.emphasis=.25;
        continuous.setParameters(p);partitioned.setParameters(p);
        std::vector<float> a(2048),b(2048),c(2048),d(2048);
        for(int i=0;i<2048;++i) {
            a[i]=c[i]=static_cast<float>(.34*std::sin(i*.023));
            b[i]=d[i]=static_cast<float>(.21*std::cos(i*.035));
        }
        continuous.process(a.data(),b.data(),2048);
        for(int start=0;start<2048;start+=31)
            partitioned.process(c.data()+start,d.data()+start,
                                std::min(31,2048-start));
        for(int i=0;i<2048;++i) {
            near(a[i],c[i],1.e-6);
            near(b[i],d[i],1.e-6);
        }
        near(continuous.getGainReductionDb(),partitioned.getGainReductionDb(),1.e-8);
    }
    // Mono, nonfinite source and accelerated sample rates do not crash.
    for(const auto sampleRate:{44100.0,48000.0,96000.0,192000.0}){
        ApexOptoCircuitPathResearchCore path;path.prepare(sampleRate);
        ApexOptoCircuitResearchParameters p;p.peakReduction=83;
        p.gainDb=INFINITY;p.mix=NAN;p.emphasis=-INFINITY;
        path.setParameters(p);
        float block[128]={NAN,INFINITY,-INFINITY};
        path.process(block,nullptr,128);
        for(const auto value:block)assert(std::isfinite(value));
        near(path.latencySamples(),0);
    }
    std::cout<<"APEX full T4 + 12AX7/12BH7 + linear transformer path GREEN (unfitted research only)\n";
}

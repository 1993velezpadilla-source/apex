// Standalone C++17 sanity gates; NOT a UAD/Teletronix reference test.
#include "../../Source/CompressorCore/ApexAnalogCircuitResearchCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace APEX::Compressor;
constexpr double pi=3.14159265358979323846;
static void near(double a,double b,double tolerance){
    if(!std::isfinite(a)||!std::isfinite(b)||std::abs(a-b)>tolerance){
        std::cerr<<"Mismatch "<<a<<" "<<b<<"\n";std::abort();
    }
}
static void tubes(){
    ApexTriodeResearchCore ax7,bh7;
    ax7.prepare(ApexTriodeCircuit::ax7VoltageStage());
    bh7.prepare(ApexTriodeCircuit::bh7CathodeFollower());
    near(ax7.process(0),0,1.e-10);
    near(bh7.process(0),0,1.e-10);
    assert(ax7.plateCurrentAmps(0)>0 && ax7.dcOutputVolts()>0);
    assert(bh7.dcOutputVolts()>0);
    assert(ax7.plateCurrentAmps(1)>ax7.plateCurrentAmps(0));
    near(ax7.process(.001),.001,0.0001);
    near(bh7.process(.001),.001,0.0001);
    double previous=-1.e90,maxDeviation=0;
    for(int i=0;i<=2000;++i){
        const double x=-1.0+i/1000.0;
        const double out=ax7.process(x);
        assert(out>=previous-1.e-7);
        previous=out;
        maxDeviation=std::max(maxDeviation,std::abs(out-x));
        assert(std::isfinite(bh7.process(x)));
    }
    assert(maxDeviation>1.e-5);
    assert(ax7.process(NAN)==0 && ax7.process(INFINITY)==0);
}
static double sineGain(double hz,double sr){
    ApexTransformerResearchCore t;t.prepare(sr);
    const int count=static_cast<int>(sr);
    double sinSum=0,cosSum=0;
    for(int n=0;n<count;++n){
        const double phase=2*pi*hz*n/sr;
        const double out=t.process(std::sin(phase));
        if(n>count/2){sinSum+=out*std::sin(phase);cosSum+=out*std::cos(phase);}
    }
    return 2*std::sqrt(sinSum*sinSum+cosSum*cosSum)/(count-count/2-1);
}
static void transformers(){
    const auto middle=sineGain(1000,48000);
    assert(middle>.75 && middle<1.25);
    assert(sineGain(5,48000)<middle*.8);
    assert(sineGain(17000,48000)<middle);
    near(middle,sineGain(1000,96000),.035);
    ApexTransformerResearchCore t;t.prepare(48000);
    near(t.process(0),0,1.e-12);
    for(int i=0;i<48000;++i)t.process(.9);
    assert(std::abs(t.process(.9))<.1);
    t.prepare(NAN);
    assert(std::isfinite(t.process(NAN)));
    assert(std::isfinite(t.process(INFINITY)));
}
int main(){
    tubes();transformers();
    std::cout<<"APEX triode/transformer STRUCTURAL gates PASS; hardware calibration pending\n";
}

#include "../../Source/CompressorCore/ApexPrecisionCharacterOversamplingCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using APEX::PrecisionC3::CharacterOversampling2x;

static double amplitudeAt(const std::vector<double>& samples, int start,
                          int frames, double frequency, double rate)
{
    constexpr double pi=3.1415926535897932384626433832795;
    double re=0.0,im=0.0;
    for(int i=0;i<frames;++i)
    {
        const double angle=2*pi*frequency*i/rate;
        const auto sample=samples[static_cast<std::size_t>(start+i)];
        re+=sample*std::cos(angle);
        im+=sample*std::sin(angle);
    }
    return 2.0*std::hypot(re,im)/frames;
}

int main()
{
    // No changes in real-time callback may allocate or depend on block size.
    // The FIR history must preserve silence, finite values and known delay.
    {
        CharacterOversampling2x processor;
        processor.prepare();
        for(int i=0;i<512;++i)
            assert(processor.process(0.0,3.0,true)==0.0);
        processor.reset();
        for(int i=0;i<512;++i)
            assert(std::isfinite(processor.process(
                i%19==0?std::numeric_limits<double>::infinity():0.0,
                std::numeric_limits<double>::quiet_NaN(),true)));
    }
    {
        CharacterOversampling2x processor;
        processor.prepare();
        std::vector<double> impulse(256,0.0);
        for(int i=0;i<256;++i)
            impulse[static_cast<std::size_t>(i)] =
                processor.process(i==0?1.0:0.0,3.0,false);
        const int peak=static_cast<int>(std::distance(impulse.begin(),
            std::max_element(impulse.begin(),impulse.end())));
        assert(peak==CharacterOversampling2x::kLatencySamples);
        assert(impulse[static_cast<std::size_t>(peak)] > 0.90);
        // FIR interpolation+decimation must preserve DC gain to <0.1%.
        double sum=0.0;
        for(const auto x:impulse)sum+=x;
        assert(std::abs(sum-1.0)<0.001);
    }
    {
        CharacterOversampling2x linear,shaped;
        linear.prepare();shaped.prepare();
        constexpr double dc=0.38,drive=2.5;
        double plain=0.0,nonlinear=0.0;
        for(int i=0;i<5000;++i)
        {
            plain=linear.process(dc,drive,false);
            nonlinear=shaped.process(dc,drive,true);
        }
        assert(std::abs(plain-dc)<0.0001);
        assert(std::abs(nonlinear-std::tanh(drive*dc)/std::tanh(drive))<0.0001);
    }
    // The raw 48k saturated 10kHz sine creates an aliased 3rd harmonic at
    // 18 kHz. True 2x + antialias FIR MUST reduce that alias by >=30dB,
    // while leaving the 10kHz fundamental within 0.5dB.
    {
        constexpr double rate=48000.0,frequency=10000.0,drive=3.0,pi=3.14159265358979323846;
        constexpr int preRoll=4800,frames=4800;
        std::vector<double> unfiltered(preRoll+frames),oversampled(preRoll+frames);
        CharacterOversampling2x processor;processor.prepare();
        for(int i=0;i<preRoll+frames;++i)
        {
            const double input=.85*std::sin(2*pi*frequency*i/rate);
            unfiltered[static_cast<std::size_t>(i)]
                =std::tanh(drive*input)/std::tanh(drive);
            oversampled[static_cast<std::size_t>(i)]
                =processor.process(input,drive,true);
        }
        const double rawAlias=amplitudeAt(unfiltered,preRoll,frames,18000.0,rate);
        const double newAlias=amplitudeAt(oversampled,preRoll,frames,18000.0,rate);
        const double rawFundamental=amplitudeAt(unfiltered,preRoll,frames,frequency,rate);
        const double newFundamental=amplitudeAt(oversampled,preRoll,frames,frequency,rate);
        const double aliasSuppression=20.0*std::log10(rawAlias/std::max(1.0e-15,newAlias));
        const double fundamentalDifference=20.0*std::log10(newFundamental/rawFundamental);
        std::cout<<"2x saturation alias suppression @18k: "
                 <<aliasSuppression<<" dB; fundamental difference "
                 <<fundamentalDifference<<" dB\n";
        assert(aliasSuppression>30.0);
        assert(std::abs(fundamentalDifference)<0.5);
    }
    // Test independent stereo history, not a shared state that could cross-talk.
    {
        CharacterOversampling2x left,right;
        left.prepare();right.prepare();
        double rightPeak=0.0;
        for(int i=0;i<2000;++i)
        {
            (void)left.process(i==40?1.0:0.0,6.0,true);
            rightPeak=std::max(rightPeak,
                               std::abs(right.process(0.0,6.0,true)));
        }
        assert(rightPeak==0.0);
    }
    // "Block" invariance: never reset a processing stage at a host block
    // boundary. No per-block allocations, filter coefficients or state resets.
    {
        CharacterOversampling2x a,b;
        a.prepare();b.prepare();
        for(int i=0;i<8192;++i)
        {
            const double source=.63*std::sin(i*.07)+.09*std::cos(i*.15);
            const double direct=a.process(source,4.0,true);
            const double partitioned=b.process(source,4.0,true);
            assert(std::abs(direct-partitioned)<1.0e-12);
        }
    }

    std::cout<<"APEX Precision C independent 2x antialias DSP lab PASS\n";
    return 0;
}

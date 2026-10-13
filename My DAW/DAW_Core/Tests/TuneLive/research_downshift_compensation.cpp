// APEX Tune Live — RESEARCH ONLY nonlinear downshift calibration.
//
// Previous direct shifter tests established an asymmetric output ratio
// for *steady synthetic* +30-cent singers, dependent on pitch register.
// Instead of turning up global correction or inventing an Antares-style
// behavior, invert the ACTUALLY measured downshift response and compare
// to the unchanged 40ms native shifter. Not included in APEX GUI/processor.
//
// Calibration knots are from the pinned direct-shifter experiment:
// at 220Hz (A3) requests {-20,-30,-40,-50,-60} cents ->
// achieved shift magnitudes {5,9,15,22,30} cents.
// at 440Hz (A4), achieved magnitudes {7,14,25,40,55}.
// These are only approximate and may not generalize to real performances,
// varying notes, vibrato, signal spectrum, or another sample rate.
#include "../../Source/TuneLiveCore/ApexTuneLiveDspCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int rate=48000,frames=rate*2,block=128,analysisN=8192;
constexpr double pi=3.1415926535897932384626433832795;

struct Knot { double requestMagnitude, observedShift; };
constexpr std::array<Knot,5> tableA3{{
    {20,5}, {30,9}, {40,15}, {50,22}, {60,30}
}};
constexpr std::array<Knot,5> tableA4{{
    {20,7}, {30,14}, {40,25}, {50,40}, {60,55}
}};
// Do not extrapolate. An experimental calibration must fail visibly if it
// has not measured how the shifter handles the requested displacement.
double invert(const std::array<Knot,5>& table,double desiredMagnitude) {
    if(desiredMagnitude<table.front().observedShift ||
       desiredMagnitude>table.back().observedShift)
        throw std::runtime_error("correction outside measured downshift range");
    for(std::size_t i=1;i<table.size();++i) {
        if(desiredMagnitude<=table[i].observedShift) {
            const double t=(desiredMagnitude-table[i-1].observedShift)/
                           (table[i].observedShift-table[i-1].observedShift);
            return table[i-1].requestMagnitude+
                   t*(table[i].requestMagnitude-table[i-1].requestMagnitude);
        }
    }
    return table.back().requestMagnitude;
}
double calibratedDownshift(double noteHz,double desiredCents) {
    const double a3=invert(tableA3,desiredCents);
    const double a4=invert(tableA4,desiredCents);
    // Log-frequency interpolation is musical and remains bounded by
    // both proven anchors: never extrapolate A2 / soprano beyond them.
    const double blend=std::clamp(std::log2(noteHz/220.0),0.0,1.0);
    return a3+(a4-a3)*blend;
}
struct Render {
    std::vector<float> samples;
    double peak=0;
};
Render render(double targetHz,double sharpCents,double requestedShiftCents) {
    const double sourceHz=targetHz*std::pow(2.0,sharpCents/1200.0);
    std::vector<float> original(frames);
    Render output;
    output.samples.resize(frames);
    double phase=0.0;
    for(int i=0;i<frames;++i) {
        phase+=2*pi*sourceHz/rate;
        original[static_cast<std::size_t>(i)]=static_cast<float>(
            0.28*std::sin(phase)+0.06*std::sin(phase*2.0));
    }
    signalsmith::stretch::SignalsmithStretch<float> stretch{0x41504558L};
    constexpr int window=rate*40/1000;
    stretch.configure(1,window,window/4,false);
    stretch.reset();
    stretch.setTransposeFactor(static_cast<float>(
        std::pow(2.0,requestedShiftCents/1200.0)));
    stretch.setFormantFactor(1.0f,true);
    stretch.setFormantBase(static_cast<float>(sourceHz/rate));
    std::array<float,block> scratch{};
    for(int i=0;i<frames;i+=block) {
        const int count=std::min(block,frames-i);
        const float* inputs[1]={original.data()+i};
        float* outputs[1]={scratch.data()};
        stretch.process(inputs,count,outputs,count);
        for(int j=0;j<count;++j) {
            const auto v=scratch[static_cast<std::size_t>(j)];
            if(!std::isfinite(v))throw std::runtime_error("nonfinite render sample");
            output.samples[static_cast<std::size_t>(i+j)]=v;
            output.peak=std::max(output.peak,std::abs(static_cast<double>(v)));
        }
    }
    if(stretch.inputLatency()+stretch.outputLatency()!=rate*40/1000)
        throw std::runtime_error("unexpected test pitch shifter PDC");
    return output;
}
double windowedGoertzel(const std::vector<float>& samples,double hz) {
    constexpr int offset=frames-analysisN-512;
    const double coeff=2*std::cos(2*pi*hz/rate);
    double q1=0,q2=0;
    for(int i=0;i<analysisN;++i) {
        const double hann=0.5*(1-std::cos(2*pi*i/(analysisN-1)));
        const double q0=static_cast<double>(samples[static_cast<std::size_t>(offset+i)])*
                      hann+coeff*q1-q2;
        q2=q1;q1=q0;
    }
    return std::sqrt(std::max(0.0,q1*q1+q2*q2-coeff*q1*q2));
}
struct Measured { double cents=9999,energy=0; };
Measured estimate(const Render& render,double targetHz) {
    Measured m;
    for(int cents=-65;cents<=65;++cents) {
        const double hz=targetHz*std::pow(2.0,cents/1200.0);
        const double energy=windowedGoertzel(render.samples,hz);
        if(energy>m.energy) {
            m.energy=energy;
            m.cents=cents;
        }
    }
    return m;
}
bool evaluate(double targetHz,double sharpCents) {
    const double calibrated=calibratedDownshift(targetHz,sharpCents);
    const auto baseline=render(targetHz,sharpCents,-sharpCents);
    const auto candidate=render(targetHz,sharpCents,-calibrated);
    const auto measuredBaseline=estimate(baseline,targetHz);
    const auto measuredCandidate=estimate(candidate,targetHz);
    const auto reduction=std::abs(measuredBaseline.cents)-
                         std::abs(measuredCandidate.cents);
    std::cout<<"APEX_RESEARCH_SHIFTER_CALIBRATION"
             <<" note_hz="<<targetHz
             <<" source_sharp_cents="<<sharpCents
             <<" baseline_request_cents="<<-sharpCents
             <<" calibrated_request_cents="<<-calibrated
             <<" baseline_output_cents="<<measuredBaseline.cents
             <<" calibrated_output_cents="<<measuredCandidate.cents
             <<" improvement_cents="<<reduction
             <<" baseline_peak="<<baseline.peak
             <<" calibrated_peak="<<candidate.peak<<"\\n";
    // This tests the independent fixed-tone pitch shifter only, never
    // claims general vocal quality. Fail if the compensation is clearly
    // worse or produces a dangerous output.
    return measuredBaseline.energy>1.0 && measuredCandidate.energy>1.0
        && baseline.peak>0.02 && baseline.peak<1.0
        && candidate.peak>0.02 && candidate.peak<1.0
        && std::abs(measuredCandidate.cents)<=12.0
        && reduction>=3.0;
}
}
int main() {
    try {
        bool passed=true;
        for(double hz:std::array<double,2>{{220.0,440.0}})
            for(double sharp:std::array<double,2>{{25.0,30.0}})
                passed=evaluate(hz,sharp)&&passed;
        if(!passed)throw std::runtime_error(
            "non-shipping compensation failed to beat unmodified shifter");
        std::cout<<"APEX_RESEARCH_SHIFTER_CALIBRATION_PASS four independent "
                    "steady-tone downshift comparisons; production HQ40 "
                    "and GUI UNCHANGED\\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"APEX_RESEARCH_SHIFTER_CALIBRATION_FAIL: "<<e.what()<<"\\n";
        return 1;
    }
}

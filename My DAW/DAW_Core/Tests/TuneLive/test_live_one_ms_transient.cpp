// STRICT 48-sample/48kHz transient path gate for the NON-SHIPPING
// causal residual-preserving harmonic research processor.
//
// This asserts ONLY whether an isolated consonant-like impulse inside
// already-voiced audio propagates in the same sample after the dry/wet
// residual replacement becomes active. Pitch tracking/recovery is slower.
// A passing zero-lookahead transient path IS NOT evidence that pitch notes
// acquire or audibly change within one millisecond.
#include "../../Source/TuneLiveCore/Research/ApexTuneLiveHarmonicLivePrototype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
namespace {
constexpr double kPi=3.14159265358979323846;
APEX::TuneLive::VoiceSettings settings() {
    APEX::TuneLive::VoiceSettings s;
    s.root=9;s.scale=0;s.correction=1.0f;s.referenceHz=440.0f;
    s.humanize=0.0f;s.flexTune=0.0f;s.noteHysteresis=0.1f;
    return s;
}
float source(double rate,double hz,int frame) {
    const double rad=2.0*kPi*hz*frame/rate;
    return static_cast<float>(0.26*std::sin(rad)
           +0.054*std::sin(2.0*rad+0.31)
           +0.019*std::sin(3.0*rad+0.13));
}
}
int main() {
    using APEX::TuneLive::Research::HarmonicLivePrototype;
    bool pass=true;
    constexpr double rate=48000.0;
    constexpr int frames=48000;
    constexpr int consonantSample=32000;
    constexpr int block=128;
    constexpr float consonant=0.25f;
    for(double sourceHz:std::array<double,2>{{220.0,440.0}}) {
        const double detuned=sourceHz*std::pow(2.0,30.0/1200.0);
        std::vector<float> baseline(frames),withConsonant(frames);
        for(int i=0;i<frames;++i)
            baseline[static_cast<std::size_t>(i)]=
                withConsonant[static_cast<std::size_t>(i)]=source(rate,detuned,i);
        withConsonant[consonantSample]+=consonant;
        HarmonicLivePrototype reference,modified;
        reference.prepare(rate);
        modified.prepare(rate);
        reference.setVoiceSettings(settings());
        modified.setVoiceSettings(settings());
        reference.enableEarlyOnset(true);
        modified.enableEarlyOnset(true);
        reference.enableResidualPreservation(true);
        modified.enableResidualPreservation(true);
        float firstSampleDelta=0.0f;
        for(int offset=0;offset<frames;offset+=block) {
            const int count=std::min(block,frames-offset);
            float* a[1]={baseline.data()+offset};
            float* b[1]={withConsonant.data()+offset};
            reference.process(a,1,count);
            modified.process(b,1,count);
            if(consonantSample>=offset && consonantSample<offset+count)
                firstSampleDelta=withConsonant[consonantSample]-baseline[consonantSample];
        }
        // A singer's plosive/consonant attack must not be smeared by
        // the pitch shifter's history while an existing voiced note is tuned.
        const double sameSampleGain=firstSampleDelta/consonant;
        const bool naturalTransient=std::isfinite(sameSampleGain) &&
             sameSampleGain>=0.70 && sameSampleGain<=1.30;
        const bool tunedAndReady=reference.voiced() && modified.voiced() &&
             reference.researchWetMix()>=0.98 && modified.researchWetMix()>=0.98 &&
             reference.firstHalfWetSample()>=0;
        pass &= naturalTransient && tunedAndReady;
        std::cout<<"ONE_MS_TRANSIENT research_only=1 source_hz="<<detuned
                 <<" impulse_at_sample="<<consonantSample
                 <<" impulse_output_sample="<<consonantSample
                 <<" same_sample_gain="<<sameSampleGain
                 <<" voiced_after="<<modified.voiced()
                 <<" wet="<<modified.researchWetMix()
                 <<" immediate_transient="<<naturalTransient<<"\n";
    }
    // Key limitation must remain visible in passing CI: a completely new
    // pitch on first sound cannot have been learned within a single ms.
    {
        HarmonicLivePrototype engine;
        engine.prepare(rate);engine.setVoiceSettings(settings());
        engine.enableEarlyOnset(true);
        engine.enableResidualPreservation(true);
        std::array<float,128> chunk{};
        for(int i=0;i<128;++i)
            chunk[static_cast<std::size_t>(i)]=source(rate,440.0*1.01747969,i);
        float* mono[1]={chunk.data()};
        engine.process(mono,1,128);
        if(engine.firstVoiceSample()>=0 || engine.researchWetMix()!=0.0)
            pass=false; // NO pretend 1ms initial F0 estimate or correction.
        std::cout<<"FIRST_1MS_UNTRACKED guaranteed_tuning=0"
                 <<" first_pitch_sample="<<engine.firstVoiceSample()
                 <<" processed_samples="<<engine.processedSamples()<<"\n";
    }
    std::cout<<"ONE_MS_TRANSIENT_RESEARCH "<<(pass?"PASS":"FAIL")
             <<" — 48-sample budget is for preserved transients in an"
               " established voiced segment, NOT first-note pitch detection,"
               " PDC, human musical quality or Apollo Solo roundtrip\n";
    return pass?0:1;
}

// NONSHIPPING EXPERIMENT: Can a high (>400Hz) already-confirmed note
// enter the causal predicted-wet path after half a source period rather
// than one full cycle? Test the actual audio samples, NOT merely wet
// control changes. Tight independent final-output pitch and click
// checks apply, and all <400Hz signals MUST remain bit-exact to baseline.
// There is no 1ms first F0 recognition or Antares equivalent claim.
#define main prior_subms_signal_path_main_not_called
#include "test_live_sub1ms_signal_path.cpp"
#undef main

namespace {
double firstChangedMs(const Render& r,double rate,double target) {
    const double sung=target*std::pow(2.0,30.0/1200.0);
    const int window=static_cast<int>(std::ceil(rate*0.001));
    double power=0.0;
    for(int i=0;i<static_cast<int>(r.audio.size());++i) {
        const double x=r.audio[static_cast<std::size_t>(i)]
                       -sample(rate,sung,i);
        power+=x*x;
        if(i>=window) {
            const double y=r.audio[static_cast<std::size_t>(i-window)]
                           -sample(rate,sung,i-window);
            power-=y*y;
        }
        if(i>=window-1 && std::sqrt(std::max(0.0,power/window))>=0.008)
            return 1000.0*(i-window+1)/rate;
    }
    return -1.0;
}
double finalPitchCents(const Render& r,double rate,double target) {
    APEX::TuneLive::PitchTracker check;
    check.prepare(rate);
    const int n=static_cast<int>(r.audio.size());
    for(int i=n-static_cast<int>(rate*0.16);i<n;++i)
        check.push(r.audio[static_cast<std::size_t>(i)]);
    const auto f=check.latest();
    return f.voiced && f.hz>0.0f
         ?1200.0*std::log2(f.hz/target):9999.0;
}
double maxOutputStep(const Render& r) {
    double step=0.0;
    for(std::size_t i=1;i<r.audio.size();++i)
        step=std::max(step,std::abs(static_cast<double>(r.audio[i])
                                 -r.audio[i-1]));
    return step;
}
}
int main() {
    bool safe=true;
    int highImproved=0,lowUnchanged=0,highCount=0,lowCount=0;
    for(double rate:std::array<double,4>{{44100.0,48000.0,96000.0,192000.0}})
      for(double target:std::array<double,2>{{220.0,440.0}})
        for(bool guarded:std::array<bool,2>{{false,true}}) {
            const auto old=run(rate,target,guarded,false,false);
            const auto candidate=run(rate,target,guarded,false,true);
            const double oldFirst=firstChangedMs(old,rate,target);
            const double newFirst=firstChangedMs(candidate,rate,target);
            const double oldWet=1000.0*old.firstHalfWetSample/rate;
            const double newWet=1000.0*candidate.firstHalfWetSample/rate;
            const double cents=finalPitchCents(candidate,rate,target);
            double maxDelta=0.0;
            for(std::size_t i=0;i<old.audio.size();++i)
                maxDelta=std::max(maxDelta,std::abs(
                    static_cast<double>(old.audio[i]-candidate.audio[i])));
            const bool soundSafe=std::abs(cents)<18.0 &&
                maxOutputStep(candidate)<0.65 && oldFirst>=0.0 &&
                newFirst>=0.0 && candidate.firstPitchSample==old.firstPitchSample;
            const bool upper=target>=400.0;
            const bool improved=upper && oldWet-newWet>=0.4 &&
                newFirst<=oldFirst && maxDelta>0.0001;
            const bool untouched=!upper && maxDelta==0.0 &&
                oldWet==newWet && oldFirst==newFirst;
            const bool ok=soundSafe && (upper?improved:untouched);
            safe &=ok;
            if(upper){++highCount;highImproved+=ok;}
            else{++lowCount;lowUnchanged+=ok;}
            std::cout<<"HALF_CYCLE_WET_AB rate="<<rate
                     <<" target="<<target<<" guarded="<<guarded
                     <<" old_wet50_ms="<<oldWet
                     <<" half_cycle_wet50_ms="<<newWet
                     <<" wet50_saved_ms="<<oldWet-newWet
                     <<" old_audio_change_ms="<<oldFirst
                     <<" half_cycle_audio_change_ms="<<newFirst
                     <<" audio_change_saved_ms="<<oldFirst-newFirst
                     <<" candidate_output_cents="<<cents
                     <<" max_output_step="<<maxOutputStep(candidate)
                     <<" high_improved="<<improved
                     <<" low_bit_exact="<<untouched
                     <<" pass="<<ok
                     <<" NOT_PERCEPTUAL_CERTIFICATION=1\n";
        }
    std::cout<<"HALF_CYCLE_WET_GATE high="<<highImproved<<"/"<<highCount
             <<" low_bit_exact="<<lowUnchanged<<"/"<<lowCount
             <<" verdict="<<(safe?"PASS":"FAIL")
             <<" ONLY_RESEARCH_NOT_1MS_FIRST_NOTE=1\n";
    return safe?0:1;
}

// Non-shipping: can the 80-frame candidate recognize a LIVE upper octave
// transition earlier than the existing 160-frame fast-YIN transition?
// Independent final audio pitch, stereo-safe partition invariance and
// NO premature upward false leap on steady A3 are mandatory.
// This experiment does NOT yield 1ms pitch-note response.
#define main previous_causal_leap_main_disabled
#include "test_causal_live_note_leaps.cpp"
#undef main
int main() {
    bool green=true;
    int improved=0;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        const auto previous=test(sr,false,true,false);
        const auto ultra=test(sr,false,true,true);
        const auto chunked=test(sr,true,true,true);
        double maxDifference=0.0;
        for(std::size_t i=0;i<ultra.output.size();++i)
            maxDifference=std::max(maxDifference,std::abs(
                static_cast<double>(ultra.output[i]-chunked.output[i])));
        const double before=1000.0*previous.lastFastTransition/sr;
        const double after=1000.0*ultra.lastFastTransition/sr;
        const double gain=before-after;
        const double cents=ultra.finalHz>0.0
            ?1200.0*std::log2(ultra.finalHz/440.0):9999.0;
        const bool earlier=previous.lastFastTransition>=0 &&
            ultra.lastFastTransition>=0 &&
            ultra.fastTransitions>=1 &&
            ultra.fastTransitions<=previous.fastTransitions+1 &&
            after>=450.0 && after<before-0.7;
        const bool safe=ultra.finite && chunked.finite &&
            ultra.noteJumps>=1 && ultra.maxStep<0.65 &&
            chunked.maxStep<0.65 && ultra.wet>0.95 &&
            maxDifference<1.0e-5 && std::abs(cents)<18.0;
        const bool pass=earlier && safe;
        improved+=pass;green &=pass;
        std::cout<<"ULTRA_FAST_LEAP sr="<<sr
                 <<" previous_leap_ms="<<before
                 <<" ultra_leap_ms="<<after
                 <<" saved_ms="<<gain
                 <<" ultra_jump_count="<<ultra.fastTransitions
                 <<" independent_wet_cents="<<cents
                 <<" partition_diff="<<maxDifference
                 <<" safe="<<safe
                 <<" passed="<<pass
                 <<" NO_1MS_SUNG_LEAP_CLAIM=1\n";
    }
    std::cout<<"ULTRA_FAST_LEAP_GATE earlier_and_safe="<<improved
             <<"/3 verdict="<<(green?"PASS":"FAIL")
             <<" physical_1ms_NOTE_RESPONSE=NOT_PROVEN\n";
    return green?0:1;
}

// Strict experimental A/B: accelerate octave-transition recognition using
// genuinely streamed, two-frame short-window YIN with octave veto.
// Underlying preexisting note-leap fixture supplies the AUDIO, not an F0
// oracle to either processor. Full YIN baseline remains unchanged.
// No claim of <=1ms pitch correction, tone preservation or device latency.
#define main old_causal_leap_gate_unused
#include "test_causal_live_note_leaps.cpp"
#undef main
int main() {
    bool safe=true;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        const auto baseline=test(sr,false,false);
        const auto accelerated=test(sr,false,true);
        const auto partitioned=test(sr,true,true);
        double partitionDifference=0.0;
        for(std::size_t i=0;i<accelerated.output.size();++i) {
            partitionDifference=std::max(partitionDifference,
                std::abs(static_cast<double>(accelerated.output[i]-
                                             partitioned.output[i])));
        }
        const double before=1000.0*baseline.lastJump/sr;
        const double after=1000.0*accelerated.lastFastTransition/sr;
        const double reduction=before-after;
        const double cents=accelerated.finalHz>0
            ?1200.0*std::log2(accelerated.finalHz/440.0):9999.0;
        const bool earlier=baseline.lastJump>=0 &&
             accelerated.lastFastTransition>=0 &&
             accelerated.fastTransitions>0 &&
             after>=450.0 && reduction>=1.0;
        const bool safeAudio=accelerated.finite && partitioned.finite &&
             accelerated.maxStep<0.65 && partitioned.maxStep<0.65 &&
             std::abs(cents)<18.0 && accelerated.wet>0.95 &&
             partitionDifference<1e-5 && accelerated.noteJumps>=1;
        safe &=earlier && safeAudio;
        std::cout<<"FAST_LEAP_AB sr="<<sr
             <<" reference_yin_jump_ms="<<before
             <<" early_estimate_jump_ms="<<after
             <<" advance_ms="<<reduction
             <<" early_transition_count="<<accelerated.fastTransitions
             <<" total_octave_guards="<<accelerated.noteJumps
             <<" block_difference="<<partitionDifference
             <<" final_cents="<<cents
             <<" max_output_step="<<accelerated.maxStep
             <<" faster="<<earlier
             <<" audio_safe="<<safeAudio<<"\n";
    }
    std::cout<<"FAST_LEAP_AB verdict="<<(safe?"PASS":"FAIL")
             <<" (strictly controlled synthetic signal, NO 1ms tonal-response"
               " or human quality claim)\n";
    return safe?0:1;
}

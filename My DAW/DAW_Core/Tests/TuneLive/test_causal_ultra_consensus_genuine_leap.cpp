// NONSHIPPING: true upward note-leap regression for the opt-in dual-YIN
// agreement filter. Verify the anti-glitch consensus doesn't silently
// disable all real octave transitions. This is actual streamed synthetic
// audio, NOT a known-note oracle passed to the engine; checks independent
// sustained output F0, glitch, time and arbitrary host callback partitions.
// This does not certify physical <=1ms corrected note / auditory quality.
#define main original_causal_leap_main_not_called
#include "test_causal_live_note_leaps.cpp"
#undef main
int main() {
    bool green=true;
    int passed=0;
    for(double sr:std::array<double,3>{{44100.0,48000.0,96000.0}}) {
        // Existing 8-hop unguarded path, fastest 4-hop unguarded path,
        // and the experimental fast-hop dual-consensus path.
        const auto baseline=test(sr,false,true,true,false,false);
        const auto accelerated=test(sr,false,true,true,true,false);
        const auto guarded=test(sr,false,true,true,true,true);
        const auto chunked=test(sr,true,true,true,true,true);
        double maxChunkDifference=0.0;
        for(std::size_t i=0;i<guarded.output.size();++i)
            maxChunkDifference=std::max(maxChunkDifference,std::abs(
                static_cast<double>(guarded.output[i]-chunked.output[i])));
        const double baselineMs=1000.0*baseline.lastFastTransition/sr;
        const double acceleratedMs=1000.0*accelerated.lastFastTransition/sr;
        const double guardedMs=1000.0*guarded.lastFastTransition/sr;
        const double cents=guarded.finalHz>0.0
            ?1200.0*std::log2(guarded.finalHz/440.0):9999.0;
        // Genuine A3->A4 happens at 450ms; if faster consensus returns no
        // fast transitions, that's a regression, even when full YIN corrects.
        // Permit slower confirmation vs pure speculative mode, bounded 15ms.
        const bool realLeap=baseline.fastTransitions>=1 &&
            accelerated.fastTransitions>=1 &&
            guarded.fastTransitions>=1 && guarded.lastFastTransition>=0 &&
            guardedMs>=450.0 && guardedMs<=baselineMs+15.0;
        const bool finalOutput=guarded.finite && chunked.finite &&
            guarded.noteJumps>=1 && guarded.wet>0.95 &&
            std::abs(cents)<18.0 && guarded.maxStep<0.65 &&
            chunked.maxStep<0.65 && maxChunkDifference<1e-5;
        const bool pass=realLeap && finalOutput;
        passed+=pass;green &=pass;
        std::cout<<"ULTRA_CONSENSUS_GENUINE_LEAP sr="<<sr
                 <<" baseline_ms="<<baselineMs
                 <<" fast_4hop_ms="<<acceleratedMs
                 <<" guarded_ms="<<guardedMs
                 <<" 4hop_count="<<accelerated.fastTransitions
                 <<" guarded_count="<<guarded.fastTransitions
                 <<" corrected_output_cents="<<cents
                 <<" max_step="<<guarded.maxStep
                 <<" callback_partition_diff="<<maxChunkDifference
                 <<" pass="<<pass
                 <<" NOT_1MS_OUTPUT_LATENCY=1\n";
    }
    std::cout<<"ULTRA_CONSENSUS_GENUINE_LEAP_GATE passed="<<passed
             <<"/3 verdict="<<(green?"PASS":"FAIL")
             <<" no_production_promotion=1\n";
    return green?0:1;
}

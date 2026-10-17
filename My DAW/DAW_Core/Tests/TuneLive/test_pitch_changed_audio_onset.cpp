// Nonshipping objective AUDIO gate. Do not confuse zero-sample consonant
// passage with the time at which any pitch-CHANGED output actually emerges.
// Stream the same original voiced waveform, compare the complete wet output
// against the untouched dry reference, and independently measure late F0.
// One 1ms sliding-difference RMS is a waveform-change detector, NOT proof
// that pitch has settled, that formants sound right, or of real PDC.
// There is NO input-F0 oracle in the processor.
#define main prior_lt1ms_transient_test_main
#include "test_live_sub1ms_signal_path.cpp"
#undef main

int main() {
    bool green=true;
    int passed=0;
    for(const double sr:std::array<double,4>{{44100.,48000.,96000.,192000.}})
      for(const double note:std::array<double,2>{{220.,440.}})
        for(const bool guarded:std::array<bool,2>{{false,true}}) {
            const auto result=run(sr,note,guarded,false);
            const double sung=note*std::pow(2.0,30.0/1200.0);
            const int frames=static_cast<int>(result.audio.size());
            const int window=std::max(1,static_cast<int>(std::ceil(sr*0.001)));
            double sumDiff=0.0,maxDifference=0.0;
            int firstSignificant=-1;
            double totalWetChange=0.0;
            for(int i=0;i<frames;++i) {
                const double dry=sample(sr,sung,i);
                const double wet=result.audio[static_cast<std::size_t>(i)];
                const double delta=wet-dry;
                const double squared=delta*delta;
                sumDiff+=squared;
                if(i>=window)
                    sumDiff-=std::pow(
                        result.audio[static_cast<std::size_t>(i-window)]
                        -sample(sr,sung,i-window),2);
                if(i>=window-1) {
                    const double rms=std::sqrt(
                        std::max(0.0,sumDiff/static_cast<double>(window)));
                    maxDifference=std::max(maxDifference,rms);
                    // Measurable signal DIFFERENCE, not an F0/tonal metric.
                    if(firstSignificant<0 && rms>=0.008)
                        firstSignificant=i-window+1;
                }
                if(i>frames/2)totalWetChange+=squared;
            }
            APEX::TuneLive::PitchTracker tracker;
            tracker.prepare(sr);
            const int count=static_cast<int>(sr*0.16);
            for(int i=frames-count;i<frames;++i)
                tracker.push(result.audio[static_cast<std::size_t>(i)]);
            const auto f0=tracker.latest();
            const double cents=f0.voiced&&f0.hz>0.0f?
                1200.0*std::log2(f0.hz/note):9999.0;
            const bool meaningful=firstSignificant>=result.firstPitchSample &&
                firstSignificant>=0 &&
                firstSignificant<static_cast<int>(sr*0.2);
            const bool wetSoundChanged=totalWetChange>0.01 &&
                maxDifference>0.008 && std::abs(cents)<18.0;
            const bool pass=meaningful&&wetSoundChanged;
            green &=pass;passed+=pass;
            std::cout<<"PITCH_CHANGED_AUDIO_ONSET sr="<<sr
                     <<" target="<<note<<" guarded="<<guarded
                     <<" pitch_detected_ms="<<1000.0*result.firstPitchSample/sr
                     <<" wet_control_50_ms="<<1000.0*result.firstHalfWetSample/sr
                     <<" first_1ms_audio_difference_ms="
                     <<1000.0*firstSignificant/sr
                     <<" first_difference_after_pitch_ms="
                     <<1000.0*(firstSignificant-result.firstPitchSample)/sr
                     <<" max_window_difference_rms="<<maxDifference
                     <<" independent_corrected_output_cents="<<cents
                     <<" pass="<<pass
                     <<" NOT_1MS_SETTLED_PITCH_OR_PDC=1\n";
        }
    std::cout<<"PITCH_CHANGED_AUDIO_ONSET_GATE passed="<<passed
             <<"/16 verdict="<<(green?"PASS":"FAIL")
             <<" 1ms_signal_through_DOES_NOT_MEAN_1ms_pitch_changed_audio=1\n";
    return green?0:1;
}

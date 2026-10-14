#pragma once
// Experimental period-guided causal delay-tap pitch shifter.
// Portable C++17; independent from shipping APEX Tune Live inserts.
// NOT a fixed-PDC design, Antares parity, or real-vocal quality proof.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX { namespace TuneLive { namespace Research {
class PeriodGuidedPrototype final {
public:
    static constexpr int kCapacity=8192;

    void prepare(double sr, int channels=1) noexcept {
        rate_=(sr>=8000.0 && sr<=192000.0 && std::isfinite(sr)) ? sr : 48000.0;
        channels_=std::clamp(channels,1,2);
        // Prepare-time coefficient computation; never call exp() from the
        // per-sample realtime loop merely to rebuild unchanged constants.
        periodAlpha_=1.0-std::exp(-1.0/(rate_*0.010));
        ratioAlpha_=1.0-std::exp(-1.0/(rate_*0.004));
        wetAlpha_=1.0-std::exp(-1.0/(rate_*0.008));
        reset();
    }
    void reset() noexcept {
        for(auto& channel:history_) channel.fill(0.0f);
        position_=0;
        phase_=0.5;
        period_=std::clamp(rate_/220.0,16.0,static_cast<double>((kCapacity-8)/2));
        targetPeriod_=period_;
        span_=2.0*period_;
        ratio_=targetRatio_=1.0;
        wetMix_=targetWet_=0.0;
        active_=false;
    }
    // Requires an independently estimated *source* F0. The current prototype
    // assumes stable voiced pitch and has no note-change smoothing.
    void setPeriodAndRatio(double sourceHz,double outputOverInput) noexcept {
        if(!std::isfinite(sourceHz) || sourceHz<55.0 || sourceHz>1200.0 ||
           !std::isfinite(outputOverInput)) {
            targetWet_=0.0;
            targetRatio_=1.0;
            return;
        }
        targetPeriod_=std::clamp(rate_/sourceHz,16.0,
                                 static_cast<double>((kCapacity-8)/2));
        targetRatio_=std::clamp(outputOverInput,0.85,1.15);
        // Never jump the playback delay across source pitch changes.
        // The first valid estimate starts at its detected period; subsequent
        // estimates are smoothed sample by sample during process().
        if(!active_) {
            period_=targetPeriod_;
            ratio_=targetRatio_;
            span_=2.0*period_;
            phase_=0.5;
        }
        targetWet_=1.0;
        active_=true;
    }
    // A buffer HISTORY bound, NOT a host-reported algorithmic latency.
    int requiredHistorySamples() const noexcept {
        // Conservative bound covering the current and pending periods.
        return static_cast<int>(std::ceil(2.0*std::max(period_,targetPeriod_)))+3;
    }
    double smoothedRatio() const noexcept {return ratio_;}
    double wetMix() const noexcept {return wetMix_;}
    void process(float* const* channelData,int channels,int n) noexcept {
        if(!channelData || channels!=channels_ || n<=0 ||
           !channelData[0] || (channels_==2 && !channelData[1])) return;
        for(int i=0;i<n;++i) {
            period_+=(targetPeriod_-period_)*periodAlpha_;
            ratio_+=(targetRatio_-ratio_)*ratioAlpha_;
            wetMix_+=(targetWet_-wetMix_)*wetAlpha_;
            // Two heads stay exactly one source pitch period apart.
            span_=2.0*period_;
            for(int c=0;c<channels_;++c) {
                const float sample=channelData[c][i];
                history_[static_cast<std::size_t>(c)][static_cast<std::size_t>(position_)] =
                    std::isfinite(sample)?std::clamp(sample,-8.0f,8.0f):0.0f;
            }
            const double phase1=phase_;
            const double phase2=phase_<0.5 ? phase_+0.5 : phase_-0.5;
            // Each read head has zero contribution at its discontinuous reset.
            const double a=std::sin(3.14159265358979323846*phase1);
            const double weight=a*a;
            for(int c=0;c<channels_;++c) {
                const double dry=read(c,1.0+period_);
                const double corrected=weight*read(c,1.0+span_*phase1)
                                      +(1.0-weight)*read(c,1.0+span_*phase2);
                // Smoothly enter/leave pitch work on unvoiced segments.
                // Dry is period-delayed and NOT a certified fixed PDC path.
                const double sample=wetMix_*corrected+(1.0-wetMix_)*dry;
                channelData[c][i]=std::isfinite(sample)
                    ?static_cast<float>(std::clamp(sample,-8.0,8.0)):0.0f;
            }
            if(active_) {
                phase_+=(1.0-ratio_)/span_;
                if(phase_<0.0) phase_+=1.0;
                else if(phase_>=1.0) phase_-=1.0;
            }
            position_=(position_+1)%kCapacity;
        }
    }
private:
    double read(int c,double delay) const noexcept {
        // delay is always within [0, kCapacity), position_ is a ring index.
        // One conditional wrap replaces the expensive per-sample std::fmod.
        double wrapped=static_cast<double>(position_)-delay;
        if(wrapped<0.0) wrapped+=static_cast<double>(kCapacity);
        const int index=static_cast<int>(wrapped);
        const int next=(index+1)%kCapacity;
        const double mix=wrapped-index;
        const auto& history=history_[static_cast<std::size_t>(c)];
        return (1.0-mix)*history[static_cast<std::size_t>(index)]
                       +mix*history[static_cast<std::size_t>(next)];
    }
    std::array<std::array<float,kCapacity>,2> history_{};
    double rate_=48000.0,period_=218.0,targetPeriod_=218.0,span_=436.0,
           ratio_=1.0,targetRatio_=1.0,phase_=0.5,wetMix_=0.0,targetWet_=0.0;
    double periodAlpha_=0.0,ratioAlpha_=0.0,wetAlpha_=0.0;
    int channels_=1,position_=0;
    bool active_=false;
};
}}} // namespace APEX::TuneLive::Research

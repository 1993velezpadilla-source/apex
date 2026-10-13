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
        reset();
    }
    void reset() noexcept {
        for(auto& channel:history_) channel.fill(0.0f);
        position_=0;
        phase_=0.5;
    }
    // Requires an independently estimated *source* F0. The current prototype
    // assumes stable voiced pitch and has no note-change smoothing.
    void setPeriodAndRatio(double sourceHz,double outputOverInput) noexcept {
        if(!std::isfinite(sourceHz) || sourceHz<55.0 || sourceHz>1200.0 ||
           !std::isfinite(outputOverInput)) { active_=false; return; }
        period_=std::clamp(rate_/sourceHz,16.0,static_cast<double>((kCapacity-8)/2));
        // Two read heads are separated by one source pitch period, so
        // a periodic waveform has coherent phases at grain wrap boundaries.
        span_=2.0*period_;
        ratio_=std::clamp(outputOverInput,0.85,1.15);
        active_=true;
    }
    // A buffer HISTORY bound, NOT a host-reported algorithmic latency.
    int requiredHistorySamples() const noexcept {
        return static_cast<int>(std::ceil(span_))+3;
    }
    void process(float* const* channelData,int channels,int n) noexcept {
        if(!channelData || channels!=channels_ || n<=0 ||
           !channelData[0] || (channels_==2 && !channelData[1])) return;
        for(int i=0;i<n;++i) {
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
                double sample;
                if(!active_ || std::abs(ratio_-1.0)<1.0e-12) {
                    sample=read(c,1.0+period_);
                } else {
                    sample=weight*read(c,1.0+span_*phase1)
                         +(1.0-weight)*read(c,1.0+span_*phase2);
                }
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
        const double position=static_cast<double>(position_)-delay;
        double wrapped=std::fmod(position,static_cast<double>(kCapacity));
        if(wrapped<0.0) wrapped+=kCapacity;
        const int index=static_cast<int>(wrapped);
        const int next=(index+1)%kCapacity;
        const double mix=wrapped-index;
        const auto& history=history_[static_cast<std::size_t>(c)];
        return (1.0-mix)*history[static_cast<std::size_t>(index)]
                       +mix*history[static_cast<std::size_t>(next)];
    }
    std::array<std::array<float,kCapacity>,2> history_{};
    double rate_=48000.0,period_=218.0,span_=436.0,ratio_=1.0,phase_=0.5;
    int channels_=1,position_=0;
    bool active_=false;
};
}}} // namespace APEX::TuneLive::Research

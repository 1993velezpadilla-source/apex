#pragma once
// Experimental ULTRA-HIGH REGISTER YIN hypothesis: 80-frame window.
// Strict 430–1000Hz research range. 392–420Hz stays with the longer mid-register detector.
// Research-only SHORT-HISTORY YIN onset estimator. Not a replacement for
// the full C2-capable YIN authority, not shipped, and not a PDC statement.
// Its harmonic short-window estimate requires two agreeing frames. Frequencies
// below 430 Hz intentionally have no early-onset claim.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace APEX { namespace TuneLive { namespace Research {
class UltraHighRegisterProbe final {
public:
    static constexpr int kWindow=80;
    static constexpr int kMaxLag=32;
    static constexpr int kHop=8;
    struct Result {float hz=0.0f,confidence=0.0f;bool voiced=false;};
    void prepare(double rate) noexcept {
        rate_=(std::isfinite(rate)&&rate>8000.0&&rate<=192000.0)?rate:48000.0;
        decimation_=std::clamp(static_cast<int>(std::lround(rate_/12000.0)),1,16);
        detectorRate_=rate_/decimation_;
        pole_=std::exp(-2.0*3.14159265358979323846*1800.0/rate_);
        minLag_=std::max(2,static_cast<int>(detectorRate_/1000.0));
        maxLag_=std::clamp(static_cast<int>(std::ceil(detectorRate_/430.0)),
                           minLag_+2,kMaxLag);
        reset();
    }
    void reset() noexcept {
        ring_.fill(0.0f);frame_.fill(0.0f);cmndf_.fill(1.0f);
        low1_=low2_=0.0;ringPosition_=filled_=substep_=hop_=0;
        stableCount_=0;priorCandidate_=0.0f;result_={};firstSample_=-1;
        samples_=0; octaveVetoCount_=0;
    }
    // Returns true when a new analysis was produced. The latest result can
    // be unvoiced. Call once per ORIGINAL audio input sample.
    bool push(float input) noexcept {
        ++samples_;
        if(!std::isfinite(input))input=0.0f;
        low1_=pole_*low1_+(1.0-pole_)*input;
        low2_=pole_*low2_+(1.0-pole_)*low1_;
        if(++substep_<decimation_)return false;
        substep_=0;
        ring_[static_cast<std::size_t>(ringPosition_)]=static_cast<float>(low2_);
        ringPosition_=(ringPosition_+1)%kWindow;
        if(filled_<kWindow)++filled_;
        if(++hop_<kHop)return false;
        hop_=0;
        if(filled_<kWindow)return false;
        const Result candidate=measure();
        if(candidate.voiced && priorCandidate_>0.0f &&
           std::abs(1200.0f*std::log2(candidate.hz/priorCandidate_))<35.0f) {
            stableCount_=std::min(2,stableCount_+1);
        } else {
            stableCount_=candidate.voiced?1:0;
        }
        priorCandidate_=candidate.voiced?candidate.hz:0.0f;
        result_=stableCount_>=2?candidate:Result{};
        if(result_.voiced && firstSample_<0)firstSample_=samples_;
        return true;
    }
    Result latest() const noexcept {return result_;}
    int64_t firstVoicedSample() const noexcept {return firstSample_;}
    double minimumWindowMs() const noexcept {return 1000.0*kWindow/detectorRate_;}
    int octaveVetoCount() const noexcept {return octaveVetoCount_;}
private:
    Result measure() noexcept {
        const int start=ringPosition_;
        double energy=0.0;
        for(int i=0;i<kWindow;++i) {
            const float f=ring_[static_cast<std::size_t>((start+i)%kWindow)];
            frame_[static_cast<std::size_t>(i)]=f;
            energy+=static_cast<double>(f)*f;
        }
        if(energy<kWindow*0.00002)return {};
        const int sumLen=kWindow-maxLag_;
        double sum=0.0;
        int best=-1;
        float bestCmndf=1.0f;
        for(int lag=1;lag<=maxLag_;++lag) {
            double d=0.0;
            for(int i=0;i<sumLen;++i) {
                const double delta=static_cast<double>(frame_[static_cast<std::size_t>(i)])-
                             frame_[static_cast<std::size_t>(i+lag)];
                d+=delta*delta;
            }
            sum+=d;
            const float norm=sum>1e-16?static_cast<float>(d*lag/sum):1.0f;
            cmndf_[static_cast<std::size_t>(lag)]=norm;
            if(lag>minLag_) {
                const float previous=cmndf_[static_cast<std::size_t>(lag-1)];
                if(previous<0.12f && norm>=previous) {
                    best=lag-1; bestCmndf=previous;break;
                }
            }
        }
        if(best<0) {
            // With a boundary minimum, do not trust an arbitrary low CMNDF
            // point unless the last observable lag itself is very clean.
            if(cmndf_[static_cast<std::size_t>(maxLag_)]<0.06f) {
                best=maxLag_; bestCmndf=cmndf_[static_cast<std::size_t>(best)];
            }
        }
        if(best<=minLag_ || bestCmndf>0.12f)return {};
        double period=best;
        if(best>minLag_ && best<maxLag_) {
            const double a=cmndf_[static_cast<std::size_t>(best-1)];
            const double b=cmndf_[static_cast<std::size_t>(best)];
            const double c=cmndf_[static_cast<std::size_t>(best+1)];
            const double denom=a-2.0*b+c;
            if(std::abs(denom)>1.0e-9)
                period+=std::clamp(0.5*(a-c)/denom,-0.5,0.5);
        }
        const float hz=static_cast<float>(detectorRate_/period);
        if(!std::isfinite(hz)||hz<430.0f||hz>1000.0f)return {};
        // Guard against notes 220–400Hz masquerading as upper harmonics.
        // A real 220-Hz F0 with a strong 440-Hz harmonic can fool
        // an ~6.7ms short-window YIN detector into a premature A4 lock.
        // Short window MUST still reject false octave at >=20 matched source samples.
        // Compare the observed waveform error at the candidate lag and
        // TWO candidate periods over an IDENTICAL sample support. A true
        // 220-Hz source repeats at both; a 110-Hz source with audible odd
        // harmonics has substantially less error at the doubled lag.
        // Fractional interpolation avoids rejecting genuine 220-Hz sources
        // merely because the detector's true period ends in half a sample.
        const double twice=2.0*period;
        const int count=kWindow-static_cast<int>(std::ceil(twice))-2;
        if(count>=20) {
            const int lo1=static_cast<int>(period);
            const int lo2=static_cast<int>(twice);
            const double mix1=period-lo1, mix2=twice-lo2;
            double e1=0.0,e2=0.0,reference=0.0;
            for(int i=0;i<count;++i) {
                const double a=frame_[static_cast<std::size_t>(i)];
                const double b=frame_[static_cast<std::size_t>(i+lo1)]*(1.0-mix1)
                              +frame_[static_cast<std::size_t>(i+lo1+1)]*mix1;
                const double c=frame_[static_cast<std::size_t>(i+lo2)]*(1.0-mix2)
                              +frame_[static_cast<std::size_t>(i+lo2+1)]*mix2;
                e1+=(a-b)*(a-b);
                e2+=(a-c)*(a-c);
                reference+=a*a;
            }
            if(reference>1e-8 && e1/reference>0.0002 && e2<e1*0.20) {
                ++octaveVetoCount_;
                return {};
            }
        }
        return {hz,1.0f-bestCmndf,true};
    }
    std::array<float,kWindow> ring_{},frame_{};
    std::array<float,kMaxLag+1> cmndf_{};
    double rate_=48000.0,detectorRate_=12000.0,pole_=0.0,
           low1_=0.0,low2_=0.0;
    int decimation_=4,minLag_=12,maxLag_=72;
    int ringPosition_=0,filled_=0,substep_=0,hop_=0,stableCount_=0;
    int octaveVetoCount_=0;
    int64_t samples_=0,firstSample_=-1;
    float priorCandidate_=0.0f;
    Result result_{};
};
}}} // namespace APEX::TuneLive::Research

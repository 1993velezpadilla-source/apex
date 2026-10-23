#pragma once
// APEX Bubblegum Limiter — original C++17 predictive peak limiting DSP.
// NOT a clone of FabFilter's proprietary algorithms. No JUCE dependency.
// All ring/deque storage is allocated in prepare(), never in process().
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace APEX { namespace Limiter {

enum class Style : int { Transparent, Punchy, Dynamic, Allround,
                         Aggressive, Modern, Bus, Safe };

struct Settings
{
    double gainDb = 0.0;
    double ceilingDb = -1.0;
    double lookaheadMs = 5.0;
    double attackMs = 0.5;
    double releaseMs = 140.0;
    double channelLink = 1.0; // attack / transient stereo link
    double releaseLink = 1.0; // independent release stereo link
    int style = static_cast<int>(Style::Modern);
    int detectorQuality = 2; // 0=sample peak, 1=2x, 2=4x, 3=8x
    bool truePeak = true;
    bool bypass = false;
    bool unityGain = false;
    bool audition = false;
    bool dcFilter = false;
    int ditherMode = 0; // 0 off, 1 TPDF, 2 first-order noise shaping
    int ditherBits = 24;

    static Settings sanitized(Settings p) noexcept
    {
        if (!std::isfinite(p.gainDb)) p.gainDb = 0.0;
        if (!std::isfinite(p.ceilingDb)) p.ceilingDb = -1.0;
        if (!std::isfinite(p.lookaheadMs)) p.lookaheadMs = 5.0;
        if (!std::isfinite(p.attackMs)) p.attackMs = 0.5;
        if (!std::isfinite(p.releaseMs)) p.releaseMs = 140.0;
        if (!std::isfinite(p.channelLink)) p.channelLink = 1.0;
        if (!std::isfinite(p.releaseLink)) p.releaseLink = 1.0;
        p.gainDb = std::clamp(p.gainDb, 0.0, 30.0);
        p.ceilingDb = std::clamp(p.ceilingDb, -30.0, 0.0);
        p.lookaheadMs = std::clamp(p.lookaheadMs, 0.1, 20.0);
        p.attackMs = std::clamp(p.attackMs, 0.01, 1000.0);
        p.releaseMs = std::clamp(p.releaseMs, 10.0, 3000.0);
        p.channelLink = std::clamp(p.channelLink, 0.0, 1.0);
        p.releaseLink = std::clamp(p.releaseLink, 0.0, 1.0);
        p.style = std::clamp(p.style, 0, 7);
        p.detectorQuality = std::clamp(p.detectorQuality, 0, 3);
        p.ditherMode = std::clamp(p.ditherMode, 0, 2);
        p.ditherBits = p.ditherBits <= 16 ? 16 : 24;
        return p;
    }
};

class SlidingPeak final
{
public:
    void prepare(int capacity)
    {
        nodes_.assign(static_cast<size_t>(std::max(4,capacity)), Node{});
        clear();
    }
    void clear() noexcept { front_ = 0; count_ = 0; }
    void add(int64_t index, double value, int64_t oldest) noexcept
    {
        if (nodes_.empty()) return;
        const auto capacity = static_cast<int>(nodes_.size());
        while (count_ && nodes_[static_cast<size_t>(front_)].index < oldest)
        {
            front_ = (front_ + 1) % capacity;
            --count_;
        }
        while (count_)
        {
            const int tail = (front_ + count_ - 1) % capacity;
            if (nodes_[static_cast<size_t>(tail)].value > value) break;
            --count_;
        }
        // Window never exceeds the preallocated lookahead ring.
        if (count_ == capacity) { front_ = (front_ + 1) % capacity; --count_; }
        const int target = (front_ + count_) % capacity;
        nodes_[static_cast<size_t>(target)] = {index, value};
        ++count_;
    }
    double maximum() const noexcept
    {
        return count_ ? nodes_[static_cast<size_t>(front_)].value : 0.0;
    }
private:
    struct Node { int64_t index = 0; double value = 0.0; };
    std::vector<Node> nodes_;
    int front_ = 0, count_ = 0;
};

class Core final
{
public:
    void prepare(double rate)
    {
        rate_ = std::isfinite(rate) && rate >= 8000.0 ? rate : 48000.0;
        // Reserve FIR's 32 input-frame reconstruction guard in the fixed
        // host-reported latency; the 20ms user lookahead remains available.
        delaySamples_ = std::max(4, static_cast<int>(std::ceil(rate_ * 0.020))
                            + kFirHalf + 4);
        const int capacity = delaySamples_ + 8;
        for (auto& c : channel_)
        {
            c.input.assign(static_cast<size_t>(capacity), 0.0f);
            c.dry.assign(static_cast<size_t>(capacity), 0.0f);
            c.peak.assign(static_cast<size_t>(capacity), 0.0f);
            c.maximum.prepare(capacity);
        }
        prepareFIRCoefficients();
        reset();
    }
    void reset() noexcept
    {
        for (auto& c : channel_)
        {
            std::fill(c.input.begin(), c.input.end(), 0.0f);
            std::fill(c.dry.begin(), c.dry.end(), 0.0f);
            std::fill(c.peak.begin(), c.peak.end(), 0.0f);
            c.maximum.clear();
            c.dcX = c.dcY = c.reduction = 0.0;
            c.ditherError=0.0;
            c.rng = (&c == &channel_[0]) ? 0x12345678u : 0x91ABCDEFu;
        }
        index_ = 0;
        lookaheadSamples_ = -1;
        runningGain_ = 1.0;
        bypassBlend_ = settings_.bypass ? 0.0 : 1.0;
        reductionMeterDb_ = 0.0;
        blockReductionMaxDb_ = 0.0;
        peakInput_ = peakOutput_ = 0.0;
    }
    void setSettings(Settings p) noexcept { settings_ = Settings::sanitized(p); }
    const Settings& getSettings() const noexcept { return settings_; }
    int latencySamples() const noexcept { return delaySamples_; }
    double reductionDb() const noexcept { return reductionMeterDb_; }
    // Maximum instantaneous gain reduction observed in the last process()
    // span. Unlike the end-of-chunk reading, this is invariant to host
    // callback segmentation when accumulated on fixed-time display frames.
    double blockReductionMaxDb() const noexcept { return blockReductionMaxDb_; }
    double inputPeak() const noexcept { return peakInput_; }
    double outputPeak() const noexcept { return peakOutput_; }

    void process(float* left, float* right, int frames) noexcept
    {
        if (!left || frames <= 0 || channel_[0].input.empty()) return;
        const bool stereo = right && right != left;
        const int count = static_cast<int>(channel_[0].input.size());
        const int lookahead = std::clamp(static_cast<int>(std::lround(
            settings_.lookaheadMs * rate_ / 1000.0)), 1,
            delaySamples_ - kFirHalf - 2);
        if (lookahead != lookaheadSamples_)
        {
            // Re-seed prediction on control changes; output's local
            // instantaneous guard remains active during the transition.
            for (auto& c : channel_) c.maximum.clear();
            lookaheadSamples_ = lookahead;
        }
        const double ceiling = std::pow(10.0, settings_.ceilingDb / 20.0);
        const double targetGain = std::pow(10.0, settings_.gainDb / 20.0);
        const double smooth = std::exp(-1.0 / (0.005 * rate_));
        const double bypassSmooth = std::exp(-1.0 / (0.003 * rate_));
        const double dcPole = std::exp(-2.0 * 3.141592653589793 * 12.0 / rate_);
        static constexpr double attackScale[]  = {1.0,0.6,1.5,1.1,0.3,0.65,0.8,1.9};
        static constexpr double releaseScale[] = {1.3,0.7,0.75,1.0,0.5,0.85,0.6,2.2};
        const int style = settings_.style;
        const double attackCoeff = std::exp(-1.0 / std::max(1.0,
            settings_.attackMs * 0.001 * rate_ * attackScale[style]));
        const double releaseCoeff = std::exp(-1.0 / std::max(1.0,
            settings_.releaseMs * 0.001 * rate_ * releaseScale[style]));
        const double link = settings_.channelLink;
        const double desiredBlend = settings_.bypass ? 0.0 : 1.0;
        peakInput_ = peakOutput_ = 0.0;
        blockReductionMaxDb_ = 0.0;
        for (int i=0; i<frames; ++i, ++index_)
        {
            runningGain_ = smooth * runningGain_ + (1.0-smooth) * targetGain;
            bypassBlend_ = bypassSmooth * bypassBlend_ + (1.0-bypassSmooth) * desiredBlend;
            const int w = static_cast<int>(index_ % count);
            const int64_t outputIndex = index_ - delaySamples_;
            const int64_t keyIndex = outputIndex + lookahead;
            double raw[2] = {safe(left[i]), stereo ? safe(right[i]) : 0.0};
            double delayed[2] = {0, 0};
            double dryDelayed[2] = {0, 0};
            double required[2] = {0, 0};
            const int active = stereo ? 2 : 1;
            for (int ch=0; ch<active; ++ch)
            {
                auto& c = channel_[static_cast<size_t>(ch)];
                double x = raw[ch] * runningGain_;
                if (settings_.dcFilter)
                {
                    const double high = x - c.dcX + dcPole * c.dcY;
                    c.dcX = x; c.dcY = high;
                    x = high;
                }
                else { c.dcX = x; c.dcY = 0.0; }
                c.input[static_cast<size_t>(w)] = static_cast<float>(x);
                c.dry[static_cast<size_t>(w)] = static_cast<float>(raw[ch]);
                // Always retain the observed sample peak. With ISP enabled,
                // reconstruct fractional intersample peaks using windowed-
                // sinc FIR (64 taps, 2x/4x/8x). This is NOT an ITU-certified
                // true-peak or full audio-oversampling implementation.
                c.peak[static_cast<size_t>(w)] = static_cast<float>(std::abs(x));
                if (settings_.truePeak && settings_.detectorQuality > 0
                    && index_ >= 2*kFirHalf-1)
                {
                    // Reconstruction of the interval [n-32,n-31) requires
                    // inputs through n. Its peak is stored 32 samples back.
                    // Fixed latency reserves this full FIR lookahead so that
                    // the peak deque only reads completed measurements.
                    const int64_t centre=index_ - kFirHalf;
                    const int peakSlot=static_cast<int>(centre % count);
                    double reconstructionPeak=c.peak[static_cast<size_t>(peakSlot)];
                    const int quality=settings_.detectorQuality;
                    const int factor=1 << quality;
                    for(int phase=1;phase<factor;++phase)
                    {
                        const auto& taps=firCoefficients_[static_cast<size_t>(quality)]
                                                         [static_cast<size_t>(phase)];
                        double sample=0.0;
                        for(int tap=0;tap<kFirTaps;++tap)
                        {
                            const int64_t source=centre+tap-(kFirHalf-1);
                            const int slot=static_cast<int>(source%count);
                            sample+=taps[static_cast<size_t>(tap)]
                                   *c.input[static_cast<size_t>(slot)];
                        }
                        reconstructionPeak=std::max(reconstructionPeak,std::abs(sample));
                    }
                    c.peak[static_cast<size_t>(peakSlot)]
                        =static_cast<float>(reconstructionPeak);
                }
                if (keyIndex >= 0)
                {
                    const int keySlot = static_cast<int>(keyIndex % count);
                    c.maximum.add(keyIndex,
                        c.peak[static_cast<size_t>(keySlot)], outputIndex);
                }
                const double future = c.maximum.maximum();
                const double over = future / std::max(1.0e-12,ceiling);
                required[ch] = over > 1.0 ? 20.0*std::log10(over) : 0.0;
                if (outputIndex >= 0)
                {
                    const int read = static_cast<int>(outputIndex % count);
                    delayed[ch] = c.input[static_cast<size_t>(read)];
                    dryDelayed[ch] = c.dry[static_cast<size_t>(read)];
                }
                peakInput_ = std::max(peakInput_,std::abs(raw[ch]));
            }
            const double linked = std::max(required[0],required[1]);
            for (int ch=0; ch<active; ++ch)
            {
                auto& c = channel_[static_cast<size_t>(ch)];
                const bool rising=required[ch]>c.reduction;
                const double stereoLink=rising?link:settings_.releaseLink;
                const double target=(1.0-stereoLink)*required[ch] + stereoLink*linked;
                const double coeff=target>c.reduction?attackCoeff:releaseCoeff;
                c.reduction = coeff*c.reduction + (1.0-coeff)*target;
                // Last-resort sample guard: attack automation, unexpected
                // peaks and bypass transitions must never create NaN/Inf.
                const double reductionGain = std::pow(10.0,-c.reduction/20.0);
                double processed = delayed[ch] * reductionGain;
                if (std::abs(processed) > ceiling)
                    processed = std::copysign(ceiling,processed);
                if (settings_.unityGain)
                    processed /= std::max(1.0e-12,runningGain_);
                if (settings_.audition)
                    processed = processed - delayed[ch];
                // Dither follows the limiter; disabled by default, with two
                // independently seeded bounded channel RNGs. The optional
                // first-order shaping feeds back quantization error only.
                if (settings_.ditherMode > 0 && !settings_.audition)
                {
                    const double lsb=std::ldexp(1.0,1-settings_.ditherBits);
                    const auto uniform=[&c]() noexcept {
                        c.rng=c.rng*1664525u+1013904223u;
                        return static_cast<double>(c.rng>>8)/16777216.0;
                    };
                    const double tpdf=(uniform()-uniform())*lsb;
                    const double shaped=settings_.ditherMode==2
                        ? std::clamp(c.ditherError,-lsb,lsb)*0.85 : 0.0;
                    const double candidate=processed+tpdf+shaped;
                    const double quantized=std::round(candidate/lsb)*lsb;
                    c.ditherError=settings_.ditherMode==2
                        ? std::clamp(candidate-quantized,-lsb,lsb) : 0.0;
                    processed=std::clamp(quantized,-ceiling,ceiling);
                }
                const double out = dryDelayed[ch]*(1.0-bypassBlend_)
                                   + processed*bypassBlend_;
                const float result = static_cast<float>(safe(out));
                if (ch == 0) left[i] = result;
                else right[i] = result;
                peakOutput_ = std::max(peakOutput_,std::abs(static_cast<double>(result)));
            }
            reductionMeterDb_ = std::max(channel_[0].reduction,
                                        stereo ? channel_[1].reduction : 0.0);
            blockReductionMaxDb_ = std::max(blockReductionMaxDb_,reductionMeterDb_);
        }
    }
private:
    static constexpr int kFirHalf=32;
    static constexpr int kFirTaps=2*kFirHalf;

    // All FIR coefficients are calculated at prepare(), never from process().
    // Phases represent fractional samples in the interval immediately after
    // the stored source sample. DC normalization preserves a unity baseline.
    void prepareFIRCoefficients() noexcept
    {
        constexpr double pi=3.14159265358979323846;
        for(int quality=1;quality<=3;++quality)
        {
            const int factor=1<<quality;
            for(int phase=1;phase<factor;++phase)
            {
                auto& taps=firCoefficients_[static_cast<size_t>(quality)]
                                           [static_cast<size_t>(phase)];
                const double fraction=static_cast<double>(phase)/factor;
                double sum=0.0;
                for(int tap=0;tap<kFirTaps;++tap)
                {
                    const double delta=tap-(kFirHalf-1)-fraction;
                    const double absDelta=std::abs(delta);
                    const double sinc=absDelta<1.0e-12 ? 1.0
                           : std::sin(pi*delta)/(pi*delta);
                    const double window=absDelta>=kFirHalf ? 0.0
                        : 0.42+0.5*std::cos(pi*delta/kFirHalf)
                              +0.08*std::cos(2.0*pi*delta/kFirHalf);
                    const double coefficient=sinc*window;
                    taps[static_cast<size_t>(tap)]=coefficient;
                    sum+=coefficient;
                }
                if(std::abs(sum)>1.0e-12)
                    for(auto& coefficient:taps)coefficient/=sum;
            }
        }
    }
    static double safe(double x) noexcept
    {
        return std::isfinite(x) && std::abs(x) < 1.0e8 ? x : 0.0;
    }
    struct Channel
    {
        std::vector<float> input, dry, peak;
        SlidingPeak maximum;
        double dcX=0, dcY=0, reduction=0, ditherError=0;
        uint32_t rng=0x12345678u;
    };
    std::array<Channel,2> channel_;
    std::array<std::array<std::array<double,kFirTaps>,8>,4> firCoefficients_{};
    Settings settings_;
    double rate_=48000.0, runningGain_=1.0, bypassBlend_=1.0;
    double reductionMeterDb_=0.0, blockReductionMaxDb_=0.0;
    double peakInput_=0.0, peakOutput_=0.0;
    int delaySamples_=0, lookaheadSamples_=-1;
    int64_t index_=0;
};

}} // namespace APEX::Limiter

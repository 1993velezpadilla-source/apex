#pragma once

// APEX Precision C3 - original clean-room dynamics DSP, not FabFilter source,
// not a bit-identical/sonically calibrated Pro-C 3 model.
// C++17 portable; all audio-callback work is allocation-free and lock-free.
// The 14 mode names describe musical goals; precise proprietary transfer
// curves, TTM multiband and hardware characters require independent validation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace APEX::PrecisionC3
{
// Four independently automatable bell bands follow the existing HP and LP.
// This is six detector filters in total, not a claim of FabFilter's broader
// Pro-Q-style filter shapes. All processing runs on the detector ONLY.
struct SidechainBellBand
{
    double hz = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
};
struct Parameters
{
    double thresholdDb = -18.0;
    double ratio = 4.0;
    double kneeDb = 12.0;
    double rangeDb = 60.0;
    double attackMs = 10.0;
    double releaseMs = 120.0;
    double holdMs = 0.0;
    // Delays the MAIN/WET/DRY/BYPASS audio while the detector reads the
    // current input. This is feed-forward lookahead with real host PDC.
    double lookaheadMs = 0.0; // 0..20 ms, 0 preserves live tracking latency
    double inputDb = 0.0;
    double wetDb = 0.0;
    double dryPercent = 0.0; // legacy C3 beta session state, 0..100
    // Separate true Dry Gain in dB per reference dynamics workflow.
    // -100 = fully muted. When at this default value, preserve the old
    // beta percentage control for existing session/project recall.
    double dryGainDb = -100.0;
    double outputDb = 0.0;
    double sidechainHighpassHz = 0.0;  // 0 disables
    double sidechainLowpassHz = 20000.0;
    std::array<SidechainBellBand,4> sidechainBells {{
        {130.0,0.0,1.0}, {680.0,0.0,1.0},
        {2700.0,0.0,1.0}, {9300.0,0.0,1.0}
    }};
    double stereoLinkPercent = 100.0;
    double drive = 0.0; // 0..100%
    int style = 0;      // see kStyleNames below
    int character = 0;  // Off / Tube / Diode / Bright
    bool characterPre = false; // saturation before detector and compression
    bool autoThreshold = false;
    bool autoRelease = false;
    bool autoGain = false;
    bool bypass = false;

    static Parameters sanitise (Parameters p) noexcept
    {
        auto bounded = [] (double value, double lo, double hi, double fallback) noexcept
        {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        };
        p.thresholdDb = bounded(p.thresholdDb, -60.0, 0.0, -18.0);
        p.ratio = bounded(p.ratio, 1.0, 40.0, 4.0);
        p.kneeDb = bounded(p.kneeDb, 0.0, 72.0, 12.0);
        p.rangeDb = bounded(p.rangeDb, 0.0, 60.0, 60.0);
        p.attackMs = bounded(p.attackMs, 0.005, 250.0, 10.0);
        p.releaseMs = bounded(p.releaseMs, 5.0, 3000.0, 120.0);
        p.holdMs = bounded(p.holdMs, 0.0, 500.0, 0.0);
        p.lookaheadMs = bounded(p.lookaheadMs, 0.0, 20.0, 0.0);
        p.inputDb = bounded(p.inputDb, -24.0, 24.0, 0.0);
        p.wetDb = bounded(p.wetDb, -24.0, 24.0, 0.0);
        p.dryPercent = bounded(p.dryPercent, 0.0, 100.0, 0.0);
        p.dryGainDb = bounded(p.dryGainDb, -100.0, 18.0, -100.0);
        p.outputDb = bounded(p.outputDb, -24.0, 24.0, 0.0);
        p.sidechainHighpassHz = bounded(p.sidechainHighpassHz, 0.0, 2000.0, 0.0);
        p.sidechainLowpassHz = bounded(p.sidechainLowpassHz, 100.0, 20000.0, 20000.0);
        for (std::size_t i=0;i<p.sidechainBells.size();++i)
        {
            p.sidechainBells[i].hz = bounded(p.sidechainBells[i].hz, 20.0,20000.0,
                130.0 * std::pow(4.5,static_cast<double>(i)));
            p.sidechainBells[i].gainDb = bounded(p.sidechainBells[i].gainDb,-18.0,18.0,0.0);
            p.sidechainBells[i].q = bounded(p.sidechainBells[i].q,0.15,18.0,1.0);
        }
        p.stereoLinkPercent = bounded(p.stereoLinkPercent, 0.0, 100.0, 100.0);
        p.drive = bounded(p.drive, 0.0, 100.0, 0.0);
        p.style = std::clamp(p.style, 0, 13);
        p.character = std::clamp(p.character, 0, 3);
        return p;
    }
};

static constexpr std::array<const char*, 14> kStyleNames {{
    "Clean", "Versatile", "Smooth", "Punch", "Upward", "TTM",
    "Op-El", "Vari-Mu", "Classic", "Opto", "Vocal", "Mastering",
    "Bus", "Pumping"
}};

class Core final
{
public:
    void prepare (double rate) noexcept
    {
        sampleRate_ = std::isfinite(rate) && rate >= 8000.0 ? rate : 48000.0;
        smoothingCoeff_ = coefficient(0.012);
        detectorCoeff_ = coefficient(0.010);
        thresholdTrackingCoeff_ = coefficient(1.5);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : channels_) s = {};
        for (auto& h : holdRemaining_) h = 0;
        for (auto& ring : lookaheadHistory_) ring.fill(0.0f);
        lookaheadWrite_ = 0;
        smoothedInput_ = dbGain(parameters_.inputDb);
        smoothedWet_ = dbGain(parameters_.wetDb);
        smoothedOutput_ = dbGain(parameters_.outputDb);
        smoothedDry_ = parameters_.dryGainDb <= -99.5
            ? parameters_.dryPercent * 0.01 : dbGain(parameters_.dryGainDb);
        smoothedBypass_ = parameters_.bypass ? 0.0 : 1.0;
        smoothedThreshold_ = parameters_.thresholdDb;
        smoothedMakeup_ = 1.0;
        smoothedDrive_ = parameters_.drive;
        smoothedAutoCorrection_ = 0.0;
        trackedLevelDb_ = -18.0;
        gainReductionDb_ = 0.0;
        inputPeakDb_ = outputPeakDb_ = -120.0;
        initialised_ = false;
    }

    void setParameters (const Parameters& value) noexcept
    {
        parameters_ = Parameters::sanitise(value);
        const auto requested = static_cast<int>(
            std::lround(sampleRate_ * parameters_.lookaheadMs * .001));
        lookaheadSamples_ = std::clamp(requested,0,
            static_cast<int>(kLookaheadCapacity)-1);
        const auto nyquist = 0.499 * sampleRate_;
        const auto hpf = std::min(parameters_.sidechainHighpassHz, nyquist);
        const auto lpf = std::min(parameters_.sidechainLowpassHz, nyquist);
        hpCoeff_ = hpf <= 0.0 ? 0.0 : coefficient(1.0 / (6.283185307179586 * hpf));
        lpCoeff_ = lpf >= nyquist ? 0.0 : coefficient(1.0 / (6.283185307179586 * lpf));
        // RBJ peaking filters. Coefficients are calculated ONCE per block,
        // not per audio sample. Caller may automate freq, gain and Q.
        for (std::size_t i=0;i<bellCoefficients_.size();++i)
        {
            const auto& b=parameters_.sidechainBells[i];
            const double f=std::clamp(b.hz,20.0,sampleRate_*0.45);
            const double w0=6.2831853071795864769*f/sampleRate_;
            const double cs=std::cos(w0);
            const double a=std::pow(10.0,b.gainDb/40.0);
            const double alpha=std::sin(w0)/(2.0*b.q);
            const double invA0=1.0/(1.0+alpha/a);
            auto& c=bellCoefficients_[i];
            c.b0=(1.0+alpha*a)*invA0;
            c.b1=(-2.0*cs)*invA0;
            c.b2=(1.0-alpha*a)*invA0;
            c.a1=(-2.0*cs)*invA0;
            c.a2=(1.0-alpha/a)*invA0;
            c.enabled=std::abs(b.gainDb)>=1.0e-6;
        }
        if (!initialised_)
        {
            smoothedInput_ = dbGain(parameters_.inputDb);
            smoothedWet_ = dbGain(parameters_.wetDb);
            smoothedOutput_ = dbGain(parameters_.outputDb);
            smoothedDry_ = parameters_.dryGainDb <= -99.5
            ? parameters_.dryPercent * 0.01 : dbGain(parameters_.dryGainDb);
            smoothedBypass_ = parameters_.bypass ? 0.0 : 1.0;
            smoothedThreshold_ = parameters_.thresholdDb;
            smoothedMakeup_ = 1.0;
            smoothedDrive_ = parameters_.drive;
            smoothedAutoCorrection_ = 0.0;
        }
    }

    Parameters getParameters() const noexcept { return parameters_; }
    double getGainReductionDb() const noexcept { return gainReductionDb_; }
    double getInputPeakDb() const noexcept { return inputPeakDb_; }
    double getOutputPeakDb() const noexcept { return outputPeakDb_; }
    // With lookahead ON, this is the exact sample delay of BOTH wet and dry
    // paths (including bypass). It must be reported to the plugin host.
    int latencySamples() const noexcept { return lookaheadSamples_; }

    // Separate SC pointers are optional and must be valid for the whole block.
    // The native wrapper currently uses the internal path. This port is ready
    // for externally routed APEX sidechain when its bus contract is validated.
    void process (float* left, float* right, int samples,
                  const float* sideLeft = nullptr,
                  const float* sideRight = nullptr) noexcept
    {
        if (left == nullptr || samples <= 0) return;
        initialised_ = true;
        const bool stereo = right != nullptr && right != left;
        const double attackScale = styleAttackScale(parameters_.style);
        const double releaseScale = styleReleaseScale(parameters_.style);
        const double attackCoeff = coefficient(parameters_.attackMs * 0.001 * attackScale);
        const double baseRelease = parameters_.releaseMs * 0.001 * releaseScale;
        const auto inputTarget = dbGain(parameters_.inputDb);
        const auto wetTarget = dbGain(parameters_.wetDb);
        const auto outputTarget = dbGain(parameters_.outputDb);
        const double dryTarget = parameters_.dryGainDb <= -99.5
            ? parameters_.dryPercent * 0.01 : dbGain(parameters_.dryGainDb);
        const auto activeTarget = parameters_.bypass ? 0.0 : 1.0;
        const double link = parameters_.stereoLinkPercent * 0.01;
        double inputPeak = 0.0, outputPeak = 0.0;

        for (int i = 0; i < samples; ++i)
        {
            smoothedInput_ = smooth(smoothedInput_, inputTarget);
            smoothedWet_ = smooth(smoothedWet_, wetTarget);
            smoothedDry_ = smooth(smoothedDry_, dryTarget);
            smoothedOutput_ = smooth(smoothedOutput_, outputTarget);
            smoothedBypass_ = smooth(smoothedBypass_, activeTarget);
            smoothedThreshold_ = smooth(smoothedThreshold_, parameters_.thresholdDb);
            smoothedDrive_ = smooth(smoothedDrive_, parameters_.drive);

            const double inputL = safe(left[i]);
            const double inputR = stereo ? safe(right[i]) : inputL;
            inputPeak = std::max(inputPeak, std::max(std::abs(inputL),std::abs(inputR)));

            // Persist history even at zero lookahead, so switching the mode
            // never allocates on the callback. Write before reading; for N
            // delayed samples, ring[write-N] is the exact input at t-N.
            lookaheadHistory_[0][lookaheadWrite_] = static_cast<float>(inputL);
            lookaheadHistory_[1][lookaheadWrite_] = static_cast<float>(inputR);
            const auto readIndex = (lookaheadWrite_ + kLookaheadCapacity
                 - static_cast<std::size_t>(lookaheadSamples_)) % kLookaheadCapacity;
            const double dryL = lookaheadSamples_ == 0
                ? inputL : lookaheadHistory_[0][readIndex];
            const double dryR = lookaheadSamples_ == 0
                ? inputR : lookaheadHistory_[1][readIndex];
            lookaheadWrite_ = (lookaheadWrite_ + 1) % kLookaheadCapacity;
            const double x[2] = {dryL * smoothedInput_, dryR * smoothedInput_};
            // True PRE topology: saturate the WET audio before the detector
            // and gain computer. POST topology (default) colours the output
            // after compression. Host-routed external key always overrides
            // the internal detector without colour.
            const double colouredIn[2] = {
                parameters_.characterPre
                    ? colour(x[0],parameters_.character,smoothedDrive_) : x[0],
                parameters_.characterPre
                    ? colour(x[1],parameters_.character,smoothedDrive_) : x[1]
            };
            // Detector sees input at t, but the actual output below is the
            // stored input at t-lookaheadSamples_, giving it time to react.
            // At lookahead=0, preserve the prior C3 detector bit for bit.
            const double triggerL = lookaheadSamples_ == 0
                ? colouredIn[0] : (parameters_.characterPre
                    ? colour(inputL*smoothedInput_,parameters_.character,smoothedDrive_)
                    : inputL*smoothedInput_);
            const double triggerR = lookaheadSamples_ == 0
                ? colouredIn[1] : (parameters_.characterPre
                    ? colour(inputR*smoothedInput_,parameters_.character,smoothedDrive_)
                    : inputR*smoothedInput_);
            const double trigger[2] = {
                sideLeft ? safe(sideLeft[i]) : triggerL,
                sideRight ? safe(sideRight[i]) : (sideLeft ? safe(sideLeft[i]) : triggerR)
            };
            double detectorDb[2] {};
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                auto& state = channels_[static_cast<std::size_t>(c)];
                state.hpLow = hpCoeff_ * state.hpLow + (1.0 - hpCoeff_) * trigger[c];
                double filtered = parameters_.sidechainHighpassHz > 0.0
                                    ? trigger[c] - state.hpLow : trigger[c];
                state.lpLow = lpCoeff_ * state.lpLow + (1.0 - lpCoeff_) * filtered;
                if (parameters_.sidechainLowpassHz < 19999.0)
                    filtered = state.lpLow;
                // Four true parametric bells operate ONLY on the detector
                // path, never the vocal/track audio. No block allocations.
                for(std::size_t band=0;band<bellCoefficients_.size();++band)
                {
                    const auto& coeff=bellCoefficients_[band];
                    if(!coeff.enabled) continue;
                    auto& filter=state.bellState[band];
                    const double out=coeff.b0*filtered+filter.z1;
                    filter.z1=coeff.b1*filtered-coeff.a1*out+filter.z2;
                    filter.z2=coeff.b2*filtered-coeff.a2*out;
                    if(std::abs(filter.z1)<1.0e-25) filter.z1=0.0;
                    if(std::abs(filter.z2)<1.0e-25) filter.z2=0.0;
                    filtered=std::isfinite(out) ? std::clamp(out,-100.0,100.0) : 0.0;
                }
                // Smooth / opto-like modes use an RMS-ish detector.
                const bool rms = parameters_.style == 2 || parameters_.style == 6
                              || parameters_.style == 9 || parameters_.style == 11;
                const double energy = filtered * filtered;
                state.rms = detectorCoeff_ * state.rms
                          + (1.0 - detectorCoeff_) * energy;
                const double level = rms ? std::sqrt(std::max(0.0, state.rms))
                                         : std::abs(filtered);
                const double feedback = (parameters_.style == 7 || parameters_.style == 8)
                    ? state.lastGain : 1.0;
                detectorDb[c] = amplitudeDb(level * feedback);
            }
            if (!stereo) detectorDb[1] = detectorDb[0];
            const auto louder = std::max(detectorDb[0], detectorDb[1]);
            if (louder > -70.0)
                trackedLevelDb_ = thresholdTrackingCoeff_ * trackedLevelDb_
                                + (1.0 - thresholdTrackingCoeff_) * louder;
            const double correction = parameters_.autoThreshold
                ? std::clamp(trackedLevelDb_ + 18.0, -18.0, 18.0) : 0.0;
            smoothedAutoCorrection_ = smooth(smoothedAutoCorrection_, correction);
            const double threshold = std::clamp(smoothedThreshold_ + smoothedAutoCorrection_, -72.0, 18.0);
            double gr[2] {};
            for (int c = 0; c < (stereo ? 2 : 1); ++c)
            {
                auto& state = channels_[static_cast<std::size_t>(c)];
                const auto linkedDb = detectorDb[c] + (louder - detectorDb[c]) * link;
                const double targetDb = desiredReduction(linkedDb, threshold);
                if (std::abs(targetDb) > std::abs(state.reductionDb))
                {
                    state.reductionDb = attackCoeff * state.reductionDb
                                      + (1.0 - attackCoeff) * targetDb;
                    holdRemaining_[static_cast<std::size_t>(c)] = static_cast<int>(
                        sampleRate_ * parameters_.holdMs * 0.001);
                }
                else if (holdRemaining_[static_cast<std::size_t>(c)] > 0)
                    --holdRemaining_[static_cast<std::size_t>(c)];
                else
                {
                    // Longer gain reductions recover more slowly; stable at
                    // all host block sizes (the state advances per sample).
                    const double adaptive = parameters_.autoRelease
                        ? (1.0 + std::clamp(state.reductionDb / 12.0, 0.0, 3.0)) : 1.0;
                    const double release = coefficient(baseRelease * adaptive);
                    state.reductionDb = release * state.reductionDb
                                      + (1.0 - release) * targetDb;
                }
                if (std::abs(state.reductionDb) < 1.0e-9) state.reductionDb = 0.0;
                gr[c] = std::clamp(state.reductionDb, -parameters_.rangeDb,
                                   parameters_.rangeDb);
                state.lastGain = dbGain(-gr[c]);
            }
            if (!stereo) gr[1] = gr[0];
            gainReductionDb_ = std::max(0.0, std::max(gr[0], gr[1]));
            const double makeup = parameters_.autoGain
                ? std::clamp((-threshold) * (1.0 - 1.0 / parameters_.ratio) * 0.15,
                             0.0, 12.0) : 0.0;
            smoothedMakeup_ = smooth(smoothedMakeup_, dbGain(makeup));
            const double wetGain = smoothedWet_ * smoothedMakeup_;
            const double compressedL=colouredIn[0]*dbGain(-gr[0]);
            const double compressedR=colouredIn[1]*dbGain(-gr[1]);
            const double saturatedL=parameters_.characterPre ? compressedL
                : colour(compressedL,parameters_.character,smoothedDrive_);
            const double saturatedR=parameters_.characterPre ? compressedR
                : colour(compressedR,parameters_.character,smoothedDrive_);
            double yL = saturatedL*wetGain + x[0]*smoothedDry_;
            double yR = saturatedR*wetGain + x[1]*smoothedDry_;
            yL *= smoothedOutput_;
            yR *= smoothedOutput_;
            // Bypass recovers the exact original input, with no discontinuous
            // replacement of a playing waveform on button press.
            yL = dryL + (yL - dryL) * smoothedBypass_;
            yR = dryR + (yR - dryR) * smoothedBypass_;
            if (!std::isfinite(yL)) yL = 0.0;
            if (!std::isfinite(yR)) yR = 0.0;
            left[i] = static_cast<float>(std::clamp(yL, -32.0, 32.0));
            if (stereo)
                right[i] = static_cast<float>(std::clamp(yR, -32.0, 32.0));
            outputPeak = std::max(outputPeak, std::max(std::abs(yL), std::abs(yR)));
        }
        inputPeakDb_ = amplitudeDb(inputPeak);
        outputPeakDb_ = amplitudeDb(outputPeak);
    }

private:
    struct BellCoefficients
    {
        double b0=1.0,b1=0.0,b2=0.0,a1=0.0,a2=0.0;
        bool enabled=false;
    };
    struct BellFilterState
    {
        double z1=0.0,z2=0.0;
    };
    struct Channel
    {
        double hpLow = 0.0, lpLow = 0.0, rms = 0.0;
        double reductionDb = 0.0, lastGain = 1.0;
        std::array<BellFilterState,4> bellState {};
    };

    static double safe (float value) noexcept
    {
        return std::isfinite(value) ? std::clamp(static_cast<double>(value), -32.0, 32.0) : 0.0;
    }
    static double dbGain (double db) noexcept { return std::exp(db * 0.11512925464970229); }
    static double amplitudeDb (double amplitude) noexcept
    {
        return 20.0 * std::log10(std::max(1.0e-9, amplitude));
    }
    double coefficient (double seconds) const noexcept
    {
        return std::exp(-1.0 / (sampleRate_ * std::max(0.000001, seconds)));
    }
    double smooth (double previous, double target) const noexcept
    {
        return smoothingCoeff_ * previous + (1.0 - smoothingCoeff_) * target;
    }
    static double styleAttackScale (int style) noexcept
    {
        constexpr double factors[] = {1.0,0.9,1.8,0.65,0.75,0.8,2.8,1.1,
                                       1.3,3.5,0.85,0.55,1.4,0.65};
        return factors[style];
    }
    static double styleReleaseScale (int style) noexcept
    {
        constexpr double factors[] = {1.0,0.85,1.7,0.75,0.8,1.2,2.0,1.9,
                                       2.2,3.0,0.9,0.8,1.65,2.7};
        return factors[style];
    }
    double desiredReduction (double levelDb, double threshold) const noexcept
    {
        double ratio = parameters_.ratio;
        if (parameters_.style == 10) ratio = std::max(ratio, 3.0); // vocal assist
        if (parameters_.style == 7) ratio = 1.0 + (ratio - 1.0)
            * (0.5 + 0.5 * std::tanh((levelDb - threshold) * 0.08));
        const double amount = 1.0 - 1.0 / ratio;
        double knee = parameters_.kneeDb;
        if (parameters_.style == 9 || parameters_.style == 6)
            knee = std::max(knee, 24.0);
        const double above = levelDb - threshold;
        auto transfer = [amount, knee](double over) noexcept
        {
            if (knee <= 0.0) return amount * std::max(0.0, over);
            if (over <= -knee * 0.5) return 0.0;
            if (over >= knee * 0.5) return amount * over;
            const double x = over + knee * 0.5;
            return amount * x * x / (2.0 * knee);
        };
        if (parameters_.style == 4) // upward: boost quiet material, don't attenuate loud
            return -std::min(parameters_.rangeDb, transfer(-above));
        if (parameters_.style == 5) // broadband upward/downward hybrid; NOT FabFilter multiband TTM
            return std::clamp(transfer(above) - 0.35 * transfer(-above),
                              -parameters_.rangeDb, parameters_.rangeDb);
        return std::min(parameters_.rangeDb, transfer(above));
    }
    static double colour (double input, int mode, double drive) noexcept
    {
        if (mode == 0 || drive <= 0.0) return input;
        const double amount = drive * 0.01;
        const double x = input * (1.0 + 2.0 * amount);
        if (mode == 1) // symmetric soft saturation
            return (1.0 - amount) * input + amount * std::tanh(x) / (1.0 + amount);
        if (mode == 2) // asymmetric harmonic shape, zero crossing preserved
            return (1.0 - amount) * input + amount * (x / (1.0 + std::abs(x)));
        // 'Bright': restrained soft clip with increasing high-level edge.
        return (1.0 - amount) * input + amount * (x - (x*x*x) / (3.0 + x*x));
    }

    Parameters parameters_ {};
    double sampleRate_ = 48000.0, smoothingCoeff_ = 0.0;
    double detectorCoeff_ = 0.0;
    double thresholdTrackingCoeff_ = 0.0, trackedLevelDb_ = -18.0;
    double hpCoeff_ = 0.0, lpCoeff_ = 0.0;
    std::array<Channel,2> channels_ {};
    std::array<BellCoefficients,4> bellCoefficients_ {};
    // Preallocated per-channel ring: ~64 KiB. No dynamic allocation in
    // prepare(), setParameters() or process() (RT-safe, block invariant).
    static constexpr std::size_t kLookaheadCapacity = 8192;
    std::array<std::array<float,kLookaheadCapacity>,2> lookaheadHistory_ {};
    std::size_t lookaheadWrite_ = 0;
    int lookaheadSamples_ = 0;
    std::array<int,2> holdRemaining_ {};
    double smoothedInput_ = 1.0, smoothedWet_ = 1.0, smoothedOutput_ = 1.0;
    double smoothedDry_ = 0.0, smoothedBypass_ = 1.0, smoothedThreshold_ = -18.0;
    double smoothedMakeup_ = 1.0, smoothedDrive_ = 0.0, smoothedAutoCorrection_ = 0.0;
    double gainReductionDb_ = 0.0, inputPeakDb_ = -120.0, outputPeakDb_ = -120.0;
    bool initialised_ = false;
};
} // namespace APEX::PrecisionC3

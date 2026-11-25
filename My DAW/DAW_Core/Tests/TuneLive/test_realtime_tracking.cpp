#include "../../Source/TuneLiveCore/ApexTuneLiveRealtimeCore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

namespace {
std::atomic<bool> watchAllocations{false};
std::atomic<unsigned> allocations{0};

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void releaseTestAllocation(void* pointer) noexcept {
    std::free(pointer);
}
}

void* operator new(std::size_t size) {
    if (watchAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { releaseTestAllocation(p); }
void operator delete[](void* p) noexcept { releaseTestAllocation(p); }
void operator delete(void* p, std::size_t) noexcept { releaseTestAllocation(p); }
void operator delete[](void* p, std::size_t) noexcept { releaseTestAllocation(p); }

namespace {
using namespace APEX::TuneLive;
constexpr double tau = 6.28318530717958647692;

DspSettings hardTune() {
    DspSettings result;
    result.correction = 1.0f;
    result.retune = result.humanize = result.flexTune = 0.0f;
    return result;
}

std::vector<float> makeVoice(int rate, double hz, int length) {
    std::vector<float> input(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double phase = tau * hz * t;
        input[static_cast<std::size_t>(i)] = static_cast<float>(std::min(1.0, t / 0.002) *
            (0.25 * std::sin(phase) + 0.14 * std::sin(2.0 * phase + 0.33) +
             0.08 * std::sin(3.0 * phase + 0.6) + 0.031 * std::sin(4.0 * phase)));
    }
    return input;
}

void run(RealtimeCore& engine, std::vector<float>& audio, const DspSettings& settings, int maxBlock) {
    const auto length = static_cast<int>(audio.size());
    for (int offset = 0; offset < length; offset += maxBlock) {
        float* channels[]{audio.data() + offset};
        engine.process(channels, 1, std::min(maxBlock, length - offset), settings);
    }
}


// Compare the same input under mobile-sized and irregular host callbacks.
// Changes in key/mix/retune are aligned to the same sample clock.
bool testMobileCallbackPartition()
{
    constexpr std::array<int, 9> blocks{{32,48,64,96,128,160,192,256,512}};
    for (const int rate : {44100,48000,96000})
    {
        const int length = rate / 2;
        auto dry = makeVoice(rate, 228.0, length);
        for (int i = rate / 5; i < rate / 5 + rate / 100; ++i)
            dry[static_cast<std::size_t>(i)] = 0.0f;
        auto baseline = dry;
        auto candidate = dry;
        RealtimeCore fixed, varied;
        fixed.prepare(rate,1);
        varied.prepare(rate,1);
        auto settings = hardTune();
        std::size_t index = 0;
        const int checkpoint = rate / 10;
        for (int start = 0; start < length; start += checkpoint)
        {
            const int end = std::min(length,start+checkpoint);
            settings.key = start < rate/5 ? 0 : 9;
            settings.scale = 1;
            settings.wet = start < rate/4 ? 1.0f : 0.65f;
            settings.retune = start < rate/5 ? 0.0f : 0.2f;
            float* a[]{baseline.data()+start};
            fixed.process(a,1,end-start,settings);
            for (int pos=start; pos<end;)
            {
                const int count=std::min(end-pos,blocks[index++%blocks.size()]);
                float* b[]{candidate.data()+pos};
                varied.process(b,1,count,settings);
                pos+=count;
            }
        }
        double peakDifference=0.0;
        for(int i=0;i<length;++i)
        {
            if(!std::isfinite(baseline[i]) || !std::isfinite(candidate[i]))
                return false;
            peakDifference=std::max(peakDifference,std::abs(double(baseline[i])-candidate[i]));
        }
        std::cout<<"TRACKING_MOBILE_PARTITION rate="<<rate<<" peak_diff="<<peakDifference<<"\n";
        if(peakDifference>1.0e-5 || fixed.latencySamples()!=0 || varied.latencySamples()!=0)
            return false;
    }
    return true;
}


// Reproduce the reported C# major / Hard Tune / 100% Wet case. An
// independent tone projection measures leaked source F0, not just the
// detector's estimate of the loudest output note.
double toneProjection(const std::vector<float>& audio,int rate,double hz)
{
    double re=0.0,im=0.0,weight=0.0;
    const int start=rate/2, count=static_cast<int>(audio.size())-start;
    for(int i=0;i<count;++i)
    {
        const double w=0.5-0.5*std::cos(tau*i/(count-1));
        const double phase=tau*hz*(start+i)/rate;
        re+=w*audio[static_cast<std::size_t>(start+i)]*std::cos(phase);
        im+=w*audio[static_cast<std::size_t>(start+i)]*std::sin(phase);
        weight+=w;
    }
    return 2.0*std::hypot(re,im)/weight;
}
bool testVoicedReplacement()
{
    bool ok=true;
    for(int rate : {44100,48000,96000})
    {
        const double source=252.0, target=261.6255653;
        auto dry=makeVoice(rate,source,rate*2);
        auto legacy=dry, tuned=dry, bypass=dry;
        RealtimeCore reference,candidate,neutral;
        reference.prepare(rate,1);
        candidate.setVoicedReplacement(true);
        candidate.prepare(rate,1);
        neutral.setVoicedReplacement(true);
        neutral.prepare(rate,1);
        auto settings=hardTune();
        settings.key=1; settings.scale=1; settings.wet=1.0f;
        run(reference,legacy,settings,96);
        run(candidate,tuned,settings,96);
        settings.bypass=true;
        run(neutral,bypass,settings,96);
        const double oldLeak=toneProjection(legacy,rate,source);
        const double newLeak=toneProjection(tuned,rate,source);
        const double corrected=toneProjection(tuned,rate,target);
        double peak=0.0;
        for(float sample:tuned)
        {
            if(!std::isfinite(sample))return false;
            peak=std::max(peak,std::abs(double(sample)));
        }
        std::cout<<"VOICED_REPLACEMENT rate="<<rate<<" legacy_source_f0="<<oldLeak
                 <<" replacement_source_f0="<<newLeak<<" corrected_f0="<<corrected
                 <<" peak="<<peak<<"\n";
        // Do not accept reduced dry simply by silencing the voice. Also
        // preserve exact dry bypass and reject unexpected signal peaks.
        ok=ok && newLeak<=oldLeak+0.002 && newLeak<0.25*corrected
              && corrected>0.10 && peak<0.8 && bypass==dry;
    }
    return ok;
}

// Independent measurement on emitted PCM within the expected tone's range.
double measuredCents(const std::vector<float>& audio, int rate, double target) {
    const double expectedLag = static_cast<double>(rate) / target;
    const int firstLag = static_cast<int>(std::floor(expectedLag / 1.07));
    const int lastLag = static_cast<int>(std::ceil(expectedLag * 1.07));
    const auto errorAtLag = [&](int lag) {
        double sum = 0.0;
        for (int i = rate / 2; i < static_cast<int>(audio.size()); ++i) {
            const double diff = audio[static_cast<std::size_t>(i)] - audio[static_cast<std::size_t>(i - lag)];
            sum += diff * diff;
        }
        return sum;
    };
    int bestLag = firstLag;
    double minimum = errorAtLag(firstLag);
    for (int lag = firstLag + 1; lag <= lastLag; ++lag) {
        const double error = errorAtLag(lag);
        if (error < minimum) {
            minimum = error;
            bestLag = lag;
        }
    }
    const double a = errorAtLag(bestLag - 1), c = errorAtLag(bestLag + 1);
    const double curvature = a - 2.0 * minimum + c;
    const double fraction = curvature > 0.0 ? std::clamp(0.5 * (a - c) / curvature, -0.5, 0.5) : 0.0;
    return 1200.0 * std::log2(expectedLag / (bestLag + fraction));
}

bool testPitchAndOnset(bool research=false) {
    const auto settings = hardTune();
    for (const int rate : {44100, 48000, 96000, 192000}) {
        constexpr std::array<double, 5> notes{65.4063913, 110.0, 195.997718, 440.0, 830.609395};
        constexpr std::array<double, 5> offsets{32.0, 32.0, -33.0, 31.0, -34.0};
        for (std::size_t note = 0; note < notes.size(); ++note) {
            const double target = notes[note];
            const auto dry = makeVoice(rate, target * std::exp2(offsets[note] / 1200.0), rate);
            auto wet = dry;
            RealtimeCore engine;
            engine.prepare(rate, 1);
        engine.setResearchPhaseSlew(research);
            run(engine, wet, settings, 127);
            int changed = 0;
            while (changed < rate &&
                   std::abs(wet[static_cast<std::size_t>(changed)] - dry[static_cast<std::size_t>(changed)]) < 0.02f)
                ++changed;
            const double onsetMs = 1000.0 * changed / rate;
            const double cents = measuredCents(wet, rate, target);
            std::cout << "rate=" << rate << " target_hz=" << target
                      << " output_cents=" << cents << " changed_pcm_ms=" << onsetMs << '\n';
            if (std::abs(cents) > 4.0 || onsetMs > 35.0 || engine.latencySamples() != 0)
                return false;
        }
    }
    return true;
}

bool testFastestRetune(bool research=false) {
    const auto original = hardTune();
    const auto processSample = [](RealtimeCore& engine, float sample, const DspSettings& settings) {
        float* channels[]{&sample};
        engine.process(channels, 1, 1, settings);
        return sample;
    };
    double worstShiftMs = 0.0, worstPcmMs = 0.0;
    int cases = 0;
    for (const int rate : {8000, 44100, 48000, 96000, 192000}) {
        RealtimePitchTracker clock;
        clock.prepare(rate);
        for (const double note : {65.4063913, 110.0, 195.997718, 440.0, 830.609395}) {
            const auto dry = makeVoice(rate, note * std::exp2(32.0 / 1200.0), rate / 8);
            auto settings = original;
            RealtimeCore tuning, unchanged;
            tuning.prepare(rate, 1);
        tuning.setResearchPhaseSlew(research);
            unchanged.prepare(rate, 1);
        unchanged.setResearchPhaseSlew(research);
            int acquired = -1, retarget = -1;
            int accepted = -1, applied = -1, changed = -1;
            double expected = 0.0;
            for (int i = 0; i < static_cast<int>(dry.size()); ++i) {
                const bool changeTarget = i == retarget;
                if (changeTarget)
                    settings.referenceHz = 432.0f;
                const float in = dry[static_cast<std::size_t>(i)];
                const float reference = processSample(unchanged, in, original);
                const float wet = processSample(tuning, in, settings);
                const bool firstEstimate = acquired < 0 && tuning.frequencyHz() > 0.0f;
                if (firstEstimate) {
                    acquired = i;
                    retarget = acquired + 48 * clock.hopSamples();
                }
                if (firstEstimate || changeTarget) {
                    const double midi = 69.0 + 12.0 * std::log2(tuning.frequencyHz() /
                                                                 static_cast<double>(settings.referenceHz));
                    expected = std::round(midi) - midi;
                    accepted = i;
                    applied = changed = -1;
                }
                if (accepted < 0)
                    continue;
                if (applied < 0 && std::abs(tuning.appliedShiftSemitones() - expected) < 1.0e-5)
                    applied = i;
                const float comparison = i < retarget ? in : reference;
                if (changed < 0 && std::abs(wet - comparison) >= 1.0e-5f)
                    changed = i;
                if (i - accepted == rate / 1000) {
                    if (applied < 0 || changed < 0) {
                        std::cerr << "rate=" << rate << " note_hz=" << note
                                  << " retargeted=" << (i >= retarget) << " target_shift=" << expected
                                  << " applied_sample=" << applied << " changed_sample=" << changed << '\n';
                        return false;
                    }
                    ++cases;
                    worstShiftMs = std::max(worstShiftMs, (applied - accepted) * 1000.0 / rate);
                    worstPcmMs = std::max(worstPcmMs, (changed - accepted) * 1000.0 / rate);
                }
            }
        }
    }
    std::cout << "fastest_retune_cases=" << cases << " post_detection_target_shift_ms=" << worstShiftMs
              << " post_detection_first_pcm_delta_ms=" << worstPcmMs << '\n';
    return cases == 50;
}

bool testNoteTransitions(bool research=false) {
    constexpr int rate = 48000;
    constexpr int transition = rate * 2 / 5;
    constexpr int length = transition + rate / 8;
    constexpr std::array<std::array<double,2>,8> pairs{{
        {110.0, 220.0}, {220.0, 110.0}, {196.0, 392.0}, {392.0, 196.0},
        {220.0, 440.0}, {440.0, 220.0}, {329.628, 493.883}, {493.883, 329.628}
    }};
    const auto settings = hardTune();
    RealtimeCore engine;
    engine.prepare(rate, 1);
        engine.setResearchPhaseSlew(research);
    std::vector<float> dry(length);
    double worstPeakRatio = 0.0;
    float worstCorrectionStep = 0.0f;
    double worstHandoffStep = 0.0;
    unsigned handoffs = 0;
    for (const auto& notes : pairs) {
        for (const double offset : {-27.0, 30.0}) {
            const double sourceShift = std::exp2(offset / 1200.0);
            for (int seed = 0; seed < 6; ++seed) {
                const double phaseOffset = seed * tau / 6.0;
                double phase = 0.0;
                double inputPeak = 0.0, outputPeak = 0.0;
                for (int i = 0; i < length; ++i) {
                    const auto note = notes[i < transition ? 0 : 1] * sourceShift;
                    phase += tau * note / rate;
                    const double angle = phase + phaseOffset;
                    const auto value = static_cast<float>(0.255 * std::sin(angle) +
                        0.13 * std::sin(2.0 * angle + 0.4) + 0.052 * std::sin(3.0 * angle - 0.55));
                    dry[static_cast<std::size_t>(i)] = value;
                    inputPeak = std::max(inputPeak, std::abs(static_cast<double>(value)));
                }
                auto wet = dry;
                engine.reset();
                // Observe the exact release sample, not a block boundary.
                // The first outgoing sample is anchored at the previous
                // correction, so the incoming bank must have zero weight.
                double previousCorrection = 0.0;
                for (std::size_t sample = 0; sample < wet.size(); ++sample) {
                    const auto releases = engine.waveformReleaseCount();
                    float* audio[]{wet.data() + sample};
                    engine.process(audio, 1, 1, settings);
                    const double correction = static_cast<double>(wet[sample]) - dry[sample];
                    if (engine.waveformReleaseCount() != releases) {
                        ++handoffs;
                        worstHandoffStep = std::max(worstHandoffStep,
                            std::abs(correction - previousCorrection));
                    }
                    previousCorrection = correction;
                }
                for (auto i = static_cast<std::size_t>(transition); i < wet.size(); ++i) {
                    outputPeak = std::max(outputPeak, std::abs(static_cast<double>(wet[i])));
                    const float step = (wet[i] - dry[i]) - (wet[i - 1] - dry[i - 1]);
                    worstCorrectionStep = std::max(worstCorrectionStep, std::abs(step));
                }
                worstPeakRatio = std::max(worstPeakRatio, outputPeak / inputPeak);
            }
        }
    }
    std::cout << "note_transition_cases=96 max_peak_ratio=" << worstPeakRatio
              << " max_correction_sample_step=" << worstCorrectionStep << '\n';
    std::cout << "note_bank_handoffs=" << handoffs
              << " max_first_sample_handoff_step=" << worstHandoffStep << '\n';
    return worstPeakRatio < 1.55 && worstCorrectionStep < 0.17f &&
           handoffs > 0 && worstHandoffStep < 2.0e-6;
}

bool testBypassAtBankRelease() {
    // Bypass may land on the exact sample that releases an old oscillator.
    // That sample must begin the fade at the prior correction, not at zero.
    constexpr int rate=48000, length=rate/2, change=rate/4;
    const auto settings=hardTune();
    unsigned exercised=0;
    double worst=0.0;
    for(int seed=0;seed<6;++seed) {
        std::vector<float> dry(length);
        double phase=seed*tau/6.0;
        for(int i=0;i<length;++i) {
            phase+=tau*(i<change?227.0:453.0)/rate;
            dry[i]=static_cast<float>(0.26*std::sin(phase)+0.09*std::sin(2*phase+0.4));
        }
        RealtimeCore probe, candidate;
        probe.prepare(rate,1);
        candidate.prepare(rate,1);
        int release=-1;
        double last=0.0;
        for(int i=0;i<length;++i) {
            float value=dry[i];float* data[]={&value};
            const auto before=probe.waveformReleaseCount();
            probe.process(data,1,1,settings);
            if(i>=change && probe.waveformReleaseCount()!=before && std::abs(last)>1e-4) {
                release=i;break;
            }
            last=static_cast<double>(value)-dry[i];
        }
        if(release<0)continue;
        auto bypass=settings;
        bypass.bypass=true;
        double previous=0.0;
        for(int i=0;i<length;++i) {
            float value=dry[i];float* data[]={&value};
            candidate.process(data,1,1,i<release?settings:bypass);
            const double correction=static_cast<double>(value)-dry[i];
            if(!std::isfinite(value))return false;
            if(i==release) {
                worst=std::max(worst,std::abs(correction-previous));
                ++exercised;
            }
            if(i>release+rate/10 && value!=dry[i])return false;
            previous=correction;
        }
    }
    std::cout<<"bypass_on_bank_release_cases="<<exercised
             <<" worst_first_sample_step="<<worst<<'\n';
    return exercised>0 && worst<2e-6;
}

bool testTimelineAndReset(bool research=false) {
    constexpr int rate = 48000;
    constexpr int impulsePosition = 14399;
    const auto settings = hardTune();
    const auto dry = makeVoice(rate, 447.7, rate / 2);
    auto a = dry, b = dry, hit = dry;
    hit[impulsePosition] += 0.11f;
    RealtimeCore fixed, alternate, transient;
    fixed.prepare(rate, 1);
        fixed.setResearchPhaseSlew(research);
    alternate.prepare(rate, 1);
        alternate.setResearchPhaseSlew(research);
    transient.prepare(rate, 1);
        transient.setResearchPhaseSlew(research);
    run(fixed, a, settings, 128);
    run(alternate, b, settings, 1);
    run(transient, hit, settings, 127);
    if (a != b || !std::equal(a.begin(), a.begin() + impulsePosition, hit.begin()) ||
        std::abs(hit[impulsePosition] - a[impulsePosition] - 0.11f) > 1.0e-5f)
        return false;

    auto bypassed = settings;
    bypassed.bypass = true;
    a = dry;
    fixed.reset();
    run(fixed, a, bypassed, 133);
    if (a != dry)
        return false;

    fixed.reset();
    a = dry;
    run(fixed, a, settings, 333);
    return a == b;
}

bool testVibrato(bool research=false) {
    constexpr int rate = 48000;
    auto plain = hardTune(), preserve = plain;
    plain.stability = preserve.stability = 0.8f;
    preserve.vibratoPreserve = 1.0f;
    std::vector<float> dry(static_cast<std::size_t>(rate * 3));
    double phase = 0.0;
    for (std::size_t i = 0; i < dry.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double cents = 25.0 + 27.0 * std::sin(tau * 5.0 * t);
        phase += tau * (440.0 * std::exp2(cents / 1200.0)) / rate;
        dry[i] = static_cast<float>(0.35 * std::sin(phase));
    }
    auto hard = dry, expressive = dry;
    RealtimeCore engine;
    engine.prepare(rate, 1);
        engine.setResearchPhaseSlew(research);
    run(engine, hard, plain, 128);
    engine.reset();
    run(engine, expressive, preserve, 128);

    auto measure = [](const std::vector<float>& audio, int rate) {
        PitchTracker analyser;
        analyser.prepare(rate);
        double sum = 0.0, sumSquares = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < audio.size(); ++i) {
            if (analyser.push(audio[i]) && i >= static_cast<std::size_t>(rate) && analyser.latest().voiced) {
                const double cents = 1200.0 * std::log2(analyser.latest().hz / 440.0);
                sum += cents;
                sumSquares += cents * cents;
                ++count;
            }
        }
        const double mean = sum / std::max(1, count);
        return std::array<double,2>{mean, std::sqrt(std::max(0.0, sumSquares / std::max(1, count) - mean * mean))};
    };
    const auto input = measure(dry, rate), fixed = measure(hard, rate), natural = measure(expressive, rate);
    std::cout << "vibrato_dry_cents=" << input[1] << " hard_cents=" << fixed[1]
              << " preserved_cents=" << natural[1] << " preserved_center_cents=" << natural[0] << '\n';
    return input[1] > 10.0 && natural[1] > 0.55 * input[1] && natural[1] < 1.4 * input[1]
           && natural[1] > 1.7 * fixed[1] && std::abs(natural[0]) < 10.0;
}

bool testRealtimeSafety(bool research=false) {
    constexpr int rate = 48000, block = 128, inserts = 8, blocks = 375;
    const auto source = makeVoice(rate, 113.1, block * blocks);
    std::array<RealtimeCore, inserts> engines;
    std::array<double, blocks> times{};
    bool clean = true;
    for (auto& engine : engines) {
        engine.prepare(rate, 2);
        engine.setResearchPhaseSlew(research);
    }
    auto settings = hardTune();
    watchAllocations.store(true, std::memory_order_relaxed);
    for (int i = 0; i < blocks; ++i) {
        const auto start = std::chrono::steady_clock::now();
        settings.preserveFormants = (i / 20) % 2 == 0;
        settings.vocalRange = i < 100 ? VocalRange::Automatic : VocalRange::Low;
        for (auto& engine : engines) {
            std::array<float, block> left{}, right{};
            std::copy_n(source.data() + i * block, block, right.data());
            if (i == 200)
                right[7] = std::numeric_limits<float>::quiet_NaN();
            float* channels[]{left.data(), right.data()};
            engine.process(channels, 2, block, settings);
            for (int n = 0; n < block; ++n)
                clean &= left[static_cast<std::size_t>(n)] == 0.0f &&
                         std::isfinite(right[static_cast<std::size_t>(n)]);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        times[static_cast<std::size_t>(i)] = std::chrono::duration<double, std::milli>(elapsed).count();
    }
    watchAllocations.store(false, std::memory_order_relaxed);
    std::sort(times.begin(), times.end());
    const double p95=times[times.size() * 95 / 100];
    std::cout << "research_phase=" << research << " eight_stereo_inserts_48k128_p95_ms=" << p95
              << " callback_cpp_allocations=" << allocations.load() << '\n';
    // Only the optimized build is eligible for the CPU budget gate.
    // Host-runner p95 is not a guarantee of device xrun-free operation.
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_UNDEFINED__)
    if (p95 >= 1000.0 * block / rate) return false;
#endif
    return clean && allocations.load() == 0 && engines[0].frequencyHz() > 100.0f;
}

bool testStereoDetectorHysteresis() {
    constexpr int rate=48000, length=rate*2;
    const auto dry=makeVoice(rate,227.0,length);
    RealtimeCore engine;
    engine.prepare(rate,2);
    auto settings=hardTune();
    int bad=0, voiced=0;
    for(int i=0;i<length;++i) {
        // Right starts clearly dominant, then hovers around the old
        // one-way 1.44 power-ratio threshold, with inverted polarity.
        const double ratio=i<rate/4?1.6:1.20+0.12*std::sin(tau*17.0*i/rate);
        float l=dry[i],r=static_cast<float>(-ratio*dry[i]);
        float* audio[]={&l,&r};
        engine.process(audio,2,1,settings);
        if(!std::isfinite(l)||!std::isfinite(r))return false;
        if(i>rate/2 && i%48==0) {
            const double hz=engine.frequencyHz();
            if(hz>0.0) {
                ++voiced;
                if(std::abs(1200.0*std::log2(hz/227.0))>30.0)++bad;
            }
        }
    }
    std::cout<<"stereo_detector_voiced_frames="<<voiced<<" incorrect_f0="<<bad<<'\n';
    return voiced>1400 && bad==0;
}

bool testPhaseModeScales() {
    constexpr int rate=48000;
    // Inputs close to notes unique to C major/minor and to excluded
    // chromatic tones. Measure the rendered PCM, not the MIDI policy.
    constexpr std::array<std::array<int,3>,6> cases{{
        {1,64,64},{2,63,63},{1,69,69},{2,68,68},{1,61,62},{2,61,62}
    }};
    for(const auto& item:cases) {
        auto settings=hardTune();
        settings.key=0;settings.scale=item[0];
        const double input=440.0*std::exp2((item[1]-69+0.27)/12.0);
        const double target=440.0*std::exp2((item[2]-69)/12.0);
        auto wet=makeVoice(rate,input,rate);
        auto alt=wet;
        RealtimeCore a,b;
        a.prepare(rate,1);b.prepare(rate,1);
        a.setResearchPhaseSlew(true);b.setResearchPhaseSlew(true);
        run(a,wet,settings,128);run(b,alt,settings,1);
        const double cents=measuredCents(wet,rate,target);
        std::cout<<"phase_scale="<<item[0]<<" input_midi="<<item[1]
                 <<" target_midi="<<item[2]<<" output_cents="<<cents<<'\n';
        if(wet!=alt || !std::isfinite(cents) || std::abs(cents)>8.0)return false;
    }
    return true;
}

bool testExperimentalToggleDefersUntilUnvoiced() {
    constexpr int rate=48000;
    RealtimeCore engine;
    engine.prepare(rate,1);
    if(engine.researchPhaseSlewEnabledForTest())return false;
    engine.setResearchPhaseSlew(true);
    if(!engine.researchPhaseSlewEnabledForTest())return false;
    engine.setResearchPhaseSlew(false);
    if(engine.researchPhaseSlewEnabledForTest())return false;

    auto input=makeVoice(rate,447.691,rate/2);
    const auto settings=hardTune();
    for(std::size_t at=0;at<input.size();) {
        const int count=static_cast<int>(std::min<std::size_t>(127,input.size()-at));
        float* audio[]{input.data()+at};
        engine.process(audio,1,count,settings);
        at+=static_cast<std::size_t>(count);
    }
    if(engine.frequencyHz()<100.0f)return false;
    engine.setResearchPhaseSlew(true);
    if(engine.researchPhaseSlewEnabledForTest())return false;
    std::array<float,127> silence{};
    for(int n=0;n<120;++n) {
        // The insert writes into its buffer. Every callback must receive
        // *fresh zero* input, or the synthesized tail is fed back as a
        // fake microphone and the bank never becomes unvoiced.
        silence.fill(0.0f);
        float* audio[]{silence.data()};
        engine.process(audio,1,static_cast<int>(silence.size()),settings);
        // Do not repeat the request: a pending change must commit on the
        // first idle sample without another host parameter notification.
    }
    if (!engine.researchPhaseSlewEnabledForTest()) return false;
    engine.setResearchPhaseSlew(false);
    return !engine.researchPhaseSlewEnabledForTest();
}

bool testDetectorDropoutStability(bool research=false) {
    constexpr int rate = 48000;
    auto dry = makeVoice(rate, 447.691, rate * 2);
    // Airy singing: a brief 8-ms unvoiced hole followed by a later real
    // 150-ms pause. The first must not repeatedly recreate the oscillator.
    const int briefBegin = rate / 2, briefEnd = briefBegin + rate / 125;
    const int longBegin = rate + rate / 4, longEnd = longBegin + rate * 15 / 100;
    std::fill(dry.begin() + briefBegin, dry.begin() + briefEnd, 0.0f);
    std::fill(dry.begin() + longBegin, dry.begin() + longEnd, 0.0f);
    auto wet = dry;
    RealtimeCore engine;
    engine.prepare(rate, 1);
        engine.setResearchPhaseSlew(research);
    const auto settings = hardTune();
    unsigned shortReleases = 0, longReleases = 0, lastReleases = 0;
    double largestCorrectionStep = 0.0;
    bool finite = true;
    for (int offset = 0; offset < static_cast<int>(wet.size());) {
        const int count = std::min(127, static_cast<int>(wet.size()) - offset);
        float* audio[]{wet.data() + offset};
        engine.process(audio, 1, count, settings);
        if (engine.waveformReleaseCount() != lastReleases) {
            std::cout << "Tracking release sample=" << offset
                      << " active_hz=" << engine.frequencyHz()
                      << " count=" << engine.waveformReleaseCount() << '\n';
            lastReleases = engine.waveformReleaseCount();
        }
        if (offset <= briefEnd + rate / 25 && offset + count > briefEnd + rate / 25)
            shortReleases = engine.waveformReleaseCount();
        if (offset <= longEnd && offset + count > longEnd)
            longReleases = engine.waveformReleaseCount();
        offset += count;
    }
    for (std::size_t i = 1; i < wet.size(); ++i) {
        finite &= std::isfinite(wet[i]);
        const double change = (static_cast<double>(wet[i]) - dry[i])
                             - (static_cast<double>(wet[i - 1]) - dry[i - 1]);
        largestCorrectionStep = std::max(largestCorrectionStep, std::abs(change));
    }
    std::cout << "Tracking airy-voice short_drop_bank_releases=" << shortReleases
              << " sustained_silence_releases=" << longReleases
              << " max_correction_step=" << largestCorrectionStep << '\n';
    return finite && shortReleases == 0 && longReleases >= 1
           && largestCorrectionStep < 0.24;
}
} // namespace

int main() {
    if (!testMobileCallbackPartition()) return 1;
    if (!testVoicedReplacement()) { std::cerr<<"Full wet source-F0 leakage or bypass regression\n"; return 19; }
    if (!testPitchAndOnset()) { std::cerr << "Tracking output pitch or onset\n"; return 1; }
    if (!testFastestRetune()) { std::cerr << "Fastest retune time after a detected target\n"; return 6; }
    if (!testBypassAtBankRelease()) { std::cerr << "Bypass truncated outgoing correction fade\\n"; return 9; }
    if (!testTimelineAndReset()) { std::cerr << "Timeline, bypass or reset\n"; return 2; }
    if (!testVibrato()) { std::cerr << "Vibrato preservation\n"; return 3; }
    if (!testNoteTransitions()) { std::cerr << "Excessive note-transition peak or correction step\n"; return 5; }
    if (!testRealtimeSafety()) { std::cerr << "Stereo isolation or realtime allocations\n"; return 4; }
    if (!testDetectorDropoutStability()) { std::cerr << "Tracking pitch dropout click guard\n"; return 7; }
    if (!testStereoDetectorHysteresis()) { std::cerr << "Stereo detector phase chatter\\n"; return 10; }
    if (!testPhaseModeScales()) { std::cerr << "Experimental phase mode C major/minor accuracy\\n"; return 11; }
    std::cout<<"PHASE_MODE_EXTENDED_REGRESSION_BEGIN\\n";
    if (!testPitchAndOnset(true)) { std::cerr<<"Phase mode pitch/onset\\n"; return 12; }
    if (!testFastestRetune(true)) { std::cerr<<"Phase mode retune timing\\n"; return 13; }
    if (!testTimelineAndReset(true)) { std::cerr<<"Phase mode timeline/reset\\n"; return 14; }
    if (!testRealtimeSafety(true)) { std::cerr<<"Phase mode realtime CPU or allocations\n"; return 18; }
    if (!testVibrato(true)) { std::cerr<<"Phase mode vibrato\\n"; return 15; }
    if (!testNoteTransitions(true)) { std::cerr<<"Phase mode note transitions\\n"; return 16; }
    if (!testDetectorDropoutStability(true)) { std::cerr<<"Phase mode dropout\\n"; return 17; }
    std::cout<<"PHASE_MODE_EXTENDED_REGRESSION_PASS\\n";
    if (!testExperimentalToggleDefersUntilUnvoiced()) { std::cerr << "Experimental A/B mode switch must wait for silence\n"; return 8; }
}

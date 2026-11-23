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
}

void* operator new(std::size_t size) {
    if (watchAllocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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

bool testPitchAndOnset() {
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
            run(engine, wet, settings, 127);
            int changed = 0;
            while (changed < rate &&
                   std::abs(wet[static_cast<std::size_t>(changed)] - dry[static_cast<std::size_t>(changed)]) < 0.02f)
                ++changed;
            const double onsetMs = 1000.0 * changed / rate;
            const double cents = measuredCents(wet, rate, target);
            std::cout << "rate=" << rate << " target_hz=" << target
                      << " output_cents=" << cents << " changed_pcm_ms=" << onsetMs << '\n';
            if (std::abs(cents) > 4.0 || onsetMs > 50.0 || engine.latencySamples() != 0)
                return false;
        }
    }
    return true;
}

bool testTimelineAndReset() {
    constexpr int rate = 48000;
    constexpr int impulsePosition = 14399;
    const auto settings = hardTune();
    const auto dry = makeVoice(rate, 447.7, rate / 2);
    auto a = dry, b = dry, hit = dry;
    hit[impulsePosition] += 0.11f;
    RealtimeCore fixed, alternate, transient;
    fixed.prepare(rate, 1);
    alternate.prepare(rate, 1);
    transient.prepare(rate, 1);
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

bool testVibrato() {
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
    run(engine, hard, plain, 128);
    engine.reset();
    run(engine, expressive, preserve, 128);

    auto measure = [](const std::vector<float>& audio) {
        PitchTracker analyser;
        analyser.prepare(rate);
        double sum = 0.0, sumSquares = 0.0;
        int count = 0;
        for (std::size_t i = 0; i < audio.size(); ++i) {
            if (analyser.push(audio[i]) && i >= rate && analyser.latest().voiced) {
                const double cents = 1200.0 * std::log2(analyser.latest().hz / 440.0);
                sum += cents;
                sumSquares += cents * cents;
                ++count;
            }
        }
        const double mean = sum / std::max(1, count);
        return std::array<double,2>{mean, std::sqrt(std::max(0.0, sumSquares / std::max(1, count) - mean * mean))};
    };
    const auto input = measure(dry), fixed = measure(hard), natural = measure(expressive);
    std::cout << "vibrato_dry_cents=" << input[1] << " hard_cents=" << fixed[1]
              << " preserved_cents=" << natural[1] << " preserved_center_cents=" << natural[0] << '\n';
    return input[1] > 10.0 && natural[1] > 0.55 * input[1] && natural[1] < 1.4 * input[1]
           && natural[1] > 1.7 * fixed[1] && std::abs(natural[0]) < 10.0;
}

bool testRealtimeSafety() {
    constexpr int rate = 48000, block = 128, inserts = 8, blocks = 375;
    const auto source = makeVoice(rate, 113.1, block * blocks);
    std::array<RealtimeCore, inserts> engines;
    std::array<double, blocks> times{};
    bool clean = true;
    for (auto& engine : engines)
        engine.prepare(rate, 2);
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
    std::cout << "eight_stereo_inserts_48k128_p95_ms=" << times[times.size() * 95 / 100]
              << " callback_cpp_allocations=" << allocations.load() << '\n';
    return clean && allocations.load() == 0 && engines[0].frequencyHz() > 100.0f;
}
} // namespace

int main() {
    if (!testPitchAndOnset()) { std::cerr << "Tracking output pitch or onset\n"; return 1; }
    if (!testTimelineAndReset()) { std::cerr << "Timeline, bypass or reset\n"; return 2; }
    if (!testVibrato()) { std::cerr << "Vibrato preservation\n"; return 3; }
    if (!testRealtimeSafety()) { std::cerr << "Stereo isolation or realtime allocations\n"; return 4; }
}

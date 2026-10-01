// ===========================================================================
// UnifiedPitchStateCore.h
// Single source of truth for one-knob pitch macro state.
// ===========================================================================
#pragma once
#include "PitchScaleMathCore.h"
#include "PitchZoneClassifierCore.h"
#include "TimePitchTypesCore.h"
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace ArrangementEditor
{

struct UnifiedPitchSnapshot
{
    double pitchSemitones = 0.0;
    double pitchScale = 1.0;
    double formantScale = 1.0;
    double darkIntensity = 0.0;
    double chipIntensity = 0.0;
    double extremeIntensity = 0.0;
    double musicalIntensity = 1.0;
    double tapeBlend = 0.0;
    double independentBlend = 1.0;
    double wet = 0.0;
    double bodyAmount = 0.0;
    double highDamp = 0.0;
    double brightness = 0.0;
    double cryptAmount = 0.0;
    double echoAmount = 0.0;
    double howlAmount = 0.0;
    double wailAmount = 0.0;
    double stretchRatio = 1.0;
    bool preserveFormants = true;
    bool timelineCompensation = true;
    int pitchEngineVersion = 1;
    UnifiedPitchZone zone = UnifiedPitchZone::Clean;
};

class UnifiedPitchStateCore
{
public:
    static inline const juce::Identifier kPropPitchEngineVersion { "pitchEngineVersion" };

    UnifiedPitchStateCore() = default;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = std::max(1.0, PitchScaleMathCore::sanitizeNaN(sampleRate, 44100.0));
        smoothingCoefficient_ = PitchScaleMathCore::onePoleCoefficient(sampleRate_);
        resetSmoothing(targetPitchSemitones_.load(std::memory_order_relaxed));
    }

    void resetSmoothing(double pitchSemitones) noexcept
    {
        pitchSemitones = PitchScaleMathCore::clampPitch(pitchSemitones, true);
        smoothedPitchSemitones_.store(pitchSemitones, std::memory_order_relaxed);
        publish(computeSnapshot(pitchSemitones));
    }

    void setPitchSemitones(double pitchSemitones) noexcept
    {
        targetPitchSemitones_.store(PitchScaleMathCore::clampPitch(pitchSemitones, true), std::memory_order_release);
    }

    double getTargetPitchSemitones() const noexcept
    {
        return targetPitchSemitones_.load(std::memory_order_acquire);
    }

    void setStretchRatio(double stretchRatio) noexcept
    {
        stretchRatio_.store(std::max(0.0001, PitchScaleMathCore::sanitizeNaN(stretchRatio, 1.0)), std::memory_order_release);
    }

    void setPitchEngineVersion(int version) noexcept
    {
        pitchEngineVersion_.store(version <= 0 ? 1 : version, std::memory_order_release);
    }

    UnifiedPitchSnapshot advanceAndGetSnapshot(int numSamples = 1) noexcept
    {
        const double current = smoothedPitchSemitones_.load(std::memory_order_relaxed);
        const double target = targetPitchSemitones_.load(std::memory_order_acquire);
        const double coefficient = PitchScaleMathCore::onePoleCoefficientForSamples(sampleRate_, numSamples);
        const double filtered = PitchScaleMathCore::onePoleNext(current, target, coefficient);
        const double maxDelta = PitchScaleMathCore::kMaxPitchSlewSemitonesPerSecond
                              * (double)std::max(1, numSamples)
                              / std::max(1.0, sampleRate_);
        const double next = PitchScaleMathCore::limitDelta(current, filtered, maxDelta);
        smoothedPitchSemitones_.store(next, std::memory_order_relaxed);

        auto s = computeSnapshot(next);
        publish(s);
        return s;
    }

    UnifiedPitchSnapshot getSnapshot() const noexcept
    {
        UnifiedPitchSnapshot s;
        s.pitchSemitones = pitchSemitones_.load(std::memory_order_acquire);
        s.pitchScale = pitchScale_.load(std::memory_order_acquire);
        s.formantScale = formantScale_.load(std::memory_order_acquire);
        s.darkIntensity = darkIntensity_.load(std::memory_order_acquire);
        s.chipIntensity = chipIntensity_.load(std::memory_order_acquire);
        s.extremeIntensity = extremeIntensity_.load(std::memory_order_acquire);
        s.musicalIntensity = musicalIntensity_.load(std::memory_order_acquire);
        s.tapeBlend = tapeBlend_.load(std::memory_order_acquire);
        s.independentBlend = independentBlend_.load(std::memory_order_acquire);
        s.wet = wet_.load(std::memory_order_acquire);
        s.bodyAmount = bodyAmount_.load(std::memory_order_acquire);
        s.highDamp = highDamp_.load(std::memory_order_acquire);
        s.brightness = brightness_.load(std::memory_order_acquire);
        s.cryptAmount = cryptAmount_.load(std::memory_order_acquire);
        s.echoAmount = echoAmount_.load(std::memory_order_acquire);
        s.howlAmount = howlAmount_.load(std::memory_order_acquire);
        s.wailAmount = wailAmount_.load(std::memory_order_acquire);
        s.stretchRatio = stretchRatio_.load(std::memory_order_acquire);
        s.preserveFormants = preserveFormants_.load(std::memory_order_acquire) != 0;
        s.timelineCompensation = timelineCompensation_.load(std::memory_order_acquire) != 0;
        s.pitchEngineVersion = pitchEngineVersion_.load(std::memory_order_acquire);
        s.zone = static_cast<UnifiedPitchZone>(zone_.load(std::memory_order_acquire));
        return s;
    }

    static UnifiedPitchSnapshot snapshotFromState(const TimePitchState& state) noexcept
    {
        return computeSnapshotForPitch(PitchScaleMathCore::clampPitch(state.totalPitchSemitones(), true),
                                       std::max(0.0001, state.stretchRatio),
                                       1);
    }

    static double migrateLegacyPitchValue(double storedPitch, int version) noexcept
    {
        storedPitch = PitchScaleMathCore::sanitizeNaN(storedPitch);
        if (version <= 0 && std::abs(storedPitch) > 100.0)
            storedPitch = PitchScaleMathCore::centsToSemitones(storedPitch);
        return PitchScaleMathCore::clampPitch(storedPitch, true);
    }

private:
    UnifiedPitchSnapshot computeSnapshot(double smoothedPitch) const noexcept
    {
        return computeSnapshotForPitch(smoothedPitch,
                                       stretchRatio_.load(std::memory_order_acquire),
                                       pitchEngineVersion_.load(std::memory_order_acquire));
    }

    static UnifiedPitchSnapshot computeSnapshotForPitch(double pitchSt, double stretchRatio, int version) noexcept
    {
        UnifiedPitchSnapshot s;
        pitchSt = PitchScaleMathCore::clampPitch(pitchSt, true);
        const auto z = PitchZoneClassifierCore::classify(pitchSt);
        const double absSt = std::abs(pitchSt);

        s.pitchSemitones = pitchSt;
        s.pitchScale = PitchScaleMathCore::semitonesToRatio(pitchSt);
        s.darkIntensity = pitchSt < 0.0 ? std::pow(z.darkIntensity, 0.55) : 0.0;
        s.chipIntensity = z.chipIntensity;
        s.extremeIntensity = std::max(s.darkIntensity, s.chipIntensity);
        s.musicalIntensity = 1.0 - s.extremeIntensity;
        // Tape blends in proportionally with extreme-zone intensity so the
        // physical-resample character layers on top of grain pitch at -36/+36 st.
        s.tapeBlend = s.extremeIntensity;
        s.independentBlend = 1.0;
        s.wet = PitchScaleMathCore::smoothstep(0.3, 0.7, absSt);
        s.zone = z.zone;
        s.stretchRatio = std::max(0.0001, PitchScaleMathCore::sanitizeNaN(stretchRatio, 1.0));
        s.pitchEngineVersion = version <= 0 ? 1 : version;

        if (pitchSt < 0.0)
        {
            s.formantScale = PitchScaleMathCore::lerp(1.0, 0.32, s.darkIntensity);
            s.bodyAmount = std::min(1.0, s.darkIntensity * 1.15);
            s.highDamp = std::min(1.0, s.darkIntensity * 1.25);
            s.cryptAmount = s.darkIntensity * 0.55;
            s.echoAmount = s.darkIntensity * 0.40;
            s.howlAmount = 0.0;
            s.wailAmount = 0.0;
        }
        else
        {
            s.formantScale = PitchScaleMathCore::lerp(1.0, 1.65, s.chipIntensity);
            s.brightness = s.chipIntensity;
        }

        s.preserveFormants = s.extremeIntensity < 0.05;
        s.timelineCompensation = true;
        return s;
    }

    void publish(const UnifiedPitchSnapshot& s) noexcept
    {
        pitchSemitones_.store(s.pitchSemitones, std::memory_order_release);
        pitchScale_.store(s.pitchScale, std::memory_order_release);
        formantScale_.store(s.formantScale, std::memory_order_release);
        darkIntensity_.store(s.darkIntensity, std::memory_order_release);
        chipIntensity_.store(s.chipIntensity, std::memory_order_release);
        extremeIntensity_.store(s.extremeIntensity, std::memory_order_release);
        musicalIntensity_.store(s.musicalIntensity, std::memory_order_release);
        tapeBlend_.store(s.tapeBlend, std::memory_order_release);
        independentBlend_.store(s.independentBlend, std::memory_order_release);
        wet_.store(s.wet, std::memory_order_release);
        bodyAmount_.store(s.bodyAmount, std::memory_order_release);
        highDamp_.store(s.highDamp, std::memory_order_release);
        brightness_.store(s.brightness, std::memory_order_release);
        cryptAmount_.store(s.cryptAmount, std::memory_order_release);
        echoAmount_.store(s.echoAmount, std::memory_order_release);
        howlAmount_.store(s.howlAmount, std::memory_order_release);
        wailAmount_.store(s.wailAmount, std::memory_order_release);
        preserveFormants_.store(s.preserveFormants ? 1 : 0, std::memory_order_release);
        timelineCompensation_.store(s.timelineCompensation ? 1 : 0, std::memory_order_release);
        zone_.store(static_cast<int>(s.zone), std::memory_order_release);
    }

    double sampleRate_ = 44100.0;
    double smoothingCoefficient_ = 1.0;

    std::atomic<double> targetPitchSemitones_ { 0.0 };
    std::atomic<double> smoothedPitchSemitones_ { 0.0 };
    std::atomic<double> stretchRatio_ { 1.0 };
    std::atomic<int> pitchEngineVersion_ { 1 };

    std::atomic<double> pitchSemitones_ { 0.0 };
    std::atomic<double> pitchScale_ { 1.0 };
    std::atomic<double> formantScale_ { 1.0 };
    std::atomic<double> darkIntensity_ { 0.0 };
    std::atomic<double> chipIntensity_ { 0.0 };
    std::atomic<double> extremeIntensity_ { 0.0 };
    std::atomic<double> musicalIntensity_ { 1.0 };
    std::atomic<double> tapeBlend_ { 0.0 };
    std::atomic<double> independentBlend_ { 1.0 };
    std::atomic<double> wet_ { 0.0 };
    std::atomic<double> bodyAmount_ { 0.0 };
    std::atomic<double> highDamp_ { 0.0 };
    std::atomic<double> brightness_ { 0.0 };
    std::atomic<double> cryptAmount_ { 0.0 };
    std::atomic<double> echoAmount_ { 0.0 };
    std::atomic<double> howlAmount_ { 0.0 };
    std::atomic<double> wailAmount_ { 0.0 };
    std::atomic<int> preserveFormants_ { 1 };
    std::atomic<int> timelineCompensation_ { 1 };
    std::atomic<int> zone_ { static_cast<int>(UnifiedPitchZone::Clean) };
};

} // namespace ArrangementEditor

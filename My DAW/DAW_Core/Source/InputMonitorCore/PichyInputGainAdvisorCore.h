#pragma once

#include <algorithm>
#include <cmath>
#include "PichyInputGuardianCore.h"

namespace DAW {

/**
 * Pure suggestion logic; NO direct gain changes.
 * All user-facing decisions run on the message thread. Only a separate,
 * authorized command should call Track::setInputTrimDb().
 */
struct PichyInputGainAdvice
{
    enum class Status
    {
        NoSignal,
        InvalidInput,
        HardwareAttention,
        AlreadySafe,
        ReduceDigitalTrim
    };

    Status status = Status::NoSignal;
    float proposedTrimDb = 0.0f;
    float rawPeakDbfs = -120.0f;
    float expectedPostTrimPeakDbfs = -120.0f;
};

class PichyInputGainAdvisorCore
{
public:
    static PichyInputGainAdvice evaluate(
        PichyInputGuardianCore::Snapshot raw, float currentTrimDb,
        float targetPeakDbfs = -12.0f) noexcept
    {
        PichyInputGainAdvice result;
        result.proposedTrimDb = std::isfinite(currentTrimDb)
            ? clamp(currentTrimDb, -120.0f, 24.0f) : 0.0f;

        if (!std::isfinite(currentTrimDb) || !std::isfinite(targetPeakDbfs)
            || targetPeakDbfs > -3.0f || targetPeakDbfs < -30.0f)
        {
            result.status = PichyInputGainAdvice::Status::InvalidInput;
            return result;
        }
        if (raw.nonFiniteSamples != 0 || !std::isfinite(raw.rawPeakHold))
        {
            result.status = PichyInputGainAdvice::Status::InvalidInput;
            return result;
        }
        if (raw.framesObserved == 0 || raw.rawPeakHold <= 1.0e-6f)
        {
            result.status = PichyInputGainAdvice::Status::NoSignal;
            return result;
        }

        result.rawPeakDbfs = 20.0f * std::log10(raw.rawPeakHold);
        result.expectedPostTrimPeakDbfs = result.rawPeakDbfs + result.proposedTrimDb;

        // Never propose that digital trim can REPAIR probable interface/ADC
        // saturation. Pichy must advise inspecting the physical gain first.
        if (raw.nearFullScaleSamples > 0 || raw.aboveFullScaleSamples > 0
            || raw.rawPeakHold >= PichyInputGuardianCore::nearFullScaleThreshold)
        {
            result.status = PichyInputGainAdvice::Status::HardwareAttention;
            return result;
        }

        if (result.expectedPostTrimPeakDbfs <= targetPeakDbfs)
        {
            result.status = PichyInputGainAdvice::Status::AlreadySafe;
            return result;
        }

        const float neededReduction = result.expectedPostTrimPeakDbfs - targetPeakDbfs;
        result.proposedTrimDb = clamp(result.proposedTrimDb - neededReduction,
                                      -24.0f, 24.0f);
        result.expectedPostTrimPeakDbfs = result.rawPeakDbfs + result.proposedTrimDb;
        result.status = PichyInputGainAdvice::Status::ReduceDigitalTrim;
        return result;
    }

private:
    static float clamp(float value, float minValue, float maxValue) noexcept
    {
        return std::max(minValue, std::min(value, maxValue));
    }
};

} // namespace DAW

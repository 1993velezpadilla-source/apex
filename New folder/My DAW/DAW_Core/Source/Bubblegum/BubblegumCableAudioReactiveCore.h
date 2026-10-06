#pragma once
#include <unordered_map>
#include <cmath>
#include "BubblegumCableTypes.h"

namespace bubblegum
{
    class BubblegumCableAudioReactiveCore
    {
    public:
        struct ReactiveState
        {
            float smoothedEnergy01 = 0.0f;
        };

        struct ReactiveOutput
        {
            float energy01 = 0.0f;
            float thicknessScale = 1.0f;
            float flowSpeedScale = 1.0f;
            float internalGlowAlpha = 0.20f;
            float highlightBoost = 0.10f;
            float dropletChanceScale = 1.0f;
        };

        void reset();

        ReactiveOutput updateForSend(
            SendId sendId,
            float targetEnergy01,
            float dt,
            float thicknessScaleMax,
            float flowSpeedScaleMax,
            float internalGlowBase,
            float internalGlowMaxAdd,
            float highlightBase,
            float highlightMaxAdd,
            float dropletChanceMaxAdd) noexcept;

    private:
        std::unordered_map<SendId, ReactiveState> states;
    };
}

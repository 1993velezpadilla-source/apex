// Standalone portable gauntlet: g++ -std=c++17 -Wall -Wextra -pedantic
// -I. PichyInputGuardianStandalone.cpp -o guardian_test && ./guardian_test
#include "../../Source/InputMonitorCore/PichyInputGainAdvisorCore.h"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using DAW::PichyInputGuardianCore;
    using DAW::PichyInputGainAdvisorCore;
    using DAW::PichyInputGainAdvice;
    using Status = PichyInputGainAdvice::Status;

    PichyInputGuardianCore guardian;
    const float mono[] { 0.5f, -0.6f, 0.25f, 0.0f };
    guardian.processBlock(mono, mono, 4, true);
    auto s = guardian.snapshot();
    assert(s.framesObserved == 4);
    assert(std::abs(s.rawPeakHold - 0.6f) < 0.0001f);
    assert(s.nearFullScaleSamples == 0);
    assert(s.aboveFullScaleSamples == 0);
    auto advice = PichyInputGainAdvisorCore::evaluate(s, 0.0f);
    assert(advice.status == Status::ReduceDigitalTrim);
    assert(advice.proposedTrimDb < -7.0f && advice.proposedTrimDb > -8.0f);
    assert(std::abs(advice.expectedPostTrimPeakDbfs + 12.0f) < 0.01f);
    auto already = PichyInputGainAdvisorCore::evaluate(s, -15.0f);
    assert(already.status == Status::AlreadySafe);

    const float stereoLeft[] { 0.2f, 0.0f };
    const float stereoRight[] { 0.1f, -1.0f };
    guardian.processBlock(stereoLeft, stereoRight, 2, false);
    s = guardian.snapshot();
    assert(s.framesObserved == 6);
    assert(s.nearFullScaleSamples == 1);
    assert(PichyInputGainAdvisorCore::evaluate(s, -20.0f).status
           == Status::HardwareAttention);

    guardian.resetHold();
    s = guardian.snapshot();
    assert(s.framesObserved == 0 && s.nearFullScaleSamples == 0);
    assert(PichyInputGainAdvisorCore::evaluate(s, 0.0f).status == Status::NoSignal);
    guardian.processBlock(nullptr, nullptr, 100, false);
    assert(guardian.snapshot().framesObserved == 0);
    const float nearFs[] { 0.9989f, 0.999f, 1.2f };
    guardian.processBlock(nearFs, nullptr, 3, true);
    s = guardian.snapshot();
    assert(s.nearFullScaleSamples == 2);
    assert(s.aboveFullScaleSamples == 1);
    assert(s.framesObserved == 3);
    assert(PichyInputGainAdvisorCore::evaluate(s, 0.0f).status
           == Status::HardwareAttention);

    guardian.resetHold();
    const float corrupted[] { std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(), 0.4f };
    guardian.processBlock(corrupted, nullptr, 3, true);
    s = guardian.snapshot();
    assert(s.nonFiniteSamples == 2);
    assert(PichyInputGainAdvisorCore::evaluate(s, 0.0f).status == Status::InvalidInput);
    assert(PichyInputGainAdvisorCore::evaluate(s, std::numeric_limits<float>::quiet_NaN()).status
           == Status::InvalidInput);

    return 0;
}

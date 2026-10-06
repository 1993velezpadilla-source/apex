#include <JuceHeader.h>

#include "../../../Source/ParametricEQCore/ParametricEQCharacterCore.h"

class ParametricEQCharacterTests final : public juce::UnitTest
{
public:
    ParametricEQCharacterTests()
        : UnitTest ("ParametricEQ.Character", "APEX.ParametricEQ") {}

    void runTest() override
    {
        beginTest ("Pure mode is exactly transparent after prepare");
        APEX::ParametricEQ::CharacterCore core;
        core.prepare (48000.0, 2);
        core.setMode (APEX::ParametricEQ::CharacterMode::Pure);
        juce::AudioBuffer<float> buffer (2, 2048);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, std::sin (0.013f * static_cast<float> (i + ch * 7)));
        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);
        core.process (buffer.getArrayOfWritePointers(), 2, buffer.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectEquals (buffer.getSample (ch, i), reference.getSample (ch, i));

        beginTest ("Velvet and Heat remain finite and bounded");
        for (auto mode : { APEX::ParametricEQ::CharacterMode::Velvet,
                           APEX::ParametricEQ::CharacterMode::Heat })
        {
            core.prepare (96000.0, 2);
            core.setMode (mode);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, (i & 1) ? 3.0f : -3.0f);
            for (int pass = 0; pass < 4; ++pass)
                core.process (buffer.getArrayOfWritePointers(), 2, buffer.getNumSamples());
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto value = buffer.getSample (ch, i);
                    expect (std::isfinite (value));
                    expect (std::abs (value) < 3.1f);
                }
        }

        beginTest ("Mode changes are smoothed, not one-sample discontinuities");
        core.prepare (48000.0, 1);
        juce::AudioBuffer<float> mono (1, 256);
        mono.clear();
        for (int i = 0; i < mono.getNumSamples(); ++i)
            mono.setSample (0, i, 0.8f);
        core.setMode (APEX::ParametricEQ::CharacterMode::Heat);
        core.process (mono.getArrayOfWritePointers(), 1, mono.getNumSamples());
        expect (std::abs (mono.getSample (0, 1) - mono.getSample (0, 0)) < 0.02f);
    }
};

static ParametricEQCharacterTests parametricEQCharacterTests;

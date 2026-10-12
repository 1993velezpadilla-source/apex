#include "../../Source/TrackCore/TrackPolarityCore.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace
{
struct DeterministicAudioBuffer
{
    int channels = 2;
    int samples = 8;
    std::array<std::array<float, 8>, 2> data {};

    int getNumChannels() const noexcept { return channels; }
    int getNumSamples() const noexcept { return samples; }
    float* getWritePointer(int channel) noexcept
    {
        assert(channel >= 0 && channel < channels);
        return data[static_cast<std::size_t>(channel)].data();
    }

    void applyGain(int channel, int startSample, int numSamples, float gain) noexcept
    {
        assert(channel >= 0 && channel < channels);
        assert(startSample >= 0 && numSamples >= 0);
        assert(startSample + numSamples <= samples);
        for (int i = startSample; i < startSample + numSamples; ++i)
            data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)] *= gain;
    }
};

bool close(float a, float b) noexcept { return std::fabs(a - b) < 0.000001f; }
}

int main()
{
    DeterministicAudioBuffer stereo;
    for (int sample = 0; sample < 8; ++sample)
    {
        stereo.data[0][static_cast<std::size_t>(sample)] = 0.1f * (sample + 1);
        stereo.data[1][static_cast<std::size_t>(sample)] = -0.1f * (sample + 1);
    }
    const auto reference = stereo;

    DAW::TrackPolarityCore::apply(stereo, 8, false);
    assert(stereo.data == reference.data);

    DAW::TrackPolarityCore::apply(stereo, 5, true);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 8; ++sample)
            assert(close(stereo.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)],
                         (sample < 5 ? -1.0f : 1.0f)
                         * reference.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)]));

    DAW::TrackPolarityCore::apply(stereo, 5, true);
    assert(stereo.data == reference.data);

    DAW::TrackPolarityCore::apply(stereo, 4096, true);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 8; ++sample)
            assert(close(stereo.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)],
                         -reference.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)]));

    stereo.channels = 1;
    DAW::TrackPolarityCore::apply(stereo, 8, true);
    for (int sample = 0; sample < 8; ++sample)
        assert(close(stereo.data[0][static_cast<std::size_t>(sample)],
                     reference.data[0][static_cast<std::size_t>(sample)]));

    DAW::TrackPolarityCore::apply(stereo, -5, true);
    DAW::TrackPolarityCore::apply(stereo, 0, true);
    for (int sample = 0; sample < 8; ++sample)
        assert(close(stereo.data[0][static_cast<std::size_t>(sample)],
                     reference.data[0][static_cast<std::size_t>(sample)]));

    // An identical parallel signal cancels after exactly one sign inversion.
    DeterministicAudioBuffer twinA, twinB;
    twinA.data = reference.data;
    twinB.data = reference.data;
    DAW::TrackPolarityCore::apply(twinB, 8, true);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 8; ++sample)
            assert(close(twinA.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)]
                         + twinB.data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)], 0.0f));


    // Production transition: only user state changes trigger the ramp.
    // Constant 0.5 audio lets us bound the adjacent-sample discontinuity.
    DeterministicAudioBuffer switching;
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::TransitionState smooth;
    DAW::TrackPolarityCore::applyWithTransition(switching, 8, false, smooth, 4);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            assert(close(switching.data[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)], 0.5f));

    // A 4-sample +1 to -1 fade has gains +.5, 0, -.5, -1.
    switching.data = reference.data; // overwrite below to avoid source artifacts
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 8, true, smooth, 4);
    const std::array<float, 8> expectedOn = { 0.25f, 0.0f, -0.25f, -0.5f,
                                               -0.5f, -0.5f, -0.5f, -0.5f };
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            assert(close(switching.data[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)],
                         expectedOn[static_cast<std::size_t>(i)]));
    assert(smooth.remainingSamples == 0 && close(smooth.currentGain, -1.0f));

    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 8, false, smooth, 4);
    const std::array<float, 8> expectedOff = { -0.25f, 0.0f, 0.25f, 0.5f,
                                                0.5f, 0.5f, 0.5f, 0.5f };
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 8; ++i)
            assert(close(switching.data[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)],
                         expectedOff[static_cast<std::size_t>(i)]));

    // Carry a 6-sample toggle across buffers of 2 frames.
    DAW::TrackPolarityCore::TransitionState across;
    switching.samples = 2;
    for (int block = 0; block < 3; ++block)
    {
        for (auto& channel : switching.data)
            channel.fill(0.5f);
        DAW::TrackPolarityCore::applyWithTransition(switching, 2, true, across, 6);
        assert(close(switching.data[0][0], switching.data[1][0]));
    }
    assert(close(across.currentGain, -1.0f) && across.remainingSamples == 0);
    switching.samples = 8;

    // Reversing direction mid-toggle starts a new ramp from the current gain.
    DAW::TrackPolarityCore::TransitionState turnaround;
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 2, true, turnaround, 4);
    assert(close(turnaround.currentGain, 0.0f));
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 8, false, turnaround, 4);
    assert(close(switching.data[0][0], 0.125f));
    assert(close(switching.data[0][3], 0.5f));
    assert(close(turnaround.currentGain, 1.0f));

    // 64-frame default fade avoids the +/-1 discontinuity while preserving
    // exact polarity after completion. Applied gain is identical L/R.
    DAW::TrackPolarityCore::TransitionState defaultFade;
    for (int block = 0; block < 8; ++block)
    {
        for (auto& channel : switching.data)
            channel.fill(0.5f);
        DAW::TrackPolarityCore::applyWithTransition(switching, 8, true, defaultFade);
        for (int i = 0; i < 8; ++i)
            assert(close(switching.data[0][static_cast<std::size_t>(i)],
                         switching.data[1][static_cast<std::size_t>(i)]));
        if (block == 0)
            assert(std::fabs(switching.data[0][0] - 0.5f) < 0.02f);
    }
    assert(close(defaultFade.currentGain, -1.0f));
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 8, true, defaultFade);
    assert(close(switching.data[0][0], -0.5f));
    assert(close(switching.data[1][7], -0.5f));

    // Mono also uses the same per-track transition, and unused samples are safe.
    DAW::TrackPolarityCore::TransitionState monoFade;
    switching.channels = 1;
    switching.samples = 2;
    for (auto& channel : switching.data)
        channel.fill(0.5f);
    DAW::TrackPolarityCore::applyWithTransition(switching, 128, true, monoFade, 2);
    assert(close(switching.data[0][1], -0.5f));
    assert(close(switching.data[0][2], 0.5f));
    assert(close(monoFade.currentGain, -1.0f));

    std::puts("PASS: APEX polarity core portable DSP regressions");
    return 0;
}

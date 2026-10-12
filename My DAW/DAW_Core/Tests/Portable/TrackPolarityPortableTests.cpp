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

    std::puts("PASS: APEX polarity core portable DSP regressions");
    return 0;
}

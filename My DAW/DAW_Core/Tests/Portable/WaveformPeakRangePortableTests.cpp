// Portable adversarial test for exact waveform min/max peak index.
// g++ -std=c++17 -O2 -Wall -Wextra -Werror Tests/Portable/WaveformPeakRangePortableTests.cpp
#include "../../Builds/VisualStudio2026/ArrangementEditor/WaveformPeakRangeIndexCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using ArrangementEditor::WaveformPeakRangeIndexCore;

static void checkRange(const WaveformPeakRangeIndexCore& index,
                       const std::vector<float>& mins, const std::vector<float>& maxs,
                       int begin, int end)
{
    float low = 0.0f, high = 0.0f;
    index.query(mins, maxs, begin, end, low, high);
    float referenceLow = mins[static_cast<std::size_t>(begin)];
    float referenceHigh = maxs[static_cast<std::size_t>(begin)];
    for (int i = begin + 1; i <= end; ++i)
    {
        referenceLow = std::min(referenceLow, mins[static_cast<std::size_t>(i)]);
        referenceHigh = std::max(referenceHigh, maxs[static_cast<std::size_t>(i)]);
    }
    assert(referenceLow == low);
    assert(referenceHigh == high);
}

int main()
{
    std::mt19937 random(20261103);
    const std::vector<int> sizes = {1, 2, 31, 32, 33, 63, 64, 65,
                                     1023, 1024, 1025, 32768, 131072};
    for (const auto length : sizes)
    {
        std::vector<float> mins, maxs;
        mins.reserve(static_cast<std::size_t>(length));
        maxs.reserve(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i)
        {
            const float v = static_cast<float>(random() % 10001) / 10000.f;
            mins.push_back(-v);
            maxs.push_back(v);
        }
        mins[static_cast<std::size_t>(length / 2)] = -1.f; // rare transient
        maxs[static_cast<std::size_t>(length / 2)] = 1.f;
        WaveformPeakRangeIndexCore index;
        index.rebuild(mins, maxs);
        assert(index.isReadyFor(mins.size()));
        assert(index.extraBytes() <= 2 * (mins.size() + 2048) * sizeof(float));
        checkRange(index, mins, maxs, 0, length - 1);
        checkRange(index, mins, maxs, length / 2, length / 2);
        for (int i = 0; i < 2000; ++i)
        {
            int a = static_cast<int>(random() % static_cast<unsigned>(length));
            int b = static_cast<int>(random() % static_cast<unsigned>(length));
            checkRange(index, mins, maxs, std::min(a, b), std::max(a, b));
        }
        // Check every alignment boundary and 32/1024 seam.
        for (int i = 0; i < length; i += 31)
            checkRange(index, mins, maxs, i, std::min(length - 1, i + 1047));
    }
    // A long zoomed-out pixel should read << N source buckets while
    // retaining its exact extreme transient.
    {
        const std::size_t n = 131072;
        std::vector<float> low(n, -0.1f), high(n, 0.2f);
        low[55001] = -1.0f;
        high[90000] = 1.0f;
        WaveformPeakRangeIndexCore index;
        index.rebuild(low, high);
        float a = 0.0f, b = 0.0f;
        std::size_t inspected = 0;
        index.query(low, high, 1, static_cast<int>(n - 2), a, b, &inspected);
        assert(a == -1.0f && b == 1.0f);
        assert(inspected < 300); // instead of >131k peak accesses
    }
    // Unbuilt index must fall back to exact source scan.
    {
        const std::vector<float> lo{-0.1f, -1.f, -0.4f};
        const std::vector<float> hi{0.3f, 0.2f, 0.6f};
        WaveformPeakRangeIndexCore index;
        float a = 0.0f, b = 0.0f;
        index.query(lo, hi, 0, 2, a, b);
        assert(a == -1.f && b == 0.6f);
    }
    std::cout << "WaveformPeakRange: 13 sizes + randomized, seams and transients GREEN\\n";
}

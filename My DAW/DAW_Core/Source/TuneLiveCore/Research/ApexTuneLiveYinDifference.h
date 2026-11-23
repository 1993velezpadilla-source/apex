#pragma once
#include <array>
#include <cstddef>

namespace APEX { namespace TuneLive { namespace Research {

inline double yinSquaredDifference(const float* first, const float* delayed,
                                   int length) noexcept {
    // Independent accumulators let the compiler vectorize this reduction
    // without fast-math or narrowing the detector's double-precision sums.
    std::array<double, 4> sum{};
    int i = 0;
    for (; i + 4 <= length; i += 4) {
        for (std::size_t lane = 0; lane < sum.size(); ++lane) {
            const auto index = static_cast<std::size_t>(i) + lane;
            const double delta = static_cast<double>(first[index]) - delayed[index];
            sum[lane] += delta * delta;
        }
    }
    double total = (sum[0] + sum[1]) + (sum[2] + sum[3]);
    for (; i < length; ++i) {
        const double delta = static_cast<double>(first[i]) - delayed[i];
        total += delta * delta;
    }
    return total;
}

}}} // namespace APEX::TuneLive::Research

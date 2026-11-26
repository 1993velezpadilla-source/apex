#pragma once
#include <algorithm>

namespace apex_oboe {
// Bounded transition at valid/missing boundaries only. No heap, no delay on
// contiguous valid audio, and no replay of stale buffers. State persists across
// arbitrary callback boundaries, including repeated completely empty reads.
class InputContinuity
{
public:
    explicit InputContinuity(int transitionFrames = 24) noexcept
        : span_(std::max(1, transitionFrames)) {}
    void reset() noexcept
    {
        remaining_ = 0;
        last_ = anchor_ = 0.0f;
        valid_ = false;
    }
    void process(float* data, int frames, int received) noexcept
    {
        if (data == nullptr || frames <= 0) return;
        received = std::clamp(received, 0, frames);
        segment(data, received, true);
        segment(data + received, frames - received, false);
    }
private:
    void segment(float* p, int count, bool valid) noexcept
    {
        if (count <= 0) return;
        if (valid != valid_)
        {
            valid_ = valid;
            anchor_ = last_;
            remaining_ = span_;
        }
        int i = 0;
        while (i < count && remaining_ > 0)
        {
            const float t = static_cast<float>(span_ - remaining_ + 1) / static_cast<float>(span_);
            const float target = valid ? p[i] : 0.0f;
            p[i] = anchor_ * (1.0f - t) + target * t;
            --remaining_;
            ++i;
        }
        if (!valid)
            std::fill(p + i, p + count, 0.0f);
        last_ = p[count - 1];
    }
    int span_, remaining_ = 0;
    float last_ = 0.0f, anchor_ = 0.0f;
    bool valid_ = false;
};
}

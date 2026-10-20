// Multi-clip edge/fade edits. Pure message-thread edit math; no engine calls.
// Each drag tick starts from the mouse-down snapshot (not the previous tick),
// so repeated motion is deterministic and never accumulates rounding drift.
#pragma once
#include "ArrangementClipModel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ArrangementEditor
{
struct ClipGroupEditCore
{
    enum class Kind { None = 0, FadeIn = 1, FadeOut = 2, TrimLeft = 3, TrimRight = 4 };

    static ArrangementClipModel apply(const ArrangementClipModel& origin, Kind kind,
                                      double dxSeconds, bool stretch)
    {
        ArrangementClipModel result = origin;
        const double sr = origin.sourceSampleRate > 0.0 ? origin.sourceSampleRate : 44100.0;
        const double minLen = 0.01;
        switch (kind)
        {
            case Kind::FadeIn:
                result.fadeInLength = (float)juce::jlimit(0.0, origin.length,
                                                  (double)origin.fadeInLength + dxSeconds);
                break;
            case Kind::FadeOut:
                result.fadeOutLength = (float)juce::jlimit(0.0, origin.length,
                                                  (double)origin.fadeOutLength - dxSeconds);
                break;
            case Kind::TrimRight:
                if (stretch)
                {
                    const double startVisual = origin.length *
                                               juce::jmax(0.01, (double)origin.timePitch.stretchRatio);
                    result.timePitch.stretchRatio = (float)juce::jlimit(0.25, 4.0,
                                   juce::jmax(0.05, startVisual + dxSeconds) /
                                   juce::jmax(0.001, origin.length));
                }
                else
                {
                    double maxLen = std::numeric_limits<double>::infinity();
                    if (origin.sourceTotalSamples > 0)
                        maxLen = juce::jmax(minLen,
                            (double)juce::jmax(int64_t(0),
                                origin.sourceTotalSamples - origin.sourceStartSample) / sr);
                    result.length = juce::jlimit(minLen, maxLen, origin.length + dxSeconds);
                    result.sourceEndSample = juce::jmax(origin.sourceStartSample + int64_t(1),
                        origin.sourceStartSample + (int64_t)std::llround(result.length * sr));
                }
                break;
            case Kind::TrimLeft:
                if (stretch)
                {
                    const double startVisual = origin.length *
                                               juce::jmax(0.01, (double)origin.timePitch.stretchRatio);
                    const double rightEdge = origin.startTime + startVisual;
                    result.timePitch.stretchRatio = (float)juce::jlimit(0.25, 4.0,
                                    juce::jmax(0.05, startVisual - dxSeconds) /
                                    juce::jmax(0.001, origin.length));
                    result.startTime = rightEdge -
                                      origin.length * result.timePitch.stretchRatio;
                }
                else
                {
                    // Positive mouse delta trims the head, negative reveals headroom.
                    const double maxReveal = (double)juce::jmax(int64_t(0), origin.sourceStartSample) / sr;
                    const double delta = juce::jlimit(-maxReveal,
                                           juce::jmax(0.0, origin.length - minLen), dxSeconds);
                    result.startTime = origin.startTime + delta;
                    result.length = origin.length - delta;
                    result.sourceOffset = juce::jmax(0.0, origin.sourceOffset + delta);
                    result.sourceStartSample = juce::jmax(int64_t(0),
                                  origin.sourceStartSample + (int64_t)std::llround(delta * sr));
                    result.sourceEndSample = juce::jmax(result.sourceStartSample + int64_t(1),
                           origin.sourceEndSample > 0 ? origin.sourceEndSample :
                           result.sourceStartSample + (int64_t)std::llround(result.length * sr));
                }
                break;
            case Kind::None: break;
        }
        result.fadeInLength = juce::jmin(result.fadeInLength, (float)result.length);
        result.fadeOutLength = juce::jmin(result.fadeOutLength, (float)result.length);
        return result;
    }
};
} // namespace ArrangementEditor

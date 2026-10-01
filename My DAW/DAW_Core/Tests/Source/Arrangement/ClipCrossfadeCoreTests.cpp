#include <JuceHeader.h>

#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/SoundEngineCore/ApexClipCrossfadeCore.h"

namespace
{
    using DAW::AudioClip;
    using DAW::SoundEngine::ApexClipCrossfadeCore;
    using DAW::SoundEngine::ClipCrossfadeGeometry;
    using DAW::SoundEngine::ClipCrossfadeRange;
    using DAW::SoundEngine::ClipCrossfadeState;

    bool approximately(float actual, float expected, float tolerance = 1.0e-5f) noexcept
    {
        return std::abs(actual - expected) <= tolerance;
    }

    bool sameRange(const ClipCrossfadeRange& actual,
                   const ClipCrossfadeRange& expected) noexcept
    {
        return actual.startSample == expected.startSample
            && actual.endSample == expected.endSample;
    }

    void preparePair(const ClipCrossfadeGeometry& left,
                     const ClipCrossfadeGeometry& right,
                     ClipCrossfadeState& leftState,
                     ClipCrossfadeState& rightState)
    {
        leftState = {};
        rightState = {};
        ApexClipCrossfadeCore::prepareAdjacentRelationship(
            left, right, leftState, rightState);
    }
}

class ClipCrossfadeCoreTests final : public juce::UnitTest
{
public:
    ClipCrossfadeCoreTests()
        : juce::UnitTest("crossfade.focused.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest("A.basic-equal-power");
        {
            const ClipCrossfadeRange overlap { 100, 200 };
            const auto start = ApexClipCrossfadeCore::equalPowerPairAt(100, overlap);
            const auto end   = ApexClipCrossfadeCore::equalPowerPairAt(200, overlap);

            expect(approximately(start.first, 1.0f));
            expect(approximately(start.second, 0.0f));
            expect(approximately(end.first, 0.0f));
            expect(approximately(end.second, 1.0f));
            expect(approximately(ApexClipCrossfadeCore::fadeOutGainAt(100, overlap), 1.0f));
            expect(approximately(ApexClipCrossfadeCore::fadeOutGainAt(200, overlap), 0.0f));
            expect(approximately(ApexClipCrossfadeCore::fadeOutGainAt(201, overlap), 0.0f));

            float previousOutgoing = start.first;
            float previousIncoming = start.second;
            for (int64_t sample = 101; sample < 200; ++sample)
            {
                const auto pair = ApexClipCrossfadeCore::equalPowerPairAt(sample, overlap);
                expect(pair.first < previousOutgoing, "outgoing gain decreases");
                expect(pair.second > previousIncoming, "incoming gain increases");
                expect(pair.first > 0.0f && pair.second > 0.0f,
                       "both isolated clips contribute inside overlap");
                expect(approximately(pair.first * pair.first + pair.second * pair.second,
                                     1.0f, 1.0e-4f),
                       "equal-power sum of squares");
                previousOutgoing = pair.first;
                previousIncoming = pair.second;
            }
        }

        beginTest("B.no-overlap-is-unity");
        {
            const auto overlap = ApexClipCrossfadeCore::makeOverlap(0, 100, 100, 200);
            expect(!overlap.isValid(), "adjacent clips have no overlap range");

            const ClipCrossfadeState empty;
            expect(!empty.hasAnyRange());
            expect(approximately(ApexClipCrossfadeCore::gainAt(1000, empty), 1.0f));
        }

        beginTest("C.short-overlap-is-finite-and-monotonic");
        {
            const ClipCrossfadeRange overlap { 50, 52 };
            float previousOutgoing = 1.0f;
            float previousIncoming = 0.0f;
            for (int64_t sample = 49; sample <= 53; ++sample)
            {
                const auto pair = ApexClipCrossfadeCore::equalPowerPairAt(sample, overlap);
                expect(std::isfinite(pair.first) && std::isfinite(pair.second));
                if (sample >= 50 && sample <= 52)
                {
                    expect(pair.first <= previousOutgoing + 1.0e-6f,
                           "short outgoing transition is monotonic");
                    expect(pair.second + 1.0e-6f >= previousIncoming,
                           "short incoming transition is monotonic");
                    previousOutgoing = pair.first;
                    previousIncoming = pair.second;
                }
            }
        }

        beginTest("D.geometry-refresh-clears-and-rebuilds-prepared-state");
        {
            AudioClip leftClip("crossfade.left", "Left");
            AudioClip rightClip("crossfade.right", "Right");
            ClipCrossfadeState leftState, rightState;

            preparePair({ 0, 100 }, { 80, 180 }, leftState, rightState);
            leftClip.setAutoCrossfadeRanges(leftState);
            rightClip.setAutoCrossfadeRanges(rightState);
            expect(sameRange(leftClip.getAutoCrossfadeState().fadeOut, { 80, 100 }));
            expect(sameRange(rightClip.getAutoCrossfadeState().fadeIn, { 80, 100 }));

            preparePair({ 0, 100 }, { 120, 220 }, leftState, rightState);
            leftClip.setAutoCrossfadeRanges(leftState);
            rightClip.setAutoCrossfadeRanges(rightState);
            expect(!leftClip.getAutoCrossfadeState().hasAnyRange(), "move clears overlap");
            expect(!rightClip.getAutoCrossfadeState().hasAnyRange(), "move clears incoming state");

            preparePair({ 0, 100 }, { 90, 160 }, leftState, rightState);
            leftClip.setAutoCrossfadeRanges(leftState);
            rightClip.setAutoCrossfadeRanges(rightState);
            expect(sameRange(leftClip.getAutoCrossfadeState().fadeOut, { 90, 100 }));

            preparePair({ 0, 100 }, { 100, 200 }, leftState, rightState);
            leftClip.setAutoCrossfadeRanges(leftState);
            rightClip.setAutoCrossfadeRanges(rightState);
            expect(!leftClip.getAutoCrossfadeState().hasAnyRange(), "trim removal clears state");
            expect(!rightClip.getAutoCrossfadeState().hasAnyRange(), "trim removal clears incoming state");
        }

        beginTest("E.undo-redo-restores-prepared-state");
        {
            AudioClip clip("crossfade.history", "History");
            ClipCrossfadeState before;
            ClipCrossfadeState after;
            preparePair({ 0, 100 }, { 80, 180 }, before, after);
            const ClipCrossfadeState autoState = after;

            clip.setAutoCrossfadeRanges(autoState);
            expect(sameRange(clip.getAutoCrossfadeState().fadeIn, { 80, 100 }));

            clip.setAutoCrossfadeRanges({});
            expect(!clip.getAutoCrossfadeState().hasAnyRange(), "undo geometry clears state");

            clip.setAutoCrossfadeRanges(autoState);
            const ClipCrossfadeState restored = clip.getAutoCrossfadeState();
            expect(sameRange(restored.fadeIn, { 80, 100 }), "redo restores exact range");
            expect(approximately(ApexClipCrossfadeCore::gainAt(90, restored),
                                 ApexClipCrossfadeCore::fadeInGainAt(90, { 80, 100 })));
        }

        beginTest("F.manual-fades-remain-independent");
        {
            AudioClip outgoing("crossfade.manual.out", "Manual Out");
            AudioClip incoming("crossfade.manual.in", "Manual In");
            outgoing.setFadeOutLength(37);
            outgoing.setFadeOutCurve(2);
            incoming.setFadeInLength(29);
            incoming.setFadeInCurve(1);

            ClipCrossfadeState leftState, rightState;
            preparePair({ 0, 100 }, { 80, 180 }, leftState, rightState);
            outgoing.setAutoCrossfadeRanges(leftState);
            incoming.setAutoCrossfadeRanges(rightState);

            expect(outgoing.getFadeOutLength() == 37, "manual outgoing length preserved");
            expect(outgoing.getFadeOutCurve() == 2, "manual outgoing curve preserved");
            expect(incoming.getFadeInLength() == 29, "manual incoming length preserved");
            expect(incoming.getFadeInCurve() == 1, "manual incoming curve preserved");

            const float automaticGain = ApexClipCrossfadeCore::gainAt(
                90, outgoing.getAutoCrossfadeState());
            expect(approximately(automaticGain, 0.7071067f, 1.0e-4f),
                   "automatic relationship is applied once");
            expect(!approximately(automaticGain, 0.5f, 1.0e-3f),
                   "automatic relationship is not squared");
        }

        beginTest("G.only-overlapping-adjacent-pair-gets-state");
        {
            ClipCrossfadeState a, b, c;
            preparePair({ 0, 100 }, { 80, 180 }, a, b);
            const auto bFromA = b;
            ClipCrossfadeState bFromC, cFromB;
            preparePair({ 80, 180 }, { 200, 300 }, bFromC, cFromB);

            expect(a.fadeOut.isValid(), "first overlapping pair gets outgoing state");
            expect(bFromA.fadeIn.isValid(), "first overlapping pair gets incoming state");
            expect(!bFromC.hasAnyRange(), "non-overlapping pair leaves middle clip clear");
            expect(!cFromB.hasAnyRange(), "non-overlapping pair leaves third clip clear");
        }
    }
};

static ClipCrossfadeCoreTests clipCrossfadeCoreTests;

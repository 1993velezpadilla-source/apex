#include <JuceHeader.h>

#include "../../../Source/ParametricEQCore/ParametricEQSketchCore.h"

class ParametricEQSketchTests final : public juce::UnitTest
{
public:
    ParametricEQSketchTests()
        : UnitTest ("ParametricEQ.Sketch", "APEX.ParametricEQ") {}

    void runTest() override
    {
        using namespace APEX::ParametricEQ;

        beginTest ("flat gesture creates no unnecessary bands");
        std::array<SketchPoint, 16> flat {};
        for (int i = 0; i < static_cast<int> (flat.size()); ++i)
            flat[static_cast<std::size_t> (i)] = {
                static_cast<double> (i) / (flat.size() - 1), 0.0
            };
        expectEquals (SketchPlanner::plan (flat.data(),
                                           static_cast<int> (flat.size())).count, 0);

        beginTest ("bell gesture creates a bounded interior bell");
        std::array<SketchPoint, 33> bell {};
        for (int i = 0; i < static_cast<int> (bell.size()); ++i)
        {
            const double x = static_cast<double> (i) / (bell.size() - 1);
            const double dx = (x - 0.55) / 0.11;
            bell[static_cast<std::size_t> (i)] = { x, 8.0 * std::exp (-0.5 * dx * dx) };
        }
        const auto bellPlan = SketchPlanner::plan (
            bell.data(), static_cast<int> (bell.size()));
        expect (bellPlan.count >= 1);
        bool foundBell = false;
        for (int i = 0; i < bellPlan.count; ++i)
        {
            const auto& band = bellPlan.bands[static_cast<std::size_t> (i)];
            expect (std::isfinite (band.frequencyHz));
            expect (std::isfinite (band.gainDb));
            expect (std::isfinite (band.q));
            expect (band.frequencyHz >= 20.0 && band.frequencyHz <= 24000.0);
            expect (band.q >= 0.25 && band.q <= 12.0);
            foundBell |= band.shape == FilterShape::Bell && band.gainDb > 3.0;
        }
        expect (foundBell);

        beginTest ("broad tails become APEX shelves");
        std::array<SketchPoint, 20> shelves {};
        for (int i = 0; i < static_cast<int> (shelves.size()); ++i)
        {
            const double x = static_cast<double> (i) / (shelves.size() - 1);
            const double low = 5.0 * (1.0 - std::clamp (x / 0.25, 0.0, 1.0));
            const double high = -4.0 * std::clamp ((x - 0.75) / 0.25, 0.0, 1.0);
            shelves[static_cast<std::size_t> (i)] = { x, low + high };
        }
        const auto shelfPlan = SketchPlanner::plan (
            shelves.data(), static_cast<int> (shelves.size()));
        bool lowShelf = false, highShelf = false;
        for (int i = 0; i < shelfPlan.count; ++i)
        {
            lowShelf |= shelfPlan.bands[static_cast<std::size_t> (i)].shape
                        == FilterShape::LowShelf;
            highShelf |= shelfPlan.bands[static_cast<std::size_t> (i)].shape
                         == FilterShape::HighShelf;
        }
        expect (lowShelf);
        expect (highShelf);

        beginTest ("unordered noisy input remains finite and bounded");
        std::array<SketchPoint, 128> noisy {};
        for (int i = 0; i < static_cast<int> (noisy.size()); ++i)
        {
            const int reversed = static_cast<int> (noisy.size()) - 1 - i;
            const double x = static_cast<double> (reversed) / (noisy.size() - 1);
            noisy[static_cast<std::size_t> (i)] = {
                x, 12.0 * std::sin (x * 31.0)
            };
        }
        const auto noisyPlan = SketchPlanner::plan (
            noisy.data(), static_cast<int> (noisy.size()));
        expect (noisyPlan.count <= SketchPlan::kMaximumBands);
        for (int i = 0; i < noisyPlan.count; ++i)
        {
            const auto& band = noisyPlan.bands[static_cast<std::size_t> (i)];
            expect (std::isfinite (band.frequencyHz)
                    && std::isfinite (band.gainDb)
                    && std::isfinite (band.q));
        }
    }
};

static ParametricEQSketchTests parametricEQSketchTests;

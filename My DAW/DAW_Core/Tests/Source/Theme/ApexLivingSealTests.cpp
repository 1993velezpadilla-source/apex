#include <JuceHeader.h>
#include <type_traits>
#include "../../../Source/ThemeCore/ApexLivingSeal.h"

class ApexLivingSealTests final : public juce::UnitTest
{
public:
    ApexLivingSealTests() : juce::UnitTest ("theme.apex-living-seal.v1", "APEX.Theme") {}

    void runTest() override
    {
        using Seal = DAW::ApexLivingSeal;
        const juce::Rectangle<float> area (40.0f, 60.0f, 240.0f, 300.0f);

        beginTest ("seal is paint-only decoration, never a Component");
        {
            expect (! std::is_base_of<juce::Component, Seal>::value,
                    "Living Seal must not be a Component: it cannot intercept input");
        }

        beginTest ("normalized geometry stays inside the target bounds at any aspect");
        {
            const juce::Rectangle<float> aspects[] = {
                { 0, 0, 240, 300 }, { 10, 10, 150, 500 }, { 0, 0, 400, 200 },
                { 5, 5, 96, 140 },  { 0, 0, 360, 620 }
            };
            for (const auto& a : aspects)
            {
                Seal s;
                s.setBounds (a);
                const auto content = s.getContentBounds();
                expect (content.getX()      >= a.getX()      - 0.5f, "content left inside");
                expect (content.getY()      >= a.getY()      - 0.5f, "content top inside");
                expect (content.getRight()  <= a.getRight()  + 0.5f, "content right inside");
                expect (content.getBottom() <= a.getBottom() + 0.5f, "content bottom inside");

                const auto eye = s.getPartBounds (Seal::Part::Eye);
                expect (eye.getX()      >= a.getX()      - 0.5f);
                expect (eye.getY()      >= a.getY()      - 0.5f);
                expect (eye.getRight()  <= a.getRight()  + 0.5f);
                expect (eye.getBottom() <= a.getBottom() + 0.5f);
            }
        }

        beginTest ("opening sequence is deterministic bottom-to-top");
        {
            Seal s;
            s.setBounds (area);
            s.startOpening();

            s.update (0.01f); // t = 0.01: only the ambient field is forming
            expect (s.getPhase() == Seal::Phase::Opening);
            expect (s.getPartProgress (Seal::Part::Aura)  > 0.0f);
            expect (s.getPartProgress (Seal::Part::Spine) == 0.0f);
            expect (s.getPartProgress (Seal::Part::Eye)   == 0.0f);

            s.update (0.64f); // t = 0.65: seed fully popped, spine rising
            expect (s.getPartProgress (Seal::Part::Seed)  == 1.0f);
            expect (s.getPartProgress (Seal::Part::Spine) > 0.0f);
            expect (s.getPartProgress (Seal::Part::Spine) < 1.0f);
            expect (s.getPartProgress (Seal::Part::Fehu)  == 0.0f);

            s.update (0.85f); // t = 1.30: fehu + algiz forming
            expect (s.getPartProgress (Seal::Part::Spine) == 1.0f);
            expect (s.getPartProgress (Seal::Part::Fehu)  > 0.0f);
            expect (s.getPartProgress (Seal::Part::Algiz) > 0.0f);
            expect (s.getPartProgress (Seal::Part::Tiwaz) == 0.0f);

            s.update (0.85f); // t = 2.15: crown + nexus forming
            expect (s.getPartProgress (Seal::Part::Tiwaz) > 0.0f);
            expect (s.getPartProgress (Seal::Part::Nexus) > 0.0f);
            expect (s.getPartProgress (Seal::Part::Apex)  == 0.0f);

            s.update (0.85f); // t = 3.00: APEX ignited, lids opening
            expect (s.getPartProgress (Seal::Part::Apex)  == 1.0f);
            expect (s.getPartProgress (Seal::Part::Eye)   > 0.0f);
            expect (s.getPhase() == Seal::Phase::Opening);

            s.update (1.00f); // t = 4.00: idle living state
            expect (s.getPhase() == Seal::Phase::Idle);
            expect (s.getPartProgress (Seal::Part::Eye)   == 1.0f);
        }

        beginTest ("reopening resets the sequence to zero");
        {
            Seal s;
            s.setBounds (area);
            s.startOpening();
            s.update (4.5f);
            expect (s.getPhase() == Seal::Phase::Idle);

            s.startOpening(); // panel reopened
            expect (s.getPhase() == Seal::Phase::Opening);
            expect (s.getTimeSinceStart() < 0.001f);
            expect (s.getPartProgress (Seal::Part::Spine) == 0.0f);
            expect (s.getPartProgress (Seal::Part::Eye)   == 0.0f);
        }

        beginTest ("reduced motion renders final state immediately with no animation");
        {
            Seal s;
            s.setBounds (area);
            s.setReducedMotion (true);
            s.startOpening();
            expect (s.getPhase() == Seal::Phase::Idle);
            expect (s.getPartProgress (Seal::Part::Spine) == 1.0f);
            expect (s.getPartProgress (Seal::Part::Eye)   == 1.0f);
            expect (! s.needsRepaint(), "reduced motion must not schedule animation repaints");
        }

        beginTest ("gaze target is clamped and approaches smoothly, never jumps");
        {
            Seal s;
            s.setBounds (area);
            s.startOpening();
            s.update (4.0f); // idle

            // Far-away cursor must clamp to the small premium radius.
            s.setGazeTarget ({ 10000.0f, -10000.0f }, true);
            s.update (1.0f / 60.0f);
            const auto g0 = s.getGazeOffset();
            expect (g0.getDistanceFromOrigin() <= s.getMaxGazeRadius() + 0.001f,
                    "gaze must be clamped to the premium radius");

            // Smooth: first tick moves only a fraction of the way.
            const float d0 = g0.getDistanceFromOrigin();
            expect (d0 < s.getMaxGazeRadius() * 0.75f,
                    "first tick must not teleport the pupil");

            // Converges toward the clamped target over time.
            for (int i = 0; i < 120; ++i)
                s.update (1.0f / 60.0f);
            const auto g1 = s.getGazeOffset();
            expect (g1.getDistanceFromOrigin() > d0,
                    "gaze converges toward the cursor over time");
            expect (g1.getDistanceFromOrigin() <= s.getMaxGazeRadius() + 0.001f);

            // Clearing the target recenters smoothly back toward zero.
            s.setGazeTarget ({}, false);
            for (int i = 0; i < 240; ++i)
                s.update (1.0f / 60.0f);
            expect (s.getGazeOffset().getDistanceFromOrigin() < 0.15f,
                    "gaze recenters when the cursor leaves");
        }

        beginTest ("standalone living eye geometry stays inside its bounds");
        {
            DAW::ApexLivingEye eye;
            const juce::Rectangle<float> eb (20.0f, 30.0f, 76.0f, 46.0f);
            eye.setBounds (eb);
            const auto content = eye.getContentBounds();
            expect (content.getX()      >= eb.getX()      - 0.5f);
            expect (content.getY()      >= eb.getY()      - 0.5f);
            expect (content.getRight()  <= eb.getRight()  + 0.5f);
            expect (content.getBottom() <= eb.getBottom() + 0.5f);

            eye.setGazeTarget ({ 5000.0f, 5000.0f }, true);
            eye.update (1.0f / 60.0f);
            expect (eye.getGazeOffset().getDistanceFromOrigin()
                        <= eye.getMaxGazeRadius() + 0.001f);
        }

        beginTest ("animation requests repaints while opening and idle, not when closed");
        {
            Seal s;
            s.setBounds (area);
            expect (! s.needsRepaint(), "never opened = nothing to animate");
            s.startOpening();
            expect (s.needsRepaint());
            s.update (4.5f);
            expect (s.needsRepaint(), "idle living state keeps subtle motion");
        }
    }
};

static ApexLivingSealTests apexLivingSealTests;

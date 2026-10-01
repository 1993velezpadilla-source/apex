#include <JuceHeader.h>
#include "../../../Source/ThemeCore/ApexPrimitives.h"

class ApexPrimitivesTests final : public juce::UnitTest
{
public:
    ApexPrimitivesTests() : juce::UnitTest ("theme.apex-primitives.v1", "APEX.Theme") {}

    static juce::Image renderOnTransparent (int w, int h, const std::function<void (juce::Graphics&)>& draw)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        draw (g);
        return img;
    }

    void runTest() override
    {
        const juce::Rectangle<float> area (20.0f, 20.0f, 200.0f, 120.0f);

        beginTest ("signal sigil path stays inside its bounds and is non-empty");
        {
            const auto p = DAW::ApexPrimitives::buildSignalSigilPath (area);
            expect (! p.isEmpty());
            const auto b = p.getBounds();
            expect (b.getX()      >= area.getX()      - 0.5f);
            expect (b.getY()      >= area.getY()      - 0.5f);
            expect (b.getRight()  <= area.getRight()  + 0.5f);
            expect (b.getBottom() <= area.getBottom() + 0.5f);
        }

        beginTest ("signal sigil is deterministic for identical bounds");
        {
            const auto a = DAW::ApexPrimitives::buildSignalSigilPath (area);
            const auto b = DAW::ApexPrimitives::buildSignalSigilPath (area);
            expect (a.getBounds() == b.getBounds());
            expect (a == b);
        }

        beginTest ("eye motif path stays inside its bounds");
        {
            const auto p = DAW::ApexPrimitives::buildEyeMotifPath (area);
            expect (! p.isEmpty());
            const auto b = p.getBounds();
            expect (b.getX()      >= area.getX()      - 0.5f);
            expect (b.getY()      >= area.getY()      - 0.5f);
            expect (b.getRight()  <= area.getRight()  + 0.5f);
            expect (b.getBottom() <= area.getBottom() + 0.5f);
        }

        beginTest ("splatter is deterministic per seed and differs across seeds");
        {
            const auto a = DAW::ApexPrimitives::buildSplatterPaths (area, 0xA9E1);
            const auto b = DAW::ApexPrimitives::buildSplatterPaths (area, 0xA9E1);
            const auto c = DAW::ApexPrimitives::buildSplatterPaths (area, 0x1234);
            expectEquals ((int) a.size(), (int) b.size());
            expect (a.size() > 0);
            bool sameBounds = true;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].getBounds() != b[i].getBounds())
                    sameBounds = false;
            expect (sameBounds, "same seed must produce identical splatter");

            bool differs = (a.size() != c.size());
            if (! differs)
                for (size_t i = 0; i < a.size(); ++i)
                    if (a[i].getBounds() != c[i].getBounds())
                        differs = true;
            expect (differs, "different seeds must produce different splatter");
        }

        beginTest ("splatter droplets remain inside the requested region");
        {
            const auto drops = DAW::ApexPrimitives::buildSplatterPaths (area, 0xA9E1);
            for (const auto& d : drops)
            {
                const auto b = d.getBounds();
                expect (b.getX()      >= area.getX()      - 1.0f);
                expect (b.getY()      >= area.getY()      - 1.0f);
                expect (b.getRight()  <= area.getRight()  + 1.0f);
                expect (b.getBottom() <= area.getBottom() + 1.0f);
            }
        }

        beginTest ("panel primitive paints dark interior with visible border");
        {
            auto img = renderOnTransparent (240, 160, [&] (juce::Graphics& g)
            {
                DAW::ApexPrimitives::drawPanel (g, area, DAW::ApexPrimitives::PanelDepth::base, false);
            });

            const auto interior = img.getPixelAt (120, 80);
            expect (interior.getAlpha() > 0);
            expect (interior.getBrightness() < 0.15f, "APEX panels must stay near-black");

            const auto border = img.getPixelAt ((int) area.getX() + 1, (int) area.getCentreY());
            expect (border.getAlpha() > 0);
            expect (border.getBrightness() >= interior.getBrightness());
        }

        beginTest ("selected panel emits accent energy at the accent edge");
        {
            auto img = renderOnTransparent (240, 160, [&] (juce::Graphics& g)
            {
                DAW::ApexPrimitives::drawPanel (g, area, DAW::ApexPrimitives::PanelDepth::raised, true);
            });

            const auto topEdge = img.getPixelAt ((int) area.getCentreX(), (int) area.getY() + 1);
            expect (topEdge.getAlpha() > 0);
            expect (topEdge.getRed() > topEdge.getBlue() * 0.5f,
                    "selection accent should carry magenta energy, not neutral grey");
        }

        beginTest ("semantic glow is stronger for active than inactive state");
        {
            auto glowAlpha = [&] (bool active)
            {
                auto img = renderOnTransparent (240, 160, [&] (juce::Graphics& g)
                {
                    DAW::ApexPrimitives::drawGlow (g, area, active);
                });
                int total = 0;
                for (int y = 0; y < 160; y += 4)
                    for (int x = 0; x < 240; x += 4)
                        total += img.getPixelAt (x, y).getAlpha();
                return total;
            };

            const int activeSum   = glowAlpha (true);
            const int inactiveSum = glowAlpha (false);
            expect (activeSum   > 0,   "active glow must be visible");
            expect (inactiveSum >= 0);
            expect (activeSum   > inactiveSum * 2,
                    "glow is semantic: active must clearly outshine inactive");
        }

        beginTest ("trim knob keeps dark body and emits luminous arc when active");
        {
            DAW::ApexPrimitives::KnobState k;
            k.value01 = 0.75f;
            k.active  = true;

            auto img = renderOnTransparent (120, 120, [&] (juce::Graphics& g)
            {
                DAW::ApexPrimitives::drawTrimKnob (g, { 20, 20, 80, 80 }, k);
            });

            const auto body = img.getPixelAt (60, 60);
            expect (body.getAlpha() > 0);
            expect (body.getBrightness() < 0.20f, "knob body must stay dark");

            bool foundAccent = false;
            for (int y = 10; y < 110 && ! foundAccent; y += 2)
                for (int x = 10; x < 110 && ! foundAccent; x += 2)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (p.getAlpha() > 0 && p.getRed() > 150 && p.getBlue() > 60)
                        foundAccent = true;
                }
            expect (foundAccent, "active knob must show a luminous arc/pointer");
        }

        beginTest ("fader cap reads brighter than its rail");
        {
            DAW::ApexPrimitives::FaderState f;
            f.value01 = 0.6f;
            f.active  = true;

            auto img = renderOnTransparent (80, 200, [&] (juce::Graphics& g)
            {
                DAW::ApexPrimitives::drawFader (g, { 30, 10, 20, 180 }, f);
            });

            // Cap is centred on the value position: 60% from top of travel.
            const int capY = 10 + (int) ((1.0f - 0.6f) * 180.0f);

            int railBright = 0, capBright = 0;
            for (int y = 12; y < 188; ++y)
            {
                if (std::abs (y - capY) < 8)
                    continue; // exclude rows covered by the cap itself
                const auto p = img.getPixelAt (40, y);
                railBright = juce::jmax (railBright,
                                         (int) std::round (p.getBrightness() * 255.0f));
            }
            for (int dy = -2; dy <= 2; ++dy)
                capBright = juce::jmax (capBright,
                                        (int) std::round (img.getPixelAt (40, capY + dy)
                                                              .getBrightness() * 255.0f));

            expect (capBright > railBright + 20, "illuminated cap must stand out from the rail");
        }

        beginTest ("slash flow is deterministic, in-bounds, and diagonal");
        {
            const auto a = DAW::ApexPrimitives::buildSlashFlowPaths (area, 0xA9E1);
            const auto b = DAW::ApexPrimitives::buildSlashFlowPaths (area, 0xA9E1);
            const auto c = DAW::ApexPrimitives::buildSlashFlowPaths (area, 0x77);

            expect (a.size() > 0);
            expectEquals ((int) a.size(), (int) b.size());
            bool same = true;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].getBounds() != b[i].getBounds())
                    same = false;
            expect (same, "same seed must produce identical slash flow");

            bool differs = (a.size() != c.size());
            if (! differs)
                for (size_t i = 0; i < a.size(); ++i)
                    if (a[i].getBounds() != c[i].getBounds())
                        differs = true;
            expect (differs, "different seeds must produce different slash flow");

            for (const auto& p : a)
            {
                const auto bb = p.getBounds();
                expect (bb.getX()      >= area.getX()      - 1.0f);
                expect (bb.getY()      >= area.getY()      - 1.0f);
                expect (bb.getRight()  <= area.getRight()  + 1.0f);
                expect (bb.getBottom() <= area.getBottom() + 1.0f);
            }
        }

        beginTest ("control surface shows hover lift without layout change");
        {
            DAW::ApexPrimitives::ControlState idle, hover;
            hover.hover = true;
            auto i = renderOnTransparent (120, 60, [&] (juce::Graphics& g)
            { DAW::ApexPrimitives::drawControlSurface (g, { 10, 10, 100, 40 }, idle); });
            auto h = renderOnTransparent (120, 60, [&] (juce::Graphics& g)
            { DAW::ApexPrimitives::drawControlSurface (g, { 10, 10, 100, 40 }, hover); });

            const auto ci = i.getPixelAt (60, 30);
            const auto ch = h.getPixelAt (60, 30);
            expect (ch.getBrightness() > ci.getBrightness(),
                    "hover must visibly lift the control surface");
        }
    }
};

static ApexPrimitivesTests apexPrimitivesTests;

#include <JuceHeader.h>
#include "../../../Source/ThemeCore/Theme.h"

class ApexPaletteTests final : public juce::UnitTest
{
public:
    ApexPaletteTests() : juce::UnitTest ("theme.apex-palette.v1", "APEX.Theme") {}

    static double relativeLuminance (juce::Colour c)
    {
        auto channel = [] (float v)
        {
            return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126 * channel (c.getFloatRed())
             + 0.7152 * channel (c.getFloatGreen())
             + 0.0722 * channel (c.getFloatBlue());
    }

    static double contrastRatio (juce::Colour a, juce::Colour b)
    {
        const double la = relativeLuminance (a);
        const double lb = relativeLuminance (b);
        const double hi = juce::jmax (la, lb);
        const double lo = juce::jmin (la, lb);
        return (hi + 0.05) / (lo + 0.05);
    }

    void runTest() override
    {
        auto& theme = DAW::Theme::getInstance();

        beginTest ("signal-core palette exact values");
        {
            expectEquals (theme.apex.color.deepestA.getARGB(),   (juce::uint32) 0xff05070c);
            expectEquals (theme.apex.color.deepestB.getARGB(),   (juce::uint32) 0xff070a10);
            expectEquals (theme.apex.color.panelA.getARGB(),     (juce::uint32) 0xff0a0d15);
            expectEquals (theme.apex.color.panelB.getARGB(),     (juce::uint32) 0xff0e111b);
            expectEquals (theme.apex.color.panelC.getARGB(),     (juce::uint32) 0xff111522);
            expectEquals (theme.apex.color.borderSoftA.getARGB(),(juce::uint32) 0xff1a2233);
            expectEquals (theme.apex.color.borderSoftB.getARGB(),(juce::uint32) 0xff22283a);

            expectEquals (theme.apex.color.magenta.getARGB(),       (juce::uint32) 0xffff1678);
            expectEquals (theme.apex.color.magentaDeep.getARGB(),   (juce::uint32) 0xffe91572);
            expectEquals (theme.apex.color.magentaBright.getARGB(), (juce::uint32) 0xffff2a91);
            expectEquals (theme.apex.color.pink.getARGB(),          (juce::uint32) 0xffff3d9f);

            expectEquals (theme.apex.color.violet.getARGB(),        (juce::uint32) 0xff813cff);
            expectEquals (theme.apex.color.violetBright.getARGB(),  (juce::uint32) 0xffa34cff);

            expectEquals (theme.apex.color.blue.getARGB(),          (juce::uint32) 0xff087bff);
            expectEquals (theme.apex.color.blueBright.getARGB(),    (juce::uint32) 0xff168dff);
            expectEquals (theme.apex.color.cyan.getARGB(),          (juce::uint32) 0xff00c8ff);

            expectEquals (theme.apex.color.textPrimary.getARGB(),   (juce::uint32) 0xfff2f4fa);
            expectEquals (theme.apex.color.textSecondary.getARGB(), (juce::uint32) 0xffa6adbc);
            expectEquals (theme.apex.color.textMuted.getARGB(),     (juce::uint32) 0xff687083);

            expectEquals (theme.apex.color.activeGreen.getARGB(),   (juce::uint32) 0xff12c878);
        }

        beginTest ("semantic accents are mutually distinct");
        {
            expect (theme.apex.color.magenta != theme.apex.color.violet);
            expect (theme.apex.color.violet  != theme.apex.color.cyan);
            expect (theme.apex.color.cyan    != theme.apex.color.blue);
            expect (theme.apex.color.magenta != theme.apex.color.activeGreen);
        }

        beginTest ("text hierarchy is legible on deepest surfaces");
        {
            const double primary   = contrastRatio (theme.apex.color.textPrimary,   theme.apex.color.deepestA);
            const double secondary = contrastRatio (theme.apex.color.textSecondary, theme.apex.color.panelA);
            const double muted     = contrastRatio (theme.apex.color.textMuted,     theme.apex.color.panelA);

            expect (primary   >= 12.0, "primary text must be high contrast on the deepest base");
            expect (secondary >= 4.5,  "secondary text must stay readable on panels");
            expect (muted     >= 3.0,  "muted text must not vanish on panels");
            expect (primary > secondary);
            expect (secondary > muted);
        }

        beginTest ("panel depth ordering is monotonic");
        {
            const double d0 = relativeLuminance (theme.apex.color.deepestA);
            const double p1 = relativeLuminance (theme.apex.color.panelA);
            const double p2 = relativeLuminance (theme.apex.color.panelB);
            const double p3 = relativeLuminance (theme.apex.color.panelC);
            expect (d0 < p1);
            expect (p1 < p2);
            expect (p2 < p3);
        }

        beginTest ("state and decoration opacities are bounded and ordered");
        {
            auto inUnit = [] (float v) { return v >= 0.0f && v <= 1.0f; };
            expect (inUnit (theme.apex.state.hoverStrength));
            expect (inUnit (theme.apex.state.pressedStrength));
            expect (inUnit (theme.apex.state.selectedStrength));
            expect (inUnit (theme.apex.state.glowOpacityActive));
            expect (inUnit (theme.apex.state.glowOpacityInactive));
            expect (inUnit (theme.apex.decor.splatterOpacity));

            expect (theme.apex.state.glowOpacityActive   > theme.apex.state.glowOpacityInactive);
            expect (theme.apex.state.pressedStrength     > theme.apex.state.hoverStrength);
            expect (theme.apex.state.selectedStrength    > theme.apex.state.hoverStrength);
            expect (theme.apex.decor.splatterOpacity     <= 0.35f,
                    "splatter must stay decorative, never dominant");
        }

        beginTest ("stroke and radius metrics are positive and ordered");
        {
            expect (theme.apex.metric.strokeThin   > 0.0f);
            expect (theme.apex.metric.strokeNormal >= theme.apex.metric.strokeThin);
            expect (theme.apex.metric.radiusControl > 0.0f);
            expect (theme.apex.metric.radiusPanel   >= theme.apex.metric.radiusControl);
            expect (theme.apex.metric.radiusWindow  >= theme.apex.metric.radiusPanel);
            expect (theme.apex.metric.glowRadius    > 0.0f);
        }

        beginTest ("motion timings are bounded for expert workflows");
        {
            expect (theme.apex.motion.hoverMs  > 0 && theme.apex.motion.hoverMs  <= 200);
            expect (theme.apex.motion.pressMs  > 0 && theme.apex.motion.pressMs  <= 200);
            expect (theme.apex.motion.panelMs  > 0 && theme.apex.motion.panelMs  <= 400);
        }

        beginTest ("typography roles form a readable scale");
        {
            expect (theme.apex.font.displaySize   > theme.apex.font.panelTitleSize);
            expect (theme.apex.font.panelTitleSize > theme.apex.font.controlLabelSize);
            expect (theme.apex.font.controlLabelSize >= theme.apex.font.metadataSize);
            expect (theme.apex.font.numericSize    >= theme.apex.font.controlLabelSize);
            expect (theme.apex.font.metadataSize   >= 9.0f);
        }

        beginTest ("apex extension preserves existing slate theme values");
        {
            expectEquals (theme.colors.background.getARGB(), (juce::uint32) 0xff0d0d0f);
            expectEquals (theme.colors.accent.getARGB(),     (juce::uint32) 0xff7c3aed);
        }
    }
};

static ApexPaletteTests apexPaletteTests;

// ===========================================================================
// ClipColourPickerCore.cpp
// ===========================================================================
#include "ClipColourPickerCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg = 0xFF1E1E1E;
        constexpr uint32_t border = 0xFF404040;
        constexpr uint32_t text = 0xFFD4D4D4;
    }

    ClipColourPickerCore::ClipColourPickerCore()
    {
        setSize(400, 32);
        buildSwatches();
    }

    void ClipColourPickerCore::setClip(ArrangementClipModel* clip)
    {
        m_clip = clip;
        repaint();
    }

    void ClipColourPickerCore::buildSwatches()
    {
        m_swatches.clear();

        // Preset colours (8 swatches)
        const uint32_t presets[] = {
            0xFFE07B39,  // DAW orange
            0xFF4EC94E,  // green
            0xFFE0559A,  // pink
            0xFF60A0E0,  // blue
            0xFFE0D060,  // yellow
            0xFFB060E0,  // purple
            0xFFE06060,  // red
            0xFF60E0D0   // cyan
        };

        for (int i = 0; i < 8; ++i)
        {
            Swatch s;
            s.colour = fromU32(presets[i]);
            m_swatches.push_back(s);
        }
    }

    void ClipColourPickerCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds();

        // Label
        g.setColour(fromU32(Col::text));
        g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::plain));
        g.drawText("COLOUR", 0, 0, 80, 16, juce::Justification::centredLeft);

        // Swatches
        for (auto& s : m_swatches)
        {
            g.setColour(s.colour);
            g.fillRoundedRectangle(s.bounds.toFloat(), 3.f);

            // Selected indicator
            if (m_clip && m_clip->colour == s.colour)
            {
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(s.bounds.toFloat().reduced(1.f), 3.f, 2.f);
            }
            else
            {
                g.setColour(fromU32(Col::border));
                g.drawRoundedRectangle(s.bounds.toFloat().reduced(0.5f), 3.f, 1.f);
            }
        }
    }

    void ClipColourPickerCore::resized()
    {
        int x = 80;
        int y = 4;
        int swatchSize = 24;
        int gap = 4;

        for (auto& s : m_swatches)
        {
            s.bounds = juce::Rectangle<int>(x, y, swatchSize, swatchSize);
            x += swatchSize + gap;
        }
    }

    void ClipColourPickerCore::mouseDown(const juce::MouseEvent& e)
    {
        if (!m_clip) return;

        auto pt = e.getPosition();
        for (auto& s : m_swatches)
        {
            if (s.bounds.contains(pt))
            {
                m_clip->colour = s.colour;
                m_clip->explicitClipColourOverride = true; // explicit per-clip tint (body/header hue source)
                if (onColourChanged)
                    onColourChanged(s.colour);
                repaint();
                return;
            }
        }
    }

} // namespace ArrangementEditor

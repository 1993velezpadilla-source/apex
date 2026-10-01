// ===========================================================================
// PianoRollButtonCore.cpp
// ===========================================================================
#include "PianoRollButtonCore.h"

namespace ArrangementEditor
{
    namespace Col
    {
        constexpr uint32_t bg = 0xFF2A2A2A;
        constexpr uint32_t text = 0xFFD4D4D4;
        constexpr uint32_t accent = 0xFFFF1678;
        constexpr uint32_t btnNormal = 0xFF383838;
        constexpr uint32_t btnHover = 0xFF454545;
    }

    PianoRollButtonCore::PianoRollButtonCore(Style style)
        : m_style(style)
    {
        switch (m_style)
        {
        case Style::ClipOverlay:
            setSize(22, 14);
            break;
        case Style::MixerButton:
            setSize(40, 24);
            break;
        case Style::ToolbarButton:
            setSize(48, 28);
            break;
        case Style::MenuButton:
            setSize(120, 28);
            break;
        }
    }

    void PianoRollButtonCore::paint(juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();

        // Background
        g.setColour(fromU32(m_hovered ? Col::btnHover : Col::btnNormal));
        g.fillRoundedRectangle(b, 3.f);

        // Border
        g.setColour(fromU32(m_hovered ? Col::accent : 0xFF505050));
        g.drawRoundedRectangle(b.reduced(0.5f), 3.f, 1.f);

        // Content based on style
        g.setColour(fromU32(Col::text));

        switch (m_style)
        {
        case Style::ClipOverlay:
            // Tiny piano icon for clip header
            g.setFont(juce::Font("Segoe UI", 9.f, juce::Font::plain));
            g.drawText(juce::CharPointer_UTF8("\xf0\x9f\x8e\xb9"), b, juce::Justification::centred);
            break;

        case Style::MixerButton:
            // Piano icon + label
            g.setFont(juce::Font("Segoe UI", 10.f, juce::Font::plain));
            g.drawText(juce::CharPointer_UTF8("\xf0\x9f\x8e\xb9"), b.withTrimmedBottom(10), juce::Justification::centred);
            g.setFont(juce::Font("Segoe UI", 7.f, juce::Font::plain));
            g.drawText("Roll", b.withTrimmedTop(12), juce::Justification::centred);
            break;

        case Style::ToolbarButton:
            // Compact icon-only button
            g.setFont(juce::Font("Segoe UI", 14.f, juce::Font::plain));
            g.drawText(juce::CharPointer_UTF8("\xf0\x9f\x8e\xb9"), b.withTrimmedBottom(8), juce::Justification::centred);
            g.setFont(juce::Font("Segoe UI", 7.f, juce::Font::plain));
            g.drawText("Piano", b.withTrimmedTop(14), juce::Justification::centred);
            break;

        case Style::MenuButton:
            // Menu style text button
            g.setFont(juce::Font("Segoe UI", 11.f, juce::Font::plain));
            g.drawText(juce::String(juce::CharPointer_UTF8("\xf0\x9f\x8e\xb9")) + " Open Piano Roll", b.reduced(8.f, 0.f),
                      juce::Justification::centredLeft);
            break;
        }
    }

    void PianoRollButtonCore::mouseEnter(const juce::MouseEvent&)
    {
        m_hovered = true;
        repaint();
    }

    void PianoRollButtonCore::mouseExit(const juce::MouseEvent&)
    {
        m_hovered = false;
        repaint();
    }

    void PianoRollButtonCore::mouseDown(const juce::MouseEvent&)
    {
        if (onClick)
            onClick();
    }

} // namespace ArrangementEditor

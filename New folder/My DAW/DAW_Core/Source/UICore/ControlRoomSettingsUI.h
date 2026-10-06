#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../MonitorCore/ControlRoomEngine.h"

namespace DAW {

/**
 * ControlRoomSettingsUI — dedicated Control Room / Monitor Section configuration.
 *
 * Accessed from Settings → Control Room.
 * Configures the monitoring environment (NOT the render path).
 *
 * Supports:
 *   - Monitor output assignment
 *   - Speaker A/B/C setup (name, output channels, trim, enable)
 *   - Dim amount configuration
 *   - Monitor FX chain access (future)
 *   - Monitor path enable/disable
 *
 * This panel is SEPARATE from:
 *   - Audio Device settings (driver, sample rate, buffer)
 *   - Master Track settings (fader, inserts)
 *   - Track mixer settings
 */
class ControlRoomSettingsUI : public juce::Component
{
public:
    explicit ControlRoomSettingsUI(ControlRoomEngine& controlRoom)
        : controlRoom_(controlRoom)
    {}

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.backgroundDark);

        // Header
        auto header = getLocalBounds().removeFromTop(32).toFloat();
        g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.12f));
        g.fillRect(header);
        g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.85f));
        g.fillRect(header.removeFromBottom(1.5f));

        g.setFont(juce::Font(12.f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFCC9900));
        g.drawText("CONTROL ROOM / MONITOR SECTION", header.withLeft(12.f),
                   juce::Justification::centredLeft);

        closeBtnBounds_ = juce::Rectangle<float>((float)getWidth() - 24.f, 7.f, 16.f, 16.f);
        g.setColour(t.colors.surface.withAlpha(0.7f));
        g.fillRoundedRectangle(closeBtnBounds_, 3.f);
        g.setColour(t.colors.border.withAlpha(0.6f));
        g.drawRoundedRectangle(closeBtnBounds_, 3.f, 1.f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawLine(closeBtnBounds_.getX() + 4.f, closeBtnBounds_.getY() + 4.f,
                   closeBtnBounds_.getRight() - 4.f, closeBtnBounds_.getBottom() - 4.f, 1.4f);
        g.drawLine(closeBtnBounds_.getRight() - 4.f, closeBtnBounds_.getY() + 4.f,
                   closeBtnBounds_.getX() + 4.f, closeBtnBounds_.getBottom() - 4.f, 1.4f);

        // Body content
        auto b = getLocalBounds().toFloat();
        b.removeFromTop(40.f);
        float y = b.getY();
        float lx = 16.f;
        float rw = b.getWidth() - 32.f;

        // ── Speaker Sets ────────────────────────────────────────────────
        g.setFont(juce::Font(11.f, juce::Font::bold));
        g.setColour(t.colors.textSecondary.withAlpha(0.7f));
        g.drawText("SPEAKER SETS", lx, y, rw, 14.f, juce::Justification::centredLeft);
        y += 18.f;

        auto& speakers = controlRoom_.getSpeakers();
        for (int i = 0; i < SpeakerSetManager::kMaxSets; ++i)
        {
            auto& cfg = speakers.getSet(i);
            auto row = juce::Rectangle<float>(lx, y, rw, 28.f);

            g.setColour(t.colors.surface.withAlpha(0.4f));
            g.fillRoundedRectangle(row, 3.f);
            g.setColour(t.colors.border.withAlpha(0.3f));
            g.drawRoundedRectangle(row, 3.f, 1.f);

            g.setFont(juce::Font(11.f, juce::Font::bold));
            g.setColour(cfg.enabled ? t.colors.text : t.colors.textDisabled);
            g.drawText(cfg.name, row.withLeft(lx + 8.f).withWidth(40.f),
                       juce::Justification::centredLeft);

            g.setFont(juce::Font(9.5f));
            g.setColour(t.colors.textSecondary);
            juce::String chStr = "Out " + juce::String(cfg.outputChannelL + 1)
                               + "/" + juce::String(cfg.outputChannelR + 1);
            g.drawText(chStr, row.withLeft(lx + 52.f).withWidth(60.f),
                       juce::Justification::centredLeft);

            juce::String trimStr = juce::String(cfg.trimDb, 1) + " dB";
            g.drawText(trimStr, row.withRight(row.getRight() - 8.f).withWidth(50.f),
                       juce::Justification::centredRight);

            y += 32.f;
        }

        // ── Dim Amount ──────────────────────────────────────────────────
        y += 8.f;
        g.setFont(juce::Font(11.f, juce::Font::bold));
        g.setColour(t.colors.textSecondary.withAlpha(0.7f));
        g.drawText("DIM AMOUNT", lx, y, rw, 14.f, juce::Justification::centredLeft);
        y += 18.f;

        float dimDb = controlRoom_.getState().dimAmountDb.load();
        g.setFont(juce::Font(13.f, juce::Font::bold));
        g.setColour(t.colors.text);
        g.drawText(juce::String((int)dimDb) + " dB", lx, y, rw, 16.f,
                   juce::Justification::centredLeft);
        y += 24.f;

        // ── Monitor Path Status ─────────────────────────────────────────
        g.setFont(juce::Font(11.f, juce::Font::bold));
        g.setColour(t.colors.textSecondary.withAlpha(0.7f));
        g.drawText("MONITOR PATH", lx, y, rw, 14.f, juce::Justification::centredLeft);
        y += 18.f;

        auto& state = controlRoom_.getState();
        auto drawStatus = [&](const juce::String& label, bool active, juce::Colour col)
        {
            g.setFont(juce::Font(10.5f));
            g.setColour(active ? col : t.colors.textDisabled);
            g.drawText(label + ": " + (active ? "ON" : "OFF"),
                       lx, y, rw, 14.f, juce::Justification::centredLeft);
            y += 16.f;
        };

        drawStatus("Dim", state.dimActive.load(), juce::Colour(0xFFCC9900));
        drawStatus("Mute", state.muteActive.load(), juce::Colour(0xFFEF4444));
        drawStatus("Mono", state.monoActive.load(), juce::Colour(0xFF4488CC));
        drawStatus("Monitor FX", !state.monitorFxBypassed.load(), juce::Colour(0xFF44CC88));
    }

    void resized() override {}

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (closeBtnBounds_.contains(e.position))
            setVisible(false);
    }

private:
    ControlRoomEngine& controlRoom_;
    juce::Rectangle<float> closeBtnBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlRoomSettingsUI)
};

} // namespace DAW

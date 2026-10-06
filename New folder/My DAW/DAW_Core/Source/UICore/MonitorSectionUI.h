#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../MonitorCore/ControlRoomEngine.h"
#include "MixerPanel.h"

namespace DAW {

/**
 * MonitorSectionUI — dedicated Control Room / Monitor panel.
 *
 * Visually and architecturally separate from the track mixer.
 * Shows: monitor level (vertical drag), DIM / MUTE / MONO toggles,
 * speaker A/B/C select, monitor FX bypass, stereo monitor meter.
 *
 * All knobs use vertical drag (drag up = increase, drag down = decrease)
 * per DAW convention.
 */
class MonitorSectionUI : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr int kPreferredWidth = 200;

    explicit MonitorSectionUI(ControlRoomEngine& controlRoom)
        : controlRoom_(controlRoom)
    {
        startTimerHz(60);
    }

    ~MonitorSectionUI() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        auto  b = getLocalBounds().toFloat();

        // ── Background ──────────────────────────────────────────────────
        g.setColour(t.colors.backgroundDark.darker(0.12f));
        g.fillRect(b);

        // ── Left separator ──────────────────────────────────────────────
        g.setColour(t.colors.border.withAlpha(0.45f));
        g.fillRect(0.f, 0.f, 1.f, b.getHeight());

        // ── Header ──────────────────────────────────────────────────────
        auto headerR = juce::Rectangle<float>(1.f, 0.f, b.getWidth() - 1.f, 26.f);
        g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.12f));
        g.fillRect(headerR);
        g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.85f));
        g.fillRect(headerR.removeFromBottom(1.5f));

        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFCC9900));
        g.drawText("CONTROL ROOM", headerR.withLeft(8.f), juce::Justification::centredLeft);

        g.setFont(juce::Font(7.5f));
        g.setColour(t.colors.textSecondary.withAlpha(0.55f));
        g.drawText("MONITOR", headerR.withRight(headerR.getRight() - 6.f),
                   juce::Justification::centredRight);

        auto& state = controlRoom_.getState();

        // ── Layout zones ────────────────────────────────────────────────
        const float cx = b.getCentreX();
        float y = 34.f;

        // ── Monitor level fader ─────────────────────────────────────────
        {
            g.setFont(juce::Font(8.f, juce::Font::bold));
            g.setColour(t.colors.textSecondary.withAlpha(0.7f));
            g.drawText("MONITOR LEVEL", 8.f, y, b.getWidth() - 16.f, 12.f,
                       juce::Justification::centred);
            y += 14.f;

            faderBounds_ = juce::Rectangle<float>(cx - 14.f, y, 28.f, 90.f);
            drawMonitorFader(g, faderBounds_, state);
            y += 96.f;

            // dB readout
            float db = state.getMonitorGainDb();
            juce::String dbStr = (db <= -60.f) ? "-inf"
                : juce::String(db, 1) + " dB";
            g.setFont(juce::Font(9.f, juce::Font::bold));
            g.setColour(t.colors.text.withAlpha(0.85f));
            g.drawText(dbStr, 0.f, y, b.getWidth(), 14.f, juce::Justification::centred);
            y += 20.f;
        }

        // ── DIM / MUTE / MONO row ──────────────────────────────────────
        {
            const float btnW = 48.f, btnH = 22.f, gap = 6.f;
            float totalW = btnW * 3 + gap * 2;
            float bx = (b.getWidth() - totalW) * 0.5f;

            dimBtnBounds_  = juce::Rectangle<float>(bx, y, btnW, btnH);
            muteBtnBounds_ = juce::Rectangle<float>(bx + btnW + gap, y, btnW, btnH);
            monoBtnBounds_ = juce::Rectangle<float>(bx + (btnW + gap) * 2.f, y, btnW, btnH);

            drawToggleButton(g, dimBtnBounds_, "DIM",
                             state.dimActive.load(), juce::Colour(0xFFCC9900));
            drawToggleButton(g, muteBtnBounds_, "MUTE",
                             state.muteActive.load(), t.colors.transportRecord);
            drawToggleButton(g, monoBtnBounds_, "MONO",
                             state.monoActive.load(), t.colors.accent);
            y += btnH + 8.f;

            // Dim amount indicator
            if (state.dimActive.load())
            {
                g.setFont(juce::Font(7.5f));
                g.setColour(juce::Colour(0xFFCC9900).withAlpha(0.6f));
                g.drawText(juce::String((int)state.dimAmountDb.load()) + " dB",
                           dimBtnBounds_.withY(y - 4.f).withHeight(10.f),
                           juce::Justification::centred);
            }
            y += 8.f;
        }

        // ── Speaker A / B / C ───────────────────────────────────────────
        {
            g.setFont(juce::Font(8.f, juce::Font::bold));
            g.setColour(t.colors.textSecondary.withAlpha(0.6f));
            g.drawText("SPEAKERS", 8.f, y, b.getWidth() - 16.f, 12.f,
                       juce::Justification::centred);
            y += 14.f;

            const float btnW = 36.f, btnH = 20.f, gap = 8.f;
            float totalW = btnW * 3 + gap * 2;
            float bx = (b.getWidth() - totalW) * 0.5f;
            int active = state.activeSpeakerSet.load();

            for (int i = 0; i < 3; ++i)
            {
                auto& cfg = controlRoom_.getSpeakers().getSet(i);
                auto r = juce::Rectangle<float>(bx + i * (btnW + gap), y, btnW, btnH);
                spkBtnBounds_[i] = r;

                bool sel = (i == active);
                bool en  = cfg.enabled;
                g.setColour(sel ? juce::Colour(0xFFCC9900).withAlpha(0.75f)
                                : (en ? t.colors.surface.withAlpha(0.55f)
                                      : t.colors.surface.withAlpha(0.20f)));
                g.fillRoundedRectangle(r, 3.f);
                g.setColour(sel ? juce::Colour(0xFFCC9900) : t.colors.border.withAlpha(0.4f));
                g.drawRoundedRectangle(r, 3.f, 1.f);

                g.setFont(juce::Font(9.f, juce::Font::bold));
                g.setColour(sel ? juce::Colours::white
                                : (en ? t.colors.textSecondary : t.colors.textDisabled));
                g.drawText(cfg.name, r, juce::Justification::centred);
            }
            y += btnH + 10.f;
        }

        // ── Monitor FX bypass ───────────────────────────────────────────
        {
            const float btnW = 80.f, btnH = 20.f;
            fxBtnBounds_ = juce::Rectangle<float>((b.getWidth() - btnW) * 0.5f, y, btnW, btnH);
            bool bypassed = state.monitorFxBypassed.load();
            drawToggleButton(g, fxBtnBounds_, bypassed ? "MON FX OFF" : "MON FX ON",
                             !bypassed, juce::Colour(0xFF4488CC));
            y += btnH + 12.f;
        }

        // ── Stereo monitor meter ────────────────────────────────────────
        {
            g.setFont(juce::Font(7.5f, juce::Font::bold));
            g.setColour(t.colors.textSecondary.withAlpha(0.5f));
            g.drawText("MONITOR METER", 8.f, y, b.getWidth() - 16.f, 10.f,
                       juce::Justification::centred);
            y += 12.f;

            const float meterW = 10.f, meterH = 60.f, meterGap = 6.f;
            float mx = cx - meterW - meterGap * 0.5f;
            meterLBounds_ = juce::Rectangle<float>(mx, y, meterW, meterH);
            meterRBounds_ = juce::Rectangle<float>(mx + meterW + meterGap, y, meterW, meterH);

            drawMeterBar(g, meterLBounds_, smoothMeterL_);
            drawMeterBar(g, meterRBounds_, smoothMeterR_);

            // L / R labels
            g.setFont(juce::Font(7.f));
            g.setColour(t.colors.textSecondary.withAlpha(0.5f));
            g.drawText("L", meterLBounds_.withY(meterLBounds_.getBottom() + 1.f).withHeight(10.f),
                       juce::Justification::centred);
            g.drawText("R", meterRBounds_.withY(meterRBounds_.getBottom() + 1.f).withHeight(10.f),
                       juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto& state = controlRoom_.getState();
        auto pos = e.position;

        // DIM toggle
        if (dimBtnBounds_.contains(pos))
        {
            if (e.mods.isRightButtonDown())
                showDimAmountMenu();
            else
                state.dimActive.store(!state.dimActive.load());
            repaint(); return;
        }

        // MUTE toggle
        if (muteBtnBounds_.contains(pos))
        { state.muteActive.store(!state.muteActive.load()); repaint(); return; }

        // MONO toggle
        if (monoBtnBounds_.contains(pos))
        { state.monoActive.store(!state.monoActive.load()); repaint(); return; }

        // Speaker select
        for (int i = 0; i < 3; ++i)
        {
            if (spkBtnBounds_[i].contains(pos) && controlRoom_.getSpeakers().isEnabled(i))
            { state.activeSpeakerSet.store(i); repaint(); return; }
        }

        // Monitor FX bypass
        if (fxBtnBounds_.contains(pos))
        { state.monitorFxBypassed.store(!state.monitorFxBypassed.load()); repaint(); return; }

        // Fader click — start drag
        if (faderBounds_.contains(pos))
        {
            faderDragging_ = true;
            faderDragStartY_ = e.getScreenY();
            faderDragStartGain_ = state.monitorGain.load();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!faderDragging_) return;

        // Vertical drag: up = increase, down = decrease (per copilot instructions)
        int deltaY = faderDragStartY_ - e.getScreenY();
        float sensitivity = 0.006f;

        auto* core = FaderRangeCore::getGlobalInstance();
        float startDb = FaderRangeCore::gainToDb(faderDragStartGain_);
        float startPos = core ? core->dbToNorm(startDb)
                              : DbPositionMapper::dbToPos(startDb);
        float newPos = juce::jlimit(0.0f, 1.0f, startPos + deltaY * sensitivity);
        float newGain = core ? core->normToGain(newPos)
                             : DbPositionMapper::posToGain(newPos);

        controlRoom_.getState().monitorGain.store(newGain);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        faderDragging_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double-click fader → reset to 0 dB (unity)
        if (faderBounds_.contains(e.position))
        {
            controlRoom_.getState().monitorGain.store(1.0f);
            repaint();
        }
    }

private:
    ControlRoomEngine& controlRoom_;

    // Hit-test bounds (computed in paint)
    juce::Rectangle<float> faderBounds_;
    juce::Rectangle<float> dimBtnBounds_, muteBtnBounds_, monoBtnBounds_;
    juce::Rectangle<float> spkBtnBounds_[3];
    juce::Rectangle<float> fxBtnBounds_;
    juce::Rectangle<float> meterLBounds_, meterRBounds_;

    // Fader drag state
    bool  faderDragging_ = false;
    int   faderDragStartY_ = 0;
    float faderDragStartGain_ = 1.0f;

    // Smooth meter display
    float smoothMeterL_ = 0.0f, smoothMeterR_ = 0.0f;

    void timerCallback() override
    {
        auto& state = controlRoom_.getState();
        float targetL = state.meterPeakL.load(std::memory_order_relaxed);
        float targetR = state.meterPeakR.load(std::memory_order_relaxed);

        // PPM-style ballistics: instant attack, ~300ms decay
        smoothMeterL_ = targetL > smoothMeterL_ ? targetL : smoothMeterL_ * 0.92f;
        smoothMeterR_ = targetR > smoothMeterR_ ? targetR : smoothMeterR_ * 0.92f;

        if (isShowing()) repaint();
    }

    // ── Drawing helpers ─────────────────────────────────────────────────

    void drawMonitorFader(juce::Graphics& g, juce::Rectangle<float> r,
                          const MonitorStateModel& state) const
    {
        auto& t = Theme::getInstance();
        auto* core = FaderRangeCore::getGlobalInstance();
        float gain = state.monitorGain.load();
        float db = FaderRangeCore::gainToDb(gain);
        float pos = core ? core->dbToNorm(db)
                         : DbPositionMapper::dbToPos(db);

        // Track groove
        float trackX = r.getCentreX() - 2.f;
        g.setColour(t.colors.backgroundDark.darker(0.2f));
        g.fillRoundedRectangle(trackX, r.getY(), 4.f, r.getHeight(), 2.f);

        // Unity (0 dB) tick mark from the current fader format
        float unityPos = core ? core->getUnityNorm() : 0.88f;
        float unityY = r.getBottom() - r.getHeight() * unityPos;
        g.setColour(t.colors.textSecondary.withAlpha(0.35f));
        g.fillRect(r.getX(), unityY - 0.5f, r.getWidth(), 1.f);
        g.setFont(juce::Font(6.5f));
        g.drawText("0", r.withHeight(8.f).withY(unityY - 10.f), juce::Justification::centred);

        // Fill bar
        float fillH = r.getHeight() * pos;
        float fillY = r.getBottom() - fillH;

        juce::Colour fillCol = juce::Colour(0xFFCC9900);
        if (state.muteActive.load()) fillCol = juce::Colour(0xFF882222);
        else if (state.dimActive.load()) fillCol = juce::Colour(0xFF996600);

        g.setColour(fillCol.withAlpha(0.6f));
        g.fillRoundedRectangle(trackX, fillY, 4.f, fillH, 2.f);

        // Thumb
        float thumbY = juce::jlimit(r.getY() + 4.f, r.getBottom() - 4.f, fillY);
        g.setColour(fillCol);
        g.fillRoundedRectangle(r.getCentreX() - 8.f, thumbY - 4.f, 16.f, 8.f, 3.f);
        g.setColour(fillCol.brighter(0.3f));
        g.drawRoundedRectangle(r.getCentreX() - 8.f, thumbY - 4.f, 16.f, 8.f, 3.f, 1.f);
    }

    void drawToggleButton(juce::Graphics& g, juce::Rectangle<float> r,
                          const juce::String& label, bool active,
                          juce::Colour activeColor) const
    {
        auto& t = Theme::getInstance();
        bool hov = r.contains(getMouseXYRelative().toFloat());

        g.setColour(active ? activeColor.withAlpha(0.75f)
                           : (hov ? t.colors.surfaceHover : t.colors.surface.withAlpha(0.50f)));
        g.fillRoundedRectangle(r, 3.f);
        g.setColour(active ? activeColor : t.colors.border.withAlpha(0.40f));
        g.drawRoundedRectangle(r, 3.f, 1.f);

        g.setFont(juce::Font(8.f, juce::Font::bold));
        g.setColour(active ? juce::Colours::white : t.colors.textSecondary);
        g.drawText(label, r, juce::Justification::centred);
    }

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> r, float level) const
    {
        auto& t = Theme::getInstance();
        auto* core = FaderRangeCore::getGlobalInstance();

        // Background
        g.setColour(t.colors.backgroundDark.darker(0.25f));
        g.fillRoundedRectangle(r, 2.f);

        if (level <= 0.0001f) return;

        // Map level to dB position
        float db = level <= 0.000001f ? -96.f : 20.0f * std::log10(level);
        db = juce::jlimit(core ? core->getMinDb() : -96.0f,
                          core ? core->getMaxDb() : 6.0f,
                          db);
        float pos = core ? core->dbToNorm(db)
                         : DbPositionMapper::dbToPos(db);
        float fillH = r.getHeight() * pos;
        float fillY = r.getBottom() - fillH;

        // Green → yellow → red gradient
        juce::Colour barCol = juce::Colour(0xFF44CC44);
        if (db > 0.f) barCol = juce::Colour(0xFFCC4444);
        else if (db > -6.f) barCol = juce::Colour(0xFFCCCC44);

        g.setColour(barCol.withAlpha(0.85f));
        g.fillRoundedRectangle(r.getX(), fillY, r.getWidth(), fillH, 2.f);
    }

    void showDimAmountMenu()
    {
        juce::PopupMenu menu;
        auto& state = controlRoom_.getState();
        float current = state.dimAmountDb.load();

        const float amounts[] = { -6.f, -12.f, -18.f, -24.f };
        const char* labels[]  = { "-6 dB", "-12 dB", "-18 dB", "-24 dB" };

        for (int i = 0; i < 4; ++i)
            menu.addItem(i + 1, labels[i], true, std::abs(current - amounts[i]) < 0.1f);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [this, &state](int result)
        {
            const float amounts[] = { -6.f, -12.f, -18.f, -24.f };
            if (result >= 1 && result <= 4)
            {
                state.dimAmountDb.store(amounts[result - 1]);
                repaint();
            }
        });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitorSectionUI)
};

} // namespace DAW

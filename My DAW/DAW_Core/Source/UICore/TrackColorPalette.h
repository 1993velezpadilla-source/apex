#pragma once
#include <JuceHeader.h>
#include <array>
#include "../ThemeCore/Theme.h"
#include "../TrackCore/Track.h"

namespace DAW {

// Forward declare so CustomColorPanel can call TrackColorPalette::show
class TrackColorPalette;

/**
 * CustomColorPanel
 * Wraps juce::ColourSelector with Back / X / Confirm buttons.
 */
class CustomColorPanel : public juce::Component, public juce::ChangeListener
{
public:
    static constexpr int kBarH = 30;

    CustomColorPanel(Track& track, juce::Component* owner, juce::Rectangle<int> stripBounds)
        : track_(track), owner_(owner), stripBounds_(stripBounds),
          originalColor_(track.getColor())
    {
        selector_ = std::make_unique<juce::ColourSelector>(
            juce::ColourSelector::showColourAtTop
            | juce::ColourSelector::showSliders
            | juce::ColourSelector::showColourspace);
        selector_->setCurrentColour(track.getColor());
        selector_->addChangeListener(this);
        addAndMakeVisible(selector_.get());
        setSize(300, kBarH + 260 + kBarH + 4);
    }

    ~CustomColorPanel() override
    {
        if (selector_)
            selector_->removeChangeListener(this);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop(kBarH);
        auto bot = b.removeFromBottom(kBarH);

        backBtn_    = top.removeFromLeft(70).reduced(4, 3).toFloat();
        closeBtn_   = top.removeFromRight(kBarH).reduced(5, 3).toFloat();
        confirmBtn_ = bot.reduced(4, 3).toFloat();
        selector_->setBounds(b);
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.surface);

        // ← Back button
        g.setColour(t.colors.surfaceHover);
        g.fillRoundedRectangle(backBtn_, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(backBtn_, 4.f, 1.f);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.regular);
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x90 Back"), backBtn_, juce::Justification::centred);

        // X close button
        g.setColour(t.colors.surfaceHover);
        g.fillRoundedRectangle(closeBtn_, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(closeBtn_, 4.f, 1.f);
        g.setColour(t.colors.text);
        g.setFont(t.fonts.bold);
        g.drawText(juce::CharPointer_UTF8("\xc3\x97"), closeBtn_, juce::Justification::centred);

        // Confirm button
        g.setColour(t.colors.accent.darker(0.1f));
        g.fillRoundedRectangle(confirmBtn_, 4.f);
        g.setColour(t.colors.accent.brighter(0.2f));
        g.drawRoundedRectangle(confirmBtn_, 4.f, 1.f);
        g.setColour(juce::Colours::white);
        g.setFont(t.fonts.bold);
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93 Confirm"), confirmBtn_, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (auto* cs = dynamic_cast<juce::ColourSelector*>(source))
        {
            auto c = cs->getCurrentColour();
            track_.setColor(c);
            if (extraColourCallback_) extraColourCallback_(c);
            if (owner_ && owner_->getTopLevelComponent())
                owner_->getTopLevelComponent()->repaint();
        }
    }

    std::function<void(juce::Colour)> extraColourCallback_;

private:
    Track& track_;
    juce::Component* owner_ = nullptr;
    juce::Rectangle<int> stripBounds_;
    juce::Colour originalColor_;
    std::unique_ptr<juce::ColourSelector> selector_;
    juce::Rectangle<float> backBtn_, closeBtn_, confirmBtn_;
};

/**
 * TrackColorPalette
 * A compact grid of DAW-standard track colours shown in a CallOutBox.
 * Click a swatch → sets the track colour; the box auto-dismisses.
 */
class TrackColorPalette : public juce::Component
{
public:
    static constexpr int kCols   = 8;
    static constexpr int kRows   = 6;
    static constexpr int kSwatch = 24;
    static constexpr int kGap    = 4;
    static constexpr int kTotal  = kCols * kRows;
    static constexpr int kBtnH   = 26;

    explicit TrackColorPalette(Track& track) : track_(track)
    {
        int gridW = kCols * (kSwatch + kGap) + kGap;
        int gridH = kRows * (kSwatch + kGap) + kGap;
        setSize(gridW, gridH + kBtnH + kGap);
        initPalette();
    }

    void paint(juce::Graphics& g) override
    {
        auto& t = Theme::getInstance();
        g.fillAll(t.colors.surface);

        for (int i = 0; i < kTotal; ++i)
        {
            auto r = swatchRect(i);
            g.setColour(colors_[i]);
            g.fillRoundedRectangle(r, 3.f);

            if (colors_[i] == track_.getColor())
            {
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(r.reduced(1.f), 3.f, 2.f);
            }
        }

        // Bottom row: [Custom...] [✓ OK]
        auto btn = customBtnRect();
        g.setColour(t.colors.surfaceHover);
        g.fillRoundedRectangle(btn, 4.f);
        g.setColour(t.colors.border);
        g.drawRoundedRectangle(btn, 4.f, 1.f);

        // Rainbow preview bar
        float rbY = btn.getY() + 4.f;
        float rbH = btn.getHeight() - 8.f;
        for (float px = btn.getX() + 6.f; px < btn.getX() + 30.f; px += 1.f)
        {
            float hue = (px - btn.getX() - 6.f) / 24.f;
            g.setColour(juce::Colour::fromHSV(hue, 0.8f, 0.9f, 1.f));
            g.fillRect(px, rbY, 1.f, rbH);
        }
        g.setColour(t.colors.text);
        g.setFont(t.fonts.regular);
        g.drawText("Custom...", btn.withTrimmedLeft(34), juce::Justification::centredLeft);

        // OK / confirm button
        auto ok = okBtnRect();
        g.setColour(t.colors.accent.darker(0.1f));
        g.fillRoundedRectangle(ok, 4.f);
        g.setColour(t.colors.accent.brighter(0.2f));
        g.drawRoundedRectangle(ok, 4.f, 1.f);
        g.setColour(juce::Colours::white);
        g.setFont(t.fonts.bold);
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), ok, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // OK button — just close
        if (okBtnRect().contains(e.position))
        {
            if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
                cb->setVisible(false);
            return;
        }

        // Swatch click
        for (int i = 0; i < kTotal; ++i)
        {
            if (swatchRect(i).contains(e.position))
            {
                track_.setColor(colors_[i]);
                if (extraColourCallback_) extraColourCallback_(colors_[i]);
                if (owner_) owner_->getTopLevelComponent()->repaint();
                if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
                    cb->setVisible(false);
                return;
            }
        }

        // Custom button click
        if (customBtnRect().contains(e.position))
        {
            juce::Component::SafePointer<juce::Component> safeOwner(owner_);
            auto& trackRef = track_;
            auto bounds = stripBounds_;
            auto cb2 = extraColourCallback_;
            if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
                cb->setVisible(false);

            juce::MessageManager::callAsync([&trackRef, safeOwner, bounds, cb2]()
            {
                // SafePointer dereference: returns nullptr if the Component
                // was destroyed between posting and delivery — no freed-memory access.
                auto* owner = safeOwner.getComponent();
                if (owner == nullptr)
                    return;
                auto panel = std::make_unique<CustomColorPanel>(trackRef, owner, bounds);
                panel->extraColourCallback_ = cb2;
                juce::CallOutBox::launchAsynchronously(std::move(panel), bounds, nullptr);
            });
        }
    }

    static void show(Track& track, juce::Component& target, juce::Rectangle<int> stripScreenBounds)
    {
        auto palette = std::make_unique<TrackColorPalette>(track);
        palette->owner_ = &target;
        palette->stripBounds_ = stripScreenBounds;
        juce::CallOutBox::launchAsynchronously(std::move(palette),
                                               stripScreenBounds,
                                               nullptr);
    }

    /** Like show() but fires an extra callback whenever the colour changes,
     *  so callers can propagate to a multi-selection. */
    static void showWithCallback(Track& track, juce::Component& target,
                                 juce::Rectangle<int> stripScreenBounds,
                                 std::function<void(juce::Colour)> onColourChanged)
    {
        auto palette = std::make_unique<TrackColorPalette>(track);
        palette->owner_ = &target;
        palette->stripBounds_ = stripScreenBounds;
        palette->extraColourCallback_ = std::move(onColourChanged);
        juce::CallOutBox::launchAsynchronously(std::move(palette),
                                               stripScreenBounds,
                                               nullptr);
    }

private:
    Track& track_;
    juce::Component* owner_ = nullptr;
    juce::Rectangle<int> stripBounds_;
    juce::Colour colors_[kTotal];
    std::function<void(juce::Colour)> extraColourCallback_;

    void initPalette()
    {
        const auto vals = paletteValues();
        for (int i = 0; i < kTotal; ++i)
            colors_[i] = juce::Colour(vals[(size_t) i]);
    }

public:
    /** The canonical APEX 48-swatch track-color palette — the single source
     *  of truth shared by the track-color picker and the Quick Track Builder
     *  automatic role-color system. */
    static juce::Array<juce::Colour> getCanonicalPalette()
    {
        juce::Array<juce::Colour> out;
        for (auto v : paletteValues())
            out.add(juce::Colour(v));
        return out;
    }

private:
    static const std::array<juce::uint32, (size_t) kTotal>& paletteValues()
    {
        static const std::array<juce::uint32, (size_t) kTotal> vals =
        {{
            0xffff4444, 0xffff6655, 0xffff3366, 0xffcc2255,
            0xffff5577, 0xffff8888, 0xffcc3344, 0xffaa2233,
            0xffff8833, 0xffffaa44, 0xffffcc33, 0xffffdd55,
            0xffffb822, 0xffee9922, 0xffdd7711, 0xffcc6600,
            0xff44dd44, 0xff33bb55, 0xff22cc66, 0xff44ddaa,
            0xff88dd33, 0xff66cc22, 0xff339966, 0xff228855,
            0xff4488ff, 0xff3366ee, 0xff2255dd, 0xff44aaff,
            0xff55ccff, 0xff3399cc, 0xff2277aa, 0xff225588,
            0xffaa44ff, 0xff8833dd, 0xffcc55ff, 0xffff55cc,
            0xffdd44aa, 0xff9944cc, 0xff7733aa, 0xffbb66dd,
            0xffdddddd, 0xffaaaaaa, 0xff888899, 0xff667788,
            0xffaabb99, 0xffbbaa88, 0xff998877, 0xff556666,
        }};
        return vals;
    }

public:

    juce::Rectangle<float> swatchRect(int i) const
    {
        int c = i % kCols, r = i / kCols;
        return juce::Rectangle<float>(
            (float)(kGap + c * (kSwatch + kGap)),
            (float)(kGap + r * (kSwatch + kGap)),
            (float)kSwatch, (float)kSwatch);
    }

    juce::Rectangle<float> customBtnRect() const
    {
        int gridH = kRows * (kSwatch + kGap) + kGap;
        return juce::Rectangle<float>(
            (float)kGap, (float)gridH,
            (float)(getWidth() - kGap * 2 - kBtnH - kGap), (float)kBtnH);
    }

    juce::Rectangle<float> okBtnRect() const
    {
        int gridH = kRows * (kSwatch + kGap) + kGap;
        return juce::Rectangle<float>(
            (float)(getWidth() - kGap - kBtnH), (float)gridH,
            (float)kBtnH, (float)kBtnH);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackColorPalette)
};

// ── Out-of-line definition (needs full TrackColorPalette) ─────────────────────
inline void CustomColorPanel::mouseDown(const juce::MouseEvent& e)
{
    if (closeBtn_.contains(e.position))
    {
        track_.setColor(originalColor_);
        if (owner_) owner_->getTopLevelComponent()->repaint();
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);
        return;
    }
    if (confirmBtn_.contains(e.position))
    {
        if (owner_) owner_->getTopLevelComponent()->repaint();
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);
        return;
    }
    if (backBtn_.contains(e.position))
    {
        juce::Component::SafePointer<juce::Component> safeOwner(owner_);
        auto bounds = stripBounds_;
        auto& trackRef = track_;
        auto cb2 = extraColourCallback_;

        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);

        juce::MessageManager::callAsync([&trackRef, safeOwner, bounds, cb2]()
        {
            auto* owner = safeOwner.getComponent();
            if (owner == nullptr)
                return;
            if (cb2)
                TrackColorPalette::showWithCallback(trackRef, *owner, bounds, cb2);
            else
                TrackColorPalette::show(trackRef, *owner, bounds);
        });
        return;
    }
}

} // namespace DAW

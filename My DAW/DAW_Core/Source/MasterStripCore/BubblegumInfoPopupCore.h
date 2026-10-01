#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * BubblegumInfoPopup
 *
 * Floating tooltip-style popup for explaining DAW features. Used by ? buttons
 * in master strip sections (and reusable elsewhere). Shows a title + body text
 * + "affects export" badge.
 *
 * Visual language:
 *   - Pink Bubblegum border (consistent with master strip identity)
 *   - Dark body with subtle drip accent on top-left
 *   - Body text in cream/light pink for readability
 *   - "Affects export" badge: green "SI" or red "NO" (clear visual cue)
 *
 * Behavior:
 *   - Shown via showFor(parent, info)
 *   - Auto-closes on click inside, Esc, focus loss, or when another popup opens
 *
 * Threading: UI thread only.
 */
class BubblegumInfoPopup : public juce::Component
{
public:
    struct Info
    {
        juce::String title;
        juce::String body;
        bool affectsExport = false;
    };

    BubblegumInfoPopup()
    {
        setOpaque(false);
        setAlwaysOnTop(true);
        setWantsKeyboardFocus(true);
    }

    static void showFor(juce::Component* parent, const Info& info)
    {
        if (parent == nullptr) return;

        auto popup = std::make_unique<BubblegumInfoPopup>();
        popup->setInfo(info);

        const int width = 300;
        popup->layOutForWidth(width);
        const int height = popup->getRequiredHeight();

        auto parentScreenPos = parent->getScreenBounds();
        int x = parentScreenPos.getX();
        int y = parentScreenPos.getBottom() + 4;

        auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForPoint({ x, y });
        if (display != nullptr)
        {
            const auto bounds = display->userArea;
            if (x + width > bounds.getRight())
                x = bounds.getRight() - width - 8;
            if (y + height > bounds.getBottom())
                y = parentScreenPos.getY() - height - 4;
            if (y < bounds.getY()) y = bounds.getY() + 4;
            if (x < bounds.getX()) x = bounds.getX() + 4;
        }

        popup->setBounds(x, y, width, height);
        popup->addToDesktop(juce::ComponentPeer::windowIsTemporary
                          | juce::ComponentPeer::windowHasDropShadow);
        popup->setVisible(true);
        popup->grabKeyboardFocus();

        getActivePopup() = std::move(popup);
    }

    static void dismissActive()
    {
        getActivePopup().reset();
    }

    void setInfo(const Info& info)
    {
        info_ = info;
        repaint();
    }

    void layOutForWidth(int width)
    {
        layoutWidth_ = width;
    }

    int getRequiredHeight() const
    {
        const int padding = 12;
        const int titleH = 24;
        const int badgeRowH = 28;
        const int bodyW = layoutWidth_ - padding * 2;

        juce::AttributedString s(info_.body);
        s.setFont(juce::Font(11.0f));
        s.setWordWrap(juce::AttributedString::WordWrap::byWord);
        juce::TextLayout tl;
        tl.createLayout(s, (float) bodyW);
        const int bodyH = juce::jmax(40, (int) std::ceil(tl.getHeight()));

        return padding * 2 + titleH + 8 + badgeRowH + 8 + bodyH + padding;
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        juce::DropShadow ds(juce::Colours::black.withAlpha(0.55f), 8, juce::Point<int>(0, 3));
        ds.drawForRectangle(g, bounds.toNearestInt());

        juce::ColourGradient bodyGrad(
            juce::Colour(0xFF2A1A24), bounds.getX(), bounds.getY(),
            juce::Colour(0xFF1A0F16), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(bodyGrad);
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(juce::Colour(0xFFFF4F8A));
        g.drawRoundedRectangle(bounds, 8.0f, 1.2f);

        juce::ColourGradient dripGrad(
            juce::Colour(0xFFFF4F8A).withAlpha(0.6f), bounds.getX() + 8, bounds.getY(),
            juce::Colour(0xFFFF4F8A).withAlpha(0.0f), bounds.getX() + 8, bounds.getY() + 32, false);
        g.setGradientFill(dripGrad);
        g.fillRoundedRectangle(juce::Rectangle<float>(bounds.getX() + 4, bounds.getY(), 8, 32), 2.0f);

        auto inner = bounds.toNearestInt().reduced(12);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        auto titleArea = inner.removeFromTop(24);
        g.drawText(info_.title, titleArea, juce::Justification::centredLeft, false);

        inner.removeFromTop(6);

        auto badgeRow = inner.removeFromTop(20);
        g.setColour(juce::Colour(0xFFFFB3D0));
        g.setFont(juce::Font(10.0f, juce::Font::plain));
        g.drawText("Afecta el render exportado:", badgeRow.removeFromLeft(160),
                   juce::Justification::centredLeft, false);

        const auto badgeRect = badgeRow.removeFromLeft(40).reduced(0, 2);
        const juce::Colour badgeColour = info_.affectsExport
            ? juce::Colour(0xFFFF6B6B)
            : juce::Colour(0xFF22C55E);
        g.setColour(badgeColour.withAlpha(0.25f));
        g.fillRoundedRectangle(badgeRect.toFloat(), 3.0f);
        g.setColour(badgeColour);
        g.drawRoundedRectangle(badgeRect.toFloat(), 3.0f, 0.8f);
        g.setColour(badgeColour.brighter(0.3f));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(info_.affectsExport ? juce::String::fromUTF8("S\xC3\x8D") : "NO", badgeRect,
                   juce::Justification::centred, false);

        inner.removeFromTop(10);

        juce::AttributedString body(info_.body);
        body.setColour(juce::Colour(0xFFE8DEC7));
        body.setFont(juce::Font(11.0f, juce::Font::plain));
        body.setJustification(juce::Justification::topLeft);
        body.setWordWrap(juce::AttributedString::WordWrap::byWord);
        body.draw(g, inner.toFloat());
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        dismissActive();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            dismissActive();
            return true;
        }

        return false;
    }

    void inputAttemptWhenModal() override
    {
        dismissActive();
    }

    void focusLost(FocusChangeType) override
    {
        dismissActive();
    }

private:
    static std::unique_ptr<BubblegumInfoPopup>& getActivePopup()
    {
        static std::unique_ptr<BubblegumInfoPopup> instance;
        return instance;
    }

    Info info_;
    int layoutWidth_ = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumInfoPopup)
};

} // namespace DAW

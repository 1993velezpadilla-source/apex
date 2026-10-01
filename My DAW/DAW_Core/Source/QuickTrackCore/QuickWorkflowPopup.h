// ===========================================================================
// QuickWorkflowPopup.h
// APEX Quick Workflow — one compact panel with Track / Route / Send tabs.
//
// This is a presentation layer only.  Creation, colors, sends, and routing
// are delegated to QuickWorkflowCore and therefore remain on the control/UI
// side of the RoutingGraph authority boundary.
// ===========================================================================
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

#include "QuickWorkflowCore.h"
#include "QuickTrackBuilderPopup.h"
#include "../ThemeCore/Theme.h"

namespace DAW
{

class QuickWorkflowFlatButton : public juce::Button
{
public:
    explicit QuickWorkflowFlatButton(const juce::String& text)
        : juce::Button(text), text_(text)
    {
    }

    void setText(const juce::String& text)
    {
        text_ = text;
        repaint();
    }

    const juce::String& getText() const noexcept { return text_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat();
        const auto active = text_.containsIgnoreCase("CREATE")
                         || text_.containsIgnoreCase("ADD");
        g.setColour(down ? a.color.panelA
                  : active ? a.color.magenta.withAlpha(0.88f)
                  : highlighted ? a.color.panelB.brighter(0.14f)
                  : a.color.panelB);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(active ? a.color.magenta.brighter(0.2f) : a.color.borderSoftA);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(active ? juce::Colours::white : a.color.textSecondary);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(11.0f));
        g.drawText(text_, getLocalBounds().reduced(4, 0), juce::Justification::centred);
    }

private:
    juce::String text_;
};

class QuickWorkflowCloseButton final : public juce::Button
{
public:
    QuickWorkflowCloseButton() : juce::Button("Close Quick Workflow") {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(down || highlighted ? a.color.panelC.brighter(0.16f) : a.color.panelB);
        g.fillRoundedRectangle(bounds, 5.0f);
        drawClearCross(g, bounds.reduced(9.0f), a.color.textSecondary, 1.5f);
    }
};

class QuickWorkflowTabButton final : public juce::Button
{
public:
    explicit QuickWorkflowTabButton(const juce::String& text)
        : juce::Button(text), text_(text)
    {
    }

    void setActive(bool active) noexcept
    {
        active_ = active;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(active_ ? a.color.panelA : highlighted || down ? a.color.panelC : a.color.panelB);
        g.fillRoundedRectangle(bounds, 5.0f);
        if (active_)
        {
            g.setColour(a.color.magenta);
            g.fillRoundedRectangle(bounds.withHeight(2.0f), 1.0f);
        }
        g.setColour(active_ ? a.color.textPrimary : a.color.textMuted);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(11.0f));
        g.drawText(text_, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String text_;
    bool active_ = false;
};

class QuickWorkflowColorButton final : public juce::Button
{
public:
    QuickWorkflowColorButton(QuickWorkflowCore& core, const QuickTrackRole& role)
        : juce::Button("Color for " + juce::String(role.displayName)), core_(core), role_(role)
    {
        setTooltip("Auto Color");
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(down || highlighted ? a.color.panelC : a.color.panelB);
        g.fillRoundedRectangle(bounds, 5.0f);

        const auto dot = bounds.reduced(7.0f);
        const auto color = core_.getRoleColor(role_);
        if (color.isOpaque())
        {
            g.setColour(color);
            g.fillEllipse(dot);
            g.setColour(a.color.borderSoftA);
            g.drawEllipse(dot.reduced(0.5f), 1.0f);
        }
        else
        {
            g.setColour(a.color.panelC.brighter(0.2f));
            g.fillEllipse(dot);
            g.setColour(a.color.textMuted);
            g.drawEllipse(dot.reduced(0.5f), 1.0f);
        }
    }

private:
    QuickWorkflowCore& core_;
    const QuickTrackRole& role_;
};

class QuickWorkflowTrashButton final : public QuickTrackChipButton
{
public:
    QuickWorkflowTrashButton() : QuickTrackChipButton("Reset count") {}

    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                      bool, bool) override
    {
        const auto b = bounds.toFloat().reduced(7.0f);
        const auto colour = Theme::getInstance().apex.color.textMuted;
        g.setColour(colour);

        const float lidY = b.getY() + 2.0f;
        const float lidLeft = b.getX() + 1.5f;
        const float lidRight = b.getRight() - 1.5f;
        g.drawLine(lidLeft, lidY, lidRight, lidY, 1.3f);
        g.drawLine(b.getCentreX() - 1.5f, lidY - 1.5f,
                   b.getCentreX() + 1.5f, lidY - 1.5f, 1.1f);

        juce::Path body;
        const float bodyTop = lidY + 2.0f;
        const float bodyBottom = b.getBottom() - 0.5f;
        body.startNewSubPath(lidLeft + 2.5f, bodyTop);
        body.lineTo(lidRight - 2.5f, bodyTop);
        body.lineTo(lidRight - 3.2f, bodyBottom);
        body.lineTo(lidLeft + 3.2f, bodyBottom);
        body.closeSubPath();
        g.strokePath(body, juce::PathStrokeType(1.1f));

        const float lineTop = bodyTop + 2.0f;
        const float lineBottom = bodyBottom - 1.5f;
        g.drawLine(b.getCentreX(), lineTop, b.getCentreX(), lineBottom, 0.75f);
        g.drawLine(b.getCentreX() - 2.0f, lineTop,
                   b.getCentreX() - 1.7f, lineBottom, 0.75f);
        g.drawLine(b.getCentreX() + 2.0f, lineTop,
                   b.getCentreX() + 1.7f, lineBottom, 0.75f);
    }
};

class QuickWorkflowRoleCard final : public juce::Component
{
public:
    QuickWorkflowRoleCard(QuickWorkflowCore& core,
                          const QuickTrackRole& role,
                          QuickWorkflowTab tab)
        : core_(core), role_(role), tab_(tab), colorButton_(core, role),
          minusButton_(), plusButton_(), trashButton_(),
          createButton_("CREATE")
    {
        hasVariants_ = QuickWorkflowRoleCatalog::roleHasVariants(juce::String(role.roleId));
        addAndMakeVisible(colorButton_);
        addAndMakeVisible(minusButton_);
        addAndMakeVisible(plusButton_);
        addAndMakeVisible(trashButton_);
        addAndMakeVisible(createButton_);
        trashButton_.setTooltip("Reset count");

        minusButton_.onClick = [this]
        {
            count_ = juce::jmax(0, count_ - 1);
            notifyCountChanged();
        };
        plusButton_.onClick = [this]
        {
            count_ = juce::jmin(99, count_ + 1);
            notifyCountChanged();
        };
        trashButton_.onClick = [this]
        {
            if (count_ != 0)
            {
                count_ = 0;
                notifyCountChanged();
            }
        };
        createButton_.onClick = [this]
        {
            if (onCreate)
            {
                const int requested = (tab_ == QuickWorkflowTab::QuickRoute)
                                    ? count_
                                    : juce::jmax(1, count_);
                if (onCreate(role_, requested))
                {
                    count_ = 0;
                    notifyCountChanged();
                }
            }
        };
        colorButton_.onClick = [this]
        {
            if (onColorRequest)
                onColorRequest(role_, colorButton_.getScreenBounds());
        };
    }

    void paint(juce::Graphics& g) override
    {
        auto& a = Theme::getInstance().apex;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(a.color.panelB);
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

        g.setColour(a.color.textPrimary);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(12.0f));
        g.drawText(role_.displayName, 46, 0, juce::jmax(40, getWidth() - 230), getHeight(),
                   juce::Justification::centredLeft);

        g.setColour(a.color.textPrimary);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(12.0f));
        g.drawText(juce::String(count_), countBounds_, juce::Justification::centred);

        // Variant dropdown arrow (Vocal FX): clicking the arrow opens the
        // list of common vocal chains. Creation and routing are unchanged —
        // the chosen chain only overrides the track's display name.
        variantArrowBounds_ = {};
        if (hasVariants_)
        {
            const auto nameFont = Theme::getInstance().fonts.bold.withHeight(12.0f);
            const int nameX = 46;
            const int nameWidth = juce::roundToInt(
                juce::GlyphArrangement::getStringWidth(nameFont, role_.displayName));
            variantArrowBounds_ = juce::Rectangle<int>(nameX + nameWidth + 3, 0, 16, getHeight());
            g.setColour(a.color.textSecondary);
            g.setFont(nameFont);
            g.drawText(juce::CharPointer_UTF8("\xE2\x96\xBE"), variantArrowBounds_,
                       juce::Justification::centred);
        }
    }

    void resized() override
    {
        constexpr int h = 28;
        const int y = (getHeight() - h) / 2;
        const int right = getWidth() - 6;
        colorButton_.setBounds(8, y, 28, h);

        createButton_.setBounds(right - 76, y, 76, h);
        trashButton_.setBounds(createButton_.getX() - 30, y, 28, h);
        plusButton_.setBounds(trashButton_.getX() - 30, y, 28, h);
        countBounds_ = juce::Rectangle<int>(plusButton_.getX() - 28, 0, 28, getHeight());
        minusButton_.setBounds(countBounds_.getX() - 30, y, 28, h);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (hasVariants_ && variantArrowBounds_.contains(e.getPosition()))
            showVariantMenu();
    }

    const QuickTrackRole& getRole() const noexcept { return role_; }
    int getCount() const noexcept { return count_; }
    void setCount(int count) noexcept
    {
        count_ = juce::jlimit(0, 99, count);
        repaint();
    }

    juce::Button& getColorButton() noexcept { return colorButton_; }
    juce::Button& getMinusButton() noexcept { return minusButton_; }
    juce::Button& getPlusButton() noexcept { return plusButton_; }
    juce::Button& getTrashButton() noexcept { return trashButton_; }
    juce::Button& getCreateButton() noexcept { return createButton_; }

    std::function<bool(const QuickTrackRole&, int)> onCreate;
    std::function<bool(const QuickTrackRole&, const juce::String&, int)> onCreateVariant;
    std::function<void(const QuickTrackRole&, juce::Rectangle<int>)> onColorRequest;
    std::function<void()> onCountChanged;

private:
    void showVariantMenu()
    {
        if (! onCreateVariant)
            return;

        juce::PopupMenu menu;
        juce::StringArray idToName;
        QuickWorkflowRoleCatalog::buildVariantMenu(menu, juce::String(role_.roleId), idToName);
        if (idToName.isEmpty())
            return;

        juce::Component::SafePointer<QuickWorkflowRoleCard> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options()
                               .withTargetComponent(this)
                               .withTargetScreenArea(getScreenBounds()),
            [safeThis, idToName](int result)
            {
                auto* card = safeThis.getComponent();
                if (card == nullptr || result <= 0 || result > idToName.size())
                    return;

                const int requested = (card->tab_ == QuickWorkflowTab::QuickRoute)
                                    ? card->count_
                                    : juce::jmax(1, card->count_);
                if (card->onCreateVariant
                    && card->onCreateVariant(card->role_, idToName[result - 1], requested))
                {
                    card->count_ = 0;
                    card->notifyCountChanged();
                }
            });
    }

    void notifyCountChanged()
    {
        repaint();
        if (onCountChanged)
            onCountChanged();
    }

    QuickWorkflowCore& core_;
    const QuickTrackRole& role_;
    QuickWorkflowTab tab_;
    QuickWorkflowColorButton colorButton_;
    QuickTrackMinusButton minusButton_;
    QuickTrackPlusButton plusButton_;
    QuickWorkflowTrashButton trashButton_;
    QuickWorkflowFlatButton createButton_;
    juce::Rectangle<int> countBounds_;
    juce::Rectangle<int> variantArrowBounds_;
    bool hasVariants_ = false;
    int count_ = 0;
};

class QuickWorkflowRoleList final : public juce::Component
{
public:
    explicit QuickWorkflowRoleList(QuickWorkflowCore& core) : core_(core) {}

    void setRoles(const std::vector<const QuickTrackRole*>& roles, QuickWorkflowTab tab)
    {
        removeAllChildren();
        cards_.clear();
        tab_ = tab;

        for (const auto* role : roles)
        {
            if (role == nullptr)
                continue;
            auto card = std::make_unique<QuickWorkflowRoleCard>(core_, *role, tab);
            card->onCreate = [this](const QuickTrackRole& r, int count)
            {
                return onCreate ? onCreate(r, count) : false;
            };
            card->onCreateVariant = [this](const QuickTrackRole& r, const juce::String& name, int count)
            {
                return onCreateVariant ? onCreateVariant(r, name, count) : false;
            };
            card->onColorRequest = [this](const QuickTrackRole& r, juce::Rectangle<int> bounds)
            {
                if (onColorRequest)
                    onColorRequest(r, bounds);
            };
            card->onCountChanged = [this]
            {
                if (onCountChanged)
                    onCountChanged();
            };
            addAndMakeVisible(*card);
            cards_.push_back(std::move(card));
        }
        rebuildLayout();
    }

    void resized() override { rebuildLayout(); }

    void paint(juce::Graphics& g) override
    {
        auto& a = Theme::getInstance().apex;
        g.fillAll(a.color.panelC);
        int y = kPad;
        juce::String lastSection;
        for (const auto& card : cards_)
        {
            if (card == nullptr)
                continue;
            const juce::String section(card->getRole().section);
            if (section != lastSection)
            {
                lastSection = section;
                g.setColour(a.color.textMuted);
                g.setFont(Theme::getInstance().fonts.bold.withHeight(10.0f));
                g.drawText(section == "quick_buses" ? "BUSES" : "TRACKS",
                           kPad, y, getWidth() - 2 * kPad, kSectionH,
                           juce::Justification::centredLeft);
                y += kSectionH;
            }
            y += kCardH + kGap;
        }
    }

    QuickWorkflowRoleCard* getCardForRoleId(const juce::String& roleId) const
    {
        for (const auto& card : cards_)
            if (card != nullptr && roleId == card->getRole().roleId)
                return card.get();
        return nullptr;
    }

    int getNumCards() const noexcept { return (int) cards_.size(); }

    int totalQueued() const noexcept
    {
        int total = 0;
        for (const auto& card : cards_)
            if (card != nullptr)
                total += card->getCount();
        return total;
    }

    std::vector<QuickWorkflowRoleCard*> getCards() const
    {
        std::vector<QuickWorkflowRoleCard*> result;
        for (const auto& card : cards_)
            if (card != nullptr)
                result.push_back(card.get());
        return result;
    }

    std::function<bool(const QuickTrackRole&, int)> onCreate;
    std::function<bool(const QuickTrackRole&, const juce::String&, int)> onCreateVariant;
    std::function<void(const QuickTrackRole&, juce::Rectangle<int>)> onColorRequest;
    std::function<void()> onCountChanged;

    static constexpr int kPad = 10;
    static constexpr int kSectionH = 24;
    static constexpr int kCardH = 48;
    static constexpr int kGap = 6;

private:
    void rebuildLayout()
    {
        int y = kPad;
        juce::String lastSection;
        for (auto& card : cards_)
        {
            if (card == nullptr)
                continue;
            const juce::String section(card->getRole().section);
            if (section != lastSection)
            {
                lastSection = section;
                y += kSectionH;
            }
            card->setBounds(kPad, y, juce::jmax(280, getWidth() - 2 * kPad), kCardH);
            y += kCardH + kGap;
        }
        setSize(juce::jmax(1, getWidth()), juce::jmax(1, y + kPad));
    }

    QuickWorkflowCore& core_;
    QuickWorkflowTab tab_ = QuickWorkflowTab::QuickTrack;
    std::vector<std::unique_ptr<QuickWorkflowRoleCard>> cards_;
};

class QuickWorkflowSendCard final : public juce::Component
{
public:
    QuickWorkflowSendCard(QuickWorkflowCore& core,
                          const TrackID& sourceId,
                          const QuickWorkflowSendDestination& destination)
        : core_(core), sourceId_(sourceId), destination_(destination),
          prePostButton_("POST"), addButton_("ADD SEND"), removeButton_()
    {
        removeButton_.setTooltip("Remove Send");
        slider_.setSliderStyle(juce::Slider::LinearHorizontal);
        slider_.setRange(0.0, 2.0, 0.01);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.onValueChange = [this]
        {
            if (destination_.exists)
                applyMutation(
                    [this]
                    {
                        return core_.setSendLevel(sourceId_, destination_.trackId,
                                                   (float) slider_.getValue());
                    },
                    "Update Quick Send Level");
        };
        prePostButton_.onClick = [this]
        {
            const bool changed = destination_.exists && applyMutation(
                [this]
                {
                    return core_.setSendPreFader(sourceId_, destination_.trackId,
                                                 !destination_.preFader);
                },
                "Change Quick Send Pre/Post");
            if (changed && onChanged)
                onChanged();
        };
        addButton_.onClick = [this]
        {
            const bool changed = applyMutation(
                [this]
                {
                    return core_.addSend(sourceId_, destination_.trackId,
                                         (float) slider_.getValue(),
                                         prePostButton_.getText() == "PRE");
                },
                destination_.exists ? "Update Quick Send" : "Add Quick Send");
            if (changed && onChanged)
                onChanged();
        };
        removeButton_.onClick = [this]
        {
            const bool changed = applyMutation(
                [this]
                {
                    return core_.removeSend(sourceId_, destination_.trackId);
                },
                "Remove Quick Send");
            if (changed && onChanged)
                onChanged();
        };

        addAndMakeVisible(slider_);
        addAndMakeVisible(prePostButton_);
        addAndMakeVisible(addButton_);
        addAndMakeVisible(removeButton_);
        refresh(destination);
    }

    void refresh(const QuickWorkflowSendDestination& destination)
    {
        destination_ = destination;
        slider_.setValue(destination_.exists ? destination_.level : 1.0,
                         juce::dontSendNotification);
        slider_.setEnabled(destination_.exists);
        prePostButton_.setButtonText(destination_.preFader ? "PRE" : "POST");
        prePostButton_.setEnabled(destination_.exists);
        removeButton_.setVisible(destination_.exists);
        addButton_.setText(destination_.exists ? "UPDATE" : "ADD SEND");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto& a = Theme::getInstance().apex;
        g.setColour(a.color.panelB);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 7.0f);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 7.0f, 1.0f);
        g.setColour(a.color.textPrimary);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(12.0f));
        g.drawText(destination_.name, 10, 0, 150, getHeight(), juce::Justification::centredLeft);
        if (destination_.exists && !destination_.active)
        {
            g.setColour(a.color.textMuted);
            g.setFont(Theme::getInstance().fonts.small);
            g.drawText("OFF", 145, 0, 28, getHeight(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        const int y = 10;
        const int h = juce::jmax(24, getHeight() - 20);
        const int right = getWidth() - 8;
        removeButton_.setBounds(right - 28, y, 28, h);
        addButton_.setBounds(removeButton_.getX() - 82, y, 78, h);
        prePostButton_.setBounds(addButton_.getX() - 52, y, 48, h);
        slider_.setBounds(170, y, juce::jmax(40, prePostButton_.getX() - 178), h);
    }

    juce::Button& getAddButton() noexcept { return addButton_; }
    juce::Button& getPrePostButton() noexcept { return prePostButton_; }

    std::function<bool(std::function<bool()>, juce::String)> onMutation;
    std::function<void()> onChanged;

private:
    bool applyMutation(std::function<bool()> mutation, const juce::String& description)
    {
        if (!mutation)
            return false;
        return onMutation ? onMutation(std::move(mutation), description) : mutation();
    }

    QuickWorkflowCore& core_;
    TrackID sourceId_;
    QuickWorkflowSendDestination destination_;
    juce::Slider slider_;
    QuickWorkflowFlatButton prePostButton_;
    QuickWorkflowFlatButton addButton_;
    QuickTrackClearButton removeButton_;
};

class QuickWorkflowSendList final : public juce::Component
{
public:
    QuickWorkflowSendList(QuickWorkflowCore& core, const TrackID& sourceId)
        : core_(core), sourceId_(sourceId)
    {
        rebuild();
    }

    void setSource(const TrackID& sourceId)
    {
        sourceId_ = sourceId;
        rebuild();
    }

    void resized() override { rebuildLayout(); }

    void paint(juce::Graphics& g) override
    {
        auto& a = Theme::getInstance().apex;
        g.fillAll(a.color.panelC);
        if (cards_.empty())
        {
            g.setColour(a.color.textMuted);
            g.setFont(Theme::getInstance().fonts.regular.withHeight(12.0f));
            g.drawText(sourceId_.isEmpty() ? "Select a source track" : "No buses available",
                       getLocalBounds(), juce::Justification::centred);
        }
    }

    QuickWorkflowSendCard* getCardForTarget(const TrackID& targetId) const
    {
        for (const auto& card : cards_)
            if (card != nullptr && card->getName() == targetId)
                return card.get();
        return nullptr;
    }

    void rebuild()
    {
        removeAllChildren();
        cards_.clear();
        for (const auto& destination : core_.getSendDestinations(sourceId_))
        {
            auto card = std::make_unique<QuickWorkflowSendCard>(core_, sourceId_, destination);
            card->setName(destination.trackId);
            card->onMutation = [this](std::function<bool()> mutation,
                                      juce::String description)
            {
                if (onMutation)
                    return onMutation(std::move(mutation), std::move(description));
                return mutation ? mutation() : false;
            };
            card->onChanged = [this]
            {
                // Routing mutation republishes the graph synchronously, but
                // rebuilding the child list from inside a Button callback can
                // destroy the sender before JUCE finishes dispatching it.
                // Defer the refresh and guard the component lifetime.
                juce::Component::SafePointer<QuickWorkflowSendList> safeThis(this);
                juce::MessageManager::callAsync([safeThis]
                {
                    if (safeThis == nullptr)
                        return;
                    safeThis->rebuild();
                    if (safeThis->onChanged)
                        safeThis->onChanged();
                });
            };
            addAndMakeVisible(*card);
            cards_.push_back(std::move(card));
        }
        rebuildLayout();
        repaint();
    }

    std::function<void()> onChanged;
    std::function<bool(std::function<bool()>, juce::String)> onMutation;

private:
    void rebuildLayout()
    {
        int y = 10;
        for (auto& card : cards_)
        {
            if (card == nullptr)
                continue;
            card->setBounds(10, y, juce::jmax(280, getWidth() - 20), 48);
            y += 54;
        }
        setSize(juce::jmax(1, getWidth()), juce::jmax(1, y + 10));
    }

    QuickWorkflowCore& core_;
    TrackID sourceId_;
    std::vector<std::unique_ptr<QuickWorkflowSendCard>> cards_;
};

class QuickWorkflowPopup final : public juce::Component
{
public:
    QuickWorkflowPopup(QuickWorkflowCore& core, const TrackID& sourceId = {})
        : core_(core), sourceId_(sourceId), roleList_(core), sendList_(core, sourceId),
          trackTab_("QUICK TRACK"), routeTab_("QUICK ROUTE"), sendTab_("QUICK SEND"),
          footerAction_("CREATE ALL")
    {
        setOpaque(true);
        setSize(kWidth, kHeight);

        addAndMakeVisible(trackTab_);
        addAndMakeVisible(routeTab_);
        // Quick Send remains part of the underlying routing implementation but
        // is intentionally not a visible Quick Workflow surface.
        sendTab_.setVisible(false);
        addAndMakeVisible(closeButton_);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(footerAction_);

        trackTab_.onClick = [this] { setTab(QuickWorkflowTab::QuickTrack); };
        routeTab_.onClick = [this] { setTab(QuickWorkflowTab::QuickRoute); };
        sendTab_.onClick = [this]
        {
            if (kQuickSendWorkflowUiEnabled)
                setTab(QuickWorkflowTab::QuickSend);
        };
        closeButton_.onClick = [this]
        {
            if (onCloseRequested)
                onCloseRequested();
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                callout->setVisible(false);
        };
        footerAction_.onClick = [this] { createAll(); };

        roleList_.onCreate = [this](const QuickTrackRole& role, int count)
        {
            return applyRole(role, count);
        };
        roleList_.onCreateVariant = [this](const QuickTrackRole& role, const juce::String& name, int count)
        {
            return applyRoleVariant(role, name, count);
        };
        roleList_.onColorRequest = [this](const QuickTrackRole& role, juce::Rectangle<int> bounds)
        {
            showColorPicker(role, bounds);
        };
        roleList_.onCountChanged = [this] { footerAction_.repaint(); };
        sendList_.onChanged = [this]
        {
            if (onChanged)
                onChanged();
        };
        sendList_.onMutation = [this](std::function<bool()> mutation,
                                      juce::String description)
        {
            if (onSendMutation)
                return onSendMutation(std::move(mutation), std::move(description));
            return mutation ? mutation() : false;
        };

        setTab(QuickWorkflowTab::QuickTrack);
    }

    ~QuickWorkflowPopup() override
    {
        viewport_.setViewedComponent(nullptr, false);
    }

    void setSendSource(const TrackID& sourceId)
    {
        sourceId_ = sourceId;
        sendList_.setSource(sourceId_);
        if (tab_ == QuickWorkflowTab::QuickSend)
            repaint();
    }

    QuickWorkflowTab getTab() const noexcept { return tab_; }
    QuickWorkflowRoleList& getRoleList() noexcept { return roleList_; }
    QuickWorkflowSendList& getSendList() noexcept { return sendList_; }
    QuickWorkflowTabButton& getTrackTabButton() noexcept { return trackTab_; }
    QuickWorkflowTabButton& getRouteTabButton() noexcept { return routeTab_; }
    QuickWorkflowTabButton& getSendTabButton() noexcept { return sendTab_; }
    juce::Button& getFooterActionButton() noexcept { return footerAction_; }

    void showTab(QuickWorkflowTab tab)
    {
        // Keep the shared-shell API usable for compatibility/tests while the
        // Quick Send tab remains hidden from the user-facing surface.
        setTab(tab);
    }

    std::function<QuickTrackBatchResult(QuickWorkflowTab, std::vector<QuickWorkflowRequest>)> onApply;
    std::function<bool(std::function<bool()>, juce::String)> onSendMutation;
    std::function<void()> onChanged;
    std::function<void()> onCloseRequested;
    std::function<void()> onColorPickerRequested;

    static constexpr int kWidth = 540;
    static constexpr int kHeight = 620;

    void paint(juce::Graphics& g) override
    {
        auto& a = Theme::getInstance().apex;
        g.fillAll(a.color.panelC);
        g.setColour(a.color.panelB);
        g.fillRect(0, 0, getWidth(), kHeaderH);
        g.setColour(a.color.borderSoftA);
        g.drawHorizontalLine(kHeaderH - 1, 0.0f, (float) getWidth());
        g.setColour(a.color.magenta);
        g.fillRoundedRectangle(12.0f, 10.0f, 28.0f, 28.0f, 5.0f);
        g.setColour(juce::Colours::white);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(15.0f));
        g.drawText("Q", 12, 10, 28, 28, juce::Justification::centred);
        g.setColour(a.color.textPrimary);
        g.setFont(Theme::getInstance().fonts.bold.withHeight(13.0f));
        g.drawText(tab_ == QuickWorkflowTab::QuickSend ? sendHeaderText() : "QUICK WORKFLOW",
                   50, 0, getWidth() - 105, kHeaderH, juce::Justification::centredLeft);
    }

    void resized() override
    {
        closeButton_.setBounds(getWidth() - 40, 9, 30, 30);
        const int tabY = kHeaderH + 6;
        const int tabW = (getWidth() - 26) / 2;
        trackTab_.setBounds(10, tabY, tabW, 32);
        routeTab_.setBounds(trackTab_.getRight() + 6, tabY, tabW, 32);
        sendTab_.setBounds({});
        sendTab_.setVisible(false);

        footerAction_.setVisible(true);
        footerAction_.setBounds(10, getHeight() - kFooterH + 10, getWidth() - 20, 38);
        viewport_.setBounds(0, tabY + 40, getWidth(),
                            juce::jmax(0, getHeight() - (tabY + 40) - kFooterH));
        viewport_.setViewedComponent(tab_ == QuickWorkflowTab::QuickSend
                                      ? static_cast<juce::Component*>(&sendList_)
                                      : static_cast<juce::Component*>(&roleList_), false);
        if (auto* viewed = viewport_.getViewedComponent())
            viewed->setSize(viewport_.getMaximumVisibleWidth(), viewed->getHeight());
    }

private:
    void setTab(QuickWorkflowTab tab)
    {
        tab_ = tab;
        trackTab_.setActive(tab_ == QuickWorkflowTab::QuickTrack);
        routeTab_.setActive(tab_ == QuickWorkflowTab::QuickRoute);
        sendTab_.setActive(tab_ == QuickWorkflowTab::QuickSend);

        if (tab_ == QuickWorkflowTab::QuickSend)
        {
            sendList_.setSource(sourceId_);
            footerAction_.setVisible(false);
        }
        else
        {
            std::vector<const QuickTrackRole*> roles;
            if (tab_ == QuickWorkflowTab::QuickTrack)
            {
                for (const auto& role : QuickWorkflowRoleCatalog::trackRoles()) roles.push_back(&role);
                for (const auto& role : QuickWorkflowRoleCatalog::busRoles()) roles.push_back(&role);
            }
            else
                for (const auto& role : QuickWorkflowRoleCatalog::allRoles()) roles.push_back(role);
            roleList_.setRoles(roles, tab_);
            footerAction_.setVisible(true);
            footerAction_.setText(tab_ == QuickWorkflowTab::QuickRoute
                                  ? "CREATE + ROUTE ALL" : "CREATE ALL");
        }
        resized();
        repaint();
    }

    bool applyRole(const QuickTrackRole& role, int count)
    {
        std::vector<QuickWorkflowRequest> requests { { &role, count } };
        auto result = onApply ? onApply(tab_, requests) : core_.apply(tab_, requests);
        if (!result.ok())
            return false;
        if (result.ok())
        {
            if (result.count() > 0 || !result.routePairs.empty())
                if (onChanged)
                    onChanged();
            return true;
        }
        return false;
    }

    bool applyRoleVariant(const QuickTrackRole& role, const juce::String& name, int count)
    {
        QuickWorkflowRequest request;
        request.role = &role;
        request.count = count;
        request.nameOverride = name;

        std::vector<QuickWorkflowRequest> requests { request };
        auto result = onApply ? onApply(tab_, requests) : core_.apply(tab_, requests);
        if (!result.ok())
            return false;

        if (result.count() > 0 || !result.routePairs.empty())
            if (onChanged)
                onChanged();
        return true;
    }

    void createAll()
    {
        std::vector<QuickWorkflowRequest> requests;
        for (auto* card : roleList_.getCards())
        {
            if (card == nullptr)
                continue;
            if (tab_ == QuickWorkflowTab::QuickTrack && card->getCount() <= 0)
                continue;
            requests.push_back({ &card->getRole(), card->getCount() });
        }
        if (requests.empty())
            return;

        auto result = onApply ? onApply(tab_, requests) : core_.apply(tab_, requests);
        if (result.ok())
        {
            for (auto* card : roleList_.getCards())
                if (card != nullptr)
                    card->setCount(0);
            if (result.count() > 0 || !result.routePairs.empty())
                if (onChanged)
                    onChanged();
        }
    }

    void showColorPicker(const QuickTrackRole& role, const juce::Rectangle<int>& anchor)
    {
        if (onColorPickerRequested)
            onColorPickerRequested();
        if (anchor.isEmpty())
            return;
        juce::CallOutBox::launchAsynchronously(
            std::make_unique<QuickTrackRoleColorPicker>(core_.builder(), role), anchor, nullptr);
    }

    juce::String sendHeaderText() const
    {
        const auto sourceName = core_.getTrackName(sourceId_);
        if (sourceName.isEmpty())
            return "QUICK SEND - No Track Selected";
        return "QUICK SEND - FROM " + sourceName;
    }

    QuickWorkflowCore& core_;
    TrackID sourceId_;
    QuickWorkflowTab tab_ = QuickWorkflowTab::QuickTrack;
    QuickWorkflowRoleList roleList_;
    QuickWorkflowSendList sendList_;
    juce::Viewport viewport_;
    QuickWorkflowTabButton trackTab_;
    QuickWorkflowTabButton routeTab_;
    QuickWorkflowTabButton sendTab_;
    QuickWorkflowCloseButton closeButton_;
    QuickWorkflowFlatButton footerAction_;

    static constexpr int kHeaderH = 48;
    static constexpr int kFooterH = 58;
};

} // namespace DAW

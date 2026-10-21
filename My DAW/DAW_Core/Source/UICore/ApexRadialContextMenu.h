#pragma once
#include <JuceHeader.h>
#include "ApexContextMenuStyle.h"
#include "ApexRadialGeometry.h"
#include <functional>
#include <cstddef>
#include <vector>
#include <cmath>

namespace DAW
{
/**
 * APEX FLOW / RADIAL — non-RT native menu adapter.
 *
 * Uses existing juce::PopupMenu items, their canonical actions, their
 * submenus, and their item IDs (no alternate edit engine). A root/submenu
 * holds at most 7 actions and a More wedge if paginated. The centre is a
 * dedicated BACK button, or CLOSE at the root. No menu is ever allocated
 * or painted from the audio callback.
 *
 * NOTE: JUCE customComponent/customCallback/commandManager menus require
 * the native popup path, because a radial can't faithfully embed controls.
 * A single fallback is safer than silently dropping or misfiring actions.
 */
class ApexRadialContextMenu final : public juce::Component, private juce::Timer
{
public:
    using Completion = std::function<void(int)>;
    static constexpr int kPageSize = 7;

    static void show(juce::PopupMenu menu, juce::Component* target,
                     juce::Point<int> globalScreenPos, Completion completion = {})
    {
        auto* top = target != nullptr ? target->getTopLevelComponent() : nullptr;
        if (ApexContextMenuStyle::get().getMode() == ApexContextMenuStyle::Mode::Classic
            || top == nullptr || hasUnsupportedItems(menu))
        {
            auto options = juce::PopupMenu::Options()
                .withTargetScreenArea({globalScreenPos.x, globalScreenPos.y, 1, 1});
            if (target != nullptr)
                options = options.withTargetComponent(target);
            menu.showMenuAsync(options, std::move(completion));
            return;
        }

        // Never leave stacked input-blocking overlays behind.
        for (int i = top->getNumChildComponents() - 1; i >= 0; --i)
            if (auto* old = dynamic_cast<ApexRadialContextMenu*>(top->getChildComponent(i)))
                old->closeMenu();

        auto* overlay = new ApexRadialContextMenu(std::move(menu),
                                                   std::move(completion));
        top->addAndMakeVisible(overlay);
        overlay->setBounds(top->getLocalBounds());
        overlay->anchor_ = top->getLocalPoint(nullptr, globalScreenPos);
        overlay->computeGeometry();
        overlay->toFront(true);
        overlay->grabKeyboardFocus();
        overlay->startTimerHz(60); // stops itself after entrance glow settles
    }

    void resized() override { computeGeometry(); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::backspaceKey)
        {
            backOrClose();
            return true;
        }
        return false;
    }

    void paint(juce::Graphics& g) override
    {
        auto items = pageItems();
        const int slots = (int) items.size() + (hasMore() ? 1 : 0);
        if (slots <= 0) return;

        const auto centre = centre_.toFloat();
        const float inner = radius_ * 0.34f;
        constexpr float pi = juce::MathConstants<float>::pi;
        const float step = 2.0f * pi / (float) slots;

        // Lightweight opening animation; no entire-screen alpha compositing.
        const float appear = juce::jlimit(0.0f, 1.0f, age_ / 0.15f);
        const float scale = 0.95f + 0.05f * appear;
        const float outer = radius_ * scale;

        g.setColour(juce::Colour(0xff0c1421).withAlpha(0.88f * appear));
        g.fillEllipse(centre.x - outer - 5.0f, centre.y - outer - 5.0f,
                      2.0f * (outer + 5.0f), 2.0f * (outer + 5.0f));

        for (int i = 0; i < slots; ++i)
        {
            const bool more = i == (int) items.size();
            const auto* item = more ? nullptr : items[(size_t) i];
            const bool enabled = more || (item != nullptr && item->isEnabled);
            const bool selected = hovered_ == i;
            const float a0 = -pi / 2.0f + i * step + 0.012f;
            const float a1 = -pi / 2.0f + (i + 1) * step - 0.012f;

            juce::Path sector;
            const auto point = [&](float r, float a)
            {
                return juce::Point<float>(centre.x + r * std::cos(a),
                                          centre.y + r * std::sin(a));
            };
            sector.startNewSubPath(point(inner + 2.0f, a0));
            for (int j = 0; j <= 12; ++j)
                sector.lineTo(point(outer, a0 + (a1 - a0) * (float) j / 12.0f));
            for (int j = 12; j >= 0; --j)
                sector.lineTo(point(inner + 2.0f,
                                a0 + (a1 - a0) * (float) j / 12.0f));
            sector.closeSubPath();

            const auto dark = juce::Colour(0xff18273b);
            const auto bright = juce::Colour(0xff64e5da);
            g.setColour((selected ? bright.withAlpha(0.30f) :
                         dark.withAlpha(enabled ? 0.97f : 0.42f))
                            .withMultipliedAlpha(appear));
            g.fillPath(sector);
            g.setColour((selected ? bright : juce::Colour(0xff42647b))
                            .withAlpha(appear * (selected ? 0.95f : 0.5f)));
            g.strokePath(sector, juce::PathStrokeType(selected ? 2.2f : 1.0f));

            const float mid = (a0 + a1) / 2.0f;
            const auto pos = point((inner + outer) * 0.53f, mid);
            const float box = juce::jlimit(46.0f, 96.0f, radius_ * 0.59f);
            juce::String label = more ? "MORE >" : item->text;
            if (!more && item->subMenu != nullptr) label << " >";
            if (!more && item->isTicked) label = juce::String::charToString((juce::juce_wchar) 0x2713) + " " + label;
            g.setColour(juce::Colours::white.withAlpha(
                appear * (enabled ? 0.96f : 0.38f)));
            g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 13.0f, radius_ * 0.085f))));
            g.drawFittedText(label, juce::Rectangle<int>(
                (int) (pos.x - box * 0.5f), (int) (pos.y - 22.0f),
                (int) box, 44), juce::Justification::centred, 2, 0.7f);
        }

        g.setColour(juce::Colour(0xff07121d).withAlpha(appear));
        g.fillEllipse(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
        g.setColour(juce::Colour(0xff67ebd7).withAlpha(appear * 0.95f));
        g.drawEllipse(centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f, 2.0f);
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 13.0f, inner * 0.35f))));
        g.drawFittedText(levels_.size() > 1 ? "BACK" : "CLOSE",
                         juce::Rectangle<int>((int)(centre.x - inner),
                                              (int)(centre.y - inner * 0.55f),
                                              (int)(inner * 2.0f), (int)inner),
                         juce::Justification::centred, 1);
        // Breadcrumb is informative and never steals a wedge/gesture.
        if (levels_.size() > 1)
        {
            g.setColour(juce::Colour(0xffb7e1e4).withAlpha(appear));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawFittedText(levels_.back().title,
                juce::Rectangle<int>((int)(centre.x - radius_), (int)(centre.y + radius_ + 5),
                                     (int)(radius_ * 2), 18),
                juce::Justification::centred, 1);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        updateHovered(e.getPosition());
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        updateHovered(e.getPosition());
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        pressed_ = true;
        down_ = e.getPosition();
        updateHovered(down_);
        const auto delta = down_.toFloat() - centre_.toFloat();
        if (delta.getDistanceFromOrigin() > radius_ + 9.0f)
        {
            closeMenu(); // outside tap dismisses, never clicks through
            return;
        }
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!pressed_) return;
        pressed_ = false;
        const auto pos = e.getPosition();
        const float inner = radius_ * 0.34f;
        if (ApexRadialGeometry::isCentre(pos.x, pos.y, centre_.x, centre_.y, inner))
        {
            backOrClose();
            return;
        }
        const auto items = pageItems();
        const int slots = (int) items.size() + (hasMore() ? 1 : 0);
        const int index = ApexRadialGeometry::sectorAt(pos.x, pos.y,
                        centre_.x, centre_.y, inner, radius_, slots);
        if (index < 0) return; // safe neutral zone
        if (index >= (int) items.size())
        {
            ++levels_.back().page;
            hovered_ = -1;
            repaintWheel();
            return;
        }
        const auto* item = items[(size_t) index];
        if (item == nullptr || ! item->isEnabled) return;
        if (item->subMenu != nullptr)
        {
            levels_.push_back({item->subMenu.get(), 0, item->text,
                               collectItems(*item->subMenu)});
            hovered_ = -1;
            repaintWheel();
            return;
        }

        // Return the canonical ID/action, not a forked command path.
        auto action = item->action;
        const auto id = item->itemID;
        auto completion = std::move(completion_);
        closeMenu(false);
        if (action) action();
        else if (completion && id > 0) completion(id);
    }

private:
    struct Level
    {
        const juce::PopupMenu* menu;
        int page;
        juce::String title;
        std::vector<const juce::PopupMenu::Item*> items;
    };
    explicit ApexRadialContextMenu(juce::PopupMenu menu, Completion completion)
        : menu_(std::move(menu)), completion_(std::move(completion))
    {
        levels_.push_back({ &menu_, 0, "APEX", collectItems(menu_) });
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(true, true);
        setOpaque(false);
    }
    static bool hasUnsupportedItems(const juce::PopupMenu& menu)
    {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
        {
            const auto& item = it.getItem();
            if (item.customComponent != nullptr || item.customCallback != nullptr ||
                item.commandManager != nullptr)
                return true;
            if (item.subMenu != nullptr && hasUnsupportedItems(*item.subMenu))
                return true;
        }
        return false;
    }
    static std::vector<const juce::PopupMenu::Item*> collectItems(
        const juce::PopupMenu& source)
    {
        std::vector<const juce::PopupMenu::Item*> found;
        found.reserve((size_t) juce::jmax(0, source.getNumItems()));
        juce::PopupMenu::MenuItemIterator it(source);
        while (it.next())
        {
            const auto& item = it.getItem();
            if (!item.isSeparator && !item.isSectionHeader &&
                (item.subMenu != nullptr || item.action != nullptr || item.itemID != 0))
                found.push_back(&item);
        }
        return found;
    }
    std::vector<const juce::PopupMenu::Item*> pageItems() const
    {
        const auto& all = levels_.back().items;
        const size_t first = (size_t) levels_.back().page * kPageSize;
        if (first >= all.size()) return {};
        return {all.begin() + (std::ptrdiff_t) first,
                all.begin() + (std::ptrdiff_t) juce::jmin(all.size(), first + kPageSize)};
    }
    bool hasMore() const
    {
        return levels_.back().items.size() > (size_t)(levels_.back().page + 1) * kPageSize;
    }
    void computeGeometry()
    {
        // Constrain the full wheel to the available parent viewport; on
        // a compact phone the same control adapts rather than clipping.
        radius_ = juce::jlimit(58.0f, 172.0f,
                       juce::jmin(getWidth(), getHeight()) * 0.43f);
        const float margin = radius_ + 10.0f;
        centre_ = { juce::jlimit((int)margin, juce::jmax((int)margin, getWidth() - (int)margin), anchor_.x),
                    juce::jlimit((int)margin, juce::jmax((int)margin, getHeight() - (int)margin), anchor_.y) };
        repaintWheel();
    }
    void repaintWheel()
    {
        // Never invalidate the entire top-level DAW window for hover or
        // the 180ms opening animation; only the actual pie paint bounds.
        const int r = (int) std::ceil(radius_) + 40;
        repaint(centre_.x - r, centre_.y - r, r * 2, r * 2);
    }

    void updateHovered(juce::Point<int> pos)
    {
        const auto items = pageItems();
        const int slots = (int)items.size() + (hasMore() ? 1 : 0);
        const int next = ApexRadialGeometry::sectorAt(pos.x, pos.y, centre_.x,
                        centre_.y, radius_ * 0.34f, radius_, slots);
        if (next != hovered_)
        {
            hovered_ = next;
            repaintWheel();
        }
    }
    void backOrClose()
    {
        if (levels_.back().page > 0)
        {
            --levels_.back().page;
            hovered_ = -1;
            repaintWheel();
        }
        else if (levels_.size() > 1)
        {
            levels_.pop_back();
            hovered_ = -1;
            repaintWheel();
        }
        else closeMenu();
    }
    void closeMenu(bool notifyDismiss = true)
    {
        stopTimer();
        // JUCE's showMenuAsync always completes with 0 on cancellation.
        // Preserve that contract: mixer/browser components clear their
        // "menu open" state even when the user taps outside or presses Esc.
        auto completion = std::move(completion_);
        if (auto* parent = getParentComponent())
            parent->removeChildComponent(this);
        juce::Component::SafePointer<ApexRadialContextMenu> safe(this);
        juce::MessageManager::callAsync([safe]() mutable
        {
            if (safe != nullptr) delete safe.getComponent();
        });
        if (notifyDismiss && completion) completion(0);
    }
    void timerCallback() override
    {
        age_ += 1.0f / 60.0f;
        repaintWheel();
        if (age_ >= 0.18f) stopTimer();
    }

    juce::PopupMenu menu_;
    Completion completion_;
    std::vector<Level> levels_;
    juce::Point<int> anchor_;
    juce::Point<int> centre_;
    juce::Point<int> down_;
    float radius_ = 150.0f;
    float age_ = 0.0f;
    int hovered_ = -1;
    bool pressed_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ApexRadialContextMenu)
};
} // namespace DAW

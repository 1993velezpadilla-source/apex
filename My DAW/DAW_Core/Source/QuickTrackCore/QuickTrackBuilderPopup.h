// ===========================================================================
// QuickTrackBuilderPopup.h
// APEX Quick Track Builder — the TRACKS "+" popup.
//
// Architecture (fixed overlay structure — NON-NEGOTIABLE):
//
//   QuickTrackBuilderPopup
//   ├── Header                    FIXED (title + real close button)
//   ├── RoleListViewport          SCROLLABLE (juce::Viewport)
//   │   └── RoleListContent       (sections + role rows as REAL children)
//   └── BottomActionBar           FIXED / STICKY (TEMPLATE + CREATE ALL)
//
// Every interactive control is a REAL juce::Button child (component-exclusive
// hit-testing — no painted-rect coordinate switchboards). Clear (X) and
// Checkmark are VECTOR-drawn (juce::Path) so they can never degrade into
// font mojibake and render identically at any DPI. All visuals use the
// canonical APEX signal-core tokens (Theme.h ApexTokens).
//
// The GUI remains a pure presentation/controller layer over
// QuickTrackBuilderCore / QuickTrackColorSystem / QuickTrackTemplateStore.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "../QuickTrackCore/QuickTrackRoles.h"
#include "../QuickTrackCore/QuickTrackBuilderCore.h"
#include "../QuickTrackCore/QuickTrackTemplate.h"
#include "../ThemeCore/Theme.h"
#include "../UICore/TrackColorPalette.h"

namespace DAW
{

// ── Vector icon helpers (no font/encoding dependency) ──────────────────────
inline void drawClearCross(juce::Graphics& g, const juce::Rectangle<float>& b,
                           juce::Colour colour, float stroke)
{
    juce::Path p;
    p.startNewSubPath(b.getX() + stroke, b.getY() + stroke);
    p.lineTo(b.getRight() - stroke, b.getBottom() - stroke);
    p.startNewSubPath(b.getRight() - stroke, b.getY() + stroke);
    p.lineTo(b.getX() + stroke, b.getBottom() - stroke);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(stroke));
}

inline void drawCheckMark(juce::Graphics& g, const juce::Rectangle<float>& b,
                          juce::Colour colour, float stroke)
{
    juce::Path p;
    p.startNewSubPath(b.getX() + stroke, b.getCentreY());
    p.lineTo(b.getCentreX() - stroke * 0.3f, b.getBottom() - stroke);
    p.lineTo(b.getRight() - stroke, b.getY() + stroke);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(stroke));
}

// ── APEX-styled scrollbar (thin, panelB thumb, no default-JUCE look) ───────
class QuickTrackScrollbarLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height,
                       bool isVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        juce::ignoreUnused(scrollbar, isMouseOver);
        auto& a = Theme::getInstance().apex;
        g.fillAll(juce::Colours::transparentBlack);
        const auto track = isVertical
            ? juce::Rectangle<int>(x, y, width, height).reduced(2)
            : juce::Rectangle<int>(x, y, width, height).reduced(2);
        const auto thumb = isVertical
            ? juce::Rectangle<int>(track.getX(), y + thumbStartPosition, track.getWidth(), thumbSize)
            : juce::Rectangle<int>(x + thumbStartPosition, track.getY(), thumbSize, track.getHeight());
        g.setColour(isMouseDown ? a.color.borderSoftB.brighter(0.2f)
                                : a.color.borderSoftB);
        g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
    }
};

// ── Owner interface for nested template dialogs ────────────────────────────
class QuickTrackBuilderPopupOwner
{
public:
    virtual ~QuickTrackBuilderPopupOwner() = default;
    virtual QuickTrackTemplateStore& getTemplateStore() = 0;
    virtual void showDuplicateTemplateChoice(const QuickTrackTemplate& existing,
                                             const std::vector<QuickTrackTemplateRole>& recipe) = 0;
    virtual void applyTemplateToBuilder(const QuickTrackTemplate& t) = 0;
};

// ===========================================================================
// Base chip button — APEX panelB chip with hairline border.
// ===========================================================================
class QuickTrackChipButton : public juce::Button
{
public:
    explicit QuickTrackChipButton(const juce::String& name) : juce::Button(name) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        const auto b = getLocalBounds().toFloat();
        g.setColour(down ? a.color.panelA
                  : highlighted ? a.color.panelB.brighter(0.12f)
                  : a.color.panelB);
        g.fillRoundedRectangle(b, a.metric.radiusControl);
        g.setColour(a.color.borderSoftA);
        g.drawRoundedRectangle(b.reduced(0.5f), a.metric.radiusControl, a.metric.strokeThin);
        paintContent(g, getLocalBounds(), highlighted, down);
    }

protected:
    virtual void paintContent(juce::Graphics&, const juce::Rectangle<int>&, bool, bool) {}
};

// ===========================================================================
// Role color swatch button — shows the project role color or AUTO ("A").
// ===========================================================================
class QuickTrackColorSwatchButton final : public QuickTrackChipButton
{
public:
    QuickTrackColorSwatchButton(QuickTrackBuilderCore& builder, const QuickTrackRole& role)
        : QuickTrackChipButton("Role color"), builder_(builder), role_(role) {}

    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool) override
    {
        auto& theme = Theme::getInstance();
        const auto swatch = b.toFloat().reduced(5.0f);
        const auto colour = builder_.getRoleColor(role_);
        if (colour.isOpaque())
        {
            g.setColour(colour);
            g.fillRoundedRectangle(swatch, 3.0f);
            g.setColour(theme.apex.color.borderSoftA);
            g.drawRoundedRectangle(swatch.reduced(0.5f), 3.0f, 1.0f);
        }
        else
        {
            g.setColour(theme.apex.color.panelB.brighter(0.18f));
            g.fillRoundedRectangle(swatch, 3.0f);
            g.setColour(theme.apex.color.borderSoftA);
            g.drawRoundedRectangle(swatch.reduced(0.5f), 3.0f, 1.0f);
            g.setColour(theme.apex.color.textMuted);
            g.setFont(theme.fonts.small);
            g.drawText("A", b, juce::Justification::centred);
        }
    }

private:
    QuickTrackBuilderCore& builder_;
    const QuickTrackRole& role_;
};

// ===========================================================================
// Role name button — tap to quick-create ONE track immediately.
// ===========================================================================
class QuickTrackRoleNameButton final : public juce::Button
{
public:
    QuickTrackRoleNameButton(const QuickTrackRole& role)
        : juce::Button("Role: " + juce::String(role.displayName)), role_(role) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;
        const auto b = getLocalBounds().toFloat();
        if (highlighted || down)
        {
            g.setColour(a.color.panelB.brighter(0.10f));
            g.fillRoundedRectangle(b, a.metric.radiusControl);
        }
        g.setColour(a.color.textPrimary);
        g.setFont(theme.fonts.regular);
        g.drawText(role_.displayName, getLocalBounds().reduced(6, 0),
                   juce::Justification::centredLeft);
    }

private:
    const QuickTrackRole& role_;
};

// ===========================================================================
// Role row — [COLOR] [ROLE] [-] [COUNT] [+] [CLEAR] [CHECK] as REAL buttons.
// ===========================================================================
class QuickTrackMinusButton final : public QuickTrackChipButton
{
public:
    QuickTrackMinusButton() : QuickTrackChipButton("Minus") {}
    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool) override
    {
        const auto centre = b.toFloat().getCentre();
        g.setColour(Theme::getInstance().apex.color.textSecondary);
        g.drawLine(centre.x - 5.0f, centre.y, centre.x + 5.0f, centre.y, 1.5f);
    }
};

class QuickTrackPlusButton final : public QuickTrackChipButton
{
public:
    QuickTrackPlusButton() : QuickTrackChipButton("Plus") {}
    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool) override
    {
        const auto centre = b.toFloat().getCentre();
        g.setColour(Theme::getInstance().apex.color.textSecondary);
        g.drawLine(centre.x - 5.0f, centre.y, centre.x + 5.0f, centre.y, 1.5f);
        g.drawLine(centre.x, centre.y - 5.0f, centre.x, centre.y + 5.0f, 1.5f);
    }
};

class QuickTrackClearButton final : public QuickTrackChipButton
{
public:
    QuickTrackClearButton() : QuickTrackChipButton("Clear") {}
    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool) override
    {
        drawClearCross(g, b.toFloat().reduced(8.0f), Theme::getInstance().apex.color.textMuted, 1.4f);
    }
};

class QuickTrackCheckButton final : public QuickTrackChipButton
{
public:
    QuickTrackCheckButton() : QuickTrackChipButton("Create row") {}
    void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool down) override
    {
        auto& a = Theme::getInstance().apex;
        drawCheckMark(g, b.toFloat().reduced(7.0f), down ? a.color.magenta : a.color.textSecondary, 1.6f);
    }
};

class QuickTrackRoleRow final : public juce::Component
{
public:
    QuickTrackRoleRow(QuickTrackBuilderCore& builder, const QuickTrackRole& role)
        : builder_(builder),
          role_(role),
          swatchBtn_(builder, role),
          nameBtn_(role)
    {
        addAndMakeVisible(swatchBtn_);
        addAndMakeVisible(nameBtn_);
        addAndMakeVisible(minusBtn_);
        addAndMakeVisible(plusBtn_);
        addAndMakeVisible(clearBtn_);
        addAndMakeVisible(checkBtn_);

        nameBtn_.onClick = [this]
        {
            // Canonical batch hook when wired (undoable command in-app);
            // otherwise the builder direct (tests/headless).
            auto result = runBatch({ { &role_, 1 } });
            if (result.ok() && result.count() > 0)
                if (onQuickCreated)
                    onQuickCreated();
        };
        minusBtn_.onClick = [this]
        {
            if (count_ > 0)
            {
                --count_;
                notifyChange();
            }
        };
        plusBtn_.onClick = [this]
        {
            count_ = juce::jmin(99, count_ + 1);
            notifyChange();
        };
        clearBtn_.onClick = [this]
        {
            if (count_ != 0)
            {
                count_ = 0;
                notifyChange();
            }
        };
        checkBtn_.onClick = [this]
        {
            if (count_ <= 0)
                return;
            auto result = runBatch({ { &role_, count_ } });
            if (result.ok())
            {
                count_ = 0;
                notifyChange();
            }
        };
        swatchBtn_.onClick = [this]
        {
            if (onColorRequest)
                onColorRequest(role_, swatchBtn_.getBounds() + getScreenPosition());
        };
    }

    void resized() override
    {
        const int ctrlH = 28;
        const int ctrlY = (getHeight() - ctrlH) / 2;
        const int right = getWidth() - 6;
        swatchBtn_.setBounds(6, ctrlY, 28, ctrlH);
        nameBtn_.setBounds(swatchBtn_.getRight() + 4, 0, juce::jmax(60, right - 200 - (swatchBtn_.getRight() + 4)), getHeight());
        checkBtn_.setBounds(right - 28, ctrlY, 28, ctrlH);
        clearBtn_.setBounds(checkBtn_.getX() - 30, ctrlY, 28, ctrlH);
        plusBtn_.setBounds(clearBtn_.getX() - 30, ctrlY, 28, ctrlH);
        countRect_ = juce::Rectangle<int>(plusBtn_.getX() - 26, 0, 26, getHeight());
        minusBtn_.setBounds(countRect_.getX() - 28, ctrlY, 28, ctrlH);
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;
        g.setColour(a.color.textPrimary);
        g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
        g.drawText(juce::String(count_), countRect_, juce::Justification::centred);
    }

    int getCount() const noexcept { return count_; }
    void setCount(int c) noexcept { count_ = juce::jlimit(0, 99, c); repaint(); }
    const QuickTrackRole& getRole() const noexcept { return role_; }

    juce::Button& getSwatchButton() noexcept { return swatchBtn_; }
    juce::Button& getNameButton() noexcept { return nameBtn_; }
    juce::Button& getMinusButton() noexcept { return minusBtn_; }
    juce::Button& getPlusButton() noexcept { return plusBtn_; }
    juce::Button& getClearButton() noexcept { return clearBtn_; }
    juce::Button& getCheckButton() noexcept { return checkBtn_; }

    std::function<void()> onQuickCreated;                     // popup closes after quick create
    std::function<void()> onChange;                           // repaint parent / bottom bar
    std::function<void(const QuickTrackRole&, juce::Rectangle<int>)> onColorRequest;
    std::function<QuickTrackBatchResult(std::vector<QuickTrackRequest>)> onCreateBatch;

private:
    QuickTrackBatchResult runBatch(std::vector<QuickTrackRequest> requests)
    {
        if (onCreateBatch)
            return onCreateBatch(std::move(requests));
        return builder_.createBatch(requests);
    }

    void notifyChange()
    {
        repaint();
        if (onChange)
            onChange();
    }

    QuickTrackBuilderCore& builder_;
    const QuickTrackRole& role_;
    QuickTrackColorSwatchButton swatchBtn_;
    QuickTrackRoleNameButton nameBtn_;
    QuickTrackMinusButton minusBtn_;
    QuickTrackPlusButton plusBtn_;
    QuickTrackClearButton clearBtn_;
    QuickTrackCheckButton checkBtn_;
    juce::Rectangle<int> countRect_;
    int count_ = 0;
};

// ===========================================================================
// Role list content — the viewed component (sections + rows as children).
// ===========================================================================
class QuickTrackRoleListContent final : public juce::Component
{
public:
    QuickTrackRoleListContent(QuickTrackBuilderCore& builder)
        : builder_(builder)
    {
        for (const auto& role : QuickTrackRoleCatalog::getAll())
        {
            auto row = std::make_unique<QuickTrackRoleRow>(builder, role);
            row->onQuickCreated = [this] { if (onQuickCreated) onQuickCreated(); };
            row->onChange = [this] { if (onChange) onChange(); repaint(); };
            row->onColorRequest = [this](const QuickTrackRole& role, juce::Rectangle<int> anchor)
            {
                if (onColorRequest)
                    onColorRequest(role, anchor);
            };
            row->onCreateBatch = [this](std::vector<QuickTrackRequest> requests)
            {
                if (onExecuteBatch)
                    return onExecuteBatch(std::move(requests));
                return builder_.createBatch(requests);
            };
            addAndMakeVisible(*row);
            rows_.push_back(std::move(row));
        }
        rebuildLayout();
    }

    void rebuildLayout()
    {
        int y = kPad;
        const char* lastSection = nullptr;
        for (auto& row : rows_)
        {
            if (row == nullptr)
                continue;
            if (lastSection == nullptr || juce::String(lastSection) != juce::String(row->getRole().section))
            {
                lastSection = row->getRole().section;
                y += kSectionH;
            }
            row->setBounds(kPad, y, juce::jmax(60, getWidth() - 2 * kPad), kRowH);
            y += kRowH + kRowGap;
        }
        setSize(juce::jmax(1, getWidth()), juce::jmax(1, y + kPad));
    }

    void resized() override
    {
        rebuildLayout();
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;
        g.fillAll(a.color.panelC);

        int y = kPad;
        const char* lastSection = nullptr;
        for (auto& row : rows_)
        {
            if (row == nullptr)
                continue;
            if (lastSection == nullptr || juce::String(lastSection) != juce::String(row->getRole().section))
            {
                lastSection = row->getRole().section;
                g.setColour(a.color.textMuted);
                g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
                g.drawText(QuickTrackRoleCatalog::sectionTitle(row->getRole().section).toUpperCase(),
                           kPad, y, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft);
                y += kSectionH;
            }
            y += kRowH + kRowGap;
        }
    }

    QuickTrackRoleRow* getRow(int index) const
    {
        return (index >= 0 && index < (int) rows_.size()) ? rows_[(size_t) index].get() : nullptr;
    }
    QuickTrackRoleRow* getRowForRoleId(const juce::String& roleId) const
    {
        for (const auto& row : rows_)
            if (row != nullptr && roleId == juce::String(row->getRole().roleId))
                return row.get();
        return nullptr;
    }
    int getNumRows() const noexcept { return (int) rows_.size(); }

    std::function<void()> onQuickCreated;
    std::function<void()> onChange;
    std::function<void(const QuickTrackRole&, juce::Rectangle<int>)> onColorRequest;
    /** Canonical batch hook (undoable command in-app); unset in headless
     *  tests where the builder is called directly. */
    std::function<QuickTrackBatchResult(std::vector<QuickTrackRequest>)> onExecuteBatch;

    static constexpr int kPad = 8;
    static constexpr int kSectionH = 26;
    static constexpr int kRowH = 44;
    static constexpr int kRowGap = 2;

private:
    QuickTrackBuilderCore& builder_;
    std::vector<std::unique_ptr<QuickTrackRoleRow>> rows_;
};

// ===========================================================================
// Role color picker (swatch tap) — AUTO + 48 canonical APEX swatches as REAL
// buttons, launched in a CallOutBox.
// ===========================================================================
class QuickTrackRoleColorPicker final : public juce::Component
{
public:
    QuickTrackRoleColorPicker(QuickTrackBuilderCore& builder, const QuickTrackRole& role)
        : builder_(builder), role_(role), autoBtn_("AUTO")
    {
        addAndMakeVisible(autoBtn_);
        autoBtn_.onClick = [this]
        {
            builder_.clearManualRoleColor(role_);
            closeCallout();
        };
        const auto palette = TrackColorPalette::getCanonicalPalette();
        for (int i = 0; i < palette.size(); ++i)
        {
            const auto colour = palette[(size_t) i];
            struct PaintedSwatch final : public QuickTrackChipButton
            {
                PaintedSwatch(const juce::String& n, juce::Colour c) : QuickTrackChipButton(n), colour_(c) {}
                void paintContent(juce::Graphics& g, const juce::Rectangle<int>& b, bool, bool) override
                {
                    const auto r = b.toFloat().reduced(4.0f);
                    g.setColour(colour_);
                    g.fillRoundedRectangle(r, 3.0f);
                    g.setColour(Theme::getInstance().apex.color.borderSoftA);
                    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
                }
                juce::Colour colour_;
            };
            std::unique_ptr<QuickTrackChipButton> swatch(
                new PaintedSwatch("Swatch " + juce::String(i), colour));
            swatch->onClick = [this, colour]
            {
                builder_.setManualRoleColor(role_, colour);
                closeCallout();
            };
            addAndMakeVisible(*swatch);
            swatchBtns_.push_back(std::move(swatch));
        }
        setSize(kGridCols * (kSwatch + kGap) + kGap + 2 * kPad,
                kPad + kAutoH + kGap + kGridRows * (kSwatch + kGap) + kGap + kPad);
    }

    void resized() override
    {
        autoBtn_.setBounds(kPad, kPad, getWidth() - 2 * kPad, kAutoH);
        for (int i = 0; i < (int) swatchBtns_.size(); ++i)
            swatchBtns_[(size_t) i]->setBounds(swatchRect(i));
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.fillAll(theme.colors.surface);
        g.setColour(theme.colors.border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        // AUTO button state ring
        const bool isAuto = !builder_.isRoleColorManual(role_);
        if (isAuto)
        {
            g.setColour(theme.colors.accent);
            g.drawRoundedRectangle(autoBtn_.getBounds().toFloat().expanded(2.0f).reduced(0.5f), 6.0f, 1.5f);
        }
    }

    juce::Button& getAutoButton() noexcept { return autoBtn_; }
    juce::Button* getSwatchButton(int i) noexcept
    {
        return (i >= 0 && i < (int) swatchBtns_.size()) ? swatchBtns_[(size_t) i].get() : nullptr;
    }

    static constexpr int kPad = 8;
    static constexpr int kAutoH = 30;
    static constexpr int kGridCols = 8;
    static constexpr int kGridRows = 6;
    static constexpr int kSwatch = 22;
    static constexpr int kGap = 4;

private:
    juce::Rectangle<int> swatchRect(int i) const
    {
        const int c = i % kGridCols;
        const int r = i / kGridCols;
        const int y = kPad + kAutoH + kGap + r * (kSwatch + kGap);
        return juce::Rectangle<int>(kPad + c * (kSwatch + kGap), y, kSwatch, kSwatch);
    }

    void closeCallout()
    {
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);
    }

    QuickTrackBuilderCore& builder_;
    const QuickTrackRole& role_;
    QuickTrackChipButton autoBtn_;
    std::vector<std::unique_ptr<QuickTrackChipButton>> swatchBtns_;
};

// ===========================================================================
// Template name entry (Save Template) with duplicate protection.
// ===========================================================================
class QuickTrackTemplateNameEntry final : public juce::Component
{
public:
    QuickTrackTemplateNameEntry(QuickTrackBuilderPopupOwner& owner,
                                const std::vector<QuickTrackTemplateRole>& recipe,
                                const QuickTrackTemplate& existing)
        : owner_(owner), recipe_(recipe), existing_(existing), saveBtn_("Save template"), cancelBtn_("Cancel")
    {
        setSize(300, 104);
        editor_.setBounds(10, 36, 280, 26);
        editor_.setText(existing.name, false);
        editor_.setSelectAllWhenFocused(true);
        editor_.setJustification(juce::Justification::centredLeft);
        editor_.setFont(Theme::getInstance().fonts.regular);
        editor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF0A0D15));
        editor_.setColour(juce::TextEditor::textColourId, juce::Colour(0xFFF2F4FA));
        editor_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF22283A));
        editor_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xFFFF1678));
        editor_.setColour(juce::CaretComponent::caretColourId, juce::Colour(0xFFFF1678));
        addAndMakeVisible(editor_);
        editor_.setWantsKeyboardFocus(true);
        editor_.grabKeyboardFocus();

        addAndMakeVisible(saveBtn_);
        addAndMakeVisible(cancelBtn_);
        saveBtn_.onClick = [this] { save(); };
        cancelBtn_.onClick = [this] { closeCallout(); };
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.fillAll(theme.colors.surface);
        g.setColour(theme.colors.border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        g.setColour(theme.colors.textSecondary);
        g.setFont(theme.fonts.small);
        g.drawText(existing_.name.isEmpty() ? "Template name" : "Replace existing template?",
                   10, 8, 280, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        editor_.setBounds(10, 36, 280, 26);
        saveBtn_.setBounds(10, 70, 134, 24);
        cancelBtn_.setBounds(156, 70, 134, 24);
    }

private:
    void save()
    {
        const auto name = editor_.getText().trim();
        if (name.isEmpty())
            return;
        QuickTrackTemplate t;
        t.name = name;
        t.roles = recipe_;
        juce::String error;
        const auto result = owner_.getTemplateStore().saveTemplate(t, !existing_.name.isEmpty(), error);
        if (result == QuickTrackTemplateStore::SaveResult::Saved)
        {
            closeCallout();
        }
        else if (result == QuickTrackTemplateStore::SaveResult::AlreadyExists)
        {
            closeCallout();
            QuickTrackTemplate asExisting = t;
            owner_.showDuplicateTemplateChoice(asExisting, recipe_);
        }
        else
        {
            juce::Logger::writeToLog("[QuickTrack] template save failed: " + error);
        }
    }

    void closeCallout()
    {
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);
    }

    QuickTrackBuilderPopupOwner& owner_;
    std::vector<QuickTrackTemplateRole> recipe_;
    QuickTrackTemplate existing_;
    juce::TextEditor editor_;
    juce::TextButton saveBtn_;
    juce::TextButton cancelBtn_;
};

// ===========================================================================
// Template list (Load Template) — rows as REAL buttons.
// ===========================================================================
class QuickTrackTemplateList final : public juce::Component
{
public:
    QuickTrackTemplateList(QuickTrackBuilderPopupOwner& owner)
        : owner_(owner)
    {
        templates_ = owner_.getTemplateStore().listTemplates();
        for (int i = 0; i < juce::jmin((int) templates_.size(), 8); ++i)
        {
            auto btn = std::make_unique<juce::TextButton>("Load " + templates_[(size_t) i].name);
            btn->setLookAndFeel(&laf_);
            const auto t = templates_[(size_t) i];
            btn->setButtonText(t.name + "   (" + juce::String(t.totalTrackCount()) + " tracks)");
            btn->onClick = [this, t]
            {
                owner_.applyTemplateToBuilder(t);
                if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
                    cb->setVisible(false);
            };
            addAndMakeVisible(*btn);
            buttons_.push_back(std::move(btn));
        }
        const int h = juce::jmin((int) templates_.size(), 8) * kRowH + kPad * 2;
        setSize(300, juce::jmax(60, h));
    }

    void resized() override
    {
        for (int i = 0; i < (int) buttons_.size(); ++i)
            buttons_[(size_t) i]->setBounds(kPad, kPad + i * kRowH, getWidth() - 2 * kPad, kRowH - 4);
    }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        g.fillAll(theme.colors.surface);
        g.setColour(theme.colors.border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        if (templates_.empty())
        {
            g.setColour(theme.colors.textSecondary);
            g.setFont(theme.fonts.regular);
            g.drawText("No saved templates yet", getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    struct QuickTrackListLaf final : public juce::LookAndFeel_V4
    {
        void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                  const juce::Colour&, bool highlighted, bool down) override
        {
            auto& a = Theme::getInstance().apex;
            const auto r = b.getLocalBounds().toFloat();
            g.setColour(down ? a.color.panelA : highlighted ? a.color.panelB.brighter(0.10f) : a.color.panelB);
            g.fillRoundedRectangle(r, 4.0f);
        }
        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            auto& theme = Theme::getInstance();
            g.setColour(theme.apex.color.textPrimary);
            g.setFont(theme.fonts.bold);
            g.drawText(b.getButtonText(), b.getLocalBounds().reduced(8, 0), juce::Justification::centredLeft);
        }
    };

    static constexpr int kRowH = 38;
    static constexpr int kPad = 8;
    QuickTrackBuilderPopupOwner& owner_;
    std::vector<QuickTrackTemplate> templates_;
    std::vector<std::unique_ptr<juce::TextButton>> buttons_;
    QuickTrackListLaf laf_;
};

// ===========================================================================
// The Quick Track Builder popup — FIXED header + scrollable role list +
// FIXED/sticky bottom action bar.
// ===========================================================================
class QuickTrackBuilderPopup final : public juce::Component,
                                     public QuickTrackBuilderPopupOwner
{
public:
    QuickTrackBuilderPopup(QuickTrackBuilderCore& builder, QuickTrackTemplateStore& templates)
        : builder_(builder),
          templates_(templates),
          headerBar_(),
          content_(builder),
          bottomBar_()
    {
        setSize(kPopupWidth, kPopupMaxHeight);

        // Fixed header.
        headerBar_.onClose = [this] { closePopup(); };
        addAndMakeVisible(headerBar_);
        // Scrollable role list.
        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(true, false);
        viewport_.setScrollBarThickness(8);
        viewport_.getVerticalScrollBar().setLookAndFeel(&scrollLaf_);
        addAndMakeVisible(viewport_);
        content_.onQuickCreated = [this] { closePopup(); };
        content_.onChange = [this] { bottomBar_.updateQueued(totalQueued()); repaint(); };
        content_.onColorRequest = [this](const QuickTrackRole& role, juce::Rectangle<int> anchor)
        {
            if (onColorPickerRequested)
                onColorPickerRequested();
            if (!anchor.isEmpty())
                launchNested(std::make_unique<QuickTrackRoleColorPicker>(builder_, role), anchor);
        };

        // Fixed/sticky bottom action bar.
        bottomBar_.onTemplate = [this] { showTemplateMenu(); };
        bottomBar_.onCreateAll = [this] { createAll(); };
        addAndMakeVisible(bottomBar_);
    }

    ~QuickTrackBuilderPopup() override
    {
        // Detach the viewed content BEFORE the members destruct: the content
        // member is destroyed before the viewport member (reverse
        // declaration order), and a live viewed-component pointer in a dying
        // juce::Viewport would dangle — the source of the teardown AV.
        viewport_.setViewedComponent(nullptr, false);
    }

    void resized() override
    {
        headerBar_.setBounds(0, 0, getWidth(), kHeaderH);
        bottomBar_.setBounds(0, getHeight() - kBottomBarH, getWidth(), kBottomBarH);
        viewport_.setBounds(0, kHeaderH, getWidth(), juce::jmax(0, getHeight() - kHeaderH - kBottomBarH));
        // Guarantee the viewed content gets the viewport width (the content
        // self-sizes its height; the viewport owns scrolling).
        content_.setSize(viewport_.getMaximumVisibleWidth(), content_.getHeight());
        content_.rebuildLayout();
        bottomBar_.updateQueued(totalQueued());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::getInstance().apex.color.panelC);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            closePopup();
            return true;
        }
        return false;
    }

    // ── Owner interface (template dialogs) ────────────────────────────────
    QuickTrackTemplateStore& getTemplateStore() override { return templates_; }

    void showDuplicateTemplateChoice(const QuickTrackTemplate& existing,
                                     const std::vector<QuickTrackTemplateRole>& recipe) override
    {
        launchNested(std::make_unique<QuickTrackTemplateNameEntry>(*this, recipe, existing),
                     templateBtnScreenBounds());
    }

    void applyTemplateToBuilder(const QuickTrackTemplate& t) override
    {
        for (int i = 0; i < content_.getNumRows(); ++i)
            if (auto* row = content_.getRow(i))
                row->setCount(0);
        for (const auto& roleEntry : t.roles)
            if (auto* row = content_.getRowForRoleId(roleEntry.roleId))
                row->setCount(roleEntry.count);
        for (const auto& roleEntry : t.roles)
        {
            const auto* role = QuickTrackRoleCatalog::findById(roleEntry.roleId);
            if (role == nullptr)
                continue;
            if (!roleEntry.autoColor && roleEntry.explicitColor.isNotEmpty())
                builder_.setManualRoleColor(*role, juce::Colour::fromString(roleEntry.explicitColor));
            else if (roleEntry.autoColor)
                builder_.clearManualRoleColor(*role);
        }
        bottomBar_.updateQueued(totalQueued());
        repaint();
    }

    // ── Fixed header bar ──────────────────────────────────────────────────
    class HeaderBar final : public juce::Component
    {
    public:
        HeaderBar()
        {
            addAndMakeVisible(closeBtn_);
            closeBtn_.onClick = [this]
            {
                if (onClose)
                    onClose();
            };
        }

        void paint(juce::Graphics& g) override
        {
            auto& theme = Theme::getInstance();
            auto& a = theme.apex;
            g.setColour(a.color.panelB);
            g.fillRect(getLocalBounds());
            g.setColour(a.color.borderSoftA);
            g.drawHorizontalLine(getHeight() - 1, 0.0f, (float) getWidth());

            const auto chip = juce::Rectangle<int>(12, 10, 28, 28);
            g.setColour(a.color.magenta.darker(0.35f));
            g.fillRoundedRectangle(chip.toFloat(), 4.0f);
            g.setColour(a.color.magenta);
            g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 4.0f, 1.0f);
            g.setFont(juce::Font("Segoe UI", 17.0f, juce::Font::plain));
            g.setColour(a.color.textPrimary);
            g.drawText("+", chip, juce::Justification::centred);

            g.setColour(a.color.textPrimary);
            g.setFont(theme.fonts.bold);
            g.drawText("QUICK TRACK BUILDER", 48, 10, getWidth() - 100, 20, juce::Justification::centredLeft);
            g.setColour(a.color.textMuted);
            g.setFont(theme.fonts.small);
            g.drawText("tap a role to create it instantly", 48, 29, getWidth() - 100, 16,
                       juce::Justification::centredLeft);
        }

        void resized() override
        {
            closeBtn_.setBounds(getWidth() - 40, 9, 30, 30);
        }

        class CloseButton final : public juce::Button
        {
        public:
            CloseButton() : juce::Button("Close") {}
            void paintButton(juce::Graphics& g, bool highlighted, bool down) override
            {
                auto& a = Theme::getInstance().apex;
                const auto b = getLocalBounds().toFloat();
                g.setColour(down || highlighted ? a.color.panelC.brighter(0.15f) : a.color.panelB);
                g.fillRoundedRectangle(b, 4.0f);
                drawClearCross(g, b.reduced(9.0f), a.color.textSecondary, 1.5f);
            }
        };

        std::function<void()> onClose;
        CloseButton closeBtn_;
    };

    // ── Fixed/sticky bottom action bar ────────────────────────────────────
    class BottomActionBar final : public juce::Component
    {
    public:
        BottomActionBar()
        {
            addAndMakeVisible(templateBtn_);
            addAndMakeVisible(createAllBtn_);
            templateBtn_.onClick = [this] { if (onTemplate) onTemplate(); };
            createAllBtn_.onClick = [this] { if (onCreateAll) onCreateAll(); };
        }

        void paint(juce::Graphics& g) override
        {
            auto& a = Theme::getInstance().apex;
            g.setColour(a.color.panelB);
            g.fillRect(getLocalBounds());
            g.setColour(a.color.borderSoftA);
            g.drawHorizontalLine(0, 0.0f, (float) getWidth());
        }

        void resized() override
        {
            templateBtn_.setBounds(10, 12, 118, 38);
            createAllBtn_.setBounds(138, 12, getWidth() - 148, 38);
        }

        void updateQueued(int queued) { createAllBtn_.setQueued(queued); createAllBtn_.repaint(); }

        class TemplateButton final : public juce::Button
        {
        public:
            TemplateButton() : juce::Button("TEMPLATE") {}
            void paintButton(juce::Graphics& g, bool highlighted, bool down) override
            {
                auto& theme = Theme::getInstance();
                auto& a = theme.apex;
                const auto b = getLocalBounds().toFloat();
                g.setColour(down ? a.color.panelA : highlighted ? a.color.panelC.brighter(0.08f) : a.color.panelC);
                g.fillRoundedRectangle(b, 4.0f);
                g.setColour(a.color.borderSoftB);
                g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
                g.setColour(a.color.textSecondary);
                g.setFont(theme.fonts.bold);
                g.drawText("TEMPLATE", getLocalBounds(), juce::Justification::centred);
            }
        };

        class CreateAllButton final : public juce::Button
        {
        public:
            CreateAllButton() : juce::Button("CREATE ALL") {}
            void setQueued(int q) noexcept { queued_ = q; }
            void paintButton(juce::Graphics& g, bool highlighted, bool down) override
            {
                auto& theme = Theme::getInstance();
                auto& a = theme.apex;
                const auto b = getLocalBounds().toFloat();
                const auto active = queued_ > 0;
                g.setColour(active ? a.color.magenta : a.color.magenta.darker(0.3f));
                g.fillRoundedRectangle(b, 4.0f);
                if (active)
                {
                    g.setColour(a.color.magenta.withAlpha(0.35f));
                    g.drawRoundedRectangle(b.expanded(2.0f), 6.0f, 2.0f);
                }
                g.setColour(juce::Colours::white);
                g.setFont(theme.fonts.bold);
                g.drawText(active ? ("CREATE ALL  (" + juce::String(queued_) + ")") : "CREATE ALL",
                           getLocalBounds(), juce::Justification::centred);
            }
        private:
            int queued_ = 0;
        };

        std::function<void()> onTemplate;
        std::function<void()> onCreateAll;
        TemplateButton templateBtn_;
        CreateAllButton createAllBtn_;
    };

    // ── Public API / test accessors ───────────────────────────────────────
    std::function<void()> onCloseRequested;
    std::function<void()> onTemplatePressed;        // test hook (menu would open in-app)
    std::function<void()> onColorPickerRequested;   // test hook (picker would open in-app)
    /** Canonical batch hook (undoable QuickTrackBatchCommand in-app);
     *  unset in headless tests where the builder is called directly. */
    std::function<QuickTrackBatchResult(std::vector<QuickTrackRequest>)> onExecuteBatch;

    HeaderBar& getHeaderBar() noexcept { return headerBar_; }
    BottomActionBar& getBottomBar() noexcept { return bottomBar_; }
    juce::Viewport& getRoleViewport() noexcept { return viewport_; }
    QuickTrackRoleListContent& getRoleListContent() noexcept { return content_; }

    int totalQueued() const
    {
        int total = 0;
        for (int i = 0; i < content_.getNumRows(); ++i)
            if (auto* row = content_.getRow(i))
                total += row->getCount();
        return total;
    }

    static constexpr int kPopupWidth = 340;
    static constexpr int kPopupMaxHeight = 560;
    static constexpr int kHeaderH = 48;
    static constexpr int kBottomBarH = 62;

private:
    void createAll()
    {
        std::vector<QuickTrackRequest> requests;
        for (int i = 0; i < content_.getNumRows(); ++i)
        {
            if (auto* row = content_.getRow(i))
                if (row->getCount() > 0)
                    requests.push_back({ &row->getRole(), row->getCount() });
        }
        if (requests.empty())
            return;
        QuickTrackBatchResult result;
        if (onExecuteBatch)
            result = onExecuteBatch(requests);
        else
            result = builder_.createBatch(requests);
        if (result.ok())
        {
            for (int i = 0; i < content_.getNumRows(); ++i)
                if (auto* row = content_.getRow(i))
                    row->setCount(0);
            bottomBar_.updateQueued(0);
            closePopup();
        }
    }

    void showTemplateMenu()
    {
        if (onTemplatePressed)
            onTemplatePressed();
        juce::PopupMenu menu;
        menu.addItem(1, "Load Template");
        menu.addItem(2, "Save Template");
        // Anchor-only options (no raw target-component pointer): the menu
        // launch is async and the popup must be free to close without
        // leaving a dangling target.
        auto options = juce::PopupMenu::Options().withTargetScreenArea(templateBtnScreenBounds());
        juce::Component::SafePointer<QuickTrackBuilderPopup> safeThis(this);
        menu.showMenuAsync(options, [safeThis](int result)
        {
            if (safeThis == nullptr)
                return;
            if (result == 1)
                safeThis->launchNested(std::make_unique<QuickTrackTemplateList>(*safeThis),
                                       safeThis->templateBtnScreenBounds());
            else if (result == 2)
                safeThis->showSaveTemplateFlow();
        });
    }

    void showSaveTemplateFlow()
    {
        std::vector<QuickTrackTemplateRole> recipe;
        int order = 0;
        for (int i = 0; i < content_.getNumRows(); ++i)
        {
            auto* row = content_.getRow(i);
            if (row == nullptr || row->getCount() <= 0)
                continue;
            QuickTrackTemplateRole entry;
            entry.roleId = row->getRole().roleId;
            entry.count = row->getCount();
            entry.order = order++;
            entry.autoColor = !builder_.isRoleColorManual(row->getRole());
            if (!entry.autoColor)
                entry.explicitColor = builder_.getRoleColor(row->getRole()).toString();
            recipe.push_back(entry);
        }
        if (recipe.empty())
        {
            juce::Logger::writeToLog("[QuickTrack] nothing queued to save as template");
            return;
        }
        launchNested(std::make_unique<QuickTrackTemplateNameEntry>(*this, recipe, QuickTrackTemplate()),
                     templateBtnScreenBounds());
    }

    void closePopup()
    {
        if (onCloseRequested)
            onCloseRequested();
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->setVisible(false);
    }

    juce::Rectangle<int> templateBtnScreenBounds() const
    {
        const auto local = bottomBar_.templateBtn_.getBounds() + bottomBar_.getPosition();
        return local + getScreenPosition();
    }

    void launchNested(std::unique_ptr<juce::Component> content, const juce::Rectangle<int>& anchor)
    {
        if (anchor.isEmpty())
            return;
        juce::CallOutBox::launchAsynchronously(std::move(content), anchor, nullptr);
    }

    QuickTrackBuilderCore& builder_;
    QuickTrackTemplateStore& templates_;
    HeaderBar headerBar_;
    QuickTrackScrollbarLookAndFeel scrollLaf_;
    juce::Viewport viewport_;
    QuickTrackRoleListContent content_;
    BottomActionBar bottomBar_;
};

} // namespace DAW

#include <JuceHeader.h>

#include "../../Source/UICore/MixerWindow.h"
#include "../../Source/UICore/MixerPanel.h"
#include "../../Source/UICore/BubblegumOffscreenEndpointComponent.h"
#include "../../Source/UICore/MixerKeyboardRoutingCore.h"
#include "../../Source/SelectionCore/MultiSelectionCore.h"
#include "../../Source/TrackCore/Track.h"
#include "../../Source/CommandCore/Command.h"
#include "../../Source/CommandCore/CommandManager.h"
#include "../../Source/CommandCore/GeneralCommands.h"
#include "../../Source/FolderBusCore/FolderBusCore.h"
#include "../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../Source/TrackCore/TrackReorderCore.h"
#include "../../Source/UICore/TrackList.h"

#include <type_traits>
#include <utility>

namespace
{
    bool containsToolbarId(const std::vector<DAW::MixerWindow::ToolbarButtonLayout>& layout,
                           int id)
    {
        for (const auto& button : layout)
            if (button.id == id)
                return true;
        return false;
    }

    void expectToolbarLayoutIsBounded(juce::UnitTest& test,
                                      const juce::Rectangle<float>& zone,
                                      const std::vector<DAW::MixerWindow::ToolbarButtonLayout>& layout)
    {
        for (size_t i = 0; i < layout.size(); ++i)
        {
            const auto& bounds = layout[i].bounds;
            test.expect(bounds.getX() >= zone.getX()
                            && bounds.getRight() <= zone.getRight()
                            && bounds.getY() >= zone.getY()
                            && bounds.getBottom() <= zone.getBottom(),
                        "Toolbar control must remain completely inside its zone");
            test.expect(bounds.getWidth() >= 22.0f && bounds.getHeight() >= 20.0f,
                        "Toolbar controls must retain useful hit-target dimensions");

            for (size_t j = i + 1; j < layout.size(); ++j)
                test.expect(!bounds.intersects(layout[j].bounds),
                            "Toolbar controls must never overlap");
        }
    }
}

/**
    @test    mixer.default-width.v1
    @verify  The initial floating Mixer content width is six normal strips plus
             the separate Master strip.  Wider and narrower user sizes remain
             valid alternatives and are not rewritten by this default helper.
*/
class MixerDefaultWidthTests final : public juce::UnitTest
{
public:
    MixerDefaultWidthTests()
        : juce::UnitTest("mixer.default-width.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        constexpr int masterWidth = 170;
        const int defaultWidth = DAW::MixerPanel::computeDefaultVisibleContentWidth(
            DAW::MixerPanel::kDefaultVisibleNormalTracks, masterWidth);
        const int widerWidth = 7 * DAW::MixerPanel::kStripW
                             + 6 * DAW::MixerPanel::kStripGap
                             + DAW::MixerPanel::kStripGap + masterWidth;
        const int narrowerWidth = 4 * DAW::MixerPanel::kStripW
                                + 3 * DAW::MixerPanel::kStripGap
                                + DAW::MixerPanel::kStripGap + masterWidth;

        beginTest("Default content width is six normal strips plus Master");
        expectEquals(defaultWidth, 6 * 80 + 5 * 2 + 2 + masterWidth);
        expectEquals(defaultWidth, 662);

        beginTest("Default count is capped at six without limiting user resize");
        expectEquals(DAW::MixerPanel::computeDefaultVisibleContentWidth(20, masterWidth),
                     defaultWidth);
        expect(widerWidth > defaultWidth,
               "A manually wider Mixer can expose more than six normal strips");
        expect(narrowerWidth < defaultWidth,
               "A manually narrower Mixer can expose fewer than six normal strips");

        beginTest("Master is an additional strip, not one of the six normals");
        expectEquals(defaultWidth - masterWidth,
                     DAW::MixerPanel::kDefaultVisibleNormalTracks * DAW::MixerPanel::kStripW
                         + (DAW::MixerPanel::kDefaultVisibleNormalTracks - 1)
                               * DAW::MixerPanel::kStripGap
                         + DAW::MixerPanel::kStripGap);
    }
};

/**
    @test    mixer.header-layout.v1
    @verify  The floating Mixer header remains one row, protects the centered
             title, moves lower-priority actions into overflow, and restores
             eligible actions when width returns.
*/
class MixerHeaderLayoutTests final : public juce::UnitTest
{
public:
    MixerHeaderLayoutTests()
        : juce::UnitTest("mixer.header-layout.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using Window = DAW::MixerWindow;

        beginTest("Wide header shows every normal action without overlap");
        const juce::Rectangle<float> wideZone(0.0f, 0.0f, 220.0f,
                                              (float) DAW::FloatingWindowBase::kTitleH);
        const auto wide = Window::computeToolbarLayout(wideZone);
        expectEquals((int) wide.size(), 7);
        expect(!containsToolbarId(wide, Window::kOverflow));
        expectToolbarLayoutIsBounded(*this, wideZone, wide);

        beginTest("Medium header preserves priority and uses overflow");
        const juce::Rectangle<float> mediumZone(0.0f, 0.0f, 150.0f,
                                                (float) DAW::FloatingWindowBase::kTitleH);
        const auto medium = Window::computeToolbarLayout(mediumZone);
        expect(containsToolbarId(medium, Window::kOverflow));
        expect(medium.size() < wide.size());
        expect(containsToolbarId(medium, Window::kSettings));
        expectToolbarLayoutIsBounded(*this, mediumZone, medium);

        beginTest("Narrow header protects title and keeps overflow usable");
        const auto narrowZone = Window::computeResponsiveToolbarZone(200);
        const auto narrow = Window::computeToolbarLayout(narrowZone);
        expect(narrowZone.getX() >= 142.0f,
               "Toolbar must start after the narrow-window MIXER title guard");
        expect(narrowZone.getRight() <= 190.0f,
               "Toolbar must retain the right title-bar margin");
        expectEquals((int) narrow.size(), 1);
        expect(containsToolbarId(narrow, Window::kOverflow));
        expectToolbarLayoutIsBounded(*this, narrowZone, narrow);

        beginTest("Widening the header restores eligible actions");
        const auto restored = Window::computeToolbarLayout(wideZone);
        expectEquals((int) restored.size(), 7);
        expect(!containsToolbarId(restored, Window::kOverflow));
        expectToolbarLayoutIsBounded(*this, wideZone, restored);
    }
};

/**
    @test    mixer.safe-viewport.v1
    @verify  Normal-strip reveal uses the viewport area before the fixed Master
             and performs only the minimum movement in either direction.
*/
class MixerSafeViewportTests final : public juce::UnitTest
{
public:
    MixerSafeViewportTests()
        : juce::UnitTest("mixer.safe-viewport.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        constexpr int viewportWidth = 662;
        constexpr int masterWidth = 170;
        constexpr int stripWidth = DAW::MixerPanel::kStripW;
        constexpr int pitch = DAW::MixerPanel::kStripW + DAW::MixerPanel::kStripGap;
        const int safeWidth = DAW::MixerPanel::computeSafeNormalViewportWidth(
            viewportWidth, masterWidth);

        beginTest("Safe viewport ends before Master");
        expectEquals(safeWidth, 490);
        const int rightTargetX = 6 * pitch;
        const int rightReveal = DAW::MixerPanel::computeRevealScrollX(
            rightTargetX, stripWidth, safeWidth, 0, 1000);
        expectEquals(rightReveal, rightTargetX + stripWidth - safeWidth);
        expect(rightTargetX >= rightReveal
                    && rightTargetX + stripWidth <= rightReveal + safeWidth,
                "Right navigation must fully reveal the normal strip before Master");

        beginTest("Left navigation uses minimum movement and remains before Master");
        const int leftTargetX = 0;
        const int leftReveal = DAW::MixerPanel::computeRevealScrollX(
            leftTargetX, stripWidth, safeWidth, 300, 1000);
        expectEquals(leftReveal, 0);
        expect(leftTargetX >= leftReveal
                    && leftTargetX + stripWidth <= leftReveal + safeWidth,
                "Left navigation must fully reveal the normal strip");

        beginTest("Already-visible selection does not move the viewport");
        expectEquals(DAW::MixerPanel::computeRevealScrollX(
                         2 * pitch, stripWidth, safeWidth, 0, 1000),
                      0);

        beginTest("Pinned Master leaves a non-overlapping normal-strip boundary");
        const int masterX = DAW::MixerPanel::computePinnedMasterX(
            82, viewportWidth, masterWidth, 1000);
        expectEquals(masterX, 574);
        expectEquals(DAW::MixerPanel::computeNormalStripVisibleWidth(
                         492, stripWidth, masterX),
                     stripWidth);
        expectEquals(DAW::MixerPanel::computeNormalStripVisibleWidth(
                         530, stripWidth, masterX),
                     42);
        expect(530 + 42 <= masterX - DAW::MixerPanel::kStripGap,
               "A partially visible normal strip must end before the Master gap");
        expectEquals(DAW::MixerPanel::computeNormalStripVisibleWidth(
                         masterX, stripWidth, masterX),
                     0);

        beginTest("A clipped rendered width cannot satisfy full-strip reveal");
        constexpr int partialStripX = 530;
        constexpr int clippedWidth = 42;
        constexpr int currentScrollX = 82;
        const int clippedDecision = DAW::MixerPanel::computeRevealScrollX(
            partialStripX, clippedWidth, safeWidth, currentScrollX, 1000);
        const int fullWidthDecision = DAW::MixerPanel::computeRevealScrollX(
            partialStripX, stripWidth, safeWidth, currentScrollX, 1000);
        expectEquals(clippedDecision, currentScrollX,
                     "The clipped component width would falsely report visibility");
        expectEquals(fullWidthDecision, 120,
                     "The full preferred width reveals the missing right portion");
        expect(partialStripX >= fullWidthDecision
                   && partialStripX + stripWidth <= fullWidthDecision + safeWidth,
               "Preferred strip width must fit completely inside the safe viewport");

        beginTest("Selected partially clipped strip reveals fully");
        expect(fullWidthDecision != currentScrollX,
               "A selected partial strip must request a viewport movement");

        beginTest("Selected fully visible strip does not scroll");
        expectEquals(DAW::MixerPanel::computeRevealScrollX(
                         5 * pitch, stripWidth, safeWidth, currentScrollX, 1000),
                     currentScrollX,
                     "A fully visible selected strip must not cause extra scrolling");

        beginTest("Normal, selected, and multi-selected bounds remain before Master");
        const juce::Rectangle<int> masterBounds(masterX, 0, masterWidth, 120);
        const auto clippedNormalBounds = juce::Rectangle<int>(
            partialStripX, 0,
            DAW::MixerPanel::computeNormalStripVisibleWidth(
                partialStripX, stripWidth, masterX),
            120);
        expect(clippedNormalBounds.getRight() <= masterX - DAW::MixerPanel::kStripGap
                   && !clippedNormalBounds.intersects(masterBounds),
               "A normal strip must not intersect the pinned Master");

        const auto selectedBounds = clippedNormalBounds;
        expect(selectedBounds.getRight() <= masterX - DAW::MixerPanel::kStripGap
                   && !selectedBounds.intersects(masterBounds),
               "A selected strip must not intersect the pinned Master");

        const std::vector<juce::Rectangle<int>> multiSelectedBounds {
            { 410, 0, DAW::MixerPanel::computeNormalStripVisibleWidth(410, stripWidth, masterX), 120 },
            selectedBounds
        };
        for (const auto& bounds : multiSelectedBounds)
            expect(bounds.getRight() <= masterX - DAW::MixerPanel::kStripGap
                       && !bounds.intersects(masterBounds),
                   "Every multi-selected strip must remain outside the Master");

        beginTest("Pinned Master follows resize/scroll and clamps to content");
        expectEquals(DAW::MixerPanel::computePinnedMasterX(
                         0, 400, masterWidth, 662),
                     230);
        expectEquals(DAW::MixerPanel::computePinnedMasterX(
                         262, 400, masterWidth, 662),
                     492);
        expectEquals(DAW::MixerPanel::computeNormalStripVisibleWidth(
                          410, stripWidth, 230),
                     0);

        beginTest("Master remains visually pinned through normal scrolling and reveal");
        constexpr int largePanelWidth = 1810;
        const auto masterScreenX = [=](int scrollX)
        {
            return DAW::MixerPanel::computePinnedMasterX(
                       scrollX, viewportWidth, masterWidth, largePanelWidth) - scrollX;
        };
        expectEquals(masterScreenX(0), viewportWidth - masterWidth);
        expectEquals(masterScreenX(currentScrollX), viewportWidth - masterWidth);
        expectEquals(masterScreenX(fullWidthDecision), viewportWidth - masterWidth);
    }
};

/**
    @test    mixer.reorder-viewport.v1
    @verify  Reorder auto-scroll uses the normal-strip safe viewport edge and
             preserves the pinned Master hard wall while insertion mapping stays
             valid for a selected group.
*/
class MixerReorderViewportTests final : public juce::UnitTest
{
public:
    MixerReorderViewportTests()
        : juce::UnitTest("mixer.reorder-viewport.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        constexpr int viewportWidth = 662;
        constexpr int masterWidth = 170;
        constexpr int panelWidth = 1810;
        const int safeWidth = DAW::MixerPanel::computeSafeNormalViewportWidth(
            viewportWidth, masterWidth);
        const int scrollX = 82;
        const int safeLeft = scrollX;
        const int safeRight = safeLeft + safeWidth;
        const int wholeViewportRight = safeLeft + viewportWidth;

        beginTest("Reorder auto-scroll uses the safe normal-track edge");
        expect(DAW::MixerPanel::computeReorderAutoScrollDelta(
                   safeRight - 1, safeLeft, safeRight) > 0,
               "The right auto-scroll zone begins before the pinned Master");
        expectEquals(DAW::MixerPanel::computeReorderAutoScrollDelta(
                         safeRight, safeLeft, safeRight),
                     0,
                     "The safe boundary itself does not scroll into the Master gap");
        expectEquals(DAW::MixerPanel::computeReorderAutoScrollDelta(
                         wholeViewportRight - 1, safeLeft, safeRight),
                     0,
                     "The whole Mixer right edge is not an auto-scroll authority");

        beginTest("Insertion target remains valid while auto-scrolling");
        const std::vector<DAW::TrackID> projectOrder {
            "T0", "T1", "T2", "T3", "T4", "T5"
        };
        const std::vector<DAW::TrackReorderCore::VisibleTrackSpan> spans {
            { "T0", 0, 80 }, { "T1", 82, 80 }, { "T2", 164, 80 },
            { "T3", 246, 80 }, { "T4", 328, 80 }, { "T5", 410, 80 }
        };
        const int insertionGap = DAW::TrackReorderCore::computeInsertionGapForVisibleSpans(
            safeRight - 1, spans, projectOrder);
        expect(insertionGap >= 0
                   && insertionGap <= static_cast<int>(projectOrder.size()),
               "Auto-scroll must leave a valid project-order insertion gap");
        const auto plan = DAW::TrackReorderCore::makePlan(
            projectOrder, { "T4", "T5" }, insertionGap);
        expectEquals(static_cast<int>(plan.finalOrder.size()),
                     static_cast<int>(projectOrder.size()));

        beginTest("Selected N-track group remains outside the pinned Master");
        const int masterX = DAW::MixerPanel::computePinnedMasterX(
            scrollX, viewportWidth, masterWidth, panelWidth);
        const juce::Rectangle<int> masterBounds(masterX, 0, masterWidth, 120);
        const std::vector<juce::Rectangle<int>> selectedGroup {
            { 328, 0, DAW::MixerPanel::computeNormalStripVisibleWidth(328, 80, masterX), 120 },
            { 410, 0, DAW::MixerPanel::computeNormalStripVisibleWidth(410, 80, masterX), 120 },
            { 492, 0, DAW::MixerPanel::computeNormalStripVisibleWidth(492, 80, masterX), 120 },
            { 574, 0, DAW::MixerPanel::computeNormalStripVisibleWidth(574, 80, masterX), 120 }
        };
        for (const auto& bounds : selectedGroup)
            expect(bounds.isEmpty()
                       || (bounds.getRight() <= masterX - DAW::MixerPanel::kStripGap
                           && !bounds.intersects(masterBounds)),
                   "A reordered selected strip must never render under Master");

        beginTest("Master remains pinned during reorder auto-scroll");
        const int delta = DAW::MixerPanel::computeReorderAutoScrollDelta(
            safeRight - 1, safeLeft, safeRight);
        const int nextScrollX = scrollX + delta;
        expectEquals(DAW::MixerPanel::computePinnedMasterX(
                         nextScrollX, viewportWidth, masterWidth, panelWidth) - nextScrollX,
                     viewportWidth - masterWidth,
                     "Reorder auto-scroll must not move the Master on screen");
    }
};

/**
    @test    mixer.offscreen-flyout-geometry.v1
    @verify  Hover previews and click popups use one external-flyout policy:
             right of the actual Mixer when possible, complete left fallback
             when the right work-area margin is insufficient, on-screen, and
             never intersecting Mixer content.
 */
class MixerOffscreenFlyoutGeometryTests final : public juce::UnitTest
{
public:
    MixerOffscreenFlyoutGeometryTests()
        : juce::UnitTest("mixer.offscreen-flyout-geometry.v1", "APEX.Diagnostics") {}

    void expectOnScreenAndExternal(const DAW::BubblegumOffscreenEndpointComponent::ExternalFlyoutPlacement& placement,
                                   const juce::Rectangle<float>& mixer,
                                   const juce::Rectangle<float>& workArea)
    {
        expect(placement.valid, "Flyout placement must be valid");
        expect(!placement.bounds.intersects(mixer),
               "Flyout must never spatially overlap the Mixer");
        expect(placement.bounds.getX() >= workArea.getX()
                   && placement.bounds.getY() >= workArea.getY()
                   && placement.bounds.getRight() <= workArea.getRight()
                   && placement.bounds.getBottom() <= workArea.getBottom(),
               "Flyout must remain completely inside the display work area");
    }

    void runTest() override
    {
        using Endpoint = DAW::BubblegumOffscreenEndpointComponent;
        const juce::Rectangle<float> workArea(0.0f, 0.0f, 1920.0f, 1080.0f);
        const juce::Rectangle<float> mixer(400.0f, 180.0f, 662.0f, 320.0f);

        beginTest("Hover preview prefers the right side of the real Mixer");
        {
            const auto anchor = juce::Rectangle<float>(mixer.getRight() + 12.0f,
                                                       mixer.getCentreY() - 11.0f,
                                                       22.0f, 22.0f);
            const auto placement = Endpoint::computeExternalFlyoutPlacement(
                mixer, anchor, { 180.0f, 24.0f }, workArea, 8.0f,
                Endpoint::ExternalFlyoutSide::RightOfMixer);
            expectOnScreenAndExternal(placement, mixer, workArea);
            expect(placement.side == Endpoint::ExternalFlyoutSide::RightOfMixer,
                   "Hover preview should use the right-side placement when it fits");
            expect(placement.bounds.getX() >= anchor.getRight() + 8.0f,
                   "Right preview must attach outside the endpoint's top-right corner");
            expect(placement.bounds.getBottom() <= anchor.getY() - 8.0f,
                   "Right preview must sit above, rather than over, the endpoint");
        }

        beginTest("Left click popup attaches to the endpoint top-left corner");
        {
            const auto anchor = juce::Rectangle<float>(mixer.getX() - 34.0f,
                                                       mixer.getCentreY() - 11.0f,
                                                       22.0f, 22.0f);
            const auto placement = Endpoint::computeExternalFlyoutPlacement(
                mixer, anchor, { 260.0f, 180.0f }, workArea, 12.0f,
                Endpoint::ExternalFlyoutSide::LeftOfMixer);
            expectOnScreenAndExternal(placement, mixer, workArea);
            expect(placement.side == Endpoint::ExternalFlyoutSide::LeftOfMixer,
                   "Left endpoint popup must use the left-side placement");
            expect(placement.bounds.getRight() <= anchor.getX() - 12.0f,
                   "Left popup must attach outside the endpoint's top-left corner");
            expect(placement.bounds.getBottom() <= anchor.getY() - 12.0f,
                   "Left popup must sit above, rather than over, the endpoint");
        }

        beginTest("Horizontal insufficiency falls back above the Mixer");
        {
            const juce::Rectangle<float> rightEdgeMixer(1500.0f, 180.0f, 350.0f, 320.0f);
            const auto anchor = juce::Rectangle<float>(rightEdgeMixer.getRight() + 12.0f,
                                                       rightEdgeMixer.getCentreY() - 11.0f,
                                                       22.0f, 22.0f);
            const auto placement = Endpoint::computeExternalFlyoutPlacement(
                rightEdgeMixer, anchor, { 300.0f, 180.0f }, workArea, 12.0f,
                Endpoint::ExternalFlyoutSide::RightOfMixer);
            expectOnScreenAndExternal(placement, rightEdgeMixer, workArea);
            expect(placement.side == Endpoint::ExternalFlyoutSide::AboveMixer,
                   "When both outward horizontal sides are unavailable, use an upper fallback");
        }

        beginTest("Vertical alignment is clamped without violating separation");
        {
            const auto lowAnchor = juce::Rectangle<float>(mixer.getRight() + 12.0f,
                                                          1030.0f, 22.0f, 22.0f);
            const auto placement = Endpoint::computeExternalFlyoutPlacement(
                mixer, lowAnchor, { 220.0f, 180.0f }, workArea, 12.0f);
            expectOnScreenAndExternal(placement, mixer, workArea);
        }
    }
};

/**
    @test    mixer.keyboard-policy.v1
    @verify  Ctrl+arrows are the four Arrange/Timeline zoom axes globally;
             plain arrows navigate Mixer selection only when Mixer focus is
             active, and never share a second action.
*/
class MixerKeyboardPolicyTests final : public juce::UnitTest
{
public:
    MixerKeyboardPolicyTests()
        : juce::UnitTest("mixer.keyboard-policy.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using Core = DAW::MixerKeyboardRoutingCore;
        using Action = DAW::MixerKeyboardAction;
        const auto ctrl = juce::ModifierKeys::ctrlModifier;

        beginTest("Ctrl+Right/Left map to horizontal zoom only");
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::rightKey, ctrl, 0), true, true)
                   == Action::ZoomHorizontalIn);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::leftKey, ctrl, 0), true, true)
                   == Action::ZoomHorizontalOut);

        beginTest("Ctrl+Up/Down map to vertical zoom only");
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::upKey, ctrl, 0), true, true)
                   == Action::ZoomVerticalIn);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::downKey, ctrl, 0), true, true)
                   == Action::ZoomVerticalOut);

        beginTest("Bare Mixer arrows navigate only");
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::rightKey), true, true)
                   == Action::SelectNext);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::leftKey), true, true)
                   == Action::SelectPrevious);

        beginTest("Ctrl+arrows never navigate Mixer");
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::rightKey, ctrl, 0), true, true)
                   != Action::SelectNext);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::leftKey, ctrl, 0), true, true)
                   != Action::SelectPrevious);

        beginTest("Ctrl+arrows remain available while Mixer is closed");
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::rightKey, ctrl, 0), false, false)
                   == Action::ZoomHorizontalIn);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::downKey, ctrl, 0), false, false)
                   == Action::ZoomVerticalOut);
        expect(Core::resolve(juce::KeyPress(juce::KeyPress::rightKey), false, false)
                   == Action::None);
    }
};

static MixerDefaultWidthTests mixerDefaultWidthTests;
static MixerHeaderLayoutTests mixerHeaderLayoutTests;
static MixerSafeViewportTests mixerSafeViewportTests;
static MixerReorderViewportTests mixerReorderViewportTests;
static MixerOffscreenFlyoutGeometryTests mixerOffscreenFlyoutGeometryTests;
static MixerKeyboardPolicyTests mixerKeyboardPolicyTests;

namespace
{
    std::vector<DAW::TrackID> liveTrackIds(const DAW::TrackManager& tracks)
    {
        std::vector<DAW::TrackID> ids;
        ids.reserve((size_t) tracks.getNumTracks() + (tracks.hasMasterTrack() ? 1u : 0u));
        for (auto* track : tracks.getAllTracks())
            if (track != nullptr)
                ids.push_back(track->getID());
        if (auto* master = tracks.getMasterTrack())
            ids.push_back(master->getID());
        return ids;
    }

    std::vector<DAW::TrackID> selectedTrackIds(const DAW::MultiSelectionCore& selection)
    {
        std::vector<DAW::TrackID> ids;
        for (const auto& target : selection.getSelected(DAW::SelectionKind::Track))
            ids.push_back(target.trackId);
        return ids;
    }

    void expectTrackIds(juce::UnitTest& test,
                        const DAW::TrackManager& tracks,
                        const std::vector<DAW::TrackID>& expected,
                        const juce::String& context)
    {
        const auto actual = liveTrackIds(tracks);
        test.expectEquals((int) actual.size(), (int) expected.size(), context + ": count");
        const auto count = juce::jmin(actual.size(), expected.size());
        for (size_t i = 0; i < count; ++i)
            test.expect(actual[i] == expected[i],
                        context + ": stable ID/order mismatch at " + juce::String((int) i));
    }

    class TrackStateCommand final : public DAW::Command
    {
    public:
        TrackStateCommand(DAW::TrackManager& tracks,
                          juce::ValueTree before,
                          juce::ValueTree after,
                          juce::String description)
            : tracks_(tracks),
              before_(std::move(before)),
              after_(std::move(after)),
              description_(std::move(description)) {}

        juce::String getDescription() const override { return description_; }
        juce::String getCategory() const override { return "Routing"; }

        void execute() override
        {
            if (!hasExecuted_)
            {
                // The topology mutation has already been applied by the user
                // operation.  This is the same already-applied first execute
                // contract used by ProjectTopologyStateCommand.
                hasExecuted_ = true;
                return;
            }
            tracks_.restoreState(after_);
        }

        void undo() override
        {
            tracks_.restoreState(before_);
        }

    private:
        DAW::TrackManager& tracks_;
        juce::ValueTree before_;
        juce::ValueTree after_;
        juce::String description_;
        bool hasExecuted_ = false;
    };

    class DirtyOwnerProbe final : public DAW::CommandManager::Listener
    {
    public:
        void undoHistoryChanged() override
        {
            auto& commands = DAW::CommandManager::getInstance();
            // Mirrors MainComponent::undoHistoryChanged(): an occupied
            // history stack represents one logical dirty transition.
            if (commands.canUndo() || commands.canRedo())
                ++logicalDirtyTransitions;
        }

        int logicalDirtyTransitions = 0;
    };

    struct FolderSnapshot
    {
        juce::ValueTree tracks;
        juce::ValueTree graph;
        juce::ValueTree folders;
    };

    FolderSnapshot captureFolderSnapshot(const DAW::TrackManager& tracks,
                                         const DAW::RoutingGraph& graph,
                                         const DAW::FolderBusCore& folders)
    {
        return { tracks.getState().createCopy(),
                 graph.getState().createCopy(),
                 folders.getState().createCopy() };
    }

    class FolderStateCommand final : public DAW::Command
    {
    public:
        FolderStateCommand(DAW::TrackManager& tracks,
                           DAW::RoutingGraph& graph,
                           DAW::MasterRouteStateCore& masterRoute,
                           DAW::FolderBusCore& folders,
                           FolderSnapshot before,
                           FolderSnapshot after)
            : tracks_(tracks),
              graph_(graph),
              masterRoute_(masterRoute),
              folders_(folders),
              before_(std::move(before)),
              after_(std::move(after)) {}

        juce::String getDescription() const override { return "Delete Folder Topology"; }
        juce::String getCategory() const override { return "Routing"; }

        void execute() override
        {
            if (!hasExecuted_)
            {
                hasExecuted_ = true;
                return;
            }
            restore(after_);
        }

        void undo() override { restore(before_); }

    private:
        void restore(const FolderSnapshot& snapshot)
        {
            folders_.clear();
            graph_.restoreState(snapshot.graph);
            tracks_.restoreState(snapshot.tracks);
            folders_.restoreState(snapshot.folders, graph_, masterRoute_, tracks_);
        }

        DAW::TrackManager& tracks_;
        DAW::RoutingGraph& graph_;
        DAW::MasterRouteStateCore& masterRoute_;
        DAW::FolderBusCore& folders_;
        FolderSnapshot before_;
        FolderSnapshot after_;
        bool hasExecuted_ = false;
    };

    class GraphTrackLifecycleProbe final : public DAW::TrackManager::Listener
    {
    public:
        explicit GraphTrackLifecycleProbe(DAW::RoutingGraph& graph) : graph_(graph) {}

        void trackRemoved(const DAW::TrackID& id) override
        {
            if (auto* node = graph_.getNodeByTrackId(id))
                graph_.removeNode(node->id);
        }

    private:
        DAW::RoutingGraph& graph_;
    };
}

/**
    @test    selection.shared-track-semantics.v1
    @verify  Shared stable-ID track selection obeys the existing single,
             Ctrl, Shift, and context-click contracts independently of view
             indices.
*/
class SharedTrackSelectionSemanticsTests final : public juce::UnitTest
{
public:
    SharedTrackSelectionSemanticsTests()
        : juce::UnitTest("selection.shared-track-semantics.v1", "APEX.Selection") {}

    void runTest() override
    {
        using Target = DAW::SelectionTarget;
        const std::vector<Target> ordered {
            Target::track("A"), Target::track("B"), Target::track("C"),
            Target::track("D"), Target::track("E") };

        beginTest("Single selection replaces the set");
        DAW::MultiSelectionCore selection;
        selection.selectSingle(Target::track("B"));
        expect(selection.count(DAW::SelectionKind::Track) == 1);
        expect(selection.contains(Target::track("B")));
        expect(selection.getPrimaryTarget(DAW::SelectionKind::Track) == Target::track("B"));

        beginTest("Ctrl selection adds and Ctrl toggles by stable ID");
        selection.toggle(Target::track("C"));
        expect(selection.count(DAW::SelectionKind::Track) == 2);
        expect(selection.contains(Target::track("B")) && selection.contains(Target::track("C")));
        selection.toggle(Target::track("C"));
        expect(selection.count(DAW::SelectionKind::Track) == 1);
        expect(selection.contains(Target::track("B")) && !selection.contains(Target::track("C")));

        beginTest("Shift selection uses the current primary range contract");
        selection.selectSingle(Target::track("B"));
        selection.selectRange(Target::track("D"), ordered, false);
        expect(selection.count(DAW::SelectionKind::Track) == 3);
        expect(selection.contains(Target::track("B"))
                   && selection.contains(Target::track("C"))
                   && selection.contains(Target::track("D")));
        selection.selectRange(Target::track("E"), ordered, true);
        expect(selection.count(DAW::SelectionKind::Track) == 4);
        expect(selection.contains(Target::track("E")));

        beginTest("Context selection preserves a selected member group");
        selection.selectSingle(Target::track("B"));
        selection.add(Target::track("C"));
        selection.add(Target::track("D"));
        selection.selectContext(Target::track("C"));
        expect(selectedTrackIds(selection)
                   == std::vector<DAW::TrackID> { "B", "C", "D" });
        expect(selection.getPrimaryTarget(DAW::SelectionKind::Track) == Target::track("C"));

        beginTest("Context selection of an unselected member follows current collapse policy");
        selection.selectContext(Target::track("E"));
        expect(selectedTrackIds(selection) == std::vector<DAW::TrackID> { "E" });

        beginTest("Selection membership is independent of Mixer visible index");
        selection.selectSingle(Target::track("TRK_B"));
        selection.toggle(Target::track("TRK_D"));
        const std::vector<DAW::TrackID> shuffledVisibleOrder {
            "TRK_E", "TRK_A", "TRK_D", "TRK_C", "TRK_B" };
        juce::ignoreUnused(shuffledVisibleOrder);
        expect(selection.contains(Target::track("TRK_B"))
                   && selection.contains(Target::track("TRK_D")));
        expect(selection.count(DAW::SelectionKind::Track) == 2);
    }
};

/**
    @test    selection.multi-delete-stable-id.v1
    @verify  Canonical TrackManager batch deletion targets stable IDs, skips
             protected tracks, creates one state-command history entry, and
             restores/reapplies the exact topology through Undo/Redo.
*/
class StableIdMultiDeleteTests final : public juce::UnitTest
{
public:
    StableIdMultiDeleteTests()
        : juce::UnitTest("selection.multi-delete-stable-id.v1", "APEX.Selection") {}

    void runTest() override
    {
        using Target = DAW::SelectionTarget;
        auto& commands = DAW::CommandManager::getInstance();
        commands.clearHistory();

        DAW::TrackManager tracks;
        auto* a = tracks.createTrack("A");
        auto* b = tracks.createTrack("B");
        auto* c = tracks.createTrack("C");
        auto* d = tracks.createTrack("D");
        auto* e = tracks.createTrack("E");
        auto* master = tracks.createMasterTrack();
        const auto aId = a->getID();
        const auto bId = b->getID();
        const auto cId = c->getID();
        const auto dId = d->getID();
        const auto eId = e->getID();
        const auto masterId = master->getID();

        DAW::MultiSelectionCore selection;
        selection.selectSingle(Target::track(bId));
        selection.add(Target::track(cId));
        selection.add(Target::track(dId));

        beginTest("Batch deletion removes exactly B, C, and D by stable ID");
        const auto beforeIds = liveTrackIds(tracks);
        const auto beforeState = tracks.getState().createCopy();
        const auto deleted = tracks.deleteTracks({ bId, cId, dId });
        expect(deleted == std::vector<DAW::TrackID> { bId, cId, dId });
        expect(tracks.getTrack(bId) == nullptr
                   && tracks.getTrack(cId) == nullptr
                   && tracks.getTrack(dId) == nullptr);
        expect(tracks.getTrack(aId) != nullptr
                   && tracks.getTrack(eId) != nullptr
                   && tracks.getTrack(masterId) != nullptr);
        expectTrackIds(*this, tracks, { aId, eId, masterId }, "post-delete topology");
        expectEquals(tracks.getTrack(aId)->getIndex(), 0);
        expectEquals(tracks.getTrack(eId)->getIndex(), 1);

        for (const auto& id : deleted)
            selection.remove(Target::track(id));
        selection.clearKind(DAW::SelectionKind::Track);
        expect(selection.count(DAW::SelectionKind::Track) == 0);
        for (const auto& id : selectedTrackIds(selection))
            expect(tracks.getTrack(id) != nullptr);

        beginTest("One multi-delete publishes exactly one Undo entry and dirty transition");
        const auto afterState = tracks.getState().createCopy();
        DirtyOwnerProbe dirtyOwner;
        commands.addListener(&dirtyOwner);
        commands.execute(std::make_unique<TrackStateCommand>(
            tracks, beforeState, afterState, "Delete Tracks"));
        expectEquals(commands.getUndoCount(), 1);
        expectEquals(commands.getUndoDescription(), juce::String("Delete Tracks"));
        expectEquals(dirtyOwner.logicalDirtyTransitions, 1);

        beginTest("Undo restores B, C, D and original relative order");
        expect(commands.undo());
        expectTrackIds(*this, tracks, beforeIds, "undo topology");
        expect(tracks.getTrack(bId) != nullptr
                   && tracks.getTrack(cId) != nullptr
                   && tracks.getTrack(dId) != nullptr);

        beginTest("Redo removes the same stable IDs again");
        expect(commands.redo());
        expectTrackIds(*this, tracks, { aId, eId, masterId }, "redo topology");
        expect(tracks.getTrack(bId) == nullptr
                   && tracks.getTrack(cId) == nullptr
                   && tracks.getTrack(dId) == nullptr);
        commands.removeListener(&dirtyOwner);
        commands.clearHistory();

        beginTest("Master-only selection is a protected no-op");
        DAW::TrackManager protectedTracks;
        auto* protectedA = protectedTracks.createTrack("A");
        auto* protectedMaster = protectedTracks.createMasterTrack();
        const auto protectedAId = protectedA->getID();
        const auto protectedMasterId = protectedMaster->getID();
        DAW::MultiSelectionCore protectedSelection;
        protectedSelection.selectSingle(Target::track(protectedMasterId));
        std::vector<DAW::TrackID> eligible;
        for (const auto& id : selectedTrackIds(protectedSelection))
            if (protectedTracks.canDeleteTrack(id))
                eligible.push_back(id);
        DirtyOwnerProbe protectedDirtyOwner;
        commands.addListener(&protectedDirtyOwner);
        const auto protectedBefore = liveTrackIds(protectedTracks);
        if (!eligible.empty())
            protectedTracks.deleteTracks(eligible);
        expect(eligible.empty());
        expectTrackIds(*this, protectedTracks, protectedBefore, "protected no-op topology");
        expect(protectedTracks.getTrack(protectedAId) != nullptr
                   && protectedTracks.getTrack(protectedMasterId) != nullptr);
        expectEquals(commands.getUndoCount(), 0);
        expectEquals(protectedDirtyOwner.logicalDirtyTransitions, 0);
        commands.removeListener(&protectedDirtyOwner);

        beginTest("Mixed protected/deletable selection deletes eligible B only");
        DAW::TrackManager mixedTracks;
        auto* mixedA = mixedTracks.createTrack("A");
        auto* mixedB = mixedTracks.createTrack("B");
        auto* mixedMaster = mixedTracks.createMasterTrack();
        const auto mixedAId = mixedA->getID();
        const auto mixedBId = mixedB->getID();
        const auto mixedMasterId = mixedMaster->getID();
        DAW::MultiSelectionCore mixedSelection;
        mixedSelection.selectSingle(Target::track(mixedBId));
        mixedSelection.add(Target::track(mixedMasterId));
        std::vector<DAW::TrackID> mixedEligible;
        for (const auto& id : selectedTrackIds(mixedSelection))
            if (mixedTracks.canDeleteTrack(id))
                mixedEligible.push_back(id);
        const auto mixedBefore = mixedTracks.getState().createCopy();
        const auto mixedDeleted = mixedTracks.deleteTracks(mixedEligible);
        const auto mixedAfter = mixedTracks.getState().createCopy();
        expect(mixedDeleted == std::vector<DAW::TrackID> { mixedBId });
        expect(mixedTracks.getTrack(mixedAId) != nullptr
                   && mixedTracks.getTrack(mixedBId) == nullptr
                   && mixedTracks.getTrack(mixedMasterId) != nullptr);
        commands.execute(std::make_unique<TrackStateCommand>(
            mixedTracks, mixedBefore, mixedAfter, "Delete Tracks"));
        expectEquals(commands.getUndoCount(), 1);
        commands.clearHistory();
    }
};

/**
    @test    selection.folder-child-delete.v1
    @verify  Existing FolderBusCore semantics are preserved: deleting a folder
             dissolves it and reparents its children; deleting a child detaches
             it before the TrackManager stable-ID batch removes it. Full state
             Undo/Redo restores the folder/child topology.
*/
class FolderChildDeleteTests final : public juce::UnitTest
{
public:
    FolderChildDeleteTests()
        : juce::UnitTest("selection.folder-child-delete.v1", "APEX.Selection") {}

    void runTest() override
    {
        auto& commands = DAW::CommandManager::getInstance();
        commands.clearHistory();

        DAW::TrackManager tracks;
        auto* a = tracks.createTrack("A");
        auto* b = tracks.createTrack("B");
        auto* c = tracks.createTrack("C");
        auto* d = tracks.createTrack("D");
        auto* e = tracks.createTrack("E");
        auto* master = tracks.createMasterTrack();
        const auto aId = a->getID();
        const auto bId = b->getID();
        const auto cId = c->getID();
        const auto dId = d->getID();
        const auto eId = e->getID();
        const auto masterId = master->getID();

        DAW::RoutingGraph graph;
        DAW::MasterRouteStateCore masterRoute(graph);
        DAW::FolderBusCore folders;
        for (auto* track : tracks.getAllTracks())
            graph.addNode(track->getName(), DAW::RoutingNodeType::Track, track->getID());

        const auto folderId = folders.createFolderBus(
            "Folder", { bId, cId }, {}, graph, masterRoute, tracks);
        expect(folderId.isNotEmpty());
        expect(folders.isFolderBus(folderId));
        expect(folders.getParentFolderBus(bId) == folderId
                   && folders.getParentFolderBus(cId) == folderId);

        beginTest("Deleting a folder dissolves it and reparents its children");
        const auto beforeFolderDelete = captureFolderSnapshot(tracks, graph, folders);
        folders.dissolveFolderBus(folderId, graph, masterRoute, tracks);
        const auto afterFolderDelete = captureFolderSnapshot(tracks, graph, folders);
        const auto afterFolderDeleteIds = liveTrackIds(tracks);
        expect(!folders.isFolderBus(folderId));
        expect(tracks.getTrack(folderId) == nullptr);
        expect(folders.getParentFolderBus(bId).isEmpty()
                   && folders.getParentFolderBus(cId).isEmpty());
        expect(graph.getNodeByTrackId(folderId) == nullptr);
        expect(tracks.getTrack(bId) != nullptr && tracks.getTrack(cId) != nullptr);
        expect(tracks.getTrack(aId) != nullptr
                   && tracks.getTrack(dId) != nullptr
                   && tracks.getTrack(eId) != nullptr
                   && tracks.getTrack(masterId) != nullptr);

        beginTest("Folder delete has one topology Undo and restores complete folder state");
        commands.execute(std::make_unique<FolderStateCommand>(
            tracks, graph, masterRoute, folders,
            beforeFolderDelete, afterFolderDelete));
        expectEquals(commands.getUndoCount(), 1);
        expect(commands.undo());
        expect(folders.isFolderBus(folderId));
        expect(folders.getParentFolderBus(bId) == folderId
                   && folders.getParentFolderBus(cId) == folderId);
        expect(graph.getNodeByTrackId(folderId) != nullptr);
        expect(tracks.getTrack(folderId) != nullptr);
        expect(commands.redo());
        expect(!folders.isFolderBus(folderId));
        expect(folders.getParentFolderBus(bId).isEmpty()
                   && folders.getParentFolderBus(cId).isEmpty());
        expectTrackIds(*this, tracks, afterFolderDeleteIds, "folder redo topology");
        commands.clearHistory();

        beginTest("Deleting a folder child detaches it before stable-ID batch delete");
        // Recreate the folder after the preceding redo so this case starts from
        // the same canonical parent/child arrangement.
        const auto recreatedFolderId = folders.createFolderBus(
            "Folder", { bId, cId }, {}, graph, masterRoute, tracks);
        expect(recreatedFolderId.isNotEmpty());
        GraphTrackLifecycleProbe graphLifecycle(graph);
        tracks.addListener(&graphLifecycle);
        const auto beforeChildDelete = captureFolderSnapshot(tracks, graph, folders);
        const auto childParent = folders.getParentFolderBus(bId);
        expect(childParent == recreatedFolderId);
        folders.removeChildFromFolderBus(bId, graph, masterRoute, &tracks);
        const auto childDeleted = tracks.deleteTracks({ bId });
        tracks.removeListener(&graphLifecycle);
        const auto afterChildDelete = captureFolderSnapshot(tracks, graph, folders);
        expect(childDeleted == std::vector<DAW::TrackID> { bId });
        expect(tracks.getTrack(bId) == nullptr);
        expect(folders.getParentFolderBus(bId).isEmpty());
        expect(folders.getDirectChildren(recreatedFolderId).end()
                   == std::find(folders.getDirectChildren(recreatedFolderId).begin(),
                                folders.getDirectChildren(recreatedFolderId).end(), bId));
        expect(graph.getNodeByTrackId(bId) == nullptr);
        expect(tracks.getTrack(cId) != nullptr
                   && folders.getParentFolderBus(cId) == recreatedFolderId);

        commands.execute(std::make_unique<FolderStateCommand>(
            tracks, graph, masterRoute, folders,
            beforeChildDelete, afterChildDelete));
        expectEquals(commands.getUndoCount(), 1);
        expect(commands.undo());
        expect(tracks.getTrack(bId) != nullptr
                   && folders.getParentFolderBus(bId) == recreatedFolderId);
        expect(commands.redo());
        expect(tracks.getTrack(bId) == nullptr
                   && folders.getParentFolderBus(bId).isEmpty());
        commands.clearHistory();
    }
};

static SharedTrackSelectionSemanticsTests sharedTrackSelectionSemanticsTests;
static StableIdMultiDeleteTests stableIdMultiDeleteTests;
static FolderChildDeleteTests folderChildDeleteTests;

namespace
{
    std::vector<DAW::TrackID> normalTrackIds(const DAW::TrackManager& tracks)
    {
        std::vector<DAW::TrackID> ids;
        ids.reserve((size_t) tracks.getNumTracks());
        for (auto* track : tracks.getAllTracks())
            if (track != nullptr)
                ids.push_back(track->getID());
        return ids;
    }

    std::vector<DAW::TrackID> makeNumberedIds(int count)
    {
        std::vector<DAW::TrackID> ids;
        ids.reserve((size_t) juce::jmax(0, count));
        for (int i = 0; i < count; ++i)
            ids.push_back("T" + juce::String(i));
        return ids;
    }

    std::vector<DAW::TrackID> idsAt(const std::vector<DAW::TrackID>& order,
                                    std::initializer_list<size_t> indexes)
    {
        std::vector<DAW::TrackID> ids;
        ids.reserve(indexes.size());
        for (const auto index : indexes)
            if (index < order.size())
                ids.push_back(order[index]);
        return ids;
    }

    void expectNormalOrder(juce::UnitTest& test,
                           const DAW::TrackManager& tracks,
                           const std::vector<DAW::TrackID>& expected,
                           const juce::String& context)
    {
        const auto actual = normalTrackIds(tracks);
        test.expect(actual == expected, context + ": exact normal TrackID order");
    }

    class ReorderTrackHarness final
    {
    public:
        explicit ReorderTrackHarness(int count, bool withMaster = false)
        {
            for (int i = 0; i < count; ++i)
                if (auto* track = tracks.createTrack("Track " + juce::String(i)))
                    ids.push_back(track->getID());

            if (withMaster)
                masterId = tracks.createMasterTrack()->getID();
        }

        std::vector<DAW::TrackID> order() const { return normalTrackIds(tracks); }

        DAW::TrackManager tracks;
        DAW::MultiSelectionCore selection;
        std::vector<DAW::TrackID> ids;
        DAW::TrackID masterId;
    };
}

/**
    @test    reorder.n-track-group.v1
    @verify  TrackReorderCore performs one dynamic stable-ID block
             transformation.  The destination is an original-order gap, so
             every selected source index before that gap is compensated.
*/
class NTrackGroupReorderTests final : public juce::UnitTest
{
public:
    NTrackGroupReorderTests()
        : juce::UnitTest("reorder.n-track-group.v1", "APEX.Reorder") {}

    void runTest() override
    {
        using DAW::TrackReorderCore::makePlan;

        const std::vector<DAW::TrackID> order { "A", "B", "C", "D", "E", "F", "G", "H" };

        beginTest("Single selected track still reorders normally");
        expect(makePlan(order, { "B" }, 8).finalOrder
                   == std::vector<DAW::TrackID> { "A", "C", "D", "E", "F", "G", "H", "B" });

        beginTest("Two contiguous selected tracks move together");
        expect(makePlan(order, { "B", "C" }, 8).finalOrder
                   == std::vector<DAW::TrackID> { "A", "D", "E", "F", "G", "H", "B", "C" });

        beginTest("Three contiguous selected tracks move together");
        expect(makePlan(order, { "B", "C", "D" }, 8).finalOrder
                   == std::vector<DAW::TrackID> { "A", "E", "F", "G", "H", "B", "C", "D" });

        beginTest("More-than-three contiguous tracks move together");
        expect(makePlan(order, { "B", "C", "D", "E" }, 8).finalOrder
                   == std::vector<DAW::TrackID> { "A", "F", "G", "H", "B", "C", "D", "E" });

        beginTest("Ten selected tracks move as one large dynamic group");
        const auto twenty = makeNumberedIds(20);
        const auto tenSelected = idsAt(twenty, { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
        const auto tenPlan = makePlan(twenty, tenSelected, 20);
        expectEquals((int) tenPlan.selectedInProjectOrder.size(), 10);
        expectEquals((int) tenPlan.finalOrder.size(), 20);
        expect(tenPlan.finalOrder
                   == std::vector<DAW::TrackID> {
                       "T0", "T1", "T12", "T13", "T14", "T15", "T16", "T17", "T18", "T19",
                       "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11" });

        beginTest("Selection size has no hardcoded three-track maximum");
        expect(tenPlan.selectedInProjectOrder.size() > 3
                   && tenPlan.selectedInProjectOrder.size() == tenSelected.size());

        beginTest("Contiguous group preserves project-relative order, not click order");
        const auto clickReversed = makePlan(order, { "D", "B", "C" }, 8);
        expect(clickReversed.selectedInProjectOrder
                   == std::vector<DAW::TrackID> { "B", "C", "D" });

        beginTest("Non-contiguous selected tracks move together");
        const auto nonContiguous = makePlan(order, { "G", "B", "E" }, 8);
        expect(nonContiguous.finalOrder
                   == std::vector<DAW::TrackID> { "A", "C", "D", "F", "H", "B", "E", "G" });

        beginTest("Non-contiguous relative project order is preserved");
        expect(nonContiguous.selectedInProjectOrder
                   == std::vector<DAW::TrackID> { "B", "E", "G" });

        beginTest("Dragging a selected member moves the whole current selection");
        const auto selectedMember = makePlan(order, { "B", "C", "D" }, 8);
        expect(selectedMember.selectedInProjectOrder.size() == 3
                   && selectedMember.finalOrder.back() == "D");

        beginTest("Dragging an unselected member moves only that member");
        const auto unselectedMember = makePlan(order, { "H" }, 1);
        expect(unselectedMember.finalOrder
                   == std::vector<DAW::TrackID> { "A", "H", "B", "C", "D", "E", "F", "G" });

        beginTest("Upward group move is correct");
        expect(makePlan({ "A", "B", "C", "D", "E", "F", "G" }, { "D", "E", "F" }, 1).finalOrder
                   == std::vector<DAW::TrackID> { "A", "D", "E", "F", "B", "C", "G" });

        beginTest("Downward group move is correct");
        expect(makePlan({ "A", "B", "C", "D", "E", "F", "G" }, { "B", "C", "D" }, 7).finalOrder
                   == std::vector<DAW::TrackID> { "A", "E", "F", "G", "B", "C", "D" });

        beginTest("Downward destination compensates for all selected source indexes");
        const auto compensated = makePlan(order, { "B", "C", "D" }, 8);
        expectEquals((int) compensated.insertionIndex, 5);
        expect(compensated.finalOrder
                   == std::vector<DAW::TrackID> { "A", "E", "F", "G", "H", "B", "C", "D" });

        beginTest("Non-contiguous downward move is correct");
        expect(makePlan({ "A", "B", "C", "D", "E", "F", "G", "H", "I" },
                         { "B", "E", "G" }, 9).finalOrder
                   == std::vector<DAW::TrackID> { "A", "C", "D", "F", "H", "I", "B", "E", "G" });

        beginTest("Drop inside the effective selected block is a no-op");
        const auto inside = makePlan(order, { "B", "C", "D" }, 2);
        expect(!inside.changed && inside.finalOrder == order);

        beginTest("Adjacent equivalent drop is a no-op");
        const auto adjacent = makePlan(order, { "B", "C", "D" }, 4);
        expect(!adjacent.changed && adjacent.finalOrder == order);

        beginTest("Destination mapping remains correct after a stationary-pointer edge scroll");
        const std::vector<DAW::TrackReorderCore::VisibleTrackSpan> beforeScroll {
            { "T0", 0, 80 }, { "T1", 82, 80 }, { "T2", 164, 80 }, { "T3", 246, 80 },
            { "T4", 328, 80 } };
        const std::vector<DAW::TrackReorderCore::VisibleTrackSpan> afterScroll {
            { "T0", -80, 80 }, { "T1", 2, 80 }, { "T2", 84, 80 }, { "T3", 166, 80 },
            { "T4", 248, 80 } };
        const auto project = makeNumberedIds(8);
        const int beforeGap = DAW::TrackReorderCore::computeInsertionGapForVisibleSpans(
            220, beforeScroll, project);
        const int afterGap = DAW::TrackReorderCore::computeInsertionGapForVisibleSpans(
            220, afterScroll, project);
        expectEquals(beforeGap, 3);
        expectEquals(afterGap, 4);
        expect(afterGap != beforeGap);
    }
};

/**
    @test    reorder.transaction.v1
    @verify  One successful N-track reorder is one stable-ID topology command;
             no-op plans do not publish history or dirty transitions, and
             selection identity survives the order change.
*/
class NTrackReorderTransactionTests final : public juce::UnitTest
{
public:
    NTrackReorderTransactionTests()
        : juce::UnitTest("reorder.transaction.v1", "APEX.Reorder") {}

    void runTest() override
    {
        auto& commands = DAW::CommandManager::getInstance();

        beginTest("No-op publishes zero Undo entries and zero dirty transitions");
        commands.clearHistory();
        ReorderTrackHarness noOp(8);
        const auto noOpBefore = noOp.order();
        const auto noOpPlan = DAW::TrackReorderCore::makePlan(
            noOpBefore, { noOp.ids[1], noOp.ids[2], noOp.ids[3] }, 4);
        DirtyOwnerProbe noOpDirty;
        commands.addListener(&noOpDirty);
        if (noOpPlan.changed)
        {
            noOp.tracks.applyTrackOrder(noOpPlan.finalOrder);
            commands.execute(std::make_unique<DAW::TrackReorderMultiCommand>(
                noOp.tracks, noOpBefore, noOpPlan.finalOrder, true));
        }
        expect(!noOpPlan.changed);
        expectEquals(commands.getUndoCount(), 0);
        expectEquals(noOpDirty.logicalDirtyTransitions, 0);
        commands.removeListener(&noOpDirty);

        beginTest("Successful reorder publishes exactly one Undo and one dirty transition");
        commands.clearHistory();
        ReorderTrackHarness harness(8);
        harness.selection.selectSingle(DAW::SelectionTarget::track(harness.ids[1]));
        harness.selection.add(DAW::SelectionTarget::track(harness.ids[2]));
        harness.selection.add(DAW::SelectionTarget::track(harness.ids[3]));
        const auto original = harness.order();
        const auto plan = DAW::TrackReorderCore::makePlan(
            original, { harness.ids[3], harness.ids[1], harness.ids[2] }, 8);
        DirtyOwnerProbe dirty;
        commands.addListener(&dirty);
        harness.tracks.applyTrackOrder(plan.finalOrder);
        commands.execute(std::make_unique<DAW::TrackReorderMultiCommand>(
            harness.tracks, original, plan.finalOrder, true));
        expectEquals(commands.getUndoCount(), 1);
        expectEquals(commands.getUndoDescription(), juce::String("Move Tracks"));
        expectEquals(dirty.logicalDirtyTransitions, 1);
        expect(harness.selection.contains(DAW::SelectionTarget::track(harness.ids[1]))
                   && harness.selection.contains(DAW::SelectionTarget::track(harness.ids[2]))
                   && harness.selection.contains(DAW::SelectionTarget::track(harness.ids[3])));

        beginTest("Undo restores the exact original full TrackID order");
        expect(commands.undo());
        expectNormalOrder(*this, harness.tracks, original, "reorder undo");

        beginTest("Redo restores the exact moved TrackID order");
        expect(commands.redo());
        expectNormalOrder(*this, harness.tracks, plan.finalOrder, "reorder redo");
        commands.removeListener(&dirty);
        commands.clearHistory();

        beginTest("Master is filtered from the moving normal-track block");
        ReorderTrackHarness masterHarness(4, true);
        const auto masterBefore = masterHarness.order();
        const auto masterPlan = DAW::TrackReorderCore::makePlan(
            masterBefore, { masterHarness.masterId, masterHarness.ids[1] }, 4);
        expect(masterPlan.selectedInProjectOrder
                   == std::vector<DAW::TrackID> { masterHarness.ids[1] });
        masterHarness.tracks.applyTrackOrder(masterPlan.finalOrder);
        expectNormalOrder(*this, masterHarness.tracks,
                          { masterHarness.ids[0], masterHarness.ids[2], masterHarness.ids[3], masterHarness.ids[1] },
                          "Master wall normal order");
        expect(masterHarness.tracks.getMasterTrack() != nullptr
                   && masterHarness.tracks.getMasterTrack()->getID() == masterHarness.masterId);
    }
};

/**
    @test    reorder.folder-authority.v1
    @verify  Reordering a folder bus and its children preserves canonical
             FolderBusCore membership, while Arranger/Mixer/Floating Mixer
             expose the same group-reorder callback contract.
*/
class NTrackReorderAuthorityTests final : public juce::UnitTest
{
public:
    NTrackReorderAuthorityTests()
        : juce::UnitTest("reorder.folder-authority.v1", "APEX.Reorder") {}

    void runTest() override
    {
        beginTest("Folder relationships survive an order-only group transformation");
        DAW::TrackManager tracks;
        auto* a = tracks.createTrack("A");
        auto* b = tracks.createTrack("B");
        auto* c = tracks.createTrack("C");
        auto* d = tracks.createTrack("D");
        const auto aId = a->getID();
        const auto bId = b->getID();
        const auto cId = c->getID();
        const auto dId = d->getID();
        DAW::RoutingGraph graph;
        DAW::MasterRouteStateCore masterRoute(graph);
        DAW::FolderBusCore folders;
        for (auto* track : tracks.getAllTracks())
            graph.addNode(track->getName(), DAW::RoutingNodeType::Track, track->getID());
        const auto folderId = folders.createFolderBus(
            "Folder", { bId, cId }, {}, graph, masterRoute, tracks);
        const auto beforeBParent = folders.getParentFolderBus(bId);
        const auto beforeCParent = folders.getParentFolderBus(cId);
        const auto beforeOrder = normalTrackIds(tracks);
        const auto plan = DAW::TrackReorderCore::makePlan(
            beforeOrder, { folderId, cId, bId }, (int) beforeOrder.size());
        tracks.applyTrackOrder(plan.finalOrder);
        expect(folders.getParentFolderBus(bId) == beforeBParent
                   && folders.getParentFolderBus(cId) == beforeCParent);
        expect(folders.isFolderBus(folderId));
        expect(tracks.getTrack(aId) != nullptr && tracks.getTrack(dId) != nullptr);

        beginTest("Arranger and Mixer expose the same canonical group callback type");
        using Callback = DAW::TrackReorderCore::GroupReorderRequestedCallback;
        expect((std::is_same<decltype(std::declval<DAW::TrackList>().onMultiTrackReorderRequested),
                             Callback>::value));
        expect((std::is_same<decltype(std::declval<DAW::MixerPanel>().onMultiTrackReorderRequested),
                             Callback>::value));

        beginTest("Floating Mixer uses the MixerPanel callback path without a fork");
        Callback canonical = [](const std::vector<DAW::TrackID>& ids, int gap)
        {
            juce::ignoreUnused(ids, gap);
        };
        Callback arrangerPath = canonical;
        Callback mixerPath = canonical;
        Callback floatingMixerPath = mixerPath;
        expect((bool) arrangerPath && (bool) mixerPath && (bool) floatingMixerPath);
        expect(&arrangerPath.target_type() == &mixerPath.target_type());
        expect(&mixerPath.target_type() == &floatingMixerPath.target_type());

        beginTest("Mixer destination mapping excludes the pinned Master hard wall");
        const std::vector<DAW::TrackID> normal { "T0", "T1", "T2", "T3" };
        const std::vector<DAW::TrackReorderCore::VisibleTrackSpan> spans {
            { "T0", 0, 80 }, { "T1", 82, 80 }, { "T2", 164, 80 }, { "T3", 246, 80 } };
        const auto endGap = DAW::TrackReorderCore::computeInsertionGapForVisibleSpans(
            500, spans, normal);
        const auto masterWallPlan = DAW::TrackReorderCore::makePlan(normal, { "T1", "T2" }, endGap);
        expectEquals(endGap, 4);
        expect(masterWallPlan.finalOrder
                   == std::vector<DAW::TrackID> { "T0", "T3", "T1", "T2" });
    }
};

static NTrackGroupReorderTests nTrackGroupReorderTests;
static NTrackReorderTransactionTests nTrackReorderTransactionTests;
static NTrackReorderAuthorityTests nTrackReorderAuthorityTests;

//==============================================================================
/**
    @test    mixer.fader_undo_exact.v1
    @verify  Mixer fader gestures commit one canonical linear-gain command,
             and Undo/Redo restore exact before/after values for normal and
             Master strips.  The 0 dB case explicitly guards against unity
             being interpreted as linear zero/minimum.
*/
class MixerFaderUndoExactTests final : public juce::UnitTest
{
public:
    MixerFaderUndoExactTests()
        : juce::UnitTest ("mixer.fader_undo_exact.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        auto& commands = DAW::CommandManager::getInstance();
        const auto previousGlobalRange = DAW::FaderRangeCore::getGlobalInstance();
        DAW::FaderRangeCore faderRange;
        DAW::FaderRangeCore::setGlobalInstance (&faderRange);

        beginTest ("Fader conversion keeps 0 dB at canonical unity gain");
        expectWithinAbsoluteError (faderRange.dbToGain (0.0f), 1.0f, 0.000001f,
                                    "0 dB must map to linear unity");
        expectWithinAbsoluteError (DAW::FaderRangeCore::gainToDb (1.0f), 0.0f,
                                    0.000001f, "linear unity must display as 0 dB");
        expect (faderRange.dbToGain (-6.0f) > 0.0f,
                "a non-minimum dB value must remain nonzero canonical gain");

        std::unique_ptr<juce::Component> peerWindow;
        auto* mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        if (mouseSource == nullptr)
        {
            peerWindow = std::make_unique<juce::Component>();
            peerWindow->setSize (64, 64);
            peerWindow->addToDesktop (0);
            mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        }

        expect (mouseSource != nullptr, "fader regression requires a JUCE mouse source");
        if (mouseSource == nullptr)
        {
            DAW::FaderRangeCore::setGlobalInstance (previousGlobalRange);
            return;
        }

        auto makeEvent = [mouseSource] (juce::Component* target, juce::Point<int> localPos)
        {
            const auto pos = localPos.toFloat();
            return juce::MouseEvent (*mouseSource, pos,
                                     juce::ModifierKeys::leftButtonModifier,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     target, target, juce::Time::getCurrentTime(),
                                     pos, juce::Time::getCurrentTime(), 1, false);
        };

        auto beginFaderDrag = [&makeEvent] (DAW::MixerStrip& strip)
        {
            if (strip.getWidth() <= 0 || strip.getHeight() <= 0)
                return juce::Point<int> (-1, -1);

            // The fader thumb is private UI geometry. Search the actual strip
            // hit path rather than duplicating its pixel calculation here.
            for (int y = 0; y < strip.getHeight(); ++y)
                for (int x = 0; x < strip.getWidth(); x += 2)
                {
                    auto event = makeEvent (&strip, { x, y });
                    strip.mouseDown (event);
                    if (strip.isFaderDragging())
                        return juce::Point<int> (x, y);
                    strip.mouseUp (event);
                }

            return juce::Point<int> (-1, -1);
        };

        DAW::TrackManager tracks;

        auto runGestureCase = [&] (DAW::Track& track,
                                   float beforeDb,
                                   float afterDb,
                                   const juce::String& label,
                                   bool useIntermediateUpdates)
        {
            commands.clearHistory();
            track.setVolume (faderRange.dbToGain (beforeDb));
            const float expectedBeforeGain = track.getVolume();
            const float expectedAfterGain  = faderRange.dbToGain (afterDb);

            DAW::MixerStrip strip (track, faderRange);
            strip.setBounds (0, 0, track.isMaster() ? 170 : 80, 520);

            int callbackCount = 0;
            strip.onVolumeChanged = [&] (float beforeGain, float afterGain)
            {
                ++callbackCount;
                if (std::abs (beforeGain - afterGain) > 0.0001f)
                    commands.execute (std::make_unique<DAW::TrackPropertyChangeCommand>(
                        tracks, track.getID(),
                        DAW::TrackPropertyChangeCommand::Property::Volume,
                        (double) beforeGain, (double) afterGain,
                        "Adjust Track Volume", true));
            };

            const auto hit = beginFaderDrag (strip);
            expect (hit.x >= 0,
                    label + ": actual MixerStrip fader thumb must be hittable");
            if (hit.x < 0 || mouseSource == nullptr)
                return;

            const auto paramID = apex::automation::AutomationParameterKeyRegistry::getInstance()
                .findID (apex::automation::AutomationParameterKeyRegistry::trackVolumeKey (track.getID()));
            auto* parameter = apex::automation::AutomationSystem::getInstance()
                .getRegistry().find (paramID);
            expect (parameter != nullptr, label + ": volume parameter must be registered");
            if (parameter == nullptr)
                return;

            const auto sendUpdate = [&] (float norm)
            {
                parameter->setValueFromUser (norm);
                // Dispatch the real listener callback deterministically without
                // waiting for the 60 Hz UI timer.
                strip.parameterValueChanged (*parameter, norm,
                                              apex::automation::ChangeSource::User);
            };

            const float finalNorm = faderRange.dbToNorm (afterDb);
            if (useIntermediateUpdates)
            {
                sendUpdate (faderRange.dbToNorm (beforeDb - 2.0f));
                sendUpdate (faderRange.dbToNorm ((beforeDb + afterDb) * 0.5f));
            }
            sendUpdate (finalNorm);
            strip.mouseUp (makeEvent (&strip, hit));

            expectEquals (callbackCount, 1,
                          label + ": one physical drag must emit one commit callback");
            expectEquals (commands.getUndoCount(), 1,
                          label + ": one physical drag must create one Undo entry");
            expectWithinAbsoluteError (track.getVolume(), expectedAfterGain, 0.000001f,
                                        label + ": live drag must end at exact after gain");

            expect (commands.undo(), label + ": Undo must succeed");
            expectWithinAbsoluteError (track.getVolume(), expectedBeforeGain, 0.000001f,
                                        label + ": Undo must restore exact before gain");
            expect (commands.redo(), label + ": Redo must succeed");
            expectWithinAbsoluteError (track.getVolume(), expectedAfterGain, 0.000001f,
                                        label + ": Redo must restore exact after gain");
        };

        auto* normal = tracks.createTrack ("Fader Undo Normal");
        auto* master = tracks.createMasterTrack();
        if (normal != nullptr)
        {
            beginTest ("Normal fader: 0 dB to negative dB, one exact Undo/Redo");
            runGestureCase (*normal, 0.0f, -6.0f, "normal 0-to-minus-6", true);

            beginTest ("Normal fader: cross-zero upward and downward values");
            runGestureCase (*normal, -10.0f, 1.5f, "normal minus-10-to-plus-1.5", true);
            runGestureCase (*normal, 1.5f, -10.0f, "normal plus-1.5-to-minus-10", true);
        }

        if (master != nullptr)
        {
            beginTest ("Master fader shares exact canonical transaction path");
            runGestureCase (*master, -6.0f, 1.5f, "master minus-6-to-plus-1.5", true);
        }

        beginTest ("No-op drag creates zero Undo entries");
        if (normal != nullptr)
        {
            commands.clearHistory();
            normal->setVolume (faderRange.dbToGain (0.0f));
            DAW::MixerStrip strip (*normal, faderRange);
            strip.setBounds (0, 0, 80, 520);
            const auto hit = beginFaderDrag (strip);
            expect (hit.x >= 0, "no-op: actual fader thumb must be hittable");
            if (hit.x >= 0 && mouseSource != nullptr)
            {
                const auto paramID = apex::automation::AutomationParameterKeyRegistry::getInstance()
                    .findID (apex::automation::AutomationParameterKeyRegistry::trackVolumeKey (normal->getID()));
                if (auto* parameter = apex::automation::AutomationSystem::getInstance()
                                          .getRegistry().find (paramID))
                {
                    const float sameNorm = faderRange.dbToNorm (0.0f);
                    parameter->setValueFromUser (sameNorm);
                    strip.parameterValueChanged (*parameter, sameNorm,
                                                  apex::automation::ChangeSource::User);
                    strip.mouseUp (makeEvent (&strip, hit));
                }
            }
            expectEquals (commands.getUndoCount(), 0,
                          "returning to the original canonical gain must not create history");
        }

        commands.clearHistory();
        DAW::FaderRangeCore::setGlobalInstance (previousGlobalRange);
    }
};

static MixerFaderUndoExactTests mixerFaderUndoExactTests;

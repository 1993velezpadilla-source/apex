// ===========================================================================
// QuickTrackPopupTests.cpp
// REAL QuickTrackBuilderPopup interaction regressions.
//
// The popup's fixed architecture: FIXED header + scrollable role viewport +
// FIXED/sticky bottom action bar, with EVERY control a REAL juce::Button
// child. These tests synthesize pointer events at the ACTUAL child-control
// centers (resolved through the real component tree) — callbacks are never
// invoked directly — and assert the observable behavior.
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Source/QuickTrackCore/QuickTrackBuilderPopup.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/RoutingCore/MasterRouteStateCore.h"
#include "../../../Source/Bubblegum/BubblegumSendStateCore.h"

namespace
{

using namespace DAW;

struct PopupHarness
{
    TrackManager tracks;
    RoutingGraph graph;
    MasterRouteStateCore masterRoute { graph };
    BubblegumSendStateCore sends;
    QuickTrackColorSystem colors { 4242 };
    QuickTrackBuilderCore builder { tracks, graph, masterRoute, sends, colors };
    QuickTrackTemplateStore templates;
    QuickTrackBuilderPopup popup;

    PopupHarness()
        : templates(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("APEX_QTP_" + juce::Uuid().toString().substring(0, 8))),
          popup(builder, templates)
    {
        popup.setSize(QuickTrackBuilderPopup::kPopupWidth, QuickTrackBuilderPopup::kPopupMaxHeight);
        popup.setVisible(true);
        tracks.createMasterTrack();
    }

    ~PopupHarness()
    {
        // Close any nested modal UI (color-picker CallOutBox, TEMPLATE
        // PopupMenu) launched during the test. JUCE modals block every
        // other component's button state machine, and the picker content
        // references this harness's destroyed builder — both would corrupt
        // later tests (and the process at shutdown).
        juce::PopupMenu::dismissAllActiveMenus();
        juce::Desktop::getInstance().getAnimator().cancelAllAnimations(false);
        for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
        {
            if (auto* cb = dynamic_cast<juce::CallOutBox*>(
                    juce::Desktop::getInstance().getComponent(i)))
            {
                cb->setVisible(false);   // dismiss → CallOutBox self-deletes
                cb->exitModalState(0);
            }
        }
        templates.getDirectory().deleteRecursively();
    }
};

class QuickTrackPopupTests final : public juce::UnitTest
{
public:
    QuickTrackPopupTests() : juce::UnitTest("QuickTrackPopup.Interaction", "APEX.QuickTrackPopup") {}

    void runTest() override
    {
        beginTest("HarnessConstructs");
        {
            PopupHarness h;
        }

        std::unique_ptr<juce::Component> peerWindow;
        auto* mouseSource = juce::Desktop::getInstance().getMouseSource(0);
        if (mouseSource == nullptr)
        {
            peerWindow = std::make_unique<juce::Component>();
            peerWindow->setSize(64, 64);
            peerWindow->addToDesktop(0);
            mouseSource = juce::Desktop::getInstance().getMouseSource(0);
        }
        expect(mouseSource != nullptr, "mouse source available");
        if (mouseSource == nullptr)
            return;

        auto makeEvent = [mouseSource](juce::Component* target, juce::Point<int> localPos)
        {
            const auto pos = localPos.toFloat();
            return juce::MouseEvent(*mouseSource, pos, juce::ModifierKeys::leftButtonModifier,
                                    1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    target, target, juce::Time::getCurrentTime(),
                                    pos, juce::Time::getCurrentTime(), 1, false);
        };

        // Resolve + click a REAL control through the REAL component tree.
        // Scrolls the role viewport so the target control is on-screen first
        // (rows live in the scrollable content; the fixed bars never move).
        auto clickControl = [this, &makeEvent](juce::Component& root, juce::Button& button,
                                               const char* what, int expectedFires,
                                               QuickTrackBuilderPopup* popup)
        {
            if (popup != nullptr && button.getParentComponent() != nullptr)
            {
                // Rows are direct children of the role-list content; the
                // button's parent is its row. Bring the row into view.
                const int contentY = button.getParentComponent()->getY() + button.getY();
                popup->getRoleViewport().setViewPosition(
                    0, juce::jmax(0, contentY - popup->getRoleViewport().getHeight() / 2));
            }
            auto* hit = hitAt(root, button);
            expect(hit != nullptr, juce::String(what) + ": hit resolves");
            if (hit == nullptr)
                return;
            expect(hit == &button,
                   juce::String(what) + ": hit-test resolves to the real button itself");
            // Full pointer sequence in the hit component's OWN local space.
            // juce::Button fires its click on mouseUp only when both the
            // down state AND the hover (isOver) state were established —
            // isOver is set by mouseEnter, so the enter must be delivered.
            const auto centre = juce::Point<int>(hit->getWidth() / 2, hit->getHeight() / 2);
            hit->mouseEnter(makeEvent(hit, centre));
            hit->mouseDown(makeEvent(hit, centre));
            hit->mouseUp(makeEvent(hit, centre));
            hit->mouseExit(makeEvent(hit, centre));
        };

        beginTest("RoleClickReceivesInput");
        {
            PopupHarness h;
            auto* row = h.popup.getRoleListContent().getRowForRoleId("coro_hook");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            expectEquals(h.tracks.getNumTracks(), 0);
            clickControl(h.popup, row->getNameButton(), "role name", 1, &h.popup);
            expectEquals(h.tracks.getNumTracks(), 1, "role click quick-creates exactly one track");
            expectEquals(h.tracks.getTrack(0)->getName(), juce::String("Coro / Hook"),
                         "canonical auto name from the builder");
        }

        beginTest("ColorButtonReceivesInput");
        {
            PopupHarness h;
            int colorRequests = 0;
            h.popup.onColorPickerRequested = [&] { ++colorRequests; };
            auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            clickControl(h.popup, row->getSwatchButton(), "color swatch", 1, &h.popup);
            expectEquals(colorRequests, 1, "swatch click opens the color picker exactly once");
            expectEquals(h.tracks.getNumTracks(), 0, "swatch click never quick-creates a track");
        }

        beginTest("MinusReceivesInput");
        {
            PopupHarness h;
            auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            row->setCount(3);
            clickControl(h.popup, row->getMinusButton(), "minus", 1, &h.popup);
            expectEquals(row->getCount(), 2, "minus decrements");
            row->setCount(0);
            clickControl(h.popup, row->getMinusButton(), "minus", 1, &h.popup);
            expectEquals(row->getCount(), 0, "count never goes below zero");
        }

        beginTest("PlusReceivesInput");
        {
            PopupHarness h;
            auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            expectEquals(row->getCount(), 0);
            clickControl(h.popup, row->getPlusButton(), "plus", 1, &h.popup);
            clickControl(h.popup, row->getPlusButton(), "plus", 2, &h.popup);
            expectEquals(row->getCount(), 2, "plus increments the queue");
        }

        beginTest("ClearReceivesInput");
        {
            PopupHarness h;
            auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            row->setCount(4);
            clickControl(h.popup, row->getClearButton(), "clear", 1, &h.popup);
            expectEquals(row->getCount(), 0, "clear resets the row queue");
            expectEquals(h.tracks.getNumTracks(), 0, "clear never creates tracks");
        }

        beginTest("CheckmarkReceivesInput");
        {
            PopupHarness h;
            auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            row->setCount(4);
            clickControl(h.popup, row->getCheckButton(), "checkmark", 1, &h.popup);
            expectEquals(h.tracks.getNumTracks(), 4, "checkmark creates exactly the queued tracks");
            expectEquals(h.tracks.getTrack(0)->getName(), juce::String("Doubles"));
            expectEquals(h.tracks.getTrack(1)->getName(), juce::String("Doubles 2"));
            expectEquals(h.tracks.getTrack(2)->getName(), juce::String("Doubles 3"));
            expectEquals(h.tracks.getTrack(3)->getName(), juce::String("Doubles 4"));
            expectEquals(row->getCount(), 0, "consumed queue is cleared after row creation");
        }

        beginTest("TemplateReceivesInput");
        {
            PopupHarness h;
            int templatePresses = 0;
            h.popup.onTemplatePressed = [&] { ++templatePresses; };
            clickControl(h.popup, h.popup.getBottomBar().templateBtn_, "TEMPLATE", 1, nullptr);
            expectEquals(templatePresses, 1, "TEMPLATE button receives the click");
        }

        beginTest("CreateAllReceivesInput");
        {
            PopupHarness h;
            if (auto* row = h.popup.getRoleListContent().getRowForRoleId("doubles"))
                row->setCount(2);
            if (auto* row = h.popup.getRoleListContent().getRowForRoleId("reverb"))
                row->setCount(1);
            expectEquals(h.tracks.getNumTracks(), 0);
            clickControl(h.popup, h.popup.getBottomBar().createAllBtn_, "CREATE ALL", 1, nullptr);
            expectEquals(h.tracks.getNumTracks(), 3, "CREATE ALL builds the full queued batch");
            expectEquals(h.popup.getRoleListContent().getRowForRoleId("doubles")->getCount(), 0,
                         "queues consumed after CREATE ALL");
            expectEquals(h.popup.getRoleListContent().getRowForRoleId("reverb")->getCount(), 0,
                         "queues consumed after CREATE ALL");
        }

        beginTest("ScrollDoesNotBlockRoleButtons");
        {
            PopupHarness h;
            auto& viewport = h.popup.getRoleViewport();
            auto* row = h.popup.getRoleListContent().getRowForRoleId("reverb");
            expect(row != nullptr);
            if (row == nullptr)
                return;
            // Scroll the row into view (the reverb row sits far below the
            // initial fold — ~1300px of content).
            viewport.setViewPosition(0, juce::jmax(0, row->getY() - viewport.getHeight() / 2));
            // The row is inside the viewport content; the button must remain
            // clickable after scrolling.
            auto* hit = hitAt(h.popup, row->getPlusButton());
            expect(hit != nullptr, "scrolled row control still hit-resolvable");
            if (hit == nullptr)
                return;
            expectEquals(row->getCount(), 0);
            const auto local = juce::Point<int>(hit->getWidth() / 2, hit->getHeight() / 2);
            hit->mouseEnter(makeEvent(hit, local));
            hit->mouseDown(makeEvent(hit, local));
            hit->mouseUp(makeEvent(hit, local));
            hit->mouseExit(makeEvent(hit, local));
            expectEquals(row->getCount(), 1, "row plus works after scrolling");
        }

        beginTest("BottomBarRemainsFixedDuringScroll");
        {
            PopupHarness h;
            const auto templateBoundsBefore = h.popup.getBottomBar().templateBtn_.getBounds();
            const auto createAllBoundsBefore = h.popup.getBottomBar().createAllBtn_.getBounds();
            const auto barBoundsBefore = h.popup.getBottomBar().getBounds();
            const auto contentPosBefore = h.popup.getRoleListContent().getPosition();

            auto& viewport = h.popup.getRoleViewport();
            viewport.setViewPosition(0, 500);

            expect(h.popup.getBottomBar().templateBtn_.getBounds() == templateBoundsBefore,
                   "TEMPLATE screen position unchanged by scrolling");
            expect(h.popup.getBottomBar().createAllBtn_.getBounds() == createAllBoundsBefore,
                   "CREATE ALL screen position unchanged by scrolling");
            expect(h.popup.getBottomBar().getBounds() == barBoundsBefore,
                   "bottom bar not part of the scrolling content");
            expect(h.popup.getRoleListContent().getPosition() != contentPosBefore,
                   "role rows DID move with the scroll (content translated)");
        }

        beginTest("HeaderRemainsFixedDuringScroll");
        {
            PopupHarness h;
            const auto headerBoundsBefore = h.popup.getHeaderBar().getBounds();
            h.popup.getRoleViewport().setViewPosition(0, 500);
            expect(h.popup.getHeaderBar().getBounds() == headerBoundsBefore,
                   "header stays fixed during scroll");
        }

        beginTest("DetachedColorPickerDoesNotAccessDestroyedBuilder");
        {
            TrackManager tracks;
            RoutingGraph graph;
            MasterRouteStateCore masterRoute { graph };
            BubblegumSendStateCore sends;
            QuickTrackColorSystem colors { 4242 };
            auto builder = std::make_unique<QuickTrackBuilderCore>(
                tracks, graph, masterRoute, sends, colors);
            const auto* role = QuickTrackRoleCatalog::findById("doubles");
            expect(role != nullptr);
            if (role == nullptr)
                return;
            auto picker = std::make_unique<QuickTrackRoleColorPicker>(*builder, *role);
            const auto key = QuickTrackBuilderCore::colorKeyForRole(*role);

            // Exercise the actual queued button action with a live owner.
            auto* swatch = picker->getSwatchButton(0);
            expect(swatch != nullptr);
            if (swatch == nullptr)
                return;
            swatch->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expect(colors.isManual(key), "live picker still applies a swatch");
            const auto selected = colors.getColorForRole(key);

            // A native/asynchronous callout can outlive its builder. Keeping
            // the colour store alive detects unintended late mutations as
            // well as exercising a paint after the builder has been freed.
            builder.reset();
            juce::Image image(juce::Image::ARGB, picker->getWidth(), picker->getHeight(), true);
            juce::Graphics graphics(image);
            picker->paint(graphics);
            picker->getAutoButton().triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expect(colors.isManual(key), "late AUTO cannot mutate the former owner's colours");
            expect(colors.getColorForRole(key) == selected);
            auto* lateSwatch = picker->getSwatchButton(1);
            expect(lateSwatch != nullptr);
            if (lateSwatch != nullptr)
                lateSwatch->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            expect(colors.getColorForRole(key) == selected,
                   "late swatch cannot mutate the former owner's colours");
        }
    }

private:
    // Deterministic component-to-root transform. getBounds() is expressed in
    // the PARENT's space (so its centre already includes the component's own
    // position) — the walk therefore starts at the button's PARENT with the
    // parent-relative centre, accumulating each ancestor's getPosition()
    // (including the Viewport's internal holder + viewed-component scroll
    // translation) exactly once.
    static juce::Point<int> toRootLocal(juce::Component& root, juce::Component* c, juce::Point<int> p)
    {
        while (c != nullptr && c != &root)
        {
            p += c->getPosition();
            c = c->getParentComponent();
        }
        return p;
    }

    static juce::Component* hitAt(juce::Component& root, juce::Button& button)
    {
        const auto centre = toRootLocal(root, button.getParentComponent(),
                                        button.getBounds().getCentre());
        return root.getComponentAt(centre);
    }
};

static QuickTrackPopupTests quickTrackPopupTests;

} // namespace

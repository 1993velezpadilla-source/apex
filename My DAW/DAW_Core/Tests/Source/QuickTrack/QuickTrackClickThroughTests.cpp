// ===========================================================================
// QuickTrackClickThroughTests.cpp
// APEX Quick New Track — pointer-interaction regression suite.
//
// Reproduces the LIVE click-through bug against the REAL hierarchy:
//
//   scroll the arranger so the Master track (or a normal track / clip)
//   sits underneath the Quick New Track button's screen position
//   → click "+"
//   → the Quick New Track action fires EXACTLY once
//   → the underlying lane/clip/track receives NO selection or click event.
//
// The fix under test:
//   * the "+" is a real juce::Button child (exclusive pointer consumption)
//   * ArrangementViewCore keeps the pinned toolbar above dynamic children
//   * ArrangementViewCore ignores pointer events inside the pinned strip
//     region so they can never resolve to lanes underneath.
//
// Events are synthesized the canonical repository way (G10EditorHookTests
// pattern): a real MouseInputSource + direct component mouseDown/mouseUp
// delivery after resolving the target through the real component tree.
// ===========================================================================
#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementToolbarCore.h"
#include "../../../Source/TrackCore/Track.h"
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/RoutingCore/RoutingGraph.h"
#include "../../../Source/UICore/TrackList.h"

namespace
{

using namespace ArrangementEditor;

class QuickTrackClickThroughTests final : public juce::UnitTest
{
public:
    QuickTrackClickThroughTests() : juce::UnitTest("QuickTrack.ClickThrough", "APEX.QuickTrack") {}

    // Counts events DELIVERED to the arrangement core (the "lane" layer).
    struct LaneEventCounter final : public juce::MouseListener
    {
        int downs = 0;
        int ups = 0;
        int drags = 0;
        void mouseDown(const juce::MouseEvent&) override { ++downs; }
        void mouseUp(const juce::MouseEvent&) override { ++ups; }
        void mouseDrag(const juce::MouseEvent&) override { ++drags; }
    };

    struct Hierarchy
    {
        DAW::TrackManager tracks;
        DAW::ClipManager clips;
        DAW::RoutingGraph graph;
        ArrangementViewCore core;
        LaneEventCounter laneCounter;
        int addTrackFires = 0;      // Quick New Track callback count
        int clipSelectedFires = 0;  // underlying clip selection count

        Hierarchy()
        {
            core.setAudioEngineBridge(&clips, nullptr, &tracks, 44100.0);
            core.setRoutingGraph(&graph);
            core.setSize(900, 600);
            // Standalone (no desktop/parent): JUCE hit-testing requires the
            // visible flag, so mark the hierarchy visible explicitly.
            core.setVisible(true);
            // NOTE: not added to the desktop — Component::getComponentAt is
            // pure bounds/visibility math and works headless; this avoids
            // creating/destroying native windows per scenario without a
            // running message loop (console test runner).

            tracks.createMasterTrack();
            auto* drums = tracks.createTrack("Drums");
            auto* bass = tracks.createTrack("Bass");
            graph.addNode(drums->getName(), DAW::RoutingNodeType::Track, drums->getID());
            graph.addNode(bass->getName(), DAW::RoutingNodeType::Track, bass->getID());

            core.getToolbar().onAddTrackRequested = [this] { ++addTrackFires; };
            core.onEngineClipSelected = [this](DAW::Clip&) { ++clipSelectedFires; };
            core.addMouseListener(&laneCounter, false);
        }

        ~Hierarchy()
        {
            core.removeMouseListener(&laneCounter);
        }

        void scrollTo(int scrollY) { core.setViewportScrollOffset(0, scrollY); }

        /** Center of the Quick New Track button in CORE-local coordinates. */
        juce::Point<int> addButtonCorePoint()
        {
            auto& toolbar = core.getToolbar();
            return toolbar.getPosition() + toolbar.getAddTrackButtonBounds().getCentre();
        }

        /** Place a clip on an engine track lane so the lane has an
         *  interactive child component (the pass-through carrier). The clip
         *  is backed by a real DAW::Clip so a selection would be observable
         *  through onEngineClipSelected. */
        void addClipOnEngineTrack(int engineTrackIndex)
        {
            auto* engineClip = clips.createAudioClip("under-button clip", juce::File());
            if (engineClip == nullptr)
                return;
            auto model = ArrangementClipModel::createNew(engineTrackIndex + 1, 0.0, 20.0);
            model.clipName = "under-button clip";
            core.addOrUpdateEngineClipModel(model, engineClip->getID());
        }
    };

    void runTest() override
    {
        // One real mouse source for event synthesis (G10 canonical pattern).
        // Some console/CI environments expose no source until a native peer
        // exists; a single persistent peer window forces one — and is torn
        // down with the test run, so no window is created per scenario.
        std::unique_ptr<juce::Component> peerWindow;
        auto* mouseSource = juce::Desktop::getInstance().getMouseSource(0);
        if (mouseSource == nullptr)
        {
            peerWindow = std::make_unique<juce::Component>();
            peerWindow->setSize(64, 64);
            peerWindow->addToDesktop(0);
            mouseSource = juce::Desktop::getInstance().getMouseSource(0);
        }
        expect(mouseSource != nullptr, "mouse source available for event synthesis");
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

        // Deliver down+up to the deepest component at `corePoint` — exactly
        // what JUCE's dispatch would do.
        auto clickAt = [this, &makeEvent](juce::Point<int> corePoint)
        {
            auto* h = hierarchy_;
            if (h == nullptr)
                return;
            auto* hit = h->core.getComponentAt(corePoint);
            if (hit == nullptr)
                return;
            const auto local = hit->getLocalPoint(&h->core, corePoint);
            hit->mouseDown(makeEvent(hit, local));
            hit->mouseUp(makeEvent(hit, local));
        };

        runScenario("ClickDoesNotPassThrough", 200, [this, &clickAt]
        {
            clickAt(hierarchy_->addButtonCorePoint());
        });

        runScenario("MasterUnderButtonDoesNotReceiveClick", 100, [this, &clickAt]
        {
            juce::Logger::writeToLog("[QTT-DIAG] core=" + hierarchy_->core.getLocalBounds().toString()
                + " visible=" + juce::String(hierarchy_->core.isVisible() ? 1 : 0)
                + " toolbarBounds=" + hierarchy_->core.getToolbar().getBounds().toString()
                + " addBtn=" + hierarchy_->core.getToolbar().getAddTrackButtonBounds().toString()
                + " scrollY=" + juce::String(hierarchy_->core.getToolbar().getY()));
            const auto point = hierarchy_->addButtonCorePoint();
            juce::Logger::writeToLog("[QTT-DIAG] point=" + point.toString());
            auto* hit = hierarchy_->core.getComponentAt(point);
            expect(hit != nullptr, "hit resolved");
            if (hit != nullptr)
            {
                expect(hit->findParentComponentOfClass<ArrangementToolbarCore>() != nullptr
                       || dynamic_cast<ArrangementToolbarCore*>(hit) != nullptr,
                       "click resolves into the toolbar layer, never the lane");
                clickAt(point);
            }
        });

        runScenario("TrackUnderButtonDoesNotReceiveClick", 200, [this, &clickAt]
        {
            const auto point = hierarchy_->addButtonCorePoint();
            auto* hit = hierarchy_->core.getComponentAt(point);
            expect(hit != nullptr, "hit resolved");
            if (hit != nullptr)
            {
                expect(hit->findParentComponentOfClass<ArrangementToolbarCore>() != nullptr
                       || dynamic_cast<ArrangementToolbarCore*>(hit) != nullptr,
                       "click resolves into the toolbar layer, never the lane");
                clickAt(point);
            }
        });

        runScenario("MouseDownDoesNotPassThrough", 100, [this, &makeEvent]
        {
            const auto point = hierarchy_->addButtonCorePoint();
            auto* hit = hierarchy_->core.getComponentAt(point);
            expect(hit != nullptr, "hit resolved");
            if (hit == nullptr)
                return;
            const auto local = hit->getLocalPoint(&hierarchy_->core, point);
            hit->mouseDown(makeEvent(hit, local));
            expectEquals(hierarchy_->addTrackFires, 0, "button click completes on release only");
            expectEquals(hierarchy_->laneCounter.downs, 0, "lane received no mouseDown");
            hit->mouseUp(makeEvent(hit, local));
            expectEquals(hierarchy_->addTrackFires, 1, "release completes the exclusive click");
            expectEquals(hierarchy_->laneCounter.ups, 0, "lane received no mouseUp");
        });

        runScenario("MouseUpDoesNotPassThrough", 100, [this, &makeEvent]
        {
            const auto point = hierarchy_->addButtonCorePoint();
            auto* hit = hierarchy_->core.getComponentAt(point);
            expect(hit != nullptr, "hit resolved");
            if (hit == nullptr)
                return;
            const auto local = hit->getLocalPoint(&hierarchy_->core, point);
            hit->mouseUp(makeEvent(hit, local)); // orphan release
            expectEquals(hierarchy_->addTrackFires, 0, "orphan release never creates");
            expectEquals(hierarchy_->laneCounter.ups, 0, "orphan release never reaches lane");
        });

        runScenario("PopupAnchorsToButton", 100, [this]
        {
            auto& toolbar = hierarchy_->core.getToolbar();
            const auto anchor = toolbar.getAddTrackButtonBounds();
            expectEquals(anchor.getWidth(), 44, "touch-safe anchor width");
            expectEquals(anchor.getHeight(), 40, "touch-safe anchor height");
            expect(!anchor.isEmpty(), "anchor rect valid");
            expect(toolbar.getAddTrackButtonScreenBounds() == toolbar.localAreaToGlobal(anchor),
                   "screen anchor self-consistent with local anchor");
            // The anchor sits inside the pinned strip at the scroll offset.
            const auto coreAnchor = anchor + toolbar.getPosition();
            expect(coreAnchor.getY() >= 100 && coreAnchor.getY() < 148,
                   "anchor inside pinned toolbar strip after scrolling");
        });

        runScenario("PopupConsumesInput", 100, [this, &clickAt]
        {
            // A CallOutBox-style overlay above the button must consume every
            // pointer interaction — nothing reaches the button or the lanes.
            juce::Component overlay;
            overlay.setBounds(hierarchy_->core.getToolbar().getAddTrackButtonBounds()
                              + hierarchy_->core.getToolbar().getPosition());
            overlay.setInterceptsMouseClicks(true, true);
            hierarchy_->core.addAndMakeVisible(overlay);
            overlay.toFront(false);

            const auto point = hierarchy_->addButtonCorePoint();
            auto* hit = hierarchy_->core.getComponentAt(point);
            expect(hit == &overlay, "overlay is the hit-test winner");
            clickAt(point); // delivered to the overlay
            expectEquals(hierarchy_->addTrackFires, 0, "overlay consumed the click");
            expectEquals(hierarchy_->laneCounter.downs, 0, "lanes received nothing");
            expectEquals(hierarchy_->laneCounter.ups, 0, "lanes received nothing");

            hierarchy_->core.removeChildComponent(&overlay);
            clickAt(point); // overlay gone → button works again
            expectEquals(hierarchy_->addTrackFires, 1, "interaction restored after overlay removal");
        });

        beginTest("StripGapDoesNotReachLane");
        {
            // Deterministic pass-through reproduction: a clip renderer sits in
            // the pinned-strip gap BEYOND the toolbar's right edge. Clicking it
            // must never select the clip (the strip guard absorbs the event).
            Hierarchy h;
            h.scrollTo(200);
            h.addClipOnEngineTrack(0); // Drums lane under the strip

            const auto gapPoint = juce::Point<int>(770, 220); // strip region, beyond toolbar
            auto* hit = h.core.getComponentAt(gapPoint);
            expect(hit != nullptr, "clip/lane hit resolved at gap point");
            if (hit != nullptr)
            {
                const auto local = hit->getLocalPoint(&h.core, gapPoint);
                hit->mouseDown(makeEvent(hit, local));
                hit->mouseUp(makeEvent(hit, local));
            }
            expectEquals(h.addTrackFires, 0, "gap click is not a quick-track click");
            expectEquals(h.clipSelectedFires, 0, "underlying clip NEVER selected from strip");
            expectEquals(h.laneCounter.downs, 0, "lane mouseDown never fired");
            expectEquals(h.laneCounter.ups, 0, "lane mouseUp never fired");
        }

        beginTest("ViewportWrappedHierarchyDoesNotPassThrough");
        {
            // The REAL application hosts ArrangementViewCore as the VIEWED
            // COMPONENT of a juce::Viewport (TimelineViewport in
            // MainComponent). This scenario wraps the core exactly that way,
            // scrolls the viewport so a lane sits underneath the pinned
            // toolbar strip, and proves the hit-test at the visible "+"
            // resolves into the toolbar layer (never the lane) and the click
            // reaches "+" exactly once with nothing reaching the lane.
            juce::Viewport viewport;
            Hierarchy h;
            viewport.setViewedComponent(&h.core, false); // MainComponent pattern
            viewport.setSize(900, 300);
            viewport.setVisible(true);
            h.core.setSize(900, 600); // content taller than the view

            // Mirror the viewport position into the core the way
            // MainComponent::scrollBarMoved does.
            viewport.setViewPosition(0, 100);
            h.core.setViewportScrollOffset(viewport.getViewPositionX(),
                                           viewport.getViewPositionY());

            // Visible "+" center in VIEWPORT coordinates.
            const auto corePoint = h.addButtonCorePoint();
            const auto viewportPoint = corePoint - juce::Point<int>(viewport.getViewPositionX(),
                                                                    viewport.getViewPositionY());

            auto* hit = viewport.getComponentAt(viewportPoint);
            expect(hit != nullptr, "viewport hit resolves at the visible +");
            if (hit != nullptr)
            {
                const bool inToolbar = hit->findParentComponentOfClass<ArrangementToolbarCore>() != nullptr
                                       || dynamic_cast<ArrangementToolbarCore*>(hit) != nullptr;
                expect(inToolbar, "viewport hit-test resolves into the toolbar layer, never the lane");
                const auto local = hit->getLocalPoint(&viewport, viewportPoint);
                hit->mouseDown(makeEvent(hit, local));
                hit->mouseUp(makeEvent(hit, local));
            }
            expectEquals(h.addTrackFires, 1, "viewport click reaches + exactly once");
            expectEquals(h.clipSelectedFires, 0, "underlying content never reacts");
            expectEquals(h.laneCounter.downs, 0, "lane never receives mouseDown through the viewport");
            expectEquals(h.laneCounter.ups, 0, "lane never receives mouseUp through the viewport");

            viewport.setViewedComponent(nullptr, false);
        }

        beginTest("TrackListQuickAdd");
        {
            // THE REAL PRODUCTION CONTROL the user clicks: the "+" under the
            // TRACKS label in the LEFT track panel (DAW::TrackList). The
            // pinned header is a real overlay child (TrackListHeaderBar) and
            // the "+" is a real button inside it. Rows scroll UNDER the
            // pinned header; the click must reach the quick-add callback
            // exactly once and NEVER select/affect the row underneath.
            //
            // The OLD production implementation (a painted "+" hit-tested in
            // TrackList::mouseDown) FAILS this test: with a row scrolled
            // under the "+", the row is the deepest hit-test winner, so the
            // quick-add callback never fires.
            DAW::TrackManager tracks;
            tracks.createMasterTrack();
            tracks.createTrack("Audio 1");
            tracks.createTrack("Audio 2");

            DAW::TrackList list(tracks);
            list.setSize(300, 400);
            list.setVisible(true);

            int quickAddFires = 0;
            int rowSelectionFires = 0;
            list.onQuickAddTrackRequested = [&] { ++quickAddFires; };
            list.onTrackSelectedWithModifiers = [&](const DAW::TrackID&, const juce::ModifierKeys&)
            {
                ++rowSelectionFires;
            };

            // Scroll a row under the pinned header (rows start at
            // y = 80 - scrollOffset; master row height 108 → spans
            // [-10, 98) at offset 90, covering the "+" at y [10, 38)).
            list.setScrollOffset(90);

            const auto localPoint = juce::Point<int>(list.getWidth() - 27, 24); // "+" center
            auto* hit = list.getComponentAt(localPoint);
            expect(hit != nullptr, "TRACKS + hit resolves");
            if (hit != nullptr)
            {
                const bool inHeader = hit->findParentComponentOfClass<DAW::TrackList::TrackListHeaderBar>() != nullptr
                                      || dynamic_cast<DAW::TrackList::TrackListHeaderBar*>(hit) != nullptr;
                expect(inHeader, "TRACKS + resolves into the pinned header overlay, never a row");
                const auto local = hit->getLocalPoint(&list, localPoint);
                hit->mouseDown(makeEvent(hit, local));
                hit->mouseUp(makeEvent(hit, local));
            }
            expectEquals(quickAddFires, 1, "TRACKS + quick-add callback fires exactly once");
            expectEquals(rowSelectionFires, 0, "row underneath never receives the click");
        }
    }

private:
    void runScenario(const char* name, int scrollY, const std::function<void()>& body)
    {
        beginTest(name);
        auto owned = std::make_unique<Hierarchy>();
        hierarchy_ = owned.get();
        hierarchy_->scrollTo(scrollY);
        body();
        hierarchy_ = nullptr;
        owned.reset();
    }

    Hierarchy* hierarchy_ = nullptr;
};

static QuickTrackClickThroughTests quickTrackClickThroughTests;

} // namespace

#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/ParametricEQCore/ParametricEQSpectrumGrabCore.h"
#include "../../../Source/ParametricEQCore/ParametricEQMatchCore.h"
#include "../../../Source/ParametricEQUI/ParametricEQEditor.h"
#include "../../../Source/ParametricEQUI/ApexQ4Editor.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{

using namespace APEX::ParametricEQ;

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

void enableBell (Processor& processor, int band, float frequency, float gain, float q)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
}

void processBlocks (Processor& processor, int blocks = 4)
{
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
        processor.processBlock (buffer, midi);
}

struct SizeCase
{
    int width;
    int height;
    ParametricEQEditor::LayoutMode expected;
};

constexpr std::array<SizeCase, 8> kSizes {{
    { 320, 568, ParametricEQEditor::LayoutMode::CompactPhone },
    { 480, 800, ParametricEQEditor::LayoutMode::LargePhone },
    { 640, 400, ParametricEQEditor::LayoutMode::LargePhone },
    { 700, 500, ParametricEQEditor::LayoutMode::Tablet },
    { 768, 1024, ParametricEQEditor::LayoutMode::Tablet },
    { 900, 300, ParametricEQEditor::LayoutMode::LargePhone },
    { 1280, 720, ParametricEQEditor::LayoutMode::Desktop },
    { 1920, 1080, ParametricEQEditor::LayoutMode::LargeDesktop }
}};

} // namespace

class ParametricEQEditorTests final : public juce::UnitTest
{
public:
    ParametricEQEditorTests()
        : UnitTest ("ParametricEQ.Editor", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testApexQ4CleanRoomProductionEditor();
        testManualGraphCreationAndModifierModes();
        testNativeMultiSelectionRelativeGroupedDragAndHistory();
        testTouchModeGroupLinkingAndOuterDynamicRing();
        testGainDialDynamicRingAndBandLimitedTriggerWorkflow();
        testPopoverFollowsSelectedNodeWithoutOccludingSpectrum();
        testKeyboardGroupedEditingAndDynamicTooltip();
        testExpandedDynamicPanelAndHostAutomation();
        testRealAudioDynamicMeterAndDirectRangeRing();
        testAltWheelDynamicRangeAndReverseGainTrade();
        testResponsiveLayoutMatrix();
        testAdversarialCriticBandSwitches();
        testStaticEvoEyeStamp();
        testPurpleAnalyzerAndPresetRestore();
        testPrismOffscreenRenderAndSnapshots();
        testDoubleClickCreatesNativeBandAndWheelEditsQ();
        testNodeDragUpdatesParametersThroughRealHierarchy();
        testPlacementButtonsAreTouchAccessibleAndFunctional();
        testAuditionHoldGestureLifecycle();
        testLongPressCreatesBand();
        testHeldNodeNeverTriggersImplicitAudition();
        testSketchGestureCreatesBands();
        testSpectrumGrabAndTouchToBand();
        testNativeBandCopyPasteAcrossInstances();
        testMeasuredEqMatchAndGuidedNativeApply();
        testSpectralInspectorAndDeferredPdc();
        testPhaseSelectorDrivesCanonicalParameter();
    }

private:
    void testGainDialDynamicRingAndBandLimitedTriggerWorkflow()
    {
        beginTest ("manual dynamic GAIN ring: drag creates real hosted"
                   " compression/expansion and band-key detector, CLEAR"
                   " resets; non-gain filter types never expose range ring");
        for (const auto size : { juce::Point<int> { 320, 568 },
                                 juce::Point<int> { 480, 800 },
                                 juce::Point<int> { 900, 300 },
                                 juce::Point<int> { 1280, 720 } })
        {
            auto processor = std::make_unique<Processor>();
            processor->prepareToPlay (48000.0, 128);
            enableBell (*processor, 0, 880.0f, 4.0f, 2.0f);
            auto editor = std::make_unique<ApexQ4Editor> (*processor);
            editor->setSize (size.x, size.y);
            editor->selectBandForTesting (0);
            auto* range = processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Range));
            auto* enabled = processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Enable));
            auto* detector = processor->getParametricEQParameter (
                dynamicFilterParameterIndex (0));
            const auto* freq = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency));
            const auto* gain = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain));
            const float initialHz = freq->getUnitsValue();
            const float initialGain = gain->getUnitsValue();

            expect (editor->gainRingEditableForTesting(),
                "Bell selected => outer gain dial dynamic ring is interactive");
            const auto gainBounds = editor->gainKnobBoundsForTesting();
            const float diameter = static_cast<float> (
                std::min (gainBounds.getWidth() - 12,
                          gainBounds.getHeight() - 39));
            const float radius = std::max (12.0f, diameter * 0.5f);
            expect (editor->gainRingTouchTargetForTesting ({
                    gainBounds.getWidth() * 0.5f + radius + 7.0f,
                    std::max (17.0f, radius + 2.0f) }),
                "real mouse hit-test catches the OUTER annulus separately"
                " from normal static gain knob at each tested viewport");
            expect (! editor->gainRingTouchTargetForTesting ({
                    gainBounds.getWidth() * 0.5f,
                    std::max (17.0f, radius + 2.0f) }),
                "inner static GAIN drag is not captured by dynamic ring");

            editor->dragGainRangeForTesting (-7.0f);
            expect (enabled->getBool(),
                "dragging a static band's GAIN outer ring activates DSP");
            expect (detector->getBool(),
                "new dynamics defaults to real per-band bandpass key");
            expectWithinAbsoluteError (
                static_cast<double> (range->getUnitsValue()), +7.0, 0.03,
                "manual-style -7 dB compression UI maps to the positive"
                " original APEX hosted range, preserving old automation");
            expectWithinAbsoluteError (
                static_cast<double> (editor->gainRingRangeForTesting()),
                -7.0, 0.03,
                "GUI ring follows hosted range after automation");
            expectWithinAbsoluteError (
                static_cast<double> (gain->getUnitsValue()),
                static_cast<double> (initialGain), 0.005,
                "dynamic ring does not move inner static GAIN");
            expectWithinAbsoluteError (
                static_cast<double> (freq->getUnitsValue()),
                static_cast<double> (initialHz), 0.005,
                "dynamic ring does not move selected frequency");
            expect (! processor->isAuditionHoldingForTesting(),
                "dynamic GAIN ring never triggers solo/audition");

            editor->clearSelectedDynamicsForTesting();
            expect (! enabled->getBool(),
                "Clear Dynamics actually disables real hosted DSP");
            expectWithinAbsoluteError (
                static_cast<double> (range->getUnitsValue()), 0.0, 0.01,
                "Clear Dynamics returns hosted range to neutral");

            setUnits (*processor, parameterIndex (
                0, BandParameterOffset::Shape),
                static_cast<float> (FilterShape::LowCut));
            editor->refreshForTesting();
            expect (! editor->gainRingEditableForTesting(),
                "cut filters must not expose an invalid dynamics range");
            editor->dragGainRangeForTesting (5.0f);
            expect (! enabled->getBool(),
                "disabled cut filter ring cannot enable dynamics");
            expectWithinAbsoluteError (
                static_cast<double> (range->getUnitsValue()), 0.0, 0.01);
        }
    }

    void testPopoverFollowsSelectedNodeWithoutOccludingSpectrum()
    {
        beginTest ("PASS 11 true contextual control placement: near the"
                   " selected node, never bottom-docked or occluding bands");
        for (const auto size : { juce::Point<int> { 480, 800 },
                                 juce::Point<int> { 640, 400 },
                                 juce::Point<int> { 1280, 720 },
                                 juce::Point<int> { 1920, 1080 } })
        {
            auto processor = std::make_unique<Processor>();
            processor->prepareToPlay (48000.0, 128);
            enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
            enableBell (*processor, 1, 5200.0f, -5.0f, 3.0f);
            auto editor = std::make_unique<ApexQ4Editor> (*processor);
            editor->setSize (size.x, size.y);
            juce::Colour firstBandInk;
            for (int selected = 0; selected < 2; ++selected)
            {
                editor->selectBandForTesting (selected);
                // PASS 13: controls now follow the actual selected band
                // color, not the gold aggregate transfer curve. On screen
                // this removes the ambiguity between band gain dials and
                // the overall EQ response during live FFT monitoring.
                const auto dialInk = editor->bandDialColourForTesting (0);
                expect (dialInk == editor->bandDialColourForTesting (1)
                        && dialInk == editor->bandDialColourForTesting (2),
                    "FREQ / GAIN / Q accents match the selected filter node");
                expect (dialInk != editor->eqTransferColourForTesting(),
                    "individual EQ band is visually distinct from overall EQ");
                if (selected == 0) firstBandInk = dialInk;
                else expect (dialInk != firstBandInk,
                    "changing the selected band must change all knob accents");
                const auto node = editor->nodeHitAreaForTesting (selected)
                    .getCentre();
                const auto panel = editor->panelBoundsForTesting();
                expect (editor->graphBoundsForTesting().contains (panel),
                    "contextual panel stays inside graph on every viewport");
                const int verticalDistance = std::abs (
                    panel.getCentreY() - node.y);
                expect (verticalDistance <= 210,
                    "FREQ/GAIN/Q controls belong near the selected node,"
                    " not hundreds of pixels away at the canvas bottom");
                for (int band = 0; band < 2; ++band)
                    expect (! panel.intersects (
                        editor->nodeHitAreaForTesting (band)),
                        "near-node HUD must not hide either filter handle");
                expect (! panel.intersects (
                    editor->selectedTagBoundsForTesting()),
                    juce::String ("near-node HUD must not hide selected"
                                  " value tooltip at ")
                    + juce::String (size.x) + "x"
                    + juce::String (size.y) + " selected band "
                    + juce::String (selected));
                if (size.x == 640 && size.y == 400 && selected == 1)
                {
                    // Real PASS 11 CI failure: the former 29px bottom
                    // reserve forced one card to hide the selected tooltip.
                    // The new tight 8px reserve must preserve BOTH the
                    // tooltip above and the footer below the card.
                    expect (panel.getBottom() <=
                        editor->graphBoundsForTesting().getBottom() - 8,
                        "short-height popover never enters the footer");
                    expect (panel.getY() >=
                        editor->selectedTagBoundsForTesting().getBottom(),
                        "640x400 selected band #1 remains below its readout");
                }
            }
        }
        // When expanded Dynamics needs space on a medium phone, preserve
        // visibility of BOTH cards and keep every hit target unobstructed.
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
        enableBell (*processor, 1, 5200.0f, -5.0f, 3.0f);
        ApexQ4Editor editor (*processor);
        editor.setSize (480, 800);
        editor.selectBandForTesting (0);
        editor.expandDynamicForTesting (true);
        expect (editor.primaryHudVisibleForTesting());
        expect (! editor.panelBoundsForTesting().intersects (
            editor.dynamicPanelBoundsForTesting()),
            "opened Dynamics and contextual band card cannot overlap");
        editor.expandDynamicForTesting (false);
        const auto selectedNode = editor.nodeHitAreaForTesting (0)
            .getCentre();
        expect (std::abs (editor.panelBoundsForTesting().getCentreY()
                         - selectedNode.y) <= 210,
            "closing Dynamics restores local, node-following HUD placement");
    }

    void testKeyboardGroupedEditingAndDynamicTooltip()
    {
        beginTest ("PASS 9 real JUCE keyboard: grouped arrow editing,"
                   " single-step delete/undo/redo and long dynamic readouts");
        for (const auto size : { juce::Point<int> { 320, 568 },
                                 juce::Point<int> { 480, 800 },
                                 juce::Point<int> { 1280, 720 } })
        {
            auto processor = std::make_unique<Processor>();
            processor->prepareToPlay (48000.0, 128);
            enableBell (*processor, 0, 930.0f, 6.0f, 2.0f);
            enableBell (*processor, 1, 2100.0f, -3.0f, 2.0f);
            auto editor = std::make_unique<ApexQ4Editor> (*processor);
            editor->setSize (size.x, size.y);
            editor->selectBandForTesting (0);
            auto* f0 = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency));
            auto* f1 = processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Frequency));
            auto* g0 = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain));
            auto* g1 = processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Gain));
            const auto originalTag = editor->selectedTagBoundsForTesting();
            expect (editor->graphBoundsForTesting().contains (originalTag));
            setUnits (*processor, dynamicParameterIndex (
                0, DynamicBandOffset::Enable), 1.0f);
            editor->refreshForTesting();
            const auto dynamicTag = editor->selectedTagBoundsForTesting();
            expect (editor->graphBoundsForTesting().contains (dynamicTag),
                "signed Dynamic EQ readout must fit within the graph");
            expect (dynamicTag.getWidth() > originalTag.getWidth(),
                "Dynamic gain readout must receive actual extra label width");
            setUnits (*processor, dynamicParameterIndex (
                0, DynamicBandOffset::Enable), 0.0f);
            editor->refreshForTesting();

            editor->setTouchMultiSelectForTesting (true);
            const auto second = editor->nodeHitAreaForTesting (1)
                .getCentre().toFloat();
            editor->injectGraphPointerForTesting (second, 0);
            editor->injectGraphPointerForTesting (second, 2);
            expectEquals (editor->selectedBandCountForTesting(), 2);
            const auto press = [&] (int code,
                                    juce::ModifierKeys modifier = {})
            {
                return editor->sendKeyboardShortcutForTesting (
                    juce::KeyPress (code, modifier, 0));
            };
            expect (press (juce::KeyPress::upKey),
                "arrow up is handled by the real editor callback");
            expectWithinAbsoluteError (
                static_cast<double> (g0->getUnitsValue()), 7.0, 0.06,
                "arrow up adds 1 dB to first linked band");
            expectWithinAbsoluteError (
                static_cast<double> (g1->getUnitsValue()), -2.0, 0.06,
                "arrow up adds 1 dB to second linked band");

            expect (press (juce::KeyPress::leftKey));
            const double oneSemitone = std::exp2 (-1.0 / 12.0);
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                930.0 * oneSemitone, 3.0);
            expectWithinAbsoluteError (
                static_cast<double> (f1->getUnitsValue()),
                2100.0 * oneSemitone, 5.0);
            expect (press (juce::KeyPress::rightKey,
                juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
            const double fineFrequency = std::exp2 (0.1 / 12.0);
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                930.0 * oneSemitone * fineFrequency, 4.0,
                "shift+right uses a fine logarithmic pitch step");
            expect (press (juce::KeyPress::downKey,
                juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
            expectWithinAbsoluteError (
                static_cast<double> (g0->getUnitsValue()), 6.9, 0.06,
                "shift+down uses 0.1 dB instead of 1 dB");

            const float beforeDeleteHz = f0->getUnitsValue();
            expect (press (juce::KeyPress::deleteKey));
            expectEquals (editor->activeBandsForTesting(), 0,
                "Delete disables all linked hosted EQ bands together");
            expect (press ('z',
                juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
            expectEquals (editor->activeBandsForTesting(), 2,
                "one Ctrl+Z restores ALL deleted linked bands");
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                static_cast<double> (beforeDeleteHz), 1.0);
            expect (press ('y',
                juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
            expectEquals (editor->activeBandsForTesting(), 0,
                "Ctrl+Y reapplies grouped deletion");
            expect (press ('z',
                juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
            editor->selectBandForTesting (0);
            editor->setTouchMultiSelectForTesting (true);
            expect (press (juce::KeyPress::escapeKey));
            expect (! editor->touchMultiSelectForTesting(),
                "Escape explicitly exits the touch link mode");
            expectEquals (editor->selectedBandCountForTesting(), 0,
                "Escape clears all selection state");
            const float afterEscape = f0->getUnitsValue();
            expect (! press (juce::KeyPress::upKey),
                "arrows without a selected band are not stolen from host");
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                static_cast<double> (afterEscape), 0.01);

            // The preset menu's RESET TO FLAT must behave as one atomic
            // undoable operation; it previously bypassed history entirely.
            editor->resetAllBandsForTesting();
            expectEquals (editor->activeBandsForTesting(), 0,
                "reset to flat disables all hosted EQ bands");
            expect (press ('z',
                juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)));
            expectEquals (editor->activeBandsForTesting(), 2,
                "one Undo restores every band after preset reset");
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                static_cast<double> (afterEscape), 0.5,
                "reset/undo keeps the actual hosted musical frequency");
        }
    }

    void testTouchModeGroupLinkingAndOuterDynamicRing()
    {
        beginTest ("PASS 8 touch-first group: tap link/unlink, swipe drag,"
                   " accidental insertion guard and outer 23px ring");
        // Synthetic events reach the real production JUCE callbacks. Actual
        // touchscreen hardware acceptance is a SEPARATE release gate.
        for (const auto viewport : { juce::Point<int> { 320, 568 },
                                     juce::Point<int> { 480, 800 },
                                     juce::Point<int> { 1280, 720 } })
        {
            auto processor = std::make_unique<Processor>();
            processor->prepareToPlay (48000.0, 128);
            enableBell (*processor, 0, 930.0f, 6.0f, 2.0f);
            enableBell (*processor, 1, 2100.0f, -3.0f, 2.0f);
            auto editor = std::make_unique<ApexQ4Editor> (*processor);
            editor->setSize (viewport.x, viewport.y);
            editor->selectBandForTesting (0);
            editor->setTouchMultiSelectForTesting (true);
            expect (editor->touchMultiSelectForTesting(),
                    "MORE touch-link mode can activate at each viewport");
            const auto tap = [&] (juce::Point<float> pt)
            {
                editor->injectGraphPointerForTesting (pt, 0);
                editor->injectGraphPointerForTesting (pt, 2);
            };

            const auto one = editor->nodeHitAreaForTesting (1)
                .getCentre().toFloat();
            tap (one);
            expectEquals (editor->selectedBandCountForTesting(), 2,
                "tap links the second band without Ctrl/Cmd on touchscreen");
            expect (editor->isBandSelectedForTesting (0)
                    && editor->isBandSelectedForTesting (1));
            const int before = editor->activeBandsForTesting();
            tap ({ 22.0f, static_cast<float> (
                editor->graphBoundsForTesting().getY() + 85) });
            expectEquals (editor->activeBandsForTesting(), before,
                "blank graph touch in LINK mode must not create filter");
            expectEquals (editor->selectedBandCountForTesting(), 2,
                "blank graph touch must not unlink already selected bands");

            const auto zero = editor->nodeHitAreaForTesting (0)
                .getCentre().toFloat();
            editor->injectGraphPointerForTesting (zero, 0);
            editor->injectGraphPointerForTesting (
                { zero.x + 19.0f, zero.y - 12.0f }, 1);
            editor->injectGraphPointerForTesting (
                { zero.x + 19.0f, zero.y - 12.0f }, 2);
            auto* f0 = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency));
            auto* f1 = processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Frequency));
            auto* g0 = processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain));
            auto* g1 = processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Gain));
            expect (f0->getUnitsValue() > 930.0f
                    && f1->getUnitsValue() > 2100.0f,
                "swiping a linked node edits both real hosted frequencies");
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()) / 930.0,
                static_cast<double> (f1->getUnitsValue()) / 2100.0, 0.03,
                "linked touch move preserves logarithmic frequency intervals");
            expectWithinAbsoluteError (
                static_cast<double> (g0->getUnitsValue() - 6.0f),
                static_cast<double> (g1->getUnitsValue() + 3.0f), 0.1,
                "linked touch move preserves dB offsets");
            expect (! processor->isAuditionHoldingForTesting(),
                "touch group swipe never accidentally starts SOLO");
            editor->undoForTesting();
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()), 930.0, 3.0,
                "one undo restores primary touch drag");
            expectWithinAbsoluteError (
                static_cast<double> (f1->getUnitsValue()), 2100.0, 5.0,
                "one undo restores all linked bands");
            tap (editor->nodeHitAreaForTesting (1).getCentre().toFloat());
            expectEquals (editor->selectedBandCountForTesting(), 1,
                "second tap unlinks a band but leaves primary selected");
            editor->setTouchMultiSelectForTesting (false);
            expect (! editor->touchMultiSelectForTesting());
            expectEquals (editor->selectedBandCountForTesting(), 1);

            setUnits (*processor, dynamicParameterIndex (
                0, DynamicBandOffset::Enable), 1.0f);
            setUnits (*processor, dynamicParameterIndex (
                0, DynamicBandOffset::Range), 4.0f);
            editor->selectBandForTesting (0);
            const auto ringCentre = editor->nodeHitAreaForTesting (0)
                .getCentre().toFloat();
            const auto outer = ringCentre + juce::Point<float> { 23.0f, 0.0f };
            const float oldFrequency = f0->getUnitsValue();
            const float oldGain = g0->getUnitsValue();
            auto* range = processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Range));
            editor->injectGraphPointerForTesting (outer, 0);
            editor->injectGraphPointerForTesting (
                outer + juce::Point<float> { 0.0f, -29.0f }, 1);
            editor->injectGraphPointerForTesting (
                outer + juce::Point<float> { 0.0f, -29.0f }, 2);
            expect (range->getUnitsValue() > 7.0f,
                "outer ring 23px radius remains editable, not a dead zone");
            expectWithinAbsoluteError (
                static_cast<double> (f0->getUnitsValue()),
                static_cast<double> (oldFrequency), 0.1,
                "ring swipe never moves static EQ frequency");
            expectWithinAbsoluteError (
                static_cast<double> (g0->getUnitsValue()),
                static_cast<double> (oldGain), 0.01,
                "ring swipe never changes static band gain");
            expect (! processor->isAuditionHoldingForTesting(),
                "outer ring must not trigger vocal mute/SOLO");
        }
    }

    void testAltWheelDynamicRangeAndReverseGainTrade()
    {
        beginTest ("manual: Alt-wheel dynamics, Alt+Ctrl reverse gain,"
                   " linked band scaling and no implicit SOLO");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 6.0f, 2.0f);
        enableBell (*processor, 1, 2200.0f, -3.0f, 1.6f);
        auto editor = std::make_unique<ApexQ4Editor> (*processor);
        editor->setSize (1280, 720);
        editor->selectBandForTesting (0);
        const juce::ModifierKeys ctrl (juce::ModifierKeys::ctrlModifier);
        const juce::ModifierKeys alt (juce::ModifierKeys::altModifier);
        auto pt = editor->nodeHitAreaForTesting (1).getCentre().toFloat();
        editor->injectGraphPointerForTesting (pt, 0, ctrl);
        editor->injectGraphPointerForTesting (pt, 2, ctrl);
        expectEquals (editor->selectedBandCountForTesting(), 2);
        pt = editor->nodeHitAreaForTesting (0).getCentre().toFloat();
        editor->injectGraphWheelForTesting (pt, 1.0f, alt);
        for (int b = 0; b < 2; ++b)
        {
            expect (processor->getParametricEQParameter (
                dynamicParameterIndex (b, DynamicBandOffset::Enable))
                    ->getBool(), "Alt wheel activates REAL native dynamics");
            expectWithinAbsoluteError (
                static_cast<double> (processor->getParametricEQParameter (
                    dynamicParameterIndex (b, DynamicBandOffset::Range))
                        ->getUnitsValue()), 2.0, 0.2,
                "two selected bands receive +2 dB dynamic range");
        }
        const juce::ModifierKeys linked (
            juce::ModifierKeys::altModifier
            | juce::ModifierKeys::ctrlModifier);
        editor->injectGraphWheelForTesting (pt, 1.0f, linked);
        for (int b = 0; b < 2; ++b)
            expectWithinAbsoluteError (
                static_cast<double> (processor->getParametricEQParameter (
                    dynamicParameterIndex (b, DynamicBandOffset::Range))
                        ->getUnitsValue()), 4.0, 0.2,
                "Alt+Ctrl wheel further increases hosted dynamic range");
        expectWithinAbsoluteError (
            static_cast<double> (processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                    ->getUnitsValue()), 4.0, 0.2,
            "Alt+Ctrl reverse-linked trade reduces static gain by 2 dB");
        expectWithinAbsoluteError (
            static_cast<double> (processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Gain))
                    ->getUnitsValue()), -5.0, 0.2,
            "grouped reverse trade also changes second band's gain");
        expect (! processor->isAuditionHoldingForTesting(),
            "mousewheel dynamic gestures must not mute the vocal");
    }

    void testRealAudioDynamicMeterAndDirectRangeRing()
    {
        beginTest ("real DSP dynamic meter: coherent audio snapshot and"
                   " touch range ring without frequency/gain corruption");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 6.0f, 2.0f);
        setUnits (*processor, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (*processor, dynamicParameterIndex (
            0, DynamicBandOffset::Threshold), -48.0f);
        setUnits (*processor, dynamicParameterIndex (
            0, DynamicBandOffset::Range), 12.0f);
        setUnits (*processor, dynamicParameterIndex (
            0, DynamicBandOffset::Attack), 0.001f);
        setUnits (*processor, dynamicParameterIndex (
            0, DynamicBandOffset::Release), 0.040f);
        auto editor = std::make_unique<ApexQ4Editor> (*processor);
        editor->setSize (1280, 720);
        editor->selectBandForTesting (0);
        juce::AudioBuffer<float> chunk (2, 128);
        juce::MidiBuffer midi;
        double phase = 0.0;
        constexpr double increment =
            juce::MathConstants<double>::twoPi * 930.0 / 48000.0;
        // Stream genuine PCM through processBlock, then the lock-free
        // ResponseCore queue; never inject arbitrary gain readouts.
        for (int block = 0; block < 200; ++block)
        {
            for (int sample = 0; sample < 128; ++sample)
            {
                const auto v = static_cast<float> (
                    0.35 * std::sin (phase));
                phase += increment;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
                chunk.setSample (0, sample, v);
                chunk.setSample (1, sample, v);
            }
            processor->processBlock (chunk, midi);
            if ((block % 6) == 0)
                editor->refreshMeasuredDynamicsForTesting();
        }
        editor->refreshMeasuredDynamicsForTesting();
        expect (editor->hasMeasuredDynamicFrameForTesting (0),
            "visible dynamic meter must originate from actual audio thread");
        const auto measured = editor->measuredDynamicDbForTesting (0);
        editor->refreshForTesting();
        expect (editor->gainRingMeterValidForTesting(),
            "GAIN dial inner yellow arc is authorised ONLY after real DSP"
            " ResponseFrame from audio, never synthetic visual animation");
        expectWithinAbsoluteError (
            static_cast<double> (editor->gainRingMeasuredDbForTesting()),
            static_cast<double> (measured), 0.005,
            "GAIN dial actual dB meter equals coherent DSP display frame");
        expect (std::isfinite (measured));
        expect (measured < -0.15f,
            "positive cut range under loud audio must report real reduction");
        expect (std::abs (measured) <= 24.01f,
            "GUI meter cannot exceed hosted permitted range");
        expect (! editor->hasMeasuredDynamicFrameForTesting (1),
            "inactive bands may not display invented audio gain");

        auto* range = processor->getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Range));
        const float oldRange = range->getUnitsValue();
        const auto* hz = processor->getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Frequency));
        const auto* gain = processor->getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Gain));
        const float oldHz = hz->getUnitsValue();
        const float oldGain = gain->getUnitsValue();
        const auto origin = editor->nodeHitAreaForTesting (0)
            .getCentre().toFloat() + juce::Point<float> { 18.0f, 0.0f };
        editor->injectGraphPointerForTesting (origin, 0);
        editor->injectGraphPointerForTesting (
            origin + juce::Point<float> { 0.0f, -28.0f }, 1);
        editor->injectGraphPointerForTesting (
            origin + juce::Point<float> { 0.0f, -28.0f }, 2);
        expect (range->getUnitsValue() > oldRange + 3.0f,
            "dragging outer dynamic ring must increase REAL range parameter");
        expectWithinAbsoluteError (static_cast<double> (hz->getUnitsValue()),
                                   static_cast<double> (oldHz), 0.1,
            "ring gesture must never drag the EQ frequency node");
        expectWithinAbsoluteError (static_cast<double> (gain->getUnitsValue()),
                                   static_cast<double> (oldGain), 0.01,
            "ring gesture must never change static band gain");
        expect (! processor->isAuditionHoldingForTesting(),
            "dynamic ring editing cannot trigger SOLO or mute the vocal");
        editor->undoForTesting();
        expectWithinAbsoluteError (
            static_cast<double> (range->getUnitsValue()),
            static_cast<double> (oldRange), 0.2,
            "range ring edit is one reversible host parameter transaction");
    }

    void testExpandedDynamicPanelAndHostAutomation()
    {
        beginTest ("manual dynamic EQ: real touch-adjustable threshold, range,"
                   " attack, release, bypass/filter/spectral hosted values");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 6.0f, 2.4f);
        auto editor = std::make_unique<ApexQ4Editor> (*processor);
        for (auto size : std::array<juce::Point<int>, 4> {{
               { 320, 568 }, { 480, 800 },
               { 900, 300 }, { 1280, 720 } }})
        {
            editor->setSize (size.x, size.y);
            editor->selectBandForTesting (0);
            editor->expandDynamicForTesting (true);
            expect (editor->dynamicPanelVisibleForTesting(),
                "a selected band can open its dynamic controls");
            const auto area = editor->dynamicPanelBoundsForTesting();
            expect (editor->getLocalBounds().contains (area),
                "dynamic controls stay within window on desktop and mobile");
            expect (editor->graphBoundsForTesting().contains (area),
                "expanded controls must not obscure header or footer");
            expect (area.getHeight() >= 180);
            expect (! area.expanded (8).intersects (
                        editor->nodeHitAreaForTesting (0)),
                "expanded dynamics must keep the selected frequency node"
                " visible and touchable at each responsive resolution");
            expect (! area.intersects (
                        editor->selectedTagBoundsForTesting()),
                "Dynamics panel must never hide the live EQ band data tooltip");
            // PASS 10: old four-corner solver silently layered an entire
            // DYNAMIC EQ card over FREQ/GAIN/Q at 480x800. Assert the real
            // layout at every breakpoint instead of trusting a GREEN PNG.
            const bool substituteHud = size.x < 460
                || (size.x >= 720
                    && editor->graphBoundsForTesting().getHeight() < 260);
            if (! substituteHud)
            {
                expect (editor->primaryHudVisibleForTesting(),
                    "normal-height Dynamics preserves the selected EQ HUD");
                expect (! area.expanded (3).intersects (
                    editor->panelBoundsForTesting()),
                    "dynamic panel must NOT obscure ANY part of the EQ knobs"
                    " or SOLO on normal-height phone or desktop");
            }
            else
            {
                expect (! editor->primaryHudVisibleForTesting(),
                    "compact portrait / short landscape Dynamics temporarily"
                    " replaces the FREQ/GAIN/Q HUD rather than hiding it");
            }
            const auto reset = editor->dynamicResetBoundsForTesting();
            if (! reset.isEmpty())
            {
                expect (area.contains (reset),
                    "RESET action lives INSIDE expanded dynamic card");
                expect (reset.getWidth() >= 60
                        && reset.getHeight() >= 30,
                    "RESET is never a clipped tiny touch target");
            }
            auto dials = editor->dynamicSlidersForTesting();
            for (auto* slider : dials)
            {
                expect (slider->isVisible(), "all four controls accessible");
                expect (area.contains (slider->getBounds()),
                    "each dynamic slider must fit within open panel");
                expect (slider->getWidth() >= 160);
                expect (slider->getHeight() >= 20);
            }
            editor->expandDynamicForTesting (false);
            expect (! editor->dynamicPanelVisibleForTesting(),
                "dynamic panel must close without disabling native DSP");
            expect (editor->primaryHudVisibleForTesting(),
                "closing compact dynamic editor restores FREQ/GAIN/Q knobs");
            // PASS 10 RED root cause regression: the previous visibility
            // helper resurrected phone-hidden controls at their stale desktop
            // rectangles, outside the active floating HUD. Run this after
            // OPEN -> CLOSE, not just at initial construction.
            const auto mainHud = editor->panelBoundsForTesting();
            const auto buttons = editor->contextualHeaderBoundsForTesting();
            for (const auto& bounds : buttons)
                if (! bounds.isEmpty())
                    expect (mainHud.contains (bounds),
                        "every VISIBLE responsive HUD control must fit"
                        " after closing DYNAMIC EQ");
            if (size.x < 460)
                expect (buttons[2].isEmpty() && buttons[3].isEmpty()
                        && buttons[7].isEmpty(),
                    "narrow portrait must keep slope, placement, DYN hidden"
                    " after reopening the regular HUD");
            else if (size.x < 670)
                expect (buttons[3].isEmpty() && buttons[7].isEmpty(),
                    "phone landscape/480 width must not resurrect placement"
                    " and DYN controls at stale desktop coordinates");
        }

        editor->setSize (1280, 720);
        editor->selectBandForTesting (0);
        editor->expandDynamicForTesting (true);
        editor->toggleDynamicOnForTesting();
        expect (processor->getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
            "dynamic enable is the REAL hosted processor parameter");
        auto dials = editor->dynamicSlidersForTesting();
        constexpr DynamicBandOffset offsets[] = {
            DynamicBandOffset::Threshold, DynamicBandOffset::Range,
            DynamicBandOffset::Attack, DynamicBandOffset::Release
        };
        const juce::String values[] = { "-31 dB", "+7.5 dB",
                                         "25 ms", "160 ms" };
        for (int i = 0; i < 4; ++i)
        {
            auto& slider = *dials[static_cast<std::size_t> (i)];
            slider.setValue (slider.valueFromTextFunction (values[i]),
                             juce::sendNotificationSync);
            const int paramIndex = dynamicParameterIndex (0, offsets[i]);
            const float actual = processor->getParametricEQParameter (
                paramIndex)->getValue();
            expectWithinAbsoluteError (
                static_cast<double> (slider.getValue()),
                static_cast<double> (
                    offsets[i] == DynamicBandOffset::Range
                        ? 1.0f - actual : actual), 1.0e-6,
                "range UI sign maps to original host normalized parameter"
                " without rewriting the frozen parameter ABI");
        }
        expectWithinAbsoluteError (static_cast<double> (
            processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Range))
                ->getUnitsValue()), -7.5, 0.2,
            "typed +7.5 dB expansion maps to legacy host -7.5 dB"
            " without changing saved VST3 automation IDs");
        expectWithinAbsoluteError (static_cast<double> (
            processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Threshold))
                ->getUnitsValue()), -31.0, 0.2,
            "typed detector threshold uses actual -31 dB");
        expectWithinAbsoluteError (static_cast<double> (
            processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Attack))
                ->getUnitsValue()), 0.025, 0.002,
            "25 ms input must become 0.025 seconds, NOT max 5 seconds");
        expectWithinAbsoluteError (static_cast<double> (
            processor->getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Release))
                ->getUnitsValue()), 0.160, 0.004,
            "160 ms release input must become 0.160 seconds");
        auto* attack = processor->getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Attack));
        expectWithinAbsoluteError (static_cast<double> (
            attack->getValueForText (attack->getText (
                attack->getValue(), 0))),
            static_cast<double> (attack->getValue()), 0.03,
            "native attack text must round-trip through hosted parameter");
        expect (! processor->isAuditionHoldingForTesting(),
            "adjusting dynamic EQ never silently solos/mutes the singer");
        editor->expandDynamicForTesting (false);
        expect (processor->getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
            "closing dynamic controls never erases active dynamic processing");
    }

    void testNativeMultiSelectionRelativeGroupedDragAndHistory()
    {
        beginTest ("manual multi-select: Ctrl-click two real bands, octave"
                   " relative drag, grouped automation, one-step undo");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 6.0f, 2.0f);
        enableBell (*processor, 1, 2100.0f, -3.0f, 1.5f);
        auto editor = std::make_unique<ApexQ4Editor> (*processor);
        editor->setSize (1280, 720);
        editor->selectBandForTesting (0);
        expectEquals (editor->selectedBandCountForTesting(), 1);
        const juce::ModifierKeys control (juce::ModifierKeys::ctrlModifier);
        const auto p1 = editor->nodeHitAreaForTesting (1).getCentre().toFloat();
        editor->injectGraphPointerForTesting (p1, 0, control);
        editor->injectGraphPointerForTesting (p1, 2, control);
        expectEquals (editor->selectedBandCountForTesting(), 2);
        expect (editor->isBandSelectedForTesting (0)
                && editor->isBandSelectedForTesting (1),
                "Ctrl-click must retain existing band and add new one");

        const auto p0 = editor->nodeHitAreaForTesting (0).getCentre().toFloat();
        editor->injectGraphPointerForTesting (p0, 0);
        editor->injectGraphPointerForTesting (
            { p0.x + 60.0f, p0.y - 25.0f }, 1);
        editor->injectGraphPointerForTesting (
            { p0.x + 60.0f, p0.y - 25.0f }, 2);
        const auto freq0 = processor->getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Frequency))->getUnitsValue();
        const auto freq1 = processor->getParametricEQParameter (
            parameterIndex (1, BandParameterOffset::Frequency))->getUnitsValue();
        const auto gain0 = processor->getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Gain))->getUnitsValue();
        const auto gain1 = processor->getParametricEQParameter (
            parameterIndex (1, BandParameterOffset::Gain))->getUnitsValue();
        expect (freq0 > 930.0f && freq1 > 2100.0f,
                "group drag actually edits BOTH hosted frequencies");
        expectWithinAbsoluteError (
            static_cast<double> (freq0) / 930.0,
            static_cast<double> (freq1) / 2100.0, 0.02,
            "grouped bands preserve intervals in log-frequency space");
        expectWithinAbsoluteError (static_cast<double> (gain0) - 6.0,
            static_cast<double> (gain1) + 3.0, 0.08,
            "grouped bands keep equal dB deltas and relative gain balance");
        expectEquals (editor->selectedBandCountForTesting(), 2);
        expect (! processor->isAuditionHoldingForTesting(),
                "grouped drag never implicitly auditions or mutes vocals");
        editor->undoForTesting();
        expectWithinAbsoluteError (
            static_cast<double> (processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                    ->getUnitsValue()), 930.0, 3.0,
            "undo restores first group's frequency in one action");
        expectWithinAbsoluteError (
            static_cast<double> (processor->getParametricEQParameter (
                parameterIndex (1, BandParameterOffset::Frequency))
                    ->getUnitsValue()), 2100.0, 5.0,
            "undo also restores second group's frequency");
        editor->redoForTesting();
        expectWithinAbsoluteError (
            static_cast<double> (processor->getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                    ->getUnitsValue()), static_cast<double> (freq0), 3.0,
            "redo repeats grouped change");

        const auto p1Updated = editor->nodeHitAreaForTesting (1)
            .getCentre().toFloat();
        editor->injectGraphPointerForTesting (p1Updated, 0, control);
        editor->injectGraphPointerForTesting (p1Updated, 2, control);
        expectEquals (editor->selectedBandCountForTesting(), 1,
            "Ctrl-click selected band removes it from linked edit group");
        expect (editor->isBandSelectedForTesting (0));
        expect (! editor->isBandSelectedForTesting (1));
    }

    void testManualGraphCreationAndModifierModes()
    {
        beginTest ("Pro-Q manual: deselect / Ctrl-click / Alt dynamic / "
                   "Alt+Shift spectral / result-curve drag");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
        auto editor = std::make_unique<ApexQ4Editor> (*processor);
        editor->setSize (1280, 720);
        editor->selectBandForTesting (0);
        expectEquals (editor->activeBandsForTesting(), 1);

        const auto click = [&] (juce::Point<float> pos,
                                juce::ModifierKeys keys = {})
        {
            editor->injectGraphPointerForTesting (pos, 0, keys);
            editor->injectGraphPointerForTesting (pos, 2, keys);
        };
        // One ordinary click elsewhere does NOT insert an unintended filter
        // while an active band is selected.
        click ({ 300.0f, 350.0f });
        expectEquals (editor->selectedBandForTesting(), -1);
        expectEquals (editor->activeBandsForTesting(), 1);

        const juce::ModifierKeys ctrl (juce::ModifierKeys::ctrlModifier);
        const juce::ModifierKeys altCtrl (
            juce::ModifierKeys::altModifier | juce::ModifierKeys::ctrlModifier);
        const juce::ModifierKeys spectralKeys (
            juce::ModifierKeys::altModifier
            | juce::ModifierKeys::shiftModifier
            | juce::ModifierKeys::ctrlModifier);
        // After explicitly re-selecting a band, Ctrl-click creates a NEW
        // band instead of merely cancelling the selection.
        editor->selectBandForTesting (0);
        click ({ 300.0f, 275.0f }, ctrl);
        expectEquals (editor->activeBandsForTesting(), 2);
        expectEquals (editor->selectedBandForTesting(), 1);

        click ({ 950.0f, 300.0f }, altCtrl);
        expectEquals (editor->activeBandsForTesting(), 3);
        expect (processor->getParametricEQParameter (
            dynamicParameterIndex (2, DynamicBandOffset::Enable))->getBool(),
            "Alt+Ctrl creation activates real hosted dynamic band");
        expect (processor->getParametricEQParameter (
            dynamicParameterIndex (2, DynamicBandOffset::Range))
                ->getUnitsValue() >= 1.0f,
            "Alt-created dynamics must not silently stay at zero range");

        click ({ 590.0f, 360.0f }, spectralKeys);
        expectEquals (editor->activeBandsForTesting(), 4);
        expect (processor->getParametricEQParameter (
            spectralBandParameterIndex (3))->getBool(),
            "Alt+Shift+Ctrl creation arms native spectral dynamics on band");

        // With band3 still selected, drag the yellow 0dB curve at x=20px
        // and verify a real LOW CUT is created only once after the drag.
        editor->injectGraphPointerForTesting ({ 20.0f, 350.0f }, 0);
        expectEquals (editor->activeBandsForTesting(), 4,
            "mousedown on yellow curve with a selection must not double-add");
        editor->injectGraphPointerForTesting ({ 21.0f, 260.0f }, 1);
        editor->injectGraphPointerForTesting ({ 21.0f, 260.0f }, 2);
        expectEquals (editor->activeBandsForTesting(), 5,
            "yellow-curve drag creates exactly one additional hosted band");
        expectEquals (processor->getParametricEQParameter (
            parameterIndex (4, BandParameterOffset::Shape))->getChoiceIndex(),
            static_cast<int> (FilterShape::LowCut),
            "far-left creation intelligently selects Low Cut shape");
        expect (! processor->isAuditionHoldingForTesting(),
            "all normal graph gestures leave band audition inactive");
    }

    void testApexQ4CleanRoomProductionEditor()
    {
        beginTest ("factory launches NEW native EQ4 UI — not legacy Prism editor");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        // In production the factory must instantiate the clean-room editor.
        std::unique_ptr<juce::AudioProcessorEditor> base (processor->createEditor());
        auto* editor = dynamic_cast<ApexQ4Editor*> (base.get());
        expect (editor != nullptr,
            "production factory must not launch rejected Prism V2/V3 GUI");
        if (editor == nullptr)
            return;

        expectEquals (editor->selectedBandForTesting(), -1);
        expectEquals (editor->activeBandsForTesting(), 0);
        const auto workspace = juce::SystemStats::getEnvironmentVariable (
            "GITHUB_WORKSPACE", {});

        // ADVERTISEMENT-FREE EVIDENCE: write a real RIFF/WAVE file,
        // reopen it with JUCE's WAV decoder, and stream its PCM audio into
        // the REAL APEX Processor -> analyzer FIFO -> 4,096-bin FFT worker.
        // The exported PNGs therefore show ACTUAL PRE+POST measured audio.
        // No painted/mock spectrum and no manual external laptop required.
        if (workspace.isNotEmpty())
        {
            const auto snapshotsDir = juce::File (workspace).getChildFile (
                "apex-eq-ui-snapshots");
            expect (snapshotsDir.createDirectory().wasOk(),
                    "EQ spectrum evidence directory can be created");
            const auto wavFile = snapshotsDir.getChildFile (
                "APEX-EQ-Reference-48k-Stereo.wav");
            juce::WavAudioFormat wav;
            constexpr int sampleRate = 48000;
            constexpr int totalSamples = sampleRate * 3;
            {
                auto stream = wavFile.createOutputStream();
                expect (stream != nullptr, "real WAV output stream opens");
                if (stream != nullptr)
                {
                    std::unique_ptr<juce::AudioFormatWriter> writer (
                        wav.createWriterFor (stream.release(), 48000.0,
                                             2, 16, {}, 0));
                    expect (writer != nullptr,
                            "JUCE WAV encoder provides a 48kHz stereo writer");
                    if (writer != nullptr)
                    {
                        juce::AudioBuffer<float> musicalTest (2, totalSamples);
                        juce::Random noise (0x5a071);
                        constexpr double tau = juce::MathConstants<double>::twoPi;
                        for (int s = 0; s < totalSamples; ++s)
                        {
                            const double time = s / 48000.0;
                            const double envelope = 0.73
                                + 0.27 * std::sin (tau * 1.6 * time);
                            // Deterministic musical harmonics, vocal-like
                            // upper partials, and low-level broadband floor.
                            const double waveform = envelope * (
                                0.23 * std::sin (tau * 196.0 * time)
                              + 0.17 * std::sin (tau * 392.0 * time)
                              + 0.11 * std::sin (tau * 784.0 * time)
                              + 0.08 * std::sin (tau * 1568.0 * time)
                              + 0.065 * std::sin (tau * 3136.0 * time)
                              + 0.035 * std::sin (tau * 6272.0 * time))
                              + 0.055 * (noise.nextFloat() * 2.0 - 1.0);
                            musicalTest.setSample (0, s,
                                static_cast<float> (waveform));
                            musicalTest.setSample (1, s,
                                static_cast<float> (waveform * 0.93
                                    + 0.012 * std::sin (tau * 421.0 * time)));
                        }
                        expect (writer->writeFromAudioSampleBuffer (
                            musicalTest, 0, totalSamples),
                            "real reference PCM written to disk as WAV");
                    }
                }
            }
            expect (wavFile.existsAsFile() && wavFile.getSize() > 250000,
                    "3-second stereo WAV exists and contains nontrivial PCM");
            std::unique_ptr<juce::AudioFormatReader> reader (
                wav.createReaderFor (new juce::FileInputStream (wavFile), true));
            expect (reader != nullptr, "reference WAV is decodable");
            if (reader != nullptr)
            {
                expectWithinAbsoluteError (reader->sampleRate, 48000.0, 0.1);
                // Must EQ the WAV DURING playback, not add nodes after
                // analyzer recording. This creates a genuine PRE/POST
                // spectral difference; simply painting purple bins fails.
                enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
                enableBell (*processor, 1, 5200.0f, -5.0f, 3.0f);
                processor->setAnalyzerMode (
                    APEX::Analysis::SpectrumTapMode::Both);
                juce::AudioBuffer<float> chunk (2, 128);
                juce::MidiBuffer midi;
                for (int start = 0; start < totalSamples; start += 128)
                {
                    const int count = std::min (128, totalSamples - start);
                    chunk.clear();
                    expect (reader->read (&chunk, 0, count,
                                           static_cast<juce::int64> (start),
                                           true, true),
                            "decode full PCM block");
                    processor->processBlock (chunk, midi);
                    // Let the 20ms asynchronous FFT worker drain the FIFO,
                    // avoiding fake visualization and audio FIFO overflows.
                    if ((start / 128) % 6 == 0)
                        juce::Thread::sleep (2);
                }
                for (int attempt = 0; attempt < 40; ++attempt)
                {
                    editor->refreshMeasuredSpectrumForTesting();
                    if (editor->hasMeasuredSpectrumForTesting())
                        break;
                    juce::Thread::sleep (20);
                }
                expect (editor->hasMeasuredSpectrumForTesting(),
                        "real PRE/POST FFT must publish actual WAV audio");
                int frozenFftIndex = -1;
                float frozenPost784 = -120.0f;
                if (editor->hasMeasuredSpectrumForTesting())
                {
                    // PASS 12 real screenshot critic: at 48k/128 the canvas
                    // extends to 30 kHz, but FFT data ends below 24 kHz.
                    // The old filled path incorrectly closed at x=width,
                    // showing a fictional diagonal triangle over frequencies
                    // above actual Nyquist. Both PRE and POST now share the
                    // same measured x-domain; the unmeasured strip is EMPTY.
                    for (const auto size : {
                            juce::Point<int> { 320, 568 },
                            juce::Point<int> { 480, 800 },
                            juce::Point<int> { 1280, 720 } })
                    {
                        editor->setSize (size.x, size.y);
                        // PASS 13: native screenshot hierarchy must keep
                        // the measured analyzer (PRE lilac / POST teal)
                        // perceptually different from the GOLD EQ transfer.
                        // This is tested with actual 48 kHz WAV FFT frames,
                        // not synthesized paint-only preview data.
                        const auto preInk =
                            editor->spectrumTraceColourForTesting (false);
                        const auto postInk =
                            editor->spectrumTraceColourForTesting (true);
                        const auto eqInk =
                            editor->eqTransferColourForTesting();
                        expect (preInk != postInk && preInk != eqInk
                                && postInk != eqInk,
                            "PRE/POST real measured FFT and the EQ response"
                            " MUST have three distinct visual identities");
                        expect (postInk.getGreen() > postInk.getRed()
                                && postInk.getBlue() > postInk.getRed(),
                            "POST FFT should stay teal, not gold like EQ");
                        expect (eqInk.getRed() > eqInk.getBlue(),
                            "EQ transfer curve retains its warm gold color");
                        const auto legend =
                            editor->spectrumLegendBoundsForTesting();
                        expect (! legend.isEmpty(),
                            "live measured PRE+POST spectrum has a key");
                        expect (editor->graphBoundsForTesting().contains (legend),
                            "PRE/POST legend stays in FFT graph at mobile"
                            " and desktop aspect ratios");
                        expect (legend.getWidth() >= 120
                                && legend.getHeight() >= 20,
                            "both measured PRE and POST have readable keys");
                        const auto spectrumX =
                            editor->measuredSpectrumDomainXForTesting();
                        expectWithinAbsoluteError (
                            static_cast<double> (spectrumX.getStart()),
                            0.0, 1.0,
                            "measured spectrum starts at 20 Hz");
                        expect (spectrumX.getEnd() >
                                    size.x * 0.90f
                                && spectrumX.getEnd() <
                                    size.x * 0.99f,
                            "48 kHz FFT ends at genuine ~24 kHz Nyquist,"
                            " NOT at the graph's 30 kHz right edge");
                        expectWithinAbsoluteError (
                            static_cast<double> (
                                editor->displayedSpectrumDbForTesting (
                                    true, 29000.0f)),
                            -120.0, 0.001,
                            "display MUST NOT extrapolate POST energy into"
                            " the unmeasured >24 kHz spectrum region");
                    }
                    expect (editor->maximumMeasuredSpectrumDbForTesting (false)
                                > -70.0f,
                        "real measured PRE bins contain strong audible energy");
                    expect (editor->maximumMeasuredSpectrumDbForTesting (true)
                                > -80.0f,
                        "real measured POST bins contain audible EQ audio");
                    const float pre = editor->measuredSpectrumAtHzForTesting (
                        false, 784.0f);
                    const float post = editor->measuredSpectrumAtHzForTesting (
                        true, 784.0f);
                    expect (std::isfinite (pre) && std::isfinite (post),
                            "PRE/POST measured FFT bins are finite");
                    expect (post > pre + 1.0f,
                        "930 Hz +8 dB band must boost the real WAV 784 Hz"
                        " harmonic in measured POST spectrum");
                    const float smoothPre =
                        editor->displayedSpectrumDbForTesting (false, 784.0f);
                    const float smoothPost =
                        editor->displayedSpectrumDbForTesting (true, 784.0f);
                    expect (std::isfinite (smoothPre)
                            && std::isfinite (smoothPost),
                        "presentation smoothing stays finite with real PCM");
                    expect (smoothPost > smoothPre + 0.4f,
                        "smoothed WAV spectrum retains genuine EQ PRE/POST gain");
                    expect (smoothPost <=
                            editor->maximumMeasuredSpectrumDbForTesting (true)
                            + 0.25f,
                        "presentation may never invent FFT energy above"
                        " the measured highest real bin");
                    for (const auto width : { 1.0 / 36.0,
                                              1.0 / 18.0, 1.0 / 9.0 })
                    {
                        editor->setSpectrumSmoothingForTesting (width);
                        const auto db = editor->displayedSpectrumDbForTesting (
                            true, 784.0f);
                        expect (std::isfinite (db),
                            "octave-resolution switching cannot produce NaN");
                        expect (db <=
                                editor->maximumMeasuredSpectrumDbForTesting (true)
                                + 0.25f,
                            "FFT presentation smoothing cannot boost above"
                            " any genuinely measured PCM bin");
                    }
                    editor->setSpectrumSmoothingForTesting (1.0 / 18.0);
                    // PASS 7: Hold must seed from a REAL 48k PCM/FFT frame,
                    // without making up peak values or mutating audio.
                    editor->setPeakHoldForTesting (true);
                    expect (editor->hasPeakHoldForTesting (false)
                            && editor->hasPeakHoldForTesting (true),
                        "PRE and POST hold are seeded by real FFT data");
                    expectWithinAbsoluteError (
                        static_cast<double> (editor->heldPeakAtHzForTesting (
                            true, 784.0f)),
                        static_cast<double> (
                            editor->measuredSpectrumAtHzForTesting (
                                true, 784.0f)), 0.01,
                        "first held bin equals the actual worker measurement");
                    editor->setSpectrumFreezeForTesting (true);
                    expect (editor->spectrumFrozenForTesting(),
                        "real frame is present before display freeze");
                    frozenFftIndex = editor->spectrumFrameIndexForTesting();
                    frozenPost784 = editor->measuredSpectrumAtHzForTesting (
                        true, 784.0f);
                }

                // The screenshot must also show REAL dynamic DSP activity.
                // Replay the SAME WAV that is packaged with the artifact,
                // enabling hosted dynamics only AFTER validating static EQ's
                // PRE/POST difference above. No external/mock render data.
                setUnits (*processor, dynamicParameterIndex (
                    0, DynamicBandOffset::Enable), 1.0f);
                setUnits (*processor, dynamicParameterIndex (
                    0, DynamicBandOffset::Threshold), -48.0f);
                setUnits (*processor, dynamicParameterIndex (
                    0, DynamicBandOffset::Range), 9.0f);
                setUnits (*processor, dynamicParameterIndex (
                    0, DynamicBandOffset::Attack), 0.003f);
                setUnits (*processor, dynamicParameterIndex (
                    0, DynamicBandOffset::Release), 0.090f);
                for (int start = 0; start < 512 * 128; start += 128)
                {
                    chunk.clear();
                    expect (reader->read (&chunk, 0, 128,
                                           static_cast<juce::int64> (start),
                                           true, true),
                            "replay SAME WAV through real dynamic EQ");
                    processor->processBlock (chunk, midi);
                    if ((start / 128) % 5 == 0)
                        editor->refreshMeasuredDynamicsForTesting();
                    if ((start / 128) % 9 == 0)
                        juce::Thread::sleep (2);
                }
                editor->refreshMeasuredDynamicsForTesting();
                editor->refreshMeasuredSpectrumForTesting();
                expect (editor->hasMeasuredDynamicFrameForTesting (0),
                    "native dynamic screenshot derives from coherent"
                    " ResponseCore gain measured on reference WAV");
                expect (editor->measuredDynamicDbForTesting (0) < -0.05f,
                    "reference WAV must trigger real native dynamic reduction");
                if (frozenFftIndex >= 0)
                {
                    expect (editor->spectrumFrozenForTesting());
                    expectEquals (editor->spectrumFrameIndexForTesting(),
                                  frozenFftIndex,
                        "freeze must not latch subsequent WAV FFT frames");
                    expectWithinAbsoluteError (
                        static_cast<double> (
                            editor->measuredSpectrumAtHzForTesting (
                                true, 784.0f)),
                        static_cast<double> (frozenPost784), 0.01,
                        "freeze preserves the actual earlier measured bin");
                    // The dynamics meter above MUST continue during freeze:
                    // the analyzer UI cannot suspend realtime DSP/metering.
                    editor->setSpectrumFreezeForTesting (false);
                    for (int attempt = 0; attempt < 20; ++attempt)
                    {
                        editor->refreshMeasuredSpectrumForTesting();
                        if (editor->spectrumFrameIndexForTesting()
                            > frozenFftIndex)
                            break;
                        juce::Thread::sleep (12);
                    }
                    expect (! editor->spectrumFrozenForTesting());
                    expect (editor->spectrumFrameIndexForTesting()
                            > frozenFftIndex,
                        "unfreeze resumes with a NEW published FFT frame");
                    if (editor->hasMeasuredSpectrumForTesting())
                        expect (editor->heldPeakAtHzForTesting (true, 784.0f)
                                + 0.01f >=
                            editor->measuredSpectrumAtHzForTesting (
                                true, 784.0f),
                            "peak-held POST cannot be below latest real POST");
                    editor->clearHeldPeaksForTesting();
                    expect (! editor->hasPeakHoldForTesting (false)
                            && ! editor->hasPeakHoldForTesting (true),
                        "clear removes obsolete peaks without touching audio");
                    editor->setPeakHoldForTesting (true);
                    expect (editor->hasPeakHoldForTesting (true),
                        "re-enabled peak hold starts from current measured WAV");
                }
            }
        }

        for (auto size : std::array<juce::Point<int>, 5> {{
                { 320, 568 }, { 480, 800 }, { 900, 300 },
                { 1280, 720 }, { 1920, 1080 } }})
        {
            editor->setSize (size.x, size.y);
            const auto graph = editor->graphBoundsForTesting();
            expect (editor->getLocalBounds().contains (graph),
                    "new EQ4 graph stays inside host at every size");
            expect (graph.getHeight() >= 140);
            expect (graph.getWidth() == size.x,
                    "no old Prism dock or side pane reduces graph width");

            // Populate two REAL hosted bands. The graph and band controls
            // must paint from processor values, not mocked view-only points.
            enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
            enableBell (*processor, 1, 5200.0f, -5.0f, 3.0f);
            editor->selectBandForTesting (0);
            editor->refreshForTesting();
            expectEquals (editor->activeBandsForTesting(), 2);
            const auto popover = editor->panelBoundsForTesting();
            expect (editor->getLocalBounds().contains (popover),
                    "floating band controls must remain on-screen");
            expect (graph.contains (popover),
                    "EQ4 contextual controls must FLOAT within the canvas");
            const auto bootEye = editor->bootEyeBoundsForTesting();
            expect (editor->getLocalBounds().contains (bootEye),
                    "canonical APEX boot eye remains entirely inside header");
            expect (! bootEye.intersects (editor->presetBoundsForTesting()),
                    "boot logo may never cover the actual preset control");
            expect (popover.getHeight() <= 136,
                    "no giant inspector covering the entire EQ");
            // Adversarial critic: geometry-only snapshots previously hid
            // collisions between tiny SOLOS, MORE, channel and filter icons.
            const auto controls = editor->contextualHeaderBoundsForTesting();
            for (std::size_t i = 0; i < controls.size(); ++i)
            {
                if (controls[i].isEmpty()) continue;
                expect (popover.contains (controls[i]),
                    "all contextual hit targets fit entirely in floating HUD");
                for (std::size_t j = i + 1; j < controls.size(); ++j)
                    if (! controls[j].isEmpty())
                        expect (! controls[i].intersects (controls[j]),
                            "no contextual filter, dynamic or SOLO targets overlap");
            }
            expect (popover.getHeight() <= (size.x < 460 ? 136 : 116),
                    "contextual strip remains compact across layouts");
            // A visual green status is meaningless if the contextual panel
            // still masks the EQ node being edited, or another active band.
            for (int band = 0; band < 2; ++band)
                expect (! popover.intersects (
                    editor->nodeHitAreaForTesting (band)),
                    "band HUD MUST NOT cover an active EQ node");

            editor->selectBandForTesting (1);
            const auto secondBandPanel = editor->panelBoundsForTesting();
            for (int band = 0; band < 2; ++band)
                expect (! secondBandPanel.intersects (
                    editor->nodeHitAreaForTesting (band)),
                    "switching bands must not make any EQ node untouchable");
            editor->selectBandForTesting (0);

            juce::Image screenshot (juce::Image::RGB, size.x, size.y, true);
            {
                juce::Graphics graphics (screenshot);
                editor->paintEntireComponent (graphics, false);
            }
            expect (screenshot.getPixelAt (0, 0)
                    != screenshot.getPixelAt (size.x / 2, size.y / 2),
                    "new native GUI paints more than a flat background");
            if (workspace.isNotEmpty())
            {
                const auto dir = juce::File (workspace).getChildFile (
                    "apex-eq-ui-snapshots");
                expect (dir.createDirectory().wasOk());
                const auto file = dir.getChildFile (
                    "APEX-EQ-REAL-FFT-" + juce::String (size.x) + "x"
                    + juce::String (size.y) + ".png");
                auto stream = file.createOutputStream();
                expect (stream != nullptr);
                if (stream != nullptr)
                {
                    juce::PNGImageFormat png;
                    expect (png.writeImageToStream (screenshot, *stream));
                }

                // PASS 8 review evidence: the same genuine WAV-derived PRE/
                // POST spectrum, this time with TWO touch-linked nodes and
                // an explicit interactive mode badge. Never fabricate FFT.
                editor->setTouchMultiSelectForTesting (true);
                const auto touchNode = editor->nodeHitAreaForTesting (1)
                    .getCentre().toFloat();
                editor->injectGraphPointerForTesting (touchNode, 0);
                editor->injectGraphPointerForTesting (touchNode, 2);
                expectEquals (editor->selectedBandCountForTesting(), 2,
                    "PASS 8 screenshot has real touch-linked EQ nodes");
                juce::Image touchScreenshot (juce::Image::RGB,
                                              size.x, size.y, true);
                {
                    juce::Graphics graphics (touchScreenshot);
                    editor->paintEntireComponent (graphics, false);
                }
                const auto touchFile = dir.getChildFile (
                    "APEX-EQ-TOUCH-LINK-REAL-FFT-"
                    + juce::String (size.x) + "x"
                    + juce::String (size.y) + ".png");
                auto touchStream = touchFile.createOutputStream();
                expect (touchStream != nullptr);
                if (touchStream != nullptr)
                {
                    juce::PNGImageFormat png;
                    expect (png.writeImageToStream (
                        touchScreenshot, *touchStream));
                }
                editor->setTouchMultiSelectForTesting (false);
                editor->selectBandForTesting (0);

                // Also record the TRUE Ctrl-click multi-select visual state
                // using the same measured WAV FFT frame for user review.
                if (size.x >= 480)
                {
                    const auto band2 = editor->nodeHitAreaForTesting (1)
                        .getCentre().toFloat();
                    juce::ModifierKeys control (juce::ModifierKeys::ctrlModifier);
                    editor->injectGraphPointerForTesting (band2, 0, control);
                    editor->injectGraphPointerForTesting (band2, 2, control);
                    expectEquals (editor->selectedBandCountForTesting(), 2,
                        "native group snapshot has two genuinely selected bands");
                    juce::Image grouped (juce::Image::RGB,
                                          size.x, size.y, true);
                    {
                        juce::Graphics graphics (grouped);
                        editor->paintEntireComponent (graphics, false);
                    }
                    const auto groupFile = dir.getChildFile (
                        "APEX-EQ-GROUPED-WAV-FFT-"
                        + juce::String (size.x) + "x"
                        + juce::String (size.y) + ".png");
                    auto groupStream = groupFile.createOutputStream();
                    expect (groupStream != nullptr);
                    if (groupStream != nullptr)
                    {
                        juce::PNGImageFormat png;
                        expect (png.writeImageToStream (
                            grouped, *groupStream));
                    }
                    editor->selectBandForTesting (0);
                }
                // PASS 10: native visual evidence at EVERY responsive
                // breakpoint, not just the already-easy desktop geometry.
                if (size.x >= 320)
                {
                    editor->expandDynamicForTesting (true);
                    expect (editor->dynamicPanelVisibleForTesting(),
                        "dynamic-editor native screenshot requires open controls");
                    if (editor->primaryHudVisibleForTesting())
                        expect (! editor->dynamicPanelBoundsForTesting()
                            .expanded (3).intersects (
                                editor->panelBoundsForTesting()),
                            "exported real WAV FFT screenshot cannot hide EQ HUD");
                    else
                        expect (size.x < 460 || size.y < 390,
                            "only space-constrained viewports may substitute"
                            " dynamic editor for the FREQ/GAIN/Q HUD");
                    juce::Image dynamicImage (juce::Image::RGB,
                                               size.x, size.y, true);
                    {
                        juce::Graphics graphics (dynamicImage);
                        editor->paintEntireComponent (graphics, false);
                    }
                    const auto dynamicFile = dir.getChildFile (
                        "APEX-EQ-DYNAMIC-WAV-FFT-"
                        + juce::String (size.x) + "x"
                        + juce::String (size.y) + ".png");
                    auto dynamicStream = dynamicFile.createOutputStream();
                    expect (dynamicStream != nullptr);
                    if (dynamicStream != nullptr)
                    {
                        juce::PNGImageFormat png;
                        expect (png.writeImageToStream (
                            dynamicImage, *dynamicStream));
                    }
                    editor->expandDynamicForTesting (false);
                }
            }
        }
        // Full range precision: never assume typed UI values are raw 0..1.
        editor->selectBandForTesting (-1);
        editor->createBandForTesting ({ 300.0f, 230.0f });
        expectEquals (editor->activeBandsForTesting(), 3);
        expect (editor->selectedBandForTesting() >= 0,
                "new EQ4 creation binds a persistent hosted band");
    }

    void testStaticEvoEyeStamp()
    {
        beginTest ("PRISM uses exact static open boot-eye renderer, no extra GUI controls");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (*processor);
        for (const auto& dimensions : std::array<juce::Point<int>, 3> {{
                {320, 568}, {900, 620}, {1920, 1080} }})
        {
            editor->setSize (dimensions.x, dimensions.y);
            // AudioProcessorEditor adds JUCE's built-in resize-corner
            // child as the fourth entry. The eye is PAINTED by the existing
            // editor; it must not create a fifth interactive component.
            expectEquals (editor->getNumChildComponents(), 4,
                "only three EQ panels plus JUCE resize-corner; no eye widget");
            juce::Image rendered (juce::Image::RGB,
                dimensions.x, dimensions.y, true);
            {
                juce::Graphics graphics (rendered);
                editor->paintEntireComponent (graphics, false);
            }
            expect (rendered.isValid(), "branding render keeps GUI valid");
        }
#if JUCE_WINDOWS
        juce::Image stamp (juce::Image::ARGB, 84, 36, true);
        {
            juce::Graphics g (stamp);
            APEX::Branding::EvoEyeStamp::paint (g, {0.0f, 0.0f, 84.0f, 36.0f});
        }
        int opaque = 0;
        for (int y = 0; y < stamp.getHeight(); ++y)
            for (int x = 0; x < stamp.getWidth(); ++x)
                if (stamp.getPixelAt (x, y).getAlpha() > 64)
                    ++opaque;
        expect (opaque > 50,
                "canonical boot eye open frame paints recognizable plugin stamp");
#endif
    }

    void testPurpleAnalyzerAndPresetRestore()
    {
        beginTest ("editor-on analyzer shows normal dBFS material full-height "
                   "and releases its worker when closed");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 512);
        expect (processor->getAnalyzerMode()
            == APEX::Analysis::SpectrumTapMode::Closed);
        {
            auto editor = std::make_unique<ParametricEQEditor> (*processor);
            editor->setSize (1280, 720);
            expect (processor->getAnalyzerMode()
                == APEX::Analysis::SpectrumTapMode::Both,
                "GUI opens the actual pre/post FFT without changing no-GUI default");
            const float upper = editor->analyzerYForTesting (-6.0f);
            const float mid = editor->analyzerYForTesting (-38.0f);
            const float lower = editor->analyzerYForTesting (-96.0f);
            expect (upper < mid && mid < lower);
            expect (lower - mid > editor->getGraphBoundsForTesting()
                          .getHeight() * 0.3f,
                    "ordinary -38 dBFS analyzer peaks are no longer squashed "
                    "onto the graph baseline");

            beginTest ("phone effect uses real hosted low/high cuts and preset "
                       "undo restores 24-band identity");
            enableBell (*processor, 7, 1880.0f, -3.5f, 2.6f);
            setUnits (*processor, placementParameterIndex (7), 3.0f);
            std::array<std::array<float, 7>, kMaxBands> before {};
            for (int band = 0; band < kMaxBands; ++band)
                for (int off = 0; off < 7; ++off)
                    before[static_cast<std::size_t> (band)]
                          [static_cast<std::size_t> (off)] =
                        processor->getParametricEQParameter (parameterIndex (
                            band, static_cast<BandParameterOffset> (off)))->getValue();
            editor->applyPresetForTesting (2);
            const auto enabled = [&] (int band)
            {
                return processor->getParametricEQParameter (parameterIndex (
                    band, BandParameterOffset::Enabled))->getBool();
            };
            expect (enabled (0) && enabled (1) && enabled (2));
            expect (! enabled (7), "old bands are disabled not destructively removed");
            expectEquals (processor->getParametricEQParameter (parameterIndex (
                0, BandParameterOffset::Shape))->getChoiceIndex(),
                static_cast<int> (FilterShape::LowCut));
            expectEquals (processor->getParametricEQParameter (parameterIndex (
                1, BandParameterOffset::Shape))->getChoiceIndex(),
                static_cast<int> (FilterShape::HighCut));
            expectWithinAbsoluteError (
                processor->getParametricEQParameter (parameterIndex (
                    0, BandParameterOffset::Frequency))->getUnitsValue(),
                340.0f, 2.0f);
            editor->applyPresetForTesting (7);
            for (int band = 0; band < kMaxBands; ++band)
                for (int off = 0; off < 7; ++off)
                    expectWithinAbsoluteError (
                        processor->getParametricEQParameter (parameterIndex (
                            band, static_cast<BandParameterOffset> (off)))
                            ->getValue(),
                        before[static_cast<std::size_t> (band)]
                              [static_cast<std::size_t> (off)], 1.0e-6f,
                        "preset undo preserves stored hosted band IDs");
            expectWithinAbsoluteError (
                processor->getParametricEQParameter (
                    placementParameterIndex (7))->getUnitsValue(),
                3.0f, 0.01f, "undo restores original Mid placement");
        }
        expect (processor->getAnalyzerMode()
            == APEX::Analysis::SpectrumTapMode::Closed,
            "closing editor restores dormant analyzer unless changed by user");
    }

    // Master Orchestrator / Adversarial Critic gate: one deterministic
    // regression probe in the EXISTING Gauntlet. No extra CI jobs, retries,
    // external LLM dependency or unbounded test loops.
    void testAdversarialCriticBandSwitches()
    {
        beginTest ("adversarial critic: changing selected bands preserves "
                   "node access and isolates hosted channel state");
        auto processor = std::make_unique<Processor>();
        enableBell (*processor, 0, 930.0f, 8.0f, 2.4f);
        enableBell (*processor, 1, 5200.0f, -5.0f, 3.0f);
        processor->prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (*processor);
        auto& channel = editor->getPlacementComboForTesting();

        // Regression from the larger approved floating panel: changing
        // the focus must not strand either EQ point under the HUD.
        // The small case uses an intentionally separate touch sheet.
        for (const auto size : std::array<juce::Point<int>, 4> {{
                {320, 568}, {900, 620}, {1280, 720}, {1920, 1080}
             }})
        {
            editor->setSize (size.x, size.y);
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                const int selected = attempt % 2;
                const int choice = (attempt + selected) % kChannelPlacementCount;
                editor->setSelectedBandForTesting (selected);
                expectEquals (channel.getSelectedId(),
                    processor->getParametricEQParameter (
                        placementParameterIndex (selected))->getChoiceIndex() + 1,
                    "dropdown follows SELECTED band's canonical placement");
                channel.setSelectedId (choice + 1, juce::sendNotificationSync);
                expectEquals (processor->getParametricEQParameter (
                    placementParameterIndex (selected))->getChoiceIndex(),
                    choice, "dropdown persists its choice to the selected band");
                const int otherBand = 1 - selected;
                const int previousOther = processor->getParametricEQParameter (
                    placementParameterIndex (otherBand))->getChoiceIndex();
                // Selecting the next band without editing it may not carry
                // the previous band's Left/Right/Mid/Side choice over.
                editor->setSelectedBandForTesting (otherBand);
                expectEquals (channel.getSelectedId(), previousOther + 1,
                    "changing bands never leaks another band's channel mode");

                const auto graph = editor->getGraphBoundsForTesting();
                const auto inspector = editor->getInspectorBoundsForTesting();
                expect (editor->getLocalBounds().contains (inspector),
                    "inspector stays on screen under rapid band switching");
                expect (! inspector.intersects (editor->getTopBarBoundsForTesting()),
                    "inspector never obscures the footer");
                if (size.x >= 900 && size.y >= 500)
                {
                    expect (graph.contains (inspector),
                        "floating inspector never escapes the frequency graph");
                    for (int band = 0; band < 2; ++band)
                        expect (! inspector.intersects (
                            editor->getNodeBoundsForTesting (band).translated (
                                graph.getX(), graph.getY())),
                            "active EQ handles remain available while switching");
                }
            }
        }
    }

    void testResponsiveLayoutMatrix()
    {
        beginTest ("responsive layout at phone, tablet and desktop sizes");
        for (const auto& sizeCase : kSizes)
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            enableBell (processor, 1, 5200.0f, -5.0f, 3.0f);
            processor.prepareToPlay (48000.0, 512);

            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (sizeCase.width, sizeCase.height);
            expectEquals (static_cast<int> (editor->getLayoutMode()),
                          static_cast<int> (sizeCase.expected));
            editor->setSelectedBandForTesting (0);

            const auto editorBounds = editor->getLocalBounds();
            const auto topBar = editor->getTopBarBoundsForTesting();
            const auto graph = editor->getGraphBoundsForTesting();
            const auto inspector = editor->getInspectorBoundsForTesting();
            // Premium graph has four visible 44px+ actions; remaining
            // clipboard/reference features live in the contextual menu.
            const std::array<juce::TextButton*, 4> tools {{
                &editor->getSketchButtonForTesting(),
                &editor->getGrabButtonForTesting(),
                &editor->getMatchButtonForTesting(),
                &editor->getToolsButtonForTesting()
            }};
            for (auto* hidden : { &editor->getCopyButtonForTesting(),
                                   &editor->getPasteButtonForTesting(),
                                   &editor->getMatchSaveButtonForTesting(),
                                   &editor->getMatchLoadButtonForTesting() })
            {
                expect (! hidden->isVisible(),
                    "contextual features must not crowd the EQ graph");
                expect (hidden->getWidth() >= 44
                    && hidden->getHeight() >= 44,
                    "hidden action handlers remain accessible and testable");
            }
            for (std::size_t tool = 0; tool < tools.size(); ++tool)
            {
                const auto b = tools[tool]->getBounds();
                expect (b.getWidth() >= 44 && b.getHeight() >= 44,
                        "graph actions have >=44px touch targets");
                expect (juce::Rectangle<int> (0, 0, graph.getWidth(),
                                                graph.getHeight()).contains (b),
                        "graph actions fit their own panel");
                if (tool > 0)
                    expect (! b.intersects (tools[tool - 1]->getBounds()),
                            "adjacent graph actions cannot overlap");
            }
            if (graph.getHeight() <= 200)
            {
                // Real 640x400 native screenshots previously hid the boosted
                // EQ handle behind EQ MATCH. Test the source of the problem,
                // not just whether the seven action buttons fit on screen.
                for (int band = 0; band < 2; ++band)
                {
                    const auto node = editor->getNodeBoundsForTesting (band);
                    for (const auto* tool : tools)
                        expect (! node.intersects (tool->getBounds()),
                                "short landscape EQ node is NOT concealed "
                                "behind any top-row graph action");
                }
            }
            expectEquals (editor->getMatchButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("MATCH")
                                      : juce::String ("EQ MATCH"),
                "compact EQ Match caption stays readable");
            expectEquals (editor->getMatchSaveButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("SAVE")
                                      : juce::String ("REF SAVE"),
                "compact reference save caption stays readable");
            expectEquals (editor->getMatchLoadButtonForTesting().getButtonText(),
                sizeCase.width <= 430 ? juce::String ("LOAD")
                                      : juce::String ("REF LOAD"),
                "compact reference load caption stays readable");

            const auto primary = editor->getPrimaryDialsForTesting();
            for (const auto* dial : primary)
            {
                expect (dial != nullptr, "every primary EQ dial exists");
                if (dial == nullptr)
                    continue;
                expect (dial->getWidth() >= 44,
                        "primary parameter keeps a safe 44px touch target");
                if (sizeCase.width >= 900 && sizeCase.height >= 500)
                {
                    expect (dial->getSliderStyle()
                            == juce::Slider::RotaryHorizontalVerticalDrag,
                            "floating HUD uses real interactive APEX rotary dial");
                    expect (dial->getHeight() >= 110,
                            "floating dial stays legible and touch safe");
                }
                else
                {
                    expect (dial->getSliderStyle()
                            == juce::Slider::LinearHorizontal,
                            "phone/tablet sheet retains horizontal touch sliders");
                    const bool shortSheet = sizeCase.height < 500;
                    expect (dial->getHeight() >= (shortSheet ? 88 : 96)
                            && dial->getWidth() >= 80,
                            "touch quick-edit strip keeps real FREQ/GAIN/Q "
                            "controls large enough for fingers and text");
                    expect (dial->getY() < 70,
                            "touch EQ essentials immediately visible before "
                            "secondary inspector controls");
                }
            }
            if (sizeCase.width < 900 || sizeCase.height < 500)
            {
                expectEquals (primary[0]->getY(), primary[1]->getY(),
                              "quick-edit band strip aligns FREQ and GAIN");
                expectEquals (primary[1]->getY(), primary[2]->getY(),
                              "quick-edit band strip aligns GAIN and Q");
                expect (! primary[0]->getBounds().intersects (
                            primary[1]->getBounds()),
                        "FREQ and GAIN hit targets cannot overlap");
                expect (! primary[1]->getBounds().intersects (
                            primary[2]->getBounds()),
                        "GAIN and Q hit targets cannot overlap");
                if (sizeCase.height < 500)
                    for (const auto* dial : primary)
                        expect (dial->getBottom() + 10 <= inspector.getHeight(),
                            "short landscape numeric text box needs at least "
                            "10px breathing room BEFORE the bottom command "
                            "dock (previous native PNG cropped Hz/dB/Q)");
            }

            auto inBounds = [] (const juce::Rectangle<int>& child,
                                const juce::Rectangle<int>& parent)
            {
                return child.getWidth() > 0 && child.getHeight() > 0
                    && parent.contains (child);
            };
            expect (inBounds (topBar, editorBounds), "bottom dock in bounds");
            expect (inBounds (graph, editorBounds), "graph in bounds");
            expect (inBounds (inspector, editorBounds), "inspector in bounds");
            expect (! graph.intersects (topBar), "graph/bottom dock overlap");
            expect (! inspector.intersects (topBar), "inspector/bottom dock overlap");
            expectEquals (topBar.getBottom(), editorBounds.getBottom(),
                          "Pro-Q-style bottom command dock sits at window foot");

            const bool touchSheet = sizeCase.width < 900
                || sizeCase.height < 500;
            if (touchSheet)
                expect (! graph.intersects (inspector),
                        "touch phone/tablet inspector stays BELOW graph in "
                        "both landscape and portrait orientations");
            else
            {
                expect (graph.contains (inspector),
                        "floating APEX band HUD stays INSIDE graph");
                expectEquals (graph.getWidth(), sizeCase.width,
                              "immersive desktop graph uses the entire width");
                const auto selectedNode = editor->getNodeBoundsForTesting (0)
                    .translated (graph.getX(), graph.getY());
                expect (! inspector.intersects (selectedNode),
                        "floating band HUD cannot cover selected node");
                const auto otherNode = editor->getNodeBoundsForTesting (1)
                    .translated (graph.getX(), graph.getY());
                expect (! inspector.intersects (otherNode),
                        "floating HUD must preserve access to other EQ nodes");
                editor->setSelectedBandForTesting (1);
                const auto movedInspector = editor->getInspectorBoundsForTesting();
                expect (! movedInspector.intersects (
                    editor->getNodeBoundsForTesting (0).translated (
                        graph.getX(), graph.getY())),
                    "switching bands repositions the HUD without hiding prior band");
            }
            expect (graph.getWidth() >= 0 && graph.getHeight() >= 0);
            expect (graph.getHeight() >= 140, "graph must remain usable");
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= editorBounds.getWidth() * editorBounds.getHeight(),
                    "graph must stay a dominant surface");
            if (touchSheet)
                expect (graph.getWidth() * graph.getHeight() * 100
                            >= editorBounds.getWidth()
                                 * editorBounds.getHeight() * 45,
                        "touch canvas must reserve 45% of screen for response "
                        "in both landscape and portrait");

            const auto& placements = editor->getPlacementButtonsForTesting();
            expectEquals (placements.size(), static_cast<std::size_t> (5));
            for (const auto& button : placements)
            {
                expect (button->isEnabled(), "placement enabled");
                const auto bounds = button->getBounds();
                expect (bounds.getWidth() >= 40 && bounds.getHeight() >= 40,
                        "placement touch target size");
                expect (bounds.getX() >= 0 && bounds.getY() >= 0,
                        "placement non-negative");
            }
            for (const auto& button : placements)
                expect (! button->isVisible(),
                    "five legacy channel buttons must not clutter the HUD");
            auto& channel = editor->getPlacementComboForTesting();
            expect (channel.isVisible(), "single channel dropdown is visible");
            expectEquals (channel.getNumItems(), 5,
                          "Stereo/Left/Right/Mid/Side all remain available");
            expectEquals (channel.getSelectedId(), 1,
                          "per-band channel defaults to Stereo");
            expect (channel.getWidth() >= 44 && channel.getHeight() >= 40,
                    "channel selector stays touch accessible");

            const auto audition = editor->getAuditionButtonForTesting().getBounds();
            expect (editor->getAuditionButtonForTesting().isEnabled(),
                    "audition enabled with a selected band");
            expect (audition.getHeight() >= 44, "audition touch target height");
            expect (audition.getWidth() >= 60, "audition touch target width");
            expect (audition.getX() >= 0 && audition.getY() >= 0,
                    "audition non-negative");

            editor->setSelectedBandForTesting (-1);
            expect (! editor->getAuditionButtonForTesting().isEnabled(),
                    "audition disabled without selection");
            for (const auto& button : placements)
                expect (! button->isEnabled(), "placement disabled without selection");
            expect (! channel.isEnabled(), "channel selector disabled without band");
        }
    }

    void testPrismOffscreenRenderAndSnapshots()
    {
        beginTest ("APEX Prism native vector GUI renders at phone, tablet and desktop sizes");
        const auto workspace = juce::SystemStats::getEnvironmentVariable (
            "GITHUB_WORKSPACE", {});
        for (const auto& sizeCase : kSizes)
        {
            auto holder = std::make_unique<Processor>();
            auto& processor = *holder;
            enableBell (processor, 0, 930.0f, 8.0f, 2.4f);
            enableBell (processor, 1, 5200.0f, -5.0f, 3.0f);
            processor.prepareToPlay (48000.0, 128);
            processBlocks (processor);
            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (sizeCase.width, sizeCase.height);
            editor->setSelectedBandForTesting (0);
            editor->refreshResponseForTesting();
            // The host plugin window is opaque: use an opaque RGB backing
            // store instead of an initially transparent ARGB screenshot.
            // Release the Graphics renderer BEFORE querying pixels/PNG so
            // the backend can finish any pending rasterisation.
            juce::Image image (juce::Image::RGB, sizeCase.width,
                               sizeCase.height, true);
            {
                juce::Graphics graphics (image);
                editor->paintEntireComponent (graphics, false);
            }
            const auto graphBounds = editor->getGraphBoundsForTesting();
            const auto headerPixel = image.getPixelAt (4, 4);
            const auto graphPixel = image.getPixelAt (
                graphBounds.getCentreX(), graphBounds.getCentreY());
            // A genuinely painted native GUI has distinct navy header
            // and dark blue response-canvas surfaces. Merely allocating an
            // RGB image cannot satisfy this: all-clear-black is caught.
            expect (headerPixel != graphPixel,
                    "Prism offscreen screenshot must contain multiple "
                    "rendered UI surfaces: "
                        + juce::String (sizeCase.width) + "x"
                        + juce::String (sizeCase.height)
                        + " header=" + headerPixel.toDisplayString (true)
                        + " graph=" + graphPixel.toDisplayString (true));
            const auto graph = editor->getGraphBoundsForTesting();
            expect (graph.getWidth() * graph.getHeight() * 4
                        >= sizeCase.width * sizeCase.height,
                    "frequency graph remains dominant at every layout");

            // Export true software-rendered native JUCE screenshots from
            // Windows CI. The snapshots are QA previews, not AI mockups or
            // proof of audio quality. No files are written on user machines.
            // Save the ENTIRE responsive matrix to catch visual regressions
            // not observable from JUCE geometry assertions alone, including
            // 640x400 / 700x500 landscape and 900x300 short desktop.
            const bool snapshot = true;
            if (snapshot && workspace.isNotEmpty())
            {
                auto dir = juce::File (workspace).getChildFile (
                    "apex-eq-ui-snapshots");
                expect (dir.createDirectory().wasOk(),
                        "CI Prism snapshot folder must be writable");
                const auto file = dir.getChildFile (
                    "APEX-Prism-EQ-" + juce::String (sizeCase.width) + "x"
                    + juce::String (sizeCase.height) + ".png");
                auto stream = file.createOutputStream();
                expect (stream != nullptr, "CI PNG stream must open");
                if (stream != nullptr)
                {
                    juce::PNGImageFormat png;
                    expect (png.writeImageToStream (image, *stream),
                            "native Prism preview PNG must be valid");
                    stream->flush();
                }
            }
        }
    }

    void testDoubleClickCreatesNativeBandAndWheelEditsQ()
    {
        beginTest ("immersive graph double-click creates band and wheel edits Q");
        auto holder = std::make_unique<Processor>();
        auto& processor = *holder;
        processor.prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        const auto graph = editor->getGraphBoundsForTesting();
        const auto point = juce::Point<float> (
            static_cast<float> (graph.getWidth()) * 0.39f,
            static_cast<float> (graph.getHeight()) * 0.43f);
        const auto band = 0;
        auto* enabled = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Enabled));
        expect (! enabled->getBool(), "new processor begins with no active band");
        editor->injectGraphDoubleClickForTesting (point);
        expect (enabled->getBool(), "double-click creates a real hosted band");
        expectEquals (editor->getSelectedBandForTesting(), band);
        expect (graph.contains (editor->getInspectorBoundsForTesting()),
                "creation opens native floating inspector within canvas");
        const auto frequency = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Frequency))
                ->getUnitsValue();
        const auto expectedFrequency = 20.0 * std::pow (
            1200.0, 0.39);
        expectWithinAbsoluteError (
            static_cast<double> (frequency), expectedFrequency,
            expectedFrequency * 0.025, "created band's direct Hz location");

        auto* q = processor.getParametricEQParameter (
            parameterIndex (band, BandParameterOffset::Q));
        const float oldQ = q->getValue();
        const auto centre = editor->getNodeBoundsForTesting (band).getCentre();
        editor->injectGraphWheelForTesting (centre.toFloat(), 0.75f);
        expect (q->getValue() > oldQ,
                "hovering band wheel increases canonical hosted Q");
        expect (std::isfinite (q->getUnitsValue()),
                "wheel editing keeps Q finite");
    }

    void testNodeDragUpdatesParametersThroughRealHierarchy()
    {
        beginTest ("node drag updates frequency and gain with balanced gestures");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 0.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        processBlocks (processor);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        editor->setSelectedBandForTesting (0);
        const auto graphBounds = editor->getGraphBoundsForTesting();

        const auto start = editor->getNodeBoundsForTesting (0).getCentre();
        const double targetRatioX = 0.5;
        const double targetRatioY = 0.25;
        // Graph-local coordinates (the injected events target the graph).
        const auto target = juce::Point<float> (
            static_cast<float> (targetRatioX * graphBounds.getWidth()),
            static_cast<float> (targetRatioY * graphBounds.getHeight()));

        editor->injectGraphMouseDownForTesting (start.toFloat());
        editor->injectGraphMouseDragForTesting (target);
        editor->injectGraphMouseUpForTesting (target);
        // The draggable response nodes must stay interactable after their
        // parameter values move: the floating native HUD follows the band.
        const auto inspector = editor->getInspectorBoundsForTesting();
        const auto graphAfter = editor->getGraphBoundsForTesting();
        expect (graphAfter.contains (inspector),
                "moving a band keeps floating HUD inside graph");
        expect (! inspector.intersects (
                    editor->getNodeBoundsForTesting (0).translated (
                        graphAfter.getX(), graphAfter.getY())),
                "moving a band cannot strand its node underneath the HUD");

        const double logMin = std::log10 (20.0);
        const double logMax = std::log10 (24000.0);
        const double expectedFrequency = std::pow (10.0,
            logMin + targetRatioX * (logMax - logMin));
        const double expectedGain = 18.0 * (1.0 - 2.0 * targetRatioY);
        const double actualFrequency = static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))->getUnitsValue());
        const double actualGain = static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))->getUnitsValue());
        expectWithinAbsoluteError (actualFrequency, expectedFrequency,
                                   expectedFrequency * 0.05 + 1.0,
                                   "dragged frequency");
        expectWithinAbsoluteError (actualGain, expectedGain, 0.75,
                                   "dragged gain");
    }

    void testPlacementButtonsAreTouchAccessibleAndFunctional()
    {
        beginTest ("single Channel dropdown drives the stable placement parameter");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        editor->setSelectedBandForTesting (0);
        const auto& placements = editor->getPlacementButtonsForTesting();

        // Real JUCE click command path: triggerClick posts the click command
        // message and the dispatch loop delivers it to the same
        // internalClickCallback a genuine pointer press uses.
        juce::MessageManager::getInstance();
        auto press = [] (juce::TextButton& button)
        {
            button.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };

        auto& channel = editor->getPlacementComboForTesting();
        expectEquals (channel.getSelectedId(), 1, "Stereo is the default");
        for (int choice = 0; choice < kChannelPlacementCount; ++choice)
        {
            channel.setSelectedId (choice + 1, juce::sendNotificationSync);
            expectEquals (processor.getParametricEQParameter (
                              placementParameterIndex (0))->getChoiceIndex(),
                          choice, "dropdown changes canonical hosted parameter");
            expectEquals (channel.getText(), juce::String (channelPlacementName (
                              static_cast<ChannelPlacement> (choice))),
                          "dropdown displays active placement");
        }
        // Old API is preserved for backwards-compatible programmatic use,
        // but its five buttons never appear in the on-screen inspector.
        for (const auto& button : placements)
            expect (! button->isVisible(), "no duplicate channel buttons");
        press (*placements[0]);
        expectEquals (processor.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 0);
        editor->setSelectedBandForTesting (0);
        expectEquals (channel.getSelectedId(), 1);
    }

    void testAuditionHoldGestureLifecycle()
    {
        beginTest ("audition hold gesture begins, ends and survives editor teardown");
        {
            auto processorStorage = std::make_unique<Processor>();
            auto& processor = *processorStorage;
            enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
            processor.prepareToPlay (48000.0, 512);

            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (900, 620);
            editor->setSelectedBandForTesting (0);
            editor->beginAuditionGesture();
            expect (editor->isAuditionArmedForTesting());
            processBlocks (processor, 2);
            expect (processor.isAuditionHoldingForTesting(),
                    "processor must hold audition after press");
            editor->endAuditionGesture();
            expect (! editor->isAuditionArmedForTesting());
            processBlocks (processor, 8);
            expect (! processor.isAuditionHoldingForTesting(),
                    "processor must release after the matching end");
        }

        // Editor teardown must never leave audition latched.
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);
        {
            auto editor = std::make_unique<ParametricEQEditor> (processor);
            editor->setSize (900, 620);
            editor->setSelectedBandForTesting (0);
            editor->beginAuditionGesture();
            processBlocks (processor, 2);
            expect (processor.isAuditionHoldingForTesting());
        }
        processBlocks (processor, 8);
        expect (! processor.isAuditionHoldingForTesting(),
                "editor destruction must cancel audition");
    }

    void testLongPressCreatesBand()
    {
        beginTest ("long-press on empty graph creates a band");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        for (int band = 0; band < kMaxBands; ++band)
            expect (! processor.getParametricEQParameter (
                        parameterIndex (band, BandParameterOffset::Enabled))->getBool());

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        const auto graphBounds = editor->getGraphBoundsForTesting();

        juce::MessageManager::getInstance();
        // Graph-local coordinates for the injected events.
        const auto position = juce::Point<float> (
            static_cast<float> (graphBounds.getWidth() * 0.4),
            static_cast<float> (graphBounds.getHeight() * 0.5));
        editor->injectGraphMouseDownForTesting (position);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
        editor->injectGraphMouseUpForTesting (position);

        int created = -1;
        for (int band = 0; band < kMaxBands; ++band)
            if (processor.getParametricEQParameter (
                    parameterIndex (band, BandParameterOffset::Enabled))->getBool())
            {
                created = band;
                break;
            }
        expect (created >= 0, "long-press must enable a band");
        expectEquals (editor->getSelectedBandForTesting(), created,
                      "created band becomes the selection");
    }

    void testHeldNodeNeverTriggersImplicitAudition()
    {
        beginTest ("held and dragged EQ nodes keep the vocal path intact");
        auto storage = std::make_unique<Processor>();
        auto& processor = *storage;
        enableBell (processor, 0, 1000.0f, 7.0f, 2.0f);
        processor.prepareToPlay (48000.0, 128);
        processBlocks (processor, 2);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        editor->setSelectedBandForTesting (0);
        const auto start = editor->getNodeBoundsForTesting (0).getCentre().toFloat();
        editor->injectGraphMouseDownForTesting (start);

        // This previously called beginAuditionGesture() after 450 ms:
        // playback became isolated to one narrow frequency region, often
        // making the singer disappear until finger-up.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (620);
        processBlocks (processor, 2);
        expect (! editor->isAuditionArmedForTesting(),
                "holding a move handle must never arm solo audition");
        expect (! processor.isAuditionHoldingForTesting(),
                "the processor must not silence the singer during a held move");

        editor->injectGraphMouseDragForTesting (
            start.translated (43.0f, -24.0f));
        processBlocks (processor, 3);
        expect (! processor.isAuditionHoldingForTesting(),
                "continuous drag must stay in full-mix mode");
        editor->injectGraphMouseUpForTesting (start);
        expect (! editor->isAuditionArmedForTesting());
    }

    void testSketchGestureCreatesBands()
    {
        beginTest ("EQ Sketch converts a freehand gesture into native bands");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        auto& sketch = editor->getSketchButtonForTesting();
        expect (sketch.getBounds().getHeight() >= 44,
                "Sketch must retain a touch-safe target");

        juce::MessageManager::getInstance();
        sketch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("SKETCH ON"), sketch.getButtonText());

        const auto graph = editor->getGraphBoundsForTesting();
        const auto pointFor = [&graph] (double xRatio, double gainDb)
        {
            const auto yRatio = 0.5 * (1.0 - gainDb / 18.0);
            return juce::Point<float> (
                static_cast<float> (xRatio * graph.getWidth()),
                static_cast<float> (yRatio * graph.getHeight()));
        };

        editor->injectGraphMouseDownForTesting (pointFor (0.20, 0.0));
        for (int step = 1; step <= 12; ++step)
        {
            const double x = 0.20 + 0.05 * step;
            const double dx = (x - 0.50) / 0.09;
            const double gain = 8.0 * std::exp (-0.5 * dx * dx);
            editor->injectGraphMouseDragForTesting (pointFor (x, gain));
        }
        editor->injectGraphMouseUpForTesting (pointFor (0.80, 0.0));

        int enabled = 0;
        for (int band = 0; band < kMaxBands; ++band)
            enabled += processor.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled))->getBool()
                     ? 1 : 0;

        expect (enabled >= 1, "Sketch should publish at least one band");
        expectEquals (juce::String ("SKETCH"), sketch.getButtonText(),
                      "Sketch returns to one-shot idle mode after commit");
        expect (editor->getSelectedBandForTesting() >= 0,
                "last created Sketch band becomes selected");
    }


    void testSpectrumGrabAndTouchToBand()
    {
        beginTest ("Spectrum Grab detects true resonances, rejects flat/NaN, "
                   "and inserts a native EQ band via a 44px touch marker");
        constexpr int bins = APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        constexpr double binHz = 48000.0
            / APEX::Analysis::SpectrumAnalyzerCore::kFftSize;
        std::array<float, bins> spectrum {};
        spectrum.fill (-40.0f);
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, binHz).count, 0,
            "a flat loud spectrum has no narrow resonances");
        spectrum.fill (-120.0f);
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, binHz).count, 0,
            "silence must not create candidates");
        expectEquals (SpectrumGrab::analyze (
            nullptr, bins, binHz).count, 0, "null frame");
        expectEquals (SpectrumGrab::analyze (
            spectrum.data(), bins, std::numeric_limits<double>::quiet_NaN())
            .count, 0, "nonfinite bin frequency");

        for (int bin = 0; bin < bins; ++bin)
        {
            const double f = std::max (1.0, bin * binHz);
            const double low = std::log2 (f / 1500.0) / 0.025;
            const double high = std::log2 (f / 5400.0) / 0.030;
            spectrum[static_cast<std::size_t> (bin)] = static_cast<float> (
                -92.0 + 75.0 * std::exp (-0.5 * low * low)
                      + 60.0 * std::exp (-0.5 * high * high));
        }
        const auto plan = SpectrumGrab::analyze (
            spectrum.data(), bins, binHz);
        expect (plan.count >= 2, "both resonances should be detected");
        bool foundLow = false, foundHigh = false;
        for (int i = 0; i < plan.count; ++i)
        {
            const auto& peak = plan.peaks[static_cast<std::size_t> (i)];
            expect (std::isfinite (peak.frequencyHz)
                    && std::isfinite (peak.suggestedQ));
            expect (peak.suggestedQ >= 0.7 && peak.suggestedQ <= 20.0);
            foundLow |= std::abs (peak.frequencyHz - 1500.0) < 50.0;
            foundHigh |= std::abs (peak.frequencyHz - 5400.0) < 80.0;
        }
        expect (foundLow && foundHigh, "resonance center frequencies");

        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        processor.prepareToPlay (48000.0, 512);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (1280, 720);
        auto& grab = editor->getGrabButtonForTesting();
        expect (grab.getBounds().getHeight()
                >= ParametricEQEditor::kTouchTarget);
        expect (! grab.getBounds().intersects (
            editor->getSketchButtonForTesting().getBounds()),
            "touch buttons do not overlap");
        editor->setGrabSpectrumForTesting (spectrum.data(), bins, binHz);
        expectEquals (juce::String ("GRAB ON"), grab.getButtonText());
        const auto marker = editor->getGrabPeakBoundsForTesting (0);
        expect (marker.getWidth() >= ParametricEQEditor::kTouchTarget);
        editor->injectGraphMouseDownForTesting (marker.getCentre().toFloat());
        editor->injectGraphMouseUpForTesting (marker.getCentre().toFloat());
        const auto* enabled = processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled));
        expect (enabled->getBool(), "tapping Grab marker enables a native band");
        expectWithinAbsoluteError (static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                    ->getUnitsValue()), plan.peaks[0].frequencyHz,
            40.0, "grab frequency is published to the stable parameter");
        expectWithinAbsoluteError (static_cast<double> (
            processor.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                    ->getUnitsValue()), -3.0, 0.1,
            "initial resonance cut starts at -3 dB");
        expectEquals (juce::String ("GRAB"), grab.getButtonText(),
                      "one-shot preview automatically disarms");
    }


    void testNativeBandCopyPasteAcrossInstances()
    {
        beginTest ("APEX copy/paste transfers full native band to another EQ "
                   "instance without mutating the source or parameter ABI");
        auto sourceStorage = std::make_unique<Processor>();
        auto destinationStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        auto& destination = *destinationStorage;
        enableBell (source, 0, 2400.0f, -6.5f, 3.2f);
        setUnits (source, placementParameterIndex (0), 4.0f);
        setUnits (source, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (source, dynamicParameterIndex (
            0, DynamicBandOffset::Range), -4.0f);
        setUnits (source, dynamicFilterParameterIndex (0), 1.0f);
        setUnits (source, spectralBandParameterIndex (0), 1.0f);
        source.prepareToPlay (48000.0, 256);
        destination.prepareToPlay (48000.0, 256);
        auto sourceEditor = std::make_unique<ParametricEQEditor> (source);
        auto targetEditor = std::make_unique<ParametricEQEditor> (destination);
        sourceEditor->setSize (320, 568);
        targetEditor->setSize (320, 568);
        sourceEditor->setSelectedBandForTesting (0);

        auto& copy = sourceEditor->getCopyButtonForTesting();
        auto& paste = targetEditor->getPasteButtonForTesting();
        const auto sourceGraph = sourceEditor->getGraphBoundsForTesting();
        const auto targetGraph = targetEditor->getGraphBoundsForTesting();
        expect (copy.getBounds().getHeight() >= 44
                && paste.getBounds().getHeight() >= 44,
                "clipboard controls are touch-safe");
        expect (copy.getRight() <= sourceGraph.getWidth()
                && paste.getRight() <= targetGraph.getWidth(),
                "clipboard controls fit on narrow phones");

        juce::MessageManager::getInstance();
        copy.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        paste.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (destination.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled))->getBool(),
            "paste enabled a new band in another native EQ instance");
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Frequency))
                ->getUnitsValue()), 2400.0, 1.0);
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                ->getUnitsValue()), -6.5, 0.05);
        expectEquals (destination.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 4);
        expect (destination.getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool());
        expect (destination.getParametricEQParameter (
            dynamicFilterParameterIndex (0))->getBool());
        expect (destination.getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool(),
            "native clipboard must include the appended per-band spectral mode");
        expectWithinAbsoluteError (static_cast<double> (
            destination.getParametricEQParameter (
                dynamicParameterIndex (0, DynamicBandOffset::Range))
                ->getUnitsValue()), -4.0, 0.1);
        expectEquals (targetEditor->getSelectedBandForTesting(), 0);
        // Source and destination are independent native processor instances.
        expectWithinAbsoluteError (static_cast<double> (
            source.getParametricEQParameter (
                parameterIndex (0, BandParameterOffset::Gain))
                ->getUnitsValue()), -6.5, 0.05);
    }

    void testMeasuredEqMatchAndGuidedNativeApply()
    {
        beginTest ("APEX Match measured PRE reference versus input, level "
                   "invariance, safety guards and guided hosted band insert");
        constexpr int bins = APEX::Analysis::SpectrumAnalyzerCore::kSpectrumBins;
        constexpr double binHz = 48000.0
            / APEX::Analysis::SpectrumAnalyzerCore::kFftSize;
        std::array<float, bins> input {}, reference {}, silent {};
        input.fill (-44.0f);
        silent.fill (-120.0f);
        for (int bin = 0; bin < bins; ++bin)
        {
            const double hz = std::max (1.0, bin * binHz);
            const double normalized = std::log2 (hz / 1400.0) / 0.20;
            reference[static_cast<std::size_t> (bin)] = static_cast<float> (
                -44.0 + 8.5 * std::exp (-0.5 * normalized * normalized));
        }

        MatchCapture referenceCapture, inputCapture;
        expect (! referenceCapture.append (nullptr, bins, binHz),
                "null frame is rejected");
        expect (! referenceCapture.append (silent.data(), bins, binHz),
                "silence must never count as a reference frame");
        auto corrupt = reference;
        corrupt.fill (std::numeric_limits<float>::quiet_NaN());
        expect (! referenceCapture.append (corrupt.data(), bins, binHz),
                "NaN frames are rejected atomically");
        expectEquals (referenceCapture.frameCount(), 0,
                      "invalid frames did not advance capture");

        for (int frame = 0; frame < MatchCapture::kRequiredFrames; ++frame)
        {
            expect (referenceCapture.append (reference.data(), bins, binHz),
                    "reference capture must accept finite live frames");
            expect (inputCapture.append (input.data(), bins, binHz),
                    "input capture must accept finite live frames");
        }
        expect (referenceCapture.ready() && inputCapture.ready());
        expect (! inputCapture.append (input.data(), bins, binHz),
                "bounded capture stops at its fixed frame budget");
        const auto fitted = MatchEngine::fit (referenceCapture, inputCapture);
        expect (fitted.count >= 1,
                "reference 1.4 kHz peak must yield an APEX fit proposal");
        bool located = false;
        for (int i = 0; i < fitted.count; ++i)
        {
            const auto& suggestion = fitted.bands[
                static_cast<std::size_t> (i)];
            located |= std::abs (suggestion.frequencyHz - 1400.0) < 250.0;
            expect (std::isfinite (suggestion.frequencyHz)
                && std::isfinite (suggestion.gainDb)
                && std::isfinite (suggestion.q),
                "match suggestions must be finite");
            expect (std::abs (suggestion.gainDb) <= 9.001,
                    "matching must remain bounded to +/-9 dB");
        }
        expect (located, "native fitter found the deliberately colored range");

        MatchCapture louder;
        auto boosted = input;
        boosted.fill (-29.0f);  // 15 dB whole-signal level difference
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            expect (louder.append (boosted.data(), bins, binHz));
        expectEquals (MatchEngine::fit (louder, inputCapture).count, 0,
            "level-only difference is not a tonal mismatch");
        expectEquals (MatchEngine::fit (referenceCapture, MatchCapture {})
            .count, 0, "partial/empty input must not create bands");

        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        // Reuse a disabled slot with stale spectral/dynamic values: EQ Match
        // must create a plain fresh band, not re-enable hidden old effects.
        setUnits (processor, spectralBandParameterIndex (0), 1.0f);
        setUnits (processor, dynamicParameterIndex (
            0, DynamicBandOffset::Enable), 1.0f);
        setUnits (processor, parameterIndex (
            0, BandParameterOffset::Bypass), 1.0f);
        setUnits (processor, placementParameterIndex (0), 4.0f);
        processor.prepareToPlay (48000.0, 512);
        processor.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Pre);
        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (320, 568);
        auto& match = editor->getMatchButtonForTesting();
        expect (match.getBounds().getHeight() >= 44,
                "EQ MATCH touch button fits compact phone");
        expect (match.getBounds().getRight()
                <= editor->getGraphBoundsForTesting().getWidth());
        juce::MessageManager::getInstance();
        auto click = [&match]()
        {
            match.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };
        click();
        expectEquals (juce::String ("REF..."), match.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            editor->feedMatchSpectrumForTesting (
                reference.data(), bins, binHz);
        expectEquals (juce::String ("GET INPUT"), match.getButtonText());

        // A second EQ instance can explicitly adopt the measured reference
        // without borrowing the first processor's lifetime or analyzer frame.
        auto& save = editor->getMatchSaveButtonForTesting();
        auto& load = editor->getMatchLoadButtonForTesting();
        expect (save.getBounds().getHeight() >= 44
            && load.getBounds().getHeight() >= 44);
        expect (! save.isVisible() && ! load.isVisible(),
            "reference actions remain reachable via TOOLS without adding "
            "overlapping visible graph buttons");
        save.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("SAVED"), save.getButtonText());
        auto secondProcessorStorage = std::make_unique<Processor>();
        auto& secondProcessor = *secondProcessorStorage;
        secondProcessor.prepareToPlay (48000.0, 512);
        secondProcessor.setAnalyzerMode (APEX::Analysis::SpectrumTapMode::Pre);
        auto secondEditor = std::make_unique<ParametricEQEditor> (
            secondProcessor);
        secondEditor->setSize (320, 568);
        secondEditor->getMatchLoadButtonForTesting().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("GET INPUT"),
            secondEditor->getMatchButtonForTesting().getButtonText(),
            "a second EQ instance loaded the independently owned reference");

        click();
        expectEquals (juce::String ("INPUT..."), match.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            editor->feedMatchSpectrumForTesting (input.data(), bins, binHz);
        expectEquals (juce::String ("APPLY EQ"), match.getButtonText());
        click();
        int inserted = 0;
        for (int band = 0; band < kMaxBands; ++band)
            inserted += processor.getParametricEQParameter (
                parameterIndex (band, BandParameterOffset::Enabled))->getBool()
                ? 1 : 0;
        expect (inserted >= 1, "Apply inserts real APEX EQ bands");
        expect (! processor.getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool(),
            "newly created Match band cannot inherit stale spectral enable");
        expect (! processor.getParametricEQParameter (
            dynamicParameterIndex (0, DynamicBandOffset::Enable))->getBool(),
            "newly created Match band cannot inherit hidden old dynamics");
        expect (! processor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Bypass))->getBool(),
            "newly created Match band must actually affect output");
        expectEquals (processor.getParametricEQParameter (
            placementParameterIndex (0))->getChoiceIndex(), 0,
            "newly created Match band defaults to true stereo");
        expectEquals (juce::String ("MATCH"), match.getButtonText(),
                      "matching resets to compact-width button text after apply");

        // The second instance uses the shared reference against its own
        // input, applying a *new* band without changing the source instance.
        auto& secondMatch = secondEditor->getMatchButtonForTesting();
        secondMatch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expectEquals (juce::String ("INPUT..."), secondMatch.getButtonText());
        for (int i = 0; i < MatchCapture::kRequiredFrames; ++i)
            secondEditor->feedMatchSpectrumForTesting (
                input.data(), bins, binHz);
        expectEquals (juce::String ("APPLY EQ"), secondMatch.getButtonText());
        secondMatch.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (secondProcessor.getParametricEQParameter (
            parameterIndex (0, BandParameterOffset::Enabled))->getBool(),
            "second plugin independently fits and applies shared reference");
    }

    void testSpectralInspectorAndDeferredPdc()
    {
        beginTest ("native spectral inspector preserves touch dimensions "
                   "and advertises transport restart for PDC");
        auto processor = std::make_unique<Processor>();
        processor->prepareToPlay (48000.0, 128);
        auto editor = std::make_unique<ParametricEQEditor> (*processor);
        editor->setSize (320, 568);
        editor->setSelectedBandForTesting (0);
        auto& arm = editor->getSpectralArmButtonForTesting();
        auto& band = editor->getSpectralBandButtonForTesting();

        expect (arm.getBounds().getHeight() >= 44);
        expect (band.getBounds().getHeight() >= 44);
        expect (! arm.getBounds().intersects (band.getBounds()));
        expect (arm.isEnabled());
        expect (band.isEnabled());
        expect (! processor->getParametricEQParameter (
            kSpectralArmParameter)->getBool());

        arm.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (processor->getParametricEQParameter (
            kSpectralArmParameter)->getBool());
        expect (arm.getButtonText().containsIgnoreCase ("RESTART"),
                "user must not be misled that host PDC switches mid-recording");
        expect (processor->isSpectralRepreparePendingForTesting());
        expectEquals (processor->getLatencySamples(), 0);

        band.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (processor->getParametricEQParameter (
            spectralBandParameterIndex (0))->getBool());
        processor->prepareToPlay (48000.0, 128);
        editor->setSelectedBandForTesting (0);
        expectEquals (processor->getLatencySamples(), 1024);
        expect (! processor->isSpectralRepreparePendingForTesting());
        expect (arm.getButtonText().containsIgnoreCase ("ON"),
                "inspector reflects actual prepared spectral path");

        for (int index = 0; index < kNumParameters; ++index)
            expect (! Processor::getParameterId (index).isEmpty(),
                    "spectral extension must retain stable hosted identity");
    }

    void testPhaseSelectorDrivesCanonicalParameter()
    {
        beginTest ("phase selector drives peq.phase and reflects external state");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        enableBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        auto editor = std::make_unique<ParametricEQEditor> (processor);
        editor->setSize (900, 620);
        auto& phaseButton = editor->getPhaseButtonForTesting();
        auto* phaseParameter = processor.getParametricEQParameter (
            kPhaseModeParameter);

        expectEquals (juce::String ("Minimum"), phaseButton.getButtonText());
        expect (! phaseParameter->getBool());

        auto press = [] (juce::TextButton& button)
        {
            button.triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        };

        // Minimum -> Linear through the real click command path.
        press (phaseButton);
        expect (phaseParameter->getBool(),
                "click switches peq.phase to linear");
        expectEquals (juce::String ("Linear"), phaseButton.getButtonText());

        // Linear -> Minimum.
        press (phaseButton);
        expect (! phaseParameter->getBool(),
                 "second click returns peq.phase to minimum");
        expectEquals (juce::String ("Minimum"), phaseButton.getButtonText());

        // External host/state changes must reach the top bar through the
        // low-rate synchronization timer, not only through button clicks.
        phaseParameter->setValueNotifyingHost (1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        expectEquals (juce::String ("Linear"), phaseButton.getButtonText(),
                      "external parameter change is reflected by the GUI");

        // Editor reopen must synchronize with the canonical parameter.
        editor.reset();
        auto reopened = std::make_unique<ParametricEQEditor> (processor);
        reopened->setSize (900, 620);
        expectEquals (juce::String ("Linear"),
                      reopened->getPhaseButtonForTesting().getButtonText(),
                      "editor reopen reflects the canonical parameter");

        // State restore into a fresh processor must carry peq.phase.
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (state.getData(),
                                      static_cast<int> (state.getSize()));
        expect (restored.getParametricEQParameter (kPhaseModeParameter)->getBool(),
                "peq.phase survives a state round-trip");
        auto restoredEditor = std::make_unique<ParametricEQEditor> (restored);
        restoredEditor->setSize (900, 620);
        expectEquals (juce::String ("Linear"),
                      restoredEditor->getPhaseButtonForTesting().getButtonText(),
                      "restored editor shows the restored phase mode");

        // The hosted Phase control lives in Utility instead of adding a
        // sixth permanent footer button; hidden click path is still tested.
        for (const auto& sizeCase : kSizes)
        {
            restoredEditor->setSize (sizeCase.width, sizeCase.height);
            const auto topBar = restoredEditor->getTopBarBoundsForTesting();
            // Phase button bounds are LOCAL to its bottom-dock parent;
            // translate into editor coordinates before containment. The
            // pre-V2 test only worked because the bar started at (0, 0).
            const auto bounds = restoredEditor->getPhaseButtonForTesting()
                .getBounds().translated (topBar.getX(), topBar.getY());
            expect (topBar.contains (bounds),
                    "phase button stays inside its bottom command dock");
            expect (bounds.getWidth() >= 44, "phase touch target width");
            expect (bounds.getHeight() >= 32, "phase touch target height");
        }
    }
};

static ParametricEQEditorTests parametricEQEditorTests;

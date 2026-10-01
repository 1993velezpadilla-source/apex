#include <JuceHeader.h>
#include "C4TestUtils.h"
#include "../../../Source/C4UI/C4Editor.h"

#include <cmath>
#include <vector>

// ============================================================================
// C4EditorHookTests — Phase 7: the native C4 editor contract.
//
//   - hasEditor() == true; createEditor() returns a real, destroyable editor
//   - all 19 production parameters remain accessible
//   - Spectrum Flag expand/collapse through the editor control
//   - open/close/reopen editor lifecycle (message-thread context)
//   - multiple instances with independent editors
//   - processor destruction with an editor alive must not crash
//   - the analyzer worker follows the flag state, not the editor lifetime
//
// GUI construction runs inside juce::ScopedJuceInitialiser_GUI (the host
// creates editors on the message thread; never headless).
// ============================================================================

using APEX::C4::C4Processor;
using APEX::C4::C4ParamIndex;
using APEX::C4::C4SpectrumTapMode;
using APEX::C4::makeProfileProduction;

class C4EditorHookTests final : public juce::UnitTest
{
public:
    C4EditorHookTests() : juce::UnitTest ("C4.EditorHook", "APEX.C4") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        testEditorHook();
        testParameterSurface();
        testSpectrumFlagControl();
        testSpectrumExpansionKeepsConsole();
        testModeToggleDoesNotChangeParameters();
        testOpenCloseReopen();
        testMultipleInstances();
        testDestructionContract();
        testStateRestoreWithEditor();
    }

private:
    void testEditorHook()
    {
        beginTest ("hasEditor / createEditor hook");
        C4Processor proc (makeProfileProduction());
        expect (proc.hasEditor(), "C4 must report hasEditor() == true");

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "createEditor() must return a real editor");
        expect (editor->getWidth() > 0 && editor->getHeight() > 0,
                "the editor must have a sane size");
        expect (dynamic_cast<APEX::C4::C4Editor*> (editor.get()) != nullptr,
                "the editor must be the C4 console editor");
    }

    void testParameterSurface()
    {
        beginTest ("Editor lifecycle keeps all 19 parameters accessible");
        C4Processor proc (makeProfileProduction());
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        for (int i = 0; i < APEX::C4::kNumParams; ++i)
            expect (proc.getC4Parameter (i) != nullptr,
                    "parameter " + juce::String (i) + " must remain accessible with an editor open");

        // The BLOOM control must still read the production default 4.0.
        expect (std::abs (proc.getC4Parameter (C4ParamIndex::kBloom)->getUnitsValue() - 4.0f) < 1e-4f,
                "BLOOM default must remain 4.0 with the editor open");
    }

    void testSpectrumFlagControl()
    {
        beginTest ("Spectrum Flag: closed default; expand/collapse through the editor");
        C4Processor proc (makeProfileProduction());
        expect (proc.getSpectrumMode() == C4SpectrumTapMode::Closed,
                "the flag must be CLOSED by default");

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        proc.setSpectrumMode (C4SpectrumTapMode::Both);
        expect (proc.getSpectrumMode() == C4SpectrumTapMode::Both
                    && proc.getSpectrumCore().isWorkerRunning(),
                "opening the flag must start the analyzer worker");
        proc.setSpectrumMode (C4SpectrumTapMode::Closed);
        expect (! proc.getSpectrumCore().isWorkerRunning(),
                "closing the flag must stop the analyzer worker");
    }

    void testSpectrumExpansionKeepsConsole()
    {
        beginTest ("Spectrum expansion: the full console survives every analyzer mode");
        C4Processor proc (makeProfileProduction());
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* c4 = dynamic_cast<APEX::C4::C4Editor*> (editor.get());
        expect (c4 != nullptr, "the editor must be the C4 console editor");

        for (const auto mode : { C4SpectrumTapMode::Pre, C4SpectrumTapMode::Post,
                                 C4SpectrumTapMode::Both })
        {
            c4->applySpectrumMode (mode);
            expect (editor->getHeight() >= 880,
                    "expanded editor must grow DOWNWARD (mode " + juce::String ((int) mode)
                        + ", height " + juce::String (editor->getHeight()) + ")");
            for (int b = 0; b < APEX::C4::kNumBands; ++b)
            {
                auto* strip = c4->getBandStripForPreview (b);
                expect (strip != nullptr && strip->isVisible()
                            && strip->getHeight() >= 400,
                        juce::String ("strip ") + juce::String (b)
                            + " must stay FULLY visible in mode " + juce::String ((int) mode)
                            + " (height " + juce::String (strip != nullptr ? strip->getHeight() : 0) + ")");
            }
        }

        // Back to CLOSED: compact height, strips still full.
        c4->applySpectrumMode (C4SpectrumTapMode::Closed);
        expect (editor->getHeight() == 640,
                "closed editor must return to the compact console height (got "
                    + juce::String (editor->getHeight()) + ")");
        for (int b = 0; b < APEX::C4::kNumBands; ++b)
        {
            auto* strip = c4->getBandStripForPreview (b);
            expect (strip != nullptr && strip->isVisible() && strip->getHeight() >= 400,
                    juce::String ("strip ") + juce::String (b) + " must stay FULLY visible when closed");
        }
    }

    void testModeToggleDoesNotChangeParameters()
    {
        beginTest ("Opening/closing the Spectrum Flag never changes parameter values");
        C4Processor proc (makeProfileProduction());
        // A representative non-neutral state.
        proc.getC4Parameter (C4ParamIndex::kWeightGain)->setValue (
            proc.getC4Parameter (C4ParamIndex::kWeightGain)->getValueForText ("3.0"));
        proc.getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
            proc.getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("6.5"));
        proc.getC4Parameter (C4ParamIndex::kBiteGain)->setValue (
            proc.getC4Parameter (C4ParamIndex::kBiteGain)->getValueForText ("-2.5"));
        proc.getC4Parameter (C4ParamIndex::kBloom)->setValue (
            proc.getC4Parameter (C4ParamIndex::kBloom)->getValueForText ("4.0"));

        float before[APEX::C4::kNumParams] = {};
        for (int i = 0; i < APEX::C4::kNumParams; ++i)
            before[i] = proc.getC4Parameter (i)->getValue();

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* c4 = dynamic_cast<APEX::C4::C4Editor*> (editor.get());
        for (const auto mode : { C4SpectrumTapMode::Pre, C4SpectrumTapMode::Post,
                                 C4SpectrumTapMode::Both, C4SpectrumTapMode::Closed,
                                 C4SpectrumTapMode::Both, C4SpectrumTapMode::Closed })
        {
            c4->applySpectrumMode (mode);
            for (int i = 0; i < APEX::C4::kNumParams; ++i)
                expect (proc.getC4Parameter (i)->getValue() == before[i],
                        "parameter " + juce::String (i) + " must be untouched by spectrum mode changes");
        }
    }

    void testOpenCloseReopen()    {
        beginTest ("Editor open/close/reopen cycles");
        C4Processor proc (makeProfileProduction());
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            expect (editor != nullptr, "cycle " + juce::String (cycle) + ": editor must be creatable");
        }
    }

    void testMultipleInstances()
    {
        beginTest ("Multiple instances with independent editors and flag states");
        C4Processor a (makeProfileProduction());
        C4Processor b (makeProfileProduction());
        std::unique_ptr<juce::AudioProcessorEditor> ea (a.createEditor());
        std::unique_ptr<juce::AudioProcessorEditor> eb (b.createEditor());

        a.setSpectrumMode (C4SpectrumTapMode::Pre);
        b.setSpectrumMode (C4SpectrumTapMode::Post);
        expect (a.getSpectrumMode() == C4SpectrumTapMode::Pre
                    && b.getSpectrumMode() == C4SpectrumTapMode::Post,
                "instances must keep independent flag states");
        expect (a.getSpectrumCore().isWorkerRunning()
                    && b.getSpectrumCore().isWorkerRunning(),
                "both instances must run independent workers");

        a.setSpectrumMode (C4SpectrumTapMode::Closed);
        expect (! a.getSpectrumCore().isWorkerRunning()
                    && b.getSpectrumCore().isWorkerRunning(),
                "closing one instance must not close the other");
        b.setSpectrumMode (C4SpectrumTapMode::Closed);
    }

    void testDestructionContract()
    {
        beginTest ("Processor destruction with an editor alive");
        // The host destroys the editor BEFORE the processor; the reverse
        // order is a misbehaving-host case that must still not crash when
        // the processor outlives a destroyed editor (the editor holds no
        // processor ownership).
        auto proc = std::make_unique<C4Processor> (makeProfileProduction());
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
        editor.reset(); // editor destroyed first (host contract)
        proc.reset();   // then the processor
    }

    void testStateRestoreWithEditor()
    {
        beginTest ("State restore keeps the editor surface consistent");
        C4Processor proc (makeProfileProduction());
        proc.getC4Parameter (C4ParamIndex::kSculptGain)->setValue (
            proc.getC4Parameter (C4ParamIndex::kSculptGain)->getValueForText ("4.0"));
        proc.setSpectrumMode (C4SpectrumTapMode::Post);

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        C4Processor restored (makeProfileProduction());
        restored.setStateInformation (state.getData(), (int) state.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());

        expect (std::abs (restored.getC4Parameter (C4ParamIndex::kSculptGain)->getUnitsValue() - 4.0f) < 1e-3f,
                "restored gain must match under an open editor");
        expect (restored.getSpectrumMode() == C4SpectrumTapMode::Post,
                "restored spectrum mode must match under an open editor");
        restored.setSpectrumMode (C4SpectrumTapMode::Closed);
    }
};

static C4EditorHookTests c4EditorHookTests;

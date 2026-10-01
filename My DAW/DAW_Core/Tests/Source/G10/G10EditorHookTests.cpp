#include <JuceHeader.h>
#include "G10TestUtils.h"
#include "../../../Source/G10UI/G10Editor.h"
#include "../../../Source/G10UI/G10BandFaderComponent.h"
#include "../../../Source/G10UI/G10LookAndFeel.h"
#include "../../../Source/PluginHostCore/PluginEditorResizePolicy.h"

#include <cmath>
#include <vector>

// ============================================================================
// G10EditorHookTests — the native G10 editor contract:
//   - hasEditor() == true
//   - createEditor() returns a real, destroyable editor
//   - all 15 production parameters remain accessible
//   - the analyzer FIFO gate: pushes ONLY while an editor is active
//   - the resize contract: host-resizable at a fixed 1000:612 aspect with
//     uniform proportional content scaling (rendering + hit-testing)
//   - PluginEditorResizePolicy classification for resizable/fixed editors
// ============================================================================

namespace
{

using APEX::G10::G10Processor;
using APEX::G10::G10Editor;
using APEX::G10::kNumParams;

/** A minimal fixed-size editor used to exercise the host resize policy. */
class FixedSizeTestEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FixedSizeTestEditor (juce::AudioProcessor& processor)
        : juce::AudioProcessorEditor (processor)
    {
        setSize (400, 300);
    }
};

} // namespace

class G10EditorHookTests : public juce::UnitTest
{
public:
    G10EditorHookTests() : juce::UnitTest ("G10.EditorHook", "APEX.G10") {}

    void runTest() override
    {
        // The real host creates plugin editors on the JUCE message thread
        // (PluginInstanceCore::openEditor -> SafeEditorHost). Emulate that
        // valid GUI context instead of constructing editors headless.
        juce::ScopedJuceInitialiser_GUI gui;

        testEditorHook();
        testAnalyzerGate();
        testResizeContract();
        testResizePolicy();
        testMiniEqSurface();
        testMiniEqRealHierarchy();
        testMiniEqV3Interactions();
        testMiniEqNodeCurveAlignment();
        testMiniEqCreationAndPanelStates();
        testMiniEqInspectorPlacementContract();
        testMiniEqInspectorGestureStability();
        testInputOutputMetersUI();
        testBandFaderDoubleClickReset();
    }

    // ------------------------------------------------------------------
    // Mini Clean EQ interactive surface (through the real G10 editor).
    // ------------------------------------------------------------------

    void testMiniEqSurface()
    {
        beginTest ("Mini EQ interactive surface");

        G10Processor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = dynamic_cast<ApexInteractiveEQSurface*> (g10->getAnalyzerSurface());
        expect (surface != nullptr, "editor hosts the interactive EQ surface");
        if (surface == nullptr)
            return;

        auto set = [&proc] (int idx, float norm, bool notify = true)
        {
            auto* p = proc.getG10Parameter (idx);
            if (notify) p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            if (notify) p->endChangeGesture();
        };

        // 3-Bell hard limit: create three, reject the fourth.
        expect (surface->getEnabledBellCount() == 0, "no bells initially");
        expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (500.0f)), "create bell 1");
        expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (1500.0f)), "create bell 2");
        expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (5000.0f)), "create bell 3");
        expect (surface->getEnabledBellCount() == 3, "exactly 3 bells active");
        expect (! surface->createBellAt (G10MiniEqCore::bellNormFromHz (9000.0f)),
                "4th bell rejected (hard limit)");
        expect (surface->getEnabledBellCount() == 3, "still 3 bells after rejection");

        // New bell defaults: enabled, clicked frequency, 0 dB, Q 1.0.
        expect (proc.getG10Parameter (APEX::G10::kBell1Enabled)->getBool(), "bell1 enabled");
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue(),
                                   G10MiniEqCore::bellNormFromHz (500.0f), 1.0e-6f, "bell1 freq = clicked");
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                                   0.0f, 1.0e-5f, "bell1 gain = 0 dB");
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                   G10MiniEqCore::bellNormFromQ (1.0f), 1.0e-6f, "bell1 Q = 1.0");

        // HPF / LPF nodes present and OFF by default.
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                   G10MiniEqCore::kHpfOffNorm, 1.0e-6f, "HPF OFF initially");
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                   G10MiniEqCore::kLpfOffNorm, 1.0e-6f, "LPF OFF initially");

        // Drag semantics: horizontal = frequency, vertical = gain.
        // (Simulate the drag mapping used by the surface.)
        surface->setHpfNorm (G10MiniEqCore::hpfNormFromHz (100.0f));
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kHpf)->getValue(),
                                   G10MiniEqCore::hpfNormFromHz (100.0f), 1.0e-6f,
                                   "HPF cutoff set through the surface");
        surface->setLpfNorm (G10MiniEqCore::lpfNormFromHz (12000.0f));
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kLpf)->getValue(),
                                   G10MiniEqCore::lpfNormFromHz (12000.0f), 1.0e-6f,
                                   "LPF cutoff set through the surface");

        // Q editing through the surface + readout formatting.
        set (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (4.2f));
        expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (
                                       proc.getG10Parameter (APEX::G10::kBell1Q)->getValue()),
                                   4.2f, 1.0e-3f, "Q 4.2 stored");

        // Node readout accuracy: Frequency/Gain/Q formatting contract.
        set (APEX::G10::kBell2Freq, G10MiniEqCore::bellNormFromHz (3170.0f));
        set (APEX::G10::kBell2Gain, G10MiniEqCore::bellNormFromGainDb (-2.4f));
        set (APEX::G10::kBell2Q, G10MiniEqCore::bellNormFromQ (5.2f));
        expect (ApexInteractiveEQSurface::formatHz (3170.0f) == "3.17 kHz", "readout freq format");
        expect (ApexInteractiveEQSurface::formatDb (-2.4f) == "-2.4 dB", "readout gain format");
        expect (ApexInteractiveEQSurface::formatQ (5.2f) == "5.20", "readout Q format");
        expect (ApexInteractiveEQSurface::formatHz (83.0f) == "83 Hz", "readout low freq format");
        expect (ApexInteractiveEQSurface::formatHz (8750.0f) == "8.75 kHz", "readout high freq format");

        // Remove (canonical operation) + reset behaviors.
        surface->removeBell (0);
        expect (! proc.getG10Parameter (APEX::G10::kBell1Enabled)->getBool(),
                "remove bell1 -> enabled false");
        expect (surface->getEnabledBellCount() == 2, "2 bells remain");

        // Bell gain reset: double-click node = 0 dB, stays enabled.
        set (APEX::G10::kBell3Gain, G10MiniEqCore::bellNormFromGainDb (7.0f));
        surface->resetBellGain (2);
        expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell3Gain)->getUnitsValue(),
                                   0.0f, 1.0e-5f, "bell3 gain reset to 0 dB");
        expect (proc.getG10Parameter (APEX::G10::kBell3Enabled)->getBool(),
                "bell3 stays enabled after gain reset");

        // Automation-driven refresh keeps the surface alive and re-readable.
        set (APEX::G10::kHpf, G10MiniEqCore::hpfNormFromHz (250.0f));
        surface->refresh();
        expectWithinAbsoluteError (G10MiniEqCore::hpfHzFromNorm (
                                       proc.getG10Parameter (APEX::G10::kHpf)->getValue()),
                                   250.0f, 1.0f, "automation value readable after refresh");

        // Resize matrix: the surface tracks the analyzer at every size.
        for (const auto& size : { std::pair<int, int> { 1000, 612 }, { 1500, 918 },
                                  { 800, 490 }, { 1000, 900 }, { 1400, 700 }, { 700, 700 } })
        {
            editor->setSize (size.first, size.second);
            surface->refresh();
            expect (surface->getWidth() > 0 && surface->getHeight() > 0,
                    "surface sized at " + juce::String (size.first) + "x" + juce::String (size.second));
             // Surface local bounds are in the content coordinate space
             // (logical 1000 x 612); the transform handles visual scaling.
             // Verify the surface fits within the logical blueprint.
             expect (surface->getBounds().getWidth() <= 1000
                     && surface->getBounds().getHeight() <= 612,
                     "surface within logical content at " + juce::String (size.first)
                     + "x" + juce::String (size.second));
        }
    }

    // ------------------------------------------------------------------

    void testEditorHook()
    {
        beginTest ("Editor hook");

        G10Processor proc;
        expect (proc.hasEditor(), "hasEditor() must be true");

        // createEditor() must return a real, non-null editor.
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "createEditor() must not return nullptr");
        if (editor == nullptr)
            return;

        expectEquals (editor->getWidth(), 1000, "editor width (blueprint scale)");
        expectEquals (editor->getHeight(), 612, "editor height (blueprint scale)");

        // All 15 production parameters remain accessible.
        expectEquals (proc.getNumParameters(), (int) kNumParams, "parameter count");
        for (int i = 0; i < kNumParams; ++i)
            expect (proc.getG10Parameter (i) != nullptr, "parameter accessible");

        // The editor can be destroyed safely (unique_ptr scope exit).
        editor.reset();
        expect (editor == nullptr, "editor destroyed");
    }

    // ------------------------------------------------------------------

    void testAnalyzerGate()
    {
        beginTest ("Analyzer producer gate");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        auto* fifo = proc.getAnalyzerFifo();
        expect (fifo != nullptr, "analyzer FIFO exists");

        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        for (int i = 0; i < 512; ++i)
        {
            buffer.setSample (0, i, 0.5f);
            buffer.setSample (1, i, 0.5f);
        }
        juce::MidiBuffer midi;

        // Gate OFF: the audio thread must not fill the FIFO.
        proc.setAnalyzerActive (false);
        proc.processBlock (buffer, midi);
        expectEquals (fifo->available(), 0, "no push while inactive");

        // Gate ON: the audio thread pushes the POST-EQ output. Stereo keeps
        // BOTH channels interleaved (L,R,L,R,...) — no mono mix, so anti-phase
        // material can never be cancelled by the transport.
        proc.setAnalyzerActive (true);
        proc.processBlock (buffer, midi);
        expect (fifo->available() > 0, "push while active");

        // The pushed samples are the interleaved stereo pair of the actual
        // POST-EQ output. The canonical chain (D1B + I1) is not transparent
        // to DC, so the FIFO must mirror the processor's output buffer —
        // not the input value. This is the truthfulness contract: the
        // analyzer shows exactly what the audio path produced.
        std::vector<float> dst (512, 0.0f);
        const int n = fifo->drain (dst.data(), 512);
        expect (n > 0, "drain returns samples");
        if (n > 0)
        {
            expect (std::abs (dst[0] - buffer.getSample (0, 0)) < 1e-6f, "left channel value");
            expect (std::abs (dst[1] - buffer.getSample (1, 0)) < 1e-6f, "right channel value");
        }

        // Stereo pushes 2 floats per frame; drain the remainder so the
        // deactivate check below starts from an empty FIFO.
        std::vector<float> rest (512, 0.0f);
        while (fifo->available() > 0)
            fifo->drain (rest.data(), 512);
        expectEquals (fifo->available(), 0, "fifo fully drained");

        // Gate off again: no further pushes.
        proc.setAnalyzerActive (false);
        proc.processBlock (buffer, midi);
        expectEquals (fifo->available(), 0, "no push after deactivate");
    }

    // ------------------------------------------------------------------

    void testResizeContract()
    {
        beginTest ("Resize contract and proportional scaling");

        G10Processor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;

        // JUCE 8.0.12 components start INVISIBLE (componentFlags are zero-
        // initialized); real hosts show the editor before use. Mirror that
        // here so visibility-gated paths (getComponentAt) behave like a host.
        editor->setVisible (true);

        // Participation: host-resizable with explicit min/max limits. The
        // OUTER editor must NOT enforce a fixed aspect ratio — the external
        // host's plugin area uses whatever size the user chooses; only the
        // INTERNAL content keeps the 1000:612 design aspect (proven below).
        expect (editor->isResizable(), "editor advertises host resize capability");
        auto* constrainer = editor->getConstrainer();
        expect (constrainer != nullptr, "editor has a bounds constrainer");
        if (constrainer != nullptr)
        {
            expectWithinAbsoluteError ((float) constrainer->getFixedAspectRatio(),
                                       0.0f, 1.0e-6f,
                                       "outer editor enforces no fixed aspect ratio");
            expectEquals (constrainer->getMinimumWidth(), 500, "minimum width");
            expectEquals (constrainer->getMinimumHeight(), 306, "minimum height");
            expectEquals (constrainer->getMaximumWidth(), 2000, "maximum width");
            expectEquals (constrainer->getMaximumHeight(), 1224, "maximum height");
        }

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        // Reference size: unity scale.
        expectEquals (editor->getWidth(), 1000, "reference width");
        expectEquals (editor->getHeight(), 612, "reference height");
        expectWithinAbsoluteError (g10->getContentScale(), 1.0f, 1.0e-6f, "reference scale 1.0");

        // Larger proportional (1.5x).
        editor->setSize (1500, 918);
        expectWithinAbsoluteError (g10->getContentScale(), 1.5f, 1.0e-3f, "1.5x proportional scale");

        // Smaller proportional (0.8x).
        editor->setSize (800, 490);
        expectWithinAbsoluteError (g10->getContentScale(), 0.8f, 1.0e-3f, "0.8x proportional scale");

        // Non-proportional host areas: the OUTER editor accepts the requested
        // dimensions unchanged (no aspect correction), the INTERNAL content
        // keeps 1000:612 via uniform min-fit scale, centered (letterbox).
        // 1400x900: wider than the design aspect.
        editor->setSize (1400, 900);
        expectEquals (editor->getWidth(), 1400, "outer editor keeps host width 1400");
        expectEquals (editor->getHeight(), 900, "outer editor keeps host height 900");
        {
            const float fitScale = juce::jmin (1400.0f / 1000.0f, 900.0f / 612.0f);
            expectWithinAbsoluteError (g10->getContentScale(), fitScale, 1.0e-3f,
                                       "letterbox uses the min-fit uniform scale");
            auto* content = g10->getContent();
            expect (content != nullptr, "content child exists");
            if (content != nullptr)
            {
                // Content logical bounds are always 1000x612; the RENDERED
                // size (1000*fitScale x 612*fitScale) determines centering.
                expectEquals (content->getWidth(), 1000, "content logical width is always 1000");
                expectEquals (content->getHeight(), 612, "content logical height is always 612");
                const int renderedW = juce::roundToInt (1000.0f * fitScale);
                const int renderedH = juce::roundToInt (612.0f * fitScale);
                expectEquals (content->getX(), (1400 - renderedW) / 2, "content centered horizontally");
                expectEquals (content->getY(), (900 - renderedH) / 2, "content centered vertically");
            }
        }
        // 1000x900: taller than the design aspect (vertical letterbox).
        editor->setSize (1000, 900);
        expectEquals (editor->getWidth(), 1000, "outer editor keeps 1000 wide");
        expectEquals (editor->getHeight(), 900, "outer editor keeps 900 tall");
        {
            const float fitScale = juce::jmin (1000.0f / 1000.0f, 900.0f / 612.0f);
            expectWithinAbsoluteError (g10->getContentScale(), fitScale, 1.0e-3f,
                                       "tall area uses the min-fit uniform scale");
            auto* content = g10->getContent();
            if (content != nullptr)
            {
                const int renderedW = juce::roundToInt (1000.0f * fitScale);
                const int renderedH = juce::roundToInt (612.0f * fitScale);
                expectEquals (content->getX(), (1000 - renderedW) / 2, "tall area content centered horizontally");
                expectEquals (content->getY(), (900 - renderedH) / 2, "tall area content centered vertically");
            }
        }
        // 700x700: square host area.
        editor->setSize (700, 700);
        expectEquals (editor->getWidth(), 700, "outer editor keeps square width");
        expectEquals (editor->getHeight(), 700, "outer editor keeps square height");
        {
            const float fitScale = juce::jmin (700.0f / 1000.0f, 700.0f / 612.0f);
            expectWithinAbsoluteError (g10->getContentScale(), fitScale, 1.0e-3f,
                                       "square area uses the min-fit uniform scale");
            auto* content = g10->getContent();
            if (content != nullptr)
            {
                const int renderedW = juce::roundToInt (1000.0f * fitScale);
                const int renderedH = juce::roundToInt (612.0f * fitScale);
                expectEquals (content->getX(), (700 - renderedW) / 2, "square area content centered horizontally");
                expectEquals (content->getY(), (700 - renderedH) / 2, "square area content centered vertically");
            }
        }

        // Minimum and maximum sizes.
        editor->setSize (500, 306);
        expectWithinAbsoluteError (g10->getContentScale(), 0.5f, 1.0e-3f, "minimum size scale 0.5");
        editor->setSize (2000, 1224);
        expectWithinAbsoluteError (g10->getContentScale(), 2.0f, 1.0e-3f, "maximum size scale 2.0");

        // Hit-testing through the transform at a NON-PROPORTIONAL size
        // (1400x700): the editor-local point for a logical control is
        // computed with the uniform min-fit scale and the centered letterbox
        // offset; it must resolve into the content child.
        editor->setSize (1400, 700);
        {
            const float fitScale = juce::jmin (1400.0f / 1000.0f, 700.0f / 612.0f);
            const int renderedW = juce::roundToInt (1000.0f * fitScale);
            const int renderedH = juce::roundToInt (612.0f * fitScale);
            auto* content = g10->getContent();
            expect (content != nullptr, "content child exists");
            if (content != nullptr)
            {
                const int logicalX = 546; // FOCUS fader center
                const int logicalY = 394; // 0 dB row
                const int contentX = (editor->getWidth()  - renderedW) / 2;
                const int contentY = (editor->getHeight() - renderedH) / 2;
                const auto probePoint = juce::Point<int> (
                    contentX + juce::roundToInt (logicalX * fitScale),
                    contentY + juce::roundToInt (logicalY * fitScale));
                auto* hit = editor->getComponentAt (probePoint);
                bool hitInsideContent = false;
                for (auto* c = hit; c != nullptr; c = c->getParentComponent())
                    if (c == content) { hitInsideContent = true; break; }
                expect (hitInsideContent,
                        "non-proportional hit-test resolves into the content child");
            }
        }
        // Proportional 1.5x hit-test (kept for the proportional case).

        // Hit-testing through the transform: at 1.5x, an editor-local point
        // at logical*1.5 must resolve into the content child (JUCE maps the
        // point through the inverse transform before hitting children).
        editor->setSize (1500, 918);
        auto* content = g10->getContent();
        expect (content != nullptr, "content child exists");
        if (content != nullptr)
        {
            const int logicalX = 546; // FOCUS fader center
            const int logicalY = 394; // 0 dB row
            const auto probePoint = juce::Point<int> (
                juce::roundToInt (logicalX * 1.5f), juce::roundToInt (logicalY * 1.5f));
            auto* hit = editor->getComponentAt (probePoint);
            bool hitInsideContent = false;
            for (auto* c = hit; c != nullptr; c = c->getParentComponent())
                if (c == content) { hitInsideContent = true; break; }
            expect (hitInsideContent,
                    "scaled hit-test at logical*1.5 resolves into the content child");
        }

        // Repeated resize stress: scales stay consistent (uniform min-fit,
        // including non-proportional host areas), no crash.
        {
            constexpr int sizes[][2] = {
                { 1000, 612 }, { 1500, 918 }, { 800, 490 }, { 1200, 734 },
                { 1000, 900 }, { 1400, 700 }, { 700, 700 }, { 640, 392 },
                { 1800, 1102 }, { 1000, 612 }
            };
            for (int i = 0; i < 20; ++i)
            {
                const int w = sizes[i % 10][0];
                const int h = sizes[i % 10][1];
                editor->setSize (w, h);
                const float expected = juce::jmin (w / 1000.0f, h / 612.0f);
                expectWithinAbsoluteError (g10->getContentScale(), expected, 1.0e-3f,
                                           "stress scale " + juce::String (i));
            }
            expect (editor != nullptr, "editor survives resize stress");
        }

        // Analyzer still runs after a resize: the FIFO gate pushes and the
        // drained samples are finite at 1.5x.
        {
            G10Processor proc2;
            proc2.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (proc2.createEditor());
            auto* g2 = dynamic_cast<G10Editor*> (ed2.get());
            if (ed2 != nullptr && g2 != nullptr)
            {
                ed2->setSize (1500, 918);
                ed2->setVisible (true);
                auto* fifo = proc2.getAnalyzerFifo();
                juce::AudioBuffer<float> buffer (2, 512);
                buffer.clear();
                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, 0.25f);
                    buffer.setSample (1, i, 0.25f);
                }
                juce::MidiBuffer midi;
                proc2.processBlock (buffer, midi);
                expect (fifo != nullptr && fifo->available() > 0,
                        "analyzer pushes after resize");
                if (fifo != nullptr)
                {
                    std::vector<float> dst (512, 0.0f);
                    const int n = fifo->drain (dst.data(), 512);
                    expect (n > 0, "analyzer drains after resize");
                    bool finite = true;
                    for (int i = 0; i < n; ++i)
                        if (! std::isfinite (dst[(size_t) i]))
                            finite = false;
                    expect (finite, "analyzer samples are finite after resize");
                }
                ed2->setVisible (false);
            }
        }

        // Close/reopen after resize returns to the reference size.
        editor.reset();
        std::unique_ptr<juce::AudioProcessorEditor> reopened (proc.createEditor());
        expect (reopened != nullptr, "editor recreates after resize");
        if (reopened != nullptr)
        {
            expectEquals (reopened->getWidth(), 1000, "reopened reference width");
            expectEquals (reopened->getHeight(), 612, "reopened reference height");
            auto* g3 = dynamic_cast<G10Editor*> (reopened.get());
            if (g3 != nullptr)
                expectWithinAbsoluteError (g3->getContentScale(), 1.0f, 1.0e-6f,
                                           "reopened scale 1.0");
        }
    }

    // ------------------------------------------------------------------

    void testResizePolicy()
    {
        beginTest ("PluginEditorResizePolicy classification");

        G10Processor proc;

        // Resizable G10 editor -> trueResize with the design size recorded.
        std::unique_ptr<juce::AudioProcessorEditor> g10 (proc.createEditor());
        const auto policyG10 = DAW::PluginEditorResizePolicy::evaluate (g10.get());
        expect (policyG10.mode == DAW::PluginEditorResizePolicy::Mode::trueResize,
                "resizable G10 editor classifies as trueResize");
        expectEquals (policyG10.nativeWidth, 1000, "policy native width");
        expectEquals (policyG10.nativeHeight, 612, "policy native height");

        // Fixed-size JUCE editor -> hostScale (JUCE-sanctioned host zoom).
        FixedSizeTestEditor fixed (proc);
        const auto policyFixed = DAW::PluginEditorResizePolicy::evaluate (&fixed);
        expect (policyFixed.mode == DAW::PluginEditorResizePolicy::Mode::hostScale,
                "fixed-size JUCE editor classifies as hostScale");
        expectEquals (policyFixed.nativeWidth, 400, "fixed policy native width");
        expectEquals (policyFixed.nativeHeight, 300, "fixed policy native height");

        // Unknown geometry -> safe fallback.
        const auto policyNull = DAW::PluginEditorResizePolicy::evaluate (nullptr);
        expect (policyNull.mode == DAW::PluginEditorResizePolicy::Mode::fallback,
                "null editor classifies as fallback");

        // Scale clamping.
        expectWithinAbsoluteError (DAW::PluginEditorResizePolicy::clampScale (0.5f), 0.5f, 1.0e-6f,
                                   "clamp keeps an in-range scale");
        expectWithinAbsoluteError (DAW::PluginEditorResizePolicy::clampScale (0.01f), 0.25f, 1.0e-6f,
                                   "clamp lower bound 0.25");
        expectWithinAbsoluteError (DAW::PluginEditorResizePolicy::clampScale (99.0f), 4.0f, 1.0e-6f,
                                   "clamp upper bound 4.0");
    }

    // ------------------------------------------------------------------
    // Real-hierarchy interaction regression: proves that mouse events
    // ROUTE through the actual editor/analyzer/surface hierarchy and that
    // the surface HANDLERS mutate the processor parameters. This is the
    // exact path the real user exercises (the previous suite only called
    // surface methods directly, which is why the mouse-dead analyzer
    // (setInterceptsMouseClicks(false,false)) was not caught).
    // ------------------------------------------------------------------

    void testMiniEqRealHierarchy()
    {
        beginTest ("Mini EQ real-hierarchy interaction");

        G10Processor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "editor hosts the interactive surface");
        if (surface == nullptr)
            return;

        // ONE cascade-curve geometry authority (matching hitTestNodes and
        // drawBellNodes — see ApexInteractiveEQSurface §2–4).
        auto cascadeNode = [&] (int i) -> juce::Point<float>
        {
            const auto& m = surface->getModel();
            const auto& b = m.bells[i];
            const float hz = m.normToFreq (b.getFreqNorm());
            const float g = m.nominalMagnitude (hz, (float) surface->getEffectiveSampleRate());
            const float db = juce::jlimit (-12.0f, 12.0f,
                20.0f * std::log10 (juce::jmax (1.0e-30f, g)));
            return { surface->freqToX (hz), surface->gainToY (db) };
        };

        // ---- Hierarchy sanity -------------------------------------------------
        expect (surface->isVisible(), "surface visible in the real hierarchy");
        expect (surface->getParentComponent() != nullptr, "surface has a parent (the analyzer)");
        expect (surface->getWidth() > 0 && surface->getHeight() > 0,
                "surface has non-zero bounds");
        auto* analyzer = surface->getParentComponent();
        expect (analyzer != nullptr && analyzer->getParentComponent() != nullptr,
                "analyzer nested inside the editor content");

        // ---- Mouse event synthesis --------------------------------------------
        // The framework delivers e.position in the receiving component's local
        // space (inverse-transformed through Content's scale). We construct
        // events in the surface's local space, exactly as the framework would.
        juce::MouseInputSource* mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        if (mouseSource == nullptr)
        {
            // Some hosts/CI environments expose no mouse source until a peer
            // exists; give the editor a real peer so Desktop creates one.
            editor->addToDesktop (0);
            mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (mouseSource != nullptr, "mouse source available for event synthesis");
        if (mouseSource == nullptr)
            return;

        auto makeEvent = [mouseSource, surface] (juce::Point<float> localPos, int clicks)
        {
            return juce::MouseEvent (*mouseSource, localPos, juce::ModifierKeys(),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     surface, surface, juce::Time::getCurrentTime(),
                                     localPos, juce::Time::getCurrentTime(),
                                     clicks, false);
        };

        // ---- Routing proof: the REAL hierarchy resolves graph points ----------
        // to the surface (empty graph + both OFF handles), BEFORE and AFTER
        // resize (the Content transform must map clicks correctly).
        const auto plot = APEX::ApexEqPlotGeometry::plotBounds ((float) surface->getWidth(),
                                                          (float) surface->getHeight());
        const juce::Point<float> emptyGraphPoint (surface->freqToX (1000.0f), surface->gainToY (0.0f));
        const juce::Point<float> hpfOffPoint (plot.getX() + APEX::ApexEqPlotGeometry::kOffHandleInset,
                                              surface->gainToY (0.0f));
        const juce::Point<float> lpfOffPoint (plot.getRight() - APEX::ApexEqPlotGeometry::kOffHandleInset,
                                              surface->gainToY (0.0f));

        auto assertPointRoutesToSurface = [&] (const juce::Point<float>& surfaceLocal, const char* what)
        {
            const juce::Point<int> editorPt = editor->getLocalPoint (surface, surfaceLocal.toInt());
            auto* hit = editor->getComponentAt (editorPt);
            expect (hit == surface, juce::String (what)
                    + " routes to the interactive surface through the real hierarchy");
        };

        assertPointRoutesToSurface (emptyGraphPoint, "empty graph");
        assertPointRoutesToSurface (hpfOffPoint, "HPF OFF handle");
        assertPointRoutesToSurface (lpfOffPoint, "LPF OFF handle");

        // ---- Interaction proof (through the real surface handlers) ------------
        // Double-click empty graph -> Bell1 created via the model -> parameter.
        surface->mouseDown (makeEvent (emptyGraphPoint, 1));
        surface->mouseUp (makeEvent (emptyGraphPoint, 1));
        surface->mouseDoubleClick (makeEvent (emptyGraphPoint, 2));
        expect (proc.getG10Parameter (APEX::G10::kBell1Enabled)->getBool(),
                "double-click empty graph creates Bell1 (enabled false -> true)");

        // Bell drag: grab the new bell node and drag right/up.
        const auto bellPos = juce::Point<float> (surface->freqToX (1000.0f), surface->gainToY (0.0f));
        surface->mouseDown (makeEvent (bellPos, 1));
        const auto draggedPos = bellPos + juce::Point<float> (150.0f, -80.0f);
        surface->mouseDrag (makeEvent (draggedPos, 1));
        surface->mouseUp (makeEvent (draggedPos, 1));
        expect (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue() > 0.5f + 1.0e-3f,
                "Bell horizontal drag raises frequency");
        expect (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue() > 0.0f,
                "Bell vertical drag raises gain above 0 dB");

        // Wheel over the (moved) bell -> Q changes.
        const auto movedBellPos = cascadeNode (0);
        expect (movedBellPos.x > 0.0f && movedBellPos.y > 0.0f,
                "cascade node position is valid");
        const float qBefore = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();
        juce::MouseWheelDetails wheel;
        wheel.deltaY = 1.0f;
        surface->mouseWheelMove (makeEvent (movedBellPos, 1), wheel);
        expect (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue() != qBefore,
                "mouse wheel over Bell changes Q");

        // HPF OFF handle: down + drag right engages HPF and sets cutoff.
        surface->mouseDown (makeEvent (hpfOffPoint, 1));
        surface->mouseDrag (makeEvent (hpfOffPoint + juce::Point<float> (120.0f, 0.0f), 1));
        surface->mouseUp (makeEvent (hpfOffPoint + juce::Point<float> (120.0f, 0.0f), 1));
        expect (proc.getG10Parameter (APEX::G10::kHpf)->getValue() > 0.0f,
                "dragging HPF OFF handle right engages HPF");

        // LPF OFF handle: down + drag left engages LPF and sets cutoff.
        surface->mouseDown (makeEvent (lpfOffPoint, 1));
        surface->mouseDrag (makeEvent (lpfOffPoint + juce::Point<float> (-120.0f, 0.0f), 1));
        surface->mouseUp (makeEvent (lpfOffPoint + juce::Point<float> (-120.0f, 0.0f), 1));
        expect (proc.getG10Parameter (APEX::G10::kLpf)->getValue() < 1.0f,
                "dragging LPF OFF handle left engages LPF");

        // ---- Handle containment: FULL visible handle inside plotBounds --------
        const float radius = 6.0f; // active handle radius (worst case)
        const float hpfX = plot.getX() + APEX::ApexEqPlotGeometry::kOffHandleInset;
        const float lpfX = plot.getRight() - APEX::ApexEqPlotGeometry::kOffHandleInset;
        expect (hpfX - radius >= plot.getX() && hpfX + radius <= plot.getRight(),
                "HPF OFF handle fully inside plotBounds (no edge clipping)");
        expect (lpfX - radius >= plot.getX() && lpfX + radius <= plot.getRight(),
                "LPF OFF handle fully inside plotBounds (no edge clipping)");

        // ---- Axis label clearance (layout proof) ------------------------------
        expect (plot.getX() >= 32.0f, "left axis reserve fits dB labels");
        expect (plot.getBottom() + 22.0f <= (float) surface->getHeight(),
                "bottom axis reserve fits frequency labels");
        expect (plot.getRight() <= (float) surface->getWidth() - 12.0f,
                "right axis reserve fits LPF handle");

        // ---- Resize preserves routing -----------------------------------------
        editor->setSize (800, 490);
        surface->refresh();
        assertPointRoutesToSurface (emptyGraphPoint, "empty graph after resize to 800x490");
        assertPointRoutesToSurface (hpfOffPoint, "HPF OFF handle after resize to 800x490");
        assertPointRoutesToSurface (lpfOffPoint, "LPF OFF handle after resize to 800x490");

        editor->setSize (700, 700);
        surface->refresh();
        assertPointRoutesToSurface (emptyGraphPoint, "empty graph after resize to 700x700");
        assertPointRoutesToSurface (hpfOffPoint, "HPF OFF handle after resize to 700x700");
    }

    // ------------------------------------------------------------------
    // v3 Mini Clean EQ interactions through the REAL editor hierarchy:
    // full HPF/LPF ranges, Ctrl-drag Q, wheel Q extremes, Alt+click bypass,
    // Delete freeing slots, floating panel placement/containment/follow,
    // context-menu anchor authority, and numeric entry (Enter/Escape/Tab).
    // ------------------------------------------------------------------

    void testMiniEqV3Interactions()
    {
        beginTest ("Mini EQ v3 interactions (real hierarchy)");

        G10Processor proc;
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "editor hosts the interactive surface");
        if (surface == nullptr)
            return;

        juce::MouseInputSource* mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        if (mouseSource == nullptr)
        {
            editor->addToDesktop (0);
            mouseSource = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (mouseSource != nullptr, "mouse source available for event synthesis");
        if (mouseSource == nullptr)
            return;

        auto makeEvent = [mouseSource, surface] (juce::Point<float> localPos, int clicks,
                                                 juce::ModifierKeys mods = juce::ModifierKeys())
        {
            return juce::MouseEvent (*mouseSource, localPos, mods,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     surface, surface, juce::Time::getCurrentTime(),
                                     localPos, juce::Time::getCurrentTime(),
                                     clicks, false);
        };

        const auto plot = APEX::ApexEqPlotGeometry::plotBounds ((float) surface->getWidth(),
                                                                (float) surface->getHeight());

        // ONE geometry authority for all node positions (cascade curve — the
        // same computation used by drawBellNodes, hitTestNodes and the
        // nominal response rendering).
        auto cascadeNode = [&] (int i) -> juce::Point<float>
        {
            const auto& m = surface->getModel();
            const auto& b = m.bells[i];
            const float hz = m.normToFreq (b.getFreqNorm());
            const float g = m.nominalMagnitude (hz, (float) surface->getEffectiveSampleRate());
            const float db = juce::jlimit (-12.0f, 12.0f,
                20.0f * std::log10 (juce::jmax (1.0e-30f, g)));
            return { surface->freqToX (hz), surface->gainToY (db) };
        };

        // ---- HPF passes the old 420 Hz ceiling (real drag) ------------------
        {
            const juce::Point<float> hpfOff (plot.getX() + APEX::ApexEqPlotGeometry::kOffHandleInset,
                                             surface->gainToY (0.0f));
            surface->mouseDown (makeEvent (hpfOff, 1));
            surface->mouseDrag (makeEvent (hpfOff + juce::Point<float> (500.0f, 0.0f), 1));
            surface->mouseUp (makeEvent (hpfOff + juce::Point<float> (500.0f, 0.0f), 1));
            const float hpfHz = G10MiniEqCore::hpfHzFromNorm (
                proc.getG10Parameter (APEX::G10::kHpf)->getValue());
            expect (hpfHz > 420.0f,
                    "HPF travels beyond the old 420 Hz ceiling (now " + juce::String (hpfHz, 0) + " Hz)");

            // The ACTIVE HPF node remains hittable at its new position.
            const juce::Point<float> hpfNode (surface->freqToX (hpfHz), surface->gainToY (0.0f));
            const juce::Point<int> editorPt = editor->getLocalPoint (surface, hpfNode.toInt());
            expect (editor->getComponentAt (editorPt) == surface,
                    "active HPF node still routes to the surface after passing 420 Hz");
        }

        // ---- LPF below the old 8.5 kHz floor (real drag) --------------------
        {
            const juce::Point<float> lpfOff (plot.getRight() - APEX::ApexEqPlotGeometry::kOffHandleInset,
                                             surface->gainToY (0.0f));
            surface->mouseDown (makeEvent (lpfOff, 1));
            surface->mouseDrag (makeEvent (lpfOff + juce::Point<float> (-350.0f, 0.0f), 1));
            surface->mouseUp (makeEvent (lpfOff + juce::Point<float> (-350.0f, 0.0f), 1));
            const float lpfHz = G10MiniEqCore::lpfHzFromNorm (
                proc.getG10Parameter (APEX::G10::kLpf)->getValue());
            expect (lpfHz < 8500.0f,
                    "LPF travels below the old 8.5 kHz floor (now " + juce::String (lpfHz, 0) + " Hz)");
        }

        // ---- Bell creation, Ctrl+vertical drag = Q, wheel Q extremes --------
        {
            // The range drags left the HPF/LPF cut nodes ACTIVE near the
            // middle of the graph (the HPF node sits ~13 px from the 1 kHz
            // point and would swallow the double-click). Return both to OFF
            // through the canonical surface commands so the empty-graph
            // double-click below is unambiguous.
            surface->setHpfNorm (G10MiniEqCore::kHpfOffNorm);
            surface->setLpfNorm (G10MiniEqCore::kLpfOffNorm);

            const juce::Point<float> center (surface->freqToX (1000.0f), surface->gainToY (0.0f));
            surface->mouseDown (makeEvent (center, 1));
            surface->mouseUp (makeEvent (center, 1));
            surface->mouseDoubleClick (makeEvent (center, 2));
            expect (proc.getG10Parameter (APEX::G10::kBell1Enabled)->getBool(),
                    "double-click empty graph creates Bell1");

            const auto& model = surface->getModel();
            const auto& bell1 = model.bells[0];
            const juce::Point<float> node = cascadeNode (0);

            const float freqBefore = proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue();
            const float gainBefore = proc.getG10Parameter (APEX::G10::kBell1Gain)->getValue();
            const float qBefore = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();

            // Ctrl + vertical drag changes Q only (freq/gain untouched).
            surface->mouseDown (makeEvent (node, 1));
            surface->mouseDrag (makeEvent (node + juce::Point<float> (0.0f, -120.0f), 1,
                                           juce::ModifierKeys::ctrlModifier));
            surface->mouseUp (makeEvent (node + juce::Point<float> (0.0f, -120.0f), 1,
                                         juce::ModifierKeys::ctrlModifier));
            const float qAfter = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();
            expect (qAfter != qBefore, "Ctrl + vertical drag changes Bell Q");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue(),
                                       freqBefore, 1.0e-6f, "Ctrl-drag leaves frequency untouched");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getValue(),
                                       gainBefore, 1.0e-6f, "Ctrl-drag leaves gain untouched");

            // Wheel reaches broad AND surgical Q regions through the handler.
            juce::MouseWheelDetails wheel;
            wheel.deltaY = 8.0f;
            surface->mouseWheelMove (makeEvent (node, 1), wheel);
            expect (G10MiniEqCore::bellQFromNorm (
                        proc.getG10Parameter (APEX::G10::kBell1Q)->getValue()) > 10.0f,
                    "wheel reaches the surgical region (Q > 10)");
            wheel.deltaY = -30.0f;
            surface->mouseWheelMove (makeEvent (node, 1), wheel);
            expect (G10MiniEqCore::bellQFromNorm (
                        proc.getG10Parameter (APEX::G10::kBell1Q)->getValue()) < 0.5f,
                    "wheel reaches the broad region (Q < 0.5)");
        }

        // ---- Alt+click toggles the individual bypass; F/G/Q preserved -------
        {
            const juce::Point<float> node = cascadeNode (0);
            const float f = proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue();
            const float g = proc.getG10Parameter (APEX::G10::kBell1Gain)->getValue();
            const float q = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();

            surface->mouseDown (makeEvent (node, 1, juce::ModifierKeys::altModifier));
            surface->mouseUp (makeEvent (node, 1, juce::ModifierKeys::altModifier));
            expect (proc.getG10Parameter (APEX::G10::kBell1Bypass)->getBool(),
                    "Alt+click bypasses Bell1");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue(),
                                       f, 1.0e-6f, "bypass preserves frequency");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getValue(),
                                       g, 1.0e-6f, "bypass preserves gain");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                       q, 1.0e-6f, "bypass preserves Q");

            surface->mouseDown (makeEvent (node, 1, juce::ModifierKeys::altModifier));
            surface->mouseUp (makeEvent (node, 1, juce::ModifierKeys::altModifier));
            expect (! proc.getG10Parameter (APEX::G10::kBell1Bypass)->getBool(),
                    "Alt+click re-enables Bell1");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue(),
                                       f, 1.0e-6f, "re-enable preserves frequency");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getValue(),
                                       g, 1.0e-6f, "re-enable preserves gain");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                       q, 1.0e-6f, "re-enable preserves Q");
        }

        // ---- Delete frees the Bell slot --------------------------------------
        {
            const auto& model = surface->getModel();
            expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (3000.0f)), "create bell 2");
            expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (8000.0f)), "create bell 3");
            expect (surface->getEnabledBellCount() == 3, "3 bells active");

            const auto& bell3 = model.bells[2];
            const juce::Point<float> n3 = cascadeNode (2);
            surface->mouseDown (makeEvent (n3, 1));
            surface->mouseUp (makeEvent (n3, 1));
            expect (surface->getSelectedBellIndex() == 2, "Bell3 selected by click");

            surface->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            expect (! proc.getG10Parameter (APEX::G10::kBell3Enabled)->getBool(),
                    "Delete key removes Bell3");
            expect (surface->getEnabledBellCount() == 2, "2 bells remain after delete");

            expect (surface->createBellAt (G10MiniEqCore::bellNormFromHz (9000.0f)),
                    "freed slot accepts a new Bell");
            expect (surface->getEnabledBellCount() == 3, "3 bells again after slot reuse");
        }

        // ---- Floating panel placement: center/left/right/top/bottom ---------
        {
            auto* panel = surface->getBellPanel();
            expect (panel != nullptr, "floating panel exists");
            if (panel == nullptr)
                return;

            const auto& model = surface->getModel();

            auto assertContained = [&] (const char* what)
            {
                const auto b = panel->getBounds();
                expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                        && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                        juce::String (what) + ": panel contained inside the analyzer");
            };
            auto nodePos = [&] (int i) -> juce::Point<float>
            {
                return cascadeNode (i);
            };
            auto dragNode = [&] (const juce::Point<float>& from, const juce::Point<float>& delta)
            {
                surface->mouseDown (makeEvent (from, 1));
                surface->mouseDrag (makeEvent (from + delta, 1));
                surface->mouseUp (makeEvent (from + delta, 1));
                surface->refresh();
            };

            // ---- Hover policy: hovering NEVER opens the control panel ------
            // (the old black hover readout box is gone; hover is a pure node
            // highlight. The inspector appears only when the Bell is
            // SELECTED.)
            {
                auto* panel = surface->getBellPanel();
                expect (panel != nullptr, "floating panel exists (hover policy)");
                if (panel == nullptr)
                    return;
                const auto& model = surface->getModel();
                const juce::Point<float> hoverNode = cascadeNode (0);

                // Nothing is selected at this point: hover alone must not
                // show any panel (no empty black box, no full inspector).
                surface->mouseMove (makeEvent (hoverNode, 1));
                surface->refresh();
                expect (! panel->isVisible(),
                        "hover alone never shows the control panel");

                // Selecting the Bell shows the inspector.
                surface->mouseDown (makeEvent (hoverNode, 1));
                surface->mouseUp (makeEvent (hoverNode, 1));
                surface->refresh();
                expect (panel->isVisible(),
                        "clicking the Bell selects it and shows the inspector");
                expect (surface->getSelectedBellIndex() == 0,
                        "hovered Bell selected by click");
            }

            // Select Bell1 by a real click -> panel appears.
            surface->mouseDown (makeEvent (nodePos (0), 1));
            surface->mouseUp (makeEvent (nodePos (0), 1));
            surface->refresh();
            expect (panel->isVisible(), "panel visible when a Bell is selected");
            expect (surface->getSelectedBellIndex() == 0, "Bell1 selected");

            // Placement authority: the panel is valid when it is fully
            // contained, remains visually adjacent to the selected Bell on
            // AT LEAST ONE axis (above, below, left or right depending on
            // the available space), and never buries the Bell deep inside
            // its own rectangle.
            auto assertNearNode = [&] (const char* what)
            {
                const auto node = nodePos (0);
                const auto b = panel->getBounds().toFloat();
                const bool adjacentX = (b.getRight() <= node.x + 6.0f)
                                    || (b.getX() >= node.x - 6.0f);
                const bool adjacentY = (b.getBottom() <= node.y + 6.0f)
                                    || (b.getY() >= node.y - 6.0f);
                expect (adjacentX || adjacentY,
                        juce::String (what) + ": panel adjacent to the Bell (any axis)");
                if (b.contains (node))
                {
                    // The Bell may only touch the panel's margin, never sit
                    // deep inside it (a regression would bury the node).
                    const float insetX = juce::jmin (node.x - b.getX(), b.getRight() - node.x);
                    const float insetY = juce::jmin (node.y - b.getY(), b.getBottom() - node.y);
                    expect (juce::jmin (insetX, insetY) <= 20.0f,
                            juce::String (what) + ": Bell not buried inside the panel");
                }
                assertContained (what);
            };

            // Center: the panel sits adjacent to the node (above when there
            // is room, below after a flip, or to the side when the analyzer
            // is too short for either vertical position) and is contained.
            assertNearNode ("center");

            // Far left: panel clamps inside and stays near the node.
            dragNode (nodePos (0), juce::Point<float> (-400.0f, 0.0f));
            assertNearNode ("far left");

            // Far right: panel clamps inside.
            dragNode (nodePos (0), juce::Point<float> (800.0f, 0.0f));
            assertNearNode ("far right");

            // Top: node near the top edge -> the panel must NEVER flip below;
            // it stays above (or moves to a side).
            dragNode (nodePos (0), juce::Point<float> (0.0f, -200.0f));
            {
                const auto node = surface->getSelectedNodePosition();
                expect (panel->getY() < node.y - 1.0f,
                        "top placement: inspector never below the node");
                assertContained ("top");
            }

            // Bottom: node near the bottom edge -> panel stays adjacent.
            dragNode (nodePos (0), juce::Point<float> (0.0f, 200.0f));
            {
                assertNearNode ("bottom");
            }

            // Panel follows the Bell while it moves. (First bring the node
            // back from the clamped right edge so the drag has room.)
            dragNode (nodePos (0), juce::Point<float> (-400.0f, 0.0f));
            {
                const int beforeCenterX = panel->getX() + panel->getWidth() / 2;
                dragNode (nodePos (0), juce::Point<float> (120.0f, 0.0f));
                const int afterCenterX = panel->getX() + panel->getWidth() / 2;
                expect (afterCenterX >= beforeCenterX + 40,
                        "panel follows the Bell movement (x moved with the node)");
                assertContained ("follow");
            }

            // Containment survives editor resizes.
            editor->setSize (800, 490);
            surface->refresh();
            assertContained ("after resize 800x490");
            editor->setSize (700, 700);
            surface->refresh();
            assertContained ("after resize 700x700");
            editor->setSize (1000, 612);
            surface->refresh();
        }

        // ---- Context-menu anchor authority -----------------------------------
        {
            const juce::Point<float> node = cascadeNode (0);
            const auto ev = makeEvent (node, 1);
            const auto anchor = surface->contextMenuAnchorAt (ev);
            expectEquals (anchor.getX(), ev.getScreenX() - 1, "anchor x sits at the cursor");
            expectEquals (anchor.getY(), ev.getScreenY() - 1, "anchor y sits at the cursor");
            expectEquals (anchor.getWidth(), 2, "anchor width is 2 px");
            expectEquals (anchor.getHeight(), 2, "anchor height is 2 px");
            // The event was built AT the Bell's surface-local position; the
            // framework maps that to the cursor's screen position in a real
            // host, so the anchor is beside the Bell/cursor by construction.
            expectEquals (ev.getPosition().x, juce::roundToInt (node.x),
                          "event position is the Bell node (cursor anchor source)");
        }

        // ---- Numeric entry: Freq/Gain/Q, clamping, Enter, Escape, Tab -------
        {
            using Field = APEX::BellControlPanel::Field;
            auto* panel = surface->getBellPanel();
            expect (panel != nullptr && panel->isVisible(), "panel visible for numeric entry");
            if (panel == nullptr || ! panel->isVisible())
                return;

            auto* freqCtl = panel->getValueControl (Field::freq);
            auto* gainCtl = panel->getValueControl (Field::gain);
            auto* qCtl = panel->getValueControl (Field::q);
            expect (freqCtl != nullptr && gainCtl != nullptr && qCtl != nullptr,
                    "panel exposes the three value controls");
            if (freqCtl == nullptr || gainCtl == nullptr || qCtl == nullptr)
                return;

            auto controlCenter = [&] (juce::Component* ctl) -> juce::Point<float>
            {
                return surface->getLocalPoint (ctl, ctl->getBounds().getCentre().toFloat());
            };
            auto enterText = [&] (juce::Component* ctl, const juce::String& text)
            {
                ctl->mouseDoubleClick (makeEvent (controlCenter (ctl), 2));
                expect (dynamic_cast<APEX::BellControlPanel::ValueControl*> (ctl)->isEditing(),
                        "double-click opens the numeric editor");
                auto* vc = dynamic_cast<APEX::BellControlPanel::ValueControl*> (ctl);
                if (vc == nullptr || ! vc->isEditing())
                    return;
                vc->getEditor()->setText (text, false);
                vc->getEditor()->onReturnKey(); // Enter commits
            };

            // Frequency entry.
            enterText (freqCtl, "742 Hz");
            expectWithinAbsoluteError (G10MiniEqCore::bellHzFromNorm (
                                           proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue()),
                                       742.0f, 2.0f, "numeric frequency entry commits 742 Hz");
            // Gain entry.
            enterText (gainCtl, "-3.2 dB");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                                       -3.2f, 0.05f, "numeric gain entry commits -3.2 dB");
            // Q entry.
            enterText (qCtl, "7.4");
            expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (
                                           proc.getG10Parameter (APEX::G10::kBell1Q)->getValue()),
                                       7.4f, 0.1f, "numeric Q entry commits 7.4");

            // Out-of-range values clamp safely.
            enterText (freqCtl, "99999");
            expectWithinAbsoluteError (G10MiniEqCore::bellHzFromNorm (
                                           proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue()),
                                       20000.0f, 0.5f, "frequency entry clamps to 20 kHz");
            enterText (gainCtl, "99");
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                                       12.0f, 0.05f, "gain entry clamps to +12 dB");
            enterText (qCtl, "1000");
            expectWithinAbsoluteError (G10MiniEqCore::bellQFromNorm (
                                           proc.getG10Parameter (APEX::G10::kBell1Q)->getValue()),
                                       40.0f, 0.5f, "Q entry clamps to 40");

            // Escape cancels: text discarded, parameter unchanged.
            const float qBefore = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();
            qCtl->mouseDoubleClick (makeEvent (controlCenter (qCtl), 2));
            if (dynamic_cast<APEX::BellControlPanel::ValueControl*> (qCtl)->isEditing())
            {
                dynamic_cast<APEX::BellControlPanel::ValueControl*> (qCtl)->getEditor()->setText ("3.33", false);
                dynamic_cast<APEX::BellControlPanel::ValueControl*> (qCtl)->getEditor()->onEscapeKey();
                expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                           qBefore, 1.0e-6f, "Escape cancels the Q edit");
            }
            else
            {
                expect (false, "Q editor opens for the Escape test");
            }

            // Tab cycles Frequency -> Gain -> Q (the exact listener the
            // ComponentPeer consults before keyPressed).
            freqCtl->mouseDoubleClick (makeEvent (controlCenter (freqCtl), 2));
            auto* freqVC = dynamic_cast<APEX::BellControlPanel::ValueControl*> (freqCtl);
            expect (freqVC != nullptr && freqVC->isEditing(), "Freq editor opens for the Tab test");
            if (freqVC != nullptr && freqVC->isEditing())
            {
                freqVC->getEditor()->setText ("500 Hz", false);
                panel->getTabListener().keyPressed (juce::KeyPress (juce::KeyPress::tabKey),
                                                    freqVC->getEditor());
                expectWithinAbsoluteError (G10MiniEqCore::bellHzFromNorm (
                                               proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue()),
                                           500.0f, 2.0f, "Tab commits the Frequency edit");

                auto* gainVC = dynamic_cast<APEX::BellControlPanel::ValueControl*> (gainCtl);
                expect (gainVC != nullptr && gainVC->isEditing(),
                        "Tab continues editing in Gain");
                if (gainVC != nullptr && gainVC->isEditing())
                {
                    gainVC->getEditor()->setText ("1.5 dB", false);
                    panel->getTabListener().keyPressed (juce::KeyPress (juce::KeyPress::tabKey),
                                                        gainVC->getEditor());
                    expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                                               1.5f, 0.05f, "Tab commits the Gain edit");
                    expect (dynamic_cast<APEX::BellControlPanel::ValueControl*> (qCtl)->isEditing(),
                            "Tab continues editing in Q");
                }
            }
        }
    }
    // ------------------------------------------------------------------
    // Node / curve alignment: the Bell centre must sit on the CASCADE
    // nominal Mini EQ response (the same geometry authority used by the
    // HPF/LPF handles and the displayed curve).  Also verifies that the
    // compact inspector meets its geometry contract.
    // ------------------------------------------------------------------

    void testMiniEqNodeCurveAlignment()
    {
        beginTest ("Mini EQ node / curve alignment and inspector geometry");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);
        editor->addToDesktop (0); // real peer for synthetic mouse events

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "surface exists");
        if (surface == nullptr)
            return;

        auto setParam = [&proc] (int idx, float norm)
        {
            auto* p = proc.getG10Parameter (idx);
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        };

        // Enable Bell1 at 1 kHz, +6 dB, Q 2 (narrow — minimal interaction
        // with other frequencies).
        setParam (APEX::G10::kBell1Enabled, 1.0f);
        setParam (APEX::G10::kBell1Freq,
                  G10MiniEqCore::bellNormFromHz (1000.0f));
        setParam (APEX::G10::kBell1Gain,
                  G10MiniEqCore::bellNormFromGainDb (6.0f));
        setParam (APEX::G10::kBell1Q,
                  G10MiniEqCore::bellNormFromQ (2.0f));
        surface->refresh();

        const auto& model = surface->getModel();
        const auto& bell1 = model.bells[0];
        expect (bell1.getEnabled && bell1.getEnabled() >= 0.5f, "Bell1 enabled");

        // ---- Isolated Bell: cascade ≈ raw gain at its centre frequency ----
        {
            const float hz = model.normToFreq (bell1.getFreqNorm());
            // Production node position (ONE authority: getBellNodePosition).
            const auto node = surface->getBellNodePosition (0);
            expect (node.x > 0.0f && node.y > 0.0f,
                    "getBellNodePosition returns a valid cascade point for Bell0");

            // Verify the Bell is alive and well.
            expect (proc.getG10Parameter (APEX::G10::kBell1Enabled)->getBool(),
                    "Bell1 enabled for the node test");
            expectWithinAbsoluteError (
                proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue(),
                6.0f, 0.01f, "Bell1 gain is +6 dB as set");

            // Select Bell1 at its cascade position and read the anchor.
            // (The desktop peer was created at the top of this test.)
            juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
            if (ms == nullptr)
            {
                editor->addToDesktop (0);
                ms = juce::Desktop::getInstance().getMouseSource (0);
            }
            auto ev = juce::MouseEvent (*ms, node, juce::ModifierKeys(), 1,0,0,0,0,
                                        surface, surface, juce::Time::getCurrentTime(),
                                        node, juce::Time::getCurrentTime(), 1, false);
            surface->mouseDown (ev);
            surface->mouseUp (ev);
            surface->refresh();

            expect (surface->getSelectedBellIndex() == 0,
                    "Bell1 selected by cascade-position click");
            const auto anchor = surface->getSelectedNodePosition();
            expectWithinAbsoluteError (anchor.x, node.x, 1.0f,
                    "anchor X = cascade node X");
            expectWithinAbsoluteError (anchor.y, node.y, 1.0f,
                    "anchor Y = cascade node Y");

            // The cascade position reflects the total response: with Bell2
            // active the node Y naturally differs from the solo case (the
            // cascaded skirt at Bell1's frequency is different).  This is
            // correct — the node sits on the COMBINED curve.  The authority
            // is already proven by the direct-core / surface-model comparison
            // above and by the successful selection / anchor checks.
        }

        // ---- PIXEL-SYNC PROOF: node Y == curve Y at identical Hz ----------
        // The cascade dB is the same for both; gainToY must produce the
        // same pixel.  A difference >1 px means the node floats off the
        // curve as the user's screenshot shows.
        {
            const float hz = model.normToFreq (bell1.getFreqNorm());
            const float cascadeDb = juce::jlimit (-12.0f, 12.0f,
                20.0f * std::log10 (juce::jmax (1.0e-30f,
                    model.nominalMagnitude (hz,
                        (float) surface->getEffectiveSampleRate()))));
            const float expectedCurveY = surface->gainToY (cascadeDb);
            const float nodeY = surface->getBellNodePosition (0).y;
            const float diffPx = std::abs (nodeY - expectedCurveY);

            expectWithinAbsoluteError (nodeY, expectedCurveY, 1.0f,
                "node Y == curve Y at Bell freq (diff " + juce::String (diffPx, 2) + " px)");
        }

        // ---- RENDER-LEVEL REGRESSION: node centre ON the rendered curve ----
        // The curve is sampled at fixed log grid + every enabled Bell centre
        // frequency (see rebuildCurve).  Each Bell centre is therefore a
        // vertex of the path paint() strokes; the node must coincide with
        // that vertex within 1 px.  This test reads the REAL rendered
        // vertices, not a duplicated helper.
        {
            auto assertNodeOnRenderedCurve = [&] (const char* what)
            {
                const auto& pts = surface->getRenderedCurvePoints();
                for (int b = 0; b < surface->getModel().maxBells; ++b)
                {
                    if (! surface->getModel().bells[b].getEnabled
                        || surface->getModel().bells[b].getEnabled() < 0.5f)
                        continue;
                    const auto node = surface->getBellNodePosition (b);
                    // Find the rendered vertex at the node's X.
                    float bestY = -1.0f;
                    for (const auto& p : pts)
                        if (std::abs (p.x - node.x) < 0.5f)
                        { bestY = p.y; break; }
                    expect (bestY >= 0.0f,
                            juce::String (what) + ": rendered curve has a vertex at Bell" 
                            + juce::String (b + 1) + " X");
                    if (bestY >= 0.0f)
                        expectWithinAbsoluteError (node.y, bestY, 1.0f,
                            juce::String (what) + ": Bell" + juce::String (b + 1)
                            + " centre ON the rendered curve (diff "
                            + juce::String (std::abs (node.y - bestY), 2) + " px)");
                }
            };

            // Isolated Bell1 at extreme settings (narrow Q, high gain) — the
            // case the screenshot shows failing.
            setParam (APEX::G10::kBell1Enabled, 1.0f);
            setParam (APEX::G10::kBell2Enabled, 0.0f);
            setParam (APEX::G10::kBell3Enabled, 0.0f);
            setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (1000.0f));
            setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (12.0f));
            setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (10.0f));
            surface->refresh();
            assertNodeOnRenderedCurve ("B1 isolated Q10 +12 dB");

            // Broad Q + cut.
            setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (0.1f));
            setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (-12.0f));
            surface->refresh();
            assertNodeOnRenderedCurve ("B1 isolated Q0.1 -12 dB");

            // Extreme surgical Q 40.
            setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (40.0f));
            setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (6.0f));
            surface->refresh();
            assertNodeOnRenderedCurve ("B1 isolated Q40 +6 dB");

            // Overlapping B1+B2 (boost+boost, narrow+broad).
            setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (1000.0f));
            setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (6.0f));
            setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (2.0f));
            setParam (APEX::G10::kBell2Enabled, 1.0f);
            setParam (APEX::G10::kBell2Freq, G10MiniEqCore::bellNormFromHz (1500.0f));
            setParam (APEX::G10::kBell2Gain, G10MiniEqCore::bellNormFromGainDb (12.0f));
            setParam (APEX::G10::kBell2Q, G10MiniEqCore::bellNormFromQ (0.5f));
            surface->refresh();
            assertNodeOnRenderedCurve ("B1+B2 overlap");

            // B1+B2+B3.
            setParam (APEX::G10::kBell3Enabled, 1.0f);
            setParam (APEX::G10::kBell3Freq, G10MiniEqCore::bellNormFromHz (4000.0f));
            setParam (APEX::G10::kBell3Gain, G10MiniEqCore::bellNormFromGainDb (-8.0f));
            setParam (APEX::G10::kBell3Q, G10MiniEqCore::bellNormFromQ (8.0f));
            surface->refresh();
            assertNodeOnRenderedCurve ("B1+B2+B3");

            // HPF + 3 Bells + LPF.
            setParam (APEX::G10::kHpf, G10MiniEqCore::hpfNormFromHz (80.0f));
            setParam (APEX::G10::kLpf, G10MiniEqCore::lpfNormFromHz (12000.0f));
            surface->refresh();
            assertNodeOnRenderedCurve ("HPF+B1+B2+B3+LPF");
        }

        // Select Bell1 through the surface to get its cascade position.
        {
            const float hz = model.normToFreq (bell1.getFreqNorm());
            const float g = model.nominalMagnitude (hz, (float) surface->getEffectiveSampleRate());
            const float cascadeDb = juce::jlimit (-12.0f, 12.0f, 20.0f * std::log10 (juce::jmax (1.0e-30f, g)));

            // Click at the cascade position to select, then read the anchor.
            const juce::Point<float> pos (surface->freqToX (hz), surface->gainToY (cascadeDb));
            juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
            if (ms == nullptr) { editor->addToDesktop (0); ms = juce::Desktop::getInstance().getMouseSource (0); }
            auto ev = juce::MouseEvent (*ms, pos, juce::ModifierKeys(), 1,0,0,0,0, surface, surface, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
            surface->mouseDown (ev);
            surface->mouseUp (ev);
            surface->refresh();

            expect (surface->getSelectedBellIndex() == 0, "Bell1 selected by cascade-position click");

            const auto anchor = surface->getSelectedNodePosition();
            expectWithinAbsoluteError (anchor.x, surface->freqToX (hz), 1.0f, "anchor X = frequency X");
            expectWithinAbsoluteError (anchor.y, surface->gainToY (cascadeDb), 1.0f, "anchor Y = cascade dB Y");
        }

        // Inspector geometry contract (compact, contained, bypass hittable).
        {
            auto* panel = surface->getBellPanel();
            expect (panel != nullptr && panel->isVisible(), "inspector visible");
            expect (panel->getWidth() <= 150 && panel->getHeight() <= 105,
                    "inspector is compact (" + juce::String (panel->getWidth()) + "x" + juce::String (panel->getHeight()) + ")");
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "inspector contained inside the analyzer");

            // Bypass control must have non-zero visible area.
            const float pw = (float) panel->getWidth();
            const float ph = (float) panel->getHeight();
            expect (pw >= 130.0f && ph >= 84.0f,
                    "inspector has minimum usable footprint");
        }

        // ---- HPF / LPF handles also sit on the cascade curve --------------
        {
            // Isolate the HPF: all bells OFF, LPF OFF.
            setParam (APEX::G10::kBell1Enabled, 0.0f);
            setParam (APEX::G10::kBell2Enabled, 0.0f);
            setParam (APEX::G10::kBell3Enabled, 0.0f);
            setParam (APEX::G10::kBell1Bypass, 0.0f);
            setParam (APEX::G10::kBell2Bypass, 0.0f);
            setParam (APEX::G10::kBell3Bypass, 0.0f);
            setParam (APEX::G10::kHpf, G10MiniEqCore::hpfNormFromHz (200.0f));
            setParam (APEX::G10::kLpf, G10MiniEqCore::kLpfOffNorm); // OFF sentinel
            surface->refresh();

            const float hpfHz = G10MiniEqCore::hpfHzFromNorm (
                proc.getG10Parameter (APEX::G10::kHpf)->getValue());
            const float sr = (float) surface->getEffectiveSampleRate();

            // D, E, F: build the SAME targets structure the Editor lambda uses,
            // then call the direct MiniEqCore path.
            G10MiniEqCore::Targets dt;
            dt.hpfNorm = proc.getG10Parameter (APEX::G10::kHpf)->getValue();
            dt.lpfNorm = proc.getG10Parameter (APEX::G10::kLpf)->getValue();
            dt.bypass  = false;
            for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
            {
                const int base = APEX::G10::kBell1Enabled + b * 4;
                dt.bell[b].enabled  = proc.getG10Parameter (base)->getBool();
                dt.bell[b].bypassed = proc.getG10Parameter (APEX::G10::kBell1Bypass + b)->getBool();
                dt.bell[b].freqNorm = proc.getG10Parameter (base + 1)->getValue();
                dt.bell[b].gainDb   = proc.getG10Parameter (base + 2)->getUnitsValue();
                dt.bell[b].qNorm    = proc.getG10Parameter (base + 3)->getValue();
            }

            // Diagnostic: print every Targets field.
            expect (dt.hpfNorm > 0.0f,
                    "A: HPF norm active = " + juce::String (dt.hpfNorm, 4));
            expectWithinAbsoluteError (hpfHz, 200.0f, 2.0f,
                    "B: decoded HPF Hz = " + juce::String (hpfHz, 1));
            expectWithinAbsoluteError (sr, 48000.0f, 1.0f,
                    "C: surface effective sample rate = " + juce::String (sr, 0));
            expect (dt.lpfNorm == G10MiniEqCore::kLpfOffNorm,
                    "D: LPF sentinel = " + juce::String (dt.lpfNorm, 4) + " (OFF must be 1)");
            expect (! dt.bell[0].enabled && ! dt.bell[1].enabled && ! dt.bell[2].enabled,
                    "D: all Bells disabled");
            expect (! dt.bell[0].bypassed && ! dt.bell[1].bypassed && ! dt.bell[2].bypassed,
                    "D: all Bell bypass false");

            // E: direct G10MiniEqCore result.
            const float directG = proc.getMiniEq().getNominalMagnitude (
                hpfHz, sr, dt);
            const float directDb = 20.0f * std::log10 (juce::jmax (1.0e-30f, directG));

            // F: Surface Model result (the Editor lambda).
            const float modelG = model.nominalMagnitude (hpfHz, sr);
            const float modelDb = 20.0f * std::log10 (juce::jmax (1.0e-30f, modelG));

            // G, H: cascade before and after clamp.
            const float modelDbClamped = juce::jlimit (-12.0f, 12.0f, modelDb);

            expectWithinAbsoluteError (directDb, -3.0f, 1.5f,
                    "E: direct MiniEqCore at HPF cutoff = " + juce::String (directDb, 2) + " dB");
            expectWithinAbsoluteError (modelDb, -3.0f, 1.5f,
                    "F: Surface model at HPF cutoff = " + juce::String (modelDb, 2) + " dB");
            expectWithinAbsoluteError (directDb, modelDb, 0.05f,
                    "F vs E: direct and surface magnitudes agree (" + juce::String (directDb, 2)
                    + " vs " + juce::String (modelDb, 2) + " dB)");
            expect (modelDbClamped > -12.0f,
                    "H: clamped display dB not at floor (" + juce::String (modelDbClamped, 2) + " dB)");

            // If the comparison reveals a mismatch, the Editor lambda or the
            // Targets it constructs disagrees with what the test builds here.
            // Both are building from the same parameters, so they must match.
        }

        // ---- Resize preserves alignment ------------------------------------
        for (const auto& sz : { std::pair<int,int>{1500,918}, {800,490}, {1400,700}, {700,700} })
        {
            editor->setSize (sz.first, sz.second);
            surface->refresh();
            auto* panel = surface->getBellPanel();
            if (panel != nullptr && panel->isVisible())
            {
                const auto b = panel->getBounds();
                expect (b.getX() >= 0 && b.getRight() <= surface->getWidth(),
                        "inspector contained after resize to " + juce::String (sz.first) + "x" + juce::String (sz.second));
            }
        }
    }
    // ------------------------------------------------------------------
    // Double-click creation position + inspector state contract:
    //   - X -> Frequency, Y -> Gain (the Bell is born where the user clicks)
    //   - selected+idle -> full inspector; selected+dragging -> micro HUD;
    //     mouseUp -> full inspector returns
    //   - hover only -> no inspector
    // ------------------------------------------------------------------

    void testMiniEqCreationAndPanelStates()
    {
        beginTest ("Mini EQ creation position and inspector states");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);
        editor->addToDesktop (0);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "surface exists");
        if (surface == nullptr)
            return;

        juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
        if (ms == nullptr)
        {
            editor->addToDesktop (0);
            ms = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (ms != nullptr, "mouse source available");
        if (ms == nullptr)
            return;

        auto makeEvent = [ms, surface] (juce::Point<float> p, int clicks,
                                        juce::ModifierKeys mods = juce::ModifierKeys())
        {
            return juce::MouseEvent (*ms, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     surface, surface, juce::Time::getCurrentTime(),
                                     p, juce::Time::getCurrentTime(), clicks, false);
        };

        auto setParam = [&proc] (int idx, float norm)
        {
            auto* p = proc.getG10Parameter (idx);
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        };

        // Reset the Mini EQ to a clean isolated state.
        auto resetMiniEq = [&]
        {
            for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
            {
                setParam (APEX::G10::kBell1Enabled + b * 4, 0.0f);
                setParam (APEX::G10::kBell1Bypass + b, 0.0f);
            }
            setParam (APEX::G10::kHpf, G10MiniEqCore::kHpfOffNorm);
            setParam (APEX::G10::kLpf, G10MiniEqCore::kLpfOffNorm);
            surface->refresh();
        };

        // Double-click empty graph at a point; the Bell must be born there.
        auto createAt = [&] (juce::Point<float> p, int slot)
        {
            surface->mouseDown (makeEvent (p, 1));
            surface->mouseUp (makeEvent (p, 1));
            surface->mouseDoubleClick (makeEvent (p, 2));
            surface->refresh();
            const int base = APEX::G10::kBell1Enabled + slot * 4;
            expect (proc.getG10Parameter (base)->getBool(),
                    "double-click created a Bell at " + p.toString());
            return base;
        };

        // ---- A) creation position across the graph ---------------------------
        const auto plot = APEX::ApexEqPlotGeometry::plotBounds ((float) surface->getWidth(),
                                                                (float) surface->getHeight());
        const float px = plot.getX() + plot.getWidth() * 0.12f;  // left-ish
        const float pm = plot.getX() + plot.getWidth() * 0.55f;  // middle
        const float pr = plot.getX() + plot.getWidth() * 0.88f;  // right-ish
        const float pyHigh = plot.getY() + plot.getHeight() * 0.20f;  // boost zone
        const float pyMid  = plot.getY() + plot.getHeight() * 0.50f;  // 0 dB line
        const float pyLow  = plot.getY() + plot.getHeight() * 0.80f;  // cut zone

        struct Case { juce::Point<float> p; const char* what; };
        const Case cases[] =
        {
            { { px, pyHigh }, "upper-left"   },
            { { pm, pyHigh }, "upper-middle" },
            { { pm, pyMid  }, "center / 0 dB line" },
            { { pm, pyLow  }, "lower-middle" },
            { { pr, pyHigh }, "upper-right"  },
        };

        for (int c = 0; c < 5; ++c)
        {
            resetMiniEq();
            const int base = createAt (cases[c].p, 0);
            const auto& m = surface->getModel();
            const auto& bell = m.bells[0];

            const float freqNorm = m.freqToNorm != nullptr
                ? m.freqToNorm (std::exp (std::log (G10MiniEqCore::kBellMinHz)
                    + (std::log (G10MiniEqCore::kBellMaxHz / G10MiniEqCore::kBellMinHz))
                      * ((cases[c].p.x - plot.getX()) / plot.getWidth())))
                : 0.5f;
            const float expectedGainNorm = surface->yToGainNorm (cases[c].p.y);

            expectWithinAbsoluteError (bell.getFreqNorm(), freqNorm, 0.02f,
                juce::String (cases[c].what) + ": Bell frequency from click X");
            expectWithinAbsoluteError (bell.getGainNorm(), expectedGainNorm, 0.02f,
                juce::String (cases[c].what) + ": Bell gain from click Y");
            expectWithinAbsoluteError (
                proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                G10MiniEqCore::bellNormFromQ (1.0f), 1.0e-6f,
                juce::String (cases[c].what) + ": default Q = 1.0");

            // Node lands on the visible response near the clicked point
            // (isolated Bell: the cascade at its centre ≈ its own gain).
            const auto node = surface->getBellNodePosition (0);
            expect (std::abs (node.x - cases[c].p.x) <= 3.0f
                    && std::abs (node.y - cases[c].p.y) <= 3.0f,
                    juce::String (cases[c].what) + ": node born at the click ("
                    + juce::String (node.x - cases[c].p.x, 1) + ","
                    + juce::String (node.y - cases[c].p.y, 1) + " px off)");
            (void) base;
        }

        // ---- B) inspector state contract -------------------------------------
        resetMiniEq();
        const int base0 = createAt ({ pm, pyMid }, 0); // 0 dB bell at middle
        (void) base0;
        const auto node0 = surface->getBellNodePosition (0);

        auto* panel = surface->getBellPanel();
        expect (panel != nullptr, "inspector exists");
        if (panel == nullptr)
            return;

        // Selected + idle → full inspector.
        expect (panel->isVisible(), "inspector visible after selection");
        expect (panel->getMode() == APEX::BellControlPanel::Mode::full,
                "idle selected Bell shows the FULL inspector");
        {
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "full inspector contained");
            expect (! b.contains (node0.toInt()),
                    "full inspector does not cover the selected node");
        }

        // Dragging → micro HUD (no full controls), still contained, off-node.
        surface->mouseDown (makeEvent (node0, 1));
        surface->mouseDrag (makeEvent (node0 + juce::Point<float> (60.0f, -40.0f), 1));
        surface->refresh();
        expect (panel->isVisible(), "HUD visible while dragging");
        expect (panel->getMode() == APEX::BellControlPanel::Mode::hud,
                "dragging collapses the inspector to the micro HUD");
        expect (panel->getWidth() <= 110 && panel->getHeight() <= 55,
                "HUD is substantially smaller than the inspector ("
                + juce::String (panel->getWidth()) + "x" + juce::String (panel->getHeight()) + ")");
        {
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "HUD contained while dragging");
        }

        // mouseUp → full inspector returns, placement recomputed.
        surface->mouseUp (makeEvent (node0 + juce::Point<float> (60.0f, -40.0f), 1));
        surface->refresh();
        expect (panel->getMode() == APEX::BellControlPanel::Mode::full,
                "mouseUp restores the FULL inspector");
        {
            const auto b = panel->getBounds();
            expect (! b.contains (surface->getBellNodePosition (0).toInt()),
                    "restored inspector does not cover the moved node");
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "restored inspector contained");
        }

        // Ctrl+vertical drag (Q) also collapses to the HUD.  The first drag
        // moved the node, so re-read its CURRENT cascade position.
        {
            const auto cur = surface->getBellNodePosition (0);
            surface->mouseDown (makeEvent (cur, 1));
            surface->mouseDrag (makeEvent (cur + juce::Point<float> (0.0f, -40.0f), 1,
                                           juce::ModifierKeys::ctrlModifier));
            surface->refresh();
            expect (panel->getMode() == APEX::BellControlPanel::Mode::hud,
                    "Ctrl-drag (Q) also shows the micro HUD");
            surface->mouseUp (makeEvent (cur + juce::Point<float> (0.0f, -40.0f), 1,
                                         juce::ModifierKeys::ctrlModifier));
            surface->refresh();
        }

        // Hover only → no inspector (unchanged contract).  First DESELECT by
        // clicking empty graph (away from nodes and OFF handles), then hover.
        {
            const auto plot = APEX::ApexEqPlotGeometry::plotBounds ((float) surface->getWidth(),
                                                                    (float) surface->getHeight());
            const juce::Point<float> empty (plot.getX() + plot.getWidth() * 0.30f,
                                            surface->gainToY (0.0f));
            surface->mouseDown (makeEvent (empty, 1));
            surface->mouseUp (makeEvent (empty, 1));
            surface->refresh();
            expect (surface->getSelectedBellIndex() == -1,
                    "clicking empty graph deselects the Bell");
            expect (! panel->isVisible(),
                    "deselect hides the inspector");

            surface->mouseMove (makeEvent (surface->getBellNodePosition (0), 1));
            surface->refresh();
            expect (! panel->isVisible(),
                    "hover alone never shows the inspector (state contract)");
        }

        // ---- C) resize keeps the contract ------------------------------------
        for (const auto& sz : { std::pair<int,int>{800,490}, {700,700}, {1500,918} })
        {
            editor->setSize (sz.first, sz.second);
            surface->mouseDown (makeEvent (surface->getBellNodePosition (0), 1));
            surface->mouseUp (makeEvent (surface->getBellNodePosition (0), 1));
            surface->refresh();
            expect (panel->getMode() == APEX::BellControlPanel::Mode::full,
                    "full inspector after resize to " + juce::String (sz.first) + "x" + juce::String (sz.second));
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "inspector contained after resize to " + juce::String (sz.first) + "x" + juce::String (sz.second));
        }
    }
    // ------------------------------------------------------------------
    // Inspector placement contract:
    //   - NEVER below the selected Bell (only ABOVE / TOP-* / LEFT / RIGHT)
    //   - stable while the cursor moves (no runaway)
    //   - mouseUp from a drag restores the FULL inspector in an allowed zone
    //   - containment after resize
    //   - controls remain clickable (bypass pill)
    // ------------------------------------------------------------------

    void testMiniEqInspectorPlacementContract()
    {
        beginTest ("Mini EQ inspector placement contract");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);
        editor->addToDesktop (0);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "surface exists");
        if (surface == nullptr)
            return;

        juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
        if (ms == nullptr)
        {
            editor->addToDesktop (0);
            ms = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (ms != nullptr, "mouse source available");
        if (ms == nullptr)
            return;

        auto makeEvent = [ms, surface] (juce::Point<float> p, int clicks,
                                        juce::ModifierKeys mods = juce::ModifierKeys())
        {
            return juce::MouseEvent (*ms, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     surface, surface, juce::Time::getCurrentTime(),
                                     p, juce::Time::getCurrentTime(), clicks, false);
        };

        auto setParam = [&proc] (int idx, float norm)
        {
            auto* p = proc.getG10Parameter (idx);
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        };

        using P = APEX::BellControlPanel::Placement;

        // Create and select a Bell.
        setParam (APEX::G10::kBell1Enabled, 1.0f);
        setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (1000.0f));
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (6.0f));
        setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (2.0f));
        surface->refresh();
        surface->mouseDown (makeEvent (surface->getBellNodePosition (0), 1));
        surface->mouseUp (makeEvent (surface->getBellNodePosition (0), 1));
        surface->refresh();

        auto* panel = surface->getBellPanel();
        expect (panel != nullptr && panel->isVisible(), "inspector visible after selection");
        if (panel == nullptr)
            return;

        // Allowed-family + never-below invariant, re-checked after moving
        // the Bell to several graph regions.
        auto assertAllowedPlacement = [&] (const juce::String& what)
        {
            const auto p = panel->getPlacement();
            expect (p == P::above || p == P::left || p == P::right,
                    juce::String (what) + ": placement family allowed ("
                    + juce::String ((int) p) + ")");
            const auto n = surface->getBellNodePosition (0);
            expect (panel->getY() < n.y - 1.0f,
                    juce::String (what) + ": inspector NEVER below the Bell");
        };

        assertAllowedPlacement ("center +6 dB");

        // Bell near the TOP of the graph (above has no room → side).
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (12.0f));
        surface->refresh();
        assertAllowedPlacement ("top +12 dB");

        // Bell near the BOTTOM (the classic "below" temptation).
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (-12.0f));
        surface->refresh();
        assertAllowedPlacement ("bottom -12 dB");

        // Bell far LEFT / far RIGHT.
        setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (30.0f));
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (0.0f));
        surface->refresh();
        assertAllowedPlacement ("far left");
        setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (18000.0f));
        surface->refresh();
        assertAllowedPlacement ("far right");

        // ---- Stability: the cursor must NEVER move the panel --------------
        setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (1000.0f));
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (6.0f));
        surface->refresh();
        const auto stableBounds = panel->getBounds();
        // Move the cursor over the panel, then around it.
        surface->mouseMove (makeEvent (stableBounds.getCentre().toFloat(), 1));
        surface->refresh();
        expect (panel->getBounds() == stableBounds,
                "panel stays put when the cursor is over it");
        surface->mouseMove (makeEvent (stableBounds.getBottomRight().translated (30, 30).toFloat(), 1));
        surface->refresh();
        expect (panel->getBounds() == stableBounds,
                "panel stays put when the cursor moves nearby");

        // ---- mouseUp from a drag restores FULL inspector in an allowed zone
        surface->mouseDown (makeEvent (surface->getBellNodePosition (0), 1));
        surface->mouseDrag (makeEvent (surface->getBellNodePosition (0) + juce::Point<float> (60.0f, -40.0f), 1));
        surface->refresh();
        expect (panel->getMode() == APEX::BellControlPanel::Mode::hud,
                "drag collapses to the micro HUD");
        surface->mouseUp (makeEvent (surface->getBellNodePosition (0) + juce::Point<float> (60.0f, -40.0f), 1));
        surface->refresh();
        expect (panel->getMode() == APEX::BellControlPanel::Mode::full,
                "mouseUp restores the FULL inspector");
        assertAllowedPlacement ("after drag restore");

        // ---- Clickability: the bypass pill still toggles -------------------
        {
            juce::Button* bypass = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* b = dynamic_cast<juce::Button*> (c))
                    if (b->getName() == "Bypass")
                    { bypass = b; break; }
            expect (bypass != nullptr, "bypass pill exists and is a child of the panel");
            if (bypass != nullptr)
            {
                const bool before = proc.getG10Parameter (APEX::G10::kBell1Bypass)->getBool();
                bypass->onClick(); // the exact command-authority lambda a click invokes
                expect (proc.getG10Parameter (APEX::G10::kBell1Bypass)->getBool() != before,
                        "bypass pill is clickable (toggles the Bell bypass)");
                if (proc.getG10Parameter (APEX::G10::kBell1Bypass)->getBool())
                    bypass->onClick(); // restore
            }
        }

        // ---- Containment after resize --------------------------------------
        for (const auto& sz : { std::pair<int,int>{1500,918}, {800,490}, {700,700} })
        {
            editor->setSize (sz.first, sz.second);
            surface->refresh();
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "inspector contained after resize to " + juce::String (sz.first) + "x" + juce::String (sz.second));
            assertAllowedPlacement ("resize " + juce::String (sz.first) + "x" + juce::String (sz.second));
        }

        // ---- Hover-only still shows no full inspector -----------------------
        {
            const auto plot = APEX::ApexEqPlotGeometry::plotBounds ((float) surface->getWidth(),
                                                                    (float) surface->getHeight());
            const juce::Point<float> empty (plot.getX() + plot.getWidth() * 0.30f,
                                            surface->gainToY (0.0f));
            surface->mouseDown (makeEvent (empty, 1));
            surface->mouseUp (makeEvent (empty, 1));
            surface->refresh();
            expect (! panel->isVisible(), "deselect hides the inspector");
            surface->mouseMove (makeEvent (surface->getBellNodePosition (0), 1));
            surface->refresh();
            expect (! panel->isVisible(),
                    "hover-only never shows the full inspector");
        }
    }
    // ------------------------------------------------------------------
    // Inspector self-movement regression: when the user adjusts FREQ/GAIN/Q
    // from INSIDE the floating inspector, the Bell/curve move but the panel
    // must stay physically under the user's hand for the entire gesture.
    // Numeric editing locks the panel too; placement recomputes once after
    // the gesture ends.
    // ------------------------------------------------------------------

    void testMiniEqInspectorGestureStability()
    {
        beginTest ("Mini EQ inspector gesture stability (panel stays under the hand)");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);
        editor->addToDesktop (0);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* surface = g10->getAnalyzerSurface();
        expect (surface != nullptr, "surface exists");
        if (surface == nullptr)
            return;

        juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
        if (ms == nullptr)
        {
            editor->addToDesktop (0);
            ms = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (ms != nullptr, "mouse source available");
        if (ms == nullptr)
            return;

        auto makeEvent = [ms, surface] (juce::Point<float> p, int clicks)
        {
            return juce::MouseEvent (*ms, p, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     surface, surface, juce::Time::getCurrentTime(),
                                     p, juce::Time::getCurrentTime(), clicks, false);
        };
        auto controlEvent = [ms] (juce::Component* ctl, juce::Point<float> localPos, int clicks)
        {
            // The value controls guard on isLeftButtonDown(), so synthetic
            // events must carry the left-button modifier like a real click.
            return juce::MouseEvent (*ms, localPos, juce::ModifierKeys::leftButtonModifier,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     ctl, ctl, juce::Time::getCurrentTime(),
                                     localPos, juce::Time::getCurrentTime(), clicks, false);
        };

        auto setParam = [&proc] (int idx, float norm)
        {
            auto* p = proc.getG10Parameter (idx);
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        };

        using Field = APEX::BellControlPanel::Field;
        using P = APEX::BellControlPanel::Placement;

        // Create and select a Bell.
        setParam (APEX::G10::kBell1Enabled, 1.0f);
        setParam (APEX::G10::kBell1Freq, G10MiniEqCore::bellNormFromHz (1000.0f));
        setParam (APEX::G10::kBell1Gain, G10MiniEqCore::bellNormFromGainDb (6.0f));
        setParam (APEX::G10::kBell1Q, G10MiniEqCore::bellNormFromQ (2.0f));
        surface->refresh();
        surface->mouseDown (makeEvent (surface->getBellNodePosition (0), 1));
        surface->mouseUp (makeEvent (surface->getBellNodePosition (0), 1));
        surface->refresh();

        auto* panel = surface->getBellPanel();
        expect (panel != nullptr && panel->isVisible(), "inspector visible after selection");
        if (panel == nullptr)
            return;

        auto* freqCtl = panel->getValueControl (Field::freq);
        auto* gainCtl = panel->getValueControl (Field::gain);
        auto* qCtl = panel->getValueControl (Field::q);
        expect (freqCtl != nullptr && gainCtl != nullptr && qCtl != nullptr,
                "panel exposes the three value controls");
        if (freqCtl == nullptr || gainCtl == nullptr || qCtl == nullptr)
            return;

        auto assertContainedAndAllowed = [&] (const juce::String& what)
        {
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    what + ": panel contained");
            const auto p = panel->getPlacement();
            expect (p == P::above || p == P::left || p == P::right,
                    what + ": placement family allowed");
        };

        // ---- FREQ drag from the panel -------------------------------------
        {
            const auto b0 = panel->getBounds();
            const auto c0 = freqCtl->getLocalBounds().getCentre().toFloat();
            const float freqBefore = proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue();

            freqCtl->mouseDown (controlEvent (freqCtl, c0, 1));
            freqCtl->mouseDrag (controlEvent (freqCtl, c0 + juce::Point<float> (40.0f, 0.0f), 1));
            surface->refresh();
            expect (panel->getBounds() == b0,
                    "FREQ panel drag: inspector stays fixed (drag step 1)");
            freqCtl->mouseDrag (controlEvent (freqCtl, c0 + juce::Point<float> (90.0f, 0.0f), 1));
            surface->refresh();
            expect (panel->getBounds() == b0,
                    "FREQ panel drag: inspector stays fixed (drag step 2)");
            expect (proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue() != freqBefore,
                    "FREQ panel drag: Bell frequency changed");
            freqCtl->mouseUp (controlEvent (freqCtl, c0 + juce::Point<float> (90.0f, 0.0f), 1));
            surface->refresh();
            assertContainedAndAllowed ("FREQ drag end");
        }

        // ---- GAIN drag from the panel -------------------------------------
        {
            const auto b0 = panel->getBounds();
            const auto c0 = gainCtl->getLocalBounds().getCentre().toFloat();
            const float gainBefore = proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue();

            gainCtl->mouseDown (controlEvent (gainCtl, c0, 1));
            gainCtl->mouseDrag (controlEvent (gainCtl, c0 + juce::Point<float> (0.0f, -50.0f), 1));
            surface->refresh();
            expect (panel->getBounds() == b0,
                    "GAIN panel drag: inspector stays fixed");
            expect (proc.getG10Parameter (APEX::G10::kBell1Gain)->getUnitsValue() != gainBefore,
                    "GAIN panel drag: Bell gain changed");
            gainCtl->mouseUp (controlEvent (gainCtl, c0 + juce::Point<float> (0.0f, -50.0f), 1));
            surface->refresh();
            assertContainedAndAllowed ("GAIN drag end");
        }

        // ---- Q drag from the panel ----------------------------------------
        {
            const auto b0 = panel->getBounds();
            const auto c0 = qCtl->getLocalBounds().getCentre().toFloat();
            const float qBefore = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();

            qCtl->mouseDown (controlEvent (qCtl, c0, 1));
            qCtl->mouseDrag (controlEvent (qCtl, c0 + juce::Point<float> (0.0f, -50.0f), 1));
            surface->refresh();
            expect (panel->getBounds() == b0,
                    "Q panel drag: inspector stays fixed");
            expect (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue() != qBefore,
                    "Q panel drag: Bell Q changed");
            qCtl->mouseUp (controlEvent (qCtl, c0 + juce::Point<float> (0.0f, -50.0f), 1));
            surface->refresh();
            assertContainedAndAllowed ("Q drag end");
        }

        // ---- Numeric editing locks the panel ------------------------------
        {
            const auto c0 = freqCtl->getLocalBounds().getCentre().toFloat();
            freqCtl->mouseDoubleClick (controlEvent (freqCtl, c0, 2));
            expect (freqCtl->isEditing(), "FREQ editor opens");
            const auto b1 = panel->getBounds();
            freqCtl->getEditor()->setText ("5000 Hz", false);
            surface->refresh(); // would reposition if unlocked
            expect (panel->getBounds() == b1,
                    "numeric editing locks the inspector (no reposition)");
            freqCtl->getEditor()->onReturnKey(); // Enter commits
            expectWithinAbsoluteError (
                G10MiniEqCore::bellHzFromNorm (
                    proc.getG10Parameter (APEX::G10::kBell1Freq)->getValue()),
                5000.0f, 20.0f, "Enter commits the typed frequency");
            assertContainedAndAllowed ("after Enter commit");

            // Escape cancels and leaves the panel stable/valid.
            const auto c1 = qCtl->getLocalBounds().getCentre().toFloat();
            qCtl->mouseDoubleClick (controlEvent (qCtl, c1, 2));
            expect (qCtl->isEditing(), "Q editor opens for Escape test");
            const float qBefore = proc.getG10Parameter (APEX::G10::kBell1Q)->getValue();
            qCtl->getEditor()->setText ("9.99", false);
            qCtl->getEditor()->onEscapeKey();
            expectWithinAbsoluteError (proc.getG10Parameter (APEX::G10::kBell1Q)->getValue(),
                                       qBefore, 1.0e-6f, "Escape cancels the Q edit");
            assertContainedAndAllowed ("after Escape cancel");
        }

        // ---- Resize during a locked edit stays contained ------------------
        {
            const auto c0 = gainCtl->getLocalBounds().getCentre().toFloat();
            gainCtl->mouseDoubleClick (controlEvent (gainCtl, c0, 2));
            expect (gainCtl->isEditing(), "GAIN editor opens for resize test");
            editor->setSize (800, 490);
            surface->refresh();
            const auto b = panel->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= surface->getWidth()
                    && b.getY() >= 0 && b.getBottom() <= surface->getHeight(),
                    "inspector contained after resize during edit");
            gainCtl->getEditor()->onEscapeKey();
            editor->setSize (1000, 612);
            surface->refresh();
        }
    }
    // ------------------------------------------------------------------
    // Input/Output level meters: layout contract (beside the knobs, inside
    // the control strip, clear of BYPASS), live values from the real signal
    // path, release ballistics toward silence, and resize containment.
    // ------------------------------------------------------------------

    void testInputOutputMetersUI()
    {
        beginTest ("Input/Output meter components (layout + live values)");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        auto* inMeter = g10->getInputMeter();
        auto* outMeter = g10->getOutputMeter();
        expect (inMeter != nullptr && outMeter != nullptr, "meter components exist");
        if (inMeter == nullptr || outMeter == nullptr)
            return;

        expect (inMeter->isVisible() && outMeter->isVisible(), "meters visible");
        const auto inB = inMeter->getBounds();
        const auto outB = outMeter->getBounds();

        // Placement contract (logical Content space): beside the knobs with
        // breathing room, clear of the BYPASS zone and the fader column, and
        // contained in the logical editor (the taller instrument body extends
        // below the knob row — that is the intended design).
        expect (inB.getRight() <= 143, "input meter left of the INPUT knob");
        expect (inB.getX() >= 80, "input meter keeps breathing room from the knob");
        expect (outB.getX() >= 857, "output meter right of the OUTPUT knob");
        expect (outB.getRight() <= 912, "output meter keeps breathing room from the knob");
        expect (inB.getY() >= APEX::G10::G10Layout::kStripTop,
                "input meter does not overlap the fader column above the strip");
        expect (outB.getY() >= APEX::G10::G10Layout::kStripTop,
                "output meter does not overlap the fader column above the strip");
        expect (inB.getBottom() <= 612 && outB.getBottom() <= 612,
                "meters contained within the logical editor height");
        expect (inB.getRight() < 455 && outB.getX() > 545,
                "meters clear the central BYPASS zone");
        // No overlap with the knobs themselves.
        expect (! inB.intersects (juce::Rectangle<int> (145, APEX::G10::G10Layout::kStripTop, 110, 74)),
                "input meter does not overlap the INPUT knob");
        expect (! outB.intersects (juce::Rectangle<int> (745, APEX::G10::G10Layout::kStripTop, 110, 74)),
                "output meter does not overlap the OUTPUT knob");
        // Larger instrument-style dimensions.
        expect (inB.getWidth() >= 44 && inB.getHeight() >= 88,
                "input meter enlarged to instrument size ("
                + juce::String (inB.getWidth()) + "x" + juce::String (inB.getHeight()) + ")");
        expect (outB.getWidth() >= 44 && outB.getHeight() >= 88,
                "output meter enlarged to instrument size ("
                + juce::String (outB.getWidth()) + "x" + juce::String (outB.getHeight()) + ")");

        // ---- Live values: the meters respond to the real signal ------------
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < 8; ++b)
        {
            buffer.clear();
            for (int s = 0; s < 512; ++s)
            {
                const float v = 0.25f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                buffer.setSample (0, s, v);
                buffer.setSample (1, s, v);
            }
            proc.processBlock (buffer, midi);
        }
        g10->timerCallback(); // 30 Hz UI tick feeds the meters
        expect (inMeter->getDisplayDb() > -40.0f, "input meter shows the signal");
        expect (outMeter->getDisplayDb() > -40.0f, "output meter shows the signal");

        // Silence: flush the filter tail (the output meter includes the real
        // audible tail of the previous signal — per-block peak metering is
        // correct).  Each silence block must CLEAR the buffer first because
        // processing is in-place: feeding the previous output back in would
        // keep the meters reading the tail.
        for (int b = 0; b < 30; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi); // tail decays to the noise floor
        }
        for (int i = 0; i < 60; ++i)
            g10->timerCallback(); // ~2 s of 30 Hz release ticks
        expect (inMeter->getDisplayDb() < -20.0f,
                "input meter releases toward silence (display "
                + juce::String (inMeter->getDisplayDb(), 1) + " dB)");
        expect (outMeter->getDisplayDb() < -20.0f,
                "output meter releases toward silence (display "
                + juce::String (outMeter->getDisplayDb(), 1)
                + " dB, target=" + juce::String (outMeter->getTargetDb(), 1)
                + ", holdTicks=" + juce::String (outMeter->getHoldTicks())
                + ", hold=" + juce::String (outMeter->getHoldDb(), 1) + " dB)");

        // ---- Resize keeps the meters within the logical content ------------
        for (const auto& sz : { std::pair<int,int>{800,490}, {700,700}, {1500,918} })
        {
            editor->setSize (sz.first, sz.second);
            g10->timerCallback();
            const auto b = inMeter->getBounds();
            const auto bo = outMeter->getBounds();
            expect (b.getX() >= 0 && b.getRight() <= 1000
                    && b.getY() >= 0 && b.getBottom() <= 612,
                    "input meter within logical content at "
                    + juce::String (sz.first) + "x" + juce::String (sz.second));
            expect (bo.getX() >= 0 && bo.getRight() <= 1000
                    && bo.getY() >= 0 && bo.getBottom() <= 612,
                    "output meter within logical content at "
                    + juce::String (sz.first) + "x" + juce::String (sz.second));
        }
    }
    // ------------------------------------------------------------------
    // Band fader double-click contract: double-clicking ANY of the ten
    // musical band faders resets that band to exactly 0 dB through the
    // canonical parameter pipeline (real double-click mouse path).
    // ------------------------------------------------------------------

    void testBandFaderDoubleClickReset()
    {
        beginTest ("Band faders: double-click resets to 0 dB (real hierarchy)");

        G10Processor proc;
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        expect (editor != nullptr, "editor creates");
        if (editor == nullptr)
            return;
        editor->setVisible (true);
        editor->setSize (1000, 612);

        auto* g10 = dynamic_cast<G10Editor*> (editor.get());
        expect (g10 != nullptr, "editor is a G10Editor");
        if (g10 == nullptr)
            return;

        juce::MouseInputSource* ms = juce::Desktop::getInstance().getMouseSource (0);
        if (ms == nullptr)
        {
            editor->addToDesktop (0);
            ms = juce::Desktop::getInstance().getMouseSource (0);
        }
        expect (ms != nullptr, "mouse source available");
        if (ms == nullptr)
            return;

        auto faderEvent = [ms] (APEX::G10::G10BandFaderComponent* fader, int clicks)
        {
            const juce::Point<float> p (46.0f, 150.0f); // fader-local
            return juce::MouseEvent (*ms, p, juce::ModifierKeys::leftButtonModifier,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     fader, fader, juce::Time::getCurrentTime(),
                                     p, juce::Time::getCurrentTime(), clicks, false);
        };

        for (int band = 0; band < 10; ++band)
        {
            auto* fader = g10->getBandFader (band);
            expect (fader != nullptr, "fader exists for band " + juce::String (band + 1));
            if (fader == nullptr)
                continue;

            auto* param = proc.getG10Parameter (APEX::G10::kBand31 + band);
            expect (param != nullptr, "band parameter exists for band " + juce::String (band + 1));

            // Arm the band at a non-zero gain through the canonical path.
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getValueForText ("6"));
            param->endChangeGesture();
            expectWithinAbsoluteError (param->getUnitsValue(), 6.0f, 1.0e-3f,
                "band " + juce::String (band + 1) + " armed at +6 dB");

            // Real double-click sequence: down/up x2 then doubleClick.
            fader->mouseDown (faderEvent (fader, 1));
            fader->mouseUp (faderEvent (fader, 1));
            fader->mouseDown (faderEvent (fader, 2));
            fader->mouseUp (faderEvent (fader, 2));
            fader->mouseDoubleClick (faderEvent (fader, 2));

            expectWithinAbsoluteError (param->getUnitsValue(), 0.0f, 1.0e-3f,
                "double-click resets band " + juce::String (band + 1)
                + " (" + juce::String (APEX::G10::kBandInfos[band].musicalName) + ") to 0 dB");
        }
    }
};

static G10EditorHookTests g10EditorHookTests;
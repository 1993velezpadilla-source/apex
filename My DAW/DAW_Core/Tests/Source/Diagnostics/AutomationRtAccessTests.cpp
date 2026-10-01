#include <JuceHeader.h>

#include "../../../Source/Automation/AutomationParameterRegistryCore.h"
#include "../../../Source/Automation/AutomationLaneStoreCore.h"
#include "../../../Source/Automation/AutomationModeStateCore.h"
#include "../../../Source/Automation/AutomationParameterKeyCore.h"
#include "../../../Source/Automation/AutomationEvaluatorCore.h"

using namespace apex::automation;

/**
    Regression tests for the low-buffer hardening of the apex::automation
    realtime access path.

    Background: every rolling audio block previously took a global
    CriticalSection for the ENTIRE parameter-registry walk
    (AutomationEvaluator via registry.forEach), plus per-parameter
    CriticalSections in AutomationModeState::getMode,
    AutomationLaneStore::findLane and AutomationParameterKeyRegistry::findID,
    and PluginInstanceCore additionally built heap juce::String keys per
    parameter per block. Any message-thread automation edit (lane write,
    mode change, parameter registration) could block the audio callback —
    a priority-inversion window that scales with parameter count and is
    fatal at 64/128-sample buffers.

    The hardened design publishes immutable copy-on-write snapshots on
    mutation (message thread) and gives the audio thread lock-free,
    allocation-free RT accessors. These tests pin snapshot semantics,
    retire semantics, change generations, allocation-freedom, and
    end-to-end evaluator behavior.
*/
class AutomationRtAccessTests final : public juce::UnitTest
{
public:
    AutomationRtAccessTests() : juce::UnitTest ("automation.rt-access.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("key registry RT snapshot");
        {
            AutomationParameterKeyRegistry reg;
            const ParameterID idA = reg.getOrCreateID ("plugin.TRK_1.slot0.Foo.cutoff");
            const ParameterID idB = reg.getOrCreateID ("track.TRK_1.volume");
            expect (idA != kInvalidParameterID && idB != kInvalidParameterID && idA != idB);
            expectEquals ((int) reg.findIDRT ("plugin.TRK_1.slot0.Foo.cutoff"), (int) idA);
            expectEquals ((int) reg.findIDRT ("track.TRK_1.volume"), (int) idB);
            expectEquals ((int) reg.findIDRT ("nonexistent.key"), (int) kInvalidParameterID);

            const auto gen0 = reg.getChangeGeneration();
            reg.getOrCreateID ("new.key");
            expect (reg.getChangeGeneration() != gen0);

            // getOrCreateID on an existing key neither republishes nor changes the ID.
            const auto gen1 = reg.getChangeGeneration();
            expectEquals ((int) reg.getOrCreateID ("track.TRK_1.volume"), (int) idB);
            expectEquals (reg.getChangeGeneration(), gen1);

            // findIDRT must be allocation-free (audio-thread contract).
            // NOTE: the juce::String key is hoisted out of the checked scope —
            // constructing a String from a literal legitimately allocates,
            // which is exactly why the production RT path caches its Strings.
            const juce::String cachedKey ("track.TRK_1.volume");
            {
                juce::UnitTestAllocationChecker checker (*this);
                std::uint64_t acc = 0;
                for (int i = 0; i < 1000; ++i)
                    acc += reg.findIDRT (cachedKey);
                expect (acc != 0);
            }
        }

        beginTest ("lane store RT snapshot and lane lifetime");
        {
            AutomationLaneStore store;
            expect (store.findLaneRT (42) == nullptr);

            auto& lane = store.getOrCreateLane (42);
            auto found = store.findLaneRT (42);
            expect (found != nullptr);
            expect (found.get() == &lane);
            expect (store.findLaneRT (7) == nullptr);

            // A lane held by the audio thread survives removeLane (RCU).
            auto& lane99 = store.getOrCreateLane (99);
            (void) lane99;
            auto held = store.findLaneRT (99);
            store.removeLane (99);
            expect (store.findLaneRT (99) == nullptr);
            expect (held != nullptr);
            expectEquals ((int) held->getParameterID(), 99);

            const auto gen0 = store.getChangeGeneration();
            store.getOrCreateLane (100);
            expect (store.getChangeGeneration() != gen0);

            // findLaneRT must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                bool ok = true;
                for (int i = 0; i < 1000; ++i)
                    ok = ok && (store.findLaneRT (100) != nullptr)
                            && (store.findLaneRT (12345) == nullptr);
                expect (ok);
            }
        }

        beginTest ("mode state RT reads");
        {
            AutomationModeState modes;
            expect (modes.getModeRT (11) == AutomationMode::Read); // global default
            modes.setMode (11, AutomationMode::Touch);
            expect (modes.getModeRT (11) == AutomationMode::Touch);
            expect (modes.isReadingRT (11));

            modes.setGlobalDefaultMode (AutomationMode::Trim);
            expect (modes.getModeRT (22) == AutomationMode::Trim);

            modes.setLatchHeld (33, true);
            expect (modes.latchHeldRT (33));
            expect (! modes.latchHeldRT (44));
            modes.clearAllLatches();
            expect (! modes.latchHeldRT (33));

            modes.clearOverride (11);
            expect (modes.getModeRT (11) == AutomationMode::Trim);

            // RT reads must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                int acc = 0;
                for (int i = 0; i < 1000; ++i)
                {
                    if (modes.isReadingRT (11)) ++acc;
                    if (modes.latchHeldRT (33)) ++acc;
                }
                expectEquals (acc, 1000);
            }
        }

        beginTest ("registry RT snapshot and retire semantics");
        {
            AutomationParameterRegistry reg;
            ParameterRange range;
            range.defaultValue = 0.5f;

            auto* p1 = reg.createParameter (101, "cutoff", range);
            auto* p2 = reg.createParameter (102, "resonance", range);
            expect (p1 != nullptr && p2 != nullptr);
            expect (reg.findRT (101).get() == p1);
            expect (reg.findRT (103) == nullptr);

            int count = 0;
            reg.forEachRT ([&] (AutomationParameter&) { ++count; });
            expectEquals (count, 2);

            // Retire: unregistered parameters stay alive for in-flight RT
            // readers holding a shared_ptr (never destroyed on the audio thread).
            auto held = reg.findRT (101);
            expect (reg.unregisterParameter (101));
            expect (reg.findRT (101) == nullptr);
            expect (held != nullptr);
            expectEquals ((int) held->getID(), 101);

            // findRT + forEachRT must be allocation-free (audio-thread contract).
            {
                juce::UnitTestAllocationChecker checker (*this);
                int n = 0;
                for (int i = 0; i < 100; ++i)
                {
                    if (reg.findRT (102) != nullptr) ++n;
                    reg.forEachRT ([] (AutomationParameter&) {});
                }
                expectEquals (n, 100);
            }
        }

        beginTest ("evaluator drives parameters from published snapshots");
        {
            AutomationParameterRegistry reg;
            AutomationLaneStore lanes;
            AutomationModeState modes;
            AutomationEvaluator eval (reg, lanes, modes);

            ParameterRange range; // 0..1, default 0
            auto* p = reg.createParameter (7, "wet", range);
            expect (p != nullptr);

            {
                AutomationLane::PointVector pts;
                pts.push_back ({ 0.0, 0.25f, CurveType::Linear, 0.0f });
                pts.push_back ({ 4.0, 0.75f, CurveType::Linear, 0.0f });
                lanes.getOrCreateLane (7).replacePoints (std::move (pts));
            }

            eval.evaluateBlock (2.0, true); // linear midpoint between the two points
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);

            // Stopped transport -> no evaluation, value holds.
            eval.evaluateBlock (4.0, false);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);

            // Mode Off -> parameter skipped.
            modes.setMode (7, AutomationMode::Off);
            eval.evaluateBlock (4.0, true);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.5f, 1.0e-5f);
            modes.setMode (7, AutomationMode::Read);
            eval.evaluateBlock (4.0, true);
            expectWithinAbsoluteError (p->getNormalizedValue(), 0.75f, 1.0e-5f);

            // Steady-state evaluation must be allocation-free (this is the
            // exact call the audio engine makes every rolling block).
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 500; ++i)
                    eval.evaluateBlock (0.001 * (double) i, true);
            }
        }
    }
};

static AutomationRtAccessTests automationRtAccessTests;

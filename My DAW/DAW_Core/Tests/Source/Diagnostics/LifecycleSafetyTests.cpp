#include <JuceHeader.h>
#include <memory>
#include <atomic>
#include <thread>
#include "../../../Source/PluginSafetyCore/PluginQuarantineCore.h"

/**
    Lifecycle safety regression tests (2026-07-26 intermittent crash forensics).

    Proves the shared_ptr<bool> lifetime-token pattern: a heap-allocated
    bool survives the owning object's destruction, allowing queued async
    work to check cancellation without dereferencing freed memory.

    The three fixes that rely on this pattern:
    1. PluginScanCoordinatorCore  — token + destructor drain
    2. AudioDevicePanelUI         — SafePointer (JUCE Component)
    3. TrackColorPalette          — SafePointer (JUCE Component)

    This test validates the TOKEN MECHANISM directly (no JUCE Component
    dependencies, no message loop required).
*/
class LifecycleSafetyTests final : public juce::UnitTest
{
public:
    LifecycleSafetyTests() : juce::UnitTest ("lifecycle.safety.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("token is heap-allocated and survives object destruction");
        {
            struct FakeOwner
            {
                std::shared_ptr<bool> lifetimeToken;
                FakeOwner() : lifetimeToken(std::make_shared<bool>(true)) {}
                ~FakeOwner() { *lifetimeToken = false; }
            };

            auto owner = std::make_unique<FakeOwner>();
            auto tokenCopy = owner->lifetimeToken;

            // Token is true while owner exists
            EXPECT_TRUE(*tokenCopy);

            // Destroy owner
            owner.reset();

            // Token copy is still valid and set to false — no freed-memory access
            EXPECT_TRUE(!*tokenCopy);

            // Confirm the token heap-memory is still accessible
            *tokenCopy = true;  // should not crash
            EXPECT_TRUE(*tokenCopy);
        }

        beginTest ("token checked in a worker thread after destruction — no crash");
        {
            // This is the exact pattern used by the fixes:
            //   1. Allocate token
            //   2. Worker thread captures token + object this
            //   3. Object destructor sets token false
            //   4. Worker thread checks token before accessing this
            struct TrackedObject
            {
                std::shared_ptr<bool> token;
                std::atomic<int> accessCount{0};
                TrackedObject() : token(std::make_shared<bool>(true)) {}
                void markAccessed() { accessCount.fetch_add(1, std::memory_order_relaxed); }
            };

            auto token = std::make_shared<bool>(true);
            std::atomic<int> workerAccessed{0};

            // Simulate: worker captures token, object is destroyed, worker checks
            std::thread worker([token, &workerAccessed]()
            {
                if (*token)
                    workerAccessed.store(1, std::memory_order_relaxed);
                // If token is false, the worker skips — no dangling access
            });

            // Destroy the object (sets token to false)
            *token = false;

            worker.join();
            // Worker must have seen false — safe, no freed-memory access
            EXPECT_FALSE(workerAccessed.load());
        }

        beginTest ("multiple concurrent tokens are independent");
        {
            auto t1 = std::make_shared<bool>(true);
            auto t2 = std::make_shared<bool>(true);
            int r1 = 0, r2 = 0;

            // Simulate two concurrent work items
            std::thread w1([t1, &r1]() { if (*t1) r1 = 1; });
            std::thread w2([t2, &r2]() { if (*t2) r2 = 1; });

            *t1 = false;  // only t1 is destroyed
            w1.join();
            w2.join();

            EXPECT_FALSE(r1);
            EXPECT_TRUE(r2);
        }

        beginTest ("runtime-proven Antares VST3 quarantine is narrow and pre-instantiation");
        {
            const juce::String manufacturer = "Antares Audio Technologies";
            const juce::String vst3 = "VST3";
            EXPECT_TRUE(DAW::PluginQuarantineCore::blocksInProcessInstantiation(
                "Auto-Tune EFX+", manufacturer, vst3));
            EXPECT_TRUE(DAW::PluginQuarantineCore::blocksNativeEditor(
                "Auto-Tune EFX+", manufacturer, vst3));

            EXPECT_FALSE(DAW::PluginQuarantineCore::blocksInProcessInstantiation(
                "Auto-Tune Vocal EQ", manufacturer, vst3));
            EXPECT_TRUE(DAW::PluginQuarantineCore::blocksNativeEditor(
                "Auto-Tune Vocal EQ", manufacturer, vst3));

            EXPECT_FALSE(DAW::PluginQuarantineCore::blocksInProcessInstantiation(
                "Harmony Engine", manufacturer, vst3));
            EXPECT_FALSE(DAW::PluginQuarantineCore::blocksNativeEditor(
                "Harmony Engine", manufacturer, vst3));

            EXPECT_FALSE(DAW::PluginQuarantineCore::blocksInProcessInstantiation(
                "Auto-Tune EFX+", manufacturer, "AudioUnit"));
            EXPECT_FALSE(DAW::PluginQuarantineCore::blocksNativeEditor(
                "Auto-Tune EFX+", manufacturer, "AudioUnit"));
        }
    }

    void EXPECT_TRUE(bool c) { expect(c); }
    void EXPECT_FALSE(bool c) { expect(!c); }
};

static LifecycleSafetyTests lifecycleSafetyTests;

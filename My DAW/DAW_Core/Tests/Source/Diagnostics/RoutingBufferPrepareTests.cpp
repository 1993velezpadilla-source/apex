#include <JuceHeader.h>

// Production routing-buffer nucleus under test (header-only, RT-capable).
#include "../../Source/SoundEngineCore/ApexRoutingBufferCore.h"

//==============================================================================
/**
    @test    routing-buffer.prepare.v1
    @verify  ApexRoutingBufferCore sizing contract used by the C2 device
             lifecycle fix:

             - after AudioEngine::prepare() (message thread) every per-node
               buffer is pre-sized to the worst-case block (>= 8192 samples),
               so the first audio callback after a device reconfiguration
               (e.g. 512 -> 2048) performs capacity checks only — no
               allocation inside syncNodeBuffers();
             - repeated syncFromSnapshot() calls with growing callback block
               sizes never reallocate (stable channel pointers);
             - clearSnapshotAudio() with the device block size stays inside
               the prepared capacity;
             - after releaseResources() + prepare() the buffers are recreated
               at worst-case capacity immediately, again before any callback.

    The integration point (AudioEngine::prepare -> routingBuffers_.
    syncFromSnapshot with worstCaseBlock) is verified by code inspection and
    manual device-reconfiguration validation.
*/
class RoutingBufferPrepareTests : public juce::UnitTest
{
public:
    RoutingBufferPrepareTests()
        : juce::UnitTest ("routing-buffer.prepare.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        using DAW::SoundEngine::ApexRoutingBufferCore;
        using DAW::RoutingSnapshot;

        constexpr int kDeviceBlock512  = 512;
        constexpr int kDeviceBlock2048 = 2048;
        constexpr int kWorstCaseBlock  = 8192;   // AudioEngine::prepare worstCaseBlock floor

        auto makeSnapshot = []() -> RoutingSnapshot
        {
            RoutingSnapshot snap;
            snap.version = 7;
            snap.nodes.push_back ({ "t1",     "t1",     DAW::RoutingNodeType::Track,  true });
            snap.nodes.push_back ({ "t2",     "t2",     DAW::RoutingNodeType::Track,  true });
            snap.nodes.push_back ({ "master", "master", DAW::RoutingNodeType::Master, true });
            snap.processingOrder = { "t1", "t2", "master" };
            snap.nodeIndexById.emplace ("t1", 0u);
            snap.nodeIndexById.emplace ("t2", 1u);
            snap.nodeIndexById.emplace ("master", 2u);
            return snap;
        };

        beginTest ("prepare() pre-sizes all node buffers to worst-case capacity");
        {
            ApexRoutingBufferCore buffers;
            buffers.prepare (kWorstCaseBlock, 8);          // AudioEngine::prepare path
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock512);

            auto* t1 = buffers.findNodeBuffer ("t1");
            auto* t2 = buffers.findNodeBuffer ("t2");
            auto* master = buffers.findNodeBuffer ("master");
            expect (t1 != nullptr, "t1 node buffer exists");
            expect (t2 != nullptr, "t2 node buffer exists");
            expect (master != nullptr, "master node buffer exists");

            // The audio thread's first sync (lastRoutingSnapshotVersion_ == 0)
            // computes capacity = jmax(deviceBlock, blockSize_) where blockSize_
            // is the worst-case block — so capacity is at least 8192.
            if (t1 != nullptr)
            {
                expectEquals (t1->getNumChannels(), 2, "stereo node buffer");
                expect (t1->getNumSamples() >= kWorstCaseBlock,
                        "node buffer pre-sized to worst-case block");
            }
        }

        beginTest ("growing callback block never reallocates after prepare()");
        {
            ApexRoutingBufferCore buffers;
            buffers.prepare (kWorstCaseBlock, 8);
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock512);

            auto* t1 = buffers.findNodeBuffer ("t1");
            expect (t1 != nullptr);
            if (t1 == nullptr) return;

            const float* p512 = t1->getWritePointer (0);
            expect (p512 != nullptr);

            // Device reconfigures to 2048: the first audio-thread sync must be
            // a capacity check only. Same storage -> same channel pointer.
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock2048);
            const float* p2048 = t1->getWritePointer (0);
            expect (p512 == p2048, "no reallocation when device block grows 512 -> 2048");

            // A second sync at the same block stays put as well.
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock2048);
            expect (p2048 == t1->getWritePointer (0),
                    "repeated syncs are no-ops at stable capacity");
        }

        beginTest ("clearSnapshotAudio stays within prepared capacity");
        {
            ApexRoutingBufferCore buffers;
            buffers.prepare (kWorstCaseBlock, 8);
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock2048);

            auto* t1 = buffers.findNodeBuffer ("t1");
            expect (t1 != nullptr);
            if (t1 == nullptr) return;

            // Marker at the last sample of a 2048 device block.
            t1->setSample (0, kDeviceBlock2048 - 1, 1.0f);
            buffers.clearSnapshotAudio (makeSnapshot(), kDeviceBlock2048);
            expectEquals (t1->getSample (0, kDeviceBlock2048 - 1), 0.0f,
                          "clear covers the full device block within capacity");
        }

        beginTest ("releaseResources + prepare recreates buffers pre-sized");
        {
            ApexRoutingBufferCore buffers;
            buffers.prepare (kWorstCaseBlock, 8);
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock512);

            // audioDeviceStopped -> AudioEngine::releaseResources -> clear().
            buffers.releaseResources();
            expect (buffers.findNodeBuffer ("t1") == nullptr,
                    "buffers destroyed on release");

            // audioDeviceAboutToStart -> AudioEngine::prepare -> sync on the
            // message thread (the fix): recreated at worst-case capacity
            // BEFORE the first callback, so the 2048 callback never allocates.
            buffers.prepare (kWorstCaseBlock, 8);
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock512);
            auto* t1 = buffers.findNodeBuffer ("t1");
            expect (t1 != nullptr, "node buffers recreated after release");
            if (t1 == nullptr) return;

            expect (t1->getNumSamples() >= kWorstCaseBlock,
                    "recreated buffers are pre-sized to worst-case block");

            const float* p1 = t1->getWritePointer (0);
            buffers.syncFromSnapshot (makeSnapshot(), kDeviceBlock2048);
            expect (p1 == t1->getWritePointer (0),
                    "first 2048 callback after restart does not reallocate");
        }
    }
};

static RoutingBufferPrepareTests routingBufferPrepareTests;

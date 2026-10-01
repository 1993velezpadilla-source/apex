#include <JuceHeader.h>
#include "../../../Source/DiagnosticsCore/CallbackAuditCore.h"

class CallbackAuditRingTests final : public juce::UnitTest
{
public:
    CallbackAuditRingTests() : juce::UnitTest ("callback-audit-ring.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("FIFO order");
        {
            CallbackAuditRing<4> ring;
            for (uint64_t i = 0; i < 4; ++i)
            {
                CallbackAuditRecord rec;
                rec.sequence = i;
                expect (ring.tryPush (rec));
            }
            for (uint64_t i = 0; i < 4; ++i)
            {
                CallbackAuditRecord rec;
                expect (ring.tryPop (rec));
                expectEquals (rec.sequence, i);
            }
            CallbackAuditRecord extra;
            expect (! ring.tryPop (extra));
        }

        beginTest ("drop-newest on full ring");
        {
            CallbackAuditRing<4> ring;
            for (int i = 0; i < 4; ++i)
            {
                CallbackAuditRecord rec;
                rec.sequence = static_cast<uint64_t> (i);
                expect (ring.tryPush (rec));
            }
            CallbackAuditRecord overflow;
            overflow.sequence = 999;
            expect (! ring.tryPush (overflow));
            expectEquals (ring.getOverflowCount(), static_cast<uint64_t> (1));
        }

        beginTest ("monotonic overflow count");
        {
            CallbackAuditRing<2> ring;
            for (int i = 0; i < 10; ++i)
            {
                CallbackAuditRecord rec;
                rec.sequence = static_cast<uint64_t> (i);
                ring.tryPush (rec);
            }
            expectEquals (ring.getOverflowCount(), static_cast<uint64_t> (8));
        }

        beginTest ("wraparound");
        {
            CallbackAuditRing<4> ring;
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                for (int i = 0; i < 4; ++i)
                {
                    CallbackAuditRecord rec;
                    rec.sequence = static_cast<uint64_t> (cycle * 4 + i);
                    ring.tryPush (rec);
                }
                for (int i = 0; i < 4; ++i)
                {
                    CallbackAuditRecord rec;
                    expect (ring.tryPop (rec));
                    expectEquals (rec.sequence, static_cast<uint64_t> (cycle * 4 + i));
                }
            }
        }

        beginTest ("empty drain");
        {
            CallbackAuditRing<4> ring;
            CallbackAuditRecord rec;
            expect (! ring.tryPop (rec));
            expectEquals (ring.getOverflowCount(), static_cast<uint64_t> (0));
        }
    }
};

static CallbackAuditRingTests callbackAuditRingTests;

class CallbackAuditAccumulatorTests final : public juce::UnitTest
{
public:
    CallbackAuditAccumulatorTests() : juce::UnitTest ("callback-audit-accumulator.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("deadline miss classification");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            rec.durationTicks = 500;
            rec.deadlineMiss = 0;
            acc.add (rec);
            auto snap = acc.snapshot (0);
            expectEquals (snap.deadlineMisses, static_cast<uint64_t> (0));

            rec.deadlineMiss = 1;
            acc.add (rec);
            snap = acc.snapshot (0);
            expectEquals (snap.deadlineMisses, static_cast<uint64_t> (1));
        }

        beginTest ("p50/p95/p99/p99.9/p99.99/max from fixed histogram");
        {
            CallbackAuditAccumulator acc;
            const int count = 10000;
            for (int i = 0; i < count; ++i)
            {
                CallbackAuditRecord rec;
                rec.periodTicks = 10000;
                rec.durationTicks = (i + 1) * 2;
                rec.deadlineMiss = 0;
                acc.add (rec);
            }
            auto snap = acc.snapshot (0);
            expectEquals (snap.callbacks, static_cast<uint64_t> (count));
            expect (snap.p50 > 0.0);
            expect (snap.p95 >= snap.p50);
            expect (snap.p99 >= snap.p95);
            expect (snap.p999 >= snap.p99);
            expect (snap.p9999 >= snap.p999);
            expect (snap.maximum > 0.0);
            expect (snap.maximum >= snap.p9999);
        }

        beginTest ("reset clears state");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            rec.durationTicks = 500;
            acc.add (rec);
            acc.reset();
            auto snap = acc.snapshot (0);
            expectEquals (snap.callbacks, static_cast<uint64_t> (0));
            expectEquals (snap.p50, 0.0);
        }

        beginTest ("ring overflow passed through");
        {
            CallbackAuditAccumulator acc;
            auto snap = acc.snapshot (42);
            expectEquals (snap.ringOverflows, static_cast<uint64_t> (42));
        }

        beginTest ("maximum consecutive misses");
        {
            CallbackAuditAccumulator acc;
            for (int i = 0; i < 5; ++i)
            {
                CallbackAuditRecord rec;
                rec.periodTicks = 1000;
                rec.durationTicks = 1500;
                rec.deadlineMiss = 1;
                acc.add (rec);
            }
            CallbackAuditRecord ok;
            ok.periodTicks = 1000;
            ok.durationTicks = 500;
            ok.deadlineMiss = 0;
            acc.add (ok);
            auto snap = acc.snapshot (0);
            expectEquals (snap.maximumConsecutiveMisses, static_cast<uint64_t> (5));
        }
    }
};

static CallbackAuditAccumulatorTests callbackAuditAccumulatorTests;

class CallbackAuditAllocationTests final : public juce::UnitTest
{
public:
    CallbackAuditAllocationTests() : juce::UnitTest ("callback-audit-no-allocation.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("ring tryPush and tryPop are allocation-free");
        {
            CallbackAuditRing<256> ring;
            CallbackAuditRecord rec;
            rec.sequence = 1;
            rec.periodTicks = 1000;
            rec.durationTicks = 500;

            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 1000; ++i)
                {
                    rec.sequence = static_cast<uint64_t> (i);
                    ring.tryPush (rec);
                }
                CallbackAuditRecord out;
                for (int i = 0; i < 500; ++i)
                    ring.tryPop (out);
            }
        }

        beginTest ("accumulator add is allocation-free");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            rec.durationTicks = 500;
            rec.deadlineMiss = 0;

            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 1000; ++i)
                {
                    rec.durationTicks = i + 1;
                    rec.deadlineMiss = (i % 10 == 0) ? 1 : 0;
                    acc.add (rec);
                }
            }
        }

        beginTest ("snapshot is allocation-free");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            rec.durationTicks = 500;

            for (int i = 0; i < 100; ++i)
                acc.add (rec);

            {
                juce::UnitTestAllocationChecker checker (*this);
                auto snap = acc.snapshot (0);
                juce::ignoreUnused (snap);
            }
        }
    }
};

static CallbackAuditAllocationTests callbackAuditAllocationTests;

class CallbackAuditForensicTests final : public juce::UnitTest
{
public:
    CallbackAuditForensicTests() : juce::UnitTest ("callback-audit-forensic.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("record stays POD and bounded");
        {
            expect (std::is_trivially_copyable<CallbackAuditRecord>::value);
            // 8 stage slots + context — must stay small for the SPSC ring.
            expect (sizeof (CallbackAuditRecord) <= 160);
        }

        beginTest ("engine overrun vs late delivery classification");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;

            // APEX slow: duration exceeds the nominal period.
            rec.durationTicks = 1500;
            rec.intervalTicks = 1000;
            rec.deadlineMiss = 1;
            acc.add (rec);

            // Driver/OS late: callback arrived late but APEX finished in budget.
            rec.durationTicks = 600;
            rec.intervalTicks = 1400;
            rec.deadlineMiss = 0;
            acc.add (rec);

            auto snap = acc.snapshot (0);
            expectEquals (snap.callbacks, (uint64_t) 2);
            expectEquals (snap.engineOverruns, (uint64_t) 1);
            expectEquals (snap.lateDeliveries, (uint64_t) 1);
            expectWithinAbsoluteError (snap.intervalMaximum, 1.4, 1.0e-9);
        }

        beginTest ("maxRecord captures stage breakdown and context of slowest block");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;

            rec.sequence = 1;
            rec.durationTicks = 300;
            acc.add (rec);

            rec.sequence = 2;
            rec.durationTicks = 900;
            rec.flags = 0x3;
            rec.contextTrackCount = 17;
            rec.contextGraphVersion = 4242;
            rec.stageTicks[(size_t) CallbackStageEngine] = 700;
            rec.stageTicks[(size_t) CallbackStageRecording] = 150;
            acc.add (rec);

            rec.sequence = 3;
            rec.durationTicks = 400;
            acc.add (rec);

            auto snap = acc.snapshot (0);
            expectEquals (snap.maxRecord.sequence, (uint64_t) 2);
            expectEquals ((int) snap.maxRecord.flags, 0x3);
            expectEquals ((int) snap.maxRecord.contextTrackCount, 17);
            expectEquals (snap.maxRecord.contextGraphVersion, (uint64_t) 4242);
            expectEquals (snap.maxRecord.stageTicks[(size_t) CallbackStageEngine], (int64_t) 700);
            expectEquals (snap.maxRecord.stageTicks[(size_t) CallbackStageRecording], (int64_t) 150);
        }

        beginTest ("reset clears forensic state");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            rec.durationTicks = 2000;
            rec.intervalTicks = 1500;
            acc.add (rec);
            acc.reset();
            auto snap = acc.snapshot (0);
            expectEquals (snap.callbacks, (uint64_t) 0);
            expectEquals (snap.engineOverruns, (uint64_t) 0);
            expectEquals (snap.lateDeliveries, (uint64_t) 0);
            expectWithinAbsoluteError (snap.intervalMaximum, 0.0, 1.0e-12);
            expectEquals (snap.maxRecord.durationTicks, (int64_t) 0);
        }

        beginTest ("add with forensic fields is allocation-free");
        {
            CallbackAuditAccumulator acc;
            CallbackAuditRecord rec;
            rec.periodTicks = 1000;
            {
                juce::UnitTestAllocationChecker checker (*this);
                for (int i = 0; i < 1000; ++i)
                {
                    rec.sequence = (uint64_t) i;
                    rec.durationTicks = 100 + (i % 900);
                    rec.intervalTicks = 900 + (i % 400);
                    rec.deadlineMiss = (i % 7 == 0) ? 1 : 0;
                    rec.stageTicks[(size_t) CallbackStageEngine] = i;
                    acc.add (rec);
                }
            }
            auto snap = acc.snapshot (0);
            expectEquals (snap.callbacks, (uint64_t) 1000);
        }
    }
};

/**
    B4 nominal-period and adaptive-drain math (2026-07-25 sample-rate /
    32-sample-buffer expansion). The nominal deadline must derive from the
    ACTUAL device rate/block for every supported combination, and the ring
    drain cadence must keep the 1024-slot ring below half full even at
    192 kHz / 32 samples (6000 callbacks/sec).
*/
class CallbackAuditPeriodTests final : public juce::UnitTest
{
public:
    CallbackAuditPeriodTests() : juce::UnitTest ("callback-audit-period.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("period ticks derive from actual rate/block across the full matrix");
        {
            constexpr double tps = 10000000.0; // Windows high-resolution ticks/second
            const double rates[]  = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            const int    blocks[] = { 32, 64, 128, 256, 512, 1024, 2048 };

            for (const double rate : rates)
                for (const int block : blocks)
                {
                    const double expected = (static_cast<double> (block) / rate) * tps;
                    const auto ticks = computeCallbackPeriodTicks (block, rate, tps);
                    expectWithinAbsoluteError (static_cast<double> (ticks), expected, 1.0);
                    expect (ticks > 0);
                }
        }

        beginTest ("spot-exact nominal deadlines");
        {
            constexpr double tps = 10000000.0;
            expectEquals (computeCallbackPeriodTicks (64, 48000.0, tps),  (int64_t) 13333); // 1.333 ms
            expectEquals (computeCallbackPeriodTicks (32, 48000.0, tps),  (int64_t) 6666);  // 0.667 ms
            expectEquals (computeCallbackPeriodTicks (32, 96000.0, tps),  (int64_t) 3333);  // 0.333 ms
            expectEquals (computeCallbackPeriodTicks (32, 192000.0, tps), (int64_t) 1666);  // 0.167 ms
            expectEquals (computeCallbackPeriodTicks (32, 44100.0, tps),  (int64_t) 7256);  // 0.726 ms
            expectEquals (computeCallbackPeriodTicks (256, 48000.0, tps), (int64_t) 53333); // 5.333 ms (protected baseline)
        }

        beginTest ("invalid inputs yield zero (caller treats as unarmed)");
        {
            expectEquals (computeCallbackPeriodTicks (0, 48000.0, 10000000.0), (int64_t) 0);
            expectEquals (computeCallbackPeriodTicks (64, 0.0, 10000000.0),    (int64_t) 0);
            expectEquals (computeCallbackPeriodTicks (64, 48000.0, 0.0),       (int64_t) 0);
        }

        beginTest ("adaptive drain interval keeps the ring below half full");
        {
            constexpr size_t capacity = 1024;
            // 48k/32: 0.5 * 1024 * 0.000667 = 0.341 s
            expectWithinAbsoluteError (computeAuditDrainIntervalSeconds (32.0 / 48000.0, capacity),
                                       0.3413, 0.001);
            // 192k/32: 0.0853 s — must NOT be clamped above 0.1 s
            expectWithinAbsoluteError (computeAuditDrainIntervalSeconds (32.0 / 192000.0, capacity),
                                       0.0853, 0.001);
            // 48k/64: 0.6827 s
            expectWithinAbsoluteError (computeAuditDrainIntervalSeconds (64.0 / 48000.0, capacity),
                                       0.6827, 0.001);
            // floor: absurdly small period clamps to 0.05 s
            expectEquals (computeAuditDrainIntervalSeconds (1.0 / 192000.0, capacity), 0.05);
            // ceiling: 48k/2048 = 21.8 s -> 5.0 s cap
            expectEquals (computeAuditDrainIntervalSeconds (2048.0 / 48000.0, capacity), 5.0);
            // degenerate inputs fall back to the legacy 5 s cadence
            expectEquals (computeAuditDrainIntervalSeconds (0.0, capacity), 5.0);
            expectEquals (computeAuditDrainIntervalSeconds (0.001, (size_t) 0), 5.0);
        }

        beginTest ("ring exposes its capacity for drain math");
        {
            expectEquals (CallbackAuditRing<1024>::kCapacity, (size_t) 1024);
            expectEquals (CallbackAuditRing<4>::kCapacity, (size_t) 4);
        }
    }
};

static CallbackAuditPeriodTests callbackAuditPeriodTests;

static CallbackAuditForensicTests callbackAuditForensicTests;
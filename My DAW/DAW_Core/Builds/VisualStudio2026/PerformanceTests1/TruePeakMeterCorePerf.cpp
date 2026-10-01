
#include "pch.h"
#include "CppUnitTest.h"
#include <vector>
#include <cmath>
#include <chrono>
#include <numeric>
#include <string>
#include "../../../Source/MeteringCore/TruePeakMeterCore.h"
// Pull in the unit under test


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DAW;

namespace TruePeakMeterCorePerf
{
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    static std::vector<float> makeSineBlock(int numSamples, float freq = 440.f, float sr = 44100.f)
    {
        std::vector<float> buf(numSamples);
        for (int i = 0; i < numSamples; ++i)
            buf[i] = 0.8f * std::sin(2.f * 3.14159265f * freq * (float)i / sr);
        return buf;
    }

    // -----------------------------------------------------------------------
    // Performance test class
    // -----------------------------------------------------------------------
    TEST_CLASS(TruePeakMeterCorePerformanceTest)
    {
    public:

        // --------------------------------------------------------------------
        // Baseline: 512-sample block, single channel, 1 000 iterations.
        // Mirrors the real audio-thread call pattern (86 blocks/sec @ 44.1 kHz).
        // --------------------------------------------------------------------
        TEST_METHOD(Baseline_512Block_1000Iterations)
        {
            constexpr int   kBlockSize  = 512;
            constexpr int   kIterations = 1000;
            constexpr double kSampleRate = 44100.0;

            TruePeakMeterCore meter;
            meter.prepare(kSampleRate, kBlockSize);

            auto block = makeSineBlock(kBlockSize);

            // --- warm-up (not measured) ---
            for (int w = 0; w < 10; ++w)
                meter.process(block.data(), kBlockSize);

            // --- measured loop ---
            auto t0 = std::chrono::high_resolution_clock::now();

            for (int it = 0; it < kIterations; ++it)
                meter.process(block.data(), kBlockSize);

            auto t1 = std::chrono::high_resolution_clock::now();

            const double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double perBlockUs = (totalMs / kIterations) * 1000.0;   // µs per block
            const double budgetUs   = (1.0 / 86.0) * 1e6;                 // ~11 628 µs real-time budget

            // Log for the Test Output pane
            std::wstring msg =
                L"[TruePeakMeterCore Baseline]\n"
                L"  Block size      : " + std::to_wstring(kBlockSize) + L" samples\n"
                L"  Iterations      : " + std::to_wstring(kIterations) + L"\n"
                L"  Total time      : " + std::to_wstring((long long)totalMs) + L" ms\n"
                L"  Per-block (avg) : " + std::to_wstring((long long)perBlockUs) + L" µs\n"
                L"  RT budget/block : " + std::to_wstring((long long)budgetUs) + L" µs\n"
                L"  Budget used     : " + std::to_wstring((long long)((perBlockUs / budgetUs) * 100.0)) + L"%\n";
            Logger::WriteMessage(msg.c_str());

            // Sanity: must produce a valid (non-NaN) peak
            Assert::IsTrue(std::isfinite(meter.getPeak()), L"getPeak() returned non-finite value");

            // Performance assertion: single-track, single-channel process() must
            // complete a 512-sample block in under 2 000 µs (well under RT budget).
            Assert::IsTrue(perBlockUs < 2000.0,
                (L"Per-block time " + std::to_wstring((long long)perBlockUs) +
                 L" µs exceeds 2000 µs threshold").c_str());
        }

        // --------------------------------------------------------------------
        // Stress: 8 tracks × 2 channels — simulates the many-plugins scenario
        // that was observed at ~42% CPU in the profiler.
        // --------------------------------------------------------------------
        TEST_METHOD(Stress_8Tracks_Stereo_500Iterations)
        {
            constexpr int   kNumTracks  = 8;
            constexpr int   kBlockSize  = 512;
            constexpr int   kIterations = 500;
            constexpr double kSampleRate = 44100.0;

            std::vector<TruePeakMeterCore> metersL(kNumTracks), metersR(kNumTracks);
            for (int t = 0; t < kNumTracks; ++t)
            {
                metersL[t].prepare(kSampleRate, kBlockSize);
                metersR[t].prepare(kSampleRate, kBlockSize);
            }

            auto blockL = makeSineBlock(kBlockSize, 440.f);
            auto blockR = makeSineBlock(kBlockSize, 660.f);

            // warm-up
            for (int w = 0; w < 5; ++w)
                for (int t = 0; t < kNumTracks; ++t)
                {
                    metersL[t].process(blockL.data(), kBlockSize);
                    metersR[t].process(blockR.data(), kBlockSize);
                }

            auto t0 = std::chrono::high_resolution_clock::now();

            for (int it = 0; it < kIterations; ++it)
                for (int t = 0; t < kNumTracks; ++t)
                {
                    metersL[t].process(blockL.data(), kBlockSize);
                    metersR[t].process(blockR.data(), kBlockSize);
                }

            auto t1 = std::chrono::high_resolution_clock::now();

            const double totalMs     = std::chrono::duration<double, std::milli>(t1 - t0).count();
            const double perRoundUs  = (totalMs / kIterations) * 1000.0;   // µs for all tracks, one block
            const double budgetUs    = (1.0 / 86.0) * 1e6;

            std::wstring msg =
                L"[TruePeakMeterCore Stress — 8 tracks stereo]\n"
                L"  Iterations      : " + std::to_wstring(kIterations) + L"\n"
                L"  Total time      : " + std::to_wstring((long long)totalMs) + L" ms\n"
                L"  Per-round (avg) : " + std::to_wstring((long long)perRoundUs) + L" µs\n"
                L"  RT budget/block : " + std::to_wstring((long long)budgetUs) + L" µs\n"
                L"  Budget used     : " + std::to_wstring((long long)((perRoundUs / budgetUs) * 100.0)) + L"%\n";
            Logger::WriteMessage(msg.c_str());

            for (int t = 0; t < kNumTracks; ++t)
            {
                Assert::IsTrue(std::isfinite(metersL[t].getPeak()), L"L getPeak() non-finite");
                Assert::IsTrue(std::isfinite(metersR[t].getPeak()), L"R getPeak() non-finite");
            }
        }
    };
}

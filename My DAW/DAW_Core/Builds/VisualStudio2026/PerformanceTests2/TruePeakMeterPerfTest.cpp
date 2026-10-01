#include "pch.h"
#include <CppUnitTest.h>
#include <vector>
#include <chrono>
#include <cmath>
#include "../../../Source/MeteringCore/TruePeakMeterCore.h"
// Pull in the header under test directly (header-only implementation)


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace TruePeakMeterPerfTests
{
    TEST_CLASS(TruePeakMeterPerfTest)
    {
    public:
        TEST_METHOD(Baseline_Process_512Samples)
        {
            DAW::TruePeakMeterCore meter;
            meter.prepare(44100.0, 512);

            // Synthesise a 1 kHz sine wave block — realistic metering input
            constexpr int kNumSamples = 512;
            std::vector<float> block(kNumSamples);
            for (int i = 0; i < kNumSamples; ++i)
                block[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * (float)i / 44100.0f);

            // Warm up — not measured
            for (int w = 0; w < 10; ++w)
                meter.process(block.data(), kNumSamples);

            // Timed measurement — 1000 iterations simulating ~22 seconds of audio
            constexpr int kIter = 1000;
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int it = 0; it < kIter; ++it)
                meter.process(block.data(), kNumSamples);
            auto t1 = std::chrono::high_resolution_clock::now();

            double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double perBlockUs = (totalMs / kIter) * 1000.0;

            Logger::WriteMessage(("TruePeakMeterCore::process 512-sample block — "
                + std::to_string(perBlockUs) + " us/block  ("
                + std::to_string(totalMs) + " ms total for "
                + std::to_string(kIter) + " iters)").c_str());

            // Sanity: result must be non-trivial
            Assert::IsTrue(meter.getPeak() > 0.0f, L"Peak should be > 0");
        }

        TEST_METHOD(Baseline_Process_2048Samples)
        {
            DAW::TruePeakMeterCore meter;
            meter.prepare(44100.0, 2048);

            constexpr int kNumSamples = 2048;
            std::vector<float> block(kNumSamples);
            for (int i = 0; i < kNumSamples; ++i)
                block[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * (float)i / 44100.0f);

            for (int w = 0; w < 10; ++w)
                meter.process(block.data(), kNumSamples);

            constexpr int kIter = 250;
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int it = 0; it < kIter; ++it)
                meter.process(block.data(), kNumSamples);
            auto t1 = std::chrono::high_resolution_clock::now();

            double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            double perBlockUs = (totalMs / kIter) * 1000.0;

            Logger::WriteMessage(("TruePeakMeterCore::process 2048-sample block — "
                + std::to_string(perBlockUs) + " us/block  ("
                + std::to_string(totalMs) + " ms total for "
                + std::to_string(kIter) + " iters)").c_str());

            Assert::IsTrue(meter.getPeak() > 0.0f, L"Peak should be > 0");
        }
    };
}

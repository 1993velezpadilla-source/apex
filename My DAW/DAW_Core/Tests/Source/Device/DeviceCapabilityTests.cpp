#include <JuceHeader.h>
#include "../../../Source/DeviceCore/DeviceCapabilityCore.h"

/**
    Device-authoritative capability enumeration (2026-07-25 sample-rate /
    32-sample-buffer expansion).

    The UI may only offer what the device/driver actually reports,
    intersected with the professional rate set and the supported buffer
    ladder. 32 samples is EXPERIMENTAL_ULTRA_LOW_LATENCY and must be
    labeled as such. These suites cover the pure selection/labeling logic
    (DeviceCapabilityCore.h is deliberately free of juce_audio_devices so
    it is testable here); the device-facing probe is exercised on hardware.
*/
class DeviceCapabilityTests final : public juce::UnitTest
{
public:
    DeviceCapabilityTests() : juce::UnitTest ("device.capabilities.v1", "APEX.Device") {}

    void runTest() override
    {
        beginTest ("professional rate set is exact and ascending");
        {
            const auto rates = DAW::DeviceCapability::professionalSampleRates();
            const double expected[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            expectEquals (rates.size(), 7);
            for (int i = 0; i < rates.size() && i < 7; ++i)
                expectWithinAbsoluteError (rates[i], expected[i], 0.001);
        }

        beginTest ("buffer ladder is exact and ascending");
        {
            const auto sizes = DAW::DeviceCapability::supportedBufferSizeLadder();
            const int expected[] = { 32, 64, 128, 256, 512, 1024, 2048 };
            expectEquals (sizes.size(), 7);
            for (int i = 0; i < sizes.size() && i < 7; ++i)
                expectEquals (sizes[i], expected[i]);
        }

        beginTest ("rate intersection keeps only device-supported professional rates");
        {
            juce::Array<double> deviceRates { 96000.0, 44100.0, 22050.0, 192000.0, 44100.0, 48000.5 };
            const auto out = DAW::DeviceCapability::intersectProfessionalRates (deviceRates);
            // 48000.5 is NOT 48000; 22050 is not professional; duplicate 44100 deduped.
            expectEquals (out.size(), 3);
            expectWithinAbsoluteError (out[0], 44100.0, 0.001);
            expectWithinAbsoluteError (out[1], 96000.0, 0.001);
            expectWithinAbsoluteError (out[2], 192000.0, 0.001);

            expect (DAW::DeviceCapability::intersectProfessionalRates ({}).isEmpty());
            expect (DAW::DeviceCapability::intersectProfessionalRates ({ 12345.0 }).isEmpty());
        }

        beginTest ("buffer intersection keeps only device-supported ladder sizes incl. 32");
        {
            juce::Array<int> deviceSizes { 4096, 16, 32, 96, 64, 128, 2048, 64 };
            const auto out = DAW::DeviceCapability::filterSupportedBufferSizes (deviceSizes);
            // 4096/16/96 are not in the ladder; duplicate 64 deduped; ladder order kept.
            expectEquals (out.size(), 4);
            expectEquals (out[0], 32);
            expectEquals (out[1], 64);
            expectEquals (out[2], 128);
            expectEquals (out[3], 2048);
            expectEquals (out.getLast(), 2048);

            expect (DAW::DeviceCapability::filterSupportedBufferSizes ({}).isEmpty());
            expect (DAW::DeviceCapability::filterSupportedBufferSizes ({ 24, 8192 }).isEmpty());
        }

        beginTest ("32 is the only experimental ultra-low-latency size and is labeled");
        {
            expect (DAW::DeviceCapability::isExperimentalUltraLowLatency (32));
            expect (! DAW::DeviceCapability::isExperimentalUltraLowLatency (64));
            expect (! DAW::DeviceCapability::isExperimentalUltraLowLatency (0));
            expectEquals (DAW::DeviceCapability::formatBufferSizeLabel (32),
                          juce::String ("32 (experimental)"));
            expectEquals (DAW::DeviceCapability::formatBufferSizeLabel (256),
                          juce::String ("256"));
        }

        beginTest ("32 label parses back to 32 for the commit path");
        {
            // AudioDevicePanelUI parses combo text with getIntValue(); the
            // label must keep the leading integer intact.
            expectEquals (DAW::DeviceCapability::formatBufferSizeLabel (32).getIntValue(), 32);
            expectEquals (DAW::DeviceCapability::formatBufferSizeLabel (64).getIntValue(), 64);
        }

        beginTest ("resume does not restart a device for fewer granted inputs than the request capacity");
        {
            // Windows Audio commonly grants a stereo microphone while Apex
            // requests capacity for up to 64 channels. That is already a valid
            // device setup and must not cause a second restart after Apply.
            expect (! DAW::DeviceCapability::shouldRepairMissingInputChannels (true, 64, 2));
            expect (! DAW::DeviceCapability::shouldRepairMissingInputChannels (true, 64, 1));
            expect (DAW::DeviceCapability::shouldRepairMissingInputChannels (true, 64, 0));
            expect (! DAW::DeviceCapability::shouldRepairMissingInputChannels (false, 64, 0));
            expect (! DAW::DeviceCapability::shouldRepairMissingInputChannels (true, 0, 0));
        }
    }
};

class DeviceConfigValidationTests final : public juce::UnitTest
{
public:
    DeviceConfigValidationTests() : juce::UnitTest ("device.config-validation.v1", "APEX.Device") {}

    void runTest() override
    {
        beginTest ("empty capability lists disable enforcement (advisory mode)");
        {
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (12345.0, 47, {}, {});
            expect (report.ok);
        }

        beginTest ("supported values pass");
        {
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (
                96000.0, 32, { 44100.0, 48000.0, 96000.0 }, { 32, 64, 256 });
            expect (report.ok);
        }

        beginTest ("unsupported rate rejected with explicit reason");
        {
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (
                192000.0, 64, { 44100.0, 48000.0 }, { 32, 64 });
            expect (! report.ok);
            expect (juce::String (report.reason).containsIgnoreCase ("sample rate"));
            expect (juce::String (report.reason).containsIgnoreCase ("not supported"));
        }

        beginTest ("32-sample buffer rejected when the device did not report it");
        {
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (
                48000.0, 32, { 44100.0, 48000.0 }, { 64, 128, 256 });
            expect (! report.ok);
            expect (juce::String (report.reason).containsIgnoreCase ("buffer"));
        }

        beginTest ("32-sample buffer accepted when the device reported it");
        {
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (
                48000.0, 32, { 44100.0, 48000.0 }, { 32, 64 });
            expect (report.ok);
        }

        beginTest ("a request slightly off an offered rate is rejected honestly");
        {
            // 48000.5 is not 48000 — no silent aliasing of unsupported rates.
            const auto report = DAW::DeviceCapability::checkAgainstSupportedConfig (
                48000.5, 64, { 44100.0, 48000.0 }, { 32, 64 });
            expect (! report.ok);
        }
    }
};

static DeviceConfigValidationTests deviceConfigValidationTests;

static DeviceCapabilityTests deviceCapabilityTests;

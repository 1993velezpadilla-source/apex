#pragma once

#include <JuceHeader.h>
#include <functional>
#include <set>
#include "DevicePanelModelCore.h"
#include "DeviceCapabilityCore.h"

namespace DAW {

struct DeviceRequest
{
    juce::String typeName;
    juce::String outputDeviceName;
    juce::String inputDeviceName;
    juce::BigInteger outputChannels;
    bool useDefaultOutputChannels = true;
    double sampleRate = 48000.0;
    int bufferSize = 512;
};

// Practical multichannel input ceiling: lets JUCE expose every hardware
// input the driver offers (devices clamp oversized masks safely) instead
// of hard-limiting the app to whatever count the manager last cached.
static constexpr int kMaxRequestedInputChannels = 64;

struct ValidationReport
{
    bool ok = false;
    juce::String reason;
};

class DeviceSessionCore
{
public:
    DeviceSessionCore(juce::AudioDeviceManager& dm,
                      std::function<bool()> recordingActiveQuery,
                      std::function<void()> beforeDeviceMutation)
        : dm_(dm),
          recordingActiveQuery_(std::move(recordingActiveQuery)),
          beforeDeviceMutation_(std::move(beforeDeviceMutation))
    {
    }

    /** Forget which backends have been scanned. Call when the panel opens
        so the session re-detects hot-plugged devices exactly once. */
    void invalidateScanCache()
    {
        scannedTypes_.clear();
    }

    /** Optional device-reported capability lists (pushed by the panel after
        enumeration). Empty lists = advisory mode: no enforcement, JUCE/driver
        nearest-match + read-back govern (as before this feature). */
    void setSupportedConfig(juce::Array<double> rates, juce::Array<int> sizes)
    {
        supportedRates_ = std::move (rates);
        supportedBufferSizes_ = std::move (sizes);
    }

    /** Capability-membership check mapped onto ValidationReport. The pure
        logic lives in DeviceCapabilityCore.h (unit-tested headless). */
    static ValidationReport checkAgainstSupportedConfig(const DeviceRequest& r,
                                                        const juce::Array<double>& rates,
                                                        const juce::Array<int>& sizes)
    {
        const auto result = DeviceCapability::checkAgainstSupportedConfig (r.sampleRate, r.bufferSize, rates, sizes);
        return { result.ok, juce::String (result.reason) };
    }

    ValidationReport validate(const DeviceRequest& r) const
    {
        if (r.typeName.isEmpty())
            return { false, "Select an audio backend." };

        auto& types = dm_.getAvailableDeviceTypes();
        juce::AudioIODeviceType* type = nullptr;
        for (auto* candidate : types)
        {
            if (candidate != nullptr && candidate->getTypeName() == r.typeName)
            {
                type = candidate;
                break;
            }
        }

        if (type == nullptr)
            return { false, "Selected audio backend is unavailable." };

        // Scan once per panel session, not on every keystroke/combo change.
        // ASIO4ALL rescans re-probe every WDM endpoint and can block for
        // seconds — this was the "panel gets really slow" root cause.
        ensureScanned(*type);

        const auto outputs = type->getDeviceNames(false);
        const auto outputName = r.outputDeviceName.trim();
        if (outputName.isEmpty() || outputName == DevicePanelModelCore::noDeviceSentinel())
            return { false, "Select an output device before applying." };
        if (!outputs.contains(outputName))
            return { false, "Selected output device is unavailable." };

        const auto inputName = r.inputDeviceName.trim();
        if (inputName.isNotEmpty())
        {
            const auto inputs = type->getDeviceNames(true);
            if (!inputs.contains(inputName))
                return { false, "Selected input device is unavailable." };
        }

        if (isAsioType(r.typeName)
            && !r.useDefaultOutputChannels
            && r.outputChannels.countNumberOfSetBits() <= 0)
        {
            return { false, "Select an ASIO output pair before applying." };
        }

        if (r.sampleRate <= 0.0)
            return { false, "Select a valid sample rate." };
        if (r.bufferSize <= 0)
            return { false, "Select a valid buffer size." };

        // Device authority: when the panel supplied the enumerated capability
        // lists, reject configurations the device did not report (32-sample
        // buffers and high rates are only legal when actually exposed).
        const auto capability = checkAgainstSupportedConfig (r, supportedRates_, supportedBufferSizes_);
        if (! capability.ok)
            return capability;

        return { true, {} };
    }

    DeviceRequest snapshotCurrent() const
    {
        DeviceRequest request;
        request.typeName = dm_.getCurrentAudioDeviceType();

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm_.getAudioDeviceSetup(setup);

        request.outputDeviceName = setup.outputDeviceName;
        request.inputDeviceName = setup.inputDeviceName;
        request.outputChannels = setup.outputChannels;
        request.useDefaultOutputChannels = setup.useDefaultOutputChannels;
        request.sampleRate = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
        request.bufferSize = setup.bufferSize > 0 ? setup.bufferSize : 512;

        if (request.typeName.containsIgnoreCase("asio")
            && (request.outputDeviceName.trim().isEmpty()
                || request.outputDeviceName.containsIgnoreCase("none")))
        {
            request.outputDeviceName = DevicePanelModelCore::noDeviceSentinel();
        }

        return request;
    }

    juce::String commit(const DeviceRequest& r)
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

        DBG("[DeviceSession] commit request type=" + r.typeName
            + " out=" + r.outputDeviceName
            + " in=" + r.inputDeviceName
            + " sr=" + juce::String(juce::roundToInt(r.sampleRate))
            + " buf=" + juce::String(r.bufferSize));

        if (recordingActiveQuery_ && recordingActiveQuery_())
            return "Recording in progress - stop recording before changing audio devices.";

        const auto validation = validate(r);
        if (!validation.ok)
        {
            DBG("[DeviceSession] validation failed: " + validation.reason);
            return validation.reason;
        }

        if (beforeDeviceMutation_)
            beforeDeviceMutation_();

        const auto oldType = dm_.getCurrentAudioDeviceType();
        juce::AudioDeviceManager::AudioDeviceSetup oldSetup;
        dm_.getAudioDeviceSetup(oldSetup);

        DBG("[DeviceSession] previous active type=" + oldType
            + " out=" + oldSetup.outputDeviceName
            + " in=" + oldSetup.inputDeviceName
            + " sr=" + juce::String(juce::roundToInt(oldSetup.sampleRate))
            + " buf=" + juce::String(oldSetup.bufferSize));

        if (r.typeName != oldType)
            dm_.setCurrentAudioDeviceType(r.typeName, true);

        juce::String resolvedInput = r.inputDeviceName.trim();
        bool typeHasSeparateInputs = true;
        for (auto* t : dm_.getAvailableDeviceTypes())
        {
            if (t != nullptr && t->getTypeName() == r.typeName)
            {
                typeHasSeparateInputs = t->hasSeparateInputsAndOutputs();
                if (resolvedInput.isEmpty() && typeHasSeparateInputs)
                {
                    const auto inputs = t->getDeviceNames(true);
                    if (! inputs.isEmpty())
                    {
                        const int def = juce::jlimit(0, inputs.size() - 1,
                                                     t->getDefaultDeviceIndex(true));
                        resolvedInput = inputs[def];
                        DBG("[DeviceSession] auto-selected default input=["
                            + resolvedInput + "]");
                    }
                }
                break;
            }
        }

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.outputDeviceName = r.outputDeviceName.trim();
        setup.inputDeviceName = resolvedInput;
        setup.sampleRate = r.sampleRate;
        setup.bufferSize = r.bufferSize;
        setup.outputChannels = r.outputChannels;
        setup.useDefaultOutputChannels = r.useDefaultOutputChannels;

        // Explicit input mask instead of useDefaultInputChannels: JUCE resolves
        // default channels from the manager's cached numInputChansNeeded, and
        // if the manager was ever configured with 0 inputs that cache stays 0 -
        // every commit would then reopen the device with no input channels even
        // though a valid input device is selected (no monitoring, no recording).
        // An explicit mask re-derives the count from the mask itself and the
        // device clamps it to the hardware's real channel set.
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        if (resolvedInput.isNotEmpty() || ! typeHasSeparateInputs)
            setup.inputChannels.setRange(0, kMaxRequestedInputChannels, true);

        const auto error = dm_.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
        {
            // Input endpoint blocked (mic privacy / exclusive claim)? Keep the
            // user's chosen device alive output-only instead of failing the
            // whole commit - the input watchdog and record preflight surface
            // the input problem without killing playback.
            if (setup.inputDeviceName.isNotEmpty())
            {
                DBG("[DeviceSession] combined open failed: " + error
                    + " - retrying output-only");

                auto outputOnly = setup;
                outputOnly.inputDeviceName.clear();
                outputOnly.inputChannels.clear();

                if (dm_.setAudioDeviceSetup(outputOnly, true).isEmpty()
                    && dm_.getCurrentAudioDevice() != nullptr)
                {
                    DBG("[DeviceSession] output-only fallback active - input"
                        " unavailable: " + error);
                    return {};
                }
            }

            DBG("[DeviceSession] setAudioDeviceSetup failed: " + error);
            dm_.setCurrentAudioDeviceType(oldType, true);
            dm_.setAudioDeviceSetup(oldSetup, true);
            return error + "  (rolled back to previous device)";
        }

        auto* activeDevice = dm_.getCurrentAudioDevice();
        const auto activeType = dm_.getCurrentAudioDeviceType();
        juce::AudioDeviceManager::AudioDeviceSetup activeSetup;
        dm_.getAudioDeviceSetup(activeSetup);

        DBG("[DeviceSession] post-commit active type=" + activeType
            + " out=" + activeSetup.outputDeviceName
            + " in=" + activeSetup.inputDeviceName
            + " sr=" + juce::String(juce::roundToInt(activeSetup.sampleRate))
            + " buf=" + juce::String(activeSetup.bufferSize)
            + " device=" + (activeDevice != nullptr ? activeDevice->getName() : juce::String("<null>"))
            + " open=" + juce::String(activeDevice != nullptr && activeDevice->isOpen() ? 1 : 0)
            + " playing=" + juce::String(activeDevice != nullptr && activeDevice->isPlaying() ? 1 : 0)
            + " lastError=" + (activeDevice != nullptr ? activeDevice->getLastError() : juce::String("<null>")));

        if (r.typeName.containsIgnoreCase("asio"))
        {
            const bool activeTypeMatches = activeType == r.typeName;
            const bool activeOutputMatches = activeSetup.outputDeviceName.trim() == r.outputDeviceName.trim();
            const bool activeDeviceMatches = activeDevice != nullptr && activeDevice->getName().trim() == r.outputDeviceName.trim();

            if (!activeTypeMatches || !activeOutputMatches || !activeDeviceMatches)
            {
                const auto reason = "Selected ASIO device did not become active.  (rolled back to previous device)";
                DBG("[DeviceSession] " + reason);
                dm_.setCurrentAudioDeviceType(oldType, true);
                dm_.setAudioDeviceSetup(oldSetup, true);
                return reason;
            }
        }

        return {};
    }

private:
    juce::AudioDeviceManager& dm_;
    std::function<bool()> recordingActiveQuery_;
    std::function<void()> beforeDeviceMutation_;
    juce::Array<double> supportedRates_;
    juce::Array<int> supportedBufferSizes_;
    mutable std::set<juce::String> scannedTypes_;

    void ensureScanned(juce::AudioIODeviceType& type) const
    {
        if (scannedTypes_.count(type.getTypeName()) > 0)
            return;

        type.scanForDevices();
        scannedTypes_.insert(type.getTypeName());
    }

    static bool isAsioType(const juce::String& typeName)
    {
        return typeName.containsIgnoreCase("asio");
    }
};

} // namespace DAW

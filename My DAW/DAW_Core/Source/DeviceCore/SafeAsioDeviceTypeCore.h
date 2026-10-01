#pragma once
#include <JuceHeader.h>

namespace DAW {

namespace seh {
bool tryOpen (juce::AudioIODevice* d,
              const juce::BigInteger& in,
              const juce::BigInteger& out,
              double sr,
              int buf,
              juce::String& errOut) noexcept;
bool tryStart (juce::AudioIODevice* d,
               juce::AudioIODeviceCallback* cb) noexcept;
juce::AudioIODevice* tryCreate (juce::AudioIODeviceType* t,
                                const juce::String& o,
                                const juce::String& i) noexcept;
}

class GuardedAsioDevice : public juce::AudioIODevice
{
public:
    explicit GuardedAsioDevice (std::unique_ptr<juce::AudioIODevice> inner);

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    std::optional<juce::BigInteger> getDefaultOutputChannels() const override;
    std::optional<juce::BigInteger> getDefaultInputChannels() const override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;
    juce::String open (const juce::BigInteger& inputChannels,
                       const juce::BigInteger& outputChannels,
                       double sampleRate,
                       int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override;
    void start (juce::AudioIODeviceCallback* callback) override;
    void stop() override;
    bool isPlaying() override;
    juce::String getLastError() override;
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override;
    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;
    juce::AudioWorkgroup getWorkgroup() const override;
    bool hasControlPanel() const override;
    bool showControlPanel() override;
    bool setAudioPreprocessingEnabled (bool shouldBeEnabled) override;
    int getXRunCount() const noexcept override;

private:
    std::unique_ptr<juce::AudioIODevice> inner_;
    juce::String lastError_;
};

class SafeAsioDeviceType : public juce::AudioIODeviceType,
                           private juce::AudioIODeviceType::Listener
{
public:
    SafeAsioDeviceType();
    ~SafeAsioDeviceType() override;

    bool isUsable() const noexcept { return inner_ != nullptr; }

    juce::StringArray getDeviceNames (bool wantInputNames = false) const override;
    int getDefaultDeviceIndex (bool forInput) const override;
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override;

    void scanForDevices() override;
    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName,
                                       const juce::String& inputDeviceName) override;
    void notifyDeviceListChanged() { callDeviceChangeListeners(); }

private:
    void audioDeviceListChanged() override { callDeviceChangeListeners(); }

    std::unique_ptr<juce::AudioIODeviceType> inner_;
};

// Registers the full Windows device-type set on `dm`, substituting
// SafeAsioDeviceType for JUCE's raw ASIO. MUST run before the first call to
// dm.getAvailableDeviceTypes()/getCurrentAudioDeviceType()/initialise()/
// setAudioChannels() — otherwise JUCE already created the raw ASIO type and
// this adds a DUPLICATE.
void installSafeAudioDeviceTypes (juce::AudioDeviceManager& dm);

} // namespace DAW

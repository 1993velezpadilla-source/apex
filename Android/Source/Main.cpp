/*
    APEX Android - Phase 1 USB audio bring-up.
    Intentionally independent of the Windows desktop shell.
    No allocations, file I/O, device switching, or GUI work in the audio callback.
    Not yet a full DAW or recorder.
*/
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

class ApexAndroidAudioProbe final : public juce::Component,
                                    private juce::AudioIODeviceCallback,
                                    private juce::Timer
{
public:
    ApexAndroidAudioProbe()
    {
        addAndMakeVisible(title_);
        addAndMakeVisible(description_);
        addAndMakeVisible(status_);
        addAndMakeVisible(meter_);
        addAndMakeVisible(monitor_);
        addAndMakeVisible(settings_);
        addAndMakeVisible(refresh_);

        title_.setText("APEX  /  ANDROID", juce::dontSendNotification);
        title_.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
        title_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(245, 245, 249));

        description_.setText("USB AUDIO  •  CLASS-COMPLIANT DEVICE PROBE",
                             juce::dontSendNotification);
        description_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(169, 177, 195));
        status_.setColour(juce::Label::textColourId, juce::Colours::white);
        meter_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(141, 240, 172));
        monitor_.setButtonText("Input monitoring (headphones only)");
        monitor_.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        monitor_.setToggleState(false, juce::dontSendNotification);
        monitor_.onClick = [this]
        {
            monitorEnabled_.store(monitor_.getToggleState(), std::memory_order_release);
        };
        settings_.setButtonText("Audio devices / input and output");
        settings_.onClick = [this] { showDeviceSettings(); };
        refresh_.setButtonText("Refresh audio connection");
        refresh_.onClick = [this] { initialiseAudio(); };

        setOpaque(true);
        startTimerHz(12);
#if JUCE_ANDROID
        // Android 6+ requires the microphone permission at runtime.
        // Do not open an input stream before permission is granted.
        if (!juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
        {
            status_.setText("Requesting microphone permission...",
                            juce::dontSendNotification);
            juce::Component::SafePointer<ApexAndroidAudioProbe> safeThis(this);
            juce::RuntimePermissions::request(
                juce::RuntimePermissions::recordAudio,
                [safeThis](bool granted)
                {
                    if (safeThis == nullptr)
                        return;
                    if (granted)
                        safeThis->initialiseAudio();
                    else
                        safeThis->status_.setText(
                            "Microphone denied. Allow permission in Android settings.",
                            juce::dontSendNotification);
                });
        }
        else
#endif
        {
            initialiseAudio();
        }
    }

    ~ApexAndroidAudioProbe() override
    {
        stopTimer();
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(17, 19, 29));
        const auto area = getLocalBounds().reduced(20);
        g.setColour(juce::Colour::fromRGB(39, 44, 66));
        g.fillRoundedRectangle(area.toFloat(), 18.0f);
        g.setColour(juce::Colour::fromRGB(100, 189, 247));
        g.fillRoundedRectangle(juce::Rectangle<float>(
            (float) area.getX(), (float) area.getY(), (float) area.getWidth(), 4.0f), 2.0f);
    }

    void resized() override
    {
        const auto area = getLocalBounds().reduced(40);
        int y = area.getY() + 14;
        title_.setBounds(area.getX(), y, area.getWidth(), 52); y += 55;
        description_.setBounds(area.getX(), y, area.getWidth(), 30); y += 40;
        status_.setBounds(area.getX(), y, area.getWidth(), 68); y += 76;
        meter_.setBounds(area.getX(), y, area.getWidth(), 36); y += 44;
        monitor_.setBounds(area.getX(), y, area.getWidth(), 45); y += 54;
        settings_.setBounds(area.getX(), y, juce::jmin(380, area.getWidth()), 48); y += 58;
        refresh_.setBounds(area.getX(), y, juce::jmin(380, area.getWidth()), 48);
    }

private:
    void initialiseAudio()
    {
#if JUCE_ANDROID
        if (!juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio))
        {
            status_.setText("Microphone permission required.",
                            juce::dontSendNotification);
            return;
        }
#endif
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
        // Let Android and JUCE negotiate the actual sample rate, buffer, and
        // device route. This must NOT force a particular interface/brand.
        const auto error = deviceManager_.initialise(1, 2, nullptr, true);
        if (error.isNotEmpty())
        {
            status_.setText("Audio open failed: " + error, juce::dontSendNotification);
            return;
        }
        deviceManager_.addAudioCallback(this);
        updateStatus();
    }

    void showDeviceSettings()
    {
        // A JUCE selector is a convenience. On some Android versions the
        // USB route is chosen by Android itself and will not appear as an
        // independently selectable JUCE device name.
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager_, 1, 2, 1, 2, false, false, true, false);
        selector->setSize(620, 480);
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(selector.release());
        options.dialogTitle = "APEX Audio Devices";
        options.dialogBackgroundColour = juce::Colour::fromRGB(28, 32, 47);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = true;
        options.launchAsync();
    }

    void updateStatus()
    {
        auto* device = deviceManager_.getCurrentAudioDevice();
        if (device == nullptr)
        {
            status_.setText("No active audio device.", juce::dontSendNotification);
            return;
        }
        const auto sampleRate = (int) std::lround(device->getCurrentSampleRate());
        const auto bufferSize = device->getCurrentBufferSizeSamples();
        const auto inputCount = device->getActiveInputChannels().countNumberOfSetBits();
        const auto outputCount = device->getActiveOutputChannels().countNumberOfSetBits();
        status_.setText(
            "Route: " + device->getName()
            + "\n" + juce::String(sampleRate) + " Hz  /  "
            + juce::String(bufferSize) + " frames  /  input "
            + juce::String(inputCount) + "  output "
            + juce::String(outputCount),
            juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const auto peak = peak_.exchange(0.0f, std::memory_order_relaxed);
        const auto db = 20.0f * std::log10(juce::jmax(peak, 0.000001f));
        meter_.setText("INPUT PEAK   " + juce::String(db, 1) + " dBFS",
                       juce::dontSendNotification);
        // Device changes may be initiated by USB hotplug / the Android OS.
        updateStatus();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override
    {
        peak_.store(0.0f, std::memory_order_relaxed);
    }
    void audioDeviceStopped() override
    {
        peak_.store(0.0f, std::memory_order_relaxed);
    }
    void audioDeviceIOCallbackWithContext(
        const float* const* inputs, int inputChannels,
        float* const* outputs, int outputChannels, int samples,
        const juce::AudioIODeviceCallbackContext&) override
    {
        const float* input = (inputs != nullptr && inputChannels > 0) ? inputs[0] : nullptr;
        float peak = 0.0f;
        if (input != nullptr)
            for (int i = 0; i < samples; ++i)
                peak = juce::jmax(peak, std::abs(input[i]));
        auto previous = peak_.load(std::memory_order_relaxed);
        while (peak > previous
            && !peak_.compare_exchange_weak(previous, peak,
                                            std::memory_order_relaxed)) {}

        const bool monitor = monitorEnabled_.load(std::memory_order_acquire);
        // Output is silent by default to prevent speaker feedback.
        for (int ch = 0; ch < outputChannels; ++ch)
        {
            auto* out = outputs != nullptr ? outputs[ch] : nullptr;
            if (out == nullptr)
                continue;
            if (monitor && input != nullptr)
                juce::FloatVectorOperations::copy(out, input, samples);
            else
                juce::FloatVectorOperations::clear(out, samples);
        }
    }

    juce::AudioDeviceManager deviceManager_;
    std::atomic<float> peak_ { 0.0f };
    std::atomic<bool> monitorEnabled_ { false };
    juce::Label title_, description_, status_, meter_;
    juce::ToggleButton monitor_;
    juce::TextButton settings_, refresh_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ApexAndroidAudioProbe)
};

class ApexAndroidApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "APEX Android"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        window_ = std::make_unique<MainWindow>();
    }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : juce::DocumentWindow("APEX Android",
                                   juce::Colour::fromRGB(17, 19, 29), 0)
        {
            setContentOwned(new ApexAndroidAudioProbe(), true);
            setUsingNativeTitleBar(true);
            setFullScreen(true);
            setVisible(true);
        }
        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION(ApexAndroidApplication)

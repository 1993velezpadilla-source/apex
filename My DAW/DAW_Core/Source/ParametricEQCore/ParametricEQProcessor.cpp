#include "ParametricEQProcessor.h"
#include "../ParametricEQUI/ParametricEQEditor.h"

#include <cmath>
#include <utility>

namespace APEX::ParametricEQ
{

class Processor::KernelWorker final : public juce::Thread
{
public:
    explicit KernelWorker (Processor& owner)
        : juce::Thread ("APEX Parametric EQ Linear Phase Kernel"),
          owner_ (owner)
    {
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const auto request = owner_.linearKernelRequest_.load (
                std::memory_order_acquire);
            if (request == 0)
            {
                juce::Thread::sleep (5);
                continue;
            }
            owner_.buildLinearKernel (request);
        }
    }

private:
    Processor& owner_;
};

void Processor::buildLinearKernel (std::uint32_t generation) noexcept
{
    const auto active = linearKernelActiveIndex_.load (std::memory_order_acquire);
    const auto targetIndex = 1u - active;
    LinearPhaseKernelBuilder builder;
    builder.build (pendingKernelSettings_, pendingKernelMode_,
                   pendingKernelRate_,
                   linearKernels_[static_cast<std::size_t> (targetIndex)]);
    linearKernelActiveIndex_.store (targetIndex, std::memory_order_release);
    linearKernelGeneration_.store (generation, std::memory_order_release);
    linearKernelRequest_.store (0, std::memory_order_release);
}

void Processor::requestLinearKernelRebuild() noexcept
{
    // The snapshot handoff is race-free: the audio thread writes the staging
    // snapshot only while the worker is idle (request == 0), and the worker
    // acquires the request only after the snapshot's release store. If a
    // build is in flight, mark pending and re-request once it completes.
    if (linearKernelRequest_.load (std::memory_order_acquire) != 0)
    {
        kernelRebuildPending_ = true;
        return;
    }
    pendingKernelSettings_ = targetSettings_;
    pendingKernelMode_ = targetMode_;
    pendingKernelRate_ = getSampleRate();
    kernelRebuildPending_ = false;
    linearKernelRequest_.store (
        linearKernelGeneration_.load (std::memory_order_acquire) + 1,
        std::memory_order_release);
}

void Processor::adoptPhaseMode() noexcept
{
    const bool requested = parameters_[static_cast<std::size_t> (
        kPhaseModeParameter)]->getBool();
    if (requested == linearPhaseTarget_)
        return;

    linearPhaseTarget_ = requested;
    modeStep_ = 1.0 / std::max (1.0, getSampleRate() * 0.010);
    if (requested)
    {
        // Entering linear phase: clean convolvers, a fresh kernel request,
        // and the minimum-phase engine keeps running while modeMix_ ramps.
        convolverNextL_.reset();
        convolverNextR_.reset();
        requestLinearKernelRebuild();
        currentKernelIndex_ = linearKernelActiveIndex_.load (
            std::memory_order_acquire);
        kernelTransitionActive_ = false;
        kernelMix_ = 1.0;
    }
    else
    {
        // Leaving linear phase: snap the minimum-phase engine to the current
        // settings so the crossfade starts from the correct target.
        configureEngine (currentEngine_, targetSettings_, targetMode_, true);
        targetEngine_ = currentEngine_;
        currentSettings_ = targetSettings_;
        currentMode_ = targetMode_;
        transitionMix_ = 0.0;
        transitionActive_ = false;
        pendingPlacementBands_ = 0;
    }

    // Latency must match the audible path before the crossfade progresses.
    setLatencySamples (requested ? kLinearPhaseLatencySamples : 0);
    linearPhaseMode_ = requested;
}

void Processor::processLinearPhase (juce::AudioBuffer<float>& buffer,
                                    int numberOfChannels,
                                    int numberOfSamples) noexcept
{
    const int channels = std::min (numberOfChannels,
                                   linearPhaseScratch_.getNumChannels());
    if (channels <= 0)
        return;

    // Adopt any newly published kernel generation, starting a bounded
    // crossfade between the old and new kernels.
    const auto generation = linearKernelGeneration_.load (
        std::memory_order_acquire);
    if (generation != linearKernelAdoptedGeneration_)
    {
        if (! kernelTransitionActive_)
        {
            kernelTransitionActive_ = true;
            kernelMix_ = 0.0;
            convolverNextL_.reset();
            convolverNextR_.reset();
            nextKernelIndex_ = linearKernelActiveIndex_.load (
                std::memory_order_acquire);
        }
        linearKernelAdoptedGeneration_ = generation;
        if (kernelRebuildPending_)
            requestLinearKernelRebuild();
    }

    const auto& currentKernels = linearKernels_[static_cast<std::size_t> (
        currentKernelIndex_)];
    convolverCurrentL_.processBlock (buffer.getWritePointer (0),
                                     numberOfSamples, currentKernels.ll.data());
    if (channels >= 2)
        convolverCurrentR_.processBlock (buffer.getWritePointer (1),
                                         numberOfSamples,
                                         currentKernels.rr.data());
    else
        convolverCurrentR_.processBlock (buffer.getWritePointer (0),
                                         numberOfSamples,
                                         currentKernels.ll.data());

    if (kernelTransitionActive_ && kernelMix_ < 1.0)
    {
        // Copy the dry input into the next-kernel scratch and convolve with
        // the newly published kernel; crossfade per sample.
        const auto& nextKernels = linearKernels_[static_cast<std::size_t> (
            nextKernelIndex_)];
        for (int channel = 0; channel < channels; ++channel)
            kernelNextScratch_.copyFrom (channel, 0, buffer, channel, 0,
                                         numberOfSamples);
        convolverNextL_.processBlock (kernelNextScratch_.getWritePointer (0),
                                      numberOfSamples, nextKernels.ll.data());
        if (channels >= 2)
            convolverNextR_.processBlock (kernelNextScratch_.getWritePointer (1),
                                          numberOfSamples, nextKernels.rr.data());
        else
            convolverNextR_.processBlock (kernelNextScratch_.getWritePointer (0),
                                          numberOfSamples, nextKernels.ll.data());

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const auto mix = static_cast<float> (kernelMix_);
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* destination = buffer.getWritePointer (channel);
                const auto* next = kernelNextScratch_.getReadPointer (channel);
                destination[sample] += mix * (next[sample] - destination[sample]);
            }
            kernelMix_ = std::min (1.0, kernelMix_ + kernelStep_);
        }
        if (kernelMix_ >= 1.0)
        {
            std::swap (convolverCurrentL_, convolverNextL_);
            std::swap (convolverCurrentR_, convolverNextR_);
            currentKernelIndex_ = nextKernelIndex_;
            kernelTransitionActive_ = false;
            convolverNextL_.reset();
            convolverNextR_.reset();
        }
    }
}
namespace
{

constexpr const char* kShapeNames[] =
{
    "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
    "Notch", "Band Pass", "Tilt", "Flat Tilt", "All Pass"
};

constexpr const char* kCharacterNames[] =
{
    "Pure", "Velvet", "Heat"
};

constexpr const char* kBandParameterSuffixes[] =
{
    "enabled", "bypass", "shape", "frequency", "gain", "q", "slope"
};

float parseFrequency (const juce::String& source) noexcept
{
    const auto text = source.trim().toLowerCase();
    auto value = text.getFloatValue();
    if (text.containsChar ('k'))
        value *= 1000.0f;
    return value;
}

juce::String formatFrequency (float frequencyHz)
{
    if (frequencyHz >= 1000.0f)
        return juce::String (frequencyHz / 1000.0f,
                             frequencyHz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (frequencyHz, frequencyHz < 10.0f ? 1 : 0) + " Hz";
}

} // namespace

ParametricEQParameter::ParametricEQParameter (
    const juce::ParameterID& id, const juce::String& name,
    Kind kind, float minimumUnits, float maximumUnits, float defaultUnits)
    : juce::AudioProcessorParameterWithID (id, name),
      kind_ (kind),
      minimumUnits_ (minimumUnits),
      maximumUnits_ (maximumUnits),
      defaultUnits_ (defaultUnits),
      value_ (toNormalised (defaultUnits))
{
}

float ParametricEQParameter::getValue() const
{
    return value_.load (std::memory_order_relaxed);
}

void ParametricEQParameter::setValue (float newValue)
{
    if (! std::isfinite (newValue))
        return;
    value_.store (std::clamp (newValue, 0.0f, 1.0f),
                  std::memory_order_relaxed);
}

float ParametricEQParameter::getDefaultValue() const
{
    return toNormalised (defaultUnits_);
}

juce::String ParametricEQParameter::getText (float normalisedValue,
                                             int maximumLength) const
{
    juce::ignoreUnused (maximumLength);
    const auto value = fromNormalised (normalisedValue);
    switch (kind_)
    {
        case Kind::Toggle:
            return normalisedValue >= 0.5f ? "On" : "Off";
        case Kind::Shape:
            return kShapeNames[std::clamp (static_cast<int> (std::lround (value)),
                                           0, 9)];
        case Kind::Placement:
            return channelPlacementName (static_cast<ChannelPlacement> (
                std::clamp (static_cast<int> (std::lround (value)), 0,
                            kChannelPlacementCount - 1)));
        case Kind::ThresholdDb:
            return juce::String (value >= 0.0f ? "+" : "")
                 + juce::String (value, 1) + " dB";
        case Kind::RangeDb:
            return juce::String (value >= 0.0f ? "+" : "")
                 + juce::String (value, 1) + " dB";
        case Kind::TimeSeconds:
            return juce::String (value * 1000.0f, value < 0.01f ? 1 : 0) + " ms";
        case Kind::DetectorChoice:
            return normalisedValue >= 0.5f ? "RMS" : "Peak";
        case Kind::SourceChoice:
            return normalisedValue >= 0.5f ? "External" : "Internal";
        case Kind::PhaseModeChoice:
            return normalisedValue >= 0.5f ? "Linear Phase" : "Minimum Phase";
        case Kind::CharacterChoice:
            return kCharacterNames[std::clamp (
                static_cast<int> (std::lround (value)), 0,
                kCharacterModeCount - 1)];
        case Kind::Frequency:
            return formatFrequency (value);
        case Kind::Gain:
            return juce::String (value >= 0.0f ? "+" : "")
                 + juce::String (value, 2) + " dB";
        case Kind::Q:
            return "Q " + juce::String (value, value < 1.0f ? 3 : 2);
        case Kind::Slope:
            return juce::String (value, 1) + " dB/oct";
        case Kind::DesignMode:
            return normalisedValue >= 0.5f ? "Analog Matched" : "Realtime";
    }
    return {};
}

float ParametricEQParameter::getValueForText (const juce::String& source) const
{
    const auto text = source.trim();
    switch (kind_)
    {
        case Kind::Toggle:
            return text.equalsIgnoreCase ("on")
                || text.equalsIgnoreCase ("true")
                || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
        case Kind::Shape:
            for (int index = 0; index < 10; ++index)
                if (text.equalsIgnoreCase (kShapeNames[index]))
                    return toNormalised (static_cast<float> (index));
            return toNormalised (text.getFloatValue());
        case Kind::Placement:
            for (int index = 0; index < kChannelPlacementCount; ++index)
                if (text.equalsIgnoreCase (channelPlacementName (
                        static_cast<ChannelPlacement> (index))))
                    return toNormalised (static_cast<float> (index));
            return toNormalised (text.getFloatValue());
        case Kind::DetectorChoice:
            return text.containsIgnoreCase ("peak")
                || text.getFloatValue() < 0.5f ? 0.0f : 1.0f;
        case Kind::SourceChoice:
            return text.containsIgnoreCase ("external")
                || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
        case Kind::PhaseModeChoice:
            return text.containsIgnoreCase ("linear")
                || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
        case Kind::CharacterChoice:
            for (int index = 0; index < kCharacterModeCount; ++index)
                if (text.equalsIgnoreCase (kCharacterNames[index]))
                    return toNormalised (static_cast<float> (index));
            return toNormalised (text.getFloatValue());
        case Kind::Frequency:
            return toNormalised (parseFrequency (text));
        case Kind::DesignMode:
            return text.containsIgnoreCase ("analog")
                || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
        default:
            return toNormalised (text.getFloatValue());
    }
}

int ParametricEQParameter::getNumSteps() const
{
    if (kind_ == Kind::Shape)
        return 10;
    if (kind_ == Kind::Placement)
        return kChannelPlacementCount;
    if (kind_ == Kind::CharacterChoice)
        return kCharacterModeCount;
    if (kind_ == Kind::Toggle || kind_ == Kind::DesignMode
        || kind_ == Kind::DetectorChoice || kind_ == Kind::SourceChoice
        || kind_ == Kind::PhaseModeChoice)
        return 2;
    return juce::AudioProcessorParameter::getDefaultNumParameterSteps();
}

bool ParametricEQParameter::isDiscrete() const
{
    return kind_ == Kind::Shape || kind_ == Kind::Placement
        || kind_ == Kind::CharacterChoice
        || kind_ == Kind::Toggle || kind_ == Kind::DesignMode
        || kind_ == Kind::DetectorChoice || kind_ == Kind::SourceChoice
        || kind_ == Kind::PhaseModeChoice;
}

bool ParametricEQParameter::isBoolean() const
{
    return kind_ == Kind::Toggle;
}

juce::String ParametricEQParameter::getLabel() const
{
    switch (kind_)
    {
        case Kind::Frequency: return "Hz";
        case Kind::Gain: return "dB";
        case Kind::Q: return "Q";
        case Kind::Slope: return "dB/oct";
        default: return {};
    }
}

float ParametricEQParameter::getUnitsValue() const noexcept
{
    return fromNormalised (getValue());
}

int ParametricEQParameter::getChoiceIndex() const noexcept
{
    return static_cast<int> (std::lround (getUnitsValue()));
}

float ParametricEQParameter::toNormalised (float units) const noexcept
{
    if (! std::isfinite (units))
        units = defaultUnits_;

    if (kind_ == Kind::Toggle || kind_ == Kind::DesignMode
        || kind_ == Kind::DetectorChoice || kind_ == Kind::SourceChoice
        || kind_ == Kind::PhaseModeChoice)
        return units >= 0.5f ? 1.0f : 0.0f;

    if (kind_ == Kind::Shape || kind_ == Kind::Placement
        || kind_ == Kind::CharacterChoice)
        return std::clamp (std::round (units), minimumUnits_, maximumUnits_)
             / std::max (1.0f, maximumUnits_);

    units = std::clamp (units, minimumUnits_, maximumUnits_);
    if (kind_ == Kind::Frequency || kind_ == Kind::Q
        || kind_ == Kind::TimeSeconds)
    {
        const auto safeMinimum = std::max (minimumUnits_, 1.0e-9f);
        return std::log (units / safeMinimum)
             / std::log (maximumUnits_ / safeMinimum);
    }
    return (units - minimumUnits_) / (maximumUnits_ - minimumUnits_);
}

float ParametricEQParameter::fromNormalised (float normalised) const noexcept
{
    normalised = std::clamp (normalised, 0.0f, 1.0f);
    if (kind_ == Kind::Toggle || kind_ == Kind::DesignMode
        || kind_ == Kind::DetectorChoice || kind_ == Kind::SourceChoice
        || kind_ == Kind::PhaseModeChoice)
        return normalised >= 0.5f ? 1.0f : 0.0f;
    if (kind_ == Kind::Shape || kind_ == Kind::Placement
        || kind_ == Kind::CharacterChoice)
        return static_cast<float> (std::clamp (
            static_cast<int> (std::lround (normalised * maximumUnits_)),
            0, static_cast<int> (maximumUnits_)));
    if (kind_ == Kind::Frequency || kind_ == Kind::Q
        || kind_ == Kind::TimeSeconds)
    {
        const auto safeMinimum = std::max (minimumUnits_, 1.0e-9f);
        return safeMinimum * std::pow (maximumUnits_ / safeMinimum, normalised);
    }
    return minimumUnits_ + normalised * (maximumUnits_ - minimumUnits_);
}

Processor::Processor()
    : juce::AudioPluginInstance (juce::AudioProcessor::BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto add = [this] (int index, ParametricEQParameter::Kind kind,
                       const juce::String& name,
                       float minimum, float maximum, float defaultValue)
    {
        auto parameter = std::make_unique<ParametricEQParameter> (
            juce::ParameterID (getParameterId (index), 1), name, kind,
            minimum, maximum, defaultValue);
        parameters_[static_cast<std::size_t> (index)] = parameter.get();
        addHostedParameter (std::move (parameter));
    };

    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto number = juce::String (band + 1).paddedLeft ('0', 2);
        const auto prefix = "Band " + number + " ";
        add (parameterIndex (band, BandParameterOffset::Enabled),
             ParametricEQParameter::Kind::Toggle, prefix + "Enabled", 0.0f, 1.0f, 0.0f);
        add (parameterIndex (band, BandParameterOffset::Bypass),
             ParametricEQParameter::Kind::Toggle, prefix + "Bypass", 0.0f, 1.0f, 0.0f);
        add (parameterIndex (band, BandParameterOffset::Shape),
             ParametricEQParameter::Kind::Shape, prefix + "Shape", 0.0f, 9.0f, 0.0f);
        add (parameterIndex (band, BandParameterOffset::Frequency),
             ParametricEQParameter::Kind::Frequency, prefix + "Frequency",
             static_cast<float> (kMinimumFrequencyHz),
             static_cast<float> (kMaximumFrequencyHz), 1000.0f);
        add (parameterIndex (band, BandParameterOffset::Gain),
             ParametricEQParameter::Kind::Gain, prefix + "Gain",
             static_cast<float> (kMinimumGainDb),
             static_cast<float> (kMaximumGainDb), 0.0f);
        add (parameterIndex (band, BandParameterOffset::Q),
             ParametricEQParameter::Kind::Q, prefix + "Q",
             static_cast<float> (kMinimumQ),
             static_cast<float> (kMaximumQ), 1.0f);
        add (parameterIndex (band, BandParameterOffset::Slope),
             ParametricEQParameter::Kind::Slope, prefix + "Slope", 0.0f,
             static_cast<float> (kMaximumCutSlopeDbPerOctave), 12.0f);
    }

    add (kDesignModeParameter, ParametricEQParameter::Kind::DesignMode, "Design Mode",
         0.0f, 1.0f, 0.0f);
    add (kGlobalBypassParameter, ParametricEQParameter::Kind::Toggle, "Bypass",
         0.0f, 1.0f, 0.0f);

    // Phase 3: per-band channel placement appends AFTER the frozen Phase 2
    // identity so no existing stable parameter identity is reordered.
    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto number = juce::String (band + 1).paddedLeft ('0', 2);
        add (placementParameterIndex (band), ParametricEQParameter::Kind::Placement,
             "Band " + number + " Placement", 0.0f,
             static_cast<float> (kChannelPlacementCount - 1), 0.0f);
    }

    // Phase 5: per-band Dynamic EQ parameters append AFTER the frozen
    // Phase 3 identity (indices 194-313), followed by three globals.
    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto number = juce::String (band + 1).paddedLeft ('0', 2);
        const auto prefix = "Band " + number + " Dynamic ";
        add (dynamicParameterIndex (band, DynamicBandOffset::Enable),
             ParametricEQParameter::Kind::Toggle, prefix + "Enable",
             0.0f, 1.0f, 0.0f);
        add (dynamicParameterIndex (band, DynamicBandOffset::Threshold),
             ParametricEQParameter::Kind::ThresholdDb, prefix + "Threshold",
             -60.0f, 0.0f, -24.0f);
        add (dynamicParameterIndex (band, DynamicBandOffset::Range),
             ParametricEQParameter::Kind::RangeDb, prefix + "Range",
             -24.0f, 24.0f, 0.0f);
        add (dynamicParameterIndex (band, DynamicBandOffset::Attack),
             ParametricEQParameter::Kind::TimeSeconds, prefix + "Attack",
             0.0005f, 5.0f, 0.010f);
        add (dynamicParameterIndex (band, DynamicBandOffset::Release),
             ParametricEQParameter::Kind::TimeSeconds, prefix + "Release",
             0.001f, 20.0f, 0.100f);
    }
    add (kDynamicDetectorParameter, ParametricEQParameter::Kind::DetectorChoice,
         "Dynamic Detector", 0.0f, 1.0f, 1.0f); // RMS default
    add (kDynamicSidechainParameter, ParametricEQParameter::Kind::SourceChoice,
         "Dynamic Sidechain", 0.0f, 1.0f, 0.0f); // Internal default
    add (kDynamicLinkParameter, ParametricEQParameter::Kind::Toggle,
         "Dynamic Link", 0.0f, 1.0f, 1.0f); // Linked default

    // Phase 6: processing mode (appends after the Phase 5 identity).
    add (kPhaseModeParameter, ParametricEQParameter::Kind::PhaseModeChoice,
         "Phase Mode", 0.0f, 1.0f, 0.0f); // Minimum Phase default

    // Phase 7: APEX-owned character stage.  Append-only ABI: all prior
    // automation IDs and saved states retain their original indices.
    add (kCharacterModeParameter, ParametricEQParameter::Kind::CharacterChoice,
         "Character", 0.0f, static_cast<float> (kCharacterModeCount - 1), 0.0f);

    // Phase 8: optional band-limited detector key for every dynamic band.
    // Defaults OFF for exact migration of all earlier sessions.
    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto number = juce::String (band + 1).paddedLeft ('0', 2);
        add (dynamicFilterParameterIndex (band),
             ParametricEQParameter::Kind::Toggle,
             "Band " + number + " Dynamic SC Filter", 0.0f, 1.0f, 0.0f);
    }

    jassert (getParameters().size() == kNumParameters);
}

void Processor::fillInPluginDescription (
    juce::PluginDescription& description) const
{
    description.name = kPluginName;
    description.descriptiveName = kPluginName;
    description.pluginFormatName = kFormatName;
    description.category = kCategory;
    description.manufacturerName = kManufacturer;
    description.version = kVersion;
    description.fileOrIdentifier = kFileOrIdentifier;
    description.uniqueId = kUniqueId;
    description.deprecatedUid = kUniqueId;
    description.isInstrument = false;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto rate = std::isfinite (sampleRate) && sampleRate > 1.0
                    ? sampleRate : 44100.0;
    const auto block = std::max (1, samplesPerBlock);
    setRateAndBufferSizeDetails (rate, block);

    const auto channels = std::clamp (std::max (1, getMainBusNumInputChannels()),
                                      1, kMaxChannels);
    currentEngine_.prepare (rate, block, channels);
    targetEngine_.prepare (rate, block, channels);
    dryScratch_.setSize (channels, block, false, false, true);
    targetScratch_.setSize (channels, block, false, false, true);
    auditionScratch_.setSize (channels, block, false, false, true);
    dynamicGainScratch_.setSize (kMaxBands, block, false, false, true);
    externalKeyScratch_.setSize (channels, block, false, false, true);
    linearPhaseScratch_.setSize (channels, block, false, false, true);
    kernelNextScratch_.setSize (channels, block, false, false, true);
    externalKeyValid_ = false;
    kernelStep_ = 1.0 / std::max (1.0, rate * 0.010);
    for (auto& convolver : { &convolverCurrentL_, &convolverCurrentR_,
                             &convolverNextL_, &convolverNextR_ })
        convolver->reset();
    currentKernelIndex_ = linearKernelActiveIndex_.load (std::memory_order_acquire);
    linearKernelAdoptedGeneration_ = linearKernelGeneration_.load (
        std::memory_order_acquire);
    kernelTransitionActive_ = false;
    kernelMix_ = 1.0;

    if (kernelWorker_ == nullptr)
        kernelWorker_ = std::make_unique<KernelWorker> (*this);
    if (! kernelWorker_->isThreadRunning())
        kernelWorker_->startThread (juce::Thread::Priority::low);
    const bool linearRequested = parameters_[static_cast<std::size_t> (
        kPhaseModeParameter)]->getBool();
    linearPhaseMode_ = linearRequested;
    linearPhaseTarget_ = linearRequested;
    modeMix_ = linearRequested ? 1.0 : 0.0;
    if (linearRequested)
        requestLinearKernelRebuild();
    setLatencySamples (linearRequested ? kLinearPhaseLatencySamples : 0);
    for (int band = 0; band < kMaxBands; ++band)
    {
        dynamicDetectors_[static_cast<std::size_t> (band)].prepare (rate);
        dynamicEnvelopes_[static_cast<std::size_t> (band)].prepare (
            rate, dynamicParameters_[static_cast<std::size_t> (band)].attackSeconds,
            dynamicParameters_[static_cast<std::size_t> (band)].releaseSeconds);
        dynamicKeyFilterCoefficients_[static_cast<std::size_t> (band)]
            = BiquadCoefficients::identity();
        dynamicKeyFilterFrequencyHz_[static_cast<std::size_t> (band)] = 0.0;
        dynamicKeyFilterQ_[static_cast<std::size_t> (band)] = 0.0;
        for (auto& state : dynamicKeyFilterStates_[static_cast<std::size_t> (band)])
            state.reset();
    }
    adoptDynamicParameters();

    transitionStep_ = 1.0 / std::max (1.0, rate * 0.010);
    bypassStep_ = 1.0 / std::max (1.0, rate * 0.010);
    auditionStep_ = 1.0 / std::max (1.0, rate * 0.010);
    responsePrepareEpoch_ = responseCore_.beginPrepareEpoch();
    prepared_ = true;
    snapToPublishedParameters (true);
    characterCore_.prepare (rate, channels);
    characterCore_.setMode (readCharacterModeParameter());
    analyzer_.prepare (rate);
    analyzer_.signalDiscontinuity();
    // Keep PDC truthful.  adoptPhaseMode() owns subsequent latency changes.
    setLatencySamples (linearRequested ? kLinearPhaseLatencySamples : 0);
}

void Processor::releaseResources()
{
    invalidateAudition (true);
    if (kernelWorker_ != nullptr)
        kernelWorker_->stopThread (500);
    linearKernelRequest_.store (0, std::memory_order_release);
    currentEngine_.reset();
    targetEngine_.reset();
    for (auto& detector : dynamicDetectors_)
        detector.reset();
    for (auto& envelope : dynamicEnvelopes_)
        envelope.reset();
    for (auto& bandStates : dynamicKeyFilterStates_)
        for (auto& state : bandStates)
            state.reset();
    publishedDynamicGainDb_.fill (0.0);
    dynamicSmoothedGainDb_.fill (0.0);
    externalKeyValid_ = false;
    characterCore_.reset();
    analyzer_.signalDiscontinuity();
    prepared_ = false;
}

void Processor::reset()
{
    if (prepared_)
        snapToPublishedParameters (true);
    else
    {
        currentEngine_.reset();
        targetEngine_.reset();
    }
    for (auto& detector : dynamicDetectors_)
        detector.reset();
    for (auto& envelope : dynamicEnvelopes_)
        envelope.reset();
    for (auto& bandStates : dynamicKeyFilterStates_)
        for (auto& state : bandStates)
            state.reset();
    publishedDynamicGainDb_.fill (0.0);
    dynamicSmoothedGainDb_.fill (0.0);
    characterCore_.reset();
    characterCore_.setMode (readCharacterModeParameter());
    for (auto& convolver : { &convolverCurrentL_, &convolverCurrentR_,
                             &convolverNextL_, &convolverNextR_ })
        convolver->reset();
    kernelTransitionActive_ = false;
    kernelMix_ = 1.0;
    currentKernelIndex_ = linearKernelActiveIndex_.load (std::memory_order_acquire);
    linearKernelAdoptedGeneration_ = linearKernelGeneration_.load (
        std::memory_order_acquire);
    invalidateAudition (true);
    analyzer_.signalDiscontinuity();
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int channels = std::min ({ buffer.getNumChannels(),
                                     getMainBusNumOutputChannels(),
                                     kMaxChannels });
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    analyzer_.pushPre (buffer, channels, samples);

    const bool bypassTarget = readBypassParameter();
    adoptPublishedParameters (bypassTarget);
    adoptDynamicParameters();
    adoptPhaseMode();
    characterCore_.setMode (readCharacterModeParameter());
    adoptAuditionCommand();

    if (bypassTarget && auditionHold_)
        auditionHold_ = false;

    if (bypassTarget && bypassMix_ >= 1.0 && ! transitionActive_)
    {
        if (! wetStateResetForBypass_)
        {
            currentEngine_.reset();
            targetEngine_ = currentEngine_;
            wetStateResetForBypass_ = true;
        }
        if (auditionMix_ > 0.0 || auditionReady_)
        {
            auditionMix_ = 0.0;
            auditionReady_ = false;
            auditionBand_.reset();
            auditionBandIndex_ = -1;
        }
        analyzer_.pushPost (buffer, channels, samples);
        if (responseDirty_)
            publishResponseFrame();
        return;
    }

    if (! bypassTarget)
        wetStateResetForBypass_ = false;

    if (! transitionActive_ && bypassMix_ <= 0.0 && ! bypassTarget
        && ! (auditionReady_ && (auditionHold_ || auditionMix_ > 0.0))
        && ! dynamicAnyActive_ && ! linearPhaseTarget_ && modeMix_ <= 0.0)
    {
        currentEngine_.process (buffer.getArrayOfWritePointers(), channels,
                                samples);
        characterCore_.process (buffer.getArrayOfWritePointers(), channels,
                                samples);
    }
    else
    {
        const int capacity = std::max (1, dryScratch_.getNumSamples());
        int offset = 0;
        while (offset < samples)
        {
            const int count = std::min (capacity, samples - offset);
            processChunk (buffer, channels, offset, count, bypassTarget);
            offset += count;
        }
    }

    if (bypassTarget && bypassMix_ >= 1.0 && ! wetStateResetForBypass_)
    {
        currentEngine_.reset();
        targetEngine_ = currentEngine_;
        wetStateResetForBypass_ = true;
    }

    analyzer_.pushPost (buffer, channels, samples);
    if (responseDirty_)
        publishResponseFrame();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    const bool inputSupported = input == juce::AudioChannelSet::mono()
                             || input == juce::AudioChannelSet::stereo();
    const bool outputSupported = output == juce::AudioChannelSet::mono()
                              || output == juce::AudioChannelSet::stereo();
    return inputSupported && outputSupported && input == output;
}

juce::AudioProcessorParameter* Processor::getBypassParameter() const
{
    return parameters_[static_cast<std::size_t> (kGlobalBypassParameter)];
}

void Processor::getStateInformation (juce::MemoryBlock& destinationData)
{
    juce::ValueTree state (kStateTag);
    state.setProperty (kVersionProperty, kStateVersion, nullptr);
    for (int index = 0; index < kNumParameters; ++index)
    {
        const auto* parameter = parameters_[static_cast<std::size_t> (index)];
        state.setProperty (juce::Identifier (getParameterId (index)),
                           parameter->getValue(), nullptr);
    }
    state.setProperty (kAnalyzerProperty,
                       static_cast<int> (getAnalyzerMode()), nullptr);

    juce::MemoryOutputStream stream (destinationData, false);
    state.writeToStream (stream);
}

void Processor::setStateInformation (const void* data, int sizeInBytes)
{
    invalidateAudition (true);
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto state = juce::ValueTree::readFromData (
        data, static_cast<std::size_t> (sizeInBytes));
    if (! state.isValid() || ! state.hasType (juce::Identifier (kStateTag)))
        return;

    for (int index = 0; index < kNumParameters; ++index)
    {
        const juce::Identifier property (getParameterId (index));
        if (! state.hasProperty (property))
            continue;
        const auto value = static_cast<float> (state.getProperty (property));
        if (std::isfinite (value))
            parameters_[static_cast<std::size_t> (index)]->setValue (value);
    }

    if (state.hasProperty (kAnalyzerProperty))
    {
        const auto mode = std::clamp (
            static_cast<int> (state.getProperty (kAnalyzerProperty)), 0, 3);
        setAnalyzerMode (static_cast<Analysis::SpectrumTapMode> (mode));
    }
}

ParametricEQParameter* Processor::getParametricEQParameter (int index) noexcept
{
    return index >= 0 && index < kNumParameters
         ? parameters_[static_cast<std::size_t> (index)] : nullptr;
}

Processor::~Processor()
{
    if (kernelWorker_ != nullptr)
        kernelWorker_->stopThread (500);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ParametricEQEditor (*this);
}

const ParametricEQParameter* Processor::getParametricEQParameter (int index) const noexcept
{
    return index >= 0 && index < kNumParameters
         ? parameters_[static_cast<std::size_t> (index)] : nullptr;
}

juce::String Processor::getParameterId (int index)
{
    if (index == kDesignModeParameter)
        return "peq.design";
    if (index == kGlobalBypassParameter)
        return "peq.bypass";
    if (index >= kFirstPlacementParameter && index < kFirstDynamicParameter)
        return "peq.band"
             + juce::String (index - kFirstPlacementParameter + 1).paddedLeft ('0', 2)
             + ".placement";
    if (index >= kFirstDynamicParameter && index < kDynamicDetectorParameter)
    {
        const int local = index - kFirstDynamicParameter;
        const int band = local / kDynamicParametersPerBand;
        const int offset = local % kDynamicParametersPerBand;
        static const char* suffixes[] = {
            "enable", "threshold", "range", "attack", "release"
        };
        return "peq.band" + juce::String (band + 1).paddedLeft ('0', 2)
             + ".dyn." + suffixes[offset];
    }
    if (index == kDynamicDetectorParameter)
        return "peq.dyn.detector";
    if (index == kDynamicSidechainParameter)
        return "peq.dyn.sidechain";
    if (index == kDynamicLinkParameter)
        return "peq.dyn.link";
    if (index == kPhaseModeParameter)
        return "peq.phase";
    if (index == kCharacterModeParameter)
        return "peq.character";
    if (index >= kFirstDynamicFilterParameter
        && index < kFirstDynamicFilterParameter + kDynamicFilterParameterCount)
        return "peq.band"
             + juce::String (index - kFirstDynamicFilterParameter + 1)
                   .paddedLeft ('0', 2)
             + ".dyn.filter";
    if (index < 0 || index >= kBandParameterCount)
        return {};

    const int band = index / kParametersPerBand;
    const int offset = index % kParametersPerBand;
    return "peq.band"
         + juce::String (band + 1).paddedLeft ('0', 2)
         + "." + kBandParameterSuffixes[offset];
}

void Processor::setAnalyzerMode (Analysis::SpectrumTapMode mode) noexcept
{
    analyzer_.setTapMode (mode);
}

Analysis::SpectrumTapMode Processor::getAnalyzerMode() const noexcept
{
    return analyzer_.getTapMode();
}

bool Processor::settingsEqual (const BandSettings& lhs,
                               const BandSettings& rhs) noexcept
{
    return lhs.enabled == rhs.enabled
        && lhs.bypassed == rhs.bypassed
        && lhs.shape == rhs.shape
        && lhs.placement == rhs.placement
        && lhs.frequencyHz == rhs.frequencyHz
        && lhs.gainDb == rhs.gainDb
        && lhs.q == rhs.q
        && lhs.slopeDbPerOctave == rhs.slopeDbPerOctave;
}

bool Processor::placementChanged (const BandSettings& lhs,
                                  const BandSettings& rhs) noexcept
{
    return lhs.placement != rhs.placement;
}

void Processor::readBandPlacementParameters (
    std::array<BandSettings, kMaxBands>& settings) const noexcept
{
    for (int band = 0; band < kMaxBands; ++band)
    {
        const auto* parameter = parameters_[static_cast<std::size_t> (
            placementParameterIndex (band))];
        settings[static_cast<std::size_t> (band)].placement
            = static_cast<ChannelPlacement> (std::clamp (
                parameter->getChoiceIndex(), 0, kChannelPlacementCount - 1));
    }
}

BandSettings Processor::readBandParameters (int band) const noexcept
{
    BandSettings settings;
    settings.enabled = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Enabled))]->getBool();
    settings.bypassed = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Bypass))]->getBool();
    settings.shape = static_cast<FilterShape> (std::clamp (
        parameters_[static_cast<std::size_t> (parameterIndex (
            band, BandParameterOffset::Shape))]->getChoiceIndex(), 0, 9));
    settings.placement = static_cast<ChannelPlacement> (std::clamp (
        parameters_[static_cast<std::size_t> (placementParameterIndex (band))]
            ->getChoiceIndex(), 0, kChannelPlacementCount - 1));
    settings.frequencyHz = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Frequency))]->getUnitsValue();
    settings.gainDb = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Gain))]->getUnitsValue();
    settings.q = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Q))]->getUnitsValue();
    settings.slopeDbPerOctave = parameters_[static_cast<std::size_t> (parameterIndex (
        band, BandParameterOffset::Slope))]->getUnitsValue();
    return sanitise (settings, getSampleRate());
}

DesignMode Processor::readDesignModeParameter() const noexcept
{
    return parameters_[static_cast<std::size_t> (kDesignModeParameter)]->getBool()
         ? DesignMode::AnalogMatched : DesignMode::Realtime;
}

CharacterMode Processor::readCharacterModeParameter() const noexcept
{
    const int index = std::clamp (
        parameters_[static_cast<std::size_t> (kCharacterModeParameter)]
            ->getChoiceIndex(),
        0, kCharacterModeCount - 1);
    return static_cast<CharacterMode> (index);
}

bool Processor::readBypassParameter() const noexcept
{
    return parameters_[static_cast<std::size_t> (kGlobalBypassParameter)]->getBool();
}

void Processor::configureEngine (
    Engine& engine, const std::array<BandSettings, kMaxBands>& settings,
    DesignMode mode, bool resetState) noexcept
{
    for (int band = 0; band < kMaxBands; ++band)
        engine.setBand (band, settings[static_cast<std::size_t> (band)],
                        mode, resetState);
}

void Processor::snapToPublishedParameters (bool resetState) noexcept
{
    for (int band = 0; band < kMaxBands; ++band)
        currentSettings_[static_cast<std::size_t> (band)]
            = readBandParameters (band);
    currentMode_ = readDesignModeParameter();
    configureEngine (currentEngine_, currentSettings_, currentMode_, resetState);
    targetEngine_ = currentEngine_;
    targetSettings_ = currentSettings_;
    targetMode_ = currentMode_;
    transitionMix_ = 0.0;
    transitionActive_ = false;
    pendingPlacementBands_ = 0;
    bypassMix_ = readBypassParameter() ? 1.0 : 0.0;
    wetStateResetForBypass_ = bypassMix_ >= 1.0;
    if (wetStateResetForBypass_)
    {
        currentEngine_.reset();
        targetEngine_ = currentEngine_;
    }
    adoptDynamicParameters();
    publishedDynamicGainDb_.fill (0.0);
    dynamicSmoothedGainDb_.fill (0.0);
    invalidateAudition (true);
    responseDirty_ = true;
}

bool Processor::adoptPublishedParameters (bool bypassTarget) noexcept
{
    std::array<BandSettings, kMaxBands> latest {};
    bool changed = false;
    for (int band = 0; band < kMaxBands; ++band)
    {
        latest[static_cast<std::size_t> (band)] = readBandParameters (band);
        changed |= ! settingsEqual (latest[static_cast<std::size_t> (band)],
                                    targetSettings_[static_cast<std::size_t> (band)]);
    }
    const auto latestMode = readDesignModeParameter();
    changed |= latestMode != targetMode_;

    if (auditionReady_ && auditionHold_ && auditionBandIndex_ >= 0
        && auditionBandIndex_ < kMaxBands)
    {
        const auto& audible = auditionBand_.settings;
        const auto& fresh = latest[static_cast<std::size_t> (auditionBandIndex_)];
        if (audible.enabled != fresh.enabled
            || audible.bypassed != fresh.bypassed
            || audible.shape != fresh.shape
            || audible.placement != fresh.placement)
        {
            // Domain or identity change: cancel cleanly; a fresh press is
            // required to audition the new configuration.
            auditionHold_ = false;
        }
        else if (audible.frequencyHz != fresh.frequencyHz
                 || audible.q != fresh.q
                 || audible.slopeDbPerOctave != fresh.slopeDbPerOctave)
        {
            // Domain-stable coefficient edits retarget the audition lane
            // without resetting its history.
            auditionBand_.configure (fresh, latestMode, getSampleRate(), false);
        }
    }

    if (! changed)
        return false;

    // While fully bypassed, target changes are inaudible. Adopt them directly
    // with cleared histories instead of spending callback time crossfading a
    // hidden path. Un-bypass will warm the wet path under its own dry crossfade.
    if (bypassTarget && bypassMix_ >= 1.0)
    {
        currentSettings_ = latest;
        targetSettings_ = latest;
        currentMode_ = latestMode;
        targetMode_ = latestMode;
        configureEngine (currentEngine_, currentSettings_, currentMode_, true);
        targetEngine_ = currentEngine_;
        transitionMix_ = 0.0;
        transitionActive_ = false;
        pendingPlacementBands_ = 0;
        wetStateResetForBypass_ = true;
        responseDirty_ = true;
        return true;
    }

    const bool modeChanged = latestMode != targetMode_;
    std::uint32_t deferredMask = 0;

    if (! transitionActive_)
    {
        targetEngine_ = currentEngine_;
        targetSettings_ = currentSettings_;
        targetMode_ = currentMode_;
        transitionMix_ = 0.0;
        transitionActive_ = true;
    }
    else
    {
        // An audible transition cannot reset a band's state in place.
        // Placement changes switch the effective state domain, so they are
        // deferred until the current crossfade completes.
        for (int band = 0; band < kMaxBands; ++band)
            if (placementChanged (targetEngine_.getBand (band),
                                  latest[static_cast<std::size_t> (band)]))
                deferredMask |= 1u << band;
    }

    targetSettings_ = latest;
    targetMode_ = latestMode;
    if (modeChanged)
    {
        configureEngine (targetEngine_, targetSettings_, targetMode_, false);
    }
    else
    {
        for (int band = 0; band < kMaxBands; ++band)
        {
            if ((deferredMask & (1u << band)) != 0)
                continue;
            if (! settingsEqual (targetEngine_.getBand (band),
                                 targetSettings_[static_cast<std::size_t> (band)]))
                targetEngine_.setBand (
                    band, targetSettings_[static_cast<std::size_t> (band)],
                    targetMode_, false);
        }
    }
    pendingPlacementBands_ |= deferredMask;
    responseDirty_ = true;
    if (linearPhaseMode_)
        requestLinearKernelRebuild();
    return true;
}

void Processor::processChunk (juce::AudioBuffer<float>& buffer,
                              int numberOfChannels, int offset,
                              int numberOfSamples, bool bypassTarget) noexcept
{
    jassert (numberOfSamples <= dryScratch_.getNumSamples());
    jassert (numberOfSamples <= targetScratch_.getNumSamples());

    juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                    numberOfChannels, offset, numberOfSamples);
    for (int channel = 0; channel < numberOfChannels; ++channel)
        dryScratch_.copyFrom (channel, 0, chunk, channel, 0, numberOfSamples);

    computeDynamicGains (numberOfChannels, numberOfSamples);
    const bool applyDynamics = dynamicAnyActive_
        && dynamicGainScratch_.getNumSamples() >= numberOfSamples;
    const auto dynamicGainChannels = dynamicGainScratch_.getArrayOfWritePointers();

    if (transitionActive_)
    {
        for (int channel = 0; channel < numberOfChannels; ++channel)
            targetScratch_.copyFrom (channel, 0, dryScratch_, channel, 0,
                                     numberOfSamples);

        currentEngine_.processWithDynamicGains (chunk.getArrayOfWritePointers(),
                                                numberOfChannels, numberOfSamples,
                                                applyDynamics ? dynamicActiveMask_ : 0,
                                                applyDynamics ? dynamicGainChannels : nullptr);
        targetEngine_.processWithDynamicGains (targetScratch_.getArrayOfWritePointers(),
                                               numberOfChannels, numberOfSamples,
                                               applyDynamics ? dynamicActiveMask_ : 0,
                                               applyDynamics ? dynamicGainChannels : nullptr);

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const auto mix = static_cast<float> (transitionMix_);
            for (int channel = 0; channel < numberOfChannels; ++channel)
            {
                auto* destination = chunk.getWritePointer (channel);
                const auto* target = targetScratch_.getReadPointer (channel);
                if (transitionMix_ >= 1.0)
                    destination[sample] = target[sample];
                else if (transitionMix_ > 0.0)
                    destination[sample] += mix * (target[sample] - destination[sample]);
            }
            transitionMix_ = std::min (1.0, transitionMix_ + transitionStep_);
        }
        responseDirty_ = true;
        completeTransitionIfReady();
    }
    else
    {
        currentEngine_.processWithDynamicGains (chunk.getArrayOfWritePointers(),
                                                numberOfChannels, numberOfSamples,
                                                applyDynamics ? dynamicActiveMask_ : 0,
                                                applyDynamics ? dynamicGainChannels : nullptr);
    }

    if (auditionReady_ && (auditionHold_ || auditionMix_ > 0.0))
        processAudition (chunk, numberOfChannels, numberOfSamples);

    // Linear-phase mixing: the linear path runs from the moment the target
    // mode is requested (at mix 0 it contributes nothing) and ramps once a
    // kernel generation exists. Adoption and the old/new kernel crossfade
    // happen inside processLinearPhase, so the readiness test must not
    // require adoption (which would deadlock the first generation).
    const bool kernelReady = linearKernelGeneration_.load (
        std::memory_order_acquire) != 0;
    if ((linearPhaseTarget_ || modeMix_ > 0.0) && kernelReady)
    {
        for (int channel = 0; channel < numberOfChannels; ++channel)
            linearPhaseScratch_.copyFrom (channel, 0, dryScratch_, channel, 0,
                                          numberOfSamples);
        processLinearPhase (linearPhaseScratch_, numberOfChannels,
                            numberOfSamples);
        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const auto mix = static_cast<float> (modeMix_);
            for (int channel = 0; channel < numberOfChannels; ++channel)
            {
                auto* destination = chunk.getWritePointer (channel);
                const auto* linear = linearPhaseScratch_.getReadPointer (channel);
                if (modeMix_ >= 1.0)
                    destination[sample] = linear[sample];
                else if (modeMix_ > 0.0)
                    destination[sample] += mix * (linear[sample] - destination[sample]);
            }
            modeMix_ = linearPhaseTarget_
                     ? std::min (1.0, modeMix_ + modeStep_)
                     : std::max (0.0, modeMix_ - modeStep_);
        }
        responseDirty_ = true;
    }

    // Character is part of the wet path, before the global bypass blend.
    characterCore_.process (chunk.getArrayOfWritePointers(),
                            numberOfChannels, numberOfSamples);

    if (bypassMix_ > 0.0 || bypassTarget)
    {
        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const auto mix = static_cast<float> (bypassMix_);
            for (int channel = 0; channel < numberOfChannels; ++channel)
            {
                auto* destination = chunk.getWritePointer (channel);
                const auto* dry = dryScratch_.getReadPointer (channel);
                if (bypassMix_ >= 1.0)
                    destination[sample] = dry[sample];
                else if (bypassMix_ > 0.0)
                    destination[sample] += mix * (dry[sample] - destination[sample]);
            }
            bypassMix_ = bypassTarget
                       ? std::min (1.0, bypassMix_ + bypassStep_)
                       : std::max (0.0, bypassMix_ - bypassStep_);
        }
        responseDirty_ = true;
    }
}

void Processor::completeTransitionIfReady() noexcept
{
    if (! transitionActive_ || transitionMix_ < 1.0)
        return;
    currentEngine_ = targetEngine_;
    currentSettings_ = targetSettings_;
    currentMode_ = targetMode_;
    transitionMix_ = 0.0;
    transitionActive_ = false;

    // Deferred placement changes were frozen out of the audible target while
    // the previous crossfade ran. Apply them now with a deliberate state
    // reset and begin a fresh inaudible-at-zero crossfade.
    if (pendingPlacementBands_ != 0)
    {
        for (int band = 0; band < kMaxBands; ++band)
            if ((pendingPlacementBands_ & (1u << band)) != 0)
                targetEngine_.setBand (
                    band, targetSettings_[static_cast<std::size_t> (band)],
                    targetMode_, true);
        pendingPlacementBands_ = 0;
        transitionActive_ = true;
    }
    responseDirty_ = true;
}

void Processor::adoptDynamicParameters() noexcept
{
    dynamicDetectorMode_ = parameters_[static_cast<std::size_t> (
        kDynamicDetectorParameter)]->getBool()
                         ? APEX::Dynamics::DetectorMode::Rms
                         : APEX::Dynamics::DetectorMode::Peak;
    dynamicLink_ = parameters_[static_cast<std::size_t> (
        kDynamicLinkParameter)]->getBool();
    dynamicSource_ = parameters_[static_cast<std::size_t> (
        kDynamicSidechainParameter)]->getBool()
                   ? DetectorSource::External : DetectorSource::Internal;

    std::uint32_t mask = 0;
    const auto previousMask = dynamicActiveMask_;
    for (int band = 0; band < kMaxBands; ++band)
    {
        DynamicBandParameters next;
        next.enabled = parameters_[static_cast<std::size_t> (dynamicParameterIndex (
            band, DynamicBandOffset::Enable))]->getBool();
        next.thresholdDb = static_cast<double> (parameters_[
            static_cast<std::size_t> (dynamicParameterIndex (
                band, DynamicBandOffset::Threshold))]->getUnitsValue());
        next.rangeDb = static_cast<double> (parameters_[
            static_cast<std::size_t> (dynamicParameterIndex (
                band, DynamicBandOffset::Range))]->getUnitsValue());
        next.attackSeconds = static_cast<double> (parameters_[
            static_cast<std::size_t> (dynamicParameterIndex (
                band, DynamicBandOffset::Attack))]->getUnitsValue());
        next.releaseSeconds = static_cast<double> (parameters_[
            static_cast<std::size_t> (dynamicParameterIndex (
                band, DynamicBandOffset::Release))]->getUnitsValue());
        next.sidechainFilter = parameters_[static_cast<std::size_t> (
            dynamicFilterParameterIndex (band))]->getBool();
        next = DynamicBandParameters::sanitised (next);

        const auto& previous = dynamicParameters_[static_cast<std::size_t> (band)];
        const auto& detectorBand = targetSettings_[static_cast<std::size_t> (band)];
        const bool detectorBandChanged
            = dynamicKeyFilterFrequencyHz_[static_cast<std::size_t> (band)]
                  != detectorBand.frequencyHz
           || dynamicKeyFilterQ_[static_cast<std::size_t> (band)]
                  != detectorBand.q;
        if (next.sidechainFilter && (detectorBandChanged
                                     || ! previous.sidechainFilter))
        {
            dynamicKeyFilterCoefficients_[static_cast<std::size_t> (band)]
                = FilterDesigner::designRbj (
                    FilterShape::BandPass, detectorBand.frequencyHz,
                    detectorBand.q, 0.0, getSampleRate());
            dynamicKeyFilterFrequencyHz_[static_cast<std::size_t> (band)]
                = detectorBand.frequencyHz;
            dynamicKeyFilterQ_[static_cast<std::size_t> (band)]
                = detectorBand.q;
            for (auto& state : dynamicKeyFilterStates_[
                    static_cast<std::size_t> (band)])
                state.reset();
        }
        else if (! next.sidechainFilter && previous.sidechainFilter)
        {
            for (auto& state : dynamicKeyFilterStates_[
                    static_cast<std::size_t> (band)])
                state.reset();
        }
        const bool compatible = next.enabled
            && isDynamicCompatibleShape (targetSettings_[
                static_cast<std::size_t> (band)].shape);
        if (compatible)
            mask |= 1u << band;
        else
            publishedDynamicGainDb_[static_cast<std::size_t> (band)] = 0.0;

        if (next.enabled != previous.enabled)
        {
            // Engagement edge: begin/end from a known detector and envelope
            // state (no stale reduction after re-enable).
            dynamicDetectors_[static_cast<std::size_t> (band)].reset();
            dynamicEnvelopes_[static_cast<std::size_t> (band)].reset();
            dynamicSmoothedGainDb_[static_cast<std::size_t> (band)] = 0.0;
        }
        if (next.attackSeconds != previous.attackSeconds
            || next.releaseSeconds != previous.releaseSeconds
            || ! prepared_)
        {
            // Timing retargets adopt without resetting ballistics so active
            // reduction stays click-free.
            dynamicEnvelopes_[static_cast<std::size_t> (band)].prepare (
                getSampleRate(), next.attackSeconds, next.releaseSeconds);
        }

        dynamicParameters_[static_cast<std::size_t> (band)] = next;
    }
    dynamicActiveMask_ = mask;
    dynamicAnyActive_ = mask != 0;
    if (mask != previousMask)
        responseDirty_ = true; // activation/deactivation must republish
}

void Processor::computeDynamicGains (int numberOfChannels,
                                     int numberOfSamples) noexcept
{
    // Dynamic EQ is a minimum-phase feature in Phase 6 (documented): in
    // settled linear phase the dynamic gains are suppressed and the
    // published state reports zeros.
    if (linearPhaseMode_ && modeMix_ >= 1.0)
    {
        publishedDynamicGainDb_.fill (0.0);
        responseDirty_ = true;
        return;
    }
    if (! dynamicAnyActive_ || numberOfSamples <= 0)
        return;

    const bool stereo = numberOfChannels >= 2;
    const auto* left = dryScratch_.getReadPointer (0);
    const auto* right = stereo ? dryScratch_.getReadPointer (1) : left;
    const bool useExternalKey = dynamicSource_ == DetectorSource::External
                                && externalKeyValid_;
    const bool externalUnavailable = dynamicSource_ == DetectorSource::External
                                     && ! externalKeyValid_;
    const auto* keyLeft = useExternalKey
                        ? externalKeyScratch_.getReadPointer (0) : left;
    const auto* keyRight = useExternalKey
                         ? (stereo ? externalKeyScratch_.getReadPointer (1)
                                   : externalKeyScratch_.getReadPointer (0))
                         : right;

    for (int band = 0; band < kMaxBands; ++band)
    {
        if ((dynamicActiveMask_ & (1u << band)) == 0)
            continue;
        const auto& parameters = dynamicParameters_[static_cast<std::size_t> (band)];
        const auto placement = targetSettings_[static_cast<std::size_t> (band)].placement;
        auto& detector = dynamicDetectors_[static_cast<std::size_t> (band)];
        auto& envelope = dynamicEnvelopes_[static_cast<std::size_t> (band)];
        auto* gains = dynamicGainScratch_.getWritePointer (band);
        auto& keyStates = dynamicKeyFilterStates_[static_cast<std::size_t> (band)];
        const auto& keyCoefficients
            = dynamicKeyFilterCoefficients_[static_cast<std::size_t> (band)];

        if (externalUnavailable && parameters.sidechainFilter)
            for (auto& state : keyStates)
                state.reset();

        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            // An external source with no submitted key must see silence, not
            // the program material.
            const double keyL = externalUnavailable ? 0.0
                              : (std::isfinite (keyLeft[sample])
                                 ? static_cast<double> (keyLeft[sample]) : 0.0);
            const double keyR = externalUnavailable ? 0.0
                              : (std::isfinite (keyRight[sample])
                                 ? static_cast<double> (keyRight[sample]) : 0.0);
            const double detectorL = parameters.sidechainFilter
                                     && ! externalUnavailable
                ? static_cast<double> (keyStates[0].process (
                    static_cast<float> (keyL), keyCoefficients))
                : keyL;
            const double detectorR = parameters.sidechainFilter
                                     && ! externalUnavailable
                ? static_cast<double> (keyStates[1].process (
                    static_cast<float> (keyR), keyCoefficients))
                : keyR;
            double levelDb = -144.0;
            switch (placement)
            {
                case ChannelPlacement::Stereo:
                    levelDb = detector.processStereoFrame (detectorL, detectorR,
                                                           dynamicDetectorMode_,
                                                           dynamicLink_);
                    break;
                case ChannelPlacement::Left:
                    levelDb = detector.processMono (detectorL, dynamicDetectorMode_);
                    break;
                case ChannelPlacement::Right:
                    levelDb = detector.processMono (detectorR, dynamicDetectorMode_);
                    break;
                case ChannelPlacement::Mid:
                {
                    double mid = 0.0;
                    double side = 0.0;
                    encodeMidSide (detectorL, detectorR, mid, side);
                    levelDb = detector.processMono (mid, dynamicDetectorMode_);
                    break;
                }
                case ChannelPlacement::Side:
                {
                    double mid = 0.0;
                    double side = 0.0;
                    encodeMidSide (detectorL, detectorR, mid, side);
                    levelDb = detector.processMono (side, dynamicDetectorMode_);
                    break;
                }
            }

            // Signed product-gain smoothing using the reusable envelope's
            // exact attack/release coefficients. The state is signed; the
            // direction rule compares magnitudes (|target| vs |state|), so a
            // target of the opposite sign moves the state THROUGH zero
            // continuously. Range sign crossings therefore cannot jump.
            const auto target = dynamicGainDbForLevel (levelDb, parameters);
            auto state = dynamicSmoothedGainDb_[static_cast<std::size_t> (band)];
            const bool engaging = std::abs (target) > std::abs (state);
            const auto coefficient = engaging ? envelope.attackCoefficient()
                                              : envelope.releaseCoefficient();
            auto next = coefficient * state + (1.0 - coefficient) * target;
            if (next == state) // float stagnation guard (C4-L2 law)
                next = target;
            state = std::isfinite (next) ? next : target;
            dynamicSmoothedGainDb_[static_cast<std::size_t> (band)] = state;

            gains[sample] = std::isfinite (state)
                          ? static_cast<float> (std::pow (10.0, state / 20.0))
                          : 1.0f;
            publishedDynamicGainDb_[static_cast<std::size_t> (band)]
                = std::isfinite (state) ? state : 0.0;
        }
    }
    responseDirty_ = true;
}

bool Processor::submitExternalDetectorKey (
    const juce::AudioBuffer<float>& key) noexcept
{
    if (! prepared_)
        return false;
    const int channels = std::min (key.getNumChannels(),
                                   externalKeyScratch_.getNumChannels());
    if (channels <= 0)
        return false;
    const int samples = std::min (key.getNumSamples(),
                                  externalKeyScratch_.getNumSamples());
    for (int channel = 0; channel < channels; ++channel)
        externalKeyScratch_.copyFrom (channel, 0, key, channel, 0, samples);
    for (int sample = samples; sample < externalKeyScratch_.getNumSamples(); ++sample)
        for (int channel = 0; channel < externalKeyScratch_.getNumChannels(); ++channel)
            externalKeyScratch_.setSample (channel, sample, 0.0f);
    externalKeyValid_ = true;
    return true;
}

void Processor::clearExternalDetectorKey() noexcept
{
    externalKeyValid_ = false;
}

double Processor::getDynamicGainDbForTesting (int band) const noexcept
{
    if (band < 0 || band >= kMaxBands)
        return 0.0;
    return publishedDynamicGainDb_[static_cast<std::size_t> (band)];
}

bool Processor::beginAudition (int band, std::uint32_t token) noexcept
{
    if (band < 0 || band >= kMaxBands || ! prepared_)
        return false;
    // Audition operates on the minimum-phase band filter; it is unavailable
    // in settled linear-phase mode (documented Phase 6 limitation).
    if (linearPhaseMode_ && modeMix_ >= 1.0)
        return false;
    const auto sequence = auditionSequence_.fetch_add (1, std::memory_order_acq_rel) + 1;
    auditionMailbox_.store (AuditionCommand::begin (band, token, sequence).bits,
                            std::memory_order_release);
    return true;
}

void Processor::endAudition (std::uint32_t token) noexcept
{
    auditionMailbox_.store (AuditionCommand::end (token).bits,
                            std::memory_order_release);
}

void Processor::cancelAudition() noexcept
{
    // Realtime-safe cancellation: bump the epoch and clear the mailbox. The
    // audio thread adopts the invalidation at its next block boundary.
    auditionEpoch_.fetch_add (1, std::memory_order_acq_rel);
    auditionMailbox_.store (0, std::memory_order_release);
}

void Processor::invalidateAudition (bool incrementEpoch) noexcept
{
    // Control-plane lifecycle points only; audio must be quiescent.
    if (incrementEpoch)
        auditionEpoch_.fetch_add (1, std::memory_order_acq_rel);
    auditionMailbox_.store (0, std::memory_order_release);
    auditionHold_ = false;
    auditionMix_ = 0.0;
    auditionReady_ = false;
    auditionToken_ = 0;
    auditionBand_.reset();
    auditionBandIndex_ = -1;
}

void Processor::adoptAuditionCommand() noexcept
{
    const auto epoch = auditionEpoch_.load (std::memory_order_acquire);
    if (epoch != auditionObservedEpoch_)
    {
        auditionObservedEpoch_ = epoch;
        auditionHold_ = false;
        auditionMix_ = 0.0;
        auditionReady_ = false;
        auditionToken_ = 0;
        auditionBand_.reset();
        auditionBandIndex_ = -1;
    }

    const auto word = auditionMailbox_.exchange (0, std::memory_order_acq_rel);
    if (word == 0)
        return;
    const AuditionCommand command { word };

    if (command.isActive())
    {
        const int band = command.band();
        if (band < 0 || band >= kMaxBands || ! prepared_
            || command.sequence() == auditionLastSequence_)
            return;
        auditionLastSequence_ = command.sequence();

        const auto settings = readBandParameters (band);
        if (! settings.enabled || settings.bypassed)
        {
            auditionHold_ = false;
            return;
        }
        auditionBand_.configure (settings, readDesignModeParameter(),
                                 getSampleRate(), true);
        auditionToken_ = command.token();
        auditionBandIndex_ = band;
        auditionHold_ = true;
        auditionReady_ = true;
    }
    else if (auditionReady_ && command.token() == auditionToken_)
    {
        // Matching token: freeze the configuration and fade the audition
        // contribution out to exact zero.
        auditionHold_ = false;
    }
}

void Processor::processAudition (juce::AudioBuffer<float>& buffer,
                                 int numberOfChannels,
                                 int numberOfSamples) noexcept
{
    if (! auditionReady_ || (! auditionHold_ && auditionMix_ <= 0.0))
        return;

    const int channels = std::min (numberOfChannels,
                                   auditionScratch_.getNumChannels());
    for (int channel = 0; channel < channels; ++channel)
        auditionScratch_.clear (channel, 0, numberOfSamples);

    auto* scratchLeft = auditionScratch_.getWritePointer (0);
    auto* scratchRight = channels >= 2
                       ? auditionScratch_.getWritePointer (1) : scratchLeft;
    // Audition is order-independent by design: its region is derived from the
    // plugin's unprocessed input for this chunk, never from an interior point
    // of the band cascade.
    const auto* left = dryScratch_.getReadPointer (0);
    const auto* right = channels >= 2
                      ? dryScratch_.getReadPointer (1) : left;

    for (int sample = 0; sample < numberOfSamples; ++sample)
    {
        const double leftInput = std::isfinite (left[sample])
                               ? static_cast<double> (left[sample]) : 0.0;
        const double rightInput = std::isfinite (right[sample])
                                ? static_cast<double> (right[sample]) : 0.0;

        float regionL = 0.0f;
        float regionR = 0.0f;
        switch (auditionBand_.placement)
        {
            case ChannelPlacement::Stereo:
                regionL = auditionBand_.regionFor (
                    auditionBand_.filterSample (0, static_cast<float> (leftInput)),
                    static_cast<float> (leftInput));
                if (channels >= 2)
                    regionR = auditionBand_.regionFor (
                        auditionBand_.filterSample (
                            1, static_cast<float> (rightInput)),
                        static_cast<float> (rightInput));
                else
                    regionR = regionL;
                break;
            case ChannelPlacement::Left:
                regionL = auditionBand_.regionFor (
                    auditionBand_.filterSample (0, static_cast<float> (leftInput)),
                    static_cast<float> (leftInput));
                regionR = 0.0f;
                break;
            case ChannelPlacement::Right:
                regionL = 0.0f;
                if (channels >= 2)
                    regionR = auditionBand_.regionFor (
                        auditionBand_.filterSample (
                            0, static_cast<float> (rightInput)),
                        static_cast<float> (rightInput));
                break;
            case ChannelPlacement::Mid:
            {
                double mid = 0.0;
                double side = 0.0;
                encodeMidSide (leftInput, rightInput, mid, side);
                const auto component = static_cast<float> (mid);
                const auto region = auditionBand_.regionFor (
                    auditionBand_.filterSample (0, component), component);
                regionL = region;
                regionR = region;
                break;
            }
            case ChannelPlacement::Side:
            {
                double mid = 0.0;
                double side = 0.0;
                encodeMidSide (leftInput, rightInput, mid, side);
                const auto component = static_cast<float> (side);
                const auto region = auditionBand_.regionFor (
                    auditionBand_.filterSample (0, component), component);
                regionL = region;
                regionR = -region;
                break;
            }
        }

        scratchLeft[sample] = regionL;
        if (channels >= 2)
            scratchRight[sample] = regionR;

        const auto mix = static_cast<float> (auditionMix_);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = buffer.getWritePointer (channel);
            const auto* region = auditionScratch_.getReadPointer (channel);
            if (auditionMix_ >= 1.0)
                destination[sample] = region[sample];
            else if (auditionMix_ > 0.0)
                destination[sample] += mix * (region[sample] - destination[sample]);
        }
        auditionMix_ = auditionHold_
                     ? std::min (1.0, auditionMix_ + auditionStep_)
                     : std::max (0.0, auditionMix_ - auditionStep_);
    }

    responseDirty_ = true;
    if (! auditionHold_ && auditionMix_ <= 0.0)
    {
        auditionReady_ = false;
        auditionBand_.reset();
        auditionBandIndex_ = -1;
        auditionToken_ = 0;
    }
}

void Processor::publishResponseFrame() noexcept
{
    ResponseFrame frame;
    frame.currentBands = currentSettings_;
    frame.targetBands = transitionActive_ ? targetSettings_ : currentSettings_;
    frame.currentMode = currentMode_;
    frame.targetMode = transitionActive_ ? targetMode_ : currentMode_;
    frame.sampleRate = getSampleRate();
    frame.channelCount = std::clamp (getMainBusNumInputChannels(), 1, kMaxChannels);
    frame.transitionMix = transitionActive_ ? transitionMix_ : 0.0;
    frame.bypassMix = bypassMix_;
    frame.auditionActive = auditionReady_ && (auditionHold_ || auditionMix_ > 0.0);
    frame.auditionBand = auditionReady_ ? auditionBandIndex_ : -1;
    frame.auditionMix = auditionMix_;
    frame.dynamicActiveMask = dynamicActiveMask_;
    frame.dynamicGainDb = publishedDynamicGainDb_;
    frame.generation = ++responseGeneration_;
    frame.prepareEpoch = responsePrepareEpoch_;
    if (responseCore_.push (frame))
        responseDirty_ = false;
}

} // namespace APEX::ParametricEQ

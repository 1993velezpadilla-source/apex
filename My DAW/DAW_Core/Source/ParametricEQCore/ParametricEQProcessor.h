#pragma once

#include <JuceHeader.h>

#include "ParametricEQAuditionCore.h"
#include "ParametricEQCharacterCore.h"
#include "ParametricEQLinearPhase.h"
#include "ParametricEQResponseCore.h"
#include "../AnalysisCore/ApexSpectrumAnalyzerCore.h"
#include "../DynamicsCore/ApexDynamicsCore.h"

#include <array>
#include <atomic>
#include <memory>

namespace APEX::ParametricEQ
{

enum class BandParameterOffset : int
{
    Enabled = 0,
    Bypass,
    Shape,
    Frequency,
    Gain,
    Q,
    Slope
};

constexpr int kParametersPerBand = 7;
constexpr int kBandParameterCount = kMaxBands * kParametersPerBand;
constexpr int kDesignModeParameter = kBandParameterCount;
constexpr int kGlobalBypassParameter = kBandParameterCount + 1;
constexpr int kFirstPlacementParameter = kBandParameterCount + 2;
constexpr int kPlacementParameterCount = kMaxBands;
constexpr int kFirstDynamicParameter = kFirstPlacementParameter + kPlacementParameterCount;
constexpr int kDynamicParametersPerBand = 5;
constexpr int kDynamicBandParameterCount = kMaxBands * kDynamicParametersPerBand;
constexpr int kDynamicDetectorParameter = kFirstDynamicParameter + kDynamicBandParameterCount;
constexpr int kDynamicSidechainParameter = kDynamicDetectorParameter + 1;
constexpr int kDynamicLinkParameter = kDynamicDetectorParameter + 2;
constexpr int kPhaseModeParameter = kDynamicDetectorParameter + 3;
constexpr int kCharacterModeParameter = kPhaseModeParameter + 1;
constexpr int kNumParameters = kCharacterModeParameter + 1;

constexpr int parameterIndex (int band, BandParameterOffset offset) noexcept
{
    return band * kParametersPerBand + static_cast<int> (offset);
}

constexpr int placementParameterIndex (int band) noexcept
{
    return kFirstPlacementParameter + band;
}

static_assert (kFirstPlacementParameter == 170 && kFirstDynamicParameter == 194,
               "Phase 3/4 ABI prefixes are frozen; Phase 5 appends after 193");

// Phase 5: per-band Dynamic EQ parameters append AFTER the frozen Phase 3
// identity. Indices 0-193 never move; a reordering regression would fail the
// static assertion and the State suite's literal-index pins.
enum class DynamicBandOffset : int
{
    Enable = 0,
    Threshold,
    Range,
    Attack,
    Release
};

constexpr int dynamicParameterIndex (int band, DynamicBandOffset offset) noexcept
{
    return kFirstDynamicParameter + band * kDynamicParametersPerBand
         + static_cast<int> (offset);
}

static_assert (kDynamicDetectorParameter == 314 && kPhaseModeParameter == 317,
               "Phase 5 prefix is frozen; peq.phase appends at index 317");

// Phase 6: the processing-mode selector appends after the Phase 5 identity.
// Indices 0-316 never move.
constexpr int kPhaseModeMinimum = 0;
constexpr int kPhaseModeLinear = 1;

static_assert (kCharacterModeParameter == 318,
               "Phase 7 appends peq.character after the frozen Phase 6 ABI");
static_assert (kNumParameters == 319,
               "Phase 7 parameter count must remain append-only");

class ParametricEQParameter final : public juce::AudioProcessorParameterWithID
{
public:
    enum class Kind
    {
        Toggle,
        Shape,
        Placement,
        Frequency,
        Gain,
        Q,
        Slope,
        ThresholdDb,
        RangeDb,
        TimeSeconds,
        DetectorChoice,
        SourceChoice,
        PhaseModeChoice,
        CharacterChoice,
        DesignMode
    };

    ParametricEQParameter (const juce::ParameterID& id, const juce::String& name,
                           Kind kind, float minimumUnits, float maximumUnits,
                           float defaultUnits);

    float getValue() const override;
    void setValue (float newValue) override;
    float getDefaultValue() const override;
    juce::String getText (float normalisedValue, int maximumLength) const override;
    float getValueForText (const juce::String& text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::String getLabel() const override;

    float getUnitsValue() const noexcept;
    int getChoiceIndex() const noexcept;
    Kind getKind() const noexcept { return kind_; }
    float getMinimumUnits() const noexcept { return minimumUnits_; }
    float getMaximumUnits() const noexcept { return maximumUnits_; }
    bool getBool() const noexcept { return getValue() >= 0.5f; }
    bool isStorageLockFree() const noexcept { return value_.is_lock_free(); }

    float toNormalised (float units) const noexcept;
    float fromNormalised (float normalised) const noexcept;

private:
    const Kind kind_;
    const float minimumUnits_;
    const float maximumUnits_;
    const float defaultUnits_;
    std::atomic<float> value_;
};

static_assert (std::atomic<float>::is_always_lock_free,
               "APEX hosted parameter publication must be lock-free");

class Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId = 0x504551; // "PEQ"
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kPluginName = "APEX Parametric EQ";
    static constexpr const char* kCategory = "EQ";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "1.1.0";
    static constexpr const char* kFileOrIdentifier = "APEX::ParametricEQ";
    static constexpr const char* kStateTag = "parametriceqstate";
    static constexpr const char* kVersionProperty = "version";
    static constexpr const char* kAnalyzerProperty = "peq.analyzer";
    static constexpr int kStateVersion = 5;

    Processor();
    ~Processor() override;

    const juce::String getName() const override { return kPluginName; }
    void fillInPluginDescription (juce::PluginDescription& description) const override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    ParametricEQParameter* getParametricEQParameter (int index) noexcept;
    const ParametricEQParameter* getParametricEQParameter (int index) const noexcept;
    static juce::String getParameterId (int index);

    void setAnalyzerMode (Analysis::SpectrumTapMode mode) noexcept;
    Analysis::SpectrumTapMode getAnalyzerMode() const noexcept;
    Analysis::SpectrumAnalyzerCore& getAnalyzer() noexcept { return analyzer_; }
    const Analysis::SpectrumAnalyzerCore& getAnalyzer() const noexcept
    {
        return analyzer_;
    }

    // Transient Band Solo/Audition (Phase 3). Control-thread commands only;
    // the audio thread adopts them through a lock-free mailbox. Audition is
    // not a hosted parameter, is not serialized, and cannot latch across
    // delete, bypass, placement/shape changes, state restore, reset,
    // reprepare, editor teardown, or processor recreation.
    bool beginAudition (int band, std::uint32_t token) noexcept;
    void endAudition (std::uint32_t token) noexcept;
    void cancelAudition() noexcept;
    bool isAuditionHoldingForTesting() const noexcept { return auditionHold_; }
    double getAuditionMixForTesting() const noexcept { return auditionMix_; }

    // External-sidechain-ready plumbing (Phase 5): the shared host supplies
    // detector key audio through this bounded, preallocated interface; the
    // processor never owns host routing. Internal detection uses the placed
    // component of the plugin input. Call only when audio is quiescent or
    // between blocks (control thread).
    bool submitExternalDetectorKey (const juce::AudioBuffer<float>& key) noexcept;
    void clearExternalDetectorKey() noexcept;
    bool isExternalKeyValidForTesting() const noexcept { return externalKeyValid_; }
    double getDynamicGainDbForTesting (int band) const noexcept;

    ResponseCore& getResponseCore() noexcept { return responseCore_; }
    const ResponseCore& getResponseCore() const noexcept { return responseCore_; }

    // Test/future-editor introspection. These are audio-owned values and must
    // not be read concurrently with processBlock; use ResponseCore in a GUI.
    const Engine& getCurrentEngineForTesting() const noexcept { return currentEngine_; }
    double getTransitionMixForTesting() const noexcept { return transitionMix_; }
    double getBypassMixForTesting() const noexcept { return bypassMix_; }
    bool isTransitionActiveForTesting() const noexcept { return transitionActive_; }

    // Phase 6 test introspection. These only REPORT already-existing state
    // and are not part of any production control flow. The generation
    // counter is the worker publication observable; it is nonzero once a
    // linear-phase kernel has been published. modeMix_/linearPhaseMode_ are
    // audio-owned and must not be read concurrently with processBlock.
    std::uint32_t linearKernelGenerationForTesting() const noexcept
    {
        return linearKernelGeneration_.load (std::memory_order_acquire);
    }
    bool isLinearPhaseModeSettledForTesting() const noexcept
    {
        return linearPhaseMode_ && modeMix_ >= 1.0;
    }

private:
    static bool settingsEqual (const BandSettings& lhs,
                               const BandSettings& rhs) noexcept;
    static bool placementChanged (const BandSettings& lhs,
                                  const BandSettings& rhs) noexcept;
    BandSettings readBandParameters (int band) const noexcept;
    DesignMode readDesignModeParameter() const noexcept;
    CharacterMode readCharacterModeParameter() const noexcept;
    bool readBypassParameter() const noexcept;
    void readBandPlacementParameters (
        std::array<BandSettings, kMaxBands>& settings) const noexcept;

    void configureEngine (Engine& engine,
                          const std::array<BandSettings, kMaxBands>& settings,
                          DesignMode mode, bool resetState) noexcept;
    void snapToPublishedParameters (bool resetState) noexcept;
    bool adoptPublishedParameters (bool bypassTarget) noexcept;
    void adoptDynamicParameters() noexcept;
    void computeDynamicGains (int numberOfChannels,
                              int numberOfSamples) noexcept;
    void adoptPhaseMode() noexcept;
    void processLinearPhase (juce::AudioBuffer<float>& buffer,
                             int numberOfChannels, int numberOfSamples) noexcept;
    void requestLinearKernelRebuild() noexcept;
    void buildLinearKernel (std::uint32_t generation) noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int numberOfChannels,
                       int offset, int numberOfSamples,
                       bool bypassTarget) noexcept;
    void completeTransitionIfReady() noexcept;
    void publishResponseFrame() noexcept;
    void adoptAuditionCommand() noexcept;
    void processAudition (juce::AudioBuffer<float>& buffer, int numberOfChannels,
                          int numberOfSamples) noexcept;
    void invalidateAudition (bool incrementEpoch) noexcept;

    std::array<ParametricEQParameter*, kNumParameters> parameters_ {};

    Engine currentEngine_;
    Engine targetEngine_;
    std::array<BandSettings, kMaxBands> currentSettings_ {};
    std::array<BandSettings, kMaxBands> targetSettings_ {};
    DesignMode currentMode_ = DesignMode::Realtime;
    DesignMode targetMode_ = DesignMode::Realtime;

    juce::AudioBuffer<float> dryScratch_;
    juce::AudioBuffer<float> targetScratch_;
    juce::AudioBuffer<float> auditionScratch_;

    double transitionMix_ = 0.0;
    double transitionStep_ = 1.0;
    bool transitionActive_ = false;
    std::uint32_t pendingPlacementBands_ = 0; // bits: bands whose domain changed
    double bypassMix_ = 0.0;
    double bypassStep_ = 1.0;
    bool wetStateResetForBypass_ = false;
    bool prepared_ = false;
    bool responseDirty_ = true;
    std::uint64_t responseGeneration_ = 0;
    std::uint64_t responsePrepareEpoch_ = 0;

    // Phase 5 dynamic EQ runtime (audio-owned). Detector and envelope come
    // from the reusable APEX::Dynamics core; the threshold/range mapping law
    // is the documented Parametric EQ dynamic-range contract.
    std::array<APEX::Dynamics::Detector, kMaxBands> dynamicDetectors_ {};
    std::array<APEX::Dynamics::Envelope, kMaxBands> dynamicEnvelopes_ {};
    std::array<DynamicBandParameters, kMaxBands> dynamicParameters_ {};
    std::array<double, kMaxBands> dynamicSmoothedGainDb_ {};
    std::uint32_t dynamicActiveMask_ = 0;
    bool dynamicAnyActive_ = false;
    APEX::Dynamics::DetectorMode dynamicDetectorMode_
        = APEX::Dynamics::DetectorMode::Rms;
    bool dynamicLink_ = true;
    DetectorSource dynamicSource_ = DetectorSource::Internal;
    juce::AudioBuffer<float> dynamicGainScratch_;
    juce::AudioBuffer<float> externalKeyScratch_;
    bool externalKeyValid_ = false;
    std::array<double, kMaxBands> publishedDynamicGainDb_ {};

    // Phase 6 linear-phase runtime. Kernel design runs on a dedicated
    // low-priority worker; the audio thread only adopts published
    // generations and performs bounded direct convolution.
    class KernelWorker;
    std::unique_ptr<KernelWorker> kernelWorker_;
    std::array<LinearPhaseKernels, 2> linearKernels_ {};
    std::atomic<std::uint32_t> linearKernelActiveIndex_ { 0 };
    std::atomic<std::uint32_t> linearKernelGeneration_ { 0 };
    std::atomic<std::uint32_t> linearKernelRequest_ { 0 };
    std::array<BandSettings, kMaxBands> pendingKernelSettings_ {};
    DesignMode pendingKernelMode_ = DesignMode::Realtime;
    double pendingKernelRate_ = 44100.0;
    std::uint32_t linearKernelAdoptedGeneration_ = 0;
    bool kernelRebuildPending_ = false;
    LinearPhaseConvolverChannel convolverCurrentL_;
    LinearPhaseConvolverChannel convolverCurrentR_;
    LinearPhaseConvolverChannel convolverNextL_;
    LinearPhaseConvolverChannel convolverNextR_;
    bool linearPhaseMode_ = false;
    bool linearPhaseTarget_ = false;
    std::uint32_t currentKernelIndex_ = 0;
    std::uint32_t nextKernelIndex_ = 0;
    double modeMix_ = 0.0;         // 0 = minimum phase, 1 = linear phase
    double modeStep_ = 1.0;
    bool kernelTransitionActive_ = false;
    double kernelMix_ = 1.0;
    double kernelStep_ = 1.0;
    juce::AudioBuffer<float> linearPhaseScratch_;
    juce::AudioBuffer<float> kernelNextScratch_;

    // Audition state is audio-owned except for the lock-free mailbox and the
    // monotonic epoch, which control threads update before the audio thread
    // observes them at a block boundary.
    alignas (64) std::atomic<std::uint64_t> auditionMailbox_ { 0 };
    alignas (64) std::atomic<std::uint32_t> auditionSequence_ { 0 };
    std::atomic<std::uint64_t> auditionEpoch_ { 0 };
    std::uint64_t auditionObservedEpoch_ = 0;
    std::uint32_t auditionLastSequence_ = 0;
    std::uint32_t auditionToken_ = 0;
    AuditionBand auditionBand_;
    int auditionBandIndex_ = -1;
    double auditionMix_ = 0.0;
    double auditionStep_ = 1.0;
    bool auditionHold_ = false;
    bool auditionReady_ = false;

    CharacterCore characterCore_;
    ResponseCore responseCore_;
    Analysis::SpectrumAnalyzerCore analyzer_ { "APEX Parametric EQ Analyzer" };
};

} // namespace APEX::ParametricEQ

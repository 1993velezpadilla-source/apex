#pragma once
#include <JuceHeader.h>
#include "G10Types.h"
#include "G10CurveEngineCore.h"
#include "G10AnalogEngineCore.h"
#include "G10MiniEqCore.h"
#include "../G10UI/G10AnalyzerFifo.h"

namespace APEX {
namespace G10 {

class G10Editor; // defined in Source/G10UI/G10Editor.h (implemented in G10Processor.cpp)

// ============================================================================
// G10Parameter — hosted parameter for the APEX G10 processor.
//
// Derives from juce::AudioProcessorParameterWithID, which in JUCE 8.0.12
// derives from juce::HostedAudioProcessorParameter. This gives us BOTH:
//   - addHostedParameter() compatibility (AudioPluginInstance hides the plain
//     AudioProcessor::addParameter path), and
//   - the dynamic_cast<juce::AudioProcessorParameterWithID*> that the APEX
//     chain automation binding uses (PluginChainCore::configureSlotAutomation)
//     so every G10 parameter is bound to the APEX automation system by its
//     stable string ID.
//
// Value mapping (normalized 0..1 <-> units):
//   Band   -> dB in [kBandMinDb, kBandMaxDb]  (continuous)
//   Trim   -> dB in [kTrimMinDb, kTrimMaxDb]  (continuous)
//   Toggle -> 0/1                             (discrete boolean)
//
// Thread contract: the host writes setValue() (control thread) and the audio
// thread reads getValue()/getUnitsValue() — the canonical JUCE parameter
// pattern (plain float storage, no locks, no allocation).
// ============================================================================

class G10Parameter final : public juce::AudioProcessorParameterWithID
{
public:
    enum class Kind
    {
        Band,       // -12..+12 dB, continuous
        Trim,       // -18..+18 dB, continuous
        Toggle,     // 0/1
        HzHpf,      // Mini EQ HPF: 0 = OFF, (0,1] -> 20..420 Hz (log law)
        HzLpf,      // Mini EQ LPF: 1 = OFF, [0,1) -> 8.5..16 kHz (log law)
        HzBell,     // Mini EQ Bell frequency: 20 Hz..20 kHz (log law)
        BellGain,   // Mini EQ Bell gain: -12..+12 dB
        BellQ       // Mini EQ Bell Q: 0.5..10 (log law)
    };

    G10Parameter (const juce::ParameterID& id, const juce::String& name,
                  Kind kind, float minValue, float maxValue, float defaultValue)
        : juce::AudioProcessorParameterWithID (id, name),
          kind_ (kind),
          minValue_ (minValue),
          maxValue_ (maxValue),
          defaultValue_ (defaultValue),
          value_ (toNormalized (defaultValue_))
    {
    }

    // ---- AudioProcessorParameter (pure virtuals) -------------------------

    float getValue() const override { return value_; }

    void setValue (float newValue) override
    {
        value_ = juce::jlimit (0.0f, 1.0f, newValue);
    }

    float getDefaultValue() const override { return toNormalized (defaultValue_); }

    juce::String getText (float v, int) const override
    {
        switch (kind_)
        {
        case Kind::Toggle:
            return v >= 0.5f ? "On" : "Off";
        case Kind::HzHpf:
            return G10MiniEqCore::hpfHzFromNorm (v) <= 0.0f ? "Off"
                                                            : formatHz (G10MiniEqCore::hpfHzFromNorm (v));
        case Kind::HzLpf:
            return G10MiniEqCore::lpfHzFromNorm (v) <= 0.0f ? "Off"
                                                            : formatHz (G10MiniEqCore::lpfHzFromNorm (v));
        case Kind::HzBell:
            return formatHz (G10MiniEqCore::bellHzFromNorm (v));
        case Kind::BellGain:
            return formatDb (G10MiniEqCore::bellGainDbFromNorm (v));
        case Kind::BellQ:
            return "Q " + juce::String (G10MiniEqCore::bellQFromNorm (v), 2);
        default:
            return juce::String (fromNormalized (v), 1) + " dB";
        }
    }

    float getValueForText (const juce::String& text) const override
    {
        if (kind_ == Kind::Toggle)
            return text.trim().equalsIgnoreCase ("on") ? 1.0f : 0.0f;
        return toNormalized (text.getFloatValue());
    }

    int getNumSteps() const override
    {
        return kind_ == Kind::Toggle
            ? 2
            : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }

    bool isDiscrete() const override { return kind_ == Kind::Toggle; }
    bool isBoolean() const override { return kind_ == Kind::Toggle; }

    // ---- G10 helpers ------------------------------------------------------

    /** Current value in product units (dB for Band/Trim, 0/1 for Toggle,
        normalized 0..1 for the Mini EQ laws). */
    float getUnitsValue() const noexcept { return fromNormalized (value_); }

    bool getBool() const noexcept { return value_ >= 0.5f; }

private:
    static juce::String formatHz (float hz) noexcept
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    static juce::String formatDb (float db) noexcept
    {
        return juce::String (db >= 0.0f ? "+" : "-")
             + juce::String (std::abs (db), 1) + " dB";
    }

    float toNormalized (float v) const noexcept
    {
        if (kind_ == Kind::Toggle)
            return v >= 0.5f ? 1.0f : 0.0f;
        return (v - minValue_) / (maxValue_ - minValue_);
    }

    float fromNormalized (float v) const noexcept
    {
        if (kind_ == Kind::Toggle)
            return v >= 0.5f ? 1.0f : 0.0f;
        return minValue_ + v * (maxValue_ - minValue_);
    }

    const Kind kind_;
    const float minValue_, maxValue_, defaultValue_;
    float value_;
};

// ============================================================================
// G10Processor — the APEX G10 AudioPluginInstance.
//
// Identity:
//   name            "APEX G10"
//   format          "APEX Native"
//   uniqueId        0x473130 ("G10")
//   category        "EQ"
//   latency         0 samples (pure minimum-phase IIR)
//   editor          none in Phase 1 (hasEditor() == false)
//
// State: ValueTree, schema version 3, tolerant restore (unknown fields
// ignored, missing fields keep defaults, out-of-range values clamped,
// non-finite values keep defaults). v2 states are migrated SEMANTICALLY
// (old normalized -> exact old Hz/Q -> new normalized) so old sessions
// preserve their sound; v1 states load with Mini EQ defaults.
//
// Threads:
//   prepareToPlay / releaseResources -> message thread
//   processBlock                     -> audio thread only
//   parameter setValue               -> control thread (JUCE canonical)
// ============================================================================

class G10Processor final : public juce::AudioPluginInstance
{
public:
    enum class ParameterExposure
    {
        nativeCompatible,
        publicVst3
    };

    static constexpr int kUniqueId = 0x473130; // "G10"
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kPluginName = "APEX G10";
    static constexpr const char* kCategory = "EQ";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "1.0.0";
    static constexpr const char* kFileOrIdentifier = "APEX::G10";

    static constexpr const char* kStateTag = "g10state";
    static constexpr const char* kVersionProp = "version";
    static constexpr int kNumPublicVst3Parameters = kNumParams - 2;

    explicit G10Processor (ParameterExposure exposure = ParameterExposure::nativeCompatible)
        : juce::AudioPluginInstance (juce::AudioProcessor::BusesProperties()
            .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (auto& semanticIndex : hostedParameterSemanticIndexes_)
            semanticIndex = -1;

        auto add = [this] (int semanticIndex,
                           std::unique_ptr<G10Parameter> p,
                           bool hostExposed) -> G10Parameter*
        {
            auto* raw = p.get();
            params_[semanticIndex] = raw;

            if (hostExposed)
            {
                jassert (hostedParameterCount_ < kNumParams);
                hostedParameterSemanticIndexes_[hostedParameterCount_++] = semanticIndex;
                addHostedParameter (std::move (p));
            }
            else
            {
                // Stock JUCE 8.0.12 has no AudioProcessorParameter surface for
                // VST3 kIsHidden. Keep legacy state objects owned here instead
                // of exporting inert, externally writable controls.
                stateOnlyParameters_[semanticIndex] = std::move (p);
            }

            return raw;
        };

        const bool exposeLegacyHostParameters = exposure == ParameterExposure::nativeCompatible;

        add (kInput, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamInput, 1), "Input", G10Parameter::Kind::Trim,
            kTrimMinDb, kTrimMaxDb, kTrimDefaultDb), true);

        for (int b = 0; b < kNumBands; ++b)
            add (kBand31 + b, std::make_unique<G10Parameter> (
                juce::ParameterID (kBandInfos[b].paramId, 1),
                kBandInfos[b].musicalName, G10Parameter::Kind::Band,
                kBandMinDb, kBandMaxDb, kBandDefaultDb), true);

        add (kOutput, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamOutputId, 1), "Output", G10Parameter::Kind::Trim,
            kTrimMinDb, kTrimMaxDb, kTrimDefaultDb), true);
        add (kAnalog, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamAnalogId, 1), "Analog", G10Parameter::Kind::Toggle,
            0.0f, 1.0f, 0.0f), exposeLegacyHostParameters);
        add (kQuality, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamQualityId, 1), "Quality", G10Parameter::Kind::Toggle,
            0.0f, 1.0f, 0.0f), exposeLegacyHostParameters);
        add (kBypass, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamBypassId, 1), "Bypass", G10Parameter::Kind::Toggle,
            0.0f, 1.0f, 0.0f), true);

        // ---- Mini Clean EQ (clean digital correction layer) ---------------
        // All 17 parameters are REAL public controls in BOTH exposure
        // policies. Stable IDs below become permanent once this revision is
        // published. Hosted order follows the semantic index order.
        add (kHpf, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamHpfId, 1), "HPF", G10Parameter::Kind::HzHpf,
            0.0f, 1.0f, G10MiniEqCore::kHpfOffNorm), true);
        add (kLpf, std::make_unique<G10Parameter> (
            juce::ParameterID (kParamLpfId, 1), "LPF", G10Parameter::Kind::HzLpf,
            0.0f, 1.0f, G10MiniEqCore::kLpfOffNorm), true);

        struct BellIds { const char* enabled; const char* freq; const char* gain; const char* q; const char* bypass; };
        static constexpr BellIds kBellIds[G10MiniEqCore::kMaxBells] =
        {
            { kParamBell1EnabledId, kParamBell1FreqId, kParamBell1GainId, kParamBell1QId, kParamBell1BypassId },
            { kParamBell2EnabledId, kParamBell2FreqId, kParamBell2GainId, kParamBell2QId, kParamBell2BypassId },
            { kParamBell3EnabledId, kParamBell3FreqId, kParamBell3GainId, kParamBell3QId, kParamBell3BypassId }
        };

        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int base = kBell1Enabled + b * 4;
            const juce::String num = juce::String (b + 1);
            add (base, std::make_unique<G10Parameter> (
                juce::ParameterID (kBellIds[b].enabled, 1), "B" + num + " Enabled",
                G10Parameter::Kind::Toggle, 0.0f, 1.0f, 0.0f), true);
            add (base + 1, std::make_unique<G10Parameter> (
                juce::ParameterID (kBellIds[b].freq, 1), "B" + num + " Freq",
                G10Parameter::Kind::HzBell, 0.0f, 1.0f, 0.5f), true);
            add (base + 2, std::make_unique<G10Parameter> (
                juce::ParameterID (kBellIds[b].gain, 1), "B" + num + " Gain",
                G10Parameter::Kind::BellGain,
                G10MiniEqCore::kBellMinDb, G10MiniEqCore::kBellMaxDb, 0.0f), true);
            add (base + 3, std::make_unique<G10Parameter> (
                juce::ParameterID (kBellIds[b].q, 1), "B" + num + " Q",
                G10Parameter::Kind::BellQ, 0.0f, 1.0f, G10MiniEqCore::bellNormFromQ (G10MiniEqCore::kBellDefaultQ)), true);
        }

        // v3: individual Bell bypass, appended AFTER the v2 IDs so the stable
        // ordering of every earlier parameter is untouched. Bypass != Delete:
        // the Bell stays present, Freq/Gain/Q persist, only the DSP
        // contribution fades to identity (~5 ms ramp, same architecture as
        // enable/disable; see G10MiniEqCore::processBlock).
        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int idx = kBell1Bypass + b;
            const juce::String num = juce::String (b + 1);
            add (idx, std::make_unique<G10Parameter> (
                juce::ParameterID (kBellIds[b].bypass, 1), "B" + num + " Bypass",
                G10Parameter::Kind::Toggle, 0.0f, 1.0f, 0.0f), true);
        }

        const int expectedHostedCount = exposeLegacyHostParameters
            ? kNumParams
            : kNumPublicVst3Parameters;
        jassert (getParameters().size() == expectedHostedCount);
        jassert (hostedParameterCount_ == expectedHostedCount);
        juce::ignoreUnused (expectedHostedCount);
    }

    // ---- Identity ---------------------------------------------------------

    const juce::String getName() const override { return kPluginName; }

    void fillInPluginDescription (juce::PluginDescription& desc) const override
    {
        desc.name = kPluginName;
        desc.descriptiveName = kPluginName;
        desc.pluginFormatName = kFormatName;
        desc.category = kCategory;
        desc.manufacturerName = kManufacturer;
        desc.version = kVersion;
        desc.fileOrIdentifier = kFileOrIdentifier;
        desc.uniqueId = kUniqueId;
        desc.deprecatedUid = kUniqueId;
        desc.isInstrument = false;
        desc.numInputChannels = 2;
        desc.numOutputChannels = 2;
    }

    // ---- Lifecycle --------------------------------------------------------

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        // Store the host rate so getSampleRate() and downstream consumers
        // (editor / analyzer / Mini-EQ surface) can read the ACTUAL rate.
        // Without this JUCE's default AudioProcessor::getSampleRate()
        // returns 0 because setRateAndBufferSizeDetails was never called.
        setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);

        const int numChannels = juce::jmax (1, getMainBusNumInputChannels());
        engine_.prepare (sampleRate, samplesPerBlock, numChannels);
        analogNormal_.prepare (sampleRate, samplesPerBlock, numChannels, 2);
        analogHq_.prepare (sampleRate, samplesPerBlock, numChannels, 4);

        // Clean Mini EQ runs at BASE sample rate after the oversampled
        // musical/color path; never oversampled, never on the audio thread
        // allocations. Preallocated filter state sized in prepare().
        miniEq_.prepare (sampleRate, numChannels);

        // Level meters start at the floor on every prepare.
        inputMeterDb_.store (-300.0f, std::memory_order_relaxed);
        outputMeterDb_.store (-300.0f, std::memory_order_relaxed);

        // Scratch buffers for the analog crossfade paths (preallocated;
        // never allocated on the audio thread).
        scratchA_.setSize (numChannels, samplesPerBlock, false, false, true);
        scratchB_.setSize (numChannels, samplesPerBlock, false, false, true);

        // Analyzer transport layout (mono or stereo per the bus). The FIFO is
        // fixed-size and preallocated; the audio thread never allocates.
        analyzerFifo_.setNumChannels (numChannels);

        // Crossfade steps: ~10 ms for analog on/off and quality changes.
        analogStep_ = 1.0f / (float) juce::jmax (1.0, sampleRate * 0.010);
        qualityStep_ = 1.0f / (float) juce::jmax (1.0, sampleRate * 0.010);

        // Canonical identity (Internal Color): the APEX color (D1B + I1) is
        // ALWAYS active. The legacy g10.analog parameter is serialized for
        // compatibility but is inert in the audio path.
        analogMix_ = 1.0f;
        // Quality is internal: HQ (4x) offline, NORMAL (2x) realtime. The
        // host sets the non-realtime flag BEFORE prepareToPlay (see
        // PluginInstanceCore::prepareForOffline), so the snap below is
        // click-free by construction.
        qualityMix_ = isNonRealtime() ? 1.0f : 0.0f;

        // Latency is zero in every mode: the clean path and both analog
        // chains are pure minimum-phase IIR (no FIR, no lookahead, no delay
        // lines), so the oversampling path adds no bulk latency. Publish via
        // the inherited JUCE storage (getLatencySamples() is not virtual in
        // JUCE 8.0.12; setLatencySamples() is the intended API). No mode or
        // quality change can make this stale.
        setLatencySamples (0);
    }

    void releaseResources() override
    {
        engine_.reset();
        analogNormal_.reset();
        analogHq_.reset();
        miniEq_.reset();
    }

    // ---- Level metering (UI display only; never affects audio) -------------

    /** Input level (dBFS, AFTER the Input trim) measured on the audio thread
        once per host block — the real level entering G10's internal
        processing.  UI reads this on its timer; relaxed atomic handoff (no
        locks, no allocation, no FIFO). */
    float getInputMeterDb() const noexcept
    {
        return inputMeterDb_.load (std::memory_order_relaxed);
    }

    /** Output level (dBFS) of the EXACT final buffer the host receives
        (post-EQ, post-analog, post-quality, post-trim, post-bypass). */
    float getOutputMeterDb() const noexcept
    {
        return outputMeterDb_.load (std::memory_order_relaxed);
    }

    // ---- Audio ------------------------------------------------------------

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Adopt control-plane targets (JUCE canonical plain-float reads).
        engine_.setInputTargetDb (params_[kInput]->getUnitsValue());
        for (int b = 0; b < kNumBands; ++b)
            engine_.setBandTargetGainDb (b, params_[kBand31 + b]->getUnitsValue());
        engine_.setOutputTargetDb (params_[kOutput]->getUnitsValue());
        engine_.setBypassTarget (params_[kBypass]->getBool());

        // Canonical identity: the analog path is always active. The legacy
        // g10.analog / g10.quality values are serialized for compatibility
        // but are inert in the audio path.
        const bool analogTarget = true;
        const bool qualityTarget = isNonRealtime();

        // Keep the analog chains' targets in sync so they stay warm while
        // idle and resume smoothly when the analog path becomes active.
        analogNormal_.setInputTargetDb (params_[kInput]->getUnitsValue());
        analogNormal_.setOutputTargetDb (params_[kOutput]->getUnitsValue());
        analogNormal_.setBypassTarget (params_[kBypass]->getBool());
        analogHq_.setInputTargetDb (params_[kInput]->getUnitsValue());
        analogHq_.setOutputTargetDb (params_[kOutput]->getUnitsValue());
        analogHq_.setBypassTarget (params_[kBypass]->getBool());
        for (int b = 0; b < kNumBands; ++b)
        {
            const float db = params_[kBand31 + b]->getUnitsValue();
            analogNormal_.setBandTargetGainDb (b, db);
            analogHq_.setBandTargetGainDb (b, db);
        }

        const int numChannels = juce::jmin (buffer.getNumChannels(),
                                            G10CurveEngineCore::kMaxChannels);
        const int numSamples = buffer.getNumSamples();
        if (numChannels <= 0 || numSamples <= 0)
            return;

        // Level metering — INPUT: the signal entering internal processing
        // AFTER the Input trim (the trim is a linear gain, so dB adds).
        // Written once per host block; the UI reads it on its 30 Hz timer.
        inputMeterDb_.store (blockPeakDb (buffer, numChannels, numSamples)
                             + params_[kInput]->getUnitsValue(),
                             std::memory_order_relaxed);

        // Clean Mini EQ targets: plain-float reads of the current parameter
        // values (canonical JUCE pattern; no locks, no allocation).
        G10MiniEqCore::Targets miniEqTargets;
        miniEqTargets.hpfNorm = params_[kHpf]->getValue();
        miniEqTargets.lpfNorm = params_[kLpf]->getValue();
        miniEqTargets.bypass = params_[kBypass]->getBool();
        for (int b = 0; b < G10MiniEqCore::kMaxBells; ++b)
        {
            const int base = kBell1Enabled + b * 4;
            miniEqTargets.bell[b].enabled = params_[base]->getBool();
            miniEqTargets.bell[b].bypassed = params_[kBell1Bypass + b]->getBool();
            miniEqTargets.bell[b].freqNorm = params_[base + 1]->getValue();
            miniEqTargets.bell[b].gainDb = params_[base + 2]->getUnitsValue();
            miniEqTargets.bell[b].qNorm = params_[base + 3]->getValue();
        }

        // The crossfade states advance per sample inside the crossfade loops
        // below, so the ~10 ms ramp is smooth and block-size independent.

        // Settled OFF: the EXACT frozen Phase 1 path (bit-exact regression
        // authority). No analog work is performed in this state.
        if (analogMix_ <= 0.001f && ! analogTarget)
        {
            float* channelData[G10CurveEngineCore::kMaxChannels] = { nullptr, nullptr };
            for (int ch = 0; ch < numChannels; ++ch)
                channelData[ch] = buffer.getWritePointer (ch);
            engine_.processBlock (channelData, numChannels, numSamples);
            miniEq_.processBlock (channelData, numSamples, miniEqTargets);
            pushAnalyzerSamples (buffer, numChannels, numSamples);
            outputMeterDb_.store (blockPeakDb (buffer, numChannels, numSamples),
                                  std::memory_order_relaxed);
            return;
        }

        // Oversized host block defense: the analog crossfade path below uses
        // preallocated scratch buffers sized to the prepared maximum block
        // (JUCE: "program defensively in case a buggy host exceeds this
        // value"). Process the COMPLETE host buffer in consecutive bounded
        // chunks. All DSP state (EQ filters, D1B, I1/flux, oversampling AA,
        // DC blocker, smoothing, analogMix/qualityMix crossfades) lives in
        // members and is preserved across chunk boundaries, so chunking is
        // bit-equivalent to receiving the same audio as several legal
        // smaller blocks. No allocation, no locks, no resize.
        const int chunkCapacity = juce::jmax (1, scratchA_.getNumSamples());
        int offset = 0;
        while (offset < numSamples)
        {
            const int chunk = juce::jmin (chunkCapacity, numSamples - offset);
            processChunk (buffer, numChannels, offset, chunk, analogTarget, qualityTarget, miniEqTargets);
            offset += chunk;
        }

        // Level metering — OUTPUT: the final buffer the host receives.
        outputMeterDb_.store (blockPeakDb (buffer, numChannels, numSamples),
                              std::memory_order_relaxed);
    }

    // Audio thread: process one bounded chunk of the host buffer through the
    // canonical analog path. `offset`/`numSamples` address the host buffer;
    // every scratch access is guaranteed within preallocated capacity
    // (chunk <= prepared maximum block). State continuity across chunks is
    // automatic: all state is member state, advanced per sample.
    void processChunk (juce::AudioBuffer<float>& buffer, int numChannels,
                       int offset, int numSamples,
                       bool analogTarget, bool qualityTarget,
                       const G10MiniEqCore::Targets& miniEqTargets) noexcept
    {
        jassert (numSamples <= scratchA_.getNumSamples());
        jassert (numSamples <= scratchB_.getNumSamples());

        // Non-owning view over the chunk region (no allocation).
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                        numChannels, offset, numSamples);

        // scratchA_ holds the clean engine's input copy (and later its
        // output) for the analog crossfade.
        for (int ch = 0; ch < numChannels; ++ch)
            scratchA_.copyFrom (ch, 0, chunk, ch, 0, numSamples);

        if (analogMix_ < 0.999f)
        {
            float* cleanData[G10CurveEngineCore::kMaxChannels] = { nullptr, nullptr };
            for (int ch = 0; ch < numChannels; ++ch)
                cleanData[ch] = scratchA_.getWritePointer (ch);
            engine_.processBlock (cleanData, numChannels, numSamples);
        }

        // Run the active chain(s) on the chunk.
        const bool settledNormal = qualityMix_ <= 0.001f && ! qualityTarget;
        const bool settledHq = qualityMix_ >= 0.999f && qualityTarget;

        if (settledNormal)
        {
            analogNormal_.processBlock (chunk, numChannels, numSamples);
        }
        else if (settledHq)
        {
            analogHq_.processBlock (chunk, numChannels, numSamples);
        }
        else
        {
            // Quality transition: BOTH chains process the SAME dry input in
            // parallel (HQ must NEVER receive NORMAL's processed output),
            // then crossfade over ~10 ms. scratchB_ holds the dry copy;
            // the formula dst = NORMAL*(1-q) + HQ*q selects exactly NORMAL
            // at q=0 and exactly HQ at q=1.
            for (int ch = 0; ch < numChannels; ++ch)
                scratchB_.copyFrom (ch, 0, chunk, ch, 0, numSamples);
            analogNormal_.processBlock (chunk, numChannels, numSamples);
            analogHq_.processBlock (scratchB_, numChannels, numSamples);
            for (int s = 0; s < numSamples; ++s)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* dst = chunk.getWritePointer (ch);
                    const float* b2 = scratchB_.getReadPointer (ch);
                    dst[s] = dst[s] * (1.0f - qualityMix_) + b2[s] * qualityMix_;
                }
                qualityMix_ = qualityTarget ? juce::jmin (1.0f, qualityMix_ + qualityStep_)
                                            : juce::jmax (0.0f, qualityMix_ - qualityStep_);
            }
        }

        // Analog crossfade: out = clean * (1-analogMix) + chain * analogMix.
        // The mix advances per sample; the settled-OFF early return above
        // guarantees this loop is only reached while the state must move.
        for (int s = 0; s < numSamples; ++s)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* dst = chunk.getWritePointer (ch);
                const float* c = scratchA_.getReadPointer (ch);
                dst[s] = c[s] * (1.0f - analogMix_) + dst[s] * analogMix_;
            }
            analogMix_ = analogTarget ? juce::jmin (1.0f, analogMix_ + analogStep_)
                                      : juce::jmax (0.0f, analogMix_ - analogStep_);
        }

        // Clean Mini EQ: AFTER the complete musical/color path, at base rate,
        // BEFORE the analyzer tap so the measured spectrum is the true output.
        // When everything is settled OFF the stage is skipped entirely
        // (bit-identical to the frozen G10).
        miniEq_.processBlock (chunk.getArrayOfWritePointers(), numSamples, miniEqTargets);

        pushAnalyzerSamples (chunk, numChannels, numSamples);
    }

    // ---- Buses ------------------------------------------------------------

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in  = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        const bool inOk  = in  == juce::AudioChannelSet::mono()
                        || in  == juce::AudioChannelSet::stereo();
        const bool outOk = out == juce::AudioChannelSet::mono()
                        || out == juce::AudioChannelSet::stereo();
        return inOk && outOk && in == out;
    }

    // ---- Misc -------------------------------------------------------------

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // Declare the existing stable g10.bypass parameter as the processor bypass
    // parameter. JUCE's VST3 client otherwise synthesizes a second host bypass
    // parameter, producing a duplicate external control without changing DSP.
    juce::AudioProcessorParameter* getBypassParameter() const override { return params_[kBypass]; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override; // G10Processor.cpp

    // ---- State ------------------------------------------------------------

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        juce::ValueTree state (kStateTag);
        state.setProperty (kVersionProp, kStateVersion, nullptr);
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto* id = getParamId (i);
            if (id != nullptr)
                state.setProperty (juce::Identifier (id), params_[i]->getValue(), nullptr);
        }

        juce::MemoryOutputStream stream (destData, false);
        state.writeToStream (stream);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        const juce::ValueTree state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
        if (! state.isValid())
            return;

        // Tolerant restore: unknown fields ignored, missing fields keep
        // defaults, out-of-range values clamped by G10Parameter::setValue,
        // non-finite values keep defaults.
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto* id = getParamId (i);
            if (id == nullptr)
                continue;

            const juce::Identifier prop (id);
            if (! state.hasProperty (prop))
                continue;

            const float v = (float) state.getProperty (prop, 0.0f);
            if (! std::isfinite (v))
                continue;

            params_[i]->setValue (v);
        }

        // ---- Semantic migration (v2 -> v3 control laws) ---------------------
        // v2 stored normalized values under the OLD laws (HPF 20..420 Hz,
        // LPF 8.5..16 kHz, Bell Q 0.5..10). v3 re-mapped the ranges to
        // HPF/LPF 20 Hz..20 kHz and Q 0.10..40.0, so the raw old normalized
        // number must NOT be reused: decode it through the EXACT old law into
        // product units, then encode it through the new law. This preserves
        // the OLD SESSION SOUND.
        //
        // The guard is STRICTLY version == 2:
        //   - v1 states contain NO Mini EQ fields; they must stay at Mini EQ
        //     defaults exactly as before (a stray Mini-EQ-looking property in
        //     a v1-shaped state is a raw restore, never a v2 semantic value).
        //   - v3+ states already use the new laws: direct restore, no
        //     migration.
        // Each field is additionally guarded by hasProperty (defense in
        // depth: a v2 state may legitimately omit Mini EQ fields). Bell
        // Freq/Gain laws are unchanged in v3, so only HPF, LPF and Bell Q
        // are migrated. The migration runs AFTER the generic restore above
        // so the semantically encoded value wins over the raw old-law value.
        const int version = (int) state.getProperty (kVersionProp, 1);
        if (version == 2)
        {
            using MiniEq = G10MiniEqCore;

            const juce::Identifier hpfId (kParamHpfId);
            if (state.hasProperty (hpfId))
            {
                const float oldNorm = (float) state.getProperty (hpfId, 0.0f);
                if (std::isfinite (oldNorm))
                {
                    const float oldHz = MiniEq::legacyHpfHzFromNorm (oldNorm);
                    params_[kHpf]->setValue (oldHz <= 0.0f
                        ? MiniEq::kHpfOffNorm
                        : MiniEq::hpfNormFromHz (oldHz));
                }
            }

            const juce::Identifier lpfId (kParamLpfId);
            if (state.hasProperty (lpfId))
            {
                const float oldNorm = (float) state.getProperty (lpfId, 0.0f);
                if (std::isfinite (oldNorm))
                {
                    const float oldHz = MiniEq::legacyLpfHzFromNorm (oldNorm);
                    params_[kLpf]->setValue (oldHz <= 0.0f
                        ? MiniEq::kLpfOffNorm
                        : MiniEq::lpfNormFromHz (oldHz));
                }
            }

            for (int b = 0; b < MiniEq::kMaxBells; ++b)
            {
                const juce::Identifier qId (getParamId (kBell1Q + b * 4));
                if (qId.isValid() && state.hasProperty (qId))
                {
                    const float oldNorm = (float) state.getProperty (qId, 0.0f);
                    if (std::isfinite (oldNorm))
                    {
                        const float oldQ = MiniEq::legacyBellQFromNorm (oldNorm);
                        params_[kBell1Q + b * 4]->setValue (MiniEq::bellNormFromQ (oldQ));
                    }
                }
            }
        }
    }

    // ---- Introspection (tests / future UI) --------------------------------

    G10Parameter* getG10Parameter (int index) noexcept
    {
        return (index >= 0 && index < kNumParams) ? params_[index] : nullptr;
    }

    const G10Parameter* getG10Parameter (int index) const noexcept
    {
        return (index >= 0 && index < kNumParams) ? params_[index] : nullptr;
    }

    /** Translate JUCE's compact hosted-parameter index to G10's stable
        semantic index. The mapping is immutable after construction and is
        therefore safe to read from AudioProcessorListener callbacks. */
    int getSemanticParameterIndex (int hostedParameterIndex) const noexcept
    {
        return hostedParameterIndex >= 0 && hostedParameterIndex < hostedParameterCount_
            ? hostedParameterSemanticIndexes_[hostedParameterIndex]
            : -1;
    }

    G10CurveEngineCore& getEngine() noexcept { return engine_; }
    const G10CurveEngineCore& getEngine() const noexcept { return engine_; }

    G10MiniEqCore& getMiniEq() noexcept { return miniEq_; }
    const G10MiniEqCore& getMiniEq() const noexcept { return miniEq_; }

    // ---- Analyzer (editor) -------------------------------------------------
    // The editor reads the FIFO on the GUI thread; the audio thread only
    // pushes when an editor is attached (analyzerActive_ gate), so the
    // analyzer costs nothing while no G10 editor is open.
    G10AnalyzerFifo* getAnalyzerFifo() noexcept { return &analyzerFifo_; }
    const G10AnalyzerFifo* getAnalyzerFifo() const noexcept { return &analyzerFifo_; }

    void setAnalyzerActive (bool active) noexcept
    {
        analyzerActive_.store (active, std::memory_order_relaxed);
    }

    static const char* getParamId (int index) noexcept
    {
        switch (index)
        {
            case kInput:   return kParamInput;
            case kOutput:  return kParamOutputId;
            case kAnalog:  return kParamAnalogId;
            case kQuality: return kParamQualityId;
            case kBypass:  return kParamBypassId;
            case kHpf:     return kParamHpfId;
            case kLpf:     return kParamLpfId;
            case kBell1Enabled: return kParamBell1EnabledId;
            case kBell1Freq:    return kParamBell1FreqId;
            case kBell1Gain:    return kParamBell1GainId;
            case kBell1Q:       return kParamBell1QId;
            case kBell2Enabled: return kParamBell2EnabledId;
            case kBell2Freq:    return kParamBell2FreqId;
            case kBell2Gain:    return kParamBell2GainId;
            case kBell2Q:       return kParamBell2QId;
            case kBell3Enabled: return kParamBell3EnabledId;
            case kBell3Freq:    return kParamBell3FreqId;
            case kBell3Gain:    return kParamBell3GainId;
            case kBell3Q:       return kParamBell3QId;
            case kBell1Bypass:  return kParamBell1BypassId;
            case kBell2Bypass:  return kParamBell2BypassId;
            case kBell3Bypass:  return kParamBell3BypassId;
            default:
                if (index >= kBand31 && index <= kBand16k)
                    return kBandInfos[index - kBand31].paramId;
                return nullptr;
        }
    }

private:
    // Audio thread: channel-peak authority for the compact single-lane
    // meters — the LOUDER of the available channels (a stereo pair never
    // cancels; the display shows the true hot side).  Returns dBFS.
    static float blockPeakDb (const juce::AudioBuffer<float>& buffer,
                              int numChannels, int numSamples) noexcept
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int s = 0; s < numSamples; ++s)
            {
                const float a = std::abs (d[s]);
                if (a > peak)
                    peak = a;
            }
        }
        return peak <= 1.0e-9f ? -300.0f : 20.0f * std::log10 (peak);
    }

    // Audio thread: push the EXACT final samples the host receives (post-EQ,
    // post-analog, post-quality, post-trim, post-bypass) into the analyzer
    // FIFO. Gated by analyzerActive_ (an editor is open); no allocation, no
    // locks, no GUI calls. Stereo keeps BOTH channels — the analyzer combines
    // them by power, so anti-phase material is measured, never cancelled.
    void pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer,
                              int numChannels, int numSamples) noexcept
    {
        if (! analyzerActive_.load (std::memory_order_relaxed))
            return;
        if (numChannels <= 0 || numSamples <= 0)
            return;

        if (numChannels >= 2)
        {
            analyzerFifo_.pushStereo (buffer.getReadPointer (0),
                                      buffer.getReadPointer (1), numSamples);
        }
        else
        {
            analyzerFifo_.push (buffer.getReadPointer (0), numSamples);
        }
    }

    G10Parameter* params_[kNumParams] = {};
    std::unique_ptr<G10Parameter> stateOnlyParameters_[kNumParams];
    int hostedParameterSemanticIndexes_[kNumParams] = {};
    int hostedParameterCount_ = 0;
    G10CurveEngineCore engine_;    // frozen Phase 1 clean path (regression authority)
    G10AnalogChainCore analogNormal_; // Phase 2: 2x oversampled chain
    G10AnalogChainCore analogHq_;     // Phase 2: 4x oversampled chain
    G10MiniEqCore miniEq_;        // clean digital correction layer (post-Color, base rate)

    juce::AudioBuffer<float> scratchA_; // clean engine input/output copy
    juce::AudioBuffer<float> scratchB_; // second chain copy (quality transition)

    // Analyzer: lock-free SPSC FIFO (preallocated; layout fixed per
    // prepareToPlay). The audio thread pushes ONLY when an editor is open.
    G10AnalyzerFifo analyzerFifo_;
    std::atomic<bool> analyzerActive_ { false };

    // Level metering handoffs (audio thread writes once per block, UI timer
    // reads; relaxed atomics — no locks, no allocation, no FIFO).
    std::atomic<float> inputMeterDb_ { -300.0f };
    std::atomic<float> outputMeterDb_ { -300.0f };

    float analogMix_ = 0.0f;   // 0 = clean, 1 = analog (10 ms crossfade)
    float qualityMix_ = 0.0f;  // 0 = NORMAL (2x), 1 = HQ (4x) (10 ms crossfade)
    float analogStep_ = 0.0f;  // per-sample crossfade step
    float qualityStep_ = 0.0f; // per-sample crossfade step
};

} // namespace G10
} // namespace APEX

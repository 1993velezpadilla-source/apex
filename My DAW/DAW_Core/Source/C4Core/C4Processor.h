#pragma once
#include <JuceHeader.h>
#include "C4Types.h"
#include "C4TuningProfile.h"
#include "C4EngineCore.h"
#include "C4SpectrumCore.h"

namespace APEX {
namespace C4 {

// ============================================================================
// C4Parameter — hosted parameter for the APEX C4 processor.
//
// Derives from juce::AudioProcessorParameterWithID (JUCE 8.0.12), the same
// class G10 uses: addHostedParameter() compatibility AND the
// dynamic_cast<AudioProcessorParameterWithID*> binding used by the APEX
// chain automation (PluginChainCore::configureSlotAutomation), so every C4
// parameter is bound to the APEX automation system by its stable string ID.
//
// Value mapping (normalized 0..1 <-> units):
//   BandFreq -> Hz, LOG law inside the band's musical range
//   BandGain -> dB in [-15, +15] (continuous)
//   BandQ    -> Q, LOG law inside the band's user range
//   Mode     -> 0/1 (Bell/Shelf, or Bell/HALO for OPEN)
//   Hpf      -> 0 = OFF, (0,1] -> 20..1500 Hz (log)
//   Lpf      -> 0 = OFF, (0,1] -> 1.5..24 kHz (log)
//   Bloom    -> 0..10 (continuous)
//   Trim     -> dB in [-18, +18]
//   Toggle   -> 0/1
//
// Thread contract: host writes setValue() (control thread), audio thread
// reads getValue()/getUnitsValue() — plain float storage, no locks, no
// allocation (canonical JUCE pattern).
// ============================================================================

class C4Parameter final : public juce::AudioProcessorParameterWithID
{
public:
    enum class Kind
    {
        BandFreq,   // Hz, log law inside the band range
        BandGain,   // -15..+15 dB
        BandQ,      // log law inside the band user range
        Mode,       // 0/1
        Hpf,        // 0 = OFF, (0,1] -> 20..1500 Hz log
        Lpf,        // 0 = OFF, (0,1] -> 1.5..24 kHz log
        Bloom,      // 0..10 continuous
        Trim,       // -18..+18 dB
        Toggle      // 0/1
    };

    C4Parameter (const juce::ParameterID& id, const juce::String& name,
                 Kind kind, float minValue, float maxValue, float defaultValue,
                 int band = -1)
        : juce::AudioProcessorParameterWithID (id, name),
          kind_ (kind),
          band_ (band),
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
        case Kind::BandFreq:
            return formatHz (freqHzFromNorm (v));
        case Kind::BandGain:
            return formatDb (bandDbFromNorm (v));
        case Kind::BandQ:
            return "Q " + juce::String (qFromNorm (v), 2);
        case Kind::Mode:
        {
            const bool shelf = v >= 0.5f;
            if (band_ == (int) C4BandId::Open)
                return shelf ? "HALO" : "Bell";
            return shelf ? "Shelf" : "Bell";
        }
        case Kind::Hpf:
        {
            const float hz = hpfHzFromNorm (v);
            return hz <= 0.0f ? "Off" : formatHz (hz);
        }
        case Kind::Lpf:
        {
            const float hz = lpfHzFromNorm (v);
            return hz <= 0.0f ? "Off" : formatHz (hz);
        }
        case Kind::Bloom:
            return juce::String (bloomFromNorm (v), 1);
        case Kind::Trim:
            return formatDb (trimDbFromNorm (v));
        default:
            return v >= 0.5f ? "On" : "Off";
        }
    }

    float getValueForText (const juce::String& text) const override
    {
        if (kind_ == Kind::Toggle || kind_ == Kind::Mode)
            return text.trim().equalsIgnoreCase ("on")
                || text.trim().equalsIgnoreCase ("shelf")
                || text.trim().equalsIgnoreCase ("halo") ? 1.0f : 0.0f;
        if (kind_ == Kind::Hpf || kind_ == Kind::Lpf)
        {
            if (text.trim().equalsIgnoreCase ("off"))
                return 0.0f;
            const float hz = text.trim().getFloatValue();
            if (hz <= 0.0f)
                return 0.0f;
            return kind_ == Kind::Hpf ? hpfNormFromHz (hz) : lpfNormFromHz (hz);
        }
        return toNormalized (text.getFloatValue());
    }

    int getNumSteps() const override
    {
        return (kind_ == Kind::Toggle || kind_ == Kind::Mode)
            ? 2
            : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
    }

    bool isDiscrete() const override
    {
        return kind_ == Kind::Toggle || kind_ == Kind::Mode;
    }

    bool isBoolean() const override
    {
        return kind_ == Kind::Toggle || kind_ == Kind::Mode;
    }

    // ---- C4 helpers ------------------------------------------------------

    /** Current value in product units. */
    float getUnitsValue() const noexcept { return fromNormalized (value_); }

    /** Product-unit range (used by the GUI controls and tests). */
    float getMinValue() const noexcept { return minValue_; }
    float getMaxValue() const noexcept { return maxValue_; }

    bool getBool() const noexcept { return value_ >= 0.5f; }

    Kind getKind() const noexcept { return kind_; }
    int getBand() const noexcept { return band_; }

    // ---- Product-unit laws (also used by tests) --------------------------

    /** HPF/LPF: OFF is exactly normalized 0.0; every REAL cutoff maps to
        (0,1] so the minimum cutoff (20 Hz HPF / 1.5 kHz LPF) never collides
        with the OFF sentinel. */
    static constexpr float kHpfLpfNormFloor = 1.0f / 256.0f;

    static float freqNormFromHz (float hz, float minHz, float maxHz) noexcept
    {
        const float h = juce::jlimit (minHz, maxHz, hz);
        return std::log (h / minHz) / std::log (maxHz / minHz);
    }

    static float freqHzFromNorm (float norm, float minHz, float maxHz) noexcept
    {
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, norm));
    }

    static float qNormFromQ (float q, float minQ, float maxQ) noexcept
    {
        const float qq = juce::jlimit (minQ, maxQ, q);
        return std::log (qq / minQ) / std::log (maxQ / minQ);
    }

    static float qFromNorm (float norm, float minQ, float maxQ) noexcept
    {
        return minQ * std::pow (maxQ / minQ, juce::jlimit (0.0f, 1.0f, norm));
    }

    static float hpfHzFromNorm (float norm) noexcept
    {
        if (norm <= 0.0f)
            return 0.0f;
        const float t = juce::jlimit (0.0f, 1.0f,
                                      (norm - kHpfLpfNormFloor) / (1.0f - kHpfLpfNormFloor));
        return freqHzFromNorm (t, kHpfMinHz, kHpfMaxHz);
    }

    static float hpfNormFromHz (float hz) noexcept
    {
        if (hz <= 0.0f)
            return 0.0f;
        const float t = freqNormFromHz (hz, kHpfMinHz, kHpfMaxHz); // [0,1]
        return kHpfLpfNormFloor + t * (1.0f - kHpfLpfNormFloor);
    }

    static float lpfHzFromNorm (float norm) noexcept
    {
        if (norm <= 0.0f)
            return 0.0f;
        const float t = juce::jlimit (0.0f, 1.0f,
                                      (norm - kHpfLpfNormFloor) / (1.0f - kHpfLpfNormFloor));
        return freqHzFromNorm (t, kLpfMinHz, kLpfMaxHz);
    }

    static float lpfNormFromHz (float hz) noexcept
    {
        if (hz <= 0.0f)
            return 0.0f;
        const float t = freqNormFromHz (hz, kLpfMinHz, kLpfMaxHz); // [0,1]
        return kHpfLpfNormFloor + t * (1.0f - kHpfLpfNormFloor);
    }

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

    float bandDbFromNorm (float v) const noexcept
    {
        return minValue_ + v * (maxValue_ - minValue_);
    }

    float trimDbFromNorm (float v) const noexcept
    {
        return minValue_ + v * (maxValue_ - minValue_);
    }

    float bloomFromNorm (float v) const noexcept
    {
        return minValue_ + v * (maxValue_ - minValue_);
    }

    float freqHzFromNorm (float v) const noexcept
    {
        return freqHzFromNorm (v, minValue_, maxValue_);
    }

    float qFromNorm (float v) const noexcept
    {
        return qFromNorm (v, minValue_, maxValue_);
    }

    float toNormalized (float v) const noexcept
    {
        switch (kind_)
        {
        case Kind::BandFreq:
            return freqNormFromHz (v, minValue_, maxValue_);
        case Kind::BandQ:
            return qNormFromQ (v, minValue_, maxValue_);
        case Kind::Hpf:
            return hpfNormFromHz (v);
        case Kind::Lpf:
            return lpfNormFromHz (v);
        case Kind::Toggle:
        case Kind::Mode:
            return v >= 0.5f ? 1.0f : 0.0f;
        default:
            return (v - minValue_) / (maxValue_ - minValue_);
        }
    }

    float fromNormalized (float v) const noexcept
    {
        switch (kind_)
        {
        case Kind::BandFreq:
            return freqHzFromNorm (v, minValue_, maxValue_);
        case Kind::BandQ:
            return qFromNorm (v, minValue_, maxValue_);
        case Kind::Hpf:
            return hpfHzFromNorm (v);
        case Kind::Lpf:
            return lpfHzFromNorm (v);
        case Kind::Toggle:
        case Kind::Mode:
            return v >= 0.5f ? 1.0f : 0.0f;
        default:
            return minValue_ + v * (maxValue_ - minValue_);
        }
    }

    const Kind kind_;
    const int band_;
    const float minValue_, maxValue_, defaultValue_;
    float value_;
};

// ============================================================================
// C4Processor — the APEX C4 AudioPluginInstance.
//
// Identity:
//   name            "APEX C4"
//   format          "APEX Native"
//   uniqueId        0x4334 ("C4")
//   category        "EQ"
//   latency         0 samples (pure minimum-phase IIR)
//   editor          none in Phase 1 (hasEditor() == false; GUI Phase 7)
//
// State: ValueTree, schema version 1, tolerant restore (unknown fields
// ignored, missing fields keep defaults, out-of-range values clamped,
// non-finite values keep defaults).
//
// Threads:
//   prepareToPlay / releaseResources -> message thread
//   processBlock                     -> audio thread only
//   parameter setValue               -> control thread (JUCE canonical)
//
// Neutral fast path (spec §6): when every band is at 0 dB, trims unity,
// HPF/LPF OFF, BLOOM 0, Auto Gain OFF and the bypass crossfade is settled,
// the block is left untouched: bit-identical output, no filters running,
// no state advancement, no allocations.
//
// Oversized host blocks are processed in bounded chunks of the prepared
// maximum block size (all DSP state is member state, so chunking is
// bit-equivalent to legal smaller blocks).
// ============================================================================

class C4Processor final : public juce::AudioPluginInstance
{
public:
    static constexpr int kUniqueId = 0x4334; // "C4"
    static constexpr const char* kFormatName = "APEX Native";
    static constexpr const char* kPluginName = "APEX C4";
    static constexpr const char* kCategory = "EQ";
    static constexpr const char* kManufacturer = "APEX";
    static constexpr const char* kVersion = "1.0.0";
    static constexpr const char* kFileOrIdentifier = "APEX::C4";

    static constexpr const char* kStateTag = "c4state";
    static constexpr const char* kVersionProp = "version";

    explicit C4Processor (const C4TuningProfile& profile = makeProfileProduction())
        : juce::AudioPluginInstance (juce::AudioProcessor::BusesProperties()
            .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          profile_ (profile)
    {
        for (auto& semanticIndex : hostedParameterSemanticIndexes_)
            semanticIndex = -1;

        auto add = [this] (int semanticIndex,
                           std::unique_ptr<C4Parameter> p) -> C4Parameter*
        {
            auto* raw = p.get();
            params_[semanticIndex] = raw;
            jassert (hostedParameterCount_ < kNumParams);
            hostedParameterSemanticIndexes_[hostedParameterCount_++] = semanticIndex;
            addHostedParameter (std::move (p));
            return raw;
        };

        add (kWeightFreq, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamWeightFreqId, 1), "WEIGHT Freq",
            C4Parameter::Kind::BandFreq, kWeightFreqMinHz, kWeightFreqMaxHz,
            kBandInfos[(int) C4BandId::Weight].defaultFreqHz, (int) C4BandId::Weight));
        add (kWeightGain, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamWeightGainId, 1), "WEIGHT Gain",
            C4Parameter::Kind::BandGain, kGainMinDb, kGainMaxDb, 0.0f, (int) C4BandId::Weight));
        add (kWeightMode, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamWeightModeId, 1), "WEIGHT Mode",
            C4Parameter::Kind::Mode, 0.0f, 1.0f, 0.0f, (int) C4BandId::Weight));

        add (kSculptFreq, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamSculptFreqId, 1), "SCULPT Freq",
            C4Parameter::Kind::BandFreq, kSculptFreqMinHz, kSculptFreqMaxHz,
            kBandInfos[(int) C4BandId::Sculpt].defaultFreqHz, (int) C4BandId::Sculpt));
        add (kSculptGain, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamSculptGainId, 1), "SCULPT Gain",
            C4Parameter::Kind::BandGain, kGainMinDb, kGainMaxDb, 0.0f, (int) C4BandId::Sculpt));
        add (kSculptQ, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamSculptQId, 1), "SCULPT Q",
            C4Parameter::Kind::BandQ, kSculptQMin, kSculptQMax, 1.0f, (int) C4BandId::Sculpt));

        add (kBiteFreq, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamBiteFreqId, 1), "BITE Freq",
            C4Parameter::Kind::BandFreq, kBiteFreqMinHz, kBiteFreqMaxHz,
            kBandInfos[(int) C4BandId::Bite].defaultFreqHz, (int) C4BandId::Bite));
        add (kBiteGain, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamBiteGainId, 1), "BITE Gain",
            C4Parameter::Kind::BandGain, kGainMinDb, kGainMaxDb, 0.0f, (int) C4BandId::Bite));
        add (kBiteQ, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamBiteQId, 1), "BITE Q",
            C4Parameter::Kind::BandQ, kBiteQMin, kBiteQMax, 1.0f, (int) C4BandId::Bite));

        add (kOpenFreq, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamOpenFreqId, 1), "OPEN Freq",
            C4Parameter::Kind::BandFreq, kOpenFreqMinHz, kOpenFreqMaxHz,
            kBandInfos[(int) C4BandId::Open].defaultFreqHz, (int) C4BandId::Open));
        add (kOpenGain, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamOpenGainId, 1), "OPEN Gain",
            C4Parameter::Kind::BandGain, kGainMinDb, kGainMaxDb, 0.0f, (int) C4BandId::Open));
        add (kOpenMode, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamOpenModeId, 1), "OPEN Mode",
            C4Parameter::Kind::Mode, 0.0f, 1.0f, 0.0f, (int) C4BandId::Open));

        add (kHpf, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamHpfId, 1), "HPF",
            C4Parameter::Kind::Hpf, 0.0f, 1.0f, 0.0f));
        add (kLpf, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamLpfId, 1), "LPF",
            C4Parameter::Kind::Lpf, 0.0f, 1.0f, 0.0f));
        add (kBloom, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamBloomId, 1), "BLOOM",
            C4Parameter::Kind::Bloom, kBloomMin, kBloomMax, kBloomDefault));

        add (kInput, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamInputId, 1), "Input",
            C4Parameter::Kind::Trim, kTrimMinDb, kTrimMaxDb, 0.0f));
        add (kOutput, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamOutputId, 1), "Output",
            C4Parameter::Kind::Trim, kTrimMinDb, kTrimMaxDb, 0.0f));
        add (kAutoGain, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamAutoGainId, 1), "Auto Gain",
            C4Parameter::Kind::Toggle, 0.0f, 1.0f, 0.0f));
        add (kBypass, std::make_unique<C4Parameter> (
            juce::ParameterID (kParamBypassId, 1), "Bypass",
            C4Parameter::Kind::Toggle, 0.0f, 1.0f, 0.0f));

        jassert (getParameters().size() == kNumParams);
        jassert (hostedParameterCount_ == kNumParams);
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
        setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);

        const int numChannels = juce::jmax (1, getMainBusNumInputChannels());
        engine_.prepare (sampleRate, samplesPerBlock, numChannels, profile_);

        // Spectrum Flag (Phase 6): re-prepare the analyzer for this rate and
        // discard any stale spectra (message thread; audio is stopped here).
        spectrum_.prepare (sampleRate);

        // Scratch for the bypass crossfade (preallocated; never allocated on
        // the audio thread).
        scratchA_.setSize (numChannels, samplesPerBlock, false, false, true);

        bypassStep_ = 1.0f / (float) juce::jmax (1.0, sampleRate * 0.010);
        bypassMix_ = 0.0f;

        setLatencySamples (0);
    }

    void releaseResources() override
    {
        engine_.reset();
    }

    // ---- Audio ------------------------------------------------------------

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxChannels);
        const int numSamples = buffer.getNumSamples();
        if (numChannels <= 0 || numSamples <= 0)
            return;

        // Spectrum Flag tap (Phase 6): read-only, mono-mix copy of the input.
        // Zero allocation; skipped entirely while the flag is closed.
        spectrum_.pushPre (buffer, numChannels, numSamples);

        // ---- Adopt control-plane targets (plain float reads) --------------
        const float bandGainDb[kNumBands] =
        {
            params_[kWeightGain]->getUnitsValue(),
            params_[kSculptGain]->getUnitsValue(),
            params_[kBiteGain]->getUnitsValue(),
            params_[kOpenGain]->getUnitsValue()
        };

        for (int b = 0; b < kNumBands; ++b)
        {
            const int freqIdx = ((C4BandId) b == C4BandId::Weight) ? kWeightFreq
                              : ((C4BandId) b == C4BandId::Sculpt) ? kSculptFreq
                              : ((C4BandId) b == C4BandId::Bite)   ? kBiteFreq
                              :                                      kOpenFreq;
            const int modeIdx = ((C4BandId) b == C4BandId::Weight) ? kWeightMode
                              : ((C4BandId) b == C4BandId::Open)    ? kOpenMode
                              :                                       -1;
            engine_.setBandFreqTargetHz (b, params_[freqIdx]->getUnitsValue());
            engine_.setBandGainTargetDb (b, bandGainDb[b]);
            engine_.setBandModeTarget (b, modeIdx >= 0 && params_[modeIdx]->getBool());
        }

        engine_.setBandQTarget ((int) C4BandId::Sculpt, params_[kSculptQ]->getUnitsValue());
        engine_.setBandQTarget ((int) C4BandId::Bite,   params_[kBiteQ]->getUnitsValue());

        engine_.setHpfTarget (C4Parameter::hpfHzFromNorm (params_[kHpf]->getValue()),
                              params_[kHpf]->getValue() > 0.0f);
        engine_.setLpfTarget (C4Parameter::lpfHzFromNorm (params_[kLpf]->getValue()),
                              params_[kLpf]->getValue() > 0.0f);

        engine_.setInputTargetDb (params_[kInput]->getUnitsValue());
        engine_.setOutputTargetDb (params_[kOutput]->getUnitsValue());
        engine_.setBloomTarget01 (params_[kBloom]->getValue()); // 0..1 norm
        engine_.setAutoGainTarget (params_[kAutoGain]->getBool());

        const bool bypassTarget = params_[kBypass]->getBool();

        // Auto Gain (candidate law, spec §5): mean band-gain compensation,
        // profile-weighted. Keeps level-matched comparisons honest. The
        // compensation is smoothed by the output trim smoother (10 ms).
        float autoGainCompDb = 0.0f;
        if (params_[kAutoGain]->getBool())
        {
            float sum = 0.0f;
            for (float db : bandGainDb)
                sum += db;
            autoGainCompDb = -profile_.autoGainStrength * (sum / (float) kNumBands);
        }

        engine_.adoptTargets (autoGainCompDb);

        // ---- Settled fast paths -------------------------------------------
        const bool bypassSettledOn  = bypassTarget && bypassMix_ >= 0.999f;
        const bool bypassSettledOff = ! bypassTarget && bypassMix_ <= 0.001f;

        if (bypassSettledOn)
        {
            // Host bypass: dry pass-through, buffer untouched. The buffer IS
            // the true output — tap it for POST before returning.
            spectrum_.pushPost (buffer, numChannels, numSamples);
            return;
        }

        if (bypassSettledOff && engine_.isSettledNeutral())
        {
            // NEUTRAL FAST PATH (spec §6): bit-identical wire; the buffer is
            // the true output, so POST = PRE content. Tap and return.
            spectrum_.pushPost (buffer, numChannels, numSamples);
            return;
        }

        // ---- Bounded chunk processing (oversized host blocks) -------------
        const int chunkCapacity = juce::jmax (1, scratchA_.getNumSamples());
        int offset = 0;
        while (offset < numSamples)
        {
            const int chunk = juce::jmin (chunkCapacity, numSamples - offset);
            processChunk (buffer, numChannels, offset, chunk, bypassTarget);
            offset += chunk;
        }

        // Spectrum Flag tap (Phase 6): the true output after the engine and
        // the bypass crossfade.
        spectrum_.pushPost (buffer, numChannels, numSamples);
    }

    void processChunk (juce::AudioBuffer<float>& buffer, int numChannels,
                       int offset, int numSamples, bool bypassTarget) noexcept
    {
        jassert (numSamples <= scratchA_.getNumSamples());

        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                        numChannels, offset, numSamples);

        // Dry snapshot for the bypass crossfade.
        for (int ch = 0; ch < numChannels; ++ch)
            scratchA_.copyFrom (ch, 0, chunk, ch, 0, numSamples);

        float* chans[kMaxChannels] = { nullptr, nullptr };
        for (int ch = 0; ch < numChannels; ++ch)
            chans[ch] = chunk.getWritePointer (ch);

        engine_.processBlock (chans, numChannels, numSamples);

        // Bypass crossfade: out = wet * (1 - m) + dry * m, m advances per
        // sample (~10 ms ramp, block-size independent). Settled states are
        // handled by the early returns above; this loop only runs while the
        // crossfade must move.
        if (bypassMix_ < 0.999f || bypassTarget)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float* dst = chunk.getWritePointer (ch);
                    const float* dry = scratchA_.getReadPointer (ch);
                    dst[s] = dst[s] * (1.0f - bypassMix_) + dry[s] * bypassMix_;
                }
                bypassMix_ = bypassTarget ? juce::jmin (1.0f, bypassMix_ + bypassStep_)
                                          : juce::jmax (0.0f, bypassMix_ - bypassStep_);
            }
        }
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
    juce::AudioProcessorParameter* getBypassParameter() const override { return params_[kBypass]; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; } // Phase 7: C4Editor (Source/C4UI)
    juce::AudioProcessorEditor* createEditor() override; // C4Processor.cpp (Phase 7 GUI)

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
        // Spectrum Flag mode (processor state; 0..3, tolerant restore below).
        state.setProperty ("c4.spectrum", (int) spectrum_.getTapMode(), nullptr);

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
        // defaults, out-of-range values clamped by C4Parameter::setValue,
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

        // Spectrum Flag mode restore (clamped; missing/foreign values keep
        // the current mode — tolerant like the parameter restore).
        if (state.hasProperty ("c4.spectrum"))
        {
            const int raw = (int) state.getProperty ("c4.spectrum", 0);
            const int clamped = juce::jlimit (0, 3, raw);
            spectrum_.setTapMode ((C4SpectrumTapMode) clamped);
        }
    }

    // ---- Introspection (tests / future UI) --------------------------------

    C4Parameter* getC4Parameter (int index) noexcept
    {
        return (index >= 0 && index < kNumParams) ? params_[index] : nullptr;
    }

    const C4Parameter* getC4Parameter (int index) const noexcept
    {
        return (index >= 0 && index < kNumParams) ? params_[index] : nullptr;
    }

    int getSemanticParameterIndex (int hostedParameterIndex) const noexcept
    {
        return hostedParameterIndex >= 0 && hostedParameterIndex < hostedParameterCount_
            ? hostedParameterSemanticIndexes_[hostedParameterIndex]
            : -1;
    }

    C4EngineCore& getEngine() noexcept { return engine_; }
    const C4EngineCore& getEngine() const noexcept { return engine_; }

    const C4TuningProfile& getProfile() const noexcept { return profile_; }

    // ---- Spectrum Flag (Phase 6; message/UI thread — NEVER the audio thread) --
    // Observational analyzer: Closed (default) / Pre / Post / Both. Stored as
    // processor state, NOT a hosted parameter (the 19-parameter ABI is frozen).

    void setSpectrumMode (C4SpectrumTapMode mode) noexcept { spectrum_.setTapMode (mode); }
    C4SpectrumTapMode getSpectrumMode() const noexcept { return spectrum_.getTapMode(); }

    C4SpectrumCore& getSpectrumCore() noexcept { return spectrum_; }
    const C4SpectrumCore& getSpectrumCore() const noexcept { return spectrum_; }

    /** Switch the tuning profile (control thread; re-times smoothers and
        re-adopts targets at the next block). Used by A/B/C tuning tests. */
    void setProfile (const C4TuningProfile& profile, double sampleRate) noexcept
    {
        profile_ = profile;
        engine_.setProfile (profile, sampleRate);
    }

    float getBypassMix() const noexcept { return bypassMix_; }

    static const char* getParamId (int index) noexcept
    {
        switch (index)
        {
            case kWeightFreq: return kParamWeightFreqId;
            case kWeightGain: return kParamWeightGainId;
            case kWeightMode: return kParamWeightModeId;
            case kSculptFreq: return kParamSculptFreqId;
            case kSculptGain: return kParamSculptGainId;
            case kSculptQ:    return kParamSculptQId;
            case kBiteFreq:   return kParamBiteFreqId;
            case kBiteGain:   return kParamBiteGainId;
            case kBiteQ:      return kParamBiteQId;
            case kOpenFreq:   return kParamOpenFreqId;
            case kOpenGain:   return kParamOpenGainId;
            case kOpenMode:   return kParamOpenModeId;
            case kHpf:        return kParamHpfId;
            case kLpf:        return kParamLpfId;
            case kBloom:      return kParamBloomId;
            case kInput:      return kParamInputId;
            case kOutput:     return kParamOutputId;
            case kAutoGain:   return kParamAutoGainId;
            case kBypass:     return kParamBypassId;
            default:          return nullptr;
        }
    }

private:
    C4Parameter* params_[kNumParams] = {};
    int hostedParameterSemanticIndexes_[kNumParams] = {};
    int hostedParameterCount_ = 0;

    C4TuningProfile profile_;
    C4EngineCore engine_;

    // Spectrum Flag (Phase 6): observational analyzer (tap + worker + snapshot).
    C4SpectrumCore spectrum_;

    juce::AudioBuffer<float> scratchA_; // dry snapshot for the bypass crossfade

    float bypassMix_ = 0.0f;  // 0 = engine in path, 1 = bypass dry
    float bypassStep_ = 0.0f; // per-sample crossfade step (~10 ms)
};

} // namespace C4
} // namespace APEX

#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"
#include "../../../Source/PluginHostCore/ApexNativePluginFormat.h"

#include <cmath>
#include <limits>
#include <set>

namespace
{

using namespace APEX::ParametricEQ;

void setUnits (Processor& processor, int index, float units)
{
    auto* parameter = processor.getParametricEQParameter (index);
    parameter->setValue (parameter->toNormalised (units));
}

juce::MemoryBlock serialise (Processor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    return state;
}

} // namespace

class ParametricEQStateTests final : public juce::UnitTest
{
public:
    ParametricEQStateTests()
        : UnitTest ("ParametricEQ.State", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testPrismReplacesNativeEntryWithoutChangingSessionIdentity();
        testFrozenAbiAndAppendedPlacements();
        testAutoThresholdProcessesRealAudioAndMigratesLegacyState();
        testPlacementParameterContract();
        testVersionTwoRoundTrip();
        testVersionOneMigrationDefaultsPlacementsToStereo();
        testMalformedAndTolerantPlacementState();
        testAuditionIsAbsentFromState();
    }

private:
    void testAutoThresholdProcessesRealAudioAndMigratesLegacyState()
    {
        beginTest ("AUTO threshold is audible on real program, off is"
                   " legacy-neutral, state and automation remain canonical");
        auto render = [] (bool useAuto) {
            Processor processor;
            const int band = 0;
            setUnits (processor, parameterIndex (
                band, BandParameterOffset::Enabled), 1.0f);
            setUnits (processor, parameterIndex (
                band, BandParameterOffset::Shape),
                static_cast<float> (FilterShape::Bell));
            setUnits (processor, parameterIndex (
                band, BandParameterOffset::Frequency), 1000.0f);
            setUnits (processor, dynamicParameterIndex (
                band, DynamicBandOffset::Enable), 1.0f);
            setUnits (processor, dynamicParameterIndex (
                band, DynamicBandOffset::Threshold), 0.0f);
            setUnits (processor, dynamicParameterIndex (
                band, DynamicBandOffset::Range), 18.0f);
            setUnits (processor, dynamicParameterIndex (
                band, DynamicBandOffset::Attack), .001f);
            setUnits (processor, dynamicParameterIndex (
                band, DynamicBandOffset::Release), .03f);
            setUnits (processor,dynamicAutoThresholdParameterIndex (band),
                      useAuto ? 1.0f : 0.0f);
            processor.prepareToPlay (48000.0, 128);
            juce::AudioBuffer<float> audio (2, 128);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int block = 0; block < 300; ++block)
            {
                const double amplitude = block < 160 ? .02 : .20;
                for (int sample = 0; sample < 128; ++sample)
                {
                    const auto value = static_cast<float> (
                        amplitude * std::sin (phase));
                    phase += juce::MathConstants<double>::twoPi
                           * 1000.0 / 48000.0;
                    audio.setSample (0, sample, value);
                    audio.setSample (1, sample, value);
                }
                processor.processBlock (audio, midi);
            }
            const auto measured = processor.getDynamicGainDbForTesting (band);
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            return std::pair<double, juce::MemoryBlock> {
                measured, std::move (state) };
        };
        const auto manual = render (false);
        const auto automatic = render (true);
        expect (std::abs (manual.first) < .1,
            "default manual 0 dB threshold has no false compression");
        expect (automatic.first < -.5,
            "real detector-driven AUTO threshold reacts to signal changes");
        Processor restored;
        restored.setStateInformation (automatic.second.getData(),
            static_cast<int> (automatic.second.getSize()));
        expect (restored.getParametricEQParameter (
            dynamicAutoThresholdParameterIndex (0))->getBool(),
            "AUTO threshold survives actual full project state roundtrip");
        auto legacy = juce::ValueTree::readFromData (
            automatic.second.getData(), automatic.second.getSize());
        for (int band = 0; band < kMaxBands; ++band)
            legacy.removeProperty (juce::Identifier (Processor::getParameterId (
                dynamicAutoThresholdParameterIndex (band))), nullptr);
        juce::MemoryBlock olderData;
        juce::MemoryOutputStream out (olderData, false);
        legacy.writeToStream (out);
        restored.setStateInformation (olderData.getData(),
            static_cast<int> (olderData.getSize()));
        expect (! restored.getParametricEQParameter (
            dynamicAutoThresholdParameterIndex (0))->getBool(),
            "legacy sessions force AUTO off even after another session"
            " had it enabled in a reused plugin instance");
    }

    void testPrismReplacesNativeEntryWithoutChangingSessionIdentity()
    {
        beginTest ("Prism replaces the old native EQ without a duplicate or broken session ID");
        DAW::ApexNativePluginFormat nativeFormat;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        nativeFormat.findAllTypesForFile (descriptions, {});
        int eqCount = 0;
        for (auto* entry : descriptions)
        {
            if (entry->fileOrIdentifier != Processor::kFileOrIdentifier)
                continue;
            ++eqCount;
            expectEquals (entry->name, juce::String ("APEX Prism EQ"));
            expectEquals (entry->uniqueId, Processor::kUniqueId);
            expectEquals (entry->pluginFormatName, juce::String ("APEX Native"));
        }
        expectEquals (eqCount, 1, "native plugin catalog exposes exactly one EQ");
        auto legacy = NativePluginFormat::createDescription();
        legacy.name = "APEX Parametric EQ";
        legacy.descriptiveName = legacy.name;
        expect (NativePluginFormat::isDescription (legacy),
                "old EQ description remains valid by immutable format/UID/path");
        expect (nativeFormat.doesPluginStillExist (legacy),
                "an EQ instance persisted under the old display name still loads");
    }

    void testFrozenAbiAndAppendedPlacements()
    {
        beginTest ("Phase 11 AUTO threshold: 375 frozen IDs plus 24 append-only IDs");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        expectEquals (processor.getParameters().size(), kNumParameters);
        expectEquals (kNumParameters, 399);

        std::set<std::string> ids;
        for (int index = 0; index < kNumParameters; ++index)
        {
            // Derive every identity literally, not through the production
            // helper, so a reordered implementation cannot pass.
            juce::String expected;
            if (index == kBandParameterCount)
                expected = "peq.design";
            else if (index == kBandParameterCount + 1)
                expected = "peq.bypass";
            else if (index >= kFirstPlacementParameter && index < kFirstDynamicParameter)
                expected = "peq.band"
                         + juce::String (index - kFirstPlacementParameter + 1)
                               .paddedLeft ('0', 2)
                         + ".placement";
            else if (index >= kFirstDynamicParameter
                     && index < kDynamicDetectorParameter)
            {
                const int local = index - kFirstDynamicParameter;
                const int band = local / kDynamicParametersPerBand;
                const int offset = local % kDynamicParametersPerBand;
                static const char* dynamicSuffixes[] = {
                    "enable", "threshold", "range", "attack", "release"
                };
                expected = "peq.band"
                         + juce::String (band + 1).paddedLeft ('0', 2)
                         + ".dyn." + dynamicSuffixes[offset];
            }
            else if (index == kDynamicDetectorParameter)
                expected = "peq.dyn.detector";
            else if (index == kDynamicSidechainParameter)
                expected = "peq.dyn.sidechain";
            else if (index == kDynamicLinkParameter)
                expected = "peq.dyn.link";
            else if (index == kPhaseModeParameter)
                expected = "peq.phase";
            else if (index == kCharacterModeParameter)
                expected = "peq.character";
            else if (index >= kFirstDynamicFilterParameter
                     && index < kOutputGainParameter)
                expected = "peq.band"
                         + juce::String (index - kFirstDynamicFilterParameter + 1)
                               .paddedLeft ('0', 2)
                         + ".dyn.filter";
            else if (index == kOutputGainParameter)
                expected = "peq.output.gain";
            else if (index == kOutputPanParameter)
                expected = "peq.output.pan";
            else if (index == kOutputPhaseInvertParameter)
                expected = "peq.output.phaseInvert";
            else if (index == kOutputAutoGainParameter)
                expected = "peq.output.autoGain";
            else if (index == kOutputGainScaleParameter)
                expected = "peq.output.gainScale";
            else if (index == kSpectralArmParameter)
                expected = "peq.spectral.arm";
            else if (index >= kFirstSpectralBandParameter
                     && index < kSpectralDensityParameter)
                expected = "peq.band"
                    + juce::String (index - kFirstSpectralBandParameter + 1)
                          .paddedLeft ('0', 2)
                    + ".spectral.enable";
            else if (index == kSpectralDensityParameter)
                expected = "peq.spectral.density";
            else if (index == kSpectralTiltParameter)
                expected = "peq.spectral.tilt";
            else if (index >= kFirstDynamicAutoThresholdParameter)
                expected = "peq.band"
                    + juce::String (index - kFirstDynamicAutoThresholdParameter + 1)
                        .paddedLeft ('0', 2)
                    + ".dynamic.autoThreshold";
            else
            {
                const int band = index / 7;
                const int offset = index % 7;
                static const char* suffixes[] = {
                    "enabled", "bypass", "shape", "frequency", "gain", "q", "slope"
                };
                expected = "peq.band" + juce::String (band + 1).paddedLeft ('0', 2)
                         + "." + suffixes[offset];
            }

            auto* parameter = processor.getParametricEQParameter (index);
            expect (parameter != nullptr, "parameter " + juce::String (index));
            if (parameter == nullptr)
                continue;
            expectEquals (parameter->paramID, expected,
                          "stable ID mismatch at " + juce::String (index));
            expect (ids.insert (parameter->paramID.toStdString()).second,
                    "duplicate stable ID " + parameter->paramID);
            expect (parameter->isStorageLockFree(),
                    "atomic storage must be lock-free: " + parameter->paramID);
        }
        expectEquals (ids.size(), static_cast<std::size_t> (399));
        expectEquals (Processor::getParameterId (170),
                      juce::String ("peq.band01.placement"));
        expectEquals (Processor::getParameterId (193),
                      juce::String ("peq.band24.placement"));
        expectEquals (Processor::getParameterId (194),
                      juce::String ("peq.band01.dyn.enable"));
        expectEquals (Processor::getParameterId (198),
                      juce::String ("peq.band01.dyn.release"));
        expectEquals (Processor::getParameterId (313),
                      juce::String ("peq.band24.dyn.release"));
        expectEquals (Processor::getParameterId (314),
                      juce::String ("peq.dyn.detector"));
        expectEquals (Processor::getParameterId (315),
                      juce::String ("peq.dyn.sidechain"));
        expectEquals (Processor::getParameterId (316),
                      juce::String ("peq.dyn.link"));
        expectEquals (Processor::getParameterId (317),
                      juce::String ("peq.phase"));
        expectEquals (Processor::getParameterId (318),
                      juce::String ("peq.character"));
        expectEquals (Processor::getParameterId (319),
                      juce::String ("peq.band01.dyn.filter"));
        expectEquals (Processor::getParameterId (342),
                      juce::String ("peq.band24.dyn.filter"));
        expectEquals (Processor::getParameterId (343),
                      juce::String ("peq.output.gain"));
        expectEquals (Processor::getParameterId (344),
                      juce::String ("peq.output.pan"));
        expectEquals (Processor::getParameterId (345),
                      juce::String ("peq.output.phaseInvert"));
        expectEquals (Processor::getParameterId (346),
                      juce::String ("peq.output.autoGain"));
        expectEquals (Processor::getParameterId (347),
                      juce::String ("peq.output.gainScale"));
        expectEquals (Processor::getParameterId (348),
                      juce::String ("peq.spectral.arm"));
        expectEquals (Processor::getParameterId (349),
                      juce::String ("peq.band01.spectral.enable"));
        expectEquals (Processor::getParameterId (372),
                      juce::String ("peq.band24.spectral.enable"));
        expectEquals (Processor::getParameterId (373),
                      juce::String ("peq.spectral.density"));
        expectEquals (Processor::getParameterId (374),
                      juce::String ("peq.spectral.tilt"));
        expect (processor.getBypassParameter()
                == processor.getParametricEQParameter (kGlobalBypassParameter));
    }

    void testPlacementParameterContract()
    {
        beginTest ("placement parameters are discrete five-step choices");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        for (int band = 0; band < kMaxBands; ++band)
        {
            auto* parameter = processor.getParametricEQParameter (
                placementParameterIndex (band));
            expect (parameter->getKind() == ParametricEQParameter::Kind::Placement);
            expectEquals (parameter->getNumSteps(), kChannelPlacementCount);
            expect (parameter->isDiscrete());
            expect (! parameter->isBoolean());
            expectEquals (parameter->getChoiceIndex(), 0, "default must be Stereo");
            for (int choice = 0; choice < kChannelPlacementCount; ++choice)
            {
                parameter->setValue (parameter->toNormalised (
                    static_cast<float> (choice)));
                expectEquals (parameter->getChoiceIndex(), choice);
                expectEquals (parameter->getText (parameter->getValue(), 32),
                              juce::String (channelPlacementName (
                                  static_cast<ChannelPlacement> (choice))));
                expectEquals (parameter->getText (
                                  parameter->getValueForText (
                                      channelPlacementName (
                                          static_cast<ChannelPlacement> (choice))),
                                  32),
                              juce::String (channelPlacementName (
                                  static_cast<ChannelPlacement> (choice))));
            }
            parameter->setValue (0.0f);
        }
    }

    void testVersionTwoRoundTrip()
    {
        beginTest ("version-2 state round-trips every placement");
        auto sourceStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        for (int band = 0; band < kMaxBands; ++band)
        {
            setUnits (source, parameterIndex (band, BandParameterOffset::Enabled),
                      (band & 1) != 0 ? 1.0f : 0.0f);
            setUnits (source, parameterIndex (band, BandParameterOffset::Frequency),
                      50.0f * (band + 1));
            setUnits (source, parameterIndex (band, BandParameterOffset::Gain),
                      (band & 1) != 0 ? 4.0f : -4.0f);
            setUnits (source, placementParameterIndex (band),
                      static_cast<float> (band % kChannelPlacementCount));
        }
        setUnits (source, kDesignModeParameter, 1.0f);

        const auto block = serialise (source);
        const auto tree = juce::ValueTree::readFromData (
            block.getData(), static_cast<std::size_t> (block.getSize()));
        expect (tree.isValid());
        if (tree.isValid())
            expectEquals (static_cast<int> (tree.getProperty (Processor::kVersionProperty)),
                          Processor::kStateVersion);

        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (block.getData(),
                                     static_cast<int> (block.getSize()));
        for (int index = 0; index < kNumParameters; ++index)
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (index)->getValue(),
                source.getParametricEQParameter (index)->getValue(), 0.0f,
                "state mismatch at " + Processor::getParameterId (index));
    }

    void testVersionOneMigrationDefaultsPlacementsToStereo()
    {
        beginTest ("version-1 state loads with Stereo placement defaults");
        juce::ValueTree legacy (Processor::kStateTag);
        legacy.setProperty (Processor::kVersionProperty, 1, nullptr);
        for (int index = 0; index < kFirstPlacementParameter; ++index)
            legacy.setProperty (juce::Identifier (Processor::getParameterId (index)),
                                0.25f, nullptr);
        legacy.setProperty (Processor::getParameterId (parameterIndex (
            3, BandParameterOffset::Gain)), 0.9f, nullptr);
        legacy.setProperty (Processor::getParameterId (parameterIndex (
            3, BandParameterOffset::Frequency)), 0.33f, nullptr);
        juce::MemoryBlock legacyBlock;
        juce::MemoryOutputStream legacyStream (legacyBlock, false);
        legacy.writeToStream (legacyStream);

        auto freshStorage = std::make_unique<Processor>();
        auto& fresh = *freshStorage;
        fresh.setStateInformation (legacyBlock.getData(),
                                  static_cast<int> (legacyBlock.getSize()));
        expectEquals (fresh.getParametricEQParameter (parameterIndex (
                          3, BandParameterOffset::Gain))->getValue(), 0.9f);
        expectEquals (fresh.getParametricEQParameter (parameterIndex (
                          3, BandParameterOffset::Frequency))->getValue(), 0.33f);
        for (int band = 0; band < kMaxBands; ++band)
            expectEquals (fresh.getParametricEQParameter (
                              placementParameterIndex (band))->getChoiceIndex(),
                          0, "missing placement must default to Stereo");

        const auto resaved = serialise (fresh);
        const auto resavedTree = juce::ValueTree::readFromData (
            resaved.getData(), static_cast<std::size_t> (resaved.getSize()));
        expect (resavedTree.isValid());
        if (resavedTree.isValid())
        {
            expectEquals (static_cast<int> (resavedTree.getProperty (
                              Processor::kVersionProperty)), Processor::kStateVersion);
            for (int band = 0; band < kMaxBands; ++band)
                expect (resavedTree.hasProperty (juce::Identifier (
                            Processor::getParameterId (placementParameterIndex (band)))),
                        "resaved state must emit every placement property");
        }

        // A pre-modified instance keeps its placement when a legacy state
        // does not carry that field (tolerant, non-destructive loading).
        auto modifiedStorage = std::make_unique<Processor>();
        auto& modified = *modifiedStorage;
        modified.setStateInformation (legacyBlock.getData(),
                                     static_cast<int> (legacyBlock.getSize()));
        setUnits (modified, placementParameterIndex (2),
                  static_cast<float> (ChannelPlacement::Side));
        modified.setStateInformation (legacyBlock.getData(),
                                     static_cast<int> (legacyBlock.getSize()));
        expectEquals (modified.getParametricEQParameter (
                          placementParameterIndex (2))->getChoiceIndex(),
                      static_cast<int> (ChannelPlacement::Side),
                      "tolerant load must not clobber an untouched placement");
    }

    void testMalformedAndTolerantPlacementState()
    {
        beginTest ("malformed placement values sanitize without breaking state");
        auto makeState = [] (float bandTwoPlacement)
        {
            juce::ValueTree tree (Processor::kStateTag);
            tree.setProperty (Processor::kVersionProperty,
                              Processor::kStateVersion, nullptr);
            tree.setProperty (
                juce::Identifier (Processor::getParameterId (
                    placementParameterIndex (1))), bandTwoPlacement, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            tree.writeToStream (stream);
            return block;
        };

        {
            const auto block = makeState (7.0f);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectEquals (processorStorage->getParametricEQParameter (
                              placementParameterIndex (1))->getChoiceIndex(),
                          static_cast<int> (ChannelPlacement::Side),
                          "out-of-range placement must clamp to Side");
        }
        {
            const auto block = makeState (-999.0f);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectEquals (processorStorage->getParametricEQParameter (
                              placementParameterIndex (1))->getChoiceIndex(), 0,
                          "negative normalized must clamp to Stereo");
        }
        {
            const auto block = makeState (std::numeric_limits<float>::quiet_NaN());
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectEquals (processorStorage->getParametricEQParameter (
                              placementParameterIndex (1))->getChoiceIndex(), 0,
                          "NaN placement must keep the Stereo default");
        }
        {
            juce::ValueTree foreign ("foreignstate");
            foreign.setProperty (Processor::getParameterId (
                                     placementParameterIndex (0)), 1.0f, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            foreign.writeToStream (stream);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectEquals (processorStorage->getParametricEQParameter (
                              placementParameterIndex (0))->getChoiceIndex(), 0,
                          "foreign schema must not mutate placement");
        }
    }

    void testAuditionIsAbsentFromState()
    {
        beginTest ("state trees contain parameters and analyzer only, never audition");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        const auto block = serialise (processor);
        const auto tree = juce::ValueTree::readFromData (
            block.getData(), static_cast<std::size_t> (block.getSize()));
        expect (tree.isValid());
        if (tree.isValid())
        {
            expectEquals (tree.getNumProperties(), kNumParameters + 2,
                          "version + analyzer + all hosted parameters");
            for (int property = 0; property < tree.getNumProperties(); ++property)
            {
                const auto propertyName = tree.getPropertyName (property).toString();
                expect (! propertyName.containsIgnoreCase ("audition")
                             && ! propertyName.containsIgnoreCase ("solo"),
                        "no audition/solo property may exist: " + propertyName);
            }
        }
    }
};

static ParametricEQStateTests parametricEQStateTests;

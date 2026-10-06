#include "ParametricEQTestUtils.h"
#include "../../../Source/ParametricEQCore/ParametricEQProcessor.h"

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
        testFrozenAbiAndAppendedPlacements();
        testPlacementParameterContract();
        testVersionTwoRoundTrip();
        testVersionOneMigrationDefaultsPlacementsToStereo();
        testMalformedAndTolerantPlacementState();
        testAuditionIsAbsentFromState();
    }

private:
    void testFrozenAbiAndAppendedPlacements()
    {
        beginTest ("frozen 170-prefix plus append-only Phase 3/5/6/7/8 parameters = 343 stable IDs");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        expectEquals (processor.getParameters().size(), kNumParameters);
        expectEquals (kNumParameters, 343);

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
                     && index < kNumParameters)
                expected = "peq.band"
                         + juce::String (index - kFirstDynamicFilterParameter + 1)
                               .paddedLeft ('0', 2)
                         + ".dyn.filter";
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
        expectEquals (ids.size(), static_cast<std::size_t> (343));
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

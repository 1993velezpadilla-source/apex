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

void configureBell (Processor& processor, int band, float frequency, float gain,
                    float q, ChannelPlacement placement = ChannelPlacement::Stereo)
{
    setUnits (processor, parameterIndex (band, BandParameterOffset::Enabled), 1.0f);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Shape),
              static_cast<float> (FilterShape::Bell));
    setUnits (processor, parameterIndex (band, BandParameterOffset::Frequency),
              frequency);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Gain), gain);
    setUnits (processor, parameterIndex (band, BandParameterOffset::Q), q);
    setUnits (processor, placementParameterIndex (band),
              static_cast<float> (placement));
}

juce::MemoryBlock serialise (Processor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    return state;
}

} // namespace

class ParametricEQDynamicStateTests final : public juce::UnitTest
{
public:
    ParametricEQDynamicStateTests()
        : UnitTest ("ParametricEQ.DynamicState", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testV3RoundTripPreservesAll317();
        testV2StateMigratesWithDeterministicDefaults();
        testMalformedDynamicStateIsTotal();
        testRuntimeDynamicsNeverSerialized();
    }

private:
    void testV3RoundTripPreservesAll317()
    {
        beginTest ("v3 round-trips every dynamic parameter");
        auto sourceStorage = std::make_unique<Processor>();
        auto& source = *sourceStorage;
        for (int band = 0; band < kMaxBands; ++band)
        {
            configureBell (source, band, 100.0f * (band + 1), 3.0f, 1.0f);
            setUnits (source, dynamicParameterIndex (band, DynamicBandOffset::Enable),
                      (band & 1) != 0 ? 1.0f : 0.0f);
            setUnits (source, dynamicParameterIndex (band, DynamicBandOffset::Threshold),
                      -10.0f - band);
            setUnits (source, dynamicParameterIndex (band, DynamicBandOffset::Range),
                      (band & 1) != 0 ? 8.0f : -6.0f);
            setUnits (source, dynamicParameterIndex (band, DynamicBandOffset::Attack),
                      0.002f + 0.001f * band);
            setUnits (source, dynamicParameterIndex (band, DynamicBandOffset::Release),
                      0.05f + 0.01f * band);
        }
        setUnits (source, kDynamicDetectorParameter, 0.0f); // Peak
        setUnits (source, kDynamicSidechainParameter, 1.0f); // External
        setUnits (source, kDynamicLinkParameter, 0.0f); // Unlinked

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

    void testV2StateMigratesWithDeterministicDefaults()
    {
        beginTest ("version-2 state loads with deterministic dynamic defaults");
        juce::ValueTree legacy (Processor::kStateTag);
        legacy.setProperty (Processor::kVersionProperty, 2, nullptr);
        for (int index = 0; index < kFirstDynamicParameter; ++index)
            legacy.setProperty (juce::Identifier (Processor::getParameterId (index)),
                                0.25f, nullptr);
        legacy.setProperty (Processor::getParameterId (parameterIndex (
            3, BandParameterOffset::Gain)), 0.9f, nullptr);
        juce::MemoryBlock legacyBlock;
        juce::MemoryOutputStream legacyStream (legacyBlock, false);
        legacy.writeToStream (legacyStream);

        auto restoredStorage = std::make_unique<Processor>();
        auto& restored = *restoredStorage;
        restored.setStateInformation (legacyBlock.getData(),
                                     static_cast<int> (legacyBlock.getSize()));
        expectEquals (restored.getParametricEQParameter (parameterIndex (
                          3, BandParameterOffset::Gain))->getValue(), 0.9f);
        for (int band = 0; band < kMaxBands; ++band)
        {
            expect (! restored.getParametricEQParameter (dynamicParameterIndex (
                         band, DynamicBandOffset::Enable))->getBool(),
                    "v2 migration defaults dynamics to disabled");
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (dynamicParameterIndex (
                    band, DynamicBandOffset::Threshold))->getUnitsValue(),
                -24.0f, 1.0e-4f, "v2 migration threshold default");
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (dynamicParameterIndex (
                    band, DynamicBandOffset::Range))->getUnitsValue(),
                0.0f, 1.0e-4f, "v2 migration range default");
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (dynamicParameterIndex (
                    band, DynamicBandOffset::Attack))->getUnitsValue(),
                0.010f, 1.0e-5f, "v2 migration attack default");
            expectWithinAbsoluteError (
                restored.getParametricEQParameter (dynamicParameterIndex (
                    band, DynamicBandOffset::Release))->getUnitsValue(),
                0.100f, 1.0e-5f, "v2 migration release default");
        }
        expect (restored.getParametricEQParameter (
                    kDynamicDetectorParameter)->getBool(),
                "v2 migration detector default is RMS");
        expect (! restored.getParametricEQParameter (
                     kDynamicSidechainParameter)->getBool(),
                "v2 migration sidechain default is Internal");
        expect (restored.getParametricEQParameter (
                    kDynamicLinkParameter)->getBool(),
                "v2 migration link default is linked");

        const auto resaved = serialise (restored);
        const auto resavedTree = juce::ValueTree::readFromData (
            resaved.getData(), static_cast<std::size_t> (resaved.getSize()));
        expect (resavedTree.isValid());
        if (resavedTree.isValid())
        {
            expectEquals (static_cast<int> (resavedTree.getProperty (
                              Processor::kVersionProperty)), Processor::kStateVersion);
            for (int band = 0; band < kMaxBands; ++band)
                for (int offset = 0; offset < kDynamicParametersPerBand; ++offset)
                {
                    const auto id = Processor::getParameterId (dynamicParameterIndex (
                        band, static_cast<DynamicBandOffset> (offset)));
                    expect (resavedTree.hasProperty (juce::Identifier (id)),
                            "resaved state emits every dynamic property");
                }
        }
    }

    void testMalformedDynamicStateIsTotal()
    {
        beginTest ("malformed dynamic state sanitizes without breaking anything");
        auto makeState = [] (float bandOneRange, float bandTwoAttack)
        {
            juce::ValueTree tree (Processor::kStateTag);
            tree.setProperty (Processor::kVersionProperty,
                              Processor::kStateVersion, nullptr);
            tree.setProperty (
                juce::Identifier (Processor::getParameterId (dynamicParameterIndex (
                    0, DynamicBandOffset::Range))), bandOneRange, nullptr);
            tree.setProperty (
                juce::Identifier (Processor::getParameterId (dynamicParameterIndex (
                    1, DynamicBandOffset::Attack))), bandTwoAttack, nullptr);
            juce::MemoryBlock block;
            juce::MemoryOutputStream stream (block, false);
            tree.writeToStream (stream);
            return block;
        };

        {
            const auto block = makeState (99.0f, 0.5f);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectWithinAbsoluteError (
                processorStorage->getParametricEQParameter (dynamicParameterIndex (
                    0, DynamicBandOffset::Range))->getUnitsValue(),
                24.0f, 1.0e-4f, "out-of-range range clamps to +24 dB");
        }
        {
            const auto block = makeState (std::numeric_limits<float>::quiet_NaN(),
                                          -5.0f);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectWithinAbsoluteError (
                processorStorage->getParametricEQParameter (dynamicParameterIndex (
                    0, DynamicBandOffset::Range))->getUnitsValue(),
                0.0f, 1.0e-4f, "NaN range keeps the zero default");
        }
        {
            const auto block = makeState (0.5f, -3.0f);
            auto processorStorage = std::make_unique<Processor>();
            processorStorage->setStateInformation (block.getData(),
                                                  static_cast<int> (block.getSize()));
            expectWithinAbsoluteError (
                processorStorage->getParametricEQParameter (dynamicParameterIndex (
                    1, DynamicBandOffset::Attack))->getUnitsValue(),
                0.0005f, 1.0e-6f, "negative attack clamps to the fast limit");
        }
    }

    void testRuntimeDynamicsNeverSerialized()
    {
        beginTest ("detector/envelope/key runtime state is never serialized");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        configureBell (processor, 0, 1000.0f, 6.0f, 2.0f);
        processor.prepareToPlay (48000.0, 512);

        const auto block = serialise (processor);
        const auto tree = juce::ValueTree::readFromData (
            block.getData(), static_cast<std::size_t> (block.getSize()));
        expect (tree.isValid());
        if (tree.isValid())
        {
            expectEquals (tree.getNumProperties(), kNumParameters + 2,
                          "version + analyzer + 317 parameters");
            std::set<juce::String> names;
            for (int property = 0; property < tree.getNumProperties(); ++property)
                names.insert (tree.getPropertyName (property).toString());
            expectEquals (names.size(), static_cast<std::size_t> (kNumParameters + 2));
            for (const auto& name : names)
            {
                expect (! name.containsIgnoreCase ("detector.history")
                             && ! name.containsIgnoreCase ("envelope")
                             && ! name.containsIgnoreCase ("externalKey")
                             && ! name.containsIgnoreCase ("audition"),
                        "transient runtime state must not be serialized: " + name);
            }
        }
    }
};

static ParametricEQDynamicStateTests parametricEQDynamicStateTests;

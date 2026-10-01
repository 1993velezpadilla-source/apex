#include <JuceHeader.h>

#include "../../../Source/ParametricEQCore/ParametricEQNativePluginFormat.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"

using APEX::ParametricEQ::NativePluginFormat;
using APEX::ParametricEQ::Processor;

class ParametricEQFormatHostTests final : public juce::UnitTest
{
public:
    ParametricEQFormatHostTests()
        : UnitTest ("ParametricEQ.FormatHost", "APEX.ParametricEQ") {}

    void runTest() override
    {
        testDescriptionAndDirectFormat();
        testCreationAndRejection();
        testUnifiedFamilyPath();
        testIndependentInstancesAndAutomationIds();
    }

private:
    static std::unique_ptr<juce::AudioPluginInstance> createDirect (
        const juce::PluginDescription& description, juce::String& error)
    {
        juce::AudioPluginFormatManager manager;
        manager.addFormat (std::make_unique<NativePluginFormat>());
        return manager.createPluginInstance (description, 48000.0, 512, error);
    }

    void testDescriptionAndDirectFormat()
    {
        beginTest ("native description exactly matches processor identity");
        auto processorStorage = std::make_unique<Processor>();
        auto& processor = *processorStorage;
        juce::PluginDescription actual;
        processor.fillInPluginDescription (actual);
        const auto expected = NativePluginFormat::createDescription();
        expectEquals (actual.name, expected.name);
        expectEquals (actual.pluginFormatName, expected.pluginFormatName);
        expectEquals (actual.fileOrIdentifier, expected.fileOrIdentifier);
        expectEquals (actual.uniqueId, expected.uniqueId);
        expect (NativePluginFormat::isDescription (actual));

        NativePluginFormat format;
        expect (! format.canScanForPlugins());
        expect (format.isTrivialToScan());
        expect (! format.pluginNeedsRescanning (expected));
        expect (format.doesPluginStillExist (expected));
        expectEquals (format.getNameOfPluginFromIdentifier (
                          Processor::kFileOrIdentifier),
                      juce::String (Processor::kPluginName));
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, {});
        expectEquals (descriptions.size(), 1);
        expect (descriptions[0] != nullptr
                && descriptions[0]->uniqueId == Processor::kUniqueId);
    }

    void testCreationAndRejection()
    {
        beginTest ("direct format creates canonical instance and rejects foreign identity");
        juce::String error;
        auto instance = createDirect (NativePluginFormat::createDescription(), error);
        expect (instance != nullptr, error);
        expect (error.isEmpty());
        expect (dynamic_cast<Processor*> (instance.get()) != nullptr);
        if (instance != nullptr)
        {
            instance->prepareToPlay (48000.0, 512);
            expectEquals (instance->getLatencySamples(), 0);
            expectEquals (instance->getNumParameters(),
                          APEX::ParametricEQ::kNumParameters);
        }

        auto foreign = NativePluginFormat::createDescription();
        foreign.uniqueId = 0x123456;
        error.clear();
        auto rejected = createDirect (foreign, error);
        expect (rejected == nullptr);
        expect (error.isNotEmpty());
    }

    void testUnifiedFamilyPath()
    {
        beginTest ("one unified APEX Native format discovers and creates all three natives");
        DAW::PluginScanFormatsCore formats;
        int nativeFormatCount = 0;
        for (const auto& name : formats.getRegisteredFormatNames())
            if (name == "APEX Native")
                ++nativeFormatCount;
        expectEquals (nativeFormatCount, 1);

        auto* format = formats.findFormatByName ("APEX Native");
        expect (format != nullptr);
        if (format == nullptr)
            return;

        juce::OwnedArray<juce::PluginDescription> descriptions;
        format->findAllTypesForFile (descriptions, Processor::kFileOrIdentifier);
        expectEquals (descriptions.size(), 3,
                      "G10/C4 ordering stays stable and Parametric EQ appends once");
        if (descriptions.size() == 3)
        {
            expectEquals (descriptions[0]->fileOrIdentifier,
                          juce::String ("APEX::G10"));
            expectEquals (descriptions[1]->fileOrIdentifier,
                          juce::String ("APEX::C4"));
            expectEquals (descriptions[2]->fileOrIdentifier,
                          juce::String (Processor::kFileOrIdentifier));
        }

        juce::String error;
        auto instance = formats.getManager().createPluginInstance (
            NativePluginFormat::createDescription(), 96000.0, 333, error);
        expect (instance != nullptr, error);
        expect (dynamic_cast<Processor*> (instance.get()) != nullptr);
    }

    void testIndependentInstancesAndAutomationIds()
    {
        beginTest ("instances are isolated and every hosted parameter has a stable ID");
        auto firstStorage = std::make_unique<Processor>();
        auto secondStorage = std::make_unique<Processor>();
        auto& first = *firstStorage;
        auto& second = *secondStorage;
        first.getParametricEQParameter (APEX::ParametricEQ::parameterIndex (
            0, APEX::ParametricEQ::BandParameterOffset::Gain))->setValue (1.0f);
        expect (first.getParametricEQParameter (APEX::ParametricEQ::parameterIndex (
                    0, APEX::ParametricEQ::BandParameterOffset::Gain))->getValue()
                != second.getParametricEQParameter (APEX::ParametricEQ::parameterIndex (
                    0, APEX::ParametricEQ::BandParameterOffset::Gain))->getValue());

        int stableIds = 0;
        for (auto* parameter : first.getParameters())
            if (dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter) != nullptr)
                ++stableIds;
        expectEquals (stableIds, APEX::ParametricEQ::kNumParameters);
    }
};

static ParametricEQFormatHostTests parametricEQFormatHostTests;

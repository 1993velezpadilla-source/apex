#pragma once
#include <JuceHeader.h>

#include "../G10Core/G10NativePluginFormat.h"
#include "../C4Core/C4NativePluginFormat.h"
#include "../ParametricEQCore/ParametricEQNativePluginFormat.h"

namespace DAW {

// ============================================================================
// ApexNativePluginFormat — the ONE registered "APEX Native" format object.
//
// JUCE contract (measured defect, Phase 8): AudioPluginFormatManager keys
// formats by NAME and SILENTLY REJECTS a second format with the same name
// (jassertfalse + drop in Debug). G10 and C4 both carry the "APEX Native"
// family name, so registering two format objects left C4's format DEAD —
// C4 descriptions could never resolve through the manager ("No compatible
// plug-in format exists") and the Debug app asserted on every launch.
//
// The correct architecture per that contract: ONE format object per unique
// name. This unified format contains the whole intrinsic family (G10, C4 and
// Parametric EQ), matches descriptions by identifier, and creates the right
// processor.
// The per-plugin format classes (G10NativePluginFormat / C4NativePluginFormat)
// remain the canonical description builders and are still used by their own
// direct-format tests; this class is the host registration surface.
// ============================================================================

class ApexNativePluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* kFormatName = "APEX Native";

    juce::String getName() const override { return kFormatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String&) override
    {
        results.add (new juce::PluginDescription (
            APEX::G10::G10NativePluginFormat::createG10Description()));
        results.add (new juce::PluginDescription (
            APEX::C4::C4NativePluginFormat::createC4Description()));
        results.add (new juce::PluginDescription (
            APEX::ParametricEQ::NativePluginFormat::createDescription()));
    }

    /** The manager matches on getName() AND this predicate; accept every
        intrinsic family identifier (and empty for built descriptions). */
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override
    {
        return fileOrIdentifier.isEmpty()
            || fileOrIdentifier == APEX::G10::G10NativePluginFormat::createG10Description().fileOrIdentifier
            || fileOrIdentifier == APEX::C4::C4NativePluginFormat::createC4Description().fileOrIdentifier
            || fileOrIdentifier == APEX::ParametricEQ::Processor::kFileOrIdentifier;
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override
    {
        if (fileOrIdentifier == APEX::G10::G10NativePluginFormat::createG10Description().fileOrIdentifier)
            return APEX::G10::G10NativePluginFormat::createG10Description().name;
        if (fileOrIdentifier == APEX::C4::C4NativePluginFormat::createC4Description().fileOrIdentifier)
            return APEX::C4::C4NativePluginFormat::createC4Description().name;
        if (fileOrIdentifier == APEX::ParametricEQ::Processor::kFileOrIdentifier)
            return APEX::ParametricEQ::Processor::kPluginName;
        return {};
    }

    bool pluginNeedsRescanning (const juce::PluginDescription&) override
    {
        return false; // intrinsic; never stale
    }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return APEX::G10::G10NativePluginFormat::isG10Description (desc)
            || APEX::C4::C4NativePluginFormat::isC4Description (desc)
            || APEX::ParametricEQ::NativePluginFormat::isDescription (desc);
    }

    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override
    {
        return {};
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override
    {
        return {};
    }

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override
    {
        return false; // synchronous, message-thread safe
    }

protected:
    void createPluginInstance (const juce::PluginDescription& description,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override
    {
        if (callback == nullptr)
            return;

        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;

        try
        {
            if (APEX::C4::C4NativePluginFormat::isC4Description (description))
                instance = std::make_unique<APEX::C4::C4Processor>();
            else if (APEX::G10::G10NativePluginFormat::isG10Description (description))
                instance = std::make_unique<APEX::G10::G10Processor>();
            else if (APEX::ParametricEQ::NativePluginFormat::isDescription (description))
                instance = std::make_unique<APEX::ParametricEQ::Processor>();
            else
                error = "Not an APEX native plugin (identifier="
                    + description.fileOrIdentifier + ").";

            if (instance != nullptr)
                instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
        }
        catch (...)
        {
            instance.reset();
            error = "APEX native plugin creation failed.";
        }

        callback (std::move (instance), error);
    }
};

} // namespace DAW

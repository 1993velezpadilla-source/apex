// PluginSehGuardCore.cpp
// Compiled with /EHa to enable async exception handling.
// With /EHa, catch(...) catches BOTH C++ exceptions AND Windows SEH
// exceptions (access violations 0xC0000005, etc.) — no __try/__except needed.

#include "PluginSehGuardCore.h"

namespace DAW {

// ═══════════════════════════════════════════════════════════════════════════
// createEditor — SEH guarded via /EHa catch(...)
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelyCreateEditor(juce::AudioPluginInstance* plugin,
                                   juce::AudioProcessorEditor*& outEditor)
{
    SehGuardResult result;
    outEditor = nullptr;
    if (plugin == nullptr)
        return result;
    try
    {
        // createEditor() already returns nullptr when no editor is available.
        // Calling hasEditor() first creates and destroys a VST3 IPlugView, then
        // createEditor() creates a second one. Some plugins cannot survive that
        // repeated native-view lifecycle, so perform exactly one guarded call.
        outEditor = plugin->createEditor();
        result.succeeded = (outEditor != nullptr);
    }
    catch (...)
    {
        // Catches both C++ exceptions AND SEH access violations
        // when compiled with /EHa.
        outEditor = nullptr;
        result.succeeded = false;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// createPluginInstance — SEH guarded via /EHa catch(...)
//
// This wraps formatManager.createPluginInstance() which loads the plugin DLL
// and initializes COM interfaces.  Misbehaving plugins (e.g. Antares
// Auto-Tune EFX+) can corrupt COM interfaces and crash with access violations
// during this phase.  The /EHa catch(...) catches these SEH exceptions.
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelyCreatePluginInstance(juce::AudioPluginFormatManager& formatManager,
                                           const juce::PluginDescription& desc,
                                           double sampleRate,
                                           int blockSize,
                                           juce::String& errorMsg)
{
    SehGuardResult result;
    try
    {
        result.instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMsg);
        result.succeeded = (result.instance != nullptr);
    }
    catch (...)
    {
        // Catches both C++ exceptions AND SEH access violations (0xC0000005)
        // when compiled with /EHa.  This catches the Antares crash where the
        // plugin corrupts its COM interfaces during DLL loading.
        result.succeeded = false;
        if (errorMsg.isEmpty())
            errorMsg = "Plugin crashed during creation (SEH exception caught by /EHa guard)";
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// setBusesLayout — guarded via /EHa catch(...)
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelySetBusesLayout(juce::AudioPluginInstance* plugin,
                                     const juce::AudioProcessor::BusesLayout& layout)
{
    SehGuardResult result;
    if (plugin == nullptr)
        return result;
    try
    {
        result.succeeded = plugin->setBusesLayout(layout);
    }
    catch (...)
    {
        result.succeeded = false;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// setStateInformation — guarded via /EHa catch(...)
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelySetStateInformation(juce::AudioPluginInstance* plugin,
                                         const void* data,
                                         int sizeInBytes)
{
    SehGuardResult result;
    if (plugin == nullptr)
        return result;
    try
    {
        plugin->setStateInformation(data, sizeInBytes);
        result.succeeded = true;
    }
    catch (...)
    {
        result.succeeded = false;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// prepareToPlay — guarded via /EHa catch(...)
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelyPrepareToPlay(juce::AudioPluginInstance* plugin,
                                    double sampleRate,
                                    int blockSize)
{
    SehGuardResult result;
    if (plugin == nullptr)
        return result;
    try
    {
        plugin->prepareToPlay(sampleRate, blockSize);
        result.succeeded = true;
    }
    catch (...)
    {
        result.succeeded = false;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// processBlock — guarded via /EHa catch(...)
// ═══════════════════════════════════════════════════════════════════════════

SehGuardResult safelyProcessBlock(juce::AudioPluginInstance* plugin,
                                   juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midi)
{
    SehGuardResult result;
    if (plugin == nullptr)
        return result;
    try
    {
        plugin->processBlock(buffer, midi);
        result.succeeded = true;
    }
    catch (...)
    {
        result.succeeded = false;
    }
    return result;
}

} // namespace DAW

#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * PluginSehGuardCore — SEH-safe wrappers for third-party plugin calls.
 *
 * On Windows x64, plugins may crash with access violations (0xC0000005)
 * during createEditor(), setBusesLayout(), prepareToPlay(), or processBlock().
 * These are NOT C++ exceptions — try/catch(...) cannot catch them.  They
 * require Windows Structured Exception Handling (__try/__except).
 *
 * Constraint: __try/__except cannot appear in a function that has C++
 * objects with non-trivial destructors (MSVC x64 rule).  This header
 * declares plain functions; the implementation in the .cpp file
 * (compiled with /EHa) provides the actual SEH guards.
 */

struct SehGuardResult
{
    bool succeeded = false;
    unsigned long exceptionCode = 0;
    std::unique_ptr<juce::AudioPluginInstance> instance;
};

/** Safely call formatManager.createPluginInstance(). Catches C++ and SEH exceptions. */
SehGuardResult safelyCreatePluginInstance(juce::AudioPluginFormatManager& formatManager,
                                           const juce::PluginDescription& desc,
                                           double sampleRate,
                                           int blockSize,
                                           juce::String& errorMsg);

/** Safely call createEditor() on a plugin.  Returns the editor or nullptr. */
SehGuardResult safelyCreateEditor(juce::AudioPluginInstance* plugin,
                                   juce::AudioProcessorEditor*& outEditor);

/** Safely call setBusesLayout() on a plugin. */
SehGuardResult safelySetBusesLayout(juce::AudioPluginInstance* plugin,
                                     const juce::AudioProcessor::BusesLayout& layout);

/** Safely call setStateInformation() on a plugin (project restore path).
 *  Catches C++ exceptions and, under /EHa, Windows SEH access violations so a
 *  detectable state-restore failure can degrade to a preserved-but-inactive
 *  slot instead of taking down the host process. */
SehGuardResult safelySetStateInformation(juce::AudioPluginInstance* plugin,
                                         const void* data,
                                         int sizeInBytes);

/** Safely call prepareToPlay() on a plugin. */
SehGuardResult safelyPrepareToPlay(juce::AudioPluginInstance* plugin,
                                    double sampleRate,
                                    int blockSize);

/** Safely call processBlock() on a plugin. */
SehGuardResult safelyProcessBlock(juce::AudioPluginInstance* plugin,
                                   juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midi);

} // namespace DAW

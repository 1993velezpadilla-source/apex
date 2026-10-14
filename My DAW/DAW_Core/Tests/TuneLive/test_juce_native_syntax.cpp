// Compile-only: the real JUCE 8.0.12 processor, editor and parameter API.
// This is deliberately NOT a mock of AudioPluginInstance/AudioProcessorEditor.
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include "../../Source/PluginHostCore/ApexNativePluginFormat.h"
#include <type_traits>
static_assert(std::is_base_of<juce::AudioPluginInstance,APEX::TuneLive::Processor>::value,
              "APEX Tune Live must be a real JUCE hosted AudioPluginInstance");
static_assert(std::is_base_of<juce::AudioProcessorEditor,APEX::TuneLive::Editor>::value,
              "APEX Tune Live must expose the actual JUCE editor");
static_assert(std::is_base_of<juce::AudioPluginFormat,DAW::ApexNativePluginFormat>::value,
              "APEX Tune Live must be registered inside the host's unified native format");
static_assert(APEX::TuneLive::Processor::kNumParams==11,
              "Append new parameters; don't reorder existing automation IDs");
void apexTuneLiveCompileOnly()
{
    // Compile the REAL native host format (including G10/C4/Parametric EQ)
    // and ensure Tune Live's description is accepted as an intrinsic plugin.
    DAW::ApexNativePluginFormat nativeFormat;
    const auto desc=DAW::ApexNativePluginFormat::createTuneLiveDescription();
    (void) nativeFormat.fileMightContainThisPluginType(desc.fileOrIdentifier);
    (void) DAW::ApexNativePluginFormat::isTuneLiveDescription(desc);
    APEX::TuneLive::Processor processor;
    auto* editor=processor.createEditor();
    delete editor;
}

// Compile-only: the real JUCE 8.0.12 processor, editor and parameter API.
// This is deliberately NOT a mock of AudioPluginInstance/AudioProcessorEditor.
#include "../../Source/TuneLiveCore/ApexTuneLiveProcessor.h"
#include <type_traits>
static_assert(std::is_base_of<juce::AudioPluginInstance,APEX::TuneLive::Processor>::value,
              "APEX Tune Live must be a real JUCE hosted AudioPluginInstance");
static_assert(std::is_base_of<juce::AudioProcessorEditor,APEX::TuneLive::Editor>::value,
              "APEX Tune Live must expose the actual JUCE editor");
static_assert(APEX::TuneLive::Processor::kNumParams==11,
              "Append new parameters; don't reorder existing automation IDs");
void apexTuneLiveCompileOnly()
{
    APEX::TuneLive::Processor processor;
    auto* editor=processor.createEditor();
    delete editor;
}

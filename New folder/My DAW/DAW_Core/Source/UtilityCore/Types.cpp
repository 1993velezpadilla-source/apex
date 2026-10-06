#include "Types.h"

namespace DAW {

std::atomic<int> IDGenerator::trackCounter{0};
std::atomic<int> IDGenerator::clipCounter{0};
std::atomic<int> IDGenerator::pluginCounter{0};
std::atomic<int> IDGenerator::routeCounter{0};
std::atomic<int> IDGenerator::sidechainCounter{0};

TrackID IDGenerator::generateTrackID()
{
    return "TRK_" + juce::String(++trackCounter);
}

ClipID IDGenerator::generateClipID()
{
    return "CLIP_" + juce::String(++clipCounter);
}

PluginID IDGenerator::generatePluginID()
{
    return "PLG_" + juce::String(++pluginCounter);
}

RouteID IDGenerator::generateRouteID()
{
    return "RTE_" + juce::String(++routeCounter);
}

SidechainID IDGenerator::generateSidechainID()
{
    return "SC_" + juce::String(++sidechainCounter);
}

} // namespace DAW

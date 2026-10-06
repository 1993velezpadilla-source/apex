#pragma once
#include <JuceHeader.h>

namespace DAW {

struct ExportSettings
{
    juce::File outputFile;
    double  sampleRate    = 0.0;
    int     bitDepth      = 24;
    int     blockSize     = 512;
    int64_t startSample   = 0;
    int64_t endSample     = 0;
    double  tailSeconds   = 0.0;
    bool    includeMasterFx   = true;
    bool    includeAutomation = true;
    bool    includePlugins    = true;
};

} // namespace DAW

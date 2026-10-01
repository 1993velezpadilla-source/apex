#pragma once
#include <JuceHeader.h>

namespace DAW {

struct ExportPublicationCore
{
    static bool publish (const juce::File& temporaryFile,
                         const juce::File& destinationFile)
    {
        return temporaryFile.existsAsFile()
            && temporaryFile.replaceFileIn (destinationFile);
    }
};

} // namespace DAW

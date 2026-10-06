#pragma once
#include <JuceHeader.h>
#include "../RecordingCore/RecordArmSafetyCore.h"

namespace DAW {

class RecordOverwriteConfirmDialog
{
public:
    static void show(const std::vector<RecordArmSafetyCore::OverwriteWarning>& warnings,
                     std::function<void(bool)> callback)
    {
        juce::String message = "Recording will overwrite existing clips:\n\n";
        for (const auto& w : warnings)
            message += "  " + w.trackName + ": " + w.existingClipName + "\n";
        message += "\nProceed with recording?";

        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Confirm Overwrite")
                .withMessage(message)
                .withButton("Record")
                .withButton("Cancel"),
            [callback = std::move(callback)](int result)
            {
                if (callback)
                    callback(result == 1);
            });
    }
};

} // namespace DAW

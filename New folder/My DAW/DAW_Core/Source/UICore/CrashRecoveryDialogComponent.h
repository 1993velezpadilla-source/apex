#pragma once
#include <JuceHeader.h>
#include "../ProjectCore/CrashRecoveryCore.h"

namespace DAW {

/**
 * CrashRecoveryDialogComponent — modal startup prompt shown when a previous
 * session did not shut down cleanly and an autosave is available.
 *
 * Actions:
 *   1. Recover Autosave   — open the autosave file
 *   2. Open Original      — open the original project file
 *   3. Show Autosave Folder — reveal folder in Explorer
 *   4. Discard Recovery   — soft-delete autosave to .discarded/
 *
 * Lives in UICore only. No ProjectCore headers other than RecoveryInfo.
 */
class CrashRecoveryDialogComponent : public juce::Component
{
public:
    explicit CrashRecoveryDialogComponent(const RecoveryInfo& info);
    ~CrashRecoveryDialogComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Called when user picks "Recover Autosave". Receives the autosave file. */
    std::function<void(const juce::File&)> onRecoverAutosave;

    /** Called when user picks "Open Original". Receives the original project file. */
    std::function<void(const juce::File&)> onOpenOriginal;

    /** Called when user picks "Discard Recovery". */
    std::function<void()> onDiscardRecovery;

    /** Show as a non-owning modal dialog.
     *  Ownership belongs to JUCE's ModalComponentManager (launchAsync);
     *  the caller must store the pointer in a SafePointer, not a unique_ptr. */
    static juce::DialogWindow* showAsync(
        const RecoveryInfo& info,
        std::function<void(const juce::File&)> onRecover,
        std::function<void(const juce::File&)> onOpenOrig,
        std::function<void()>                  onDiscard);

private:
    RecoveryInfo info_;

    juce::Label  titleLabel_;
    juce::Label  projectLabel_;
    juce::Label  pathLabel_;
    juce::Label  timestampLabel_;
    juce::Label  ageLabel_;
    juce::Label  sizeLabel_;

    juce::TextButton recoverBtn_  { "Recover Autosave" };
    juce::TextButton origBtn_     { "Open Original Project" };
    juce::TextButton showFolderBtn_{ "Show Autosave Folder" };
    juce::TextButton discardBtn_  { "Discard Recovery" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrashRecoveryDialogComponent)
};

} // namespace DAW

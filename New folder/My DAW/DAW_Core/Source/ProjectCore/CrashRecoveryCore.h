#pragma once
#include <JuceHeader.h>

namespace DAW {

/** All information needed to offer a recovery prompt. */
struct RecoveryInfo
{
    bool         available          = false;
    juce::File   autosaveFile;
    juce::File   originalProjectFile;
    juce::Time   autosaveTime;
    juce::String sessionId;
    bool         wasUnsavedProject  = false;
};

/**
 * CrashRecoveryCore — stateless helper that reads the previous session lock
 * and determines whether a crash recovery prompt should be shown.
 *
 * Pure reader: never writes any file itself.
 * Recovery state is read from session.lock.previous (written by RecoverySessionCore).
 */
class CrashRecoveryCore
{
public:
    CrashRecoveryCore()  = default;
    ~CrashRecoveryCore() = default;

    /** Returns true if session.lock.previous indicates an unclean shutdown. */
    bool hasCrashedPreviousSession() const;

    /** Reads the previous lock file and returns recovery info. */
    RecoveryInfo findRecoveryInfo() const;

    /**
     * Soft-deletes the autosave by moving it to <folder>/.discarded/.
     * Hard-deletes only after a second explicit confirm or the cleanup sweep.
     */
    void discardRecovery(const RecoveryInfo& info);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrashRecoveryCore)
};

} // namespace DAW

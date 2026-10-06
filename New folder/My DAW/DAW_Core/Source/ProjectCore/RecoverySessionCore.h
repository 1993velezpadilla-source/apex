#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * RecoverySessionCore — writes and maintains the session.lock file.
 *
 * Responsibilities:
 *   - Write lock on startup (cleanShutdown="0")
 *   - Update lock after each autosave
 *   - Set cleanShutdown="1" on clean exit
 *
 * This is the ONLY owner of the lock file. All other subsystems call
 * into this class rather than touching the file directly.
 */
class RecoverySessionCore
{
public:
    RecoverySessionCore();
    ~RecoverySessionCore() = default;

    /** Call at the very beginning of app startup, before any UI. */
    void startSession(const juce::String& appVersion);

    /** Update the lock with the current project + latest autosave path. */
    void updateCurrentProject(const juce::File& projectFile,
                              const juce::File& latestAutosave,
                              bool wasUnsavedProject);

    /** Call on clean shutdown (ApplicationCore destructor or systemRequestedQuit). */
    void markCleanShutdown();

    juce::String getSessionId() const noexcept { return sessionId_; }

    /** Path to the session lock file. */
    static juce::File getLockFilePath();

private:
    void writeLockFile();

    juce::String sessionId_;
    juce::String appVersion_;
    juce::File   currentProjectFile_;
    juce::File   latestAutosaveFile_;
    bool         wasUnsavedProject_ = false;
    bool         sessionStarted_    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecoverySessionCore)
};

} // namespace DAW

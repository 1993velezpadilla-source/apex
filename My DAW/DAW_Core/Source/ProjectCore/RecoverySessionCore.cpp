#include "RecoverySessionCore.h"

namespace DAW {

static juce::File getApexAppDataDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("APEX");
}

RecoverySessionCore::RecoverySessionCore()
{
}

juce::File RecoverySessionCore::getLockFilePath()
{
    return getApexAppDataDir().getChildFile("session.lock");
}

void RecoverySessionCore::startSession(const juce::String& appVersion)
{
    appVersion_    = appVersion;
    sessionId_     = juce::Uuid().toString();
    sessionStarted_ = true;

    // Move any existing lock to .previous so recovery info survives quick relaunch
    auto lockFile = getLockFilePath();
    if (lockFile.existsAsFile())
    {
        auto prev = lockFile.getSiblingFile("session.lock.previous");
        prev.deleteFile();
        lockFile.moveFileTo(prev);
    }

    writeLockFile();
}

void RecoverySessionCore::updateCurrentProject(const juce::File& projectFile,
                                                const juce::File& latestAutosave,
                                                bool wasUnsavedProject)
{
    currentProjectFile_  = projectFile;
    latestAutosaveFile_  = latestAutosave;
    wasUnsavedProject_   = wasUnsavedProject;

    if (sessionStarted_)
        writeLockFile();
}

void RecoverySessionCore::markCleanShutdown()
{
    if (!sessionStarted_) return;

    auto lockFile = getLockFilePath();

    juce::XmlElement root("APEX_SESSION");
    auto* session = root.createNewChildElement("SESSION");
    session->setAttribute("id",             sessionId_);
    session->setAttribute("appVersion",     appVersion_);
    session->setAttribute("startedUtc",     juce::Time::getCurrentTime().toISO8601(true));
    session->setAttribute("cleanShutdown",  "1");

    auto* project = session->createNewChildElement("PROJECT");
    project->setAttribute("path",                currentProjectFile_.getFullPathName());
    project->setAttribute("latestAutosave",      latestAutosaveFile_.getFullPathName());
    project->setAttribute("wasUnsavedProject",   wasUnsavedProject_ ? "1" : "0");

    root.writeTo(lockFile);

    sessionStarted_ = false;
}

void RecoverySessionCore::writeLockFile()
{
    auto lockFile = getLockFilePath();
    lockFile.getParentDirectory().createDirectory();

    juce::XmlElement root("APEX_SESSION");
    auto* session = root.createNewChildElement("SESSION");
    session->setAttribute("id",             sessionId_);
    session->setAttribute("appVersion",     appVersion_);
    session->setAttribute("startedUtc",     juce::Time::getCurrentTime().toISO8601(true));
    session->setAttribute("cleanShutdown",  "0");

    auto* project = session->createNewChildElement("PROJECT");
    project->setAttribute("path",                currentProjectFile_.getFullPathName());
    project->setAttribute("latestAutosave",      latestAutosaveFile_.getFullPathName());
    project->setAttribute("wasUnsavedProject",   wasUnsavedProject_ ? "1" : "0");

    root.writeTo(lockFile);
}

} // namespace DAW

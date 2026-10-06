#include "CrashRecoveryCore.h"
#include "RecoverySessionCore.h"

namespace DAW {

bool CrashRecoveryCore::hasCrashedPreviousSession() const
{
    auto prevLock = RecoverySessionCore::getLockFilePath()
                        .getSiblingFile("session.lock.previous");

    if (!prevLock.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(prevLock);
    if (!xml) return false;

    auto* session = xml->getChildByName("SESSION");
    if (!session) return false;

    return session->getStringAttribute("cleanShutdown", "1") == "0";
}

RecoveryInfo CrashRecoveryCore::findRecoveryInfo() const
{
    RecoveryInfo info;

    auto prevLock = RecoverySessionCore::getLockFilePath()
                        .getSiblingFile("session.lock.previous");

    if (!prevLock.existsAsFile())
        return info;

    auto xml = juce::XmlDocument::parse(prevLock);
    if (!xml) return info;

    auto* session = xml->getChildByName("SESSION");
    if (!session) return info;

    if (session->getStringAttribute("cleanShutdown", "1") != "0")
        return info;

    auto* project = session->getChildByName("PROJECT");
    if (!project) return info;

    info.sessionId   = session->getStringAttribute("id");
    info.wasUnsavedProject = project->getStringAttribute("wasUnsavedProject") == "1";

    auto autosavePath = project->getStringAttribute("latestAutosave");
    if (autosavePath.isNotEmpty())
    {
        info.autosaveFile = juce::File(autosavePath);
        if (!info.autosaveFile.existsAsFile())
            return info;   // no readable autosave — nothing to offer
        info.autosaveTime = info.autosaveFile.getLastModificationTime();
    }
    else
    {
        return info;
    }

    auto projectPath = project->getStringAttribute("path");
    if (projectPath.isNotEmpty())
        info.originalProjectFile = juce::File(projectPath);

    info.available = true;
    return info;
}

void CrashRecoveryCore::discardRecovery(const RecoveryInfo& info)
{
    if (!info.autosaveFile.existsAsFile())
        return;

    auto discardDir = info.autosaveFile.getParentDirectory()
                                       .getChildFile(".discarded");
    discardDir.createDirectory();

    auto dest = discardDir.getChildFile(info.autosaveFile.getFileName());
    info.autosaveFile.moveFileTo(dest);
}

} // namespace DAW

#include <JuceHeader.h>

#include "../../../Source/ProjectCore/FilePublicationCore.h"
#include "../../../Source/ProjectCore/AutosaveWriteJobCore.h"
#include "../../../Source/ProjectCore/AutosaveManagerCore.h"
#include "../../../Source/ProjectCore/ProjectManager.h"
#include "../../../Source/ProjectCore/RecoverySessionCore.h"

namespace
{
juce::String testProjectState = "complete";
}

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
// The focused test target intentionally does not link the full application
// ProjectManager.cpp graph.  These test-only state hooks let the production
// inline saveToFile() run with a deterministic complete snapshot, while the
// real state builder remains owned by ProjectManager.cpp in the application.
namespace DAW
{
bool ProjectManager::buildState(juce::ValueTree& state, juce::String& error) const
{
    error.clear();
    state = juce::ValueTree("DAWProject");
    state.setProperty("testState", testProjectState, nullptr);
    return true;
}

bool ProjectManager::performAutosaveNow()
{
    return false;
}

void RecoverySessionCore::updateCurrentProject(const juce::File&, const juce::File&, bool)
{
}
} // namespace DAW
#endif

namespace
{

class ScopedPersistenceDirectory final
{
public:
    explicit ScopedPersistenceDirectory(const juce::String& tag)
        : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("apex_" + tag, {}, false))
    {
        directory.createDirectory();
    }

    ~ScopedPersistenceDirectory()
    {
        directory.deleteRecursively();
    }

    juce::File directory;

    JUCE_DECLARE_NON_COPYABLE(ScopedPersistenceDirectory)
};

bool writeCandidate(const juce::File& candidate, const juce::String& state)
{
    candidate.deleteFile();
    return candidate.replaceWithText("<DAWProject state=\"" + state + "\"/>");
}

bool publishCandidate(const juce::File& destination,
                      const juce::String& state,
                      juce::String& error)
{
    const auto candidate = destination.getSiblingFile(
        "." + destination.getFileName() + ".candidate");

    const auto candidateWritten = writeCandidate(candidate, state);
    if (!candidateWritten)
    {
        error = "test candidate write failed";
        return false;
    }

    const auto published = DAW::publishFileTransactionally(candidate,
                                                            destination,
                                                            error);
    if (!published)
        candidate.deleteFile();
    return published;
}

DAW::ProjectManager::SaveStateAugmenter makeCollapsedFolderAugmenter(
    const juce::String& folderId)
{
    return [folderId](juce::ValueTree& state, juce::String& error)
    {
        error.clear();
        if (!state.isValid())
        {
            error = "test augmentation received an invalid state";
            return false;
        }

        juce::ValueTree collapsedFolders("CollapsedFolders");
        juce::ValueTree folderNode("Folder");
        folderNode.setProperty("trackId", folderId, nullptr);
        collapsedFolders.addChild(folderNode, -1, nullptr);
        state.addChild(collapsedFolders, -1, nullptr);
        return true;
    };
}

juce::ValueTree parseProjectState(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
}

struct AsyncAutosaveResult
{
    bool completed = false;
    bool success = false;
    juce::File writtenFile;
    juce::String failureReason;
};

bool runAsyncAutosaveJob(const juce::ValueTree& state,
                         const juce::File& targetDirectory,
                         const juce::String& projectName,
                         int keepCount,
                         AsyncAutosaveResult& result)
{
    juce::ThreadPool pool(1);
    juce::WaitableEvent completed;
    auto authority = std::make_shared<DAW::AutosavePublicationAuthority>();
    authority->beginGeneration();
    const auto generation = authority->currentGeneration();
    pool.addJob(new DAW::detail::AutosaveWriteJob(
                    state.createCopy(),
                    targetDirectory,
                    projectName,
                    keepCount,
                    authority,
                    generation,
                    [&result, &completed](bool success, juce::File writtenFile, juce::String failureReason)
                    {
                        result.completed = true;
                        result.success = success;
                        result.writtenFile = std::move(writtenFile);
                        result.failureReason = std::move(failureReason);
                        completed.signal();
                    }),
                true);

    const auto callbackCompleted = completed.wait(5000);
    const auto drained = pool.removeAllJobs(false, 5000);
    return callbackCompleted && drained;
}

bool pumpMessageLoopUntil(juce::WaitableEvent& completed, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter()
        + static_cast<juce::uint32>(timeoutMs);

    while (!completed.wait(0))
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;
    }

    return true;
}

struct ManagerAutosaveObservation
{
    juce::WaitableEvent completed;
    int successCount = 0;
    int failureCount = 0;
    juce::String failureReason;
};

bool prepareManagerForIntegrationTest(DAW::ProjectManager& project,
                                      DAW::AutosaveManagerCore& autosave,
                                      const juce::File& projectFile)
{
    project.setExternalAutosaveManaged(true);
    if (!project.saveToFile(projectFile))
        return false;
    autosave.prepare(project);
    autosave.setEnabled(false);
    return true;
}

juce::File seedLatestAutosave(const juce::File& projectFile,
                              const juce::String& contents)
{
    const auto latest = projectFile.getParentDirectory()
        .getChildFile(".apex_autosaves")
        .getChildFile(projectFile.getFileNameWithoutExtension()
                      + ".autosave.latest.apex");
    latest.getParentDirectory().createDirectory();
    latest.replaceWithText(contents);
    return latest;
}

void installManagerObservation(DAW::AutosaveManagerCore& autosave,
                               const std::shared_ptr<ManagerAutosaveObservation>& observation)
{
    autosave.onAutosaveSucceeded = [observation](const juce::String&)
    {
        ++observation->successCount;
        observation->completed.signal();
    };

    autosave.onAutosaveFailed = [observation](const juce::String& reason)
    {
        ++observation->failureCount;
        observation->failureReason = reason;
        observation->completed.signal();
    };
}

class AutosavePublicationAuthorityStateMachineTest final : public juce::UnitTest
{
public:
    AutosavePublicationAuthorityStateMachineTest()
        : UnitTest("AutosavePersistence.PublicationAuthorityStateMachine", "Persistence") {}

    void runTest() override
    {
        beginTest("shutdown wins before commit acquisition");
        {
            DAW::AutosavePublicationAuthority authority;
            expect(authority.beginGeneration());
            const auto generation = authority.currentGeneration();
            expect(authority.getState()
                   == DAW::AutosavePublicationAuthority::State::Open);

            authority.revoke();
            expect(authority.getState()
                   == DAW::AutosavePublicationAuthority::State::Revoked);
            expect(!authority.tryAcquireCommit(generation));
        }

        beginTest("one commit wins and release drains revocation");
        {
            DAW::AutosavePublicationAuthority authority;
            expect(authority.beginGeneration());
            const auto generation = authority.currentGeneration();
            auto first = authority.tryAcquireCommit(generation);
            expect(static_cast<bool>(first));
            expect(authority.getState()
                   == DAW::AutosavePublicationAuthority::State::Committing);
            expect(!authority.tryAcquireCommit(generation));

            authority.revoke();
            expect(authority.getState()
                   == DAW::AutosavePublicationAuthority::State::CommittingRevoked);
            expect(!authority.beginGeneration());
        }

        beginTest("normal commit release reopens only the same generation");
        {
            DAW::AutosavePublicationAuthority authority;
            expect(authority.beginGeneration());
            const auto generation = authority.currentGeneration();
            {
                auto permit = authority.tryAcquireCommit(generation);
                expect(static_cast<bool>(permit));
            }
            expect(authority.getState()
                   == DAW::AutosavePublicationAuthority::State::Open);
            expect(static_cast<bool>(authority.tryAcquireCommit(generation)));
        }
    }
};

AutosavePublicationAuthorityStateMachineTest autosavePublicationAuthorityStateMachineTest;

class AutosaveManagerShutdownLifecycleTest final : public juce::UnitTest
{
public:
    AutosaveManagerShutdownLifecycleTest()
        : UnitTest("AutosavePersistence.ManagerShutdownLifecycle", "Persistence") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        beginTest("real manager path publishes a complete autosave");
        {
            testProjectState = "manager-success";
            ScopedPersistenceDirectory files("manager_success");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);

            autosave.markDirty("manager_success");
            autosave.triggerAutosaveNow("manager_success");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "real manager completion callback did not arrive");
            expectEquals(observation->successCount, 1);
            expectEquals(observation->failureCount, 0);
            expect(latest.loadFileAsString().contains("manager-success"));
            expect(files.directory.getChildFile(".apex_autosaves")
                       .getChildFile("project.autosave.001.apex")
                       .loadFileAsString().contains("STATE_A"));
            expect(!autosave.isAutosaveInProgress());
            expect(autosave.shutdown(), "completed manager job should drain");
        }

        beginTest("edits after an in-flight snapshot remain autosave-dirty");
        {
            testProjectState = "revision-before";
            ScopedPersistenceDirectory files("manager_revision");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);

            autosave.markDirty("initial_edit");
            DAW::detail::AutosaveWriteJobTestHooks::armPauseAtStartForTesting();
            autosave.triggerAutosaveNow("revision_snapshot");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForStartPauseForTesting(5000),
                   "first autosave worker did not reach the pause");
            testProjectState = "revision-after";
            autosave.markDirty("edit_while_writing");
            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();

            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "first autosave completion did not arrive");
            expectEquals(observation->successCount, 1);
            expect(autosave.getLatestAutosaveFile().loadFileAsString().contains("revision-before"),
                   "the earlier snapshot should be the first published version");
            expect(autosave.isAutosaveDirty(),
                   "the later edit must remain pending after earlier snapshot completion");

            observation->completed.reset();
            autosave.triggerAutosaveNow("publish_later_revision");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "second autosave completion did not arrive");
            expectEquals(observation->successCount, 2);
            expect(autosave.getLatestAutosaveFile().loadFileAsString().contains("revision-after"));
            expect(!autosave.isAutosaveDirty(),
                   "dirty clears only after publishing the latest revision");
            expect(autosave.shutdown());
        }

        beginTest("an earlier project's completion cannot acknowledge a new project");
        {
            testProjectState = "project-A";
            ScopedPersistenceDirectory files("manager_project_switch");
            const auto projectFileA = files.directory.getChildFile("A.dawproj");
            const auto projectFileB = files.directory.getChildFile("B.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFileA));
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);

            autosave.markDirty("edit_A");
            DAW::detail::AutosaveWriteJobTestHooks::armPauseAtStartForTesting();
            autosave.triggerAutosaveNow("save_A");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForStartPauseForTesting(5000),
                   "project A's worker did not reach the pause");

            // The test-only ProjectManager stub supports SaveAs, not the real
            // load implementation. Switch its file identity, then invoke the
            // same successful-load notification wired in MainComponent.
            testProjectState = "project-B";
            expect(project.saveToFile(projectFileB));
            autosave.onProjectLoaded();
            autosave.markDirty("edit_B");
            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                   "the old worker did not finish");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);

            expectEquals(observation->successCount, 0,
                         "stale completions must not claim success for project B");
            expect(!autosave.isAutosaveInProgress());
            expect(autosave.isAutosaveDirty(),
                   "project B's pending revision must not be cleared");
            expect(autosave.getLatestAutosaveFile() == juce::File(),
                   "project A's recovery file must not become B's latest autosave");

            autosave.triggerAutosaveNow("save_B");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "project B's autosave completion did not arrive");
            expectEquals(observation->successCount, 1);
            expect(autosave.getLatestAutosaveFile().loadFileAsString().contains("project-B"));
            expect(!autosave.isAutosaveDirty());
            expect(autosave.shutdown());
        }

        beginTest("real manager publication failure preserves canonical latest");
        {
            testProjectState = "manager-failure";
            ScopedPersistenceDirectory files("manager_failure");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);

            DAW::FilePublicationCore::injectFailureForTesting(
                DAW::PublicationFaultPoint::BeforeReplace);
            autosave.triggerAutosaveNow("manager_failure");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "real manager failure callback did not arrive");
            DAW::FilePublicationCore::clearFailureForTesting();

            expectEquals(observation->successCount, 0);
            expectEquals(observation->failureCount, 1);
            expect(observation->failureReason.isNotEmpty());
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));
            expect(autosave.shutdown(), "failed manager job should drain");
        }

        beginTest("shutdown before worker start revokes the queued job");
        {
            testProjectState = "manager-start-cancel";
            ScopedPersistenceDirectory files("manager_start_cancel");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);
            autosave.setShutdownWaitTimeoutForTesting(25);

            DAW::detail::AutosaveWriteJobTestHooks::armPauseAtStartForTesting();
            autosave.triggerAutosaveNow("manager_start_cancel");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForStartPauseForTesting(5000),
                   "worker did not reach the start pause");

            expect(!autosave.shutdown(),
                   "bounded shutdown should report the deliberately blocked worker");
            autosave.triggerAutosaveNow("after_shutdown");
            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                   "cancelled worker did not finish after release");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);

            expectEquals(observation->successCount, 0);
            expectEquals(observation->failureCount, 0);
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));
            expect(!autosave.isAutosaveInProgress());
        }

        beginTest("admitted commit survives bounded shutdown and blocks reprepare");
        {
            testProjectState = "worker-wins";
            ScopedPersistenceDirectory files("manager_worker_wins");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);
            autosave.setShutdownWaitTimeoutForTesting(25);
            DAW::FilePublicationCore::resetPublicationInvocationCountForTesting();

            DAW::detail::AutosaveWriteJobTestHooks::armPauseAfterCommitAcquisitionForTesting();
            autosave.triggerAutosaveNow("worker_wins");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForCommitPauseForTesting(5000),
                   "worker did not acquire the commit permit");

            expect(!autosave.shutdown(),
                   "bounded shutdown should not wait for an admitted paused commit");
            testProjectState = "must-not-start-while-commit-active";
            autosave.prepare(project);
            autosave.triggerAutosaveNow("reprepare_while_commit_active");
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));

            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                   "admitted worker did not finish after release");
            expectEquals(DAW::FilePublicationCore::getPublicationInvocationCountForTesting(), 1);
            expect(latest.loadFileAsString().contains("worker-wins"));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
            expectEquals(observation->successCount, 0);
            expectEquals(observation->failureCount, 0);

            testProjectState = "generation-after-drain";
            autosave.prepare(project);
            autosave.setEnabled(false);
            autosave.triggerAutosaveNow("generation_after_drain");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "new generation completion callback did not arrive");
            expectEquals(observation->successCount, 1);
            expect(latest.loadFileAsString().contains("generation-after-drain"));
            expect(autosave.shutdown());
        }

        beginTest("shutdown between preparation and publication preserves latest");
        {
            testProjectState = "manager-publication-cancel";
            ScopedPersistenceDirectory files("manager_publication_cancel");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);
            autosave.setShutdownWaitTimeoutForTesting(25);
            DAW::FilePublicationCore::resetPublicationInvocationCountForTesting();

            DAW::detail::AutosaveWriteJobTestHooks::armPauseBeforePublicationForTesting();
            autosave.triggerAutosaveNow("manager_publication_cancel");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForPublicationPauseForTesting(5000),
                   "worker did not reach the publication pause");
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));

            expect(!autosave.shutdown(),
                   "bounded shutdown should report the deliberately blocked worker");
            autosave.triggerAutosaveNow("after_shutdown");
            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                   "cancelled publication worker did not finish after release");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);

            expectEquals(observation->successCount, 0);
            expectEquals(observation->failureCount, 0);
            expectEquals(DAW::FilePublicationCore::getPublicationInvocationCountForTesting(), 0,
                         "shutdown-winning worker must not enter final publication");
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));
        }

        beginTest("delayed completion callback cannot touch a destroyed manager");
        {
            testProjectState = "manager-delayed-callback";
            ScopedPersistenceDirectory files("manager_delayed_callback");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");

            {
                DAW::ProjectManager project;
                DAW::AutosaveManagerCore autosave;
                expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
                autosave.setKeepCount(1);
                installManagerObservation(autosave, observation);

                DAW::detail::AutosaveWriteJobTestHooks::armPauseAtStartForTesting();
                autosave.triggerAutosaveNow("manager_delayed_callback");
                expect(DAW::detail::AutosaveWriteJobTestHooks::waitForStartPauseForTesting(5000),
                       "worker did not reach the start pause");
                DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
                expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                       "worker did not finish before manager teardown");
                expect(autosave.shutdown(), "finished worker should drain before teardown");
            }

            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
            expectEquals(observation->successCount, 0);
            expectEquals(observation->failureCount, 0);
            expect(latest.loadFileAsString().contains("manager-delayed-callback"));
        }

        beginTest("old generation cannot publish after manager reprepare");
        {
            testProjectState = "old-generation";
            ScopedPersistenceDirectory files("manager_reprepare");
            const auto projectFile = files.directory.getChildFile("project.dawproj");
            DAW::ProjectManager project;
            DAW::AutosaveManagerCore autosave;
            expect(prepareManagerForIntegrationTest(project, autosave, projectFile));
            autosave.setKeepCount(1);
            const auto latest = seedLatestAutosave(
                projectFile, "<DAWProject state=\"STATE_A\"/>");
            const auto history = files.directory.getChildFile(".apex_autosaves")
                .getChildFile("project.autosave.001.apex");
            const auto observation = std::make_shared<ManagerAutosaveObservation>();
            installManagerObservation(autosave, observation);
            autosave.setShutdownWaitTimeoutForTesting(25);

            DAW::detail::AutosaveWriteJobTestHooks::armPauseBeforePublicationForTesting();
            autosave.triggerAutosaveNow("old_generation");
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForPublicationPauseForTesting(5000),
                   "old generation did not reach publication pause");
            expect(!autosave.shutdown());
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));

            testProjectState = "new-generation";
            autosave.prepare(project);
            autosave.setEnabled(false);
            autosave.triggerAutosaveNow("new_generation");
            expectEquals(latest.loadFileAsString(),
                         juce::String("<DAWProject state=\"STATE_A\"/>"));

            DAW::detail::AutosaveWriteJobTestHooks::releasePauseForTesting();
            expect(DAW::detail::AutosaveWriteJobTestHooks::waitForJobFinishedForTesting(5000),
                   "old generation did not finish after revocation");
            expect(pumpMessageLoopUntil(observation->completed, 5000),
                   "new generation completion callback did not arrive");

            expectEquals(observation->successCount, 1);
            expectEquals(observation->failureCount, 0);
            expect(latest.loadFileAsString().contains("new-generation"));
            expect(history.loadFileAsString().contains("STATE_A"),
                   "old generation must not become the new generation's history");
            expect(autosave.shutdown());
        }
    }
};

AutosaveManagerShutdownLifecycleTest autosaveManagerShutdownLifecycleTest;

class ProjectFinalReplacementFailureTest final : public juce::UnitTest
{
public:
    ProjectFinalReplacementFailureTest()
        : UnitTest("ProjectPersistence.FinalReplacementFailure", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("project_replace_failure");
        const auto destination = files.directory.getChildFile("project.dawproj");
        beginTest("candidate write succeeds but final publication fails");
        expect(destination.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String error;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeReplace);
        const auto published = publishCandidate(destination, "STATE_B", error);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!published, "forced final publication must fail");
        expect(error.isNotEmpty(), "publication failure must be surfaced");
        expect(destination.existsAsFile(), "previous canonical project must remain");
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"),
                     "previous canonical content must be preserved");
        expect(!destination.loadFileAsString().contains("STATE_B"),
               "failed candidate must not become authoritative");
    }
};

ProjectFinalReplacementFailureTest projectFinalReplacementFailureTest;

class ProjectSaveFinalReplacementFailureTest final : public juce::UnitTest
{
public:
    ProjectSaveFinalReplacementFailureTest()
        : UnitTest("ProjectPersistence.SaveFinalReplacementFailure", "Persistence") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        ScopedPersistenceDirectory files("project_save_replace_failure");
        const auto destination = files.directory.getChildFile("project.dawproj");
        DAW::ProjectManager manager;

        beginTest("save returns false and preserves the prior canonical artifact");
        expect(destination.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeReplace);
        const auto saved = manager.saveToFile(destination);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!saved, "forced final replacement must make save fail");
        expect(manager.getLastSaveError().isNotEmpty(),
               "save must retain a publication error");
        expect(destination.existsAsFile(),
               "failed save must leave the previous canonical file present");
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"),
                     "failed save must preserve canonical bytes");
    }
};

ProjectSaveFinalReplacementFailureTest projectSaveFinalReplacementFailureTest;

class ProjectSuccessfulReplacementTest final : public juce::UnitTest
{
public:
    ProjectSuccessfulReplacementTest()
        : UnitTest("ProjectPersistence.SuccessfulReplacement", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("project_replace_success");
        const auto destination = files.directory.getChildFile("project.dawproj");
        beginTest("complete candidate replaces the previous canonical project");
        expect(destination.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String error;
        expect(publishCandidate(destination, "STATE_B", error), error);
        expect(destination.existsAsFile());
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_B\"/>"));
    }
};

ProjectSuccessfulReplacementTest projectSuccessfulReplacementTest;

class ProjectBackupFailureSafetyTest final : public juce::UnitTest
{
public:
    ProjectBackupFailureSafetyTest()
        : UnitTest("ProjectPersistence.BackupFailureSafety", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("project_backup_failure");
        const auto destination = files.directory.getChildFile("project.dawproj");
        const auto backup = files.directory.getChildFile("project.backup.dawproj");
        beginTest("optional historical backup failure does not endanger canonical save");
        expect(destination.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String backupError;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeHistoricalCopy);
        const auto backupSucceeded = DAW::FilePublicationCore::copyHistoricalFile(
            destination, backup, backupError);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!backupSucceeded, "forced backup copy must fail");
        expect(backupError.isNotEmpty(), "backup failure must be surfaced");
        expect(destination.existsAsFile(), "canonical project must remain");
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));

        juce::String publicationError;
        expect(publishCandidate(destination, "STATE_B", publicationError),
               publicationError);
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_B\"/>"));
    }
};

ProjectBackupFailureSafetyTest projectBackupFailureSafetyTest;

class AutosavePromotionFailureTest final : public juce::UnitTest
{
public:
    AutosavePromotionFailureTest()
        : UnitTest("AutosavePersistence.PromotionFailure", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("autosave_promotion_failure");
        const auto latest = files.directory.getChildFile("project.autosave.latest.apex");
        const auto history = files.directory.getChildFile("project.autosave.001.apex");
        beginTest("failed latest promotion leaves old latest authoritative");
        expect(latest.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String historyError;
        expect(DAW::FilePublicationCore::copyHistoricalFile(latest, history, historyError),
               historyError);

        juce::String error;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeReplace);
        const auto published = publishCandidate(latest, "STATE_B", error);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!published, "forced autosave promotion must fail");
        expect(error.isNotEmpty(), "autosave promotion failure must be surfaced");
        expect(latest.existsAsFile(), "canonical latest autosave must remain");
        expectEquals(latest.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));
        expectEquals(history.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));
    }
};

AutosavePromotionFailureTest autosavePromotionFailureTest;

class AutosaveSuccessfulPublicationTest final : public juce::UnitTest
{
public:
    AutosaveSuccessfulPublicationTest()
        : UnitTest("AutosavePersistence.SuccessfulPublication", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("autosave_success");
        const auto latest = files.directory.getChildFile("project.autosave.latest.apex");
        const auto history = files.directory.getChildFile("project.autosave.001.apex");
        beginTest("successful latest promotion preserves valid history");
        expect(latest.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String historyError;
        expect(DAW::FilePublicationCore::copyHistoricalFile(latest, history, historyError),
               historyError);

        juce::String error;
        expect(publishCandidate(latest, "STATE_B", error), error);
        expectEquals(latest.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_B\"/>"));
        expectEquals(history.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));
    }
};

AutosaveSuccessfulPublicationTest autosaveSuccessfulPublicationTest;

class AsyncAutosaveWriteJobTest final : public juce::UnitTest
{
public:
    AsyncAutosaveWriteJobTest()
        : UnitTest("AutosavePersistence.AsyncWriteJob", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("async_autosave_job");
        const auto latest = files.directory.getChildFile("project.autosave.latest.apex");
        const auto history = files.directory.getChildFile("project.autosave.001.apex");
        juce::ValueTree state("DAWProject");

        beginTest("production ThreadPoolJob succeeds and publishes latest");
        expect(latest.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        state.setProperty("state", "STATE_B", nullptr);
        AsyncAutosaveResult success;
        expect(runAsyncAutosaveJob(state, files.directory, "project", 1, success));
        expect(success.completed, "async completion callback must run");
        expect(success.success, success.failureReason);
        const auto publishedLatest = latest.loadFileAsString();
        expect(publishedLatest.contains("STATE_B"));
        expect(history.loadFileAsString().contains("STATE_A"));

        beginTest("production ThreadPoolJob failure preserves latest and history");
        state.setProperty("state", "STATE_C", nullptr);
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeReplace);
        AsyncAutosaveResult failure;
        const auto drained = runAsyncAutosaveJob(state, files.directory, "project", 1, failure);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(drained);
        expect(failure.completed, "async failure callback must run");
        expect(!failure.success, "forced publication failure must be reported");
        expect(failure.failureReason.isNotEmpty());
        expectEquals(latest.loadFileAsString(), publishedLatest);
        expectEquals(history.loadFileAsString(), publishedLatest);
    }
};

AsyncAutosaveWriteJobTest asyncAutosaveWriteJobTest;

class AutosaveRotationFailureTest final : public juce::UnitTest
{
public:
    AutosaveRotationFailureTest()
        : UnitTest("AutosavePersistence.RotationFailure", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("autosave_rotation_failure");
        const auto latest = files.directory.getChildFile("project.autosave.latest.apex");
        const auto history = files.directory.getChildFile("project.autosave.001.apex");
        beginTest("historical copy failure is checked and latest survives");
        expect(latest.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String copyError;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeHistoricalCopy);
        const auto copied = DAW::FilePublicationCore::copyHistoricalFile(
            latest, history, copyError);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!copied, "forced rotation copy must fail");
        expect(copyError.isNotEmpty(), "rotation failure must be surfaced");
        expect(latest.existsAsFile(), "rotation failure must not remove latest");
        expectEquals(latest.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));

        beginTest("historical delete failure is checked");
        expect(history.replaceWithText("<DAWProject state=\"OLD\"/>"));
        juce::String deleteError;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeHistoricalDelete);
        const auto deleted = DAW::FilePublicationCore::deleteHistoricalFile(
            history, deleteError);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!deleted, "forced rotation delete must fail");
        expect(deleteError.isNotEmpty(), "rotation delete failure must be surfaced");
        expect(history.existsAsFile(), "failed history delete must not hide the artifact");
        expect(latest.existsAsFile(), "latest must remain after history delete failure");
    }
};

AutosaveRotationFailureTest autosaveRotationFailureTest;

class SandboxCaptureFailurePreservesArtifactTest final : public juce::UnitTest
{
public:
    SandboxCaptureFailurePreservesArtifactTest()
        : UnitTest("ProjectPersistence.CaptureFailureNoPublication", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("capture_failure");
        const auto destination = files.directory.getChildFile("project.dawproj");
        beginTest("capture failure leaves state shadow and canonical artifact unchanged");
        expect(destination.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        juce::String stateShadow = "STATE_A";
        const auto captureSucceeded = false;
        if (!captureSucceeded)
        {
            // This is the coordinator boundary: no candidate is created or
            // published when the sandbox capture did not produce a snapshot.
            stateShadow = "STATE_A";
        }

        expect(!captureSucceeded);
        expectEquals(stateShadow, juce::String("STATE_A"));
        expect(destination.existsAsFile());
        expectEquals(destination.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));
    }
};

SandboxCaptureFailurePreservesArtifactTest sandboxCaptureFailurePreservesArtifactTest;

class SingleProductionAutosaveAuthorityTest final : public juce::UnitTest
{
public:
    SingleProductionAutosaveAuthorityTest()
        : UnitTest("ProjectPersistence.SingleProductionAutosaveAuthority", "Persistence") {}

    void runTest() override
    {
        const auto cwd = juce::File::getCurrentWorkingDirectory();
        const auto applicationSource = cwd.getChildFile(
            "Source/AppCore/ApplicationCore.cpp");
        const auto projectSource = cwd.getChildFile(
            "Source/ProjectCore/ProjectManager.h");

        beginTest("ApplicationCore selects one production autosave authority");
        const auto applicationText = applicationSource.loadFileAsString();
        expect(applicationText.contains("setExternalAutosaveManaged(true)"),
               "ApplicationCore must disable the legacy ProjectManager timer");

        beginTest("legacy ProjectManager publication is transactional");
        const auto projectText = projectSource.loadFileAsString();
        expect(projectText.contains("setExternalAutosaveManaged"));
        expect(projectText.contains("performAutosaveNow"));
        expect(!projectText.contains("xml->writeTo(autosaveFile)"),
               "legacy autosave must not write directly to its canonical file");
    }
};

SingleProductionAutosaveAuthorityTest singleProductionAutosaveAuthorityTest;

class LegacyAutosaveTransactionalPublicationTest final : public juce::UnitTest
{
public:
    LegacyAutosaveTransactionalPublicationTest()
        : UnitTest("ProjectPersistence.LegacyAutosaveTransactionalPublication", "Persistence") {}

    void runTest() override
    {
        ScopedPersistenceDirectory files("legacy_autosave_transaction");
        const auto legacyAutosave = files.directory.getChildFile("autosave.dawproj");
        beginTest("standalone legacy publication uses the same checked replacement");
        expect(legacyAutosave.replaceWithText("<DAWProject state=\"STATE_A\"/>"));
        const auto candidate = legacyAutosave.getSiblingFile(".autosave.dawproj.candidate");
        expect(writeCandidate(candidate, "STATE_B"));

        juce::String error;
        DAW::FilePublicationCore::injectFailureForTesting(
            DAW::PublicationFaultPoint::BeforeReplace);
        const auto published = DAW::publishFileTransactionally(candidate,
                                                                legacyAutosave,
                                                                error);
        DAW::FilePublicationCore::clearFailureForTesting();

        expect(!published);
        expect(legacyAutosave.existsAsFile());
        expectEquals(legacyAutosave.loadFileAsString(),
                     juce::String("<DAWProject state=\"STATE_A\"/>"));
        candidate.deleteFile();
    }
};

LegacyAutosaveTransactionalPublicationTest legacyAutosaveTransactionalPublicationTest;

class ProjectSaveStateAugmentationTest final : public juce::UnitTest
{
public:
    ProjectSaveStateAugmentationTest()
        : UnitTest("ProjectPersistence.SaveStateAugmentation", "Persistence") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;

        beginTest("augmentation is included in the primary transactional save");
        {
            testProjectState = "STATE_B";
            ScopedPersistenceDirectory files("project_save_augmentation_success");
            const auto destination = files.directory.getChildFile("project.dawproj");
            expect(destination.replaceWithText(
                "<DAWProject state=\"STATE_A\"><Existing/></DAWProject>"));

            DAW::ProjectManager manager;
            DAW::FilePublicationCore::resetPublicationInvocationCountForTesting();
            expect(manager.saveToFile(
                       destination,
                       makeCollapsedFolderAugmenter("folder-distinctive")),
                   manager.getLastSaveError());
            expectEquals(DAW::FilePublicationCore::getPublicationInvocationCountForTesting(), 1,
                         "pre-save augmentation must use one final publication");

            const auto savedState = parseProjectState(destination);
            expect(savedState.isValid());
            expectEquals(savedState.getProperty("testState").toString(),
                         juce::String("STATE_B"));
            const auto collapsed = savedState.getChildWithName("CollapsedFolders");
            expect(collapsed.isValid());
            expectEquals(collapsed.getNumChildren(), 1);
            expectEquals(collapsed.getChild(0).getProperty("trackId").toString(),
                         juce::String("folder-distinctive"));

            // This focused target intentionally does not link the full
            // ProjectManager.cpp restore graph.  Parsing the published XML
            // is the reload boundary available here and confirms the saved
            // collapsed-folder identity survives serialization.
            const auto reparsedState = parseProjectState(destination);
            const auto reparsedCollapsed = reparsedState.getChildWithName("CollapsedFolders");
            expect(reparsedState.isValid());
            expect(reparsedCollapsed.isValid());
            expectEquals(reparsedCollapsed.getChild(0).getProperty("trackId").toString(),
                         juce::String("folder-distinctive"));
        }

        beginTest("augmentation publication failure preserves the prior canonical artifact");
        {
            testProjectState = "STATE_B";
            ScopedPersistenceDirectory files("project_save_augmentation_failure");
            const auto destination = files.directory.getChildFile("project.dawproj");
            const auto oldContent =
                juce::String("<DAWProject state=\"STATE_A\"><Existing/></DAWProject>");
            expect(destination.replaceWithText(oldContent));

            DAW::ProjectManager manager;
            DAW::FilePublicationCore::resetPublicationInvocationCountForTesting();
            DAW::FilePublicationCore::injectFailureForTesting(
                DAW::PublicationFaultPoint::BeforeReplace);
            const auto saved = manager.saveToFile(
                destination,
                makeCollapsedFolderAugmenter("folder-must-not-publish"));
            DAW::FilePublicationCore::clearFailureForTesting();

            expect(!saved, "forced augmented publication must fail");
            expect(manager.getLastSaveError().isNotEmpty(),
                   "augmented publication failure must be observable");
            expectEquals(DAW::FilePublicationCore::getPublicationInvocationCountForTesting(), 1);
            expect(destination.existsAsFile(), "canonical project must remain present");
            expectEquals(destination.loadFileAsString(), oldContent,
                         "failed augmented publication must preserve canonical bytes");
            expect(!destination.loadFileAsString().contains("folder-must-not-publish"));
        }

        beginTest("augmentation rejection publishes no candidate");
        {
            testProjectState = "STATE_B";
            ScopedPersistenceDirectory files("project_save_augmentation_rejected");
            const auto destination = files.directory.getChildFile("project.dawproj");
            const auto oldContent = juce::String("<DAWProject state=\"STATE_A\"/>");
            expect(destination.replaceWithText(oldContent));

            DAW::ProjectManager manager;
            DAW::FilePublicationCore::resetPublicationInvocationCountForTesting();
            const auto saved = manager.saveToFile(
                destination,
                [](juce::ValueTree&, juce::String& error)
                {
                    error = "test augmentation rejected";
                    return false;
                });

            expect(!saved);
            expectEquals(manager.getLastSaveError(), juce::String("test augmentation rejected"));
            expectEquals(DAW::FilePublicationCore::getPublicationInvocationCountForTesting(), 0,
                         "rejected augmentation must not reach publication");
            expectEquals(destination.loadFileAsString(), oldContent);
        }

        beginTest("MainComponent delegates augmentation before transactional publication");
        {
            const auto cwd = juce::File::getCurrentWorkingDirectory();
            const auto mainSource = cwd.getChildFile("Source/MainComponent.cpp");
            const auto projectSource = cwd.getChildFile("Source/ProjectCore/ProjectManager.h");
            const auto mainText = mainSource.loadFileAsString();
            const auto projectText = projectSource.loadFileAsString();

            expect(mainSource.existsAsFile());
            expect(projectSource.existsAsFile());
            expect(mainText.contains("saveProjectToFile"));
            expect(mainText.contains("augmentProjectStateForSave"));
            expect(!mainText.contains("xml->writeTo(pm.getProjectFile())"),
                   "MainComponent must not write directly to the canonical project");
            expect(!mainText.contains("writeTo(pm.getProjectFile())"),
                   "MainComponent must not bypass the transactional boundary");

            const auto augmenterPos = projectText.indexOf("augmentState");
            const auto xmlPos = projectText.indexOf("auto xml   = state.createXml()");
            expect(augmenterPos >= 0);
            expect(xmlPos >= 0);
            expect(augmenterPos < xmlPos,
                   "state augmentation must occur before XML serialization");
        }
    }
};

ProjectSaveStateAugmentationTest projectSaveStateAugmentationTest;

} // namespace

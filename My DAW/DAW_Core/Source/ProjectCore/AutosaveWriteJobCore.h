#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>

#include "AutosavePublicationAuthority.h"
#include "FilePublicationCore.h"

namespace DAW
{
namespace detail
{

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
/** Test-only pause points for exercising late worker completion safely. */
struct AutosaveWriteJobTestHooks final
{
    static void armPauseBeforePublicationForTesting() noexcept
    {
        publicationPauseEntered().reset();
        publicationPauseRelease().reset();
        jobFinished().reset();
        publicationPauseRequested().store(true, std::memory_order_release);
    }

    static void armPauseAtStartForTesting() noexcept
    {
        startPauseEntered().reset();
        startPauseRelease().reset();
        jobFinished().reset();
        startPauseRequested().store(true, std::memory_order_release);
    }

    static bool waitForPublicationPauseForTesting(int timeoutMs) noexcept
    {
        return publicationPauseEntered().wait(timeoutMs);
    }

    static void armPauseAfterCommitAcquisitionForTesting() noexcept
    {
        commitPauseEntered().reset();
        commitPauseRelease().reset();
        jobFinished().reset();
        commitPauseRequested().store(true, std::memory_order_release);
    }

    static bool waitForCommitPauseForTesting(int timeoutMs) noexcept
    {
        return commitPauseEntered().wait(timeoutMs);
    }

    static bool waitForStartPauseForTesting(int timeoutMs) noexcept
    {
        return startPauseEntered().wait(timeoutMs);
    }

    static void releasePauseForTesting() noexcept
    {
        publicationPauseRequested().store(false, std::memory_order_release);
        startPauseRequested().store(false, std::memory_order_release);
        commitPauseRequested().store(false, std::memory_order_release);
        publicationPauseRelease().signal();
        startPauseRelease().signal();
        commitPauseRelease().signal();
    }

    static bool waitForJobFinishedForTesting(int timeoutMs) noexcept
    {
        return jobFinished().wait(timeoutMs);
    }

    static void pauseAtStartIfArmed() noexcept
    {
        if (startPauseRequested().exchange(false, std::memory_order_acq_rel))
        {
            startPauseEntered().signal();
            startPauseRelease().wait(10000);
        }
    }

    static void pauseBeforePublicationIfArmed() noexcept
    {
        if (publicationPauseRequested().exchange(false, std::memory_order_acq_rel))
        {
            publicationPauseEntered().signal();
            publicationPauseRelease().wait(10000);
        }
    }

    static void pauseAfterCommitAcquisitionIfArmed() noexcept
    {
        if (commitPauseRequested().exchange(false, std::memory_order_acq_rel))
        {
            commitPauseEntered().signal();
            commitPauseRelease().wait(10000);
        }
    }

    static void signalJobFinishedForTesting() noexcept
    {
        jobFinished().signal();
    }

private:
    static std::atomic<bool>& publicationPauseRequested() noexcept
    {
        static std::atomic<bool> value { false };
        return value;
    }

    static std::atomic<bool>& startPauseRequested() noexcept
    {
        static std::atomic<bool> value { false };
        return value;
    }

    static std::atomic<bool>& commitPauseRequested() noexcept
    {
        static std::atomic<bool> value { false };
        return value;
    }

    static juce::WaitableEvent& publicationPauseEntered() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& publicationPauseRelease() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& startPauseEntered() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& startPauseRelease() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& commitPauseEntered() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& commitPauseRelease() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }

    static juce::WaitableEvent& jobFinished() noexcept
    {
        static juce::WaitableEvent event;
        return event;
    }
};
#endif

/**
 * Background autosave publication job.
 *
 * This is kept separate from AutosaveManagerCore so the exact ThreadPoolJob
 * used by production can be exercised by focused persistence tests without
 * constructing the full application graph.  The manager remains the owner of
 * the job and its completion callback.
 */
class AutosaveWriteJob final : public juce::ThreadPoolJob
{
public:
    using CompletionCallback = std::function<void(bool, juce::File, juce::String)>;

    AutosaveWriteJob(juce::ValueTree stateToWrite,
                     juce::File      targetDir,
                     juce::String    projectName,
                     int             keepCount,
                     std::shared_ptr<AutosavePublicationAuthority> publicationAuthority,
                     AutosavePublicationAuthority::Generation generation,
                     CompletionCallback completionCallback)
        : juce::ThreadPoolJob("AutosaveWrite"),
          state_      (std::move(stateToWrite)),
          targetDir_  (std::move(targetDir)),
          projectName_(std::move(projectName)),
          keepCount_  (keepCount),
          authority_  (std::move(publicationAuthority)),
          generation_ (generation),
          onComplete_ (std::move(completionCallback))
    {}

    JobStatus runJob() override
    {
#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        struct CompletionSignal final
        {
            ~CompletionSignal()
            {
                AutosaveWriteJobTestHooks::signalJobFinishedForTesting();
            }
        } completionSignal;
        AutosaveWriteJobTestHooks::pauseAtStartIfArmed();
#endif

        const auto isCancelled = [this]() noexcept
        {
            return shouldExit() || !isGenerationOpen();
        };

        const auto cancel = [this]() -> JobStatus
        {
            if (onComplete_)
                onComplete_(false, {}, "Autosave cancelled during shutdown");
            return jobHasFinished;
        };

        if (isCancelled())
            return cancel();

        const auto fail = [this](const juce::String& reason) -> JobStatus
        {
            const auto detail = reason.isNotEmpty()
                ? reason
                : juce::String("Autosave publication failed");
            juce::Logger::writeToLog("[Autosave] " + detail);
            if (onComplete_)
                onComplete_(false, {}, detail);
            return jobHasFinished;
        };

        const auto directoryResult = targetDir_.createDirectory();
        if (isCancelled())
            return cancel();
        if (directoryResult.failed())
            return fail("Could not create autosave directory: "
                        + directoryResult.getErrorMessage());

        auto latestFile = targetDir_.getChildFile(projectName_ + ".autosave.latest.apex");
        juce::TemporaryFile candidate(latestFile, juce::TemporaryFile::useHiddenFile);

        // 1. Write and close a complete candidate in the same directory.
        bool writeOk = false;
        {
            juce::FileOutputStream out(candidate.getFile());
            if (out.openedOk())
            {
                auto xml = state_.createXml();
                if (xml != nullptr)
                {
                    xml->writeTo(out);
                    out.flush();
                    writeOk = out.getStatus().wasOk();
                }
            }
        }

        if (isCancelled())
            return cancel();

        if (!writeOk || !candidate.getFile().existsAsFile())
            return fail("Autosave candidate write failed");

        // Reopen the candidate through the XML parser before touching the
        // canonical latest path.
        if (juce::XmlDocument::parse(candidate.getFile()) == nullptr)
            return fail("Autosave candidate parse failed");

        if (isCancelled())
            return cancel();

        // 2. Preserve the previous latest as history without ever moving it
        // away from its canonical pathname.
        juce::String rotationError;
        if (latestFile.existsAsFile()
            && !rotateBackups(latestFile, rotationError))
        {
            if (isCancelled())
                return cancel();
            return fail("Autosave history rotation failed: " + rotationError);
        }

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        AutosaveWriteJobTestHooks::pauseBeforePublicationIfArmed();
#endif

        // This is the mandatory late authority check. It occurs after all
        // potentially blocking candidate and history work and immediately
        // before the atomic commit-admission transition.
        if (isCancelled())
            return cancel();

        // 3. Atomically acquire OPEN(g) -> COMMITTING(g).  This transition,
        // rather than the earlier check, is the authoritative linearization
        // point.  Once admitted, the commit uses only job-owned state and is
        // not interrupted by a later shutdown request.
        juce::String publicationError;
        bool published = false;
        {
            auto commitPermit = authority_->tryAcquireCommit(generation_);
            if (!commitPermit)
                return cancel();

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
            AutosaveWriteJobTestHooks::pauseAfterCommitAcquisitionIfArmed();
#endif

            published = DAW::publishFileTransactionally(candidate.getFile(),
                                                         latestFile,
                                                         publicationError);
        }

        if (!published)
        {
            return fail("Autosave latest publication failed: " + publicationError);
        }

        if (isCancelled())
            return cancel();

        // Retention is separate from canonical publication.  A pruning
        // failure is reported, but the newly published latest remains valid.
        juce::String pruneError;
        if (!pruneBackups(pruneError))
        {
            if (isCancelled())
                return cancel();
            return fail("Autosave history pruning failed: " + pruneError);
        }

        juce::Logger::writeToLog("[Autosave] Autosave written: "
                                 + latestFile.getFullPathName());
        if (onComplete_)
            onComplete_(true, latestFile, {});
        return jobHasFinished;
    }

private:
    bool rotateBackups(const juce::File& latestFile, juce::String& error)
    {
        error.clear();

        if (shouldExit() || !isGenerationOpen())
        {
            error = "Autosave cancelled before history rotation";
            return false;
        }

        // Find a free numbered slot first.  If all slots are occupied, delete
        // the oldest slot only after checking that operation, then copy the
        // old latest into that slot.  The canonical latest is never moved.
        int nextSlot = 0;
        for (int i = 1; i <= keepCount_; ++i)
        {
            auto candidate = targetDir_.getChildFile(
                projectName_ + ".autosave." + juce::String(i).paddedLeft('0', 3) + ".apex");
            if (!candidate.existsAsFile())
            {
                nextSlot = i;
                break;
            }
        }

        if (nextSlot == 0)
        {
            nextSlot = 1;
            const auto oldest = targetDir_.getChildFile(
                projectName_ + ".autosave.001.apex");
            if (shouldExit() || !isGenerationOpen())
            {
                error = "Autosave cancelled before historical deletion";
                return false;
            }
            if (!DAW::FilePublicationCore::deleteHistoricalFile(oldest, error))
                return false;
        }

        auto dest = targetDir_.getChildFile(
            projectName_ + ".autosave." + juce::String(nextSlot).paddedLeft('0', 3) + ".apex");

        if (shouldExit() || !isGenerationOpen())
        {
            error = "Autosave cancelled before historical copy";
            return false;
        }

        return DAW::FilePublicationCore::copyHistoricalFile(latestFile, dest, error);
    }

    bool pruneBackups(juce::String& error)
    {
        error.clear();

        // Prune: delete numbered backups beyond keepCount_.
        auto all = targetDir_.findChildFiles(
            juce::File::findFiles, false, projectName_ + ".autosave.???.apex");
        all.sort();

        while (all.size() > keepCount_)
        {
            if (shouldExit() || !isGenerationOpen())
            {
                error = "Autosave cancelled before historical pruning";
                return false;
            }

            const auto oldest = all.getReference(0);
            if (!DAW::FilePublicationCore::deleteHistoricalFile(oldest, error))
                return false;
            all.remove(0);
        }

        return true;
    }

    juce::ValueTree state_;
    juce::File      targetDir_;
    juce::String    projectName_;
    int             keepCount_;
    std::shared_ptr<AutosavePublicationAuthority> authority_;
    AutosavePublicationAuthority::Generation generation_ = 0;
    CompletionCallback onComplete_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutosaveWriteJob)

    bool isGenerationOpen() const noexcept
    {
        return authority_ != nullptr && authority_->isGenerationOpen(generation_);
    }
};

} // namespace detail
} // namespace DAW

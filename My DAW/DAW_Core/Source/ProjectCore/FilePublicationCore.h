#pragma once

#include <JuceHeader.h>

#include <atomic>

namespace DAW
{

/**
 * Small, shared persistence boundary for project and autosave files.
 *
 * The caller owns the candidate and is responsible for writing and closing it
 * before publication.  Publication is deliberately limited to a same-directory
 * JUCE replacement so a failed replacement does not require deleting the
 * canonical destination first.
 */
enum class PublicationFaultPoint : int
{
    None = 0,
    BeforeReplace,
    BeforeHistoricalCopy,
    BeforeHistoricalDelete
};

struct FilePublicationCore final
{
    /**
     * Replaces destination with a complete, closed candidate.
     *
     * JUCE's File::replaceFileIn() maps to ReplaceFile on Windows when the
     * destination exists.  Its implementation returns before deleting the
     * destination when replacement fails.  The same-directory check keeps the
     * operation within the supported replacement boundary.
     */
    static bool publishFileTransactionally(const juce::File& candidate,
                                           const juce::File& destination,
                                           juce::String& error)
    {
        error.clear();

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        publicationInvocationCount().fetch_add(1, std::memory_order_acq_rel);
#endif

        if (!validateCandidateAndDestination(candidate, destination, error))
            return false;

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (consumeFault(PublicationFaultPoint::BeforeReplace))
        {
            error = "Injected final publication failure for "
                + destination.getFullPathName();
            return false;
        }
#endif

        if (!candidate.replaceFileIn(destination))
        {
            error = "Could not replace canonical file '"
                + destination.getFullPathName() + "' with candidate '"
                + candidate.getFullPathName() + "'";
            return false;
        }

        // A successful replacement must leave a readable canonical pathname.
        // The candidate is intentionally not treated as authority after this
        // point; JUCE owns its cleanup as part of replaceFileIn().
        if (!destination.existsAsFile())
        {
            error = "Replacement reported success but canonical file is missing: "
                + destination.getFullPathName();
            return false;
        }

        return true;
    }

    /** Copy an existing artifact into historical retention. */
    static bool copyHistoricalFile(const juce::File& source,
                                   const juce::File& destination,
                                   juce::String& error)
    {
        error.clear();

        if (!source.existsAsFile())
        {
            error = "Historical source is missing: " + source.getFullPathName();
            return false;
        }

        if (destination == juce::File() || destination.isDirectory())
        {
            error = "Historical destination is invalid: "
                + destination.getFullPathName();
            return false;
        }

        if (source == destination)
        {
            error = "Historical source and destination are identical: "
                + source.getFullPathName();
            return false;
        }

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (consumeFault(PublicationFaultPoint::BeforeHistoricalCopy))
        {
            error = "Injected historical-copy failure for "
                + destination.getFullPathName();
            return false;
        }
#endif

        if (!source.copyFileTo(destination) || !destination.existsAsFile())
        {
            error = "Could not copy historical file '"
                + source.getFullPathName() + "' to '"
                + destination.getFullPathName() + "'";
            return false;
        }

        return true;
    }

    /** Delete a historical artifact and verify that it is gone. */
    static bool deleteHistoricalFile(const juce::File& file,
                                     juce::String& error)
    {
        error.clear();

        if (!file.existsAsFile())
            return true;

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
        if (consumeFault(PublicationFaultPoint::BeforeHistoricalDelete))
        {
            error = "Injected historical-delete failure for "
                + file.getFullPathName();
            return false;
        }
#endif

        if (!file.deleteFile() || file.existsAsFile())
        {
            error = "Could not delete historical file: "
                + file.getFullPathName();
            return false;
        }

        return true;
    }

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    /** Inject one deterministic failure into the selected persistence step. */
    static void injectFailureForTesting(PublicationFaultPoint point) noexcept
    {
        injectedFault().store(static_cast<int>(point), std::memory_order_release);
    }

    static void clearFailureForTesting() noexcept
    {
        injectFailureForTesting(PublicationFaultPoint::None);
    }

    static void resetPublicationInvocationCountForTesting() noexcept
    {
        publicationInvocationCount().store(0, std::memory_order_release);
    }

    static int getPublicationInvocationCountForTesting() noexcept
    {
        return publicationInvocationCount().load(std::memory_order_acquire);
    }
#endif

private:
    static bool validateCandidateAndDestination(const juce::File& candidate,
                                                const juce::File& destination,
                                                juce::String& error)
    {
        if (candidate == juce::File() || destination == juce::File())
        {
            error = "Candidate and destination must both be valid files";
            return false;
        }

        if (candidate == destination)
        {
            error = "Candidate and destination must be different files";
            return false;
        }

        if (!candidate.existsAsFile())
        {
            error = "Publication candidate is missing: "
                + candidate.getFullPathName();
            return false;
        }

        if (destination.isDirectory())
        {
            error = "Publication destination is a directory: "
                + destination.getFullPathName();
            return false;
        }

        if (candidate.getParentDirectory().getFullPathName()
            != destination.getParentDirectory().getFullPathName())
        {
            error = "Candidate and destination must share a directory";
            return false;
        }

        return true;
    }

#if defined(APEX_ENABLE_TEST_HOOKS) && APEX_ENABLE_TEST_HOOKS
    static std::atomic<int>& injectedFault() noexcept
    {
        static std::atomic<int> fault {
            static_cast<int>(PublicationFaultPoint::None)
        };
        return fault;
    }

    static bool consumeFault(PublicationFaultPoint point) noexcept
    {
        auto& fault = injectedFault();
        int expected = static_cast<int>(point);
        return fault.compare_exchange_strong(expected,
                                             static_cast<int>(PublicationFaultPoint::None),
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
    }

    static std::atomic<int>& publicationInvocationCount() noexcept
    {
        static std::atomic<int> count { 0 };
        return count;
    }
#endif

    JUCE_DECLARE_NON_COPYABLE(FilePublicationCore)
};

inline bool publishFileTransactionally(const juce::File& candidate,
                                        const juce::File& destination,
                                        juce::String& error)
{
    return FilePublicationCore::publishFileTransactionally(candidate, destination, error);
}

} // namespace DAW

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace DAW
{

/**
 * Lifetime-independent publication authority for autosave jobs.
 *
 * AutosaveManagerCore owns the current session's handle, while every job and
 * delayed completion callback holds a shared reference. Revocation therefore
 * remains observable even if the manager returns from shutdown while a worker
 * is still inside filesystem I/O.
 *
 * The packed atomic state is the sole authority for both commit admission and
 * revocation.  A commit permit is the linearization point: OPEN(g) can be
 * changed only once to COMMITTING(g), and a revoked state cannot be reopened
 * by an old worker.
 */
class AutosavePublicationAuthority final
{
public:
    using Generation = std::uint64_t;

    enum class State : std::uint8_t
    {
        Revoked = 0,
        Open = 1,
        Committing = 2,
        CommittingRevoked = 3
    };

    /**
     * An admitted commit owns this permit until it leaves the final
     * publication boundary.  Its destructor releases the state-machine slot;
     * the worker keeps the shared authority alive independently of its owner.
     */
    class CommitPermit final
    {
    public:
        CommitPermit() noexcept = default;

        CommitPermit(CommitPermit&& other) noexcept
            : authority_(std::exchange(other.authority_, nullptr)),
              generation_(other.generation_),
              ownsCommit_(std::exchange(other.ownsCommit_, false))
        {
        }

        CommitPermit& operator=(CommitPermit&& other) noexcept
        {
            if (this != &other)
            {
                release();
                authority_ = std::exchange(other.authority_, nullptr);
                generation_ = other.generation_;
                ownsCommit_ = std::exchange(other.ownsCommit_, false);
            }
            return *this;
        }

        CommitPermit(const CommitPermit&) = delete;
        CommitPermit& operator=(const CommitPermit&) = delete;

        ~CommitPermit() { release(); }

        explicit operator bool() const noexcept { return ownsCommit_; }

    private:
        friend class AutosavePublicationAuthority;

        CommitPermit(AutosavePublicationAuthority& authority,
                     Generation generation) noexcept
            : authority_(&authority), generation_(generation), ownsCommit_(true)
        {
        }

        void release() noexcept
        {
            if (ownsCommit_ && authority_ != nullptr)
                authority_->releaseCommit(generation_);
            ownsCommit_ = false;
            authority_ = nullptr;
        }

        AutosavePublicationAuthority* authority_ = nullptr;
        Generation generation_ = 0;
        bool ownsCommit_ = false;
    };

    /** Start a new manager session only after all prior commits are drained. */
    bool beginGeneration() noexcept
    {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto decoded = decode(observed);
            if (decoded.state != State::Revoked)
                return false;

            const auto desired = pack(State::Open, nextGeneration(decoded.generation));
            if (state_.compare_exchange_weak(observed,
                                              desired,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
                return true;
        }
    }

    /** Revoke open authority, or mark an admitted commit as revocation-pending. */
    void revoke() noexcept
    {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto decoded = decode(observed);
            State desiredState = decoded.state;
            Generation desiredGeneration = decoded.generation;

            switch (decoded.state)
            {
                case State::Open:
                    desiredState = State::Revoked;
                    desiredGeneration = nextGeneration(decoded.generation);
                    break;

                case State::Committing:
                    desiredState = State::CommittingRevoked;
                    break;

                case State::Revoked:
                case State::CommittingRevoked:
                    return;
            }

            if (state_.compare_exchange_weak(observed,
                                              pack(desiredState, desiredGeneration),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
                return;
        }
    }

    Generation currentGeneration() const noexcept
    {
        return decode(state_.load(std::memory_order_acquire)).generation;
    }

    /**
     * Check whether a generation is open for a new commit.  This is only a
     * scheduling/cancellation hint; tryAcquireCommit() is the final authority.
     */
    bool isGenerationOpen(Generation expectedGeneration) const noexcept
    {
        const auto decoded = decode(state_.load(std::memory_order_acquire));
        return decoded.state == State::Open
            && decoded.generation == expectedGeneration;
    }

    /**
     * Atomically acquire exclusive final-publication authority:
     * OPEN(g) -> COMMITTING(g).  Failure means the caller must not publish.
     */
    CommitPermit tryAcquireCommit(Generation expectedGeneration) noexcept
    {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto decoded = decode(observed);
            if (decoded.state != State::Open
                || decoded.generation != expectedGeneration)
                return {};

            if (state_.compare_exchange_weak(observed,
                                              pack(State::Committing, expectedGeneration),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
                return CommitPermit(*this, expectedGeneration);
        }
    }

    State getState() const noexcept
    {
        return decode(state_.load(std::memory_order_acquire)).state;
    }

private:
    struct DecodedState final
    {
        State state;
        Generation generation;
    };

    static constexpr unsigned kStateBits = 2;
    static constexpr std::uint64_t kStateMask = (1u << kStateBits) - 1u;
    // Generations are plain logical sequence numbers stored in bits [2, 64).
    // pack() shifts them up and decode() shifts them down; the low bits must
    // never be stripped before packing, or generations 1..3 collapse onto 0
    // and stale workers of an old session regain authority in a new one.
    static constexpr Generation kGenerationMax =
        static_cast<Generation>(~static_cast<Generation>(0)) >> kStateBits;

    static std::uint64_t pack(State state, Generation generation) noexcept
    {
        return (generation << kStateBits)
            | static_cast<std::uint64_t>(state);
    }

    static DecodedState decode(std::uint64_t packed) noexcept
    {
        return { static_cast<State>(packed & kStateMask),
                 static_cast<Generation>(packed >> kStateBits) };
    }

    static Generation nextGeneration(Generation generation) noexcept
    {
        auto next = generation + 1u;
        return next > kGenerationMax ? 1 : next;
    }

    void releaseCommit(Generation expectedGeneration) noexcept
    {
        auto observed = state_.load(std::memory_order_acquire);
        for (;;)
        {
            const auto decoded = decode(observed);
            if (decoded.generation != expectedGeneration)
                return;

            State desiredState = State::Revoked;
            Generation desiredGeneration = decoded.generation;
            if (decoded.state == State::Committing)
            {
                desiredState = State::Open;
            }
            else if (decoded.state == State::CommittingRevoked)
            {
                desiredState = State::Revoked;
                desiredGeneration = nextGeneration(decoded.generation);
            }
            else
            {
                return;
            }

            if (state_.compare_exchange_weak(observed,
                                              pack(desiredState, desiredGeneration),
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
                return;
        }
    }

    std::atomic<std::uint64_t> state_ { pack(State::Revoked, 0) };
};

} // namespace DAW

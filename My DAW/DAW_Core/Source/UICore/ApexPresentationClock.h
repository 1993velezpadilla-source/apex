#pragma once

#include <JuceHeader.h>
#include <cstdint>

namespace DAW {

//==============================================================================
/**
    Display-aware adaptive presentation clock for APEX-owned visual components.

    Follows the refresh rate of the display that actually contains the APEX
    main window (registered via setMainWindow()).  Plugin editors, dialogs,
    and floating windows do NOT change the presentation target — only the
    registered APEX main window is authoritative.

    When the user moves the APEX window to a different monitor the clock
    automatically re-targets the new display's refresh rate (60–144 Hz).

    Components that need continuous visual updates register as TickReceiver
    instead of owning their own juce::Timer.  This avoids timer-storm on the
    message thread from dozens of independent timers.

    Active rate floor:
        When any receiver has requested continuous updates the clock runs at
        the display refresh rate clamped to [60, kMaxRateHz].  There is no
        active 30 Hz presentation path.

    Idle suppression:
        When no receiver has requested continuous updates the clock drops to
        a low idle-polling rate (kIdleRateHz ≈ 10 Hz) and does NOT dispatch
        ticks.  The transition between idle and active rates is immediate —
        requestContinuousUpdate and releaseContinuousUpdate call applyRate()
        synchronously so there is no ~100 ms wake-up latency.

    Clock-owned registration identity (ABA-safe):
        Each addReceiver call assigns a monotonically increasing registration
        ID.  The clock owns a Registration table containing the receiver
        pointer, registration ID, and continuous-update nesting count.
        ScopedUpdate stores a registration ID — never a raw TickReceiver*.
        Releasing a ScopedUpdate validates the registration ID against the
        clock's table.  A removed receiver's registration is tombstoned then
        compacted; a replacement receiver at the same memory address gets a
        different registration ID and cannot be affected by stale guards.

    Per-receiver continuous-update ownership:
        activeRequests_ counts ACTIVE RECEIVERS (registrations with
        updateCount > 0), not total nested requests.  A receiver with 3
        nested requests contributes exactly 1 to activeRequests_.

    Message-thread-only ownership:
        All public mutation methods (addReceiver, removeReceiver,
        requestContinuousUpdate, releaseContinuousUpdate) are message-thread
        only.  No mutex is held during dispatch — safety comes from the
        single-threaded message-thread invariant plus tombstone slots.

    Receiver lifetime safety (tombstone slots):
        removeReceiver replaces the registration's receiver pointer with
        nullptr immediately.  The receiver may then be destroyed safely —
        the dispatch loop skips nullptr slots.  Tombstones are compacted
        after dispatch completes (or immediately if not dispatching).

    Dispatch mutation safety:
        The dispatch loop captures the registration count at frame start
        (initialCount).  Receivers added during dispatch are appended but
        not dispatched until the next frame.  Receivers removed during
        dispatch are tombstoned and skipped.  No receiver is dispatched
        twice in one frame, no restart loop occurs, and no per-frame heap
        allocation is performed.  A ScopedValueSetter reentrancy guard
        prevents nested message-loop dispatch.

    Registration enforcement:
        requestContinuousUpdate requires that the receiver is currently
        registered (via addReceiver).  Calling request on an unregistered
        receiver triggers a debug assertion and returns 0 (invalid ID).

    Display transition detection:
        The clock listens for componentMovedOrResized on the registered
        main window (event-driven).  An elapsed-time fallback re-checks
        the display every 1 second as a safety net.

    Usage:
        class MyComponent : public ApexPresentationClock::TickReceiver {
            void onPresentationTick(double deltaSeconds) override {
                if (isAnimating_) repaint(dirtyRect_);
            }
            void startAnimation() {
                ApexPresentationClock::instance().requestContinuousUpdate(this);
            }
            void stopAnimation() {
                ApexPresentationClock::instance().releaseContinuousUpdate(this);
            }
        };

    Register in constructor (message thread):
        ApexPresentationClock::instance().addReceiver(this);

    Unregister in destructor (message thread):
        ApexPresentationClock::instance().removeReceiver(this);

    The clock fires on the message thread.  It is NOT a realtime clock —
    do not perform audio work here.
*/
class ApexPresentationClock final : private juce::Timer,
                                    private juce::ComponentListener
{
public:
    //==============================================================================
    /** Tick receiver interface.  Receivers are registered/unregistered via
        addReceiver/removeReceiver.  The clock owns all continuous-update
        state — TickReceiver itself is a pure interface with no clock
        coupling.
    */
    struct TickReceiver
    {
        virtual ~TickReceiver() = default;

        /** Called on the message thread at the presentation rate.
            @param deltaSeconds  time since the previous tick in seconds
        */
        virtual void onPresentationTick (double deltaSeconds) = 0;
    };

    //==============================================================================
    /** Access the singleton instance. */
    static ApexPresentationClock& instance()
    {
        static ApexPresentationClock clock;
        return clock;
    }

    //==============================================================================
    /** Register the APEX main application window as the authoritative
        display source for presentation rate detection.

        Must be called once during APEX initialization on the message thread.
        Pass nullptr to clear the registration (falls back to primary display).
    */
    void setMainWindow (juce::Component* mainWindow)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

        if (mainWindow_ != nullptr)
            mainWindow_->removeComponentListener (this);

        mainWindow_ = mainWindow;

        if (mainWindow_ != nullptr)
            mainWindow_->addComponentListener (this);

        updateDisplayRefreshRate();
        rateHz_ = calculateTargetRate();
        applyRate();
    }

    //==============================================================================
    /** Register a receiver to receive presentation ticks.
        Must be called on the message thread.
        The receiver MUST be unregistered before it is destroyed.

        @returns  A unique registration ID (non-zero) on success, or 0 if
                  the receiver is already registered.
    */
    uint64_t addReceiver (TickReceiver* receiver)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        jassert (receiver != nullptr);

        if (findRegistrationIndexByReceiver (receiver) >= 0)
            return 0;  // already registered — idempotent no-op

        Registration reg;
        reg.receiver       = receiver;
        reg.registrationId = nextRegistrationId_++;
        reg.updateCount    = 0;

        // Overflow guard: skip the reserved invalid ID.
        if (nextRegistrationId_ == 0)
            nextRegistrationId_ = 1;

        registrations_.add (reg);
        return reg.registrationId;
    }

    /** Unregister a receiver.
        Must be called on the message thread.
        Safe to call from within onPresentationTick().

        Tombstones the registration (sets receiver = nullptr) so the
        receiver may be destroyed immediately after this method returns.
        Exactly one active-receiver contribution is removed, regardless
        of nesting depth.  If not dispatching, tombstones are compacted
        immediately.
    */
    void removeReceiver (TickReceiver* receiver)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        jassert (receiver != nullptr);

        const int index = findRegistrationIndexByReceiver (receiver);
        if (index < 0)
            return;  // not registered

        auto& reg = registrations_.getReference (index);

        // Exactly ONE active-receiver contribution removed.
        if (reg.updateCount > 0)
        {
            reg.updateCount = 0;
            activeRequests_.fetch_sub (1, std::memory_order_acq_rel);
            applyRate();  // immediate active→idle transition if last
        }

        // Tombstone: null the receiver pointer.  Keep registrationId so
        // that a stale ScopedUpdate can detect the removal (ABA safety).
        reg.receiver = nullptr;

        if (! isDispatching_)
            compactTombstones();
    }

    //==============================================================================
    /** Returns the current presentation rate in Hz. */
    int getRateHz() const noexcept { return rateHz_; }

    /** Returns the detected display refresh rate in Hz. */
    double getDisplayRefreshRate() const noexcept { return displayRefreshRate_; }

    /** Manually override the presentation rate.
        Pass 0 to revert to automatic display-rate tracking.
        Must be called on the message thread.
    */
    void setRateHz (int hz)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

        if (hz == 0)
        {
            updateDisplayRefreshRate();
            hz = calculateTargetRate();
        }
        hz = juce::jlimit (kMinActiveRateHz, kMaxRateHz, hz);
        rateHz_ = hz;
        applyRate();
    }

    //==============================================================================
    /** Per-receiver continuous update request.

        The receiver MUST be registered via addReceiver before calling.
        activeRequests_ counts ACTIVE RECEIVERS: the first request (0→1)
        increments activeRequests_ by 1; nested requests do not.

        On the 0→1 transition applyRate() is called immediately to switch
        from idle to active rate without waiting for the next idle tick.

        Must be called on the message thread.

        @returns  The registration ID for this receiver (non-zero), or 0
                  if the receiver is not registered.
    */
    uint64_t requestContinuousUpdate (TickReceiver* receiver)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        jassert (receiver != nullptr);

        const int index = findRegistrationIndexByReceiver (receiver);
        if (index < 0)
        {
            jassertfalse;  // request on unregistered receiver
            return 0;
        }

        auto& reg = registrations_.getReference (index);

        if (reg.updateCount == 0)
        {
            activeRequests_.fetch_add (1, std::memory_order_acq_rel);
            applyRate();  // immediate idle→active transition
        }
        ++reg.updateCount;

        return reg.registrationId;
    }

    /** Per-receiver continuous update release (pointer-based).

        Must be matched with a prior requestContinuousUpdate() call
        from the same receiver.  Must be called on the message thread.

        On the 1→0 transition applyRate() is called immediately to
        switch from active to idle rate if this was the last active
        receiver.

        If the receiver is not registered, this is a safe no-op.
    */
    void releaseContinuousUpdate (TickReceiver* receiver)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        jassert (receiver != nullptr);

        const int index = findRegistrationIndexByReceiver (receiver);
        if (index < 0)
            return;  // not registered

        auto& reg = registrations_.getReference (index);

        if (reg.updateCount > 0)
        {
            --reg.updateCount;
            if (reg.updateCount == 0)
            {
                activeRequests_.fetch_sub (1, std::memory_order_acq_rel);
                applyRate();  // immediate active→idle transition if last
            }
        }
        else
        {
            jassertfalse;  // underflow: release without matching request
        }
    }

    /** Returns true if any receiver currently needs continuous updates. */
    bool hasActiveRequests() const noexcept
    {
        return activeRequests_.load (std::memory_order_acquire) > 0;
    }

    //==============================================================================
    /** Test-only: returns the number of active receivers (registrations
        with updateCount > 0).  NOT total nested requests.
    */
    int getActiveReceiverCountForTesting() const noexcept
    {
        return activeRequests_.load (std::memory_order_acquire);
    }

    /** Test-only: execute one deterministic presentation tick.
        Must be called on the message thread.
    */
    void executePresentationTickForTesting()
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        dispatchPresentationTick (1.0 / 60.0);
    }

    /** Test-only: returns the registration ID for a registered receiver,
        or 0 if not registered.
    */
    uint64_t getRegistrationIdForTesting (TickReceiver* receiver) const
    {
        const int index = findRegistrationIndexByReceiver (receiver);
        return index >= 0 ? registrations_.getReference (index).registrationId : 0;
    }

    /** Test-only: recompute active receiver count from the registration
        table.  Used to verify the activeRequests_ invariant.
        activeRequests_ must always equal the number of registrations
        with receiver != nullptr AND updateCount > 0.
    */
    int recomputeActiveReceiverCountForTesting() const
    {
        int count = 0;
        for (int i = 0; i < registrations_.size(); ++i)
        {
            const auto& reg = registrations_.getReference (i);
            if (reg.receiver != nullptr && reg.updateCount > 0)
                ++count;
        }
        return count;
    }

    //==============================================================================
    /** RAII helper for scoped continuous update requests.

        Stores a clock-owned registration ID — never a raw TickReceiver*.
        The destructor releases via releaseByRegistrationId(), which
        validates the registration ID against the clock's table.  If the
        receiver was removed (and possibly destroyed) while this guard
        was alive, the release is a safe no-op because:
        - The tombstoned registration has receiver == nullptr, OR
        - The registration was compacted (ID not found), OR
        - A replacement receiver has a different registration ID (ABA).

        Usage:
            {
                auto guard = ApexPresentationClock::ScopedUpdate (myReceiver);
                // ... continuous updates active ...
            }
            // automatically released

        The guard is move-only (not copyable) to prevent double-release.
    */
    class ScopedUpdate
    {
    public:
        /** Acquire a continuous update request for the given receiver.
            The receiver MUST be registered via addReceiver.
        */
        explicit ScopedUpdate (TickReceiver* receiver,
                               ApexPresentationClock& clock = instance())
            : clock_ (&clock)
        {
            jassert (receiver != nullptr);
            registrationId_ = clock_->requestContinuousUpdate (receiver);
        }

        /** Release the continuous update request on destruction.
            ABA-safe: uses registration ID, not raw pointer.
        */
        ~ScopedUpdate()
        {
            if (clock_ != nullptr && registrationId_ != 0)
                clock_->releaseByRegistrationId (registrationId_);
        }

        /** Move constructor — transfers ownership. */
        ScopedUpdate (ScopedUpdate&& other) noexcept
            : registrationId_ (other.registrationId_), clock_ (other.clock_)
        {
            other.registrationId_ = 0;
            other.clock_          = nullptr;
        }

        /** Move assignment — releases current, transfers ownership. */
        ScopedUpdate& operator= (ScopedUpdate&& other) noexcept
        {
            if (this != &other)
            {
                if (clock_ != nullptr && registrationId_ != 0)
                    clock_->releaseByRegistrationId (registrationId_);

                registrationId_       = other.registrationId_;
                clock_                = other.clock_;
                other.registrationId_ = 0;
                other.clock_          = nullptr;
            }
            return *this;
        }

        ScopedUpdate (const ScopedUpdate&) = delete;
        ScopedUpdate& operator= (const ScopedUpdate&) = delete;

    private:
        uint64_t registrationId_      = 0;
        ApexPresentationClock* clock_ = nullptr;
    };

    //==============================================================================
    /** Idle-polling rate in Hz when no receiver needs continuous updates. */
    static constexpr int kIdleRateHz = 10;

    /** Minimum active presentation rate (60 FPS baseline). */
    static constexpr int kMinActiveRateHz = 60;

    /** Maximum supported presentation rate. */
    static constexpr int kMaxRateHz = 144;

private:
    //==============================================================================
    /** Clock-owned registration record.  The clock owns all continuous-update
        state — TickReceiver is a pure interface with no clock coupling.
    */
    struct Registration
    {
        TickReceiver* receiver       = nullptr;  // null when tombstoned
        uint64_t      registrationId = 0;        // unique, monotonically increasing
        int           updateCount    = 0;        // continuous-update nesting count
    };

    //==============================================================================
    ApexPresentationClock()
    {
        updateDisplayRefreshRate();
        rateHz_ = calculateTargetRate();
        applyRate();
        lastTickTime_ = juce::Time::getMillisecondCounterHiRes();
        lastDisplayCheckTime_ = lastTickTime_;
    }

    ~ApexPresentationClock()
    {
        if (mainWindow_ != nullptr)
            mainWindow_->removeComponentListener (this);
    }

    //==============================================================================
    /** ABA-safe release by registration ID.  Used by ScopedUpdate.
        If the registration was removed (tombstoned or compacted), or if
        a replacement receiver has a different ID, this is a safe no-op.
    */
    void releaseByRegistrationId (uint64_t registrationId)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

        if (registrationId == 0)
            return;

        const int index = findRegistrationIndexById (registrationId);
        if (index < 0)
            return;  // compacted — registration gone entirely

        auto& reg = registrations_.getReference (index);
        if (reg.receiver == nullptr)
            return;  // tombstoned — receiver removed, not yet compacted

        if (reg.updateCount > 0)
        {
            --reg.updateCount;
            if (reg.updateCount == 0)
            {
                activeRequests_.fetch_sub (1, std::memory_order_acq_rel);
                applyRate();
            }
        }
        // else: count already 0 (e.g. cleaned up by removeReceiver) — no-op
    }

    //==============================================================================
    int findRegistrationIndexByReceiver (TickReceiver* receiver) const
    {
        for (int i = 0; i < registrations_.size(); ++i)
            if (registrations_.getReference (i).receiver == receiver)
                return i;
        return -1;
    }

    int findRegistrationIndexById (uint64_t id) const
    {
        for (int i = 0; i < registrations_.size(); ++i)
            if (registrations_.getReference (i).registrationId == id)
                return i;
        return -1;
    }

    void compactTombstones()
    {
        for (int i = registrations_.size(); --i >= 0;)
            if (registrations_.getReference (i).receiver == nullptr)
                registrations_.remove (i);
    }

    //==============================================================================
    void componentMovedOrResized (juce::Component&, bool, bool) override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastDisplayCheckTime_ < 200.0)
            return;
        lastDisplayCheckTime_ = now;

        const double oldRate = displayRefreshRate_;
        updateDisplayRefreshRate();
        if (std::abs (displayRefreshRate_ - oldRate) > 0.5)
        {
            rateHz_ = calculateTargetRate();
            applyRate();
        }
    }

    void componentBeingDeleted (juce::Component&) override
    {
        mainWindow_ = nullptr;
        updateDisplayRefreshRate();
        rateHz_ = calculateTargetRate();
        applyRate();
    }

    //==============================================================================
    void updateDisplayRefreshRate()
    {
        displayRefreshRate_ = 60.0;  // safe default

        auto& displays = juce::Desktop::getInstance().getDisplays();

        if (mainWindow_ != nullptr)
        {
            if (auto* display = displays.getDisplayForRect (mainWindow_->getBounds()))
            {
                if (display->verticalFrequencyHz.has_value()
                    && display->verticalFrequencyHz.value() > 10.0)
                {
                    displayRefreshRate_ = display->verticalFrequencyHz.value();
                    return;
                }
            }
        }

        if (auto* display = displays.getPrimaryDisplay())
        {
            if (display->verticalFrequencyHz.has_value()
                && display->verticalFrequencyHz.value() > 10.0)
                displayRefreshRate_ = display->verticalFrequencyHz.value();
        }
    }

    int calculateTargetRate() const
    {
        const int raw = (int) std::round (displayRefreshRate_);
        return juce::jlimit (kMinActiveRateHz, kMaxRateHz, raw);
    }

    void applyRate()
    {
        const bool needContinuous = activeRequests_.load (std::memory_order_acquire) > 0;
        const int targetHz = needContinuous ? rateHz_ : kIdleRateHz;

        if (targetHz != currentTimerHz_)
        {
            currentTimerHz_ = targetHz;
            startTimerHz (targetHz);
        }
    }

    //==============================================================================
    /** Core dispatch logic.  No mutex is held — safety comes from the
        message-thread-only invariant plus tombstone slots.

        - initialCount captured at frame start; receivers added during
          dispatch are appended beyond initialCount and not dispatched.
        - Receivers removed during dispatch are tombstoned (nullptr) and
          skipped on subsequent iterations.
        - ScopedValueSetter reentrancy guard prevents nested message-loop
          dispatch from recursively executing the same cycle.
        - After dispatch, tombstones are compacted.
    */
    void dispatchPresentationTick (double deltaSeconds)
    {
        if (! hasActiveRequests())
            return;

        // Reentrancy guard: if a callback triggered a nested message loop
        // that re-entered the timer, skip the nested dispatch.
        if (isDispatching_)
            return;

        const juce::ScopedValueSetter<bool> dispatchGuard (isDispatching_, true);

        const int initialCount = registrations_.size();
        for (int i = 0; i < initialCount; ++i)
        {
            // Copy the pointer BEFORE the callback.  The callback may
            // mutate the array (add/remove), but this local copy remains
            // valid for the duration of the call.
            auto* r = registrations_.getReference (i).receiver;
            if (r != nullptr)  // skip tombstones
                r->onPresentationTick (deltaSeconds);
        }

        // Compact tombstones left by removeReceiver during dispatch.
        compactTombstones();
    }

    //==============================================================================
    void timerCallback() override
    {
        applyRate();

        const double now = juce::Time::getMillisecondCounterHiRes();
        const double delta = (now - lastTickTime_) / 1000.0;
        lastTickTime_ = now;

        if (now - lastDisplayCheckTime_ >= 1000.0)
        {
            lastDisplayCheckTime_ = now;
            const double oldRate = displayRefreshRate_;
            updateDisplayRefreshRate();
            if (std::abs (displayRefreshRate_ - oldRate) > 0.5)
            {
                rateHz_ = calculateTargetRate();
                applyRate();
            }
        }

        dispatchPresentationTick (delta);
    }

    //==============================================================================
    juce::Component* mainWindow_ = nullptr;
    juce::Array<Registration> registrations_;
    uint64_t nextRegistrationId_ = 1;          // 0 = invalid
    std::atomic<int> activeRequests_ { 0 };     // count of ACTIVE RECEIVERS
    bool isDispatching_ = false;
    double displayRefreshRate_ = 60.0;
    int rateHz_ = 60;
    int currentTimerHz_ = 60;
    double lastTickTime_ = 0.0;
    double lastDisplayCheckTime_ = 0.0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexPresentationClock)
};

} // namespace DAW

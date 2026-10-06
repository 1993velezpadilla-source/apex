#pragma once
#include <JuceHeader.h>
#include "ActionID.h"

namespace DAW {

/**
 * ActionManager
 *
 * Canonical dispatcher for all DAW actions.
 * Every input source (keyboard, gamepad, bubble, menu, AI, voice) calls
 * ActionManager::getInstance().dispatch(ActionID) — never wires behaviour
 * directly to a raw key or button.
 *
 * Handlers are registered as std::function<void(float)>.
 * The float value is used for analog actions (volume, pan, scroll speed).
 * For discrete boolean actions it is always 1.0f.
 *
 * Multiple handlers per ActionID are supported (fan-out).
 */
class ActionManager
{
public:
    // ── Singleton ─────────────────────────────────────────────────────────────
    static ActionManager& getInstance()
    {
        static ActionManager instance;
        return instance;
    }

    // ── Registration ──────────────────────────────────────────────────────────

    /** Register a handler for an action. Returns a token that can be passed
     *  to unregister(). Multiple handlers for the same action are all called. */
    int registerHandler(ActionID id, std::function<void(float)> handler)
    {
        jassert(handler != nullptr);
        const int token = nextToken_++;
        handlers_[static_cast<int>(id)].push_back({ token, std::move(handler) });
        return token;
    }

    /** Convenience overload for discrete (no-value) actions. */
    int registerHandler(ActionID id, std::function<void()> handler)
    {
        return registerHandler(id, [h = std::move(handler)](float) { h(); });
    }

    /** Remove a previously registered handler by its token. */
    void unregisterHandler(int token)
    {
        for (auto& [key, list] : handlers_)
            list.erase(std::remove_if(list.begin(), list.end(),
                [token](const Entry& e) { return e.token == token; }),
                list.end());
    }

    // ── Dispatch ──────────────────────────────────────────────────────────────

    /** Dispatch an action with an optional analog value (default 1.0f). */
    void dispatch(ActionID id, float value = 1.0f)
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

        auto it = handlers_.find(static_cast<int>(id));
        if (it == handlers_.end()) return;

        for (auto& entry : it->second)
            entry.handler(value);
    }

    // ── RAII guard ────────────────────────────────────────────────────────────
    /**
     * ActionRegistration — RAII wrapper that auto-unregisters on destruction.
     * Store as a member to keep the handler alive.
     *
     *   reg_ = ActionManager::getInstance()
     *               .scoped(ActionID::TransportPlayStop, [this]{ toggle(); });
     */
    class ActionRegistration
    {
    public:
        ActionRegistration() = default;
        ActionRegistration(int token) : token_(token) {}
        ~ActionRegistration() { reset(); }

        ActionRegistration(ActionRegistration&& o) noexcept
            : token_(o.token_) { o.token_ = -1; }
        ActionRegistration& operator=(ActionRegistration&& o) noexcept
        {
            reset();
            token_ = o.token_;
            o.token_ = -1;
            return *this;
        }

        void reset()
        {
            if (token_ >= 0)
            {
                ActionManager::getInstance().unregisterHandler(token_);
                token_ = -1;
            }
        }

    private:
        int token_ = -1;
        JUCE_DECLARE_NON_COPYABLE(ActionRegistration)
    };

    ActionRegistration scoped(ActionID id, std::function<void(float)> handler)
    {
        return ActionRegistration(registerHandler(id, std::move(handler)));
    }

    ActionRegistration scoped(ActionID id, std::function<void()> handler)
    {
        return ActionRegistration(registerHandler(id, std::move(handler)));
    }

private:
    ActionManager() = default;

    struct Entry
    {
        int token;
        std::function<void(float)> handler;
    };

    std::unordered_map<int, std::vector<Entry>> handlers_;
    int nextToken_ = 0;

    JUCE_DECLARE_NON_COPYABLE(ActionManager)
};

} // namespace DAW

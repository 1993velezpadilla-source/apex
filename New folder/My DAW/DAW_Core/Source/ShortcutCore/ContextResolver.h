#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * ContextResolver — determines the active UI context for context-sensitive shortcuts.
 *
 * The same key can trigger different actions depending on where focus is:
 *   S → Split (arrangement) vs Solo (mixer)
 *   M → Mute clip (arrangement) vs Mute track (mixer)
 */
class ContextResolver
{
public:
    static ContextResolver& getInstance()
    {
        static ContextResolver instance;
        return instance;
    }

    /** Known UI contexts. */
    static constexpr const char* kGlobal      = "global";
    static constexpr const char* kArrangement = "arrangement";
    static constexpr const char* kMixer       = "mixer";
    static constexpr const char* kPianoRoll   = "piano_roll";
    static constexpr const char* kBrowser     = "browser";

    /** Set the current active context. */
    void setContext(const juce::String& ctx)
    {
        if (ctx != currentContext_)
        {
            currentContext_ = ctx;
            for (auto* l : listeners_)
                l->contextChanged(currentContext_);
        }
    }

    /** Get the current context. */
    const juce::String& getContext() const { return currentContext_; }

    /** Check if a shortcut entry's context matches the current (or is global). */
    bool matchesContext(const juce::String& entryContext) const
    {
        return entryContext.isEmpty()
            || entryContext == kGlobal
            || entryContext == currentContext_;
    }

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void contextChanged(const juce::String& newContext) = 0;
    };

    void addListener(Listener* l)    { listeners_.push_back(l); }
    void removeListener(Listener* l)
    {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l),
                         listeners_.end());
    }

private:
    ContextResolver() = default;

    juce::String           currentContext_ = kGlobal;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE(ContextResolver)
};

} // namespace DAW

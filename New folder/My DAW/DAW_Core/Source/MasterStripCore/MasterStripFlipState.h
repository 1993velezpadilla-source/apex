#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace DAW {

/**
 * MasterStripFlipState
 *
 * Global state for the master strip's front/back card flip pattern.
 *
 *   FRONT = "Mixing view"     (metering, phase/width, speaker set)
 *   BACK  = "Mastering view"  (ceiling, dither, future Reference A/B + Codec Preview)
 *
 * Single instance accessed as a global. The master strip is unique per
 * project (one master), so a singleton is the correct ownership model.
 *
 * Persistence model:
 *   The flip state is a UI preference, NOT project content. It does not
 *   live in ProjectState. Instead it persists via a juce::PropertiesFile
 *   that the host wires in via setPropertiesFile() — typically called
 *   once during app initialization. If no PropertiesFile is wired, the
 *   state is in-memory only (default Front, lost on app close).
 *
 *   This keeps the core decoupled: the FlipState doesn't know how the
 *   host stores preferences. The host (MainComponent or ApplicationCore)
 *   pushes a PropertiesFile* in and the core uses it.
 *
 * Default: Front (mixing view) — safest default since the metering and
 * level monitoring are visible immediately on session open.
 *
 * Threading: UI thread only. Listeners fire synchronously on the same
 * thread that calls setSide().
 */
class MasterStripFlipState
{
public:
    enum class Side
    {
        Front = 0,  // mixing view
        Back  = 1   // mastering view
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void masterStripFlipChanged(Side newSide) = 0;
    };

    MasterStripFlipState() = default;

    static MasterStripFlipState& getGlobalInstance()
    {
        static MasterStripFlipState instance;
        return instance;
    }

    // ── State ────────────────────────────────────────────────────────

    Side getSide() const noexcept
    {
        return (Side) side_.load(std::memory_order_relaxed);
    }

    bool isFront() const noexcept { return getSide() == Side::Front; }
    bool isBack()  const noexcept { return getSide() == Side::Back; }

    void setSide(Side s)
    {
        const int newVal = (int) s;
        const int oldVal = side_.exchange(newVal, std::memory_order_relaxed);
        if (oldVal != newVal)
        {
            saveToPropertiesIfBound();
            listeners_.call([s](Listener& l) { l.masterStripFlipChanged(s); });
        }
    }

    void flip()
    {
        setSide(isFront() ? Side::Back : Side::Front);
    }

    // ── Persistence wiring (optional) ────────────────────────────────

    /**
     * Wire a PropertiesFile so the flip state survives across sessions.
     * Pass nullptr to disconnect (e.g. on shutdown). Loads the saved
     * value immediately when wired.
     */
    void setPropertiesFile(juce::PropertiesFile* props)
    {
        properties_ = props;
        if (properties_ != nullptr)
        {
            const int saved = properties_->getIntValue(kPropertyKey, (int) Side::Front);
            const Side s = (saved == (int) Side::Back) ? Side::Back : Side::Front;
            // Set without recursing into save (we are loading, not user-driven)
            const int newVal = (int) s;
            const int oldVal = side_.exchange(newVal, std::memory_order_relaxed);
            if (oldVal != newVal)
                listeners_.call([s](Listener& l) { l.masterStripFlipChanged(s); });
        }
    }

    // ── Listener support ─────────────────────────────────────────────

    void addListener(Listener* l)    { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

private:
    static constexpr const char* kPropertyKey = "masterStripFlipSide";

    void saveToPropertiesIfBound()
    {
        if (properties_ != nullptr)
        {
            properties_->setValue(kPropertyKey, side_.load(std::memory_order_relaxed));
            properties_->setNeedsToBeSaved(true);
        }
    }

    std::atomic<int>             side_       { (int) Side::Front };
    juce::PropertiesFile*        properties_ { nullptr };
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStripFlipState)
};

} // namespace DAW

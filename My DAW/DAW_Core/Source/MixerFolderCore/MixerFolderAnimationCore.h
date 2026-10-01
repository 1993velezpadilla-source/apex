#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * MixerFolderAnimationCore
 * 
 * Manages expand/collapse animation state and progress.
 * - Smooth fade/slide/rise transitions
 * - Animation timing and easing
 * - Per-folder animation progress tracking
 * 
 * NO UI. Pure animation state management.
 */
class MixerFolderAnimationCore
{
public:
    struct AnimationState
    {
        TrackID folderId;
        bool expanding = true;              // true = expanding, false = collapsing
        float progress = 0.0f;              // 0.0 = start, 1.0 = complete
        juce::int64 startTimeMs = 0;
        int durationMs = 160;
        bool active = false;
        
        AnimationState() = default;
        explicit AnimationState(const TrackID& id) : folderId(id) {}
    };
    
    MixerFolderAnimationCore();
    
    /** Start expand animation for a folder */
    void startExpand(const TrackID& folderId);
    
    /** Start collapse animation for a folder */
    void startCollapse(const TrackID& folderId);
    
    /** Update all active animations (call from timer/repaint) */
    void updateAnimations(juce::int64 currentTimeMs);
    
    /** Get current animation progress for a folder (0.0 to 1.0) */
    float getAnimationProgress(const TrackID& folderId) const;
    
    /** Check if animation is active for a folder */
    bool isAnimating(const TrackID& folderId) const;
    
    /** Check if any animations are active */
    bool hasActiveAnimations() const;
    
    /** Stop animation for a folder immediately */
    void stopAnimation(const TrackID& folderId);
    
    /** Stop all animations */
    void stopAllAnimations();
    
    /** Get eased animation value (applies easing curve to raw progress) */
    float getEasedProgress(float rawProgress) const;
    
    /** Set animation duration in milliseconds */
    void setAnimationDuration(int durationMs) { defaultDurationMs_ = durationMs; }
    int getAnimationDuration() const { return defaultDurationMs_; }
    
    /** Listener for animation events */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void folderAnimationStarted(const TrackID& folderId, bool expanding) = 0;
        virtual void folderAnimationCompleted(const TrackID& folderId, bool expanding) = 0;
        virtual void folderAnimationProgressed(const TrackID& folderId, float progress) = 0;
    };
    
    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }
    
private:
    std::unordered_map<TrackID, AnimationState> activeAnimations_;
    int defaultDurationMs_ = 160;
    juce::ListenerList<Listener> listeners_;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderAnimationCore)
};

} // namespace DAW

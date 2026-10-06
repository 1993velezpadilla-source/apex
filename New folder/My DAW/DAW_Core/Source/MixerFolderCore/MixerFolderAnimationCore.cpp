#include "MixerFolderAnimationCore.h"

namespace DAW {

MixerFolderAnimationCore::MixerFolderAnimationCore()
{
}

void MixerFolderAnimationCore::startExpand(const TrackID& folderId)
{
    AnimationState state(folderId);
    state.expanding = true;
    state.progress = 0.0f;
    state.startTimeMs = juce::Time::getMillisecondCounter();
    state.durationMs = defaultDurationMs_;
    state.active = true;
    
    activeAnimations_[folderId] = state;
    
    listeners_.call([&](Listener& l) { l.folderAnimationStarted(folderId, true); });
}

void MixerFolderAnimationCore::startCollapse(const TrackID& folderId)
{
    AnimationState state(folderId);
    state.expanding = false;
    state.progress = 0.0f;
    state.startTimeMs = juce::Time::getMillisecondCounter();
    state.durationMs = defaultDurationMs_;
    state.active = true;
    
    activeAnimations_[folderId] = state;
    
    listeners_.call([&](Listener& l) { l.folderAnimationStarted(folderId, false); });
}

void MixerFolderAnimationCore::updateAnimations(juce::int64 currentTimeMs)
{
    std::vector<TrackID> completedAnimations;
    
    for (auto& [folderId, state] : activeAnimations_)
    {
        if (!state.active)
            continue;
        
        juce::int64 elapsed = currentTimeMs - state.startTimeMs;
        float rawProgress = juce::jlimit(0.0f, 1.0f, (float)elapsed / (float)state.durationMs);
        state.progress = getEasedProgress(rawProgress);
        
        listeners_.call([&](Listener& l) { l.folderAnimationProgressed(folderId, state.progress); });
        
        if (state.progress >= 1.0f)
        {
            state.active = false;
            completedAnimations.push_back(folderId);
        }
    }
    
    // Fire completion events
    for (const auto& folderId : completedAnimations)
    {
        auto it = activeAnimations_.find(folderId);
        if (it != activeAnimations_.end())
        {
            bool wasExpanding = it->second.expanding;
            listeners_.call([&](Listener& l) { l.folderAnimationCompleted(folderId, wasExpanding); });
            activeAnimations_.erase(it);
        }
    }
}

float MixerFolderAnimationCore::getAnimationProgress(const TrackID& folderId) const
{
    auto it = activeAnimations_.find(folderId);
    return (it != activeAnimations_.end() && it->second.active) ? it->second.progress : 0.0f;
}

bool MixerFolderAnimationCore::isAnimating(const TrackID& folderId) const
{
    auto it = activeAnimations_.find(folderId);
    return it != activeAnimations_.end() && it->second.active;
}

bool MixerFolderAnimationCore::hasActiveAnimations() const
{
    for (const auto& [folderId, state] : activeAnimations_)
    {
        if (state.active)
            return true;
    }
    return false;
}

void MixerFolderAnimationCore::stopAnimation(const TrackID& folderId)
{
    auto it = activeAnimations_.find(folderId);
    if (it != activeAnimations_.end())
    {
        it->second.active = false;
        activeAnimations_.erase(it);
    }
}

void MixerFolderAnimationCore::stopAllAnimations()
{
    activeAnimations_.clear();
}

float MixerFolderAnimationCore::getEasedProgress(float rawProgress) const
{
    // Eased cubic out for smooth deceleration
    // Formula: 1 - (1 - x)^3
    float inv = 1.0f - rawProgress;
    return 1.0f - (inv * inv * inv);
}

} // namespace DAW

#pragma once
#include <JuceHeader.h>
#include <unordered_set>
#include "../UtilityCore/Types.h"

namespace DAW {

/**
 * MixerFolderPersistenceCore
 * 
 * Saves and restores mixer folder expansion state.
 * - Per-project expansion state
 * - Optional default expansion preferences
 * 
 * NO UI. Pure persistence logic.
 */
class MixerFolderPersistenceCore
{
public:
    MixerFolderPersistenceCore() = default;
    
    /** Save current mixer expansion state to XML */
    juce::XmlElement* saveToXml(const std::unordered_set<TrackID>& expandedFolders) const;
    
    /** Restore mixer expansion state from XML */
    std::unordered_set<TrackID> restoreFromXml(const juce::XmlElement* xml) const;
    
    /** Save to project state ValueTree */
    void saveToValueTree(juce::ValueTree& state,
                        const std::unordered_set<TrackID>& expandedFolders) const;
    
    /** Restore from project state ValueTree */
    std::unordered_set<TrackID> restoreFromValueTree(const juce::ValueTree& state) const;
    
    /** Set default expansion preference (expand all new folders by default) */
    void setDefaultExpandNewFolders(bool expand) { defaultExpandNewFolders_ = expand; }
    bool shouldDefaultExpandNewFolders() const { return defaultExpandNewFolders_; }
    
private:
    bool defaultExpandNewFolders_ = true;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerFolderPersistenceCore)
};

} // namespace DAW

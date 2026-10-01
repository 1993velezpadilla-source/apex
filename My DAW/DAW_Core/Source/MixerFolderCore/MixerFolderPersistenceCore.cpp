#include "MixerFolderPersistenceCore.h"

namespace DAW {

juce::XmlElement* MixerFolderPersistenceCore::saveToXml(
    const std::unordered_set<TrackID>& expandedFolders) const
{
    auto* xml = new juce::XmlElement("MixerFolderExpansionState");
    
    for (const auto& folderId : expandedFolders)
    {
        auto* folderElement = xml->createNewChildElement("ExpandedFolder");
        folderElement->setAttribute("id", folderId);
    }
    
    return xml;
}

std::unordered_set<TrackID> MixerFolderPersistenceCore::restoreFromXml(
    const juce::XmlElement* xml) const
{
    std::unordered_set<TrackID> expandedFolders;
    
    if (xml == nullptr || !xml->hasTagName("MixerFolderExpansionState"))
        return expandedFolders;
    
    for (auto* folderElement : xml->getChildIterator())
    {
        if (folderElement->hasTagName("ExpandedFolder"))
        {
            TrackID folderId = folderElement->getStringAttribute("id");
            if (folderId.isNotEmpty())
                expandedFolders.insert(folderId);
        }
    }
    
    return expandedFolders;
}

void MixerFolderPersistenceCore::saveToValueTree(
    juce::ValueTree& state,
    const std::unordered_set<TrackID>& expandedFolders) const
{
    auto mixerFolderState = state.getOrCreateChildWithName("MixerFolderExpansion", nullptr);
    mixerFolderState.removeAllChildren(nullptr);
    
    for (const auto& folderId : expandedFolders)
    {
        juce::ValueTree folderNode("ExpandedFolder");
        folderNode.setProperty("id", folderId, nullptr);
        mixerFolderState.appendChild(folderNode, nullptr);
    }
}

std::unordered_set<TrackID> MixerFolderPersistenceCore::restoreFromValueTree(
    const juce::ValueTree& state) const
{
    std::unordered_set<TrackID> expandedFolders;
    
    auto mixerFolderState = state.getChildWithName("MixerFolderExpansion");
    if (!mixerFolderState.isValid())
        return expandedFolders;
    
    for (int i = 0; i < mixerFolderState.getNumChildren(); ++i)
    {
        auto folderNode = mixerFolderState.getChild(i);
        if (folderNode.hasType("ExpandedFolder"))
        {
            TrackID folderId = folderNode.getProperty("id").toString();
            if (folderId.isNotEmpty())
                expandedFolders.insert(folderId);
        }
    }
    
    return expandedFolders;
}

} // namespace DAW

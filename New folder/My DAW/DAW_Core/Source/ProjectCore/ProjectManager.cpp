#include "ProjectManager.h"
#include "../AppCore/ApplicationCore.h"
#include "../AutomationCore/AutomationManagerCore.h"
#include "../Automation/AutomationLaneStoreCore.h"
#include "../VocalTuneCore/ApexTuneIntegrationCore.h"
#include "../VocalTuneCore/ApexTuneProjectStateCore.h"

namespace DAW {

static constexpr int kCurrentProjectVersion = 6;

void ProjectManager::setSubsystems(const Subsystems& s)
{
    subs_ = s;

    if (subs_.clips)
    {
        subs_.clips->onClipRemoved = [this](const ClipID& clipId)
        {
            if (subs_.automation == nullptr || subs_.clips == nullptr)
                return;

            subs_.automation->removeOrphanedClipAutomation(
                [clips = subs_.clips](const juce::String& clipId) -> bool
                {
                    return clips->getClip(clipId) != nullptr;
                });

            if (subs_.appCore != nullptr)
                if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
                    integration->onClipDeleted(clipId);
        };
    }
}

juce::ValueTree ProjectManager::buildState() const
{
    juce::ValueTree state("DAWProject");
    state.setProperty("version", kCurrentProjectVersion, nullptr);
    state.setProperty("name",    projectName_, nullptr);
    state.setProperty("savedAt", juce::Time::getCurrentTime().toISO8601(true), nullptr);
    juce::ValueTree masterPhaseD("MasterPhaseD");
    masterPhaseD.setProperty("masterGain", 1.0f, nullptr);
    masterPhaseD.setProperty("ceilingMode", "SoftClip", nullptr);
    masterPhaseD.setProperty("ceilingDb", -0.1f, nullptr);
    masterPhaseD.setProperty("ditherMode", "Off", nullptr);
    masterPhaseD.setProperty("ditherBits", 24, nullptr);
    masterPhaseD.setProperty("meterDisplay", "PostFader", nullptr);
    state.addChild(masterPhaseD, -1, nullptr);

    if (subs_.tracks)     state.addChild(subs_.tracks->getState().createCopy(), -1, nullptr);
    if (subs_.clips)
    {
        auto clipsState = subs_.clips->getState().createCopy();

        if (subs_.appCore != nullptr)
        {
            if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
            {
                for (int i = 0; i < clipsState.getNumChildren(); ++i)
                {
                    auto clipTree = clipsState.getChild(i);
                    const auto clipId = clipTree.getProperty("id").toString();
                    auto* clip = subs_.clips->getClip(clipId);
                    if (clip == nullptr || clip->getType() != ClipType::Audio)
                        continue;

                    if (auto vocalState = integration->getClipState(clipId))
                        clipTree.appendChild(apex::vocaltune::ApexTuneProjectStateCore::toValueTree(*vocalState), nullptr);
                }
            }
        }

        state.addChild(clipsState, -1, nullptr);
    }
    if (subs_.markers)    state.addChild(subs_.markers->getState().createCopy(), -1, nullptr);
    if (subs_.routing)    state.addChild(subs_.routing->getState().createCopy(), -1, nullptr);
    if (subs_.faderRange) state.addChild(subs_.faderRange->toValueTree().createCopy(), -1, nullptr);
    if (subs_.appState)   state.addChild(subs_.appState->getState().createCopy(), -1, nullptr);
    if (subs_.folderBus)  state.addChild(subs_.folderBus->getState().createCopy(), -1, nullptr);
    if (subs_.automation)
    {
        auto clipExists = [clips = subs_.clips](const juce::String& clipId) -> bool
        {
            return clips != nullptr && clips->getClip(clipId) != nullptr;
        };

        state.addChild(subs_.automation->getStateFilteredByClipExistence(subs_.clips ? std::function<bool(const juce::String&)>(clipExists)
                                                                                     : std::function<bool(const juce::String&)>{}).createCopy(),
                       -1,
                       nullptr);
    }

    auto apexAutomationState = apex::automation::AutomationLaneStore::getInstance().getState();
    if (apexAutomationState.isValid() && apexAutomationState.getNumChildren() > 0)
        state.addChild(apexAutomationState.createCopy(), -1, nullptr);

    // Plugin chains - delegate to ApplicationCore
    if (subs_.appCore)
    {
        state.addChild(subs_.appCore->getClickStateTree().createCopy(), -1, nullptr);

        auto pluginState = subs_.appCore->getPluginChainsState();
        if (pluginState.isValid())
            state.addChild(pluginState.createCopy(), -1, nullptr);
    }

    if (subs_.transport)
    {
        juce::ValueTree tv("Transport");
        tv.setProperty("tempo",    subs_.transport->getTempo(),                  nullptr);
        tv.setProperty("position", (juce::int64)subs_.transport->getPosition(),  nullptr);
        tv.setProperty("looping",  subs_.transport->isLooping(),                 nullptr);
        state.addChild(tv, -1, nullptr);
    }

    return state;
}

void ProjectManager::restoreFromState(const juce::ValueTree& state)
{
    if (subs_.faderRange)
    {
        auto fv = state.getChildWithName("FaderRange");
        subs_.faderRange->fromValueTree(fv);
    }
    if (subs_.tracks)
    {
        auto tv = state.getChildWithName("Tracks");
        if (tv.isValid()) subs_.tracks->restoreState(tv);
    }
    if (subs_.clips)
    {
        auto cv = state.getChildWithName("Clips");
        if (cv.isValid()) subs_.clips->restoreState(cv);

        if (cv.isValid() && subs_.appCore != nullptr)
        {
            if (auto* integration = subs_.appCore->getVocalTuneIntegrationPtr())
            {
                for (int i = 0; i < cv.getNumChildren(); ++i)
                {
                    auto clipTree = cv.getChild(i);
                    const auto clipId = clipTree.getProperty("id").toString();
                    auto* clip = subs_.clips->getClip(clipId);
                    if (clip == nullptr || clip->getType() != ClipType::Audio)
                        continue;

                    auto vocalStateTree = clipTree.getChildWithName(apex::vocaltune::ApexTuneProjectStateCore::idRoot);
                    if (!vocalStateTree.isValid())
                        continue;

                    apex::vocaltune::ApexTuneClipState vocalState;
                    if (apex::vocaltune::ApexTuneProjectStateCore::fromValueTree(vocalStateTree, vocalState))
                    {
                        integration->restoreClipState(clipId, std::move(vocalState));
                        integration->scheduleRerenderAfterLoad(clipId);
                    }
                }
            }
        }
    }
    if (subs_.markers)
    {
        auto mv = state.getChildWithName("Markers");
        if (mv.isValid()) subs_.markers->restoreState(mv);
    }
    if (subs_.routing)
    {
        auto rv = state.getChildWithName("RoutingGraph");
        if (rv.isValid()) subs_.routing->restoreState(rv);
    }
    if (subs_.folderBus && subs_.routing && subs_.masterRoute && subs_.tracks)
    {
        auto fbv = state.getChildWithName("FolderBuses");
        if (fbv.isValid()) subs_.folderBus->restoreState(fbv, *subs_.routing, *subs_.masterRoute, *subs_.tracks);
    }
    if (subs_.appState)
    {
        auto av = state.getChildWithName("State");
        if (av.isValid()) subs_.appState->restoreState(av);
    }

    if (subs_.automation)
    {
        auto automation = state.getChildWithName("Automation");
        auto clipExists = [clips = subs_.clips](const juce::String& clipId) -> bool
        {
            return clips != nullptr && clips->getClip(clipId) != nullptr;
        };

        subs_.automation->restoreState(automation,
                                       subs_.clips ? std::function<bool(const juce::String&)>(clipExists)
                                                   : std::function<bool(const juce::String&)>{});

        if (subs_.clips)
            subs_.automation->removeOrphanedClipAutomation(clipExists);
    }

    apex::automation::AutomationLaneStore::getInstance().restoreState(state.getChildWithName("APEXAutomation"));

    if (subs_.appCore)
    {
        auto cv = state.getChildWithName("ClickState");
        subs_.appCore->restoreClickStateTree(cv);
    }

    // Plugin chains - delegate to ApplicationCore
    if (subs_.appCore)
    {
        auto pv = state.getChildWithName("PluginChains");
        if (pv.isValid())
            subs_.appCore->restorePluginChainsState(pv);
    }

    if (subs_.transport)
    {
        auto tv = state.getChildWithName("Transport");
        if (tv.isValid())
        {
            subs_.transport->setTempo((double)tv.getProperty("tempo", 120.0));
            subs_.transport->setPosition(
                (SamplePosition)(juce::int64)tv.getProperty("position", 0));
            subs_.transport->setLooping((bool)tv.getProperty("looping", false));
        }
    }
}

} // namespace DAW

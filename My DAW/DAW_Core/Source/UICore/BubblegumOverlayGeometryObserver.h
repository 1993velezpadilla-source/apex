#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <functional>
#include <vector>

namespace DAW {

/**
 * Observes the complete live component ancestry of a Mixer presentation.
 *
 * Cable route identity belongs to RoutingGraph; this observer owns only the
 * presentation event bridge that says screen-space endpoints may have moved.
 * It deliberately has no timer, polling, or routing dependency.
 */
class BubblegumOverlayGeometryObserver final : private juce::ComponentListener
{
public:
    explicit BubblegumOverlayGeometryObserver(std::function<void()> onGeometryChanged = {})
        : onGeometryChanged_(std::move(onGeometryChanged))
    {
    }

    ~BubblegumOverlayGeometryObserver() override
    {
        clear();
    }

    void setCallback(std::function<void()> onGeometryChanged)
    {
        onGeometryChanged_ = std::move(onGeometryChanged);
    }

    /** Observe the viewed Mixer panel, its viewport, and every current parent
     *  up to the JUCE top-level component. Reparenting refreshes this list. */
    void observe(juce::Component* mixerPanel, juce::Component* viewport)
    {
        clear();
        mixerPanel_ = mixerPanel;
        viewport_ = viewport;
        refreshObservedComponents();
    }

    void clear()
    {
        for (auto* component : observedComponents_)
            if (component != nullptr)
                component->removeComponentListener(this);

        observedComponents_.clear();
        mixerPanel_ = nullptr;
        viewport_ = nullptr;
    }

private:
    static void addUnique(std::vector<juce::Component*>& components,
                          juce::Component* component)
    {
        if (component != nullptr
            && std::find(components.begin(), components.end(), component) == components.end())
            components.push_back(component);
    }

    static void addComponentAndAncestors(std::vector<juce::Component*>& components,
                                         juce::Component* component)
    {
        for (auto* current = component; current != nullptr;
             current = current->getParentComponent())
            addUnique(components, current);
    }

    bool isObserved(const juce::Component* component) const
    {
        return std::find(observedComponents_.begin(), observedComponents_.end(), component)
            != observedComponents_.end();
    }

    void refreshObservedComponents()
    {
        std::vector<juce::Component*> desired;
        addComponentAndAncestors(desired, mixerPanel_);
        addComponentAndAncestors(desired, viewport_);

        for (auto* component : observedComponents_)
            if (component != nullptr)
                component->removeComponentListener(this);

        observedComponents_ = std::move(desired);

        for (auto* component : observedComponents_)
            component->addComponentListener(this);
    }

    void notifyGeometryChanged()
    {
        if (onGeometryChanged_)
            onGeometryChanged_();
    }

    void componentMovedOrResized(juce::Component& component,
                                 bool wasMoved,
                                 bool wasResized) override
    {
        if ((wasMoved || wasResized) && isObserved(&component))
            notifyGeometryChanged();
    }

    void componentParentHierarchyChanged(juce::Component& component) override
    {
        if (!isObserved(&component))
            return;

        refreshObservedComponents();
        notifyGeometryChanged();
    }

    void componentBeingDeleted(juce::Component& component) override
    {
        if (&component == mixerPanel_)
            mixerPanel_ = nullptr;
        if (&component == viewport_)
            viewport_ = nullptr;

        observedComponents_.erase(
            std::remove(observedComponents_.begin(), observedComponents_.end(), &component),
            observedComponents_.end());
    }

    std::function<void()> onGeometryChanged_;
    juce::Component* mixerPanel_ = nullptr;
    juce::Component* viewport_ = nullptr;
    std::vector<juce::Component*> observedComponents_;
};

} // namespace DAW

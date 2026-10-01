#pragma once

#include <JuceHeader.h>
#include "AutomationLaneComponent.h"
#include "../../../Source/AutomationCore/AutomationUIHelper.h"

namespace DAW {

/**
 * PHASE 1: STEP 1 — AutomationLaneContainerComponent
 * 
 * Container for multiple automation lanes per track.
 * Manages layout, visibility, and coordination of all lanes.
 */
class AutomationLaneContainerComponent : public juce::Component
{
public:
    AutomationLaneContainerComponent(AutomationUIHelper& helper,
                                    const TrackID& trackId)
        : helper_(helper), trackId_(trackId)
    {
        setInterceptsMouseClicks(true, true);
        setSize(400, 200);  // Default size, will be resized by parent
    }

    ~AutomationLaneContainerComponent() override = default;

    // ========================================================================
    // LANE MANAGEMENT
    // ========================================================================

    /**
     * Add a new automation lane for given parameter.
     * Returns reference to created component.
     */
    AutomationLaneComponent& addLane(const juce::String& parameterId)
    {
        // Check if lane already exists
        auto it = lanes_.find(parameterId);
        if (it != lanes_.end())
        {
            return *it->second;
        }

        // Create new lane component
        auto laneComponent = std::make_unique<AutomationLaneComponent>(
            helper_, trackId_, parameterId
        );

        auto* lanePtr = laneComponent.get();
        lanePtr->setSize(getWidth(), 60);  // 60 pixels high per lane

        addAndMakeVisible(laneComponent.get());
        lanePtr->setEnabled(true);
        lanes_[parameterId] = std::move(laneComponent);

        DBG("[AUTO-RECOVERY] lane target bound track=" << trackId_ << " target=" << parameterId);

        // Update layout
        resized();

        return *lanePtr;
    }

    /**
     * Remove automation lane for given parameter.
     */
    bool removeLane(const juce::String& parameterId)
    {
        auto it = lanes_.find(parameterId);
        if (it == lanes_.end())
            return false;

        removeChildComponent(it->second.get());
        lanes_.erase(it);
        resized();
        return true;
    }

    /**
     * Set visibility of a specific lane.
     */
    bool setLaneVisible(const juce::String& parameterId, bool visible)
    {
        auto it = lanes_.find(parameterId);
        if (it == lanes_.end())
            return false;

        it->second->setVisible(visible);
        resized();
        return true;
    }

    /**
     * Get lane component for given parameter.
     */
    AutomationLaneComponent* getLane(const juce::String& parameterId)
    {
        auto it = lanes_.find(parameterId);
        if (it == lanes_.end())
            return nullptr;
        return it->second.get();
    }

    /**
     * Check if lane exists for given parameter.
     */
    bool hasLane(const juce::String& parameterId) const
    {
        return lanes_.find(parameterId) != lanes_.end();
    }

    /**
     * Get number of lanes.
     */
    size_t getNumLanes() const
    {
        return lanes_.size();
    }

    /**
     * Remove all lanes.
     */
    void clearAllLanes()
    {
        lanes_.clear();
        resized();
    }

    /**
     * Get all parameter IDs that have lanes.
     */
    std::vector<juce::String> getAllParameterIds() const
    {
        std::vector<juce::String> ids;
        for (const auto& [paramId, _] : lanes_)
        {
            ids.push_back(paramId);
        }
        return ids;
    }

    // ========================================================================
    // COMPONENT LIFECYCLE
    // ========================================================================

    void resized() override
    {
        // Stack lanes vertically
        int yPos = 0;
        const int visibleLaneCount = (int)std::count_if(lanes_.begin(), lanes_.end(), [](const auto& entry)
        {
            return entry.second != nullptr && entry.second->isVisible();
        });
        const int laneHeight = visibleLaneCount > 0 ? juce::jmax(1, getHeight() / visibleLaneCount) : 0;

        for (auto& [paramId, lane] : lanes_)
        {
            if (lane->isVisible())
            {
                lane->setBounds(0, yPos, getWidth(), laneHeight);
                DBG("[AUTO-RECOVERY] lane bounds x=" << lane->getX()
                    << " y=" << lane->getY()
                    << " w=" << lane->getWidth()
                    << " h=" << lane->getHeight());
                yPos += laneHeight;
            }
        }

        // Update total height for parent to query
        totalVisibleHeight_ = yPos;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::transparentBlack);
    }

    // ========================================================================
    // ZOOM & SCALE
    // ========================================================================

    /**
     * Set samples-per-pixel for all lanes (zoom level).
     */
    void setSamplesPerPixel(double samples)
    {
        for (auto& [paramId, lane] : lanes_)
        {
            lane->setSamplesPerPixel(samples);
        }
    }

    /**
     * Set zoom level for all lanes.
     */
    void setZoomLevel(double zoom)
    {
        for (auto& [paramId, lane] : lanes_)
        {
            lane->setZoomLevel(zoom);
        }
    }

    /**
     * Set engine sample rate for all lanes (used for the APEX mirror's
     * samples-to-seconds conversion).
     */
    void setSampleRate(double sampleRate)
    {
        for (auto& [paramId, lane] : lanes_)
        {
            lane->setSampleRate(sampleRate);
        }
    }

    /**
     * Set project tempo (BPM) for all lanes (used for the APEX mirror's
     * seconds-to-PPQ conversion).
     */
    void setTempoBpm(double bpm)
    {
        for (auto& [paramId, lane] : lanes_)
        {
            lane->setTempoBpm(bpm);
        }
    }

    /**
     * Get total visible height needed for all lanes.
     */
    int getTotalVisibleHeight() const
    {
        return totalVisibleHeight_;
    }

    // ========================================================================
    // INVALIDATION
    // ========================================================================

    /**
     * Invalidate curve paths in all lanes.
     * Call when automation data changes.
     */
    void invalidateAllCurvePaths()
    {
        for (auto& [paramId, lane] : lanes_)
        {
            lane->invalidateCurvePath();
        }
    }

private:
    // ========================================================================
    // MEMBERS
    // ========================================================================

    AutomationUIHelper& helper_;
    TrackID trackId_;

    // Map from parameter ID to lane component
    std::map<juce::String, std::unique_ptr<AutomationLaneComponent>> lanes_;

    int totalVisibleHeight_ = 0;
};

} // namespace DAW
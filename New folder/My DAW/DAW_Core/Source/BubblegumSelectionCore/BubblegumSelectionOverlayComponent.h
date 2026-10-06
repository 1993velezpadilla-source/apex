#pragma once
#include "BubblegumSelectionRenderCore.h"
#include "BubblegumSelectionInteractionCore.h"

namespace DAW::BubblegumSelection
{
    class BubblegumSelectionOverlayComponent : public juce::Component,
                                               private juce::Timer
    {
    public:
        BubblegumSelectionOverlayComponent()
        {
            setInterceptsMouseClicks(false, false);
            setPaintingIsUnclipped(false);
        }

        ~BubblegumSelectionOverlayComponent() override
        {
            stopTimer();
        }

        void paint(juce::Graphics& g) override
        {
            if (!state_.shouldPaint(settings_))
                return;

            auto geometry = BubblegumSelectionGeometryCore::build(state_.dragStart, state_.dragEnd,
                                                                 state_.animationTimeSeconds, settings_);
            BubblegumSelectionRenderCore::paint(g, geometry, settings_, bubbles_, state_.alpha,
                                                state_.animationTimeSeconds);
        }

        void beginDrag(juce::Point<float> point)
        {
            if (!settings_.enabled)
                return;

            BubblegumSelectionInteractionCore::beginDrag(state_, point);
            bubbles_.reset(settings_);
            lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
            ensureTimerRunning();
            repaintActiveArea();
        }

        void updateDrag(juce::Point<float> point)
        {
            if (!settings_.enabled || !state_.isDragging)
                return;

            repaintActiveArea();
            BubblegumSelectionInteractionCore::updateDrag(state_, point);
            repaintActiveArea();
        }

        void endDrag()
        {
            if (!state_.isDragging)
                return;

            BubblegumSelectionInteractionCore::endDrag(state_);
            ensureTimerRunning();
            repaintActiveArea();
        }

        void cancel()
        {
            repaintActiveArea();
            state_.cancel();
            stopTimer();
            lastPaintBounds_ = {};
        }

        bool isActive() const noexcept { return state_.isActive(); }

        BubblegumSelectionSettings& getSettings() noexcept { return settings_; }
        const BubblegumSelectionSettings& getSettings() const noexcept { return settings_; }
        const BubblegumSelectionStateCore& getState() const noexcept { return state_; }

    private:
        void timerCallback() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const float deltaSeconds = (float)juce::jlimit(0.001, 0.05, (now - lastTickMs_) * 0.001);
            lastTickMs_ = now;

            repaintActiveArea();
            state_.advance(deltaSeconds, settings_);
            bubbles_.update(deltaSeconds);
            repaintActiveArea();

            if (!state_.isActive())
            {
                stopTimer();
                lastPaintBounds_ = {};
            }
        }

        int getTimerRateHz() const noexcept
        {
            return settings_.quality == BubblegumSelectionSettings::Quality::BubblegumFull ? 60 : 30;
        }

        void ensureTimerRunning()
        {
            if (!isTimerRunning())
                startTimerHz(getTimerRateHz());
        }

        void repaintActiveArea()
        {
            auto bounds = state_.getBounds().expanded(settings_.glowBlur + 34.0f).getSmallestIntegerContainer();
            bounds = bounds.getIntersection(getLocalBounds());
            auto repaintBounds = bounds.getUnion(lastPaintBounds_);
            lastPaintBounds_ = bounds;

            if (!repaintBounds.isEmpty())
                repaint(repaintBounds);
        }

        BubblegumSelectionSettings settings_;
        BubblegumSelectionStateCore state_;
        BubblegumSelectionBubbleCore bubbles_;
        juce::Rectangle<int> lastPaintBounds_;
        double lastTickMs_ = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblegumSelectionOverlayComponent)
    };
}

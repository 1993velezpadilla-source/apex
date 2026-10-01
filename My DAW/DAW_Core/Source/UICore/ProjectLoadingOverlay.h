#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"

namespace DAW {

class ProjectLoadingOverlay : public juce::Component, private juce::Timer
{
public:
    using Generation = uint64_t;

    ProjectLoadingOverlay()
    {
        setOpaque(false);
        setAlwaysOnTop(true);
    }

    Generation showWithProjectName(const juce::String& projectName)
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (++nextGeneration_ == 0)
            ++nextGeneration_;
        activeGeneration_ = nextGeneration_;
        shownAtMs_ = juce::Time::getMillisecondCounterHiRes();
        projectName_ = projectName;
        stage_ = "Preparing project";
        currentItem_.clear();
        progress_ = 0.0;
        determinate_ = false;
        setVisible(true);
        toFront(false);
        startTimerHz(6);
        repaint();
        return activeGeneration_;
    }

    void setProgress(const juce::String& stage,
                     const juce::String& currentItem,
                     double progress,
                     bool determinate)
    {
        stage_ = stage;
        currentItem_ = currentItem;
        progress_ = juce::jlimit(0.0, 1.0, progress);
        determinate_ = determinate;
        repaint();
    }

    void dismissAfterMinimumVisible(Generation generation,
                                    std::function<void()> completion = {})
    {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (generation == 0 || generation != activeGeneration_)
            return;

        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - shownAtMs_;
        const int delayMs = juce::jmax(0, (int) std::ceil(kMinimumVisibleMs - elapsedMs));
        juce::Component::SafePointer<ProjectLoadingOverlay> safeThis(this);
        auto finish = [safeThis, generation, completion = std::move(completion)]() mutable
        {
            if (auto* self = safeThis.getComponent())
            {
                if (generation != self->activeGeneration_)
                    return;
                self->activeGeneration_ = 0;
                self->setVisible(false);
                self->stopTimer();
                if (completion)
                    completion();
            }
        };

        if (delayMs == 0)
            finish();
        else
            juce::Timer::callAfterDelay(delayMs, std::move(finish));
    }

    bool hasActiveLoad() const noexcept { return activeGeneration_ != 0; }

    void paint(juce::Graphics& g) override
    {
        auto& theme = Theme::getInstance();
        auto& a = theme.apex;
        g.fillAll(a.color.deepestA.withAlpha(0.88f));

        auto box = getLocalBounds().withSizeKeepingCentre(480, 190).toFloat();
        ApexPrimitives::drawGlow(g, box.reduced(2.0f), true, a.color.violet);
        ApexPrimitives::drawPanel(g, box, ApexPrimitives::PanelDepth::raised, true, a.color.magenta);
        ApexPrimitives::drawSlashFlow(g, slashFlow_, a.color.magenta, a.color.cyan, 0.12f);

        auto inner = box.reduced(28.0f);
        g.setColour(a.color.textPrimary);
        g.setFont(theme.fonts.bold.withHeight(17.0f));
        g.drawText("LOADING PROJECT", inner.removeFromTop(24.0f), juce::Justification::centredLeft);
        g.setColour(a.color.textSecondary);
        g.setFont(theme.fonts.regular.withHeight(13.0f));
        g.drawText(projectName_, inner.removeFromTop(22.0f), juce::Justification::centredLeft, true);
        inner.removeFromTop(10.0f);

        g.setColour(a.color.textPrimary);
        g.setFont(theme.fonts.bold.withHeight(12.0f));
        g.drawText(stage_, inner.removeFromTop(20.0f), juce::Justification::centredLeft, true);
        g.setColour(a.color.textMuted);
        g.setFont(theme.fonts.regular.withHeight(10.5f));
        g.drawText(currentItem_, inner.removeFromTop(18.0f), juce::Justification::centredLeft, true);
        inner.removeFromTop(8.0f);

        auto bar = inner.removeFromTop(14.0f);
        g.setColour(a.color.deepestB);
        g.fillRoundedRectangle(bar, 7.0f);
        float fillFraction = (float) progress_;
        if (!determinate_)
        {
            const auto phase = (float) ((juce::Time::getMillisecondCounter() % 1200) / 1200.0);
            fillFraction = 0.18f;
            auto moving = bar.withWidth(bar.getWidth() * fillFraction)
                             .translated((bar.getWidth() - bar.getWidth() * fillFraction) * phase, 0.0f);
            juce::ColourGradient flow(a.color.magenta, moving.getX(), moving.getCentreY(),
                                      a.color.cyan, moving.getRight(), moving.getCentreY(), false);
            g.setGradientFill(flow);
            g.fillRoundedRectangle(moving, 7.0f);
        }
        else if (fillFraction > 0.0f)
        {
            auto fill = bar.withWidth(bar.getWidth() * fillFraction);
            juce::ColourGradient flow(a.color.magenta, fill.getX(), fill.getCentreY(),
                                      a.color.cyan, fill.getRight(), fill.getCentreY(), false);
            g.setGradientFill(flow);
            g.fillRoundedRectangle(fill, 7.0f);
        }
        g.setColour(a.color.borderSoftB);
        g.drawRoundedRectangle(bar, 7.0f, a.metric.strokeThin);
    }

    void resized() override
    {
        const auto box = getLocalBounds().withSizeKeepingCentre(480, 190).toFloat();
        slashFlow_ = ApexPrimitives::buildSlashFlowPaths(box.reduced(8.0f), 0xA9E12026u, 8);
    }

    bool hitTest(int, int) override { return true; }

private:
    juce::String projectName_;
    juce::String stage_;
    juce::String currentItem_;
    double progress_ = 0.0;
    bool determinate_ = false;
    std::vector<juce::Path> slashFlow_;
    static constexpr double kMinimumVisibleMs = 500.0;
    Generation nextGeneration_ = 0;
    Generation activeGeneration_ = 0;
    double shownAtMs_ = 0.0;

    void timerCallback()
    {
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingOverlay)
};

} // namespace DAW

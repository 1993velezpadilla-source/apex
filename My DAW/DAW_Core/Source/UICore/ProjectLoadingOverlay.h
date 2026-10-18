#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"
#include "../ThemeCore/ApexPrimitives.h"
#if JUCE_WINDOWS
 #include <windows.h>
 #include <cstring>
#endif

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
        determinate_ = true;
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
        // Progress callbacks can arrive at overlapping stage boundaries.
        // The existing left-to-right fill is monotonic throughout a load.
        progress_ = juce::jmax(progress_, juce::jlimit(0.0, 1.0, progress));
        determinate_ = determinate;
        repaint();
        paintExistingOverlayNow();
    }

    // A full bar means that the ACTUAL project loader succeeded, including
    // synchronous listener/UI rebuild. Failure never paints as completion.
    void markLoadResult(bool succeeded, const juce::String& error = {})
    {
        if (activeGeneration_ == 0) return;
        if (succeeded)
        {
            progress_ = 1.0;
            stage_ = "Project ready";
            currentItem_.clear();
        }
        else
        {
            progress_ = juce::jmin(0.99, progress_);
            stage_ = "Project load failed";
            currentItem_ = error;
        }
        repaint();
        paintExistingOverlayNow();
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
        // No fake looping marquee: fill always represents completed load work.
        const float fillFraction = (float) progress_;
        if (fillFraction > 0.0f)
        {
            auto fill = bar.withWidth(bar.getWidth() * fillFraction);
            juce::ColourGradient flow(a.color.magenta, fill.getX(), fill.getCentreY(),
                                      a.color.cyan, bar.getRight(), fill.getCentreY(), false);
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
    bool determinate_ = true;
    std::vector<juce::Path> slashFlow_;
    static constexpr double kMinimumVisibleMs = 500.0;
    Generation nextGeneration_ = 0;
    Generation activeGeneration_ = 0;
    double shownAtMs_ = 0.0;

    // SINGLE loading surface: repaint the EXISTING JUCE ProjectLoadingOverlay
    // in place even while ProjectManager synchronously restores plugin state.
    // Normal Component::repaint() posts a paint message that cannot run until
    // the blocking restore ends, making the pre-existing progress bar appear
    // stuck. Rendering ONLY this component's already-existing 480x190 panel
    // to the current window DC avoids pumping nested JUCE messages and avoids
    // painting an inconsistent, half-restored DAW model. No second window,
    // modal progress component, popup, or new loading bar is created.
    void paintExistingOverlayNow()
    {
       #if JUCE_WINDOWS
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (!isShowing() || getWidth() <= 0 || getHeight() <= 0)
            return;
        auto* peer = getPeer();
        if (peer == nullptr)
            return;
        HWND hwnd = static_cast<HWND>(peer->getNativeHandle());
        if (hwnd == nullptr)
            return;

        const auto panel = getLocalBounds().withSizeKeepingCentre(480, 190);
        const auto snapshot = createComponentSnapshot(panel, false);
        if (snapshot.isNull() || snapshot.getFormat() != juce::Image::ARGB)
            return;

        const juce::Image::BitmapData bitmap(snapshot, juce::Image::BitmapData::readOnly);
        if (bitmap.pixelStride != 4)
            return;
        // BitmapData::lineStride may exceed width*4; copy row by row to
        // preserve pixels on all JUCE image backends, never assume pitch.
        std::vector<juce::uint8> rows((size_t) snapshot.getWidth()
                                     * (size_t) snapshot.getHeight() * 4u);
        const size_t rowBytes = (size_t) snapshot.getWidth() * 4u;
        for (int y = 0; y < snapshot.getHeight(); ++y)
            std::memcpy(rows.data() + (size_t) y * rowBytes,
                        bitmap.getLinePointer(y), rowBytes);

        const auto local = peer->getComponent().getLocalPoint(this, panel.getTopLeft());
        const double scale = peer->getPlatformScaleFactor();
        const int x = juce::roundToInt(local.x * scale);
        const int y = juce::roundToInt(local.y * scale);
        const int w = juce::roundToInt(panel.getWidth() * scale);
        const int h = juce::roundToInt(panel.getHeight() * scale);
        if (w <= 0 || h <= 0)
            return;

        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = snapshot.getWidth();
        info.bmiHeader.biHeight = -snapshot.getHeight(); // top-to-bottom
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (auto dc = GetDC(hwnd))
        {
            StretchDIBits(dc, x, y, w, h, 0, 0,
                          snapshot.getWidth(), snapshot.getHeight(),
                          rows.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            ReleaseDC(hwnd, dc);
        }
       #endif
    }

    void timerCallback()
    {
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectLoadingOverlay)
};

} // namespace DAW

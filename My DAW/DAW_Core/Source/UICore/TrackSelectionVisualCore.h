#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "TrackLavaLampCore.h"
#include "../ThemeCore/Theme.h"

namespace DAW {

// =====================================================================
// TrackSelectionVisualCore � isolated nucleo.
//
// Renders ONLY the "selected" visual state of a track row. No routing
// awareness, no Bubblegum awareness, no mouse handling.
//
// Quality modes (settable via setQualityMode):
//   Off        � No highlight at all (max performance)
//   Minimal    � Simple glow outline only
//   Performance� Gloss effect with basic spheres (default)
//   Cinematic  � Full luxury black-glass with all effects
//
// Layer stack (painted in order, inside rowBounds.reduced(8)):
//   L1 Outer drop shadow    � strip floats above the panel
//   L2 Extruded slab        � physical thickness (warm on master)
//   L3 Static glass surface  � selected-track body and depth
//   L4 Chromatic fringe     � DPI-scaled red/cyan stereo-depth cue
//   L5 Bevel                � bright rim + top highlight + dark underside
//   L6 Left accent bar      � track colour (master = 0xFFCC9900)
//
// Driven exclusively by:
//   setSelected(bool)       � single source of truth (the track row)
//   setAccentColour(Colour) � once per track colour change
//   setQualityMode(mode)    � set visual quality level
//   onRepaintRequired       � wire to the owning row's repaint()
// =====================================================================
class TrackSelectionVisualCore
{
public:
    enum class QualityMode
    {
        Off = 0,        // No highlight (max performance)
        Minimal,        // Simple glow outline only
        Performance,    // Gloss + basic spheres (default)
        Cinematic       // Full luxury black-glass
    };

    std::function<void()> onRepaintRequired;

    TrackSelectionVisualCore()
    {
        // onRepaintRequired on lava_ intentionally not set �
        // MixerPanel drives all repaints from its single timer.
        lava_.setQualityMode(TrackLavaLampCore::LavaQualityMode::Performance);
    }

    void setSelected(bool shouldBeSelected)
    {
        if (selected_ == shouldBeSelected)
            return;
        selected_ = shouldBeSelected;
        triggerRepaint();
    }

    /** Set the bubble colors to restore when deselected.
     *  Call once at strip creation for master (matte black) vs regular strips (cool grey). */
    void setRestBubbleColours(juce::Colour deep, juce::Colour near)
    {
        lava_.setRestBubbleColours(deep, near);
        // Also prime lava's actual colors if currently unselected
        if (!selected_)
            lava_.setBubbleColours(deep, near);
    }

    void setAccentColour(juce::Colour accent)
    {
        if (accentColour_ == accent)
            return;
        accentColour_ = accent;
        lava_.setAccentColour(accent);
        cachedChrome_ = {};  // invalidate chrome cache on accent change
        if (selected_)
            triggerRepaint();
    }

    void setSphereColour(juce::Colour colour)
    {
        lava_.setSphereColour(colour);
        if (selected_)
            triggerRepaint();
    }

    void setParticleColour(juce::Colour colour)
    {
        lava_.setParticleColour(colour);
        if (selected_)
            triggerRepaint();
    }

    bool isSelected() const noexcept { return selected_; }

    // Selected-track chrome is event-driven and static.  The reusable Lava
    // core remains available through getLavaCore(), but selection itself never
    // creates a presentation-clock demand.
    bool needsAnimationTick() const noexcept { return false; }

    void setQualityMode(QualityMode mode)
    {
        if (qualityMode_ == mode)
            return;
        qualityMode_ = mode;
        cachedChrome_ = {};  // invalidate chrome cache on quality mode change

        lava_.setQualityMode(mode == QualityMode::Cinematic
            ? TrackLavaLampCore::LavaQualityMode::Cinematic
            : TrackLavaLampCore::LavaQualityMode::Performance);

        if (selected_)
            triggerRepaint();
    }

    QualityMode getQualityMode() const noexcept { return qualityMode_; }

    struct DiagnosticCounters
    {
        int chromeBakeCount = 0;
        int chromeBlitCount = 0;
        int paintIntoEarlyReturnCount = 0;
        int paintIntoFullExecuteCount = 0;
    };

    int getChromeBakeCount() const noexcept { return chromeBakeCount_; }
    int getChromeBlitCount() const noexcept { return chromeBlitCount_; }
    int getPaintIntoEarlyReturnCount() const noexcept { return paintIntoEarlyReturnCount_; }
    int getPaintIntoFullExecuteCount() const noexcept { return paintIntoFullExecuteCount_; }

    DiagnosticCounters consumeDiagnosticCounters() const noexcept
    {
        DiagnosticCounters counters;
        counters.chromeBakeCount = chromeBakeCount_;
        counters.chromeBlitCount = chromeBlitCount_;
        counters.paintIntoEarlyReturnCount = paintIntoEarlyReturnCount_;
        counters.paintIntoFullExecuteCount = paintIntoFullExecuteCount_;

        chromeBakeCount_ = 0;
        chromeBlitCount_ = 0;
        paintIntoEarlyReturnCount_ = 0;
        paintIntoFullExecuteCount_ = 0;

        return counters;
    }

    void paintInto(juce::Graphics& g, juce::Rectangle<float> rowBounds) const
    {
        if (!selected_ || qualityMode_ == QualityMode::Off)
        {
            ++paintIntoEarlyReturnCount_;
            return;
        }

        ++paintIntoFullExecuteCount_;

        const auto bounds = rowBounds.reduced(1.0f);
        if (bounds.isEmpty())
            return;

        const float cr = cornerRadius;
        constexpr float pulse = 0.5f;

        // ?? MINIMAL MODE: Simple glow outline only ????????????????????????
        if (qualityMode_ == QualityMode::Minimal)
        {
            // Minimal neon magenta outline
            for (int i = 1; i <= 2; ++i)
            {
                const float expand = (float) i * 1.2f;
                const float a = (0.22f + pulse * 0.15f) / (float) i;
                g.setColour(DAW::Theme::getInstance().apex.color.magenta.withAlpha(a));
                g.drawRoundedRectangle(bounds.expanded(expand), cr + expand * 0.4f, 1.0f);
            }

            juce::ColourGradient rimLight(
                DAW::Theme::getInstance().apex.color.magentaBright.withAlpha(0.90f + pulse * 0.10f), bounds.getCentreX(), bounds.getY(),
                DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.60f + pulse * 0.08f), bounds.getCentreX(), bounds.getBottom(), false);
            g.setGradientFill(rimLight);
            g.drawRoundedRectangle(bounds, cr, 1.4f);
            return;
        }

        // ?? PERFORMANCE / CINEMATIC MODES: Full rendering ?????????????????
        {
            juce::Path clip;
            clip.addRoundedRectangle(bounds, cr);
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(clip);

            ensureChromeCacheBuilt(bounds);
            if (cachedChrome_.isValid())
            {
                g.drawImageAt(cachedChrome_,
                              (int) std::round(bounds.getX()),
                              (int) std::round(bounds.getY()),
                              false);
                ++chromeBlitCount_;
            }

            // No Lava/bubble/physics layer is painted for selected tracks.
            // The cached glass and static rims above are the complete selected
            // background visual.
        }

        // Outer neon magenta bloom � wide diffuse halo
        g.setColour(DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.18f + pulse * 0.10f));
        g.drawRoundedRectangle(bounds.expanded(3.0f), cr + 1.0f, 4.0f);

        // Mid ring
        g.setColour(DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.42f + pulse * 0.12f));
        g.drawRoundedRectangle(bounds.expanded(1.0f), cr + 0.3f, 1.6f);

        // Inner sharp magenta rim
        juce::ColourGradient rimLight(
            DAW::Theme::getInstance().apex.color.magentaBright.withAlpha(0.95f + pulse * 0.05f), bounds.getCentreX(), bounds.getY(),
            DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.75f + pulse * 0.08f), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(rimLight);
        g.drawRoundedRectangle(bounds, cr, 1.4f);

        // Inset bevel � very subtle white for depth
        g.setColour(juce::Colours::white.withAlpha(0.10f + pulse * 0.04f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), juce::jmax(2.0f, cr - 1.0f), 0.7f);

        // ?? CINEMATIC-ONLY: Sweep shine effect ????????????????????????????
        if (qualityMode_ == QualityMode::Cinematic)
        {
            // Cinematic sweep: faint magenta sheen sliding across the rim
            juce::ColourGradient shineSweep(
                juce::Colours::transparentBlack, bounds.getX(), bounds.getY(),
                juce::Colours::transparentBlack, bounds.getRight(), bounds.getY(), false);
             constexpr double sweepPos = 0.50;
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos - 0.12), juce::Colours::transparentBlack);
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos), DAW::Theme::getInstance().apex.color.pink.withAlpha(0.22f));
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos + 0.12), juce::Colours::transparentBlack);
            g.setGradientFill(shineSweep);
            g.drawRoundedRectangle(bounds.reduced(0.55f), juce::jmax(2.0f, cr - 0.55f), 1.05f);

            g.setColour(juce::Colour(0xFF000000).withAlpha(0.28f));
            g.drawRoundedRectangle(bounds.reduced(1.8f), juce::jmax(2.0f, cr - 1.8f), 0.95f);
        }
    }

    /** Paint ONLY the lava blobs into `area` � no rim, no accent bar. */
    void paintLavaOnly(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (!lava_.hasVisibleContent() || area.isEmpty())
            return;

        const float cr = cornerRadius;
        juce::Path clip;
        clip.addRoundedRectangle(area, cr);
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(clip);
        lava_.paintBackground(g, area);
    }

     /** Expose the lava core so external components (e.g. the mixer
         channel strip) can reuse the same effect without duplicating it. */
     TrackLavaLampCore& getLavaCore() noexcept { return lava_; }
     const TrackLavaLampCore& getLavaCore() const noexcept { return lava_; }

    /** Paint rising particles only � no environment, no spheres.
        Used for unselected strips to keep lightweight visual life. */
    void paintParticlesOnly(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (area.isEmpty()) return;
        const float cr = cornerRadius;
        juce::Path clip;
        clip.addRoundedRectangle(area, cr);
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(clip);
        lava_.paintParticlesOnly(g, area);
    }

    /** Control whether rising bubble particles are shown.
        Spheres/environment always paint; only the selected strip shows bubbles. */
    void setParticlesPainted(bool show) { lava_.setParticlesPainted(show); }
    void setSpheresPainted  (bool show) { lava_.setSpheresPainted(show);   }

    /** Compatibility hook for shared callers. Selected chrome has no sweep. */
    void stepSweep(float dt) const noexcept
    {
        juce::ignoreUnused(dt);
    }

private:
    struct SelectionVisKey
    {
        int width = 0;
        int height = 0;
        bool isMaster = false;
        juce::uint32 accentARGB = 0;
        juce::uint32 trackColorARGB = 0;
        int styleVersion = 0;
        int qualityMode = 0;

        bool operator==(const SelectionVisKey& o) const noexcept
        {
            return width == o.width && height == o.height && isMaster == o.isMaster
                && accentARGB == o.accentARGB && trackColorARGB == o.trackColorARGB
                && styleVersion == o.styleVersion && qualityMode == o.qualityMode;
        }
    };

    void ensureChromeCacheBuilt(juce::Rectangle<float> bounds) const
    {
        const int w = (int) std::ceil(bounds.getWidth());
        const int h = (int) std::ceil(bounds.getHeight());

        const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();

        if (w <= 0 || h <= 0)
            return;

        SelectionVisKey key;
        key.width = w;
        key.height = h;
        key.isMaster = accentColour_.getARGB() == 0xFFE91572u; // master = magentaDeep accent
        key.accentARGB = accentColour_.getARGB();
        key.trackColorARGB = accentColour_.getARGB();
        key.styleVersion = selectionVisStyleVersion_;
        key.qualityMode = (int)qualityMode_;

        if (cachedChrome_.isValid()
            && key == cachedSelectionVisKey_
            && juce::approximatelyEqual(scale, cachedChromeScale_))
        {
            return;
        }

        cachedChrome_ = juce::Image(juce::Image::ARGB, w, h, true);
        ++chromeBakeCount_;

        cachedChromeScale_ = scale;
        cachedSelectionVisKey_ = key;

        juce::Graphics g(cachedChrome_);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);

        const juce::Rectangle<float> imgBounds(0.0f, 0.0f, (float) w, (float) h);
        const float cr = cornerRadius;

        juce::ColourGradient baseGlass(
            juce::Colour(0xE60A0B11), imgBounds.getCentreX(), imgBounds.getY(),
            juce::Colour(0xF007080D), imgBounds.getCentreX(), imgBounds.getBottom(), false);
        baseGlass.addColour(0.22, juce::Colour(0xCC12141D));
        baseGlass.addColour(0.60, juce::Colour(0xAA0A0B10));
        g.setGradientFill(baseGlass);
        g.fillRoundedRectangle(imgBounds, cr);

        g.setColour(juce::Colour(0xE90A0B11));
        g.fillEllipse(imgBounds.getX() - 0.2f, imgBounds.getY() - 0.2f, 10.0f, 10.0f);
        g.fillEllipse(imgBounds.getRight() - 9.8f, imgBounds.getY() - 0.2f, 10.0f, 10.0f);

        juce::ColourGradient vignette(
            juce::Colours::transparentBlack, imgBounds.getCentreX(), imgBounds.getY(),
            juce::Colours::black.withAlpha(0.26f), imgBounds.getCentreX(), imgBounds.getBottom(), false);
        vignette.addColour(0.22, juce::Colours::black.withAlpha(0.04f));
        vignette.addColour(0.75, juce::Colours::black.withAlpha(0.18f));
        g.setGradientFill(vignette);
        g.fillRoundedRectangle(imgBounds, cr);

        constexpr float pulse = 0.5f;

        g.setColour(DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.18f + pulse * 0.10f));
        g.drawRoundedRectangle(imgBounds.expanded(3.0f), cr + 1.0f, 4.0f);

        g.setColour(DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.42f + pulse * 0.12f));
        g.drawRoundedRectangle(imgBounds.expanded(1.0f), cr + 0.3f, 1.6f);

        juce::ColourGradient rimLight(
            DAW::Theme::getInstance().apex.color.magentaBright.withAlpha(0.95f + pulse * 0.05f), imgBounds.getCentreX(), imgBounds.getY(),
            DAW::Theme::getInstance().apex.color.magenta.withAlpha(0.75f + pulse * 0.08f), imgBounds.getCentreX(), imgBounds.getBottom(), false);
        g.setGradientFill(rimLight);
        g.drawRoundedRectangle(imgBounds, cr, 1.4f);

        g.setColour(juce::Colours::white.withAlpha(0.10f + pulse * 0.04f));
        g.drawRoundedRectangle(imgBounds.reduced(1.0f), juce::jmax(2.0f, cr - 1.0f), 0.7f);

        if (qualityMode_ == QualityMode::Cinematic)
        {
            juce::ColourGradient shineSweep(
                juce::Colours::transparentBlack, imgBounds.getX(), imgBounds.getY(),
                juce::Colours::transparentBlack, imgBounds.getRight(), imgBounds.getY(), false);
            constexpr double sweepPos = 0.50;
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos - 0.12), juce::Colours::transparentBlack);
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos), DAW::Theme::getInstance().apex.color.pink.withAlpha(0.22f));
            shineSweep.addColour(juce::jlimit(0.0, 1.0, sweepPos + 0.12), juce::Colours::transparentBlack);
            g.setGradientFill(shineSweep);
            g.drawRoundedRectangle(imgBounds.reduced(0.55f), juce::jmax(2.0f, cr - 0.55f), 1.05f);

            g.setColour(juce::Colour(0xFF000000).withAlpha(0.28f));
            g.drawRoundedRectangle(imgBounds.reduced(1.8f), juce::jmax(2.0f, cr - 1.8f), 0.95f);
        }
    }

    void triggerRepaint() const
    {
        if (onRepaintRequired)
            onRepaintRequired();
    }

    bool         selected_     { false };
    juce::Colour accentColour_ { 0xFFE91572 }; // default = APEX master magentaDeep
    QualityMode  qualityMode_  { QualityMode::Performance };
    mutable TrackLavaLampCore lava_;
    mutable juce::Image cachedChrome_;
    mutable SelectionVisKey cachedSelectionVisKey_;
    mutable float cachedChromeScale_ = 0.0f;
    mutable int selectionVisStyleVersion_ = 1;
    mutable int chromeBakeCount_ = 0;
    mutable int chromeBlitCount_ = 0;
    mutable int paintIntoEarlyReturnCount_ = 0;
    mutable int paintIntoFullExecuteCount_ = 0;

    static constexpr float cornerRadius = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackSelectionVisualCore)
};

} // namespace DAW

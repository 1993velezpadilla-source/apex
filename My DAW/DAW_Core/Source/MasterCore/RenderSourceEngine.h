#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * RenderSourceEngine — render/export tap point.
 *
 * Captures the master bus output AFTER master inserts and master fader,
 * but BEFORE the Control Room / monitor section.
 *
 * This ensures:
 *   - Monitor level does NOT affect rendered file level
 *   - Dim/mute/mono does NOT affect rendered file
 *   - Monitor FX are NOT printed into exports
 *   - Speaker switching does NOT affect rendered source
 *
 * Version 1: provides a simple buffer snapshot for offline bounce.
 * Future: supports real-time recording to disk, punch-in/out.
 */
class RenderSourceEngine
{
public:
    void prepare(int numChannels, int blockSize)
    {
        renderBuffer_.setSize(numChannels, blockSize);
    }

    void releaseResources()
    {
        renderBuffer_.setSize(0, 0);
    }

    /**
     * Captures the current master output into the render buffer.
     * Called from audio thread AFTER master processing, BEFORE monitor section.
     */
    void captureBlock(const float* L, const float* R, int numSamples)
    {
        if (renderBuffer_.getNumSamples() < numSamples)
            return;

        auto* dstL = renderBuffer_.getWritePointer(0);
        auto* dstR = renderBuffer_.getNumChannels() > 1
                     ? renderBuffer_.getWritePointer(1) : dstL;

        juce::FloatVectorOperations::copy(dstL, L, numSamples);
        juce::FloatVectorOperations::copy(dstR, R, numSamples);
    }

    /** Returns the render buffer for offline bounce reading. */
    const juce::AudioBuffer<float>& getRenderBuffer() const { return renderBuffer_; }

    bool isExporting() const noexcept { return exporting_; }
    void setExporting(bool v) noexcept { exporting_ = v; }

private:
    juce::AudioBuffer<float> renderBuffer_;
    bool exporting_ = false;
};

} // namespace DAW

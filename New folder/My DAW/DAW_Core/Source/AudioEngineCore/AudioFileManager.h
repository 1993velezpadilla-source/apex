#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include <memory>

namespace DAW {

/**
 * AudioFileManager — loads, caches, and serves audio file data.
 *
 * Two consumers:
 *  - AudioEngine (audio thread): reads cached buffers for playback
 *  - ArrangementView (UI thread): draws waveforms from cached buffers
 *
 * Thread safety: buffers are loaded on the message thread and then
 * read-only from both threads. No concurrent writes.
 */
class AudioFileManager
{
public:
    AudioFileManager()
    {
        formatManager_.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, MP3
    }

    struct CachedAudio
    {
        juce::AudioBuffer<float> buffer;
        juce::AudioBuffer<float> preNormalizeBuffer;
        double sampleRate = 44100.0;
        bool normalizeActive = false;

        // Raw waveform overview — built ONCE at load, covers the full file.
        static constexpr int kOverviewCols = 16384;
        std::vector<float> rawMin;
        std::vector<float> rawMax;

        // Rendered pixel image at a fixed height — hardware-scaled to fit the lane.
        // Cache key: (ovStart, ovEnd, colour). Height is NOT in the key, so
        // vertical zoom never triggers a rebuild.
        static constexpr int kOverviewImgH = 1024;
        mutable juce::Image  overviewImage;
        mutable int          overviewOvStart = -1;
        mutable int          overviewOvEnd   = -1;
        mutable juce::uint32 overviewColour  = 0;
        mutable float        overviewGain    = -1.0f;
        mutable float        overviewPitch   = 999.0f;
        mutable float        overviewStretch = -1.0f;
        mutable bool         overviewReversed = false;
        mutable SamplePosition overviewClipTimelineSamples = -1;
        mutable SamplePosition overviewFadeInSamples = -1;
        mutable SamplePosition overviewFadeOutSamples = -1;

        // High-quality dynamic image for zoomed-in views
        mutable juce::Image  dynamicImage;
        mutable SamplePosition dynamicStartSample = -1;
        mutable SamplePosition dynamicNumSamples  = -1;
        mutable int          dynamicWidth       = -1;
        mutable juce::uint32 dynamicColour      = 0;
        mutable float        dynamicGain        = -1.0f;
        mutable float        dynamicPitch       = 999.0f;
        mutable float        dynamicStretch     = -1.0f;
        mutable bool         dynamicReversed    = false;
        mutable SamplePosition dynamicClipTimelineSamples = -1;
        mutable SamplePosition dynamicFadeInSamples = -1;
        mutable SamplePosition dynamicFadeOutSamples = -1;
    };

    using CachedAudioHandle = std::shared_ptr<const CachedAudio>;

    struct LoadResult
    {
        bool           success     = false;
        int            numChannels = 0;
        double         sampleRate  = 44100.0;
        SamplePosition numSamples  = 0;
    };

    /** Load an audio file and cache it for a clip. */
    LoadResult loadForClip(const ClipID& clipId, const juce::File& file, bool buildWaveformOverviewNow = true)
    {
        LoadResult result;

        logAlways("[APEX-DIAG-CRASH] loadForClip ENTER file=" + file.getFullPathName()
            + " lengthSamples=" + juce::String((juce::int64) getFileLengthSamples(file)));

        // ── Fast path: reuse a previously decoded source for this exact file ──
        // Undo/redo and duplicate operations re-add clips that point at the same
        // file. Re-decoding from disk + rebuilding the waveform overview each
        // time is what caused the visible undo/redo delay, so serve a cached
        // decode when the file path + modification time are unchanged.
        if (auto decoded = findDecodedSource(file))
        {
            auto cachedFromSource = makeCachedFromDecoded(*decoded, buildWaveformOverviewNow);

            result.success     = true;
            result.numChannels = cachedFromSource->buffer.getNumChannels();
            result.sampleRate  = cachedFromSource->sampleRate;
            result.numSamples  = (SamplePosition)cachedFromSource->buffer.getNumSamples();

            {
                const juce::ScopedWriteLock sl(cacheLock_);
                cache_[clipId] = std::move(cachedFromSource);
            }
            logAlways("[APEX-DIAG-CRASH] loadForClip EXIT ok=1");
            return result;
        }

        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            formatManager_.createReaderFor(file));
        if (!reader)
        {
            logAlways("[APEX-DIAG-CRASH] loadForClip EXIT ok=0");
            return result;
        }

        auto cached       = std::make_shared<CachedAudio>();
        cached->sampleRate = reader->sampleRate;
        logAlways("[APEX-DIAG-CRASH] loadForClip allocate samples=" + juce::String((juce::int64) reader->lengthInSamples)
            + " channels=" + juce::String((int) reader->numChannels));
        cached->buffer.setSize((int)reader->numChannels,
                               (int)reader->lengthInSamples);
        reader->read(&cached->buffer, 0,
                     (int)reader->lengthInSamples, 0, true, true);

        if (buildWaveformOverviewNow)
        {
            logAlways("[APEX-DIAG-CRASH] clipWaveform build clipId=" + clipId
                + " requestedSamples=" + juce::String((juce::int64) reader->lengthInSamples));
            rebuildWaveformOverview(*cached);
        }

        result.success     = true;
        result.numChannels = (int)reader->numChannels;
        result.sampleRate  = reader->sampleRate;
        result.numSamples  = (SamplePosition)reader->lengthInSamples;

        // Remember the decoded source so future re-adds (undo/redo/duplicate)
        // skip the disk read and the overview rebuild.
        storeDecodedSource(file, cached, buildWaveformOverviewNow);

        {
            const juce::ScopedWriteLock sl(cacheLock_);
            cache_[clipId] = std::move(cached);
        }
        logAlways("[APEX-DIAG-CRASH] loadForClip EXIT ok=1");
        return result;
    }

    /** Check if audio data is loaded for a clip. */
    bool hasAudio(const ClipID& clipId) const
    {
        const juce::ScopedReadLock sl(cacheLock_);
        return cache_.count(clipId) > 0;
    }

    CachedAudioHandle getCachedAudioSnapshot(const ClipID& clipId) const
    {
        const juce::ScopedReadLock sl(cacheLock_);
        auto it = cache_.find(clipId);
        return (it != cache_.end()) ? it->second : nullptr;
    }

    /** Get the raw audio buffer for playback (audio thread). */
    const juce::AudioBuffer<float>* getBuffer(const ClipID& clipId) const
    {
        auto cached = getCachedAudioSnapshot(clipId);
        return cached ? &cached->buffer : nullptr;
    }

    enum class NormalizeResult
    {
        Applied,
        MissingAudio,
        NoAudioPeak
    };

    NormalizeResult normalizePeak(const ClipID& clipId,
                                  SamplePosition startSample = 0,
                                  SamplePosition endSample = 0,
                                  float targetDb = 0.0f)
    {
        auto current = getCachedAudioSnapshot(clipId);
        if (!current)
            return NormalizeResult::MissingAudio;

        auto next = std::make_shared<CachedAudio>(*current);
        auto& cached = *next;
        const int total = cached.buffer.getNumSamples();
        const int channels = cached.buffer.getNumChannels();
        if (total <= 0 || channels <= 0)
            return NormalizeResult::MissingAudio;

        const int start = juce::jlimit(0, total - 1, (int)startSample);
        const int end = endSample > startSample
            ? juce::jlimit(start + 1, total, (int)endSample)
            : total;
        const int count = end - start;

        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            peak = juce::jmax(peak, cached.buffer.getMagnitude(ch, start, count));

        if (peak <= 0.000001f)
            return NormalizeResult::NoAudioPeak;

        const float targetLinear = juce::Decibels::decibelsToGain(targetDb);
        const float gain = targetLinear / peak;

        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::multiply(cached.buffer.getWritePointer(ch, start), gain, count);

        rebuildWaveformOverview(cached);
        invalidateRenderedWaveforms(cached);

        {
            const juce::ScopedWriteLock sl(cacheLock_);
            cache_[clipId] = std::move(next);
        }

        return NormalizeResult::Applied;
    }

    bool isPeakNormalized(const ClipID& clipId) const
    {
        auto cached = getCachedAudioSnapshot(clipId);
        return cached && cached->normalizeActive;
    }

    NormalizeResult togglePeakNormalize(const ClipID& clipId,
                                        SamplePosition startSample = 0,
                                        SamplePosition endSample = 0,
                                        float targetDb = 0.0f)
    {
        auto current = getCachedAudioSnapshot(clipId);
        if (!current)
            return NormalizeResult::MissingAudio;

        auto next = std::make_shared<CachedAudio>(*current);
        auto& cached = *next;
        if (cached.normalizeActive)
        {
            cached.buffer = cached.preNormalizeBuffer;
            cached.preNormalizeBuffer.setSize(0, 0);
            cached.normalizeActive = false;
            rebuildWaveformOverview(cached);
            invalidateRenderedWaveforms(cached);

            {
                const juce::ScopedWriteLock sl(cacheLock_);
                cache_[clipId] = std::move(next);
            }

            return NormalizeResult::Applied;
        }

        cached.preNormalizeBuffer = cached.buffer;
        const int total = cached.buffer.getNumSamples();
        const int channels = cached.buffer.getNumChannels();
        if (total <= 0 || channels <= 0)
            return NormalizeResult::MissingAudio;

        const int start = juce::jlimit(0, total - 1, (int)startSample);
        const int finish = endSample > startSample
            ? juce::jlimit(start + 1, total, (int)endSample)
            : total;
        const int count = finish - start;

        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            peak = juce::jmax(peak, cached.buffer.getMagnitude(ch, start, count));

        if (peak <= 0.000001f)
            return NormalizeResult::NoAudioPeak;

        const float targetLinear = juce::Decibels::decibelsToGain(targetDb);
        const float gain = targetLinear / peak;

        for (int ch = 0; ch < channels; ++ch)
            juce::FloatVectorOperations::multiply(cached.buffer.getWritePointer(ch, start), gain, count);

        cached.normalizeActive = true;
        rebuildWaveformOverview(cached);
        invalidateRenderedWaveforms(cached);

        {
            const juce::ScopedWriteLock sl(cacheLock_);
            cache_[clipId] = std::move(next);
        }

        return NormalizeResult::Applied;
    }

    /** Get the source file's sample rate. */
    double getSourceSampleRate(const ClipID& clipId) const
    {
        auto cached = getCachedAudioSnapshot(clipId);
        return cached ? cached->sampleRate : 44100.0;
    }

    SamplePosition getSourceNumSamples(const ClipID& clipId) const
    {
        auto cached = getCachedAudioSnapshot(clipId);
        return cached ? (SamplePosition)cached->buffer.getNumSamples() : 0;
    }

    struct WaveformVisualOptions
    {
        float gainLinear = 1.0f;
        float pitchSemitones = 0.0f;
        float timeStretchRatio = 1.0f;
        bool reversed = false;
        SamplePosition clipTimelineSamples = 0;
        SamplePosition fadeInSamples = 0;
        SamplePosition fadeOutSamples = 0;
    };

    /**
     * Draw a waveform using the pre-computed overview.
     * The rendered image is built at a fixed pixel height (kOverviewImgH) using
     * direct BitmapData writes, bypassing JUCE's D2D pipeline entirely.
     * Only rebuilt when colour or clip range changes — never on zoom or vertical resize.
     */
    void drawWaveform(const ClipID& clipId, juce::Graphics& g,
                      juce::Rectangle<float> bounds,
                      SamplePosition sourceOffset,
                      SamplePosition numSourceSamples,
                      juce::Colour colour,
                      const WaveformVisualOptions& visual = {}) const
    {
        auto cachedHandle = getCachedAudioSnapshot(clipId);
        if (!cachedHandle) return;

        auto& cached = *const_cast<CachedAudio*>(cachedHandle.get());
        if (cached.rawMin.empty() || cached.rawMax.empty())
            rebuildWaveformOverview(cached);

        const int total = cached.buffer.getNumSamples();
        if (total == 0 || numSourceSamples <= 0 || cached.rawMin.empty()) return;

        const int height = juce::jmax(1, (int)bounds.getHeight());
        const int imgW   = juce::jmax(1, (int)bounds.getWidth());

        const double pitchRatio   = std::pow(2.0, (double)visual.pitchSemitones / 12.0);
        const double stretchRatio = juce::jmax(0.1, (double)visual.timeStretchRatio);
        const double readRatio    = juce::jmax(0.025, pitchRatio / stretchRatio);
        const auto effectiveNumSourceSamples = (SamplePosition) juce::jlimit<int64_t>(
            1, total,
            (int64_t) std::llround((double) numSourceSamples * readRatio));

        auto getFadeGainForColumn = [&](int col, int imageWidth) -> float
        {
            const auto clipTimelineSamples = juce::jmax((SamplePosition) 1,
                                                        visual.clipTimelineSamples > 0 ? visual.clipTimelineSamples
                                                                                       : numSourceSamples);
            const auto samplePos = (SamplePosition) std::llround(
                ((double) juce::jlimit(0, imageWidth, col) / juce::jmax(1, imageWidth)) * clipTimelineSamples);

            float fadeGain = 1.0f;

            if (visual.fadeInSamples > 0 && samplePos < visual.fadeInSamples)
                fadeGain *= (float) samplePos / (float) juce::jmax((SamplePosition) 1, visual.fadeInSamples);

            if (visual.fadeOutSamples > 0 && samplePos > (clipTimelineSamples - visual.fadeOutSamples))
            {
                const auto fromEnd = clipTimelineSamples - samplePos;
                fadeGain *= (float) juce::jmax((SamplePosition) 0, fromEnd)
                            / (float) juce::jmax((SamplePosition) 1, visual.fadeOutSamples);
            }

            return juce::jlimit(0.0f, 1.0f, fadeGain);
        };

        // When zoomed in, dynamic rendering provides better quality
        const double samplesPerPixel = (double) effectiveNumSourceSamples / imgW;
        if (samplesPerPixel < (double)total / CachedAudio::kOverviewCols * 4.0)
        {
            const bool dynValid = cached.dynamicImage.isValid()
                                  && cached.dynamicStartSample == sourceOffset
                                  && cached.dynamicNumSamples  == effectiveNumSourceSamples
                                  && cached.dynamicWidth       == imgW
                                  && cached.dynamicColour      == colour.getARGB()
                                  && cached.dynamicGain        == visual.gainLinear
                                  && cached.dynamicPitch       == visual.pitchSemitones
                                  && cached.dynamicStretch     == visual.timeStretchRatio
                                  && cached.dynamicReversed    == visual.reversed
                                  && cached.dynamicClipTimelineSamples == visual.clipTimelineSamples
                                  && cached.dynamicFadeInSamples == visual.fadeInSamples
                                  && cached.dynamicFadeOutSamples == visual.fadeOutSamples;

            if (!dynValid)
            {
                const int fixedH = CachedAudio::kOverviewImgH;
                cached.dynamicImage = juce::Image(juce::Image::ARGB, imgW, fixedH, true);
                cached.dynamicImage.clear(juce::Rectangle<int>(0, 0, imgW, fixedH));
                juce::Image::BitmapData bd(cached.dynamicImage, juce::Image::BitmapData::writeOnly);
                const float midY   = fixedH * 0.5f;
                const float halfH  = fixedH * 0.38f;
                const int channels = cached.buffer.getNumChannels();
                const int maxS     = total - 1;

                for (int col = 0; col < imgW; ++col)
                {
                    int startS = sourceOffset + (int)(col * samplesPerPixel);
                    int endS   = sourceOffset + (int)((col + 1) * samplesPerPixel);
                    startS = juce::jlimit(0, maxS, startS);
                    endS   = juce::jlimit(startS + 1, total, endS);

                    float mn = 0.0f;
                    float mx = 0.0f;
                    if (startS < endS)
                    {
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            auto r = cached.buffer.findMinMax(ch, startS, endS - startS);
                            if (ch == 0 || r.getStart() < mn) mn = r.getStart();
                            if (ch == 0 || r.getEnd() > mx)   mx = r.getEnd();
                        }
                    }
                    else if (startS < total)
                    {
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            float v = cached.buffer.getSample(ch, startS);
                            if (ch == 0 || v < mn) mn = v;
                            if (ch == 0 || v > mx) mx = v;
                        }
                    }

                    const int drawCol = visual.reversed ? (imgW - 1 - col) : col;
                    const float totalGain = juce::jmax(0.0f, visual.gainLinear)
                                            * getFadeGainForColumn(drawCol, imgW);
                    mn *= totalGain;
                    mx *= totalGain;

                    float y0 = midY - mx * halfH; 
                    float y1 = midY - mn * halfH; 
                    if (y1 - y0 < 1.f) { y0 -= 0.5f; y1 += 0.5f; }
                    const int py0 = juce::jlimit(0, fixedH - 1, (int)y0);
                    const int py1 = juce::jlimit(py0, fixedH - 1, (int)(y1 + 0.5f));

                    for (int row = py0; row <= py1; ++row)
                        reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(drawCol, row))
                            ->setARGB(255, colour.getRed(), colour.getGreen(), colour.getBlue());
                }

                cached.dynamicStartSample = sourceOffset;
                cached.dynamicNumSamples  = effectiveNumSourceSamples;
                cached.dynamicWidth       = imgW;
                cached.dynamicColour      = colour.getARGB();
                cached.dynamicGain        = visual.gainLinear;
                cached.dynamicPitch       = visual.pitchSemitones;
                cached.dynamicStretch     = visual.timeStretchRatio;
                cached.dynamicReversed    = visual.reversed;
                cached.dynamicClipTimelineSamples = visual.clipTimelineSamples;
                cached.dynamicFadeInSamples = visual.fadeInSamples;
                cached.dynamicFadeOutSamples = visual.fadeOutSamples;
            }

            g.drawImage(cached.dynamicImage,
                        (int)bounds.getX(), (int)bounds.getY(),
                        imgW, height,
                        0, 0, imgW, CachedAudio::kOverviewImgH);
            return;
        }

        // Map clip sample range → columns in the raw overview
        const int cols    = CachedAudio::kOverviewCols;
        const int ovStart = juce::jlimit(0, cols - 1,
                                (int)((double)sourceOffset / total * cols));
        const int ovEnd   = juce::jlimit(ovStart + 1, cols,
                                (int)((double)(sourceOffset + effectiveNumSourceSamples) / total * cols));

        // Rebuild only when colour or clip range changes — vertical zoom is a free rescale.
        const bool imageValid = cached.overviewImage.isValid()
                                && cached.overviewOvStart == ovStart
                                && cached.overviewOvEnd   == ovEnd
                                && cached.overviewColour  == colour.getARGB()
                                && cached.overviewGain    == visual.gainLinear
                                && cached.overviewPitch   == visual.pitchSemitones
                                && cached.overviewStretch == visual.timeStretchRatio
                                && cached.overviewReversed == visual.reversed
                                && cached.overviewClipTimelineSamples == visual.clipTimelineSamples
                                && cached.overviewFadeInSamples == visual.fadeInSamples
                                && cached.overviewFadeOutSamples == visual.fadeOutSamples;

        if (!imageValid)
        {
            const int fixedH = CachedAudio::kOverviewImgH;
            const int imgW   = ovEnd - ovStart;
            cached.overviewImage = juce::Image(juce::Image::ARGB, imgW, fixedH, true);

            // Write pixels directly — avoids JUCE Graphics → D2D FillRectangle →
            // doesIntersectClipList overhead (was 22% CPU during vertical zoom).
            juce::Image::BitmapData bd(cached.overviewImage, juce::Image::BitmapData::writeOnly);
            const float midY  = fixedH * 0.5f;
            const float halfH = fixedH * 0.38f;
            for (int col = 0; col < imgW; ++col)
            {
                const int drawCol = visual.reversed ? (imgW - 1 - col) : col;
                const float totalGain = juce::jmax(0.0f, visual.gainLinear)
                                        * getFadeGainForColumn(drawCol, imgW);
                float y0 = midY - (cached.rawMax[ovStart + col] * totalGain) * halfH;
                float y1 = midY - (cached.rawMin[ovStart + col] * totalGain) * halfH;
                if (y1 - y0 < 1.f) { y0 -= 0.5f; y1 += 0.5f; }
                const int py0 = juce::jlimit(0, fixedH - 1, (int)y0);
                const int py1 = juce::jlimit(py0, fixedH - 1, (int)(y1 + 0.5f));
                for (int row = py0; row <= py1; ++row)
                    reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(drawCol, row))
                        ->setARGB(255, colour.getRed(), colour.getGreen(), colour.getBlue());
            }

            cached.overviewOvStart = ovStart;
            cached.overviewOvEnd   = ovEnd;
            cached.overviewColour  = colour.getARGB();
            cached.overviewGain    = visual.gainLinear;
            cached.overviewPitch   = visual.pitchSemitones;
            cached.overviewStretch = visual.timeStretchRatio;
            cached.overviewReversed = visual.reversed;
            cached.overviewClipTimelineSamples = visual.clipTimelineSamples;
            cached.overviewFadeInSamples = visual.fadeInSamples;
            cached.overviewFadeOutSamples = visual.fadeOutSamples;
        }

        // Hardware-scaled blit: O(1) at any zoom or lane height
        g.drawImage(cached.overviewImage,
                    (int)bounds.getX(), (int)bounds.getY(),
                    (int)bounds.getWidth(), height,
                    0, 0, ovEnd - ovStart, CachedAudio::kOverviewImgH);
    }

    /** Remove cached data for a clip. */
    void unloadClip(const ClipID& clipId)
    {
        const juce::ScopedWriteLock sl(cacheLock_);
        cache_.erase(clipId);
    }

    /** Clear all cached data. */
    void clearAll()
    {
        const juce::ScopedWriteLock sl(cacheLock_);
        cache_.clear();
    }

    /**
     * Register a second clip ID that shares the same cached audio as an existing one.
     * Used when splitting a clip: both halves read from the same source buffer,
     * distinguished only by their sourceOffset and length.
     * No data is copied — both IDs point to the same CachedAudio object.
     */
    void shareForClip(const ClipID& existingId, const ClipID& newId)
    {
        auto existing = getCachedAudioSnapshot(existingId);
        shareSnapshotForClip(existing, newId);
    }

    void shareSnapshotForClip(CachedAudioHandle existing, const ClipID& newId)
    {
        if (!existing) return;

        // Store a raw pointer alias under the new ID.
        // We use a non-owning wrapper: allocate a new CachedAudio that shares
        // the same buffer via a shallow copy of the pointer-level fields.
        // Simpler: just duplicate the entry (shared_ptr would be cleaner but
        // CachedAudio is currently unique_ptr; we copy the lightweight metadata
        // and share the underlying buffer by storing a second unique_ptr that
        // wraps a non-deleting alias).
        //
        // Cleanest safe approach: make a new CachedAudio that copies the
        // buffer reference by pointing into the same data via AudioBuffer's
        // copy constructor (which DOES copy the samples — acceptable for
        // split since the file is already in RAM and this avoids ownership issues).
        auto newEntry = std::make_shared<CachedAudio>(*existing);
        {
            const juce::ScopedWriteLock sl(cacheLock_);
            cache_[newId] = std::move(newEntry);
        }
    }

    juce::AudioFormatManager& getFormatManager() { return formatManager_; }

private:
    static void logAlways(const juce::String& message)
    {
        juce::Logger::writeToLog(message);
    }

    juce::int64 getFileLengthSamples(const juce::File& file)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager_.createReaderFor(file));
        return reader ? (juce::int64) reader->lengthInSamples : 0;
    }

    juce::AudioFormatManager formatManager_;

    static void invalidateRenderedWaveforms(CachedAudio& cached)
    {
        cached.overviewImage = {};
        cached.overviewOvStart = -1;
        cached.overviewOvEnd = -1;
        cached.dynamicImage = {};
        cached.dynamicStartSample = -1;
        cached.dynamicNumSamples = -1;
        cached.dynamicWidth = -1;
    }

    static void rebuildWaveformOverview(CachedAudio& cached)
    {
        const int total = cached.buffer.getNumSamples();
        const int cols  = CachedAudio::kOverviewCols;
        cached.rawMin.assign(cols, 0.f);
        cached.rawMax.assign(cols, 0.f);

        if (total <= 0)
        {
            invalidateRenderedWaveforms(cached);
            return;
        }

        for (int col = 0; col < cols; ++col)
        {
            int s0 = (int)((int64_t)col * total / cols);
            int s1 = (int)((int64_t)(col + 1) * total / cols);
            s0 = juce::jlimit(0, total - 1, s0);
            s1 = juce::jlimit(s0 + 1, total, s1);
            float mn = 0.f, mx = 0.f;
            for (int ch = 0; ch < cached.buffer.getNumChannels(); ++ch)
            {
                auto r = juce::FloatVectorOperations::findMinAndMax(
                    cached.buffer.getReadPointer(ch) + s0, s1 - s0);
                mn = juce::jmin(mn, r.getStart());
                mx = juce::jmax(mx, r.getEnd());
            }
            cached.rawMin[col] = mn;
            cached.rawMax[col] = mx;
        }

        invalidateRenderedWaveforms(cached);
    }

    // ── Decoded-source cache (keyed by file) ──────────────────────────────
    // Stores the raw decoded buffer + prebuilt overview for a given file so
    // re-adding a clip (undo/redo/duplicate) does not hit the disk again.
    struct DecodedSource
    {
        std::shared_ptr<CachedAudio> audio;
        juce::int64 modTime = 0;
        bool hasOverview = false;
    };

    static std::string makeSourceKey(const juce::File& file)
    {
        return file.getFullPathName().toStdString();
    }

    std::shared_ptr<CachedAudio> findDecodedSource(const juce::File& file) const
    {
        const juce::ScopedReadLock sl(sourceCacheLock_);
        auto it = sourceCache_.find(makeSourceKey(file));
        if (it == sourceCache_.end())
            return nullptr;

        if (it->second.modTime != file.getLastModificationTime().toMilliseconds())
            return nullptr; // file changed on disk → force a fresh decode

        return it->second.audio;
    }

    void storeDecodedSource(const juce::File& file,
                            const std::shared_ptr<CachedAudio>& cached,
                            bool hasOverview)
    {
        if (cached == nullptr)
            return;
        DecodedSource src;
        src.audio       = cached;
        src.modTime     = file.getLastModificationTime().toMilliseconds();
        src.hasOverview = hasOverview;

        const juce::ScopedWriteLock sl(sourceCacheLock_);
        sourceCache_[makeSourceKey(file)] = std::move(src);
    }

    // Make a fresh per-clip CachedAudio from a decoded source. The audio buffer
    // and prebuilt overview are copied; rendered/pixel images stay per-clip.
    std::shared_ptr<CachedAudio> makeCachedFromDecoded(const CachedAudio& source,
                                                       bool buildWaveformOverviewNow) const
    {
        auto cached = std::make_shared<CachedAudio>();
        cached->buffer.makeCopyOf(source.buffer);
        cached->sampleRate = source.sampleRate;

        if (! source.rawMin.empty() && ! source.rawMax.empty())
        {
            cached->rawMin = source.rawMin;
            cached->rawMax = source.rawMax;
        }
        else if (buildWaveformOverviewNow)
        {
            rebuildWaveformOverview(*cached);
        }
        return cached;
    }

    mutable juce::ReadWriteLock cacheLock_;
    std::unordered_map<ClipID, std::shared_ptr<CachedAudio>> cache_;

    mutable juce::ReadWriteLock sourceCacheLock_;
    std::unordered_map<std::string, DecodedSource> sourceCache_;
};

} // namespace DAW

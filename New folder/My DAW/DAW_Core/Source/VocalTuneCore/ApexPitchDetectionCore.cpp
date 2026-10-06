// =============================================================================
//  ApexPitchDetectionCore.cpp
//  YIN pitch detection -- implementation.
//
//  Drop-in: Source/VocalTuneCore/ApexPitchDetectionCore.cpp
// =============================================================================

#include "ApexPitchDetectionCore.h"

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
float ApexPitchDetectionCore::parabolicInterp (float yLeft, float yCenter, float yRight) noexcept
{
    const float denom = (yLeft - 2.0f * yCenter + yRight);
    if (std::abs (denom) < 1.0e-9f) return 0.0f;
    return 0.5f * (yLeft - yRight) / denom;
}

// -----------------------------------------------------------------------------
float ApexPitchDetectionCore::yinFrameHz (const float* frame,
                                          int          frameSize,
                                          double       sampleRate,
                                          int          tauMin,
                                          int          tauMax,
                                          float        threshold,
                                          float&       outConf)
{
    outConf = 0.0f;

    const int W = frameSize - tauMax;
    if (W <= 0) return 0.0f;

    // -------------------------------------------------------------------------
    // Step 1: difference function d(tau) = sum_{j=0..W-1} (x[j] - x[j+tau])^2
    // -------------------------------------------------------------------------
    std::vector<float> d ((size_t) (tauMax + 1), 0.0f);

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j)
        {
            const float diff = frame[j] - frame[j + tau];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }

    // -------------------------------------------------------------------------
    // Step 2: cumulative mean normalized difference function d'(tau).
    //   d'(tau) = d(tau) * tau / sum_{i=1..tau} d(i)
    //   d'(0)   = 1
    // -------------------------------------------------------------------------
    std::vector<float> dPrime ((size_t) (tauMax + 1), 1.0f);
    dPrime[0] = 1.0f;

    float runningSum = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        runningSum += d[(size_t) tau];
        dPrime[(size_t) tau] = (runningSum > 1.0e-9f)
            ? (d[(size_t) tau] * (float) tau / runningSum)
            : 1.0f;
    }

    // -------------------------------------------------------------------------
    // Step 3: absolute threshold. First tau >= tauMin where d'(tau) < threshold,
    //         then walk to the local minimum.
    // -------------------------------------------------------------------------
    int chosenTau = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (dPrime[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax
                   && dPrime[(size_t) (tau + 1)] < dPrime[(size_t) tau])
            {
                ++tau;
            }
            chosenTau = tau;
            break;
        }
    }

    if (chosenTau < 0) return 0.0f;

    // -------------------------------------------------------------------------
    // Step 4: parabolic interpolation for sub-sample tau precision.
    // -------------------------------------------------------------------------
    float refinedTau = (float) chosenTau;
    if (chosenTau > 0 && chosenTau < tauMax)
    {
        refinedTau += parabolicInterp (dPrime[(size_t) (chosenTau - 1)],
                                       dPrime[(size_t)  chosenTau     ],
                                       dPrime[(size_t) (chosenTau + 1)]);
    }

    // Confidence proxy: 1 - d'(chosenTau). Higher = stronger periodicity.
    outConf = juce::jlimit (0.0f, 1.0f, 1.0f - dPrime[(size_t) chosenTau]);

    return (refinedTau > 0.0f) ? (float) (sampleRate / refinedTau) : 0.0f;
}

// -----------------------------------------------------------------------------
ApexTuneAnalysis ApexPitchDetectionCore::analyzeOffline (const float* mono,
                                                         int          numSamples,
                                                         double       sampleRate,
                                                         const Params& p)
{
    ApexTuneAnalysis result;
    result.frameSize       = p.frameSize;
    result.hopSamples      = p.hopSamples;
    result.sampleRate      = sampleRate;
    result.numSourceSamples = numSamples;

    if (mono == nullptr || numSamples < p.frameSize || sampleRate <= 0.0)
        return result;

    const int tauMin = juce::jmax (2,                (int) std::floor (sampleRate / p.maxHz));
    const int tauMax = juce::jmin (p.frameSize / 2,  (int) std::ceil  (sampleRate / p.minHz));
    if (tauMin >= tauMax) return result;

    const int numFrames = (numSamples - p.frameSize) / p.hopSamples + 1;
    result.frames.reserve ((size_t) numFrames);

    for (int i = 0; i < numFrames; ++i)
    {
        const int    start = i * p.hopSamples;
        const float* frame = mono + start;

        // -- Frame RMS in dBFS ------------------------------------------------
        float ss = 0.0f;
        for (int j = 0; j < p.frameSize; ++j) ss += frame[j] * frame[j];
        const float rms = std::sqrt (ss / (float) p.frameSize);
        const float dB  = 20.0f * std::log10 (rms + 1.0e-9f);

        ApexTuneFrame f;
        f.energyDb = dB;

        if (dB < p.voicedEnergyDb)
        {
            // Below energy gate -- treat as unvoiced silence/breath.
            f.f0Hz       = 0.0f;
            f.confidence = 0.0f;
            f.voiced     = false;
        }
        else
        {
            float conf = 0.0f;
            const float f0 = yinFrameHz (frame, p.frameSize, sampleRate,
                                         tauMin, tauMax, p.yinThreshold, conf);
            f.f0Hz       = f0;
            f.confidence = conf;
            f.voiced     = (f0 > 0.0f && conf >= p.voicedConfidence);
        }

        result.frames.push_back (f);
    }

    return result;
}

}} // namespace apex::vocaltune

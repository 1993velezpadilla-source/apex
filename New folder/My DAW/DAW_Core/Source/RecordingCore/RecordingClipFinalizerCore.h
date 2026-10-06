#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "../UtilityCore/Types.h"
#include "../ClipCore/Clip.h"
#include "../TrackCore/Track.h"
#include "../AudioEngineCore/AudioFileManager.h"

namespace DAW {

/** Converts a completed recording WAV into an AudioClip on the timeline. */
class RecordingClipFinalizerCore
{
public:
    struct FinalizeRequest
    {
        TrackID           trackId;
        SamplePosition    startPosition  = 0;
        int               samplesWritten = 0;
        double            sampleRate     = 44100.0;
        juce::File        outputFile;
        ClipManager*      clipManager    = nullptr;
        TrackManager*     trackManager   = nullptr;
        AudioFileManager* audioFiles     = nullptr;
    };

    static AudioClip* finalize(const FinalizeRequest& req)
    {
        logAlways("[APEX-DIAG-CRASH] finalize ENTER track=" + req.trackId
            + " samplesWritten=" + juce::String(req.samplesWritten)
            + " sampleRate=" + juce::String(req.sampleRate)
            + " fileExists=" + juce::String(req.outputFile.existsAsFile() ? 1 : 0));

        if (req.clipManager == nullptr) return nullptr;
        if (req.audioFiles == nullptr) return nullptr;

        // Belt-and-braces: trust the WAV file over the in-memory counter.
        // If the push counter reads 0 but the file on disk has real frames
        // (counter race, writer flush timing), the take MUST still survive.
        juce::int64 lengthSamples = juce::jmax(0, req.samplesWritten);
        if (lengthSamples <= 0 && req.outputFile.existsAsFile())
        {
            juce::WavAudioFormat wavFormat;
            if (auto stream = req.outputFile.createInputStream())
            {
                if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
                        wavFormat.createReaderFor(stream.release(), true)))
                {
                    lengthSamples = reader->lengthInSamples;
                    logAlways("[REC] finalize: counter was 0 but WAV file has "
                        + juce::String(lengthSamples) + " frames — recovering take from disk.");
                }
            }
        }

        if (lengthSamples <= 0)
        {
            logAlways("[REC] finalize: take DISCARDED for track=" + req.trackId
                + " — zero samples captured and file empty/missing ("
                + req.outputFile.getFullPathName() + "). See [REC]/[REC-GUARD] log lines above for the cause.");
            return nullptr;
        }

        if (!req.outputFile.existsAsFile())
        {
            logAlways("[REC] finalize: take DISCARDED for track=" + req.trackId
                + " — take file missing on disk: " + req.outputFile.getFullPathName());
            return nullptr;
        }

        const SamplePosition clipLen =
            juce::jmax((SamplePosition) 1, (SamplePosition) lengthSamples);

        juce::ValueTree state("AudioClip");
        state.setProperty("name", req.outputFile.getFileNameWithoutExtension(), nullptr);
        state.setProperty("type", (int) ClipType::Audio, nullptr);
        state.setProperty("trackID", req.trackId, nullptr);
        state.setProperty("startPosition", (juce::int64) req.startPosition, nullptr);
        state.setProperty("length", (juce::int64) clipLen, nullptr);
        state.setProperty("sourceOffset", (juce::int64) 0, nullptr);
        state.setProperty("sourceFile", req.outputFile.getFullPathName(), nullptr);
        state.setProperty("gain", 1.0f, nullptr);

        SamplePosition fadeLen =
            (SamplePosition) std::lround(juce::jmax(1.0, req.sampleRate) * 0.005);
        fadeLen = juce::jmin(fadeLen, clipLen / 2);
        fadeLen = juce::jmax((SamplePosition) 1, fadeLen);

        logAlways("[APEX-DIAG-CRASH] finalize sizes clipLen=" + juce::String((juce::int64) clipLen)
            + " fadeLen=" + juce::String((juce::int64) fadeLen));

        state.setProperty("fadeInLength",  (juce::int64) fadeLen, nullptr);
        state.setProperty("fadeOutLength", (juce::int64) fadeLen, nullptr);
        state.setProperty("fadeInCurve",   3, nullptr);
        state.setProperty("fadeOutCurve",  3, nullptr);

        if (req.trackManager != nullptr)
            if (auto* track = req.trackManager->getTrack(req.trackId))
                state.setProperty("color", track->getColor().brighter(0.1f).toString(), nullptr);

        auto* clip = dynamic_cast<AudioClip*>(req.clipManager->recreateClipFromState(state));
        if (clip == nullptr) return nullptr;

        logAlways("[APEX-DIAG-CRASH] finalize pre-loadForClip clipId=" + clip->getID()
            + " file=" + req.outputFile.getFullPathName());
        req.audioFiles->loadForClip(clip->getID(), req.outputFile);

        logAlways("[APEX-DIAG-CRASH] finalize RETURN clipId=" + clip->getID());

        return clip;
    }

private:
    static void logAlways(const juce::String& message)
    {
        juce::Logger::writeToLog(message);
    }
};

} // namespace DAW

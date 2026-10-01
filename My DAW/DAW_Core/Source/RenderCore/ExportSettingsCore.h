#pragma once
#include <JuceHeader.h>
#include "../UtilityCore/Types.h"
#include <vector>

namespace DAW {

enum class ExportAudioFormat
{
    Wav,
    Aiff,
    Flac,
    OggVorbis
};

enum class ExportContent
{
    FullMix,
    Stems
};

enum class StemTargetKind
{
    Track,
    FolderBus
};

struct StemExportTarget
{
    TrackID trackId;
    juce::String displayName;
    StemTargetKind kind = StemTargetKind::Track;
    // Captured message-thread source scope. A FolderBus target contains the
    // folder track plus its descendants; a track target contains one ID.
    std::vector<TrackID> sourceTrackIds;
};

struct ExportSettings
{
    juce::File outputFile;
    juce::File outputDirectory;
    ExportContent content = ExportContent::FullMix;
    ExportAudioFormat format = ExportAudioFormat::Wav;
    std::vector<StemExportTarget> stemTargets;
    double  sampleRate    = 0.0;
    int     bitDepth      = 24;
    int     qualityIndex  = 5;
    int     blockSize     = 512;
    int64_t startSample   = 0;
    int64_t endSample     = 0;
    double  tailSeconds   = 0.0;
    bool    includeMasterFx   = true;
    bool    includeAutomation = true;
    bool    includePlugins    = true;
    bool    applyMasterProcessingToStems = false;
};

inline juce::String exportFormatName(ExportAudioFormat format)
{
    switch (format)
    {
        case ExportAudioFormat::Wav:       return "WAV";
        case ExportAudioFormat::Aiff:      return "AIFF";
        case ExportAudioFormat::Flac:      return "FLAC";
        case ExportAudioFormat::OggVorbis: return "Ogg Vorbis";
    }
    return "Audio";
}

inline juce::String exportFormatExtension(ExportAudioFormat format)
{
    switch (format)
    {
        case ExportAudioFormat::Wav:       return ".wav";
        case ExportAudioFormat::Aiff:      return ".aiff";
        case ExportAudioFormat::Flac:      return ".flac";
        case ExportAudioFormat::OggVorbis: return ".ogg";
    }
    return ".wav";
}

} // namespace DAW

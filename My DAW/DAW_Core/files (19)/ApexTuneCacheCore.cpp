// =============================================================================
//  ApexTuneCacheCore.cpp
//  See header. Binary serialization of ApexTuneAnalysis + WAV I/O for renders.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneCacheCore.cpp
// =============================================================================

#include "ApexTuneCacheCore.h"

namespace apex { namespace vocaltune {

namespace
{
    // Binary file header for analysis cache.
    constexpr juce::uint32 kAnalysisMagic   = 0x41505841; // 'APXA' little-endian
    constexpr juce::uint32 kAnalysisVersion = 1;

    juce::File ensureDir (const juce::File& dir)
    {
        if (! dir.exists()) dir.createDirectory();
        return dir;
    }
}

// -----------------------------------------------------------------------------
juce::File ApexTuneCacheCore::getCacheRoot()
{
    const auto root = juce::File::getSpecialLocation
                        (juce::File::userApplicationDataDirectory)
                            .getChildFile ("APEX")
                            .getChildFile ("VocalTuneCache");
    return ensureDir (root);
}

// -----------------------------------------------------------------------------
juce::String ApexTuneCacheCore::fingerprintSource
    (const juce::File& sourceFile,
     const ApexPitchDetectionCore::Params& p)
{
    // Build a deterministic input string covering everything that should
    // invalidate the cache when changed.
    juce::String s;
    s << sourceFile.getFullPathName() << "|";
    s << sourceFile.getSize()         << "|";
    s << sourceFile.getLastModificationTime().toMilliseconds() << "|";
    s << "f=" << p.frameSize           << ",";
    s << "h=" << p.hopSamples          << ",";
    s << "lo=" << p.minHz              << ",";
    s << "hi=" << p.maxHz              << ",";
    s << "yt=" << p.yinThreshold       << ",";
    s << "eg=" << p.voicedEnergyDb     << ",";
    s << "vc=" << p.voicedConfidence;

    return juce::SHA256 (s.toRawUTF8(), (size_t) s.getNumBytesAsUTF8()).toHexString();
}

// -----------------------------------------------------------------------------
juce::File ApexTuneCacheCore::analysisCacheFile (const juce::String& fingerprint)
{
    return getCacheRoot().getChildFile (fingerprint + ".apxan");
}

juce::File ApexTuneCacheCore::renderCacheFile (const juce::String& fingerprint, int renderVersion)
{
    return getCacheRoot().getChildFile (fingerprint + "_r"
                                       + juce::String (renderVersion) + ".wav");
}

// -----------------------------------------------------------------------------
bool ApexTuneCacheCore::saveAnalysis (const juce::File& dest, const ApexTuneAnalysis& a)
{
    dest.deleteFile();
    juce::FileOutputStream os (dest);
    if (! os.openedOk()) return false;

    // Header
    os.writeInt   ((int) kAnalysisMagic);
    os.writeInt   ((int) kAnalysisVersion);
    os.writeInt   ((int) a.frames.size());
    os.writeInt   (a.frameSize);
    os.writeInt   (a.hopSamples);
    os.writeDouble(a.sampleRate);
    os.writeInt   (a.numSourceSamples);

    // Frames
    for (const auto& f : a.frames)
    {
        os.writeFloat (f.f0Hz);
        os.writeFloat (f.confidence);
        os.writeByte  ((char) (f.voiced ? 1 : 0));
        os.writeFloat (f.energyDb);
    }

    return os.getStatus().wasOk();
}

// -----------------------------------------------------------------------------
bool ApexTuneCacheCore::loadAnalysis (const juce::File& src, ApexTuneAnalysis& outA)
{
    if (! src.existsAsFile()) return false;

    juce::FileInputStream is (src);
    if (! is.openedOk()) return false;

    const auto magic   = (juce::uint32) is.readInt();
    const auto version = (juce::uint32) is.readInt();
    if (magic != kAnalysisMagic || version != kAnalysisVersion) return false;

    const int numFrames = is.readInt();
    if (numFrames < 0 || numFrames > 100'000'000) return false;     // sanity

    outA.frames.clear();
    outA.frames.reserve ((size_t) numFrames);

    outA.frameSize        = is.readInt();
    outA.hopSamples       = is.readInt();
    outA.sampleRate       = is.readDouble();
    outA.numSourceSamples = is.readInt();

    for (int i = 0; i < numFrames; ++i)
    {
        ApexTuneFrame f;
        f.f0Hz       = is.readFloat();
        f.confidence = is.readFloat();
        f.voiced     = (is.readByte() != 0);
        f.energyDb   = is.readFloat();
        outA.frames.push_back (f);

        if (is.isExhausted() && i + 1 < numFrames) return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
bool ApexTuneCacheCore::saveRenderWav (const juce::File& dest,
                                       const float*      mono,
                                       int               numSamples,
                                       double            sampleRate)
{
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0) return false;

    dest.deleteFile();
    juce::WavAudioFormat wav;

    std::unique_ptr<juce::FileOutputStream> stream (dest.createOutputStream());
    if (stream == nullptr) return false;

    std::unique_ptr<juce::AudioFormatWriter> writer
        (wav.createWriterFor (stream.get(), sampleRate, 1 /*mono*/, 32 /*float*/,
                              {}, 0));
    if (writer == nullptr) return false;

    stream.release(); // writer owns it now

    // AudioFormatWriter wants float* const*. We have a const float*; wrap it.
    const float* channelData[1] = { mono };
    juce::AudioBuffer<float> tmp (const_cast<float* const*>(channelData), 1, numSamples);

    return writer->writeFromAudioSampleBuffer (tmp, 0, numSamples);
}

// -----------------------------------------------------------------------------
bool ApexTuneCacheCore::loadRenderWav (const juce::File& src,
                                       std::vector<float>& outMono,
                                       double& outSampleRate)
{
    outMono.clear();
    outSampleRate = 0.0;
    if (! src.existsAsFile()) return false;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));
    if (reader == nullptr) return false;

    const int   numSamples = (int) reader->lengthInSamples;
    const int   numChans   = (int) reader->numChannels;
    outSampleRate = reader->sampleRate;

    if (numSamples <= 0 || numChans <= 0) return false;

    juce::AudioBuffer<float> buf (numChans, numSamples);
    reader->read (&buf, 0, numSamples, 0, true, numChans > 1);

    outMono.assign ((size_t) numSamples, 0.0f);
    if (numChans == 1)
    {
        const float* src0 = buf.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i) outMono[(size_t) i] = src0[i];
    }
    else
    {
        // Downmix to mono if cache was somehow saved multi-channel.
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < numChans; ++c)
                sum += buf.getReadPointer (c)[i];
            outMono[(size_t) i] = sum / (float) numChans;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
int ApexTuneCacheCore::clearStaleRenders (const juce::String& fingerprint, int keepVersion)
{
    const auto root = getCacheRoot();
    if (! root.isDirectory()) return 0;

    juce::Array<juce::File> hits;
    root.findChildFiles (hits, juce::File::findFiles, false,
                         fingerprint + "_r*.wav");

    int deleted = 0;
    for (const auto& f : hits)
    {
        const auto name = f.getFileNameWithoutExtension();    // <fp>_r<N>
        const int rPos  = name.lastIndexOf ("_r");
        if (rPos < 0) continue;

        const int ver = name.substring (rPos + 2).getIntValue();
        if (ver != keepVersion)
        {
            if (f.deleteFile()) ++deleted;
        }
    }
    return deleted;
}

}} // namespace apex::vocaltune

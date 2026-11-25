#!/usr/bin/env python3
"""Patch the pinned JUCE 8.0.12 Android Oboe backend.

These are trial requests, not hardware capability guarantees. JUCE's device
manager otherwise replaces non-burst-aligned requests with its default.
Expose the initial negotiated output queue size for these trial requests.
Do not force a fixed callback quantum or alter the desktop backend.
"""
from pathlib import Path
import sys

def patch(source):
    edits = [
        (
            "return AndroidHighPerformanceAudioHelpers::getAvailableBufferSizes (getNativeBufferSize(), getAvailableSampleRates());",
            """auto sizes = AndroidHighPerformanceAudioHelpers::getAvailableBufferSizes (getNativeBufferSize(), getAvailableSampleRates());
        // APEX experimental requests: Oboe may clamp these to its native minimum.
        for (const int trial : { 32, 48, 64, 96, 128, 160 })
            sizes.addIfNotAlreadyThere (trial);
        sizes.sort();
        return sizes;"""
        ),
        (
            '        if (! deviceOpen)\n            lastError = "Failed to create audio session";',
            '''        if (! deviceOpen)
            lastError = "Failed to create audio session";
        else if (actualBufferSize < 192)
        {
            const auto negotiated = session->getInitialOutputBufferSize();
            if (negotiated > 0)
                actualBufferSize = negotiated;
        }'''
        ),
        (
            "        int getCurrentBitDepth() const noexcept { return bitDepth; }",
            """        int getCurrentBitDepth() const noexcept { return bitDepth; }

        int getInitialOutputBufferSize() const
        {
            if (outputStream != nullptr && outputStream->openedOk())
                if (auto stream = outputStream->getNativeStream())
                    return stream->getBufferSizeInFrames();

            return 0;
        }"""
        ),
    ]
    for before, after in edits:
        if source.count(before) != 1:
            raise SystemExit("Unexpected JUCE Oboe source; refusing ambiguous patch")
        source = source.replace(before, after)
    return source

if __name__ == "__main__":
    path = Path(sys.argv[1]) / "modules/juce_audio_devices/native/juce_Oboe_android.cpp"
    original = path.read_text(encoding="utf-8")
    if "// APEX experimental requests:" not in original:
        path.write_text(patch(original), encoding="utf-8")
    print("APEX Oboe trial buffer requests enabled")

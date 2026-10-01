# Recording Identity and Live Waveform Regressions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve imported clips when recording after project reload and display truthful live recording state for low-buffer-size and unavailable-input devices.

**Architecture:** `ClipManager` will issue collision-resistant `CLIP_<UUID>` identities for new clips while preserving legacy persisted IDs. The recording callback will propagate actual valid input count separately from prepared capacity, write aligned silence for unavailable routes, publish bounded input status and peaks, and let the UI aggregate multiple callback peaks into each display pixel.

**Tech Stack:** C++20, JUCE 8.0.12, JUCE `UnitTest`, Projucer/Visual Studio 2026 projects, PowerShell build/test scripts.

## Global Constraints

- Work only in `C:\Users\1993v\OneDrive\Desktop\Apex backup\My DAW\DAW_Core` except for this plan and its approved design document.
- Preserve existing saved IDs such as `CLIP_1` exactly as loaded.
- Do not change counter-based track, plugin, route, or sidechain identities.
- Do not overwrite or revert unrelated dirty working-tree files.
- Do not allocate, block, perform I/O, call GUI code, or format logs in the realtime callback.
- Keep recording audio independent from waveform rendering and UI refresh.
- Keep source WAV deletion behavior unchanged; recording undo removes only timeline clips.
- Do not change monitoring ownership, routing authority, PDC, offline rendering, or export behavior.
- Do not commit unless the user explicitly requests a commit.
- Compilation alone is not behavioral proof; Apollo hardware validation remains required.

---

## File map

- Create `Tests/Source/Recording/RecordingIdentityRegressionTests.cpp`
  - Reproduces legacy persisted-ID collision and recording undo behavior.
- Create `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`
  - Verifies low-buffer rendering, pixel aggregation, and unavailable-input state.
- Create `Source/RecordingCore/RecordingInputValidityCore.h`
  - Pure, allocation-free functions for valid callback count and mono/stereo route availability.
- Modify `Source/UtilityCore/Types.h` and `Source/UtilityCore/Types.cpp`
  - Replace only fresh clip counter output with UUID-backed clip identities.
- Modify `Source/ClipCore/Clip.h` and `Source/ClipCore/Clip.cpp`
  - Centralize fresh clip identity under `ClipManager`, check uniqueness before insertion, and diagnose duplicate persisted IDs.
- Modify `Source/RecordingCore/LiveRecordWaveformCore.h`
  - Add atomic input availability and UI-thread min/max pixel aggregation.
- Modify `Source/RecordingCore/RecordingEngine.h`
  - Resolve routes from actual valid channels, write prepared silence for unavailable routes, and publish truthful waveform status.
- Modify `Source/MainComponent.cpp`
  - Publish callback-valid channel count before processing instead of inferring validity from allocated capacity.
- Modify `Source/AppCore/ApplicationCore.cpp`
  - Clamp preserved input to actual valid channels and pass that count to the recorder.
- Modify `Source/UICore/ArrangementView.h`
  - Draw peaks only for valid input and show `No input — recording silence` otherwise.
- Modify `Tests/APEXTests.jucer`
  - Register both regression files in the canonical test project.
- Modify `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj`
  - Merge the same compile entries into the currently generated and already-dirty Visual Studio project without removing other entries.

---

### Task 1: Add the failing persisted-clip identity regression

**Files:**
- Create: `Tests/Source/Recording/RecordingIdentityRegressionTests.cpp`
- Modify: `Tests/APEXTests.jucer:28-31`
- Modify: `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj:148-174`

**Interfaces:**
- Consumes: `DAW::ClipManager::restoreState`, `DAW::ClipManager::recreateClipFromState`, and `DAW::RecordAudioTakeCommand`.
- Produces: Test name `recording.reload-clip-identity.v1` in category `APEX.Recording`.

- [ ] **Step 1: Add the focused failing test**

Create `Tests/Source/Recording/RecordingIdentityRegressionTests.cpp`:

```cpp
#include <JuceHeader.h>
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/CommandCore/RecordCommands.h"
#include <set>

class RecordingIdentityRegressionTests final : public juce::UnitTest
{
public:
    RecordingIdentityRegressionTests()
        : juce::UnitTest ("recording.reload-clip-identity.v1", "APEX.Recording") {}

    void runTest() override
    {
        beginTest ("recording after restore cannot reuse the imported beat ID");

        DAW::ClipManager clips;

        juce::ValueTree restoredClips ("Clips");
        juce::ValueTree beatState ("AudioClip");
        beatState.setProperty ("id", "CLIP_1", nullptr);
        beatState.setProperty ("name", "Imported Beat", nullptr);
        beatState.setProperty ("type", (int) DAW::ClipType::Audio, nullptr);
        beatState.setProperty ("trackID", "TRK_1", nullptr);
        beatState.setProperty ("startPosition", (juce::int64) 0, nullptr);
        beatState.setProperty ("length", (juce::int64) 48000, nullptr);
        beatState.setProperty ("sourceFile", "beat.wav", nullptr);
        restoredClips.appendChild (beatState, nullptr);
        clips.restoreState (restoredClips);

        auto* importedBeat = dynamic_cast<DAW::AudioClip*> (clips.getClip ("CLIP_1"));
        expect (importedBeat != nullptr, "legacy beat must restore as CLIP_1");

        juce::ValueTree takeState ("AudioClip");
        takeState.setProperty ("name", "Track 3 Take", nullptr);
        takeState.setProperty ("type", (int) DAW::ClipType::Audio, nullptr);
        takeState.setProperty ("trackID", "TRK_3", nullptr);
        takeState.setProperty ("startPosition", (juce::int64) 0, nullptr);
        takeState.setProperty ("length", (juce::int64) 24000, nullptr);
        takeState.setProperty ("sourceFile", "take.wav", nullptr);

        auto* recordedTake = dynamic_cast<DAW::AudioClip*> (
            clips.recreateClipFromState (takeState));
        expect (recordedTake != nullptr, "recorded take must be created");
        if (recordedTake == nullptr)
            return;

        expect (recordedTake->getID().startsWith ("CLIP_"));
        expect (recordedTake->getID() != importedBeat->getID(),
                "recorded take must not reuse the restored beat ID");

        std::set<std::string> ids;
        for (auto* clip : clips.getAllClips())
            ids.insert (clip->getID().toStdString());
        expectEquals ((int) ids.size(), clips.getAllClips().size(),
                      "all live clip IDs must be unique");

        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("apex_recording_identity_" + juce::Uuid().toString());
        expect (tempDir.createDirectory(), "temporary fixture directory must be created");
        const auto beatFile = tempDir.getChildFile ("beat.wav");
        const auto takeFile = tempDir.getChildFile ("take.wav");

        const auto writeFixture = [] (const juce::File& file, int frames, float value)
        {
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            if (stream == nullptr)
                return false;
            juce::WavAudioFormat format;
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                format.createWriterFor (stream,
                    juce::AudioFormatWriterOptions()
                        .withSampleRate (48000.0)
                        .withNumChannels (1)
                        .withBitsPerSample (16)));
            if (writer == nullptr)
                return false;
            juce::AudioBuffer<float> buffer (1, frames);
            for (int i = 0; i < frames; ++i)
                buffer.setSample (0, i, value);
            return writer->writeFromAudioSampleBuffer (buffer, 0, frames);
        };

        expect (writeFixture (beatFile, 128, 0.25f));
        expect (writeFixture (takeFile, 256, -0.25f));
        importedBeat->setSourceFile (beatFile);
        recordedTake->setSourceFile (takeFile);

        DAW::AudioFileManager audioFiles;
        expect (audioFiles.loadForClip (importedBeat->getID(), beatFile, false).success);
        expect (audioFiles.loadForClip (recordedTake->getID(), takeFile, false).success);
        expectEquals (audioFiles.getSourceNumSamples (importedBeat->getID()),
                      (DAW::SamplePosition) 128);
        expectEquals (audioFiles.getSourceNumSamples (recordedTake->getID()),
                      (DAW::SamplePosition) 256,
                      "recorded audio must not replace the beat cache entry");

        DAW::ClipManager roundTrip;
        roundTrip.restoreState (clips.getState());
        auto* roundTripBeat = dynamic_cast<DAW::AudioClip*> (roundTrip.getClip (importedBeat->getID()));
        auto* roundTripTake = dynamic_cast<DAW::AudioClip*> (roundTrip.getClip (recordedTake->getID()));
        expect (roundTripBeat != nullptr && roundTripTake != nullptr,
                "both distinct IDs must survive save-state restoration");
        if (roundTripBeat != nullptr)
            expectEquals (roundTripBeat->getSourceFile().getFullPathName(),
                          beatFile.getFullPathName());
        if (roundTripTake != nullptr)
            expectEquals (roundTripTake->getSourceFile().getFullPathName(),
                          takeFile.getFullPathName());

        DAW::RecordAudioTakeCommand::Take commandTake;
        commandTake.state = recordedTake->getState();
        commandTake.sourceFile = recordedTake->getSourceFile();
        commandTake.currentId = recordedTake->getID();

        const auto recordedId = recordedTake->getID();
        DAW::RecordAudioTakeCommand command (clips, &audioFiles, { commandTake });
        command.execute();
        command.undo();

        expect (clips.getClip (recordedId) == nullptr,
                "undo must remove only the recorded take");
        auto* survivingBeat = clips.getClip ("CLIP_1");
        expect (survivingBeat != nullptr, "undo must preserve the imported beat");
        if (survivingBeat != nullptr)
            expectEquals (survivingBeat->getTrackID(), juce::String ("TRK_1"));

        beginTest ("duplicate persisted IDs are diagnosed before publication");

        DAW::ClipManager duplicateRestore;
        juce::ValueTree duplicateRoot ("Clips");
        auto firstState = beatState.createCopy();
        auto secondState = takeState.createCopy();
        secondState.setProperty ("id", "CLIP_DUPLICATE", nullptr);
        firstState.setProperty ("id", "CLIP_DUPLICATE", nullptr);
        duplicateRoot.appendChild (firstState, nullptr);
        duplicateRoot.appendChild (secondState, nullptr);
        duplicateRestore.restoreState (duplicateRoot);

        expectEquals (duplicateRestore.getAllClips().size(), 1,
                      "a duplicate persisted ID must not reach arrangement/cache publication");
        auto* retained = duplicateRestore.getClip ("CLIP_DUPLICATE");
        expect (retained != nullptr);
        if (retained != nullptr)
            expectEquals (retained->getTrackID(), juce::String ("TRK_1"),
                          "the first valid persisted owner is retained");

        tempDir.deleteRecursively();
    }
};

static RecordingIdentityRegressionTests recordingIdentityRegressionTests;
```

- [ ] **Step 2: Register the test without regenerating away unrelated project changes**

Add this entry beneath `RecordingWriterIntegrityTests.cpp` in `Tests/APEXTests.jucer`:

```xml
<FILE id="RC_CPP2" name="RecordingIdentityRegressionTests.cpp" compile="1" resource="0"
      file="Source/Recording/RecordingIdentityRegressionTests.cpp"/>
```

Merge this entry beside the recording test entry in `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj`:

```xml
<ClCompile Include="..\..\Source\Recording\RecordingIdentityRegressionTests.cpp"/>
```

- [ ] **Step 3: Build and run the focused test to prove RED**

Run from `My DAW/DAW_Core`:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.reload-clip-identity.v1" -Seed 0xA9E12026
```

Expected: exit code `1`; failure states that the recorded take reused `CLIP_1`, IDs are not unique, or undo did not preserve the Track 1 beat.

- [ ] **Step 4: Inspect the focused diff checkpoint**

Run:

```powershell
git diff --check -- Tests/Source/Recording/RecordingIdentityRegressionTests.cpp Tests/APEXTests.jucer Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj
```

Expected: no whitespace errors; only the new test and compile registrations appear. Do not commit.

---

### Task 2: Make newly created clip identities collision-resistant

**Files:**
- Modify: `Source/UtilityCore/Types.h:15-30`
- Modify: `Source/UtilityCore/Types.cpp:5-19`
- Modify: `Source/ClipCore/Clip.h:377-441`
- Modify: `Source/ClipCore/Clip.cpp:275-345,469-509`
- Test: `Tests/Source/Recording/RecordingIdentityRegressionTests.cpp`

**Interfaces:**
- Consumes: `DAW::IDGenerator::generateClipID()`.
- Produces: private `DAW::ClipManager::generateUniqueClipIDUnlocked() const`; fresh IDs formatted `CLIP_<UUID>`.

- [ ] **Step 1: Change only fresh clip ID generation to UUID format**

Remove `clipCounter` from `IDGenerator`'s private members in `Types.h`. Keep all other counters unchanged.

Replace the clip counter definition and `generateClipID()` body in `Types.cpp` with:

```cpp
ClipID IDGenerator::generateClipID()
{
    return "CLIP_" + juce::Uuid().toString();
}
```

Do not change track, plugin, route, or sidechain generation.

- [ ] **Step 2: Add a manager-owned uniqueness helper**

Add to the private section of `ClipManager` in `Clip.h`:

```cpp
ClipID generateUniqueClipIDUnlocked() const;
```

Add before `createAudioClip()` in `Clip.cpp`:

```cpp
ClipID ClipManager::generateUniqueClipIDUnlocked() const
{
    for (;;)
    {
        const auto candidate = IDGenerator::generateClipID();
        bool alreadyExists = false;
        for (auto* clip : clips_)
        {
            if (clip != nullptr && clip->getID() == candidate)
            {
                alreadyExists = true;
                break;
            }
        }

        if (!alreadyExists)
            return candidate;
    }
}
```

This helper is called only while `clipLock_` is held.

- [ ] **Step 3: Generate and insert each new clip under one manager lock**

Update `createAudioClip`, `recreateClipFromState`, `createMIDIClip`, and `createPatternClip` so each method:

1. Acquires `clipLock_`.
2. Calls `generateUniqueClipIDUnlocked()`.
3. Constructs/restores the clip.
4. Adds it to `clips_`.
5. Releases the lock before notifying listeners.

Use this shape for `createAudioClip`:

```cpp
AudioClip* ClipManager::createAudioClip(const juce::String& name, const juce::File& audioFile)
{
    AudioClip* clip = nullptr;
    {
        juce::ScopedLock sl (clipLock_);
        clip = new AudioClip (generateUniqueClipIDUnlocked(), name);
        clip->setTimePitchMode (TimePitchModeIds::DefaultUserMode);
        clip->setSourceFile (audioFile);
        clips_.add (clip);
    }
    listeners_.call ([clip](Listener& l) { l.clipAdded (clip); });
    return clip;
}
```

Apply the same lock/generate/add/notify order to every fresh clip subtype. `recreateClipFromState` must continue ignoring a persisted `id` because it is the duplicate/redo/record-finalization path, not project restoration.

- [ ] **Step 4: Diagnose duplicate persisted IDs before publication**

In `ClipManager::restoreState`, maintain a `juce::StringArray restoredIds`. Before constructing each clip, add this guard:

```cpp
if (id.isEmpty() || restoredIds.contains (id))
{
    juce::Logger::writeToLog (
        "[PROJECT] duplicate or empty persisted clip ID rejected: " + id);
    continue;
}
restoredIds.add (id);
```

Keep valid unique persisted IDs unchanged. Continue notifying listeners only for clips actually restored.

- [ ] **Step 5: Run the focused identity regression to prove GREEN**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.reload-clip-identity.v1" -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Name "recording.reload-clip-identity.v1" -Seed 0xA9E12026
```

Expected: both commands exit `0`; the imported beat survives and the recorded take has a distinct `CLIP_<UUID>` ID.

- [ ] **Step 6: Review the identity-only checkpoint**

Run:

```powershell
git diff --check -- Source/UtilityCore/Types.h Source/UtilityCore/Types.cpp Source/ClipCore/Clip.h Source/ClipCore/Clip.cpp Tests/Source/Recording/RecordingIdentityRegressionTests.cpp
```

Expected: no changes to non-clip identity generators and no unrelated clip editing refactor. Do not commit.

---

### Task 3: Add the failing low-buffer live-waveform regression

**Files:**
- Create: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`
- Modify: `Tests/APEXTests.jucer:28-33`
- Modify: `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj:148-176`
- Test: `Source/RecordingCore/LiveRecordWaveformCore.h`

**Interfaces:**
- Consumes: `DAW::LiveRecordWaveformCore::reset`, `pushPeak`, and `draw`.
- Produces: Test name `recording.live-waveform-low-buffer.v1` in category `APEX.Recording`.

- [ ] **Step 1: Add an image-based waveform visibility regression**

Create `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`:

```cpp
#include <JuceHeader.h>
#include "../../../Source/RecordingCore/LiveRecordWaveformCore.h"

class LiveRecordWaveformRegressionTests final : public juce::UnitTest
{
public:
    LiveRecordWaveformRegressionTests()
        : juce::UnitTest ("recording.live-waveform-low-buffer.v1", "APEX.Recording") {}

    void runTest() override
    {
        beginTest ("valid low-buffer peaks remain visible at supported zooms");

        constexpr double sampleRate = 48000.0;
        const int blockSizes[] = { 64, 128, 512 };
        const double pixelsPerSecond[] = { 5.0, 100.0, 800.0 };

        for (const int blockSize : blockSizes)
        {
            for (const double pps : pixelsPerSecond)
            {
                DAW::LiveRecordWaveformCore waveform;
                waveform.reset (blockSize);
                for (int i = 0; i < 512; ++i)
                    waveform.pushPeak (-0.75f, 0.75f);

                juce::Image image (juce::Image::ARGB, 512, 80, true);
                juce::Graphics graphics (image);
                waveform.draw (graphics,
                               image.getBounds().toFloat(),
                               juce::Colours::white,
                               pps / sampleRate,
                               512.0 * blockSize);

                bool foundPaintedPixel = false;
                for (int y = 0; y < image.getHeight() && !foundPaintedPixel; ++y)
                    for (int x = 0; x < image.getWidth(); ++x)
                        if (image.getPixelAt (x, y).getAlpha() != 0)
                        {
                            foundPaintedPixel = true;
                            break;
                        }

                expect (foundPaintedPixel,
                        "waveform missing for block=" + juce::String (blockSize)
                        + " pps=" + juce::String (pps));
            }
        }
    }
};

static LiveRecordWaveformRegressionTests liveRecordWaveformRegressionTests;
```

- [ ] **Step 2: Register the waveform test in both project representations**

Add to the Recording group in `Tests/APEXTests.jucer`:

```xml
<FILE id="RC_CPP3" name="LiveRecordWaveformRegressionTests.cpp" compile="1" resource="0"
      file="Source/Recording/LiveRecordWaveformRegressionTests.cpp"/>
```

Add beside the other recording test compile entries in the generated `.vcxproj`:

```xml
<ClCompile Include="..\..\Source\Recording\LiveRecordWaveformRegressionTests.cpp"/>
```

- [ ] **Step 3: Run the focused waveform test to prove RED**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
```

Expected: exit code `1`; at minimum, 48 kHz / 64 frames at 100 pixels per second and low-zoom cases contain no painted pixels because `pxPerPeak < 0.25` returns early.

---

### Task 4: Aggregate live peaks by display pixel

**Files:**
- Modify: `Source/RecordingCore/LiveRecordWaveformCore.h:100-149`
- Test: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Consumes: existing fixed-capacity atomic min/max ring.
- Produces: bounded UI-thread rendering for both `pxPerPeak >= 1.0` and `pxPerPeak < 1.0`.

- [ ] **Step 1: Replace the all-or-nothing density cutoff with pixel aggregation**

Keep the current direct per-peak drawing branch when `pxPerPeak >= 1.0`. Replace the `pxPerPeak < 0.25` return with a second branch equivalent to:

```cpp
if (pxPerPeak < 1.0)
{
    const int pixelColumns = juce::jmin (
        juce::jmax (1, (int) std::ceil ((double) total * pxPerPeak)),
        juce::jmax (1, (int) std::ceil ((double) region.getWidth())));

    for (int column = 0; column < pixelColumns; ++column)
    {
        const int firstPeak = juce::jlimit (
            0, total - 1, (int) std::floor ((double) column / pxPerPeak));
        const int endPeak = juce::jlimit (
            firstPeak + 1, total,
            (int) std::ceil ((double) (column + 1) / pxPerPeak));

        float aggregateMin = 1.0f;
        float aggregateMax = -1.0f;
        bool foundValidPeak = false;

        for (int i = firstPeak; i < endPeak; ++i)
        {
            const int src = (startIdx + i) % kCapacity;
            const float mn = mins_[src].load (std::memory_order_relaxed);
            const float mx = maxs_[src].load (std::memory_order_relaxed);
            if (!std::isfinite (mn) || !std::isfinite (mx)
                || mn < -1.0f || mx > 1.0f || mn > mx)
                continue;

            aggregateMin = juce::jmin (aggregateMin, mn);
            aggregateMax = juce::jmax (aggregateMax, mx);
            foundValidPeak = true;
        }

        if (!foundValidPeak)
            continue;

        const float px = regionX + (float) column;
        const float barTop = cy - aggregateMax * halfH;
        const float barBottom = cy - aggregateMin * halfH;
        g.fillRect (px, barTop, 1.0f,
                    juce::jmax (1.0f, barBottom - barTop));
    }
}
else
{
    // Existing one-bar-per-peak loop, unchanged except for formatting.
}
```

Keep clipping, validation, and graphics state restoration around both branches. Do not snapshot into vectors or allocate during paint.

- [ ] **Step 2: Run Debug and Release waveform tests to prove GREEN**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
```

Expected: both exit `0`; every block-size/zoom case paints bounded waveform pixels.

---

### Task 5: Add failing valid-input and no-fake-waveform contracts

**Files:**
- Create: `Source/RecordingCore/RecordingInputValidityCore.h`
- Modify: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Produces:
  - `DAW::RecordingInputValidityCore::clampCallbackChannels(int callbackChannels, int preparedChannels) noexcept -> int`
  - `DAW::RecordingInputValidityCore::isRouteAvailable(int firstChannel, bool mono, int validChannels) noexcept -> bool`
  - `LiveRecordWaveformCore::setInputAvailable(bool) noexcept`
  - `LiveRecordWaveformCore::isInputAvailable() const noexcept -> bool`

- [ ] **Step 1: Add the pure input-validity helper**

Create `Source/RecordingCore/RecordingInputValidityCore.h`:

```cpp
#pragma once
#include <JuceHeader.h>

namespace DAW {

struct RecordingInputValidityCore
{
    static int clampCallbackChannels (int callbackChannels,
                                      int preparedChannels) noexcept
    {
        return juce::jlimit (0, juce::jmax (0, preparedChannels),
                            juce::jmax (0, callbackChannels));
    }

    static bool isRouteAvailable (int firstChannel,
                                  bool mono,
                                  int validChannels) noexcept
    {
        if (firstChannel < 0 || validChannels <= 0 || firstChannel >= validChannels)
            return false;
        return mono || (firstChannel + 1 < validChannels);
    }
};

} // namespace DAW
```

- [ ] **Step 2: Extend the waveform test with input-validity and stale-peak suppression assertions**

Add the new helper include and these tests inside `runTest()`:

```cpp
beginTest ("prepared capacity is not valid hardware input");
expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (0, 8), 0);
expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (2, 8), 2);
expectEquals (DAW::RecordingInputValidityCore::clampCallbackChannels (12, 8), 8);

beginTest ("mono and stereo routes require their opened channels");
expect (!DAW::RecordingInputValidityCore::isRouteAvailable (0, true, 0));
expect (DAW::RecordingInputValidityCore::isRouteAvailable (0, true, 1));
expect (!DAW::RecordingInputValidityCore::isRouteAvailable (0, false, 1));
expect (DAW::RecordingInputValidityCore::isRouteAvailable (0, false, 2));
expect (!DAW::RecordingInputValidityCore::isRouteAvailable (2, true, 2));

beginTest ("unavailable input draws no stale waveform");
DAW::LiveRecordWaveformCore unavailable;
unavailable.reset (64);
unavailable.setInputAvailable (true);
unavailable.pushPeak (-0.8f, 0.8f);
unavailable.setInputAvailable (false);
expect (!unavailable.isInputAvailable());

juce::Image noInputImage (juce::Image::ARGB, 64, 40, true);
juce::Graphics noInputGraphics (noInputImage);
unavailable.draw (noInputGraphics, noInputImage.getBounds().toFloat(),
                  juce::Colours::white, 100.0 / 48000.0, 64.0);

bool noInputPainted = false;
for (int y = 0; y < noInputImage.getHeight() && !noInputPainted; ++y)
    for (int x = 0; x < noInputImage.getWidth(); ++x)
        if (noInputImage.getPixelAt (x, y).getAlpha() != 0)
        {
            noInputPainted = true;
            break;
        }
expect (!noInputPainted, "unavailable input must not draw a fake or stale waveform");
```

- [ ] **Step 3: Run the focused test to prove RED**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
```

Expected: compile failure because `setInputAvailable` and `isInputAvailable` do not yet exist. The pure helper assertions compile independently once the interface is added in Task 6.

---

### Task 6: Propagate actual input validity and record truthful silence

**Files:**
- Modify: `Source/RecordingCore/LiveRecordWaveformCore.h:39-75,100-115,151-157`
- Modify: `Source/RecordingCore/RecordingEngine.h:93-97,241-254,430-489,671-680`
- Modify: `Source/MainComponent.cpp:5983-5988,6010-6065`
- Modify: `Source/AppCore/ApplicationCore.cpp:841-865,920-945`
- Modify: `Source/UICore/ArrangementView.h:533-590`
- Test: `Tests/Source/Recording/LiveRecordWaveformRegressionTests.cpp`

**Interfaces:**
- Consumes: `RecordingInputValidityCore` from Task 5.
- Produces: truthful per-track `LiveRecordWaveformCore` availability and `RecordingEngine::processBlock(const juce::AudioBuffer<float>&, int, int)`.

- [ ] **Step 1: Add atomic input state to the live waveform model**

In `LiveRecordWaveformCore`:

```cpp
void reset (int samplesPerBlock) noexcept
{
    samplesPerPeak_.store (juce::jmax (1, samplesPerBlock), std::memory_order_relaxed);
    inputAvailable_.store (false, std::memory_order_release);
    totalPeaks_.store (0, std::memory_order_release);
    writeHead_.store (0, std::memory_order_release);
}

void setInputAvailable (bool available) noexcept
{
    inputAvailable_.store (available, std::memory_order_release);
}

bool isInputAvailable() const noexcept
{
    return inputAvailable_.load (std::memory_order_acquire);
}
```

Add:

```cpp
std::atomic<bool> inputAvailable_ { false };
```

At the start of `draw`, after validating region size, return when `!isInputAvailable()`. `pushPeak` remains allocation-free and does not infer availability from amplitude.

In the valid-input matrix at the start of `LiveRecordWaveformRegressionTests.cpp`, add this immediately after each `reset` call so the original visibility test explicitly represents a valid route:

```cpp
waveform.setInputAvailable (true);
```

- [ ] **Step 2: Publish actual callback-valid channels before processing**

Include `RecordingInputValidityCore.h` where required. In `MainComponent::audioDeviceIOCallbackWithContext`, compute and store validity before either branch calls `getNextAudioBlock`:

```cpp
const int validInputChannels = DAW::RecordingInputValidityCore::clampCallbackChannels (
    numInputChannels, liveCallbackInputBuffer_.getNumChannels());
cachedEnabledAudioInputChannels_.store (validInputChannels,
                                        std::memory_order_release);
```

Use `validInputChannels` as `safeChannels`. Remove the old post-processing store at the end of the callback. Preallocated capacity remains unchanged.

- [ ] **Step 3: Preserve only actual valid channels in ApplicationCore**

Change the input-buffer branch to require `hardwareInputChannelCount_ > 0`, and calculate:

```cpp
inputChannels = juce::jmin (hardwareInputChannelCount_,
                            hardwareInputBuffer->getNumChannels());
```

When no valid input exists, call `audioEngine_.clearLiveInputBuffer()` and leave `hasFreshHardwareInput` false. Keep the existing aligned-silence fallback and pass the actual validity to the recorder:

```cpp
recordingEngine_.processBlock (preservedHardwareInput_,
                               recordedSamples,
                               hasFreshHardwareInput ? inputChannels : 0);
```

- [ ] **Step 4: Resolve each track route from actual valid channels**

Change `RecordingEngine::processBlock` to:

```cpp
void processBlock (const juce::AudioBuffer<float>& inputBuffer,
                   int numSamples,
                   int validInputChannels)
```

Make `numHardwareInputs_` an `std::atomic<int>` and update its setter/load accordingly. Clamp the effective count to the buffer and current opened count:

```cpp
const int validChannels = juce::jmin (
    inputBuffer.getNumChannels(),
    juce::jmin (juce::jmax (0, validInputChannels),
                numHardwareInputs_.load (std::memory_order_acquire)));
```

For each active track, resolve:

```cpp
const bool inputAvailable = RecordingInputValidityCore::isRouteAvailable (
    rec->firstInputCh, rec->monoInput, validChannels);

if (tracks_ != nullptr)
    if (auto* track = tracks_->getTrack (rec->trackID))
        track->getLiveRecordWaveform().setInputAvailable (inputAvailable);
```

Keep `writableSamples` bounded by `printBuffer_`. For valid input, use the selected pointers. For unavailable input, clear both prepared scratch channels and use them as the writer source:

```cpp
const float* chans[2] = { nullptr, nullptr };
if (inputAvailable)
{
    chans[0] = inputBuffer.getReadPointer (rec->firstInputCh);
    chans[1] = rec->monoInput
        ? chans[0]
        : inputBuffer.getReadPointer (rec->firstInputCh + 1);
}
else
{
    printBuffer_.clear (0, 0, writableSamples);
    printBuffer_.clear (1, 0, writableSamples);
    chans[0] = printBuffer_.getReadPointer (0);
    chans[1] = printBuffer_.getReadPointer (1);
}

rec->writer.pushSamples (chans, writableSamples);
```

Calculate and publish a peak only when `inputAvailable` is true. Never publish a zero-valued fake peak for unavailable input.

- [ ] **Step 5: Display engine-owned recording input status**

In `ArrangementView::paintOverChildren`, query:

```cpp
const bool inputAvailable = track->getLiveRecordWaveform().isInputAvailable();
```

Call `draw` only when true. Replace the fixed header label with:

```cpp
const auto recordingLabel = inputAvailable
    ? juce::String ("Recording...")
    : juce::String ("No input — recording silence");
g.drawText (recordingLabel, header.reduced (6.0f, 0.0f).toNearestInt(),
            juce::Justification::centredLeft, true);
```

Do not infer this status from peak amplitude or meter level.

- [ ] **Step 6: Run focused Debug and Release tests to prove GREEN**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Name "recording.live-waveform-low-buffer.v1" -Seed 0xA9E12026
```

Expected: both exit `0`; low-buffer peaks paint, zero/invalid routes remain unavailable, and unavailable state paints no waveform.

- [ ] **Step 7: Check realtime and scope invariants**

Inspect the callback diff and verify:

- No new `new`, `delete`, `setSize`, vector resizing, locks, waits, file calls, logger calls, or GUI calls occur in the callback path.
- Silence uses `printBuffer_`, which is prepared in `RecordingEngine::prepare`.
- Formatted warning text is constructed only in `ArrangementView::paintOverChildren` on the UI thread.
- Monitoring and routing code is unchanged.

Run:

```powershell
git diff --check -- Source/RecordingCore/RecordingInputValidityCore.h Source/RecordingCore/LiveRecordWaveformCore.h Source/RecordingCore/RecordingEngine.h Source/MainComponent.cpp Source/AppCore/ApplicationCore.cpp Source/UICore/ArrangementView.h
```

Expected: no whitespace errors and no unrelated subsystem changes. Do not commit.

---

### Task 7: Run the complete APEX verification gate

**Files:**
- Verify only; do not create source changes to make unrelated failures disappear.

**Interfaces:**
- Consumes: all implementation tasks.
- Produces: observed build/test/evidence results and remaining Apollo hardware uncertainty.

- [ ] **Step 1: Run repository and dependency policy gates**

Run from `My DAW/DAW_Core`:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1
```

Expected: each command exits `0`.

- [ ] **Step 2: Rebuild unsigned Debug and Release applications**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
```

Expected: both commands exit `0`; Debug and Release application binaries exist.

- [ ] **Step 3: Run complete Debug and Release test suites**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

Expected: both commands exit `0`; both test binaries exist and evidence manifests contain zero failed assertions.

- [ ] **Step 4: Inspect final status and diff without touching unrelated work**

Run:

```powershell
git status --short
```

Expected: no whitespace errors. Separate pre-existing modifications from files changed by this plan in the report. Do not stage or commit.

- [ ] **Step 5: Report behavioral status precisely**

Report:

- Exact commands and exit codes.
- Focused assertion results in Debug and Release.
- Full-suite assertion totals and evidence paths.
- Application and test binary paths.
- Any warnings or unrelated failures.
- Apollo hardware behavior as `Unknown pending hardware validation` unless an actual Apollo run is observed with sample rate, block size, active channel mask, selected track input, callback count, writer count, peak count, and UI result.

Do not claim the Apollo issue fully fixed from compilation or simulated image tests alone.

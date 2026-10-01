# Sample-Rate Set + 32-Sample Buffer Expansion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add device-authoritative professional sample rates (32000–192000) and a 32-sample EXPERIMENTAL_ULTRA_LOW_LATENCY buffer option, fix all proven hardcoded-rate defects, and correct the B4 audit ring drain — without regressing the 256-sample baseline, 64-sample behavior, realtime safety, or rollback behavior.

**Architecture:** Device/driver is the authority for selectable rates/buffers (Brain §3: "the result of negotiation is authoritative"); the engine continues to prepare from the *actual* granted configuration via the existing stop/start → `applyAudioDevicePreparation` path. All DSP timing derives from the active sample rate. 32-sample buffers are only ever offered when the device enumerates them. No commits — working tree only.

**Tech Stack:** C++20, JUCE 8.0.12 (vendored), MSBuild via `Scripts\build_apex.ps1`, JUCE UnitTest console runner via `Scripts\run_apex_tests.ps1`.

**Spec:** `docs/superpowers/specs/2026-07-25-sample-rate-buffer-expansion-design.md`

## Global Constraints

- **NO GIT COMMITS.** All changes stay in the working tree (explicit user directive, 2026-07-25). Every "checkpoint" step means: verify, then move on — never commit.
- All commands run from `My DAW/DAW_Core` unless stated otherwise.
- Proven gates (must ALL exit 0 at completion): `Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`; same for `Release`; `Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`; same for `Release`; plus `Scripts\test_repository_policy.ps1`, `Scripts\verify_dependencies.ps1`, `Scripts\test_validate_test_evidence.ps1`.
- Realtime rules (Brain §2): no allocation, locks, I/O, or plugin lifecycle work on the audio thread. New code in this plan touches RT paths only in `DrumSamplerVoice::process` (must stay allocation/lock-free) and the callback-adjacent audit ring (already lock-free SPSC).
- 32 samples is `EXPERIMENTAL_ULTRA_LOW_LATENCY` — UI label `32 (experimental)`, never forced, only shown when the device enumerates it. WASAPI shared will not expose it (accepted).
- Professional rate set: `{32000, 44100, 48000, 88200, 96000, 176400, 192000}`. Buffer ladder: `{32, 64, 128, 256, 512, 1024, 2048}`.
- Do not redesign: engine, routing, monitoring, PDC, export, or the device reconfiguration flow (it already satisfies work-order §4).
- Test conventions: suites are `juce::UnitTest` subclasses with a `static` instance at file scope, named `area.topic.v1` in category `APEX.*`; new `.cpp` files must be added to `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj` as `<ClCompile Include="..\..\Source\<Area>\<File>.cpp"/>` (paths are relative to the vcxproj). Production `.cpp` dependencies are added the same way (precedent: `..\..\..\Source\StepSequencerCore\StepSequencerModel.cpp`).
- Paths below are relative to `My DAW/DAW_Core` unless absolute.

---

### Task 1: Device capability enumeration core (DevicePanelModelCore) + pure-helper tests

**Files:**
- Modify: `Source/DeviceCore/DevicePanelModelCore.h` (replace hardcoded lists at :94-102; add enumeration + caches)
- Create: `Tests/Source/Device/DeviceCapabilityTests.cpp`
- Modify: `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj` (add test file)

**Interfaces:**
- Consumes: `juce::AudioDeviceManager`, existing `findType`/`ensureScanned`/`isAsioType` privates in `DevicePanelModelCore`.
- Produces (used by Tasks 2 and tests):
  - `static juce::Array<double> DevicePanelModelCore::professionalSampleRates()` → `{32000,44100,48000,88200,96000,176400,192000}`
  - `static juce::Array<int> DevicePanelModelCore::supportedBufferSizeLadder()` → `{32,64,128,256,512,1024,2048}`
  - `static juce::Array<double> DevicePanelModelCore::intersectProfessionalRates(const juce::Array<double>& deviceRates)` — ascending, deduped, `deviceRates ∩ professional`
  - `static juce::Array<int> DevicePanelModelCore::filterSupportedBufferSizes(const juce::Array<int>& deviceSizes)` — ascending, deduped, `deviceSizes ∩ ladder`
  - `static bool DevicePanelModelCore::isExperimentalUltraLowLatency(int bufferSize) noexcept` — `bufferSize == 32`
  - `static juce::String DevicePanelModelCore::formatBufferSizeLabel(int bufferSize)` — `"32 (experimental)"` for 32, else decimal string
  - `juce::Array<double> getAvailableSampleRates(const juce::String& typeName, const juce::String& outputDeviceName) const`
  - `juce::Array<int> getAvailableBufferSizes(const juce::String& typeName, const juce::String& outputDeviceName) const`

- [ ] **Step 1: Write the failing test**

Create `Tests/Source/Device/DeviceCapabilityTests.cpp`:

```cpp
#include <JuceHeader.h>
#include "../../../Source/DeviceCore/DevicePanelModelCore.h"

class DeviceCapabilityTests final : public juce::UnitTest
{
public:
    DeviceCapabilityTests() : juce::UnitTest ("device.capabilities.v1", "APEX.Device") {}

    void runTest() override
    {
        beginTest ("professional rate set is exact and ascending");
        {
            const auto rates = DAW::DevicePanelModelCore::professionalSampleRates();
            const double expected[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            expectEquals (rates.size(), 7);
            for (int i = 0; i < rates.size() && i < 7; ++i)
                expectWithinAbsoluteError (rates[i], expected[i], 0.001);
        }

        beginTest ("buffer ladder is exact and ascending");
        {
            const auto sizes = DAW::DevicePanelModelCore::supportedBufferSizeLadder();
            const int expected[] = { 32, 64, 128, 256, 512, 1024, 2048 };
            expectEquals (sizes.size(), 7);
            for (int i = 0; i < sizes.size() && i < 7; ++i)
                expectEquals (sizes[i], expected[i]);
        }

        beginTest ("rate intersection keeps only device-supported professional rates");
        {
            juce::Array<double> deviceRates { 96000.0, 44100.0, 22050.0, 192000.0, 44100.0, 48000.5 };
            const auto out = DAW::DevicePanelModelCore::intersectProfessionalRates (deviceRates);
            // 48000.5 is NOT 48000; 22050 is not professional; duplicate 44100 deduped.
            expectEquals (out.size(), 3);
            expectWithinAbsoluteError (out[0], 44100.0, 0.001);
            expectWithinAbsoluteError (out[1], 96000.0, 0.001);
            expectWithinAbsoluteError (out[2], 192000.0, 0.001);

            expect (DAW::DevicePanelModelCore::intersectProfessionalRates ({}).isEmpty());
            expect (DAW::DevicePanelModelCore::intersectProfessionalRates ({ 12345.0 }).isEmpty());
        }

        beginTest ("buffer intersection keeps only device-supported ladder sizes incl. 32");
        {
            juce::Array<int> deviceSizes { 4096, 16, 32, 96, 64, 128, 2048, 64 };
            const auto out = DAW::DevicePanelModelCore::filterSupportedBufferSizes (deviceSizes);
            expectEquals (out.size(), 5);
            expectEquals (out[0], 32);
            expectEquals (out[1], 64);
            expectEquals (out[2], 128);
            expectEquals (out[3], 2048);
            expectEquals (out[4], 4096 - 4096 + 32 - 32 + 2048); // guard: last is 2048
            expectEquals (out.getLast(), 2048);

            expect (DAW::DevicePanelModelCore::filterSupportedBufferSizes ({}).isEmpty());
            expect (DAW::DevicePanelModelCore::filterSupportedBufferSizes ({ 24, 4096 }).isEmpty());
        }

        beginTest ("32 is the only experimental ultra-low-latency size and is labeled");
        {
            expect (DAW::DevicePanelModelCore::isExperimentalUltraLowLatency (32));
            expect (! DAW::DevicePanelModelCore::isExperimentalUltraLowLatency (64));
            expect (! DAW::DevicePanelModelCore::isExperimentalUltraLowLatency (0));
            expectEquals (DAW::DevicePanelModelCore::formatBufferSizeLabel (32),
                          juce::String ("32 (experimental)"));
            expectEquals (DAW::DevicePanelModelCore::formatBufferSizeLabel (256),
                          juce::String ("256"));
        }

        beginTest ("32 label parses back to 32 for the commit path");
        {
            // AudioDevicePanelUI parses combo text with getIntValue(); the
            // label must keep the leading integer intact.
            expectEquals (DAW::DevicePanelModelCore::formatBufferSizeLabel (32).getIntValue(), 32);
            expectEquals (DAW::DevicePanelModelCore::formatBufferSizeLabel (64).getIntValue(), 64);
        }
    }
};

static DeviceCapabilityTests deviceCapabilityTests;
```

Add to `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj`, inside the `<ItemGroup>` with the other test files (after the `PdcClickRtTests.cpp` entry):

```xml
    <ClCompile Include="..\..\Source\Device\DeviceCapabilityTests.cpp"/>
```

- [ ] **Step 2: Run test to verify it fails**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026 -- --name device.capabilities.v1` (pass-through args after `--` if the script supports them; otherwise run the full suite).
Expected: build FAILURE — `professionalSampleRates` / `intersectProfessionalRates` etc. are not members of `DevicePanelModelCore`.

- [ ] **Step 3: Implement the helpers + device enumeration**

In `Source/DeviceCore/DevicePanelModelCore.h`, replace `getCommonSampleRates()` (:94-97) and `getCommonBufferSizes()` (:99-102) with:

```cpp
    // ── Device-authoritative capability enumeration (Brain §3: the result
    //    of negotiation is authoritative; UI must never offer what the
    //    device did not report) ─────────────────────────────────────────

    static juce::Array<double> professionalSampleRates()
    {
        return { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    }

    static juce::Array<int> supportedBufferSizeLadder()
    {
        return { 32, 64, 128, 256, 512, 1024, 2048 };
    }

    static juce::Array<double> intersectProfessionalRates(const juce::Array<double>& deviceRates)
    {
        juce::Array<double> out;
        for (const double professional : professionalSampleRates())
            for (const double deviceRate : deviceRates)
                if (std::abs (deviceRate - professional) < 1.0 && ! out.contains (professional))
                    out.add (professional);
        return out; // ascending by professional-set order
    }

    static juce::Array<int> filterSupportedBufferSizes(const juce::Array<int>& deviceSizes)
    {
        juce::Array<int> out;
        for (const int ladder : supportedBufferSizeLadder())
            if (deviceSizes.contains (ladder) && ! out.contains (ladder))
                out.add (ladder);
        return out;
    }

    static bool isExperimentalUltraLowLatency(int bufferSize) noexcept
    {
        return bufferSize == 32; // EXPERIMENTAL_ULTRA_LOW_LATENCY until Apollo Solo hardware validation
    }

    static juce::String formatBufferSizeLabel(int bufferSize)
    {
        return isExperimentalUltraLowLatency (bufferSize)
            ? juce::String (bufferSize) + " (experimental)"
            : juce::String (bufferSize);
    }

    /** Sample rates the named device actually reports, intersected with the
        professional set. Fast path reads the currently open device; otherwise
        probes once via a temporary device (cached per panel session). */
    juce::Array<double> getAvailableSampleRates(const juce::String& typeName,
                                                const juce::String& outputDeviceName) const
    {
        probeRateAndBufferCaches (typeName, outputDeviceName);
        if (auto it = sampleRateCache_.find (typeName + "||" + outputDeviceName); it != sampleRateCache_.end())
            return it->second;
        return {};
    }

    /** Buffer sizes the named device actually reports, intersected with the
        supported ladder (32 only when the driver reports it). */
    juce::Array<int> getAvailableBufferSizes(const juce::String& typeName,
                                             const juce::String& outputDeviceName) const
    {
        probeRateAndBufferCaches (typeName, outputDeviceName);
        if (auto it = bufferSizeCache_.find (typeName + "||" + outputDeviceName); it != bufferSizeCache_.end())
            return it->second;
        return {};
    }
```

In `invalidateDeviceCache()` (:39-45), add the two new caches:

```cpp
    void invalidateDeviceCache()
    {
        outputListCache_.clear();
        inputListCache_.clear();
        channelChoicesCache_.clear();
        sampleRateCache_.clear();
        bufferSizeCache_.clear();
        probedCapabilityKeys_.clear();
        scannedTypes_.clear();
    }
```

Add the private probe + caches (next to `ensureScanned`):

```cpp
    /** One probe per type+device per panel session fills BOTH caches.
        Mirrors the getOutputChannelChoices fast-path/temp-device pattern:
        never blocks more than once per device per panel open. */
    void probeRateAndBufferCaches(const juce::String& typeName, const juce::String& outputDeviceName) const
    {
        const auto cacheKey = typeName + "||" + outputDeviceName;
        if (probedCapabilityKeys_.count (cacheKey) > 0)
            return;
        probedCapabilityKeys_.insert (cacheKey);

        juce::Array<double> rates;
        juce::Array<int> sizes;

        auto readCaps = [&](const juce::AudioIODevice* device)
        {
            if (device == nullptr)
                return;
            rates = device->getAvailableSampleRates();
            sizes = device->getAvailableBufferSizes();
        };

        // Fast path: the requested device is already open — read directly,
        // no temporary ASIO device, no blocking.
        if (auto* current = dm_.getCurrentAudioDevice();
            current != nullptr
                && dm_.getCurrentAudioDeviceType() == typeName
                && current->getName() == outputDeviceName)
        {
            readCaps (current);
        }
        else if (auto* type = findType (typeName))
        {
            ensureScanned (*type);
            if (std::unique_ptr<juce::AudioIODevice> temp (type->createDevice (outputDeviceName, {})))
                readCaps (temp.get());
        }

        sampleRateCache_[cacheKey]  = intersectProfessionalRates (rates);
        bufferSizeCache_[cacheKey]  = filterSupportedBufferSizes (sizes);
    }

    mutable std::map<juce::String, juce::Array<double>> sampleRateCache_;
    mutable std::map<juce::String, juce::Array<int>>    bufferSizeCache_;
    mutable std::set<juce::String> probedCapabilityKeys_;
```

(`<map>` and `<set>` are already included at :4-5.)

- [ ] **Step 4: Run test to verify it passes**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS, `device.capabilities.v1` green; zero failed assertions in the manifest.

- [ ] **Step 5: Checkpoint (NO COMMIT)**

Leave all changes in the working tree. Confirm the app still compiles later at Task 7's build (panel still references the removed `getCommon*` methods until Task 2 — that is expected; do not build the app yet).

---

### Task 2: Panel + session validation wiring (device-authoritative UI, unsupported-value rejection)

**Files:**
- Modify: `Source/UICore/AudioDevicePanelUI.h` (:81-86, :255-269, :342-361, :420-433)
- Modify: `Source/DeviceCore/DeviceSessionCore.h` (:26-30 add fields/methods; :97-100 extend validation)
- Modify: `Tests/Source/Device/DeviceCapabilityTests.cpp` (append validation suite)
- Modify: `Source/MainComponent.cpp` (:5874-5901 panel construction — no signature change needed; verify only)

**Interfaces:**
- Consumes: Task 1's enumeration + format helpers.
- Produces:
  - `void DeviceSessionCore::setSupportedConfig(juce::Array<double> rates, juce::Array<int> sizes)` — empty lists disable enforcement (advisory mode)
  - `static ValidationReport DeviceSessionCore::checkAgainstSupportedConfig(const DeviceRequest& r, const juce::Array<double>& rates, const juce::Array<int>& sizes)` — pure, unit-tested
  - Panel private: `void repopulateRateAndBufferLists()`; members `juce::Array<double> availableRates_; juce::Array<int> availableBuffers_;`

- [ ] **Step 1: Write the failing validation tests**

Append to `Tests/Source/Device/DeviceCapabilityTests.cpp` before the `static` instance:

```cpp
class DeviceConfigValidationTests final : public juce::UnitTest
{
public:
    DeviceConfigValidationTests() : juce::UnitTest ("device.config-validation.v1", "APEX.Device") {}

    void runTest() override
    {
        beginTest ("empty capability lists disable enforcement (advisory mode)");
        {
            DAW::DeviceRequest r;
            r.sampleRate = 12345.0;
            r.bufferSize = 47;
            const auto report = DAW::DeviceSessionCore::checkAgainstSupportedConfig (r, {}, {});
            expect (report.ok);
        }

        beginTest ("supported values pass");
        {
            DAW::DeviceRequest r;
            r.sampleRate = 96000.0;
            r.bufferSize = 32;
            const auto report = DAW::DeviceSessionCore::checkAgainstSupportedConfig (
                r, { 44100.0, 48000.0, 96000.0 }, { 32, 64, 256 });
            expect (report.ok);
        }

        beginTest ("unsupported rate rejected with explicit reason");
        {
            DAW::DeviceRequest r;
            r.sampleRate = 192000.0;
            r.bufferSize = 64;
            const auto report = DAW::DeviceSessionCore::checkAgainstSupportedConfig (
                r, { 44100.0, 48000.0 }, { 32, 64 });
            expect (! report.ok);
            expect (report.reason.containsIgnoreCase ("sample rate"));
            expect (report.reason.containsIgnoreCase ("not supported"));
        }

        beginTest ("32-sample buffer rejected when the device did not report it");
        {
            DAW::DeviceRequest r;
            r.sampleRate = 48000.0;
            r.bufferSize = 32;
            const auto report = DAW::DeviceSessionCore::checkAgainstSupportedConfig (
                r, { 44100.0, 48000.0 }, { 64, 128, 256 });
            expect (! report.ok);
            expect (report.reason.containsIgnoreCase ("buffer"));
        }
    }
};

static DeviceConfigValidationTests deviceConfigValidationTests;
```

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: build FAILURE — `checkAgainstSupportedConfig` does not exist.

- [ ] **Step 3: Implement session-side validation**

In `Source/DeviceCore/DeviceSessionCore.h`, add after the `ValidationReport` struct (:26-30):

```cpp
    /** Optional device-reported capability lists. Empty = advisory mode
        (no enforcement; JUCE/driver nearest-match + read-back govern). */
    void setSupportedConfig(juce::Array<double> rates, juce::Array<int> sizes)
    {
        supportedRates_ = std::move (rates);
        supportedBufferSizes_ = std::move (sizes);
    }

    /** Pure capability-membership check (unit-tested without a device). */
    static ValidationReport checkAgainstSupportedConfig(const DeviceRequest& r,
                                                        const juce::Array<double>& rates,
                                                        const juce::Array<int>& sizes)
    {
        if (rates.isNotEmpty())
        {
            bool found = false;
            for (const double supported : rates)
                if (std::abs (supported - r.sampleRate) < 1.0)
                    found = true;
            if (! found)
                return { false, "Selected sample rate is not supported by this device." };
        }

        if (sizes.isNotEmpty() && ! sizes.contains (r.bufferSize))
            return { false, "Selected buffer size is not supported by this device." };

        return { true, {} };
    }
```

Inside `validate` (:51-103), immediately before `return { true, {} };` (:102), insert:

```cpp
        const auto capability = checkAgainstSupportedConfig (r, supportedRates_, supportedBufferSizes_);
        if (! capability.ok)
            return capability;
```

Add the private members at :275-278:

```cpp
    juce::Array<double> supportedRates_;
    juce::Array<int> supportedBufferSizes_;
```

- [ ] **Step 4: Run validation tests**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS — both Device suites green.

- [ ] **Step 5: Wire the panel**

In `Source/UICore/AudioDevicePanelUI.h`:

(a) Replace `refreshFromSnapshot` body lines :263-266 with:

```cpp
        repopulateDeviceLists();
        repopulateRateAndBufferLists();
        selectComboText (sampleRateBox_, juce::String (juce::roundToInt (pendingRequest_.sampleRate)), true);
        selectComboText (bufferBox_,
                         DAW::DevicePanelModelCore::formatBufferSizeLabel (pendingRequest_.bufferSize), true);
```

(b) Add the new private method (place next to `repopulateDeviceLists`):

```cpp
    /** Device-authoritative rate/buffer lists for the currently selected
        type+output device. Falls back to the current snapshot values when
        enumeration yields nothing, so the combos are never empty. Pushes
        the lists into the session for validate/commit enforcement. */
    void repopulateRateAndBufferLists()
    {
        const auto typeName = backendBox_.getText().isNotEmpty()
            ? backendBox_.getText() : pendingRequest_.typeName;
        const auto outputName = outputBox_.getText().isNotEmpty()
            ? outputBox_.getText() : pendingRequest_.outputDeviceName;

        availableRates_   = model_.getAvailableSampleRates (typeName, outputName);
        availableBuffers_ = model_.getAvailableBufferSizes (typeName, outputName);
        session_.setSupportedConfig (availableRates_, availableBuffers_);

        juce::StringArray rateItems;
        for (const double r : availableRates_)
            rateItems.add (juce::String (juce::roundToInt (r)));
        if (rateItems.isEmpty())
            rateItems.add (juce::String (juce::roundToInt (pendingRequest_.sampleRate)));
        setComboItems (sampleRateBox_, rateItems);

        juce::StringArray bufferItems;
        for (const int b : availableBuffers_)
            bufferItems.add (DAW::DevicePanelModelCore::formatBufferSizeLabel (b));
        if (bufferItems.isEmpty())
            bufferItems.add (juce::String (pendingRequest_.bufferSize));
        setComboItems (bufferBox_, bufferItems);
    }
```

(c) In `controlsChanged` (:420-433), append a call at the end so picking another output device re-enumerates capabilities (cache makes repeat calls cheap; first probe per device is one temp-device creation, same as channel choices):

```cpp
        repopulateRateAndBufferLists();
```

(d) In `backendChanged` (:402-418), after `repopulateDeviceLists();` add:

```cpp
        repopulateRateAndBufferLists();
```

(e) Add the members next to `outputChannelChoices_` (:785):

```cpp
    juce::Array<double> availableRates_;
    juce::Array<int> availableBuffers_;
```

Note: `controlsChanged` (:428-429) already parses rate via `getDoubleValue()` and buffer via `getIntValue()`; `"32 (experimental)".getIntValue()` yields 32 — no parser change needed (covered by Task 1 test).

- [ ] **Step 6: Compile the app (Debug) and re-run tests**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: exit 0 (panel/session compile; `getCommon*` references are gone).
Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS.

- [ ] **Step 7: Checkpoint (NO COMMIT)**

---

### Task 3: B4 — pure period math, adaptive ring drain, jitter-semantics documentation

**Files:**
- Modify: `Source/DiagnosticsCore/CallbackAuditCore.h` (add free functions + ring capacity constant + header docs)
- Modify: `Source/MainComponent.h` (drain timer + member declarations; ring at :397)
- Modify: `Source/MainComponent.cpp` (:6229-6239 use pure functions + arm drain timer; :6310-6317 stop timer; add `drainCallbackAuditRing`)
- Modify: `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp` (append period/drain suite)

**Interfaces:**
- Consumes: existing `callbackAuditRing_ (CallbackAuditRing<1024>)`, `callbackAuditPeriodTicks_`, watchdog report at `MainComponent.cpp:6638-6756`.
- Produces:
  - `constexpr int64_t computeCallbackPeriodTicks (int numSamples, double sampleRate, double ticksPerSecond) noexcept`
  - `inline double computeAuditDrainIntervalSeconds (double periodSeconds, size_t ringCapacity) noexcept` — `clamp(0.5 × capacity × period, 0.05, 5.0)`
  - `static constexpr size_t CallbackAuditRing<Capacity>::kCapacity`
  - `void MainComponent::drainCallbackAuditRing()` (message thread)

- [ ] **Step 1: Write the failing tests**

Append to `Tests/Source/Diagnostics/CallbackAuditCoreTests.cpp` before the final `static`:

```cpp
class CallbackAuditPeriodTests final : public juce::UnitTest
{
public:
    CallbackAuditPeriodTests() : juce::UnitTest ("callback-audit-period.v1", "APEX.Diagnostics") {}

    void runTest() override
    {
        beginTest ("period ticks derive from actual rate/block across the full matrix");
        {
            constexpr double tps = 10000000.0; // Windows high-resolution ticks/second
            const double rates[]  = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            const int    blocks[] = { 32, 64, 128, 256, 512, 1024, 2048 };

            for (const double rate : rates)
                for (const int block : blocks)
                {
                    const double expected = (static_cast<double> (block) / rate) * tps;
                    const auto ticks = computeCallbackPeriodTicks (block, rate, tps);
                    expectWithinAbsoluteError (static_cast<double> (ticks), expected, 1.0);
                    expect (ticks > 0);
                }
        }

        beginTest ("spot-exact nominal deadlines");
        {
            constexpr double tps = 10000000.0;
            // 48k/64  = 1.333 ms -> 13333 ticks (truncation)
            expectEquals (computeCallbackPeriodTicks (64, 48000.0, tps),  (int64_t) 13333);
            // 48k/32  = 0.667 ms
            expectEquals (computeCallbackPeriodTicks (32, 48000.0, tps),  (int64_t) 6666);
            // 96k/32  = 0.333 ms
            expectEquals (computeCallbackPeriodTicks (32, 96000.0, tps),  (int64_t) 3333);
            // 192k/32 = 0.167 ms
            expectEquals (computeCallbackPeriodTicks (32, 192000.0, tps), (int64_t) 1666);
            // 44.1k/32
            expectEquals (computeCallbackPeriodTicks (32, 44100.0, tps),  (int64_t) 7256);
        }

        beginTest ("invalid inputs yield zero (caller treats as unarmed)");
        {
            expectEquals (computeCallbackPeriodTicks (0, 48000.0, 10000000.0),   (int64_t) 0);
            expectEquals (computeCallbackPeriodTicks (64, 0.0, 10000000.0),      (int64_t) 0);
            expectEquals (computeCallbackPeriodTicks (64, 48000.0, 0.0),         (int64_t) 0);
        }

        beginTest ("adaptive drain interval keeps the ring below half full");
        {
            constexpr size_t capacity = 1024;
            // 48k/32: 0.5 * 1024 * 0.000667 = 0.341 s
            expectWithinAbsoluteError (computeAuditDrainIntervalSeconds (32.0 / 48000.0, capacity),
                                       0.3413, 0.001);
            // 192k/32: 0.0853 s — must NOT be clamped above 0.1 s
            expectWithinAbsoluteError (computeAuditDrainIntervalSeconds (32.0 / 192000.0, capacity),
                                       0.0853, 0.001);
            // floor: absurdly small period clamps to 0.05 s
            expectEquals (computeAuditDrainIntervalSeconds (1.0 / 192000.0, capacity), 0.05);
            // ceiling: 48k/2048 = 21.8 s -> 5.0 s cap
            expectEquals (computeAuditDrainIntervalSeconds (2048.0 / 48000.0, capacity), 5.0);
            // degenerate inputs fall back to the legacy 5 s cadence
            expectEquals (computeAuditDrainIntervalSeconds (0.0, capacity), 5.0);
            expectEquals (computeAuditDrainIntervalSeconds (0.001, 0), 5.0);
        }

        beginTest ("ring exposes its capacity for drain math");
        {
            expectEquals (CallbackAuditRing<1024>::kCapacity, (size_t) 1024);
            expectEquals (CallbackAuditRing<4>::kCapacity, (size_t) 4);
        }
    }
};

static CallbackAuditPeriodTests callbackAuditPeriodTests;
```

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: build FAILURE — `computeCallbackPeriodTicks` / `computeAuditDrainIntervalSeconds` / `kCapacity` undefined.

- [ ] **Step 3: Implement in CallbackAuditCore.h**

In `Source/DiagnosticsCore/CallbackAuditCore.h`, add after the stage enum (:46):

```cpp
/** Nominal callback deadline in high-resolution ticks, derived from the
    ACTUAL device configuration (never a fixed 44.1/48 kHz assumption).
    Returns 0 for invalid inputs; callers treat 0 as "unarmed". */
constexpr int64_t computeCallbackPeriodTicks (int numSamples,
                                              double sampleRate,
                                              double ticksPerSecond) noexcept
{
    return (numSamples > 0 && sampleRate > 0.0 && ticksPerSecond > 0.0)
        ? static_cast<int64_t> ((static_cast<double> (numSamples) / sampleRate) * ticksPerSecond)
        : 0;
}

/** Adaptive audit-ring drain cadence. The ring must be drained at
    <= half-capacity worth of callback periods or ringOverflows fires by
    design (Brain §34.21: the lost-event counter must stay meaningful).
    48k/32 -> ~0.34 s; 192k/32 -> ~0.085 s; large blocks clamp to the
    legacy 5 s cadence. */
inline double computeAuditDrainIntervalSeconds (double periodSeconds, size_t ringCapacity) noexcept
{
    if (periodSeconds <= 0.0 || ringCapacity == 0)
        return 5.0;
    const double halfCapacitySeconds = 0.5 * static_cast<double> (ringCapacity) * periodSeconds;
    if (halfCapacitySeconds < 0.05) return 0.05;
    if (halfCapacitySeconds > 5.0)  return 5.0;
    return halfCapacitySeconds;
}
```

Add to `CallbackAuditRing` (:87-88):

```cpp
    static constexpr size_t kCapacity = Capacity;
```

Extend the header comment block (:27-31 threading note) with the jitter-semantics contract:

```cpp
    lateDeliveries semantics (2026-07-25, user decision): a strict
    start-to-start interval > nominal period measurement of CALLBACK
    DELIVERY JITTER. At ultra-small buffers (32/64 samples) ordinary
    driver/OS scheduling jitter trips this counter frequently; it is NOT
    by itself evidence of an audible failure, deadline miss, engine
    overrun, or xrun. Do not conflate: delivery jitter (lateDeliveries),
    duration > period (engineOverruns), the deadlineMiss flag, and
    driver-reported xruns are four distinct facts.
```

- [ ] **Step 4: Wire MainComponent**

In `Source/MainComponent.h`, next to the watchdog members (:403-408), add:

```cpp
    void drainCallbackAuditRing();

    struct CallbackAuditDrainTimer : public juce::Timer
    {
        explicit CallbackAuditDrainTimer (MainComponent& o) : owner (o) {}
        void timerCallback() override { owner.drainCallbackAuditRing(); }
    };
    CallbackAuditDrainTimer callbackAuditDrainTimer_ { *this };
    double callbackAuditDrainIntervalSeconds_ = 5.0;
```

In `Source/MainComponent.cpp` `applyAudioDevicePreparation`, replace the audit block (:6229-6239) with:

```cpp
    if (callbackAuditEnabled_)
    {
        callbackAuditStreamGeneration_++;
        callbackAuditSequence_ = 0;
        const double ticksPerSecond = static_cast<double> (juce::Time::getHighResolutionTicksPerSecond());
        callbackAuditPeriodTicks_ = computeCallbackPeriodTicks (currentBlockSize, currentSampleRate, ticksPerSecond);
        callbackAuditDrainIntervalSeconds_ = computeAuditDrainIntervalSeconds (
            static_cast<double> (currentBlockSize) / currentSampleRate,
            decltype (callbackAuditRing_)::kCapacity);
        callbackAuditPrevStartTicks_ = 0;
        callbackAuditAccumulator_.reset();

        // Frequent cheap drain (records are discarded — the accumulator is
        // authoritative) so ringOverflows only fires on a genuinely stalled
        // drain. The 5 s report cadence in inputWatchdogTick is unchanged.
        callbackAuditDrainTimer_.startTimer (juce::jmax (1, (int) std::round (callbackAuditDrainIntervalSeconds_ * 1000.0)));
    }
    else
    {
        callbackAuditDrainTimer_.stopTimer();
    }
```

In `releaseStoppedDeviceResources` (:6310-6317), inside the `if (callbackAuditEnabled_)` block, add:

```cpp
        callbackAuditDrainTimer_.stopTimer();
```

Add the method (next to `inputWatchdogTick`):

```cpp
void MainComponent::drainCallbackAuditRing()
{
    // Message thread. Pops are discarded by design; every record was already
    // folded into the accumulator on the audio thread.
    CallbackAuditRecord drained;
    while (callbackAuditRing_.tryPop (drained)) {}
}
```

- [ ] **Step 5: Build + test**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS — `callback-audit-period.v1` green plus existing audit suites.
Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: exit 0.

- [ ] **Step 6: Checkpoint (NO COMMIT)**

---

### Task 4: DrumSampler — engine-rate derivation + pad resampling

**Files:**
- Modify: `Source/DrumSamplerCore/DrumSamplerVoice.h` (members + setter)
- Modify: `Source/DrumSamplerCore/DrumSamplerVoice.cpp` (rate ratio, linear interp, ADSR rate)
- Modify: `Source/DrumSamplerCore/DrumSamplerVoicePool.h/.cpp` (`setSampleRate`)
- Modify: `Source/DrumSamplerCore/DrumSamplerEngine.h/.cpp` (`prepare`)
- Modify: `Source/MainComponent.cpp` (call `drumSamplerEngine_->prepare` in `applyAudioDevicePreparation` beside :6220)
- Create: `Tests/Source/DrumSampler/DrumSamplerRateTests.cpp`
- Modify: `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj` (test file + `..\..\..\Source\DrumSamplerCore\DrumSamplerVoice.cpp`, `DrumSamplerEngine.cpp`, `DrumSamplerVoicePool.cpp`)

**Interfaces:**
- Consumes: `VelocityLayer::sampleRate` (file rate, `DrumSamplerTypes.h:12`), `MainComponent`'s `currentSampleRate`.
- Produces:
  - `void DrumSamplerVoice::setSampleRate (double engineRate) noexcept`
  - `void DrumSamplerVoicePool::setSampleRate (double engineRate) noexcept`
  - `void DrumSamplerEngine::prepare (double sampleRate) noexcept`
  - Voice internals: `double engineSampleRate_ = 44100.0; double srcPosition_ = 0.0; double rateRatio_ = 1.0;`

- [ ] **Step 1: Write the failing tests**

Create `Tests/Source/DrumSampler/DrumSamplerRateTests.cpp`:

```cpp
#include <JuceHeader.h>
#include "../../../Source/DrumSamplerCore/DrumSamplerEngine.h"
#include <cmath>

class DrumSamplerRateTests final : public juce::UnitTest
{
public:
    DrumSamplerRateTests() : juce::UnitTest ("drumsampler.rate.v1", "APEX.DrumSampler") {}

    static void fillSine (juce::AudioBuffer<float>& buffer, double fileRate, double freqHz)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * freqHz * i / fileRate));
    }

    static DAW::DrumPadConfig makePad (double fileRate, double freqHz, int numSamples)
    {
        DAW::DrumPadConfig pad;
        pad.attackMs = 0.0f;   // envelope-neutral: instant attack
        pad.decayMs = 0.0f;
        pad.sustainLevel = 1.0f;
        pad.releaseMs = 0.0f;
        pad.gainDb = 0.0f;

        DAW::VelocityLayer layer;
        layer.sampleRate = (int) fileRate;
        layer.numSamples = numSamples;
        layer.audioData.setSize (1, numSamples);
        fillSine (layer.audioData, fileRate, freqHz);
        pad.layers.push_back (std::move (layer));
        return pad;
    }

    static double countZeroCrossingRate (const juce::AudioBuffer<float>& buffer, double engineRate)
    {
        int crossings = 0;
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                ++crossings;
        const double seconds = buffer.getNumSamples() / engineRate;
        return seconds > 0.0 ? (crossings / 2.0) / seconds : 0.0;
    }

    void runTest() override
    {
        beginTest ("pad pitch is preserved across engine rates (44.1k file)");
        {
            const double engineRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                auto pad = makePad (44100.0, 1000.0, 44100); // 1 s of 1 kHz
                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                const int outSamples = (int) (engineRate * 0.5); // first 500 ms
                juce::AudioBuffer<float> out (2, outSamples);
                out.clear();
                voice.process (out, 0, outSamples);

                const double measured = countZeroCrossingRate (out, engineRate);
                expectWithinAbsoluteError (measured, 1000.0, 30.0,
                                           juce::String ("pitch at engine rate ") + juce::String (engineRate));
            }
        }

        beginTest ("pad pitch is preserved when file rate exceeds engine rate");
        {
            auto pad = makePad (96000.0, 1000.0, 96000);
            DAW::DrumSamplerVoice voice;
            voice.setSampleRate (48000.0);
            voice.start (pad, 1.0f, 0);

            juce::AudioBuffer<float> out (2, 24000);
            out.clear();
            voice.process (out, 0, 24000);

            expectWithinAbsoluteError (countZeroCrossingRate (out, 48000.0), 1000.0, 30.0);
        }

        beginTest ("pad duration in seconds is preserved across engine rates");
        {
            const double engineRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                const int fileSamples = 22050; // 0.5 s at 44.1k
                auto pad = makePad (44100.0, 440.0, fileSamples);
                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                // Process in 32-sample blocks (also proves 32-sample blocks work).
                juce::AudioBuffer<float> out (2, 64);
                int rendered = 0;
                while (voice.isActive() && rendered < (int) (engineRate * 2.0))
                {
                    out.clear();
                    voice.process (out, 0, 32);
                    rendered += 32;
                }

                const double expectedSamples = 0.5 * engineRate;
                expectWithinAbsoluteError ((double) rendered, expectedSamples, 0.02 * engineRate,
                                           juce::String ("duration at engine rate ") + juce::String (engineRate));
            }
        }

        beginTest ("ADSR stage lengths are millisecond-invariant across rates");
        {
            const double engineRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            for (const double engineRate : engineRates)
            {
                auto pad = makePad (44100.0, 440.0, 44100);
                pad.attackMs = 10.0f;
                pad.sustainLevel = 1.0f;

                DAW::DrumSamplerVoice voice;
                voice.setSampleRate (engineRate);
                voice.start (pad, 1.0f, 0);

                // Ramp must complete within 10 ms (+/- one 32-sample block).
                const int probe = (int) (0.010 * engineRate) + 32;
                juce::AudioBuffer<float> out (2, probe);
                out.clear();
                voice.process (out, 0, probe);
                expect (voice.isActive());

                const float late = out.getSample (0, probe - 1);
                expectWithinAbsoluteError (std::abs (late), 1.0f, 0.05f,
                                           juce::String ("attack settled by 10 ms at ") + juce::String (engineRate));

                // And must NOT have settled far too early (half amplitude at ~5 ms).
                const int mid = (int) (0.005 * engineRate);
                expect (std::abs (out.getSample (0, mid)) < 0.9f,
                        juce::String ("attack not prematurely complete at ") + juce::String (engineRate));
            }
        }

        beginTest ("engine prepare propagates the rate to voices");
        {
            DAW::DrumSamplerEngine engine (1);
            engine.getPad (0) = makePad (44100.0, 1000.0, 22050);
            engine.getPad (0).midiNote = 36;
            engine.prepare (96000.0);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> out (2, 48000); // 0.5 s at 96k
            out.clear();
            engine.processBlock (midi, out, 0, 48000);

            expectWithinAbsoluteError (countZeroCrossingRate (out, 96000.0), 1000.0, 30.0);
        }

        beginTest ("default-constructed voice keeps legacy 44.1k behaviour (no prepare)");
        {
            auto pad = makePad (44100.0, 1000.0, 22050);
            DAW::DrumSamplerVoice voice; // no setSampleRate: defaults to 44100
            voice.start (pad, 1.0f, 0);
            juce::AudioBuffer<float> out (2, 11025);
            out.clear();
            voice.process (out, 0, 11025);
            expectWithinAbsoluteError (countZeroCrossingRate (out, 44100.0), 1000.0, 30.0);
        }
    }
};

static DrumSamplerRateTests drumSamplerRateTests;
```

Add to the test vcxproj:

```xml
    <ClCompile Include="..\..\Source\DrumSampler\DrumSamplerRateTests.cpp"/>
    <ClCompile Include="..\..\..\Source\DrumSamplerCore\DrumSamplerVoice.cpp"/>
    <ClCompile Include="..\..\..\Source\DrumSamplerCore\DrumSamplerEngine.cpp"/>
    <ClCompile Include="..\..\..\Source\DrumSamplerCore\DrumSamplerVoicePool.cpp"/>
```

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: build FAILURE — `setSampleRate`/`prepare` undefined.

- [ ] **Step 3: Implement voice changes**

`Source/DrumSamplerCore/DrumSamplerVoice.h` — add to the class:

```cpp
    /** Engine/device sample rate. Defaults to 44100 (legacy behaviour when
        the owner never prepares the sampler). */
    void setSampleRate (double engineRate) noexcept
    {
        engineSampleRate_ = (engineRate > 0.0) ? engineRate : 44100.0;
    }
```

Replace the private members `int64_t playPosition_ = 0;` block with:

```cpp
    double engineSampleRate_ = 44100.0;
    double srcPosition_ = 0.0;   // source-domain position (fractional for resampling)
    double rateRatio_ = 1.0;     // layer file rate / engine rate, captured at start()
```

`Source/DrumSamplerCore/DrumSamplerVoice.cpp`:

In `start` (:5-13), after `pad_ = &pad;`:

```cpp
    const double fileRate = (! pad.layers.empty() && pad.layers[0].sampleRate > 0)
        ? (double) pad.layers[0].sampleRate : 44100.0;
    rateRatio_ = fileRate / engineSampleRate_;
    srcPosition_ = (double) samplePos;
```

(`playPosition_ = samplePos;` is removed; `samplePos` remains the source start offset.)

In `process` (:20-46), replace the loop body:

```cpp
    const float* sourceData = layer.audioData.getReadPointer(0);
    const float gainLinear = juce::Decibels::decibelsToGain(pad_->gainDb) * velocity_;

    for (int i = 0; i < numSamples; ++i) {
        advanceEnvelope(1);
        if (envStage_ == EnvStage::Idle) { active_ = false; return; }

        const int64_t i0 = (int64_t) srcPosition_;
        if (i0 >= layer.numSamples) { active_ = false; return; }

        // Linear interpolation between i0 and i0+1 for fractional
        // source positions (rate-ratio resampling keeps pad pitch/speed
        // correct at any engine rate). Smallest correct SRC for one-shot
        // drum playback; higher-quality SRC is a measured-future upgrade.
        const float frac = (float) (srcPosition_ - (double) i0);
        const float s0 = sourceData[i0];
        const float s1 = (i0 + 1 < layer.numSamples) ? sourceData[i0 + 1] : s0;
        const float sample = (s0 + frac * (s1 - s0)) * gainLinear * envValue_;

        outputBuffer.addSample(0, startSample + i, sample);
        if (outputBuffer.getNumChannels() > 1)
            outputBuffer.addSample(1, startSample + i, sample);

        srcPosition_ += rateRatio_;
    }
```

In `advanceEnvelope` (:48-52):

```cpp
    const float rate = (float) engineSampleRate_;
    const float attackSamples  = pad_->attackMs  * 0.001f * rate;
    const float decaySamples   = pad_->decayMs   * 0.001f * rate;
    const float releaseSamples = pad_->releaseMs * 0.001f * rate;
```

RT-safety check: no allocation, no locks, no I/O — arithmetic only. Compliant with Brain §2.

`Source/DrumSamplerCore/DrumSamplerVoicePool.h` — add:

```cpp
    void setSampleRate (double engineRate) noexcept;
```

`DrumSamplerVoicePool.cpp` — add:

```cpp
void DrumSamplerVoicePool::setSampleRate (double engineRate) noexcept {
    for (auto* v : voices_)
        v->setSampleRate (engineRate);
}
```

`Source/DrumSamplerCore/DrumSamplerEngine.h` — add to the public interface:

```cpp
    /** Prepares the sampler for the actual device rate (Brain §3: prepare
        from the granted configuration). Call on device (re)start. */
    void prepare (double sampleRate) noexcept { voicePool_.setSampleRate (sampleRate); }
```

`Source/MainComponent.cpp` in `applyAudioDevicePreparation`, immediately after `stepSeqPlayback_.prepareToPlay(currentSampleRate, currentBlockSize);` (:6220), add:

```cpp
    if (drumSamplerEngine_ != nullptr)
        drumSamplerEngine_->prepare (currentSampleRate);
```

- [ ] **Step 4: Run tests + app build**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS — `drumsampler.rate.v1` green. (If the linker reports missing symbols from `MidiClip`/`Clip` internals in later tasks, mirror the StepSequencerModel.cpp precedent and add the corresponding implementation `.cpp`.)
Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: exit 0.

- [ ] **Step 5: Checkpoint (NO COMMIT)**

---

### Task 5: MidiClip / Clip rate-derived default timing + device-rate re-sync

**Files:**
- Modify: `Source/MidiCore/MidiClip.h` (:28-29 ctor declaration area; `setSampleRate`)
- Modify: `Source/MidiCore/MidiClip.cpp` (:5-16 ctor; `setSampleRate` implementation)
- Modify: `Source/ClipCore/Clip.cpp` (`createMIDIClip` at :364-373 passthrough rate)
- Modify: `Source/ClipCore/Clip.h` (:133 comment — documentation of inert placeholder)
- Modify: `Source/CommandCore/GeneralCommands.h` (MIDI-clip creation call site passes `engineSampleRate_`, :259 area)
- Modify: `Source/MainComponent.cpp` (:6005-6019 `prepareToPlay` — add device-rate re-sync for MIDI clips)
- Create: `Tests/Source/Arrangement/ClipTimingDefaultTests.cpp`
- Modify: `Tests/Builds/VisualStudio2026/APEXTests_ConsoleApp.vcxproj` (test file + `..\..\..\Source\MidiCore\MidiClip.cpp` + `..\..\..\Source\ClipCore\Clip.cpp`; if the linker then requires it, `..\..\..\Source\MidiCore\PianoRollClipModel.cpp` — mirror the StepSequencerModel.cpp precedent)

**Interfaces:**
- Consumes: `Clip::setLength/getLength`, `pianoRollModel_.setLengthTicks`, `ApplicationCore::getCurrentSampleRate`.
- Produces:
  - `MidiClip::MidiClip(const ClipID& id, const juce::String& name, double sampleRate = 44100.0)`
  - `void MidiClip::setSampleRate(double sr)` — seconds-preserving: rescales the sample length by `sr / oldRate` (musical/tick content is the invariant under linear ticks↔samples conversion)
  - `ClipManager::createMIDIClip(const juce::String& name, double sampleRate = 44100.0)`

- [ ] **Step 1: Write the failing tests**

Create `Tests/Source/Arrangement/ClipTimingDefaultTests.cpp`:

```cpp
#include <JuceHeader.h>
#include "../../../Source/MidiCore/MidiClip.h"

class ClipTimingDefaultTests final : public juce::UnitTest
{
public:
    ClipTimingDefaultTests() : juce::UnitTest ("clip.timing-defaults.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("default MIDI clip is 8 seconds (4 bars @120 BPM) at every rate");
        {
            const double rates[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double rate : rates)
            {
                DAW::MidiClip clip (DAW::ClipID(), "t", rate);
                const double seconds = (double) clip.getLength() / rate;
                expectWithinAbsoluteError (seconds, 8.0, 0.001,
                                           juce::String ("default length seconds at ") + juce::String (rate));
            }
        }

        beginTest ("default construction without a rate keeps the legacy 44.1k length");
        {
            DAW::MidiClip clip (DAW::ClipID(), "t");
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) 352800);
        }

        beginTest ("setSampleRate preserves the clip's length in seconds");
        {
            DAW::MidiClip clip (DAW::ClipID(), "t", 44100.0);
            clip.setLength ((DAW::SamplePosition) 66150); // 1.5 s at 44.1k (a trimmed clip)
            clip.setSampleRate (96000.0);
            const double seconds = (double) clip.getLength() / 96000.0;
            expectWithinAbsoluteError (seconds, 1.5, 0.001);
        }

        beginTest ("setSampleRate keeps tick position of the clip end stable");
        {
            DAW::MidiClip clip (DAW::ClipID(), "t", 48000.0);
            const auto ticksBefore = clip.samplesToTicks (clip.getLength());
            clip.setSampleRate (88200.0);
            const auto ticksAfter = clip.samplesToTicks (clip.getLength());
            expect (std::abs ((double) (ticksAfter - ticksBefore)) <= 2.0,
                    "tick extent stable across rate change (rounding only)");
        }

        beginTest ("same-rate setSampleRate is a no-op for length");
        {
            DAW::MidiClip clip (DAW::ClipID(), "t", 48000.0);
            const auto length = clip.getLength();
            clip.setSampleRate (48000.0);
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) length);
        }

        beginTest ("zero/negative rate is ignored safely");
        {
            DAW::MidiClip clip (DAW::ClipID(), "t", 48000.0);
            const auto length = clip.getLength();
            clip.setSampleRate (0.0);
            expectWithinAbsoluteError (clip.getSampleRate(), 48000.0, 0.001);
            expectEquals ((juce::int64) clip.getLength(), (juce::int64) length);
        }
    }
};

static ClipTimingDefaultTests clipTimingDefaultTests;
```

Add the test file + production deps to the vcxproj:

```xml
    <ClCompile Include="..\..\Source\Arrangement\ClipTimingDefaultTests.cpp"/>
    <ClCompile Include="..\..\..\Source\MidiCore\MidiClip.cpp"/>
    <ClCompile Include="..\..\..\Source\ClipCore\Clip.cpp"/>
```

If the linker then reports unresolved `PianoRollClipModel` symbols, also add `..\..\..\Source\MidiCore\PianoRollClipModel.cpp` (check the file exists first; same precedent as `StepSequencerModel.cpp`).

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: build FAILURE — `MidiClip` ctor does not take a rate.

- [ ] **Step 3: Implement MidiClip changes**

`Source/MidiCore/MidiClip.h`:

Change the ctor declaration (:28) to:

```cpp
    MidiClip(const ClipID& id, const juce::String& name, double sampleRate = 44100.0);
```

Replace the inline `setSampleRate` (:42) with a declaration:

```cpp
    /** Sets the engine/device rate. Seconds-preserving: the sample length is
        rescaled by newRate/oldRate so the clip's time (and tick) extent is
        invariant under device rate changes. Ignores non-positive rates. */
    void setSampleRate(double sr);
```

`Source/MidiCore/MidiClip.cpp`:

Replace the ctor (:5-16) with:

```cpp
MidiClip::MidiClip(const ClipID& id, const juce::String& name, double sampleRate)
    : Clip(id, name, ClipType::MIDI),
      sampleRate_ (sampleRate > 0.0 ? sampleRate : 44100.0)
{
    // Default 4-bar MIDI clip at 120 BPM = 8 seconds, derived from the
    // actual engine rate (was hardcoded 352800 samples @44.1kHz).
    setLength ((SamplePosition) std::llround (8.0 * sampleRate_));
    pianoRollModel_.setLengthTicks (3840);
}
```

Add the `setSampleRate` implementation:

```cpp
void MidiClip::setSampleRate (double sr)
{
    if (sr <= 0.0 || sr == sampleRate_)
        return;

    // Seconds-preserving rescale: ticks<->samples is linear at fixed tempo,
    // so scaling samples by newRate/oldRate keeps BOTH the seconds extent
    // and the tick extent constant (<=1 sample rounding).
    const double ratio = sr / sampleRate_;
    sampleRate_ = sr;
    setLength ((SamplePosition) std::llround ((double) getLength() * ratio));
}
```

`Source/ClipCore/Clip.h` (:133) — update the comment only (the value is an inert pre-rate placeholder; every real creation path sets an explicit length, verified at `createEmptyClip` Clip.cpp:382-395):

```cpp
    SamplePosition length_{44100}; // Inert pre-construction placeholder (1 s @44.1k);
                                   // real creation paths set an explicit rate-derived length
```

`Source/ClipCore/Clip.cpp` `createMIDIClip` (:364-373):

```cpp
MidiClip* ClipManager::createMIDIClip(const juce::String& name, double sampleRate)
{
    MidiClip* clip = nullptr;
    {
        juce::ScopedLock sl(clipLock_);
        clip = new MidiClip(generateUniqueClipIDUnlocked(), name, sampleRate);
        clips_.add(clip);
    }
    listeners_.call([clip](Listener& l) { l.clipAdded(clip); });
    return clip;
}
```

Update its declaration in `Clip.h` to `MidiClip* createMIDIClip(const juce::String& name, double sampleRate = 44100.0);`.

`Source/CommandCore/GeneralCommands.h` — at the MIDI-clip creation site (:259 area, `createMIDIClip` caller), pass the engine rate:

```cpp
        clipManager_.createMIDIClip (name, engineSampleRate_);
```

(The class already holds `engineSampleRate_`, ctor-injected at :616, default 44100 at :714 — confirmed by audit. If the exact call site differs, adapt: the rule is every `createMIDIClip` caller passes the current engine rate where one is in scope; `MidiInputCore.h:235` record-target creation already sets rate — leave it.)

`Source/MainComponent.cpp` `prepareToPlay` (:6005-6019) — inside the existing `juce::MessageManager::callAsync` lambda that pushes the rate to the arrangement, also re-sync MIDI clips (device-rate-change path; mirrors the tempo-change loop at :1091-1099 but WITHOUT `syncLengthFromModel`, preserving user-trimmed lengths via seconds-preserving `setSampleRate`):

```cpp
        juce::MessageManager::callAsync([safeThis, sampleRate]()
        {
            if (auto* self = safeThis.getComponent())
            {
                if (self->arrangement_)
                    self->arrangement_->setEngineSampleRate(sampleRate);

                auto& cm = self->appCore_.getClipManager();
                for (auto* clip : cm.getAllClips())
                    if (auto* midiClip = dynamic_cast<DAW::MidiClip*>(clip))
                        midiClip->setSampleRate (sampleRate);
            }
        });
```

(Verify the lambda captures and the exact `arrangement_` call at :6015-6019 when editing; keep them, add the loop.)

- [ ] **Step 4: Run tests + app build**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS — `clip.timing-defaults.v1` green.
Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: exit 0.

- [ ] **Step 5: Checkpoint (NO COMMIT)**

---

### Task 6: LUFS K-weighting at all supported rates

**Files:**
- Modify: `Source/MeteringCore/LufsMeterCore.h` (:88-149 Biquad; add public static coefficient function)
- Modify: `Tests/Source/Diagnostics/LufsMeterCoreTests.cpp` (append per-rate suite)

**Interfaces:**
- Consumes: BS.1770 analog-prototype constants (De Man refinement).
- Produces:
  - `static void LufsMeterCore::computeKWeightingCoefficients (double fs, bool shelf, double& b0, double& b1, double& b2, double& a1, double& a2)` — public, used by `Biquad::setCoefficientsForSampleRate`; `|fs−48000|<1` returns the published BS.1770 constants verbatim; all other rates compute from the prototype (shelf f0=1681.974450955533 Hz, G=+3.99984385397 dB, Q=0.7071752369554193; HP f0=38.13547087602444 Hz, Q=0.5003270373238773).

- [ ] **Step 1: Write the failing tests**

Append to `Tests/Source/Diagnostics/LufsMeterCoreTests.cpp` before the final `static`:

```cpp
class LufsKWeightingRateTests final : public juce::UnitTest
{
public:
    LufsKWeightingRateTests() : juce::UnitTest ("metering.lufs-kweighting-rates.v1", "APEX.Diagnostics") {}

    static void oracleShelf (double fs, double c[5])
    {
        // Independent oracle: RBJ high-shelf from the BS.1770/De Man prototype.
        const double f0 = 1681.974450955533, gainDb = 3.99984385397, q = 0.7071752369554193;
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cs = std::cos (w0);
        const double beta = 2.0 * std::sqrt (A) * alpha;
        const double B0 = A * ((A + 1.0) + (A - 1.0) * cs + beta);
        const double B1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
        const double B2 = A * ((A + 1.0) + (A - 1.0) * cs - beta);
        const double A0 = (A + 1.0) - (A - 1.0) * cs + beta;
        const double A1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
        const double A2 = (A + 1.0) - (A - 1.0) * cs - beta;
        c[0] = B0 / A0; c[1] = B1 / A0; c[2] = B2 / A0; c[3] = A1 / A0; c[4] = A2 / A0;
    }

    static void oracleHp (double fs, double c[5])
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cs = std::cos (w0);
        const double B0 = (1.0 + cs) * 0.5, B1 = -(1.0 + cs), B2 = (1.0 + cs) * 0.5;
        const double A0 = 1.0 + alpha, A1 = -2.0 * cs, A2 = 1.0 - alpha;
        c[0] = B0 / A0; c[1] = B1 / A0; c[2] = B2 / A0; c[3] = A1 / A0; c[4] = A2 / A0;
    }

    void runTest() override
    {
        beginTest ("48 kHz keeps the published BS.1770 coefficients verbatim");
        {
            double b0, b1, b2, a1, a2;
            DAW::LufsMeterCore::computeKWeightingCoefficients (48000.0, true, b0, b1, b2, a1, a2);
            expectWithinAbsoluteError (b0,  1.53512485958697, 1e-12);
            expectWithinAbsoluteError (b1, -2.69169618940638, 1e-12);
            expectWithinAbsoluteError (b2,  1.19839281085285, 1e-12);
            expectWithinAbsoluteError (a1, -1.69065929318241, 1e-12);
            expectWithinAbsoluteError (a2,  0.73248077421585, 1e-12);

            DAW::LufsMeterCore::computeKWeightingCoefficients (48000.0, false, b0, b1, b2, a1, a2);
            expectWithinAbsoluteError (b0,  1.0,               1e-12);
            expectWithinAbsoluteError (b1, -2.0,               1e-12);
            expectWithinAbsoluteError (b2,  1.0,               1e-12);
            expectWithinAbsoluteError (a1, -1.99004745483398, 1e-12);
            expectWithinAbsoluteError (a2,  0.99007225036621, 1e-12);
        }

        beginTest ("prototype-derived coefficients match the oracle at all supported rates");
        {
            const double rates[] = { 32000.0, 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            for (const double fs : rates)
            {
                double oracle[5];
                double b0, b1, b2, a1, a2;

                oracleShelf (fs, oracle);
                DAW::LufsMeterCore::computeKWeightingCoefficients (fs, true, b0, b1, b2, a1, a2);
                expectWithinAbsoluteError (b0, oracle[0], 1e-9, juce::String (fs) + " shelf b0");
                expectWithinAbsoluteError (b1, oracle[1], 1e-9, juce::String (fs) + " shelf b1");
                expectWithinAbsoluteError (b2, oracle[2], 1e-9, juce::String (fs) + " shelf b2");
                expectWithinAbsoluteError (a1, oracle[3], 1e-9, juce::String (fs) + " shelf a1");
                expectWithinAbsoluteError (a2, oracle[4], 1e-9, juce::String (fs) + " shelf a2");

                oracleHp (fs, oracle);
                DAW::LufsMeterCore::computeKWeightingCoefficients (fs, false, b0, b1, b2, a1, a2);
                expectWithinAbsoluteError (b0, oracle[0], 1e-9, juce::String (fs) + " hp b0");
                expectWithinAbsoluteError (a2, oracle[4], 1e-9, juce::String (fs) + " hp a2");
            }
        }

        beginTest ("-23 dBFS 1 kHz sine measures -23 LUFS at every supported rate");
        {
            const double rates[] = { 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            const int blocks[] = { 32, 256 };
            for (const double fs : rates)
                for (const int block : blocks)
                {
                    DAW::LufsMeterCore meter;
                    meter.prepare (fs);

                    const double amplitude = std::pow (10.0, -23.0 / 20.0);
                    std::vector<float> data ((size_t) block);
                    juce::int64 phase = 0;

                    // Feed > 3 s so momentary + short-term + integrated all settle.
                    const int numBlocks = (int) (fs * 3.5 / block);
                    for (int b = 0; b < numBlocks; ++b)
                    {
                        for (int i = 0; i < block; ++i, ++phase)
                            data[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * phase / fs));
                        meter.process (data.data(), block);
                    }

                    expectWithinAbsoluteError ((double) meter.getMomentaryLufs(), -23.0, 0.3,
                                               juce::String ("momentary @") + juce::String (fs) + "/" + juce::String (block));
                    expectWithinAbsoluteError ((double) meter.getIntegratedLufs(), -23.0, 0.3,
                                               juce::String ("integrated @") + juce::String (fs) + "/" + juce::String (block));
                }
        }
    }
};

static LufsKWeightingRateTests lufsKWeightingRateTests;
```

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: build FAILURE — `computeKWeightingCoefficients` does not exist. (The existing LUFS suites must still build/pass unchanged.)

- [ ] **Step 3: Implement**

In `Source/MeteringCore/LufsMeterCore.h`, add to the public section of `LufsMeterCore` (after `prepare`):

```cpp
    /** BS.1770 K-weighting coefficients. At 48 kHz the published standard
        values are used verbatim (bit-exact reference path). At every other
        rate both biquads are derived from the BS.1770 analog prototype
        (De Man refinement) so non-48k LUFS is standard-conformant rather
        than an approximation. */
    static void computeKWeightingCoefficients (double fs, bool shelf,
                                               double& b0, double& b1, double& b2,
                                               double& a1, double& a2)
    {
        if (std::abs (fs - 48000.0) < 1.0)
        {
            if (shelf)
            {
                b0 = 1.53512485958697; b1 = -2.69169618940638; b2 = 1.19839281085285;
                a1 = -1.69065929318241; a2 = 0.73248077421585;
            }
            else
            {
                b0 = 1.0; b1 = -2.0; b2 = 1.0;
                a1 = -1.99004745483398; a2 = 0.99007225036621;
            }
            return;
        }

        if (shelf)
        {
            const double f0 = 1681.974450955533, gainDb = 3.99984385397, q = 0.7071752369554193;
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double c = std::cos (w0);
            const double beta = 2.0 * std::sqrt (A) * alpha;
            const double B0 = A * ((A + 1.0) + (A - 1.0) * c + beta);
            const double B1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
            const double B2 = A * ((A + 1.0) + (A - 1.0) * c - beta);
            const double A0 = (A + 1.0) - (A - 1.0) * c + beta;
            const double A1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
            const double A2 = (A + 1.0) - (A - 1.0) * c - beta;
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
        else
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double c = std::cos (w0);
            const double B0 = (1.0 + c) * 0.5;
            const double B1 = -(1.0 + c);
            const double B2 = (1.0 + c) * 0.5;
            const double A0 = 1.0 + alpha;
            const double A1 = -2.0 * c;
            const double A2 = 1.0 - alpha;
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
    }
```

Replace `Biquad::setCoefficientsForSampleRate` body (:94-140) with:

```cpp
        void setCoefficientsForSampleRate(double fs, bool shelf)
        {
            computeKWeightingCoefficients (fs, shelf, b0, b1, b2, a1, a2);
            z1 = z2 = 0.0;
        }
```

(This deletes the old 1500 Hz/Q-0.707 and 38 Hz/Q-0.5 approximation branches — they were the defect. The `Biquad` struct is nested inside `LufsMeterCore`, so it can call the static directly.)

- [ ] **Step 4: Run tests**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Expected: PASS — new K-weighting suite green AND all pre-existing LUFS suites (they pin 48k, whose path is bit-exact).

- [ ] **Step 5: Checkpoint (NO COMMIT)**

---

### Task 7: ArrangementEditor 44100 conversions (C5 audio-affecting, C6, C7)

**Files:**
- Modify: `Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h` (:383-384, :642-643; add `setEngineSampleRate` to both panel classes)
- Modify: `Builds/VisualStudio2026/ArrangementEditor/ClipPropertiesWindowCore.h` (:285 area — plumb rate into `ClipPropertiesPanelCore` + forwarder on the window)
- Modify: `Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.h/.cpp` (:187 construct-time push; `setEngineSampleRate` update path :932/:967 push; :1093 ClipRenderCore creation)
- Modify: `Builds/VisualStudio2026/ArrangementEditor/ClipRenderCore.h/.cpp` (:70, :537, :729 — add `setEngineSampleRate`, use it)
- Modify: `Builds/VisualStudio2026/ArrangementEditor/ClipPanelKnobBridgeCore.h` (:92 — real source rate)

**Interfaces:**
- Consumes: `ArrangementViewCore::m_engineSampleRate` (ArrangementViewCore.h:314, updated via `setEngineSampleRate`), `ArrangementClipModel::sourceSampleRate` (ArrangementClipModel.h:88-91; 0 = unknown → 44100).
- Produces:
  - `void ClipAutomationPanel::setEngineSampleRate (double)` and `void ClipAutomateFloatingPanel::setEngineSampleRate (double)` (member `double engineSampleRate_ = 44100.0;`)
  - `void ClipPropertiesPanelCore::setEngineSampleRate (double)` / `void ClipPropertiesWindowCore::setEngineSampleRate (double)`
  - `void ClipRenderCore::setEngineSampleRate (double)` (member `double engineSampleRate_ = 44100.0;`)

- [ ] **Step 1: ClipAutomationPanel — both classes**

In `ClipAutomationPanel.h`:

Add to BOTH `ClipAutomateFloatingPanel` and `ClipAutomationPanel`:

```cpp
    /** Engine/device sample rate for seconds<->samples conversions.
        Pushed by the owning view; defaults to 44100 (legacy). */
    void setEngineSampleRate (double sr) { engineSampleRate_ = (sr > 0.0) ? sr : 44100.0; }
```

with member `double engineSampleRate_ = 44100.0;`.

Replace both conversion sites (the two lines appear identically twice):

```cpp
            t.regionStartSample   = (int64_t) (clip_.startTime * engineSampleRate_);
            t.regionLengthSamples = (int64_t) (clip_.length    * engineSampleRate_);
```

- [ ] **Step 2: Plumb through ClipPropertiesWindowCore**

In `ClipPropertiesWindowCore.h`:

`ClipPropertiesPanelCore` gains:

```cpp
    void setEngineSampleRate (double sr)
    {
        engineSampleRate_ = (sr > 0.0) ? sr : 44100.0;
        if (automationPanel_ != nullptr)
            automationPanel_->setEngineSampleRate (engineSampleRate_);
    }
```

member `double engineSampleRate_ = 44100.0;`, and in `rebuildAutomationPanel()` immediately after constructing `automationPanel_` (:285) add `automationPanel_->setEngineSampleRate (engineSampleRate_);`.

`ClipPropertiesWindowCore` gains a forwarder:

```cpp
    void setEngineSampleRate (double sr) { panel_.setEngineSampleRate (sr); }
```

(Verify the inner panel member name at :563-640 when editing; if the window exposes the panel differently, forward through the existing accessor.)

- [ ] **Step 3: Push from ArrangementViewCore + fix ClipRenderCore + knob bridge**

`ArrangementViewCore.cpp`:
- After `m_propertiesWindow = std::make_unique<ClipPropertiesWindowCore>();` (:187): `m_propertiesWindow->setEngineSampleRate (m_engineSampleRate);`
- In the `setEngineSampleRate` update path (:932/:967 — the method that assigns `m_engineSampleRate`): add `if (m_propertiesWindow) m_propertiesWindow->setEngineSampleRate (m_engineSampleRate);` and push to clip renderers (see next).
- At the `ClipRenderCore` creation site (:1093): immediately after `make_unique`, call `renderer->setEngineSampleRate (m_engineSampleRate);`. Locate the container the renderers are stored in (same function) and, in `setEngineSampleRate`, iterate it calling `setEngineSampleRate` on each renderer. (Exact container name is read at edit time; the creation site at :1093 is the single source of renderers.)

`ClipRenderCore.h/.cpp`: add

```cpp
    void setEngineSampleRate (double sr) { engineSampleRate_ = (sr > 0.0) ? sr : 44100.0; }
```

member `double engineSampleRate_ = 44100.0;` then replace:
- `ClipRenderCore.cpp:70`: `(int64_t)std::llround(juce::jmax(0.0, m_model->length) * engineSampleRate_)`
- `ClipRenderCore.cpp:537`: `(int64_t)std::llround(delta * engineSampleRate_)`
- `ClipRenderCore.cpp:729`: `const double visualSampleRate = engineSampleRate_;` — keep the adjacent comment, updated: `// engine-derived; cancels against clipLengthSamples for pixel mapping`

`ClipPanelKnobBridgeCore.h:92`:

```cpp
            out.sampleRate = (m_clip != nullptr && m_clip->sourceSampleRate > 0.0)
                ? m_clip->sourceSampleRate : 44100.0; // 0 = unknown on the model
```

- [ ] **Step 4: Build the app (Debug) — the tests project does not compile these files**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Expected: exit 0. No new unit tests for this task (UI/compiled-in editor code with no headless harness); the conversions are two-line arithmetic verified by review, and the audio-affecting path (automation region registration) is exercised by the app's existing automation behavior — recorded honestly in the final report.

- [ ] **Step 5: Checkpoint (NO COMMIT)**

---

### Task 8: Regression gates + registries + final report

**Files:**
- Read: `My DAW/DAW_Core/evidence/runs/**/manifest.json` (produced by test runs)
- Modify: `APEX_FIX_REGISTRY.md`, `APEX_TEST_REGISTRY.md` (workspace root — inspect format first, append entries matching it)
- Create: `docs/superpowers/reports/2026-07-25-sample-rate-buffer-expansion-final-report.md` (workspace root `docs/`)

**Interfaces:**
- Consumes: all previous tasks.
- Produces: the work-order §10 final report; registry entries per Brain §42 (engineering memory).

- [ ] **Step 1: Full test suite, both configurations**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026`
Run: `powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026`
Expected: both exit 0; manifests show zero failed assertions; new suites present: `device.capabilities.v1`, `device.config-validation.v1`, `callback-audit-period.v1`, `drumsampler.rate.v1`, `clip.timing-defaults.v1`, `metering.lufs-kweighting-rates.v1`; all pre-existing suites green (regression).

- [ ] **Step 2: Full builds, both configurations**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned`
Run: `powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned`
Expected: both exit 0; record output binary SHA-256 hashes for the report.

- [ ] **Step 3: Policy gates**

Run: `powershell -ExecutionPolicy Bypass -File Scripts\test_repository_policy.ps1`
Run: `powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1`
Run: `powershell -ExecutionPolicy Bypass -File Scripts\test_validate_test_evidence.ps1`
Expected: all exit 0.

- [ ] **Step 4: Registry updates (Brain §42 — no commit)**

Inspect the entry format at the top of `APEX_FIX_REGISTRY.md` and `APEX_TEST_REGISTRY.md`, then append:
- Fix registry: one entry per fixed defect (device-list authority, DrumSampler rate+resample, MidiClip default/rate-change, LUFS K-weighting, ArrangementEditor conversions, B4 ring drain), each with file:line evidence and test links.
- Test registry: the six new suites with their coverage statements.
Do NOT commit.

- [ ] **Step 5: Final report (work-order §10 format)**

Write the report with: (1) files changed; (2) sample-rate assumptions found (C-class table + B-class catalog from the audit); (3) fixes made; (4) supported-rate enumeration behavior (device-authoritative ∩ professional set; WASAPI note; oddball rates logged-not-offered); (5) 32-buffer implementation (device-enumerated only, `32 (experimental)` label, EXPERIMENTAL_ULTRA_LOW_LATENCY); (6) B4 corrections (pure period math, adaptive drain decoupled from the 5 s report, lateDeliveries jitter-semantics documentation — semantics unchanged); (7) test evidence (suite list + manifest paths); (8) build evidence (exit codes + SHA-256 of both app binaries); (9) remaining hardware-only verification (Apollo Solo matrix: 48k 256→128→64→32, then 96k 256→128→64→32 where the driver offers it, capturing deadlineMisses, engineOverruns, lateDeliveries, ringOverflows, maximumConsecutiveMisses, percentiles, intervalMaximum, CPU, xruns, stage timing); (10) status of 32 samples: **IMPLEMENTED / HARDWARE_UNVERIFIED**. Explicitly state: no combination is hardware-VERIFIED.

- [ ] **Step 6: Checkpoint (NO COMMIT) — hand the hardware matrix to the user**

---

## Self-Review Notes (completed by plan author)

- **Spec coverage:** spec §4.1→Task 1-2; §4.2→Task 4; §4.3→Task 5; §4.4→Task 6; §4.5→Task 7; §4.6→Task 3; §4.7→regression evidence (existing suites + gates, Task 8; propagation path unchanged by design); §5 test mapping→Tasks 1/2/3/4/5/6 tests + Task 8 gates; §6 gates→Task 8; §8 report→Task 8. Work-order §7 items covered by new tests: period math, device fallback/rejection, DrumSampler, LUFS, clip defaults, B4 deadline; items covered by existing suites re-run: recording, PDC/click RT, step-sequencer determinism, blade/split, writer integrity; items covered by code evidence (already rate-derived, unchanged code): transport conversion, metronome timing, fades, PDC samples, plugin prepare propagation (prepareRenderGraph path untouched).
- **Placeholders:** none — every code step contains the code. The two documented "read at edit time" points (exact `arrangement_` lambda body at MainComponent:6015-6019; renderer container name beside ArrangementViewCore.cpp:1093; panel member name inside ClipPropertiesWindowCore:563-640) name the exact location and the exact rule to apply.
- **Type consistency:** `setSampleRate(double)` uniform across DrumSamplerVoice/Pool/Engine.prepare, MidiClip, ClipAutomationPanel(s), ClipPropertiesPanelCore/WindowCore, ClipRenderCore. `formatBufferSizeLabel`/`isExperimentalUltraLowLatency`/`intersectProfessionalRates`/`filterSupportedBufferSizes` identical between Tasks 1-2 and their tests. `computeCallbackPeriodTicks`/`computeAuditDrainIntervalSeconds`/`kCapacity` identical between Task 3 impl and tests.
- **Commit steps:** intentionally absent (user directive). Checkpoints replace them.

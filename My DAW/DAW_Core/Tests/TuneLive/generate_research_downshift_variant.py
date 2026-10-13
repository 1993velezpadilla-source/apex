#!/usr/bin/env python3
"""Generate a RESEARCH-ONLY DSP twin from the exact singer-approved core.

Do not edit, replace, or configure the production DspCore. The generated file
is in the CI workspace only, and can be audited by diff of a handful of
deliberately anchored replacements. Its compensation is disabled by default,
and the offline renderer must prove that its disabled output is bit-identical
to the pinned production HQ40 WAV before analyzing the enabled variant.

Never add this output header to the JUCE application or shipping plugin build.
"""
from pathlib import Path
import sys

core = Path(__file__).resolve().parents[2] / "Source/TuneLiveCore/ApexTuneLiveDspCore.h"
output = Path(__file__).resolve().parent / "_generated_research_downshift_dsp.h"
original = core.read_text(encoding="utf-8")
modified = original


def exactly_one(old, new):
    global modified
    count = modified.count(old)
    if count != 1:
        raise RuntimeError(f"Expected unique production DSP anchor, found {count}: {old[:100]}")
    modified = modified.replace(old, new, 1)


exactly_one('#include "ApexTuneLiveVoiceControl.h"',
            '#include "../../Source/TuneLiveCore/ApexTuneLiveVoiceControl.h"')
exactly_one("namespace APEX { namespace TuneLive {",
            "namespace APEX { namespace TuneLiveResearch {\nusing namespace APEX::TuneLive;")
exactly_one("class DspCore final {", "class ResearchCompensatedDspCore final {")
exactly_one(
    "    void prepare(double sampleRate,int maxBlockSize,int numChannels,",
    """    // Explicitly non-shipping opt-in for OFFLINE research only. OFF is
    // required to reproduce shipping output with a byte-identical WAV.
    void setResearchCompensationEnabled(bool value) noexcept {enabled_=value;}
    void prepare(double sampleRate,int maxBlockSize,int numChannels,""")
exactly_one(
    "            stretch_.setTransposeFactor(std::pow(2.0f,smooth_/12.0f));",
    """            const float corrected=enabled_?experimentalDownshift(smooth_,frequency_):smooth_;
            stretch_.setTransposeFactor(std::pow(2.0f,corrected/12.0f));""")
exactly_one("private:\n    PitchTracker tracker_;",
"""private:
    // Measured direct-shifter transfer inverse. These knots apply ONLY to
    // stationary synthetic A3/A4, +25/+30c, 48kHz, 40ms, not human voice.
    // Avoid extrapolating either shift amount or vocal pitch to A2/soprano.
    // Smooth gate at the requested-cents lower bound to prevent hard jumps.
    // This is not proposed as a production calibration model!
    static float experimentalDownshift(float semitones,float hz) noexcept {
        if(!std::isfinite(semitones) || !std::isfinite(hz) ||
           semitones>=0.0f || hz<210.0f || hz>460.0f)return semitones;
        const float desired=-semitones*100.0f;
        if(desired<7.0f || desired>32.0f)return semitones;
        constexpr float observedA3[5]={5.0f,9.0f,15.0f,22.0f,30.0f};
        constexpr float observedA4[5]={7.0f,14.0f,25.0f,40.0f,55.0f};
        constexpr float requested[5]={20.0f,30.0f,40.0f,50.0f,60.0f};
        const auto invert=[&](const float* observed) noexcept {
            const float amount=std::clamp(desired,observed[0],observed[4]);
            for(int i=1;i<5;++i) {
                if(amount<=observed[i]) {
                    const float mix=(amount-observed[i-1])/
                                    (observed[i]-observed[i-1]);
                    return requested[i-1]+mix*(requested[i]-requested[i-1]);
                }
            }
            return requested[4];
        };
        const float a3=invert(observedA3),a4=invert(observedA4);
        const float registerMix=std::clamp(std::log2(hz/220.0f),0.0f,1.0f);
        const float corrected=a3+(a4-a3)*registerMix;
        // Small correction does not justify huge speculative correction.
        // Fade near unmeasured note and detuning boundaries.
        const float lower=std::clamp((desired-7.0f)/6.0f,0.0f,1.0f);
        const float upper=std::clamp((32.0f-desired)/2.0f,0.0f,1.0f);
        const float registerLow=std::clamp((hz-210.0f)/10.0f,0.0f,1.0f);
        const float registerHigh=std::clamp((460.0f-hz)/10.0f,0.0f,1.0f);
        const float strength=lower*upper*registerLow*registerHigh;
        return -((1.0f-strength)*desired+strength*corrected)*0.01f;
    }
    bool enabled_=false;
    PitchTracker tracker_;""")

if original == modified:
    raise RuntimeError("No research-only modifications made")
output.write_text("// GENERATED OFFLINE CI EXPERIMENT; NEVER SHIP.\n"+modified,
                  encoding="utf-8")
print(f"RESEARCH_GENERATED_VARIANT src={core} output={output}")
print("Invariant: production source bytes were READ ONLY; generated variant opt-in OFF.")
print("Guard: renderer must verify OFF matches real HQ40 PCM24 exactly.")
if len(sys.argv)>1:
    raise SystemExit("No CLI args expected")

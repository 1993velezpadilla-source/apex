APEX Tune Live Research Monitor — WINDOWS TEST BUILD — NOT THE APEX DAW

PURPOSE
- Isolated, explicitly opt-in listening prototype for APEX Tune Live's
  causal, fast-hop, dual-consensus, half-period harmonic research.
- It does NOT change APEX DAW's production HQ40 engine or its 40ms PDC.
- This app is a stand-alone JUCE8 research program, not a VST3 or APEX insert.
- No official Antares-like <=1ms total corrected-voice latency is claimed.

FIRST RUN — SAFETY
1. CONNECT HEADPHONES and begin with the headphone volume very low.
2. In Apollo Console, disable direct/hardware microphone monitoring.
   Otherwise you'll hear a separate DRY copy alongside the processed voice.
3. Select the desired Windows duplex audio device and microphone channel
   with the device selector. This preview uses Windows audio backends such
   as WASAPI. The Apollo Solo ASIO round-trip gate requires the full APEX
   host and the physical interface.
4. Check "Headphones connected; direct monitoring disabled".
5. Press START. The app emits only the processed voice, approximately
   35% of full-scale output gain. It does not provide a safety limiter.
6. Press STOP to silence monitoring and save the original DRY signal
   as a mono 24-bit WAV. Location:
   Documents/APEX Tune Live Research Takes/
   Duration is capped at 60 seconds per START/STOP cycle; subsequent
   monitoring continues but recording beyond that limit stops.
7. Use the saved DRY WAV inside APEX DAW for actual multitrack work.
   The experimental output is not used to overwrite the recorded dry take.

TECHNICAL LIMITS
- Original voice capture and audition are separate from APEX session routing.
- The native plugin, session automation, ASIO mic-to-ear roundtrip, and full
  musical quality are NOT certified.
- Pitch correction is hard-tune to A440 chromatic for this prototype.
- The test reports the first detected note and the corrective mix timing.
  The Windows driver, interface and headphone path have their own latency.
- The output may change formants, consonants and attacks.
- Never enable loudspeaker monitoring or use this for paid session takes.

DIAGNOSTICS
- Run TuneLiveResearchMonitor.exe --self-test for a synthetic offline sanity
  check. It must pass without opening microphone or output devices.
- Research development lives in GitHub PR #123; NOT merged into main.

# APEX Fix Registry

**Last updated:** 2026-10-03. FIX-001…014 remain historical and were not all re-verified on this date.

## Known Fixes in Codebase

### FIX-001 Bug 17: Sample Rate Mismatch on Device Restart
- **File:** `AudioEngineCore/AudioEngine.h:336-342`
- **Description:** Re-prepare ALL per-clip DSP cores at new sample rate on device restart
- **Root Cause:** Cores created at 44100 Hz retained stale calibration when device reopened at 48000 Hz

### FIX-002 Bug 16: Stale DSP State After Release/Prepare Cycle
- **File:** `AudioEngineCore/AudioEngine.h:385-400`
- **Description:** Clear ALL per-clip DSP state on device close
- **Root Cause:** Cores survived release/prepare cycle carrying stale ring-buffer and filter state

### FIX-003 Bug 46: PDC Sidechain Heap Allocation in Audio Callback
- **File:** `AudioEngineCore/AudioEngine.h:839-843`
- **Description:** Pre-allocated scratch buffers for PDC sidechain reads
- **Root Cause:** Heap allocation inside audio callback violated RT-safe contract

### FIX-004 Transport-Start Snap Zipper
- **File:** `AudioEngineCore/AudioEngine.h` (resetAllClipDSPState)
- **Description:** Reset all clip DSP state on transport discontinuity to prevent snap-on-start glitches

### FIX-005 Auto-Arm Track 0 Overwrite (Bug #1)
- **File:** `RecordingCore/RecordingEngine.h:176-208`
- **Description:** Auto-arm first track with NO existing clips instead of always track 0
- **Root Cause:** Always arming track 0 would record over imported beats

### FIX-006 ASIO4ALL Buffer Size Crash
- **File:** `AudioEngineCore/AudioEngine.h:296-299`
- **Description:** Pre-allocate worst-case block size (8192) to prevent crashes on driver reconfiguration

### FIX-007 Device Panel Not Device-Authoritative (2026-07-25 sample-rate expansion)
- **File:** `DeviceCore/DevicePanelModelCore.h`, `DeviceCore/DeviceCapabilityCore.h` (new), `DeviceCore/DeviceSessionCore.h`, `UICore/AudioDevicePanelUI.h`
- **Description:** Rate/buffer combos now enumerate the actual device (fast path: open device; probe: temp device, cached per panel session), intersected with the professional set {32000..192000} and ladder {32..2048}; 32 shown as `32 (experimental)`; session validate rejects values the device did not report; empty lists = advisory fallback
- **Root Cause:** Hardcoded lists `{44100,48000,88200,96000}` / `{64..2048}` were the sole selectable source; 32000/176400/192000 and 32-sample buffers were unselectable
- **Tests:** `device.capabilities.v1`, `device.config-validation.v1`

### FIX-008 DrumSampler Hardcoded 44.1 kHz + No Pad Resampling
- **File:** `DrumSamplerCore/DrumSamplerVoice.{h,cpp}`, `DrumSamplerVoicePool.{h,cpp}`, `DrumSamplerEngine.h`, `MainComponent.cpp` (prepare call site)
- **Description:** Voices derive ADSR from the actual engine rate and resample pads by fileRate/engineRate with linear interpolation (RT-safe, no alloc/lock); engine prepare propagates on every device (re)start
- **Root Cause:** `sampleRateRatio = 1.0f` and `* 0.001f * 44100.0f` — pads played wrong pitch/speed when file rate != engine rate; envelope times wrong at any rate != 44.1k
- **Tests:** `drumsampler.rate.v1`

### FIX-009 MidiClip Hardcoded Default Length + No Device-Rate Path
- **File:** `MidiCore/MidiClip.{h,cpp}`, `ClipCore/Clip.{h,cpp}`, `MidiCore/MidiInputCore.h`, `MainComponent.cpp` (prepareToPlay re-sync)
- **Description:** Default MIDI clip = 8 s at the engine rate (was 352800 samples pinned to 44.1k); `setSampleRate` rescales seconds-preserving; device-rate re-sync runs synchronously inside the safe `prepareToPlay` lifecycle (message thread, no RT mutation)
- **Root Cause:** 44.1k-pinned default length; no update path on device rate change
- **Tests:** `clip.timing-defaults.v1`

### FIX-010 LUFS K-Weighting Approximated at Non-48k Rates
- **File:** `MeteringCore/LufsMeterCore.h`
- **Description:** 48 kHz keeps the published BS.1770 coefficients verbatim; all other rates derive both biquads from the BS.1770 analog prototype (bilinear/tan-form; RLB numerator pinned [1,-2,1]) — validated to reproduce the published 48k constants <=5e-4 and to read -23.0 LUFS (+-0.3) on the reference sine at all 7 professional rates x {32,256} blocks
- **Root Cause:** RBJ 1500 Hz/+4 dB/Q.707 shelf and 38 Hz/Q.5 HP approximations used at every rate != 48k — not the standard filters
- **Tests:** `metering.lufs-kweighting-rates.v1`

### FIX-011 ArrangementEditor Hardcoded 44100 Conversions
- **File:** `Builds/VisualStudio2026/ArrangementEditor/ClipAutomationPanel.h`, `ClipPropertiesWindowCore.h`, `ArrangementViewCore.{h,cpp}`, `ClipRenderCore.{h,cpp}`, `ClipPanelKnobBridgeCore.h`
- **Description:** Engine-rate plumbed from `ArrangementViewCore::m_engineSampleRate` into automation panels (region seconds->samples targets — audio-affecting), clip renderers (peak hint, trim tooltip, fade draw), knob bridge now reads `sourceSampleRate` (fallback 44100 when unknown)
- **Root Cause:** `clip_.startTime/length * 44100.0`, `out.sampleRate = 44100.0 // TODO`, `visualSampleRate = 44100.0`
- **Tests:** build-verified (no headless harness for the editor UI); remaining 44100 occurrences are documented fallbacks only

### FIX-012 B4 Audit Ring Drain Cadence Fires ringOverflows by Design at Low Buffers
- **File:** `DiagnosticsCore/CallbackAuditCore.h`, `MainComponent.{h,cpp}`
- **Description:** Pure `computeCallbackPeriodTicks` (device-derived) + `computeAuditDrainIntervalSeconds` (clamp(0.5xcapacityxperiod, 0.05, 5.0)); dedicated adaptive drain timer decoupled from the 5 s report; lateDeliveries documented as strict delivery-jitter metric (semantics unchanged)
- **Root Cause:** 1024-slot ring drained on a 2 s watchdog tick behind a 5 s guard — at 48k/64 (750 cb/s) ~2726 overflows per cycle; counter measured cadence, not a stalled drain
- **Tests:** `callback-audit-period.v1`

### FIX-013 I/O Topology Change Silently Clamped (cross-vendor audit, item 20)
- **File:** `MainComponent.cpp` (inputWatchdogTick, :6807-6821)
- **Description:** The watchdog now consumes `needsInputBufferReprepare_` and drives the proven `ensureInputChannelsActive("topology-change")` restart path so mid-session I/O changes (S/MUX, custom I/O matrix) are adopted; guarded against panel ownership and recording
- **Root Cause:** Flag was set in the realtime callback but never consumed — topology changes were silently clamped until the next device restart
- **Tests:** four gates green (Debug/Release builds + tests, 2026-07-26T00:24/00:29Z)

### FIX-014 Project Sample-Rate Identity (pre-beta project integrity, audit item 22)
- **File:** `ProjectCore/ProjectSampleRateReconcileCore.h` (new), `ProjectCore/ProjectManager.{h,cpp}`
- **Description:** Projects persist the authoritative device-granted sample rate (only when proven); on reopen, a pure plan decides: none / seconds-preserving reconcile / legacy-unverified / device-rate-unknown. Reconcile scales clips (AudioClip engine-domain fields; MIDI via the tested setSampleRate path), transport position, and markers; PPQ automation is untouched by construction; LoadRateReport exposed for the GUI phase
- **Root Cause:** Project files carried no sample-rate identity — sample-domain timelines were silently reinterpreted at whatever rate the device happened to run
- **Tests:** `project.sample-rate-reconcile.v1` (11 cases: same-rate, 44.1->48, 48->96, 96->48, legacy, unavailable device rate, clip/MIDI time-correctness, clamps, no-op guards)

## Actualización parcial verificada — 2026-10-03

Las entradas FIX-001…014 son históricas; no se reejecutaron todas durante esta auditoría. La fotografía actual, backlog y límites de verificación están en [APEX_PROJECT_AUDIT.md](APEX_PROJECT_AUDIT.md).

### FIX-015 Reordenamiento de plugins: ganancia manual e identidad de automatización

- **Trigger:** editar valores manuales y reordenar inserts; caída permanente de volumen y posibles escrituras a instancias vecinas.
- **Causa:** suavizado realimentado desde historial obsoleto sin lane activo y bindings/keys de automatización asociados al slot anterior.
- **Archivos:** `My DAW/DAW_Core/Source/PluginHostCore/PluginInstanceCore.h:1675`; `PluginChainCore.h:1025`; `Automation/AutomationParameterKeyCore.h:183`; `AutomationCore/AutomationManagerCore.h:59`; `CommandCore/GeneralCommands.h:170`; `PluginHostCore/MixerPluginSidePanel.h:97`; `MainComponent.cpp:3325`; `AppCore/ApplicationCore.h:208`.
- **Conducta:** mover las mismas instancias, parámetros y lanes; Undo/Redo no recrea plugins; escrituras DSP al parámetro local correcto.
- **Prueba:** `plugin.reorder.gain-preservation.v1`, 40 comprobaciones dentro de PluginHost. Antes salida0.0125821 frente a0.25 (~−25.96dB), 12 fallos; después PluginHost417/0 en Debug y Release.
- **Estado:** FOCUSED_VERIFIED / RUNTIME_PENDING; continuidades/tails/latencia y sesión comercial pendientes. Las puertas globales no están aprobadas.
- **Identidad:** HEAD core `000816f2d1338d5e97728fdacb72c75e0fe9c24a` + patch local [plugin-reorder-fix.patch](docs/apex-audit/evidence/plugin-reorder-fix.patch); ver hashes en [audit-evidence.json](docs/apex-audit/audit-evidence.json).

### FIX-016 APEXTests: vida de Theme entre suites

- **Trigger:** batería de pruebas crea/destruye inicialización GUI entre suites; lectura inválida al crear un strip del mixer.
- **Causa:** JUCE DeletedAtShutdown elimina Theme pero su singleton conserva el puntero. Excepción `0xC0000005`; primer frame `MixerStrip::initChildren`, `Source/UICore/MixerPanel.h:2527`.
- **Cambio:** `My DAW/DAW_Core/Tests/Source/Main.cpp:93`, `guiLifetime`, vive durante todo el runner.
- **Verificación:** la nueva corrida amplia pasó el lugar del crash; Mixer fader42/0 en Debug y Release. La corrida amplia no terminó por benchmarks y registró otros fallos; no se afirma ausencia universal de crashes.
- **Estado:** FOCUSED_VERIFIED; logs de diagnóstico preservados en `docs/apex-audit/evidence`, dump original en core `.apex-debug/plugin-reorder/full-suite-crash.dmp`.

### FIX-017 Indicador de clip numérico y Trim/VU de gain staging

- **Trigger:** el clip box de canales/master mostraba `...`; el reset del master no limpiaba el latch; las barras variaban con el bloque y el VU de Trim no concordaba con la entrada de micrófono.
- **Causa corregida:** el texto vivía en un carril demasiado estrecho y la UI reutilizaba un hold visual como si fuera un evento nuevo. El pico de audio se atenuaba `0.92` una vez por bloque, haciendo que la respuesta dependiera del buffer. El meter de Trim no seguía de forma consistente la señal post-Trim/pre-FX que alimenta los plugins; la entrada hardware se selecciona aparte únicamente cuando está armada pero no monitoreada.
- **Cambios:** `My DAW/DAW_Core/Source/TrackCore/Track.h` guarda máximo pendiente por ventana UI, exceso sobre 0 dBFS y sanitiza muestras inválidas; `Source/AudioEngineCore/AudioEngine.h` publica pico crudo por ventana y solo cede la VU a micrófono en ruta armada sin monitoreo; `Source/ChannelStripCore/LevelMeter.h` desacopla clip del peak hold y fija verde (<−6 dBFS), amarillo (−6…0) y rojo (≥0); `Source/UICore/MixerPanel.h` muestra `+x.x dB` en un box 48×14 y conecta reset en strips normales/Master.
- **Trim/VU:** `Source/InputMonitorCore/TrackInputProcessorCore.h` mide el canal real post-Trim/pre-FX; en ruta armada sin monitoreo, `LiveInputMonitorEngine.h` mide el hardware en una copia con el target de Trim, sin avanzar otra vez el suavizador. `InputMeterCore.h`, `AnalogVuMeterComponent.h`, `AnalogVuBallisticsCore.h`, `CompactVuNeedle.h` y `InputTrimFloatingPanel.h` separan VU/RMS de peak dBFS y fijan `0 VU = −18 dBFS` para seno de referencia. `Track::setInputTrimDb` notifica el cambio para estado/autosave.
- **Pruebas:** `mixer.clip-input-meter-signal.v1`, Debug x64, 8 casos, 0 fallos; incluye VU +6 dB al subir Trim +6 dB y preview de micrófono armado/no monitoreado. Confirma Track state snapshot, no un ciclo completo de guardar/reabrir proyecto. Test SHA-256 `11A1A516E554FB3C28A4D2A2812AFF6611BC0E34A7D5F07BFA915DED927507C5`; App SHA-256 `B98C221BD66954BEB361F5D8EB58D7D0DB6450450C616B01C26F1686877959A9`. Transcript e informe en `docs/apex-audit/evidence`.
- **Patch:** [clip-vu-gain-staging-fix.patch](docs/apex-audit/evidence/clip-vu-gain-staging-fix.patch), SHA-256 `2A6285E74BE59181DD7BBE6AC52766D7A1E9C2BC22181CFA704D66FF5CF44573`; contiene los archivos de FIX-017 y pasa `git apply --reverse --check` contra el árbol actual.
- **Estado:** FOCUSED_VERIFIED / RUNTIME_PENDING. Sin prueba física de micrófono/interfaz o proyecto del usuario; Release y batería completa no re-ejecutados. El gate de dependencias sigue con advertencia Git de Signalsmith.

### FIX-018 Buffer 2048: reapertura innecesaria y ruido aún sin resolver

- **Trigger:** Windows Audio, buffer 2048, Apply y playback inmediato; el usuario sigue reportando ruido tipo zipper después del cambio anterior.
- **Corrección verificada, alcance limitado:** `My DAW/DAW_Core/Source/DeviceCore/DeviceCapabilityCore.h` y `Source/MainComponent.cpp` evitan tratar la capacidad solicitada de hasta 64 entradas como si fuera el número de entradas activas concedidas por el driver. Esto evita un segundo `setAudioDeviceSetup` cuando Windows Audio ya concedió 1–2 entradas válidas. Las pruebas confirman ese comportamiento del dispositivo; **no demuestran que esa reapertura fuera la causa del ruido reportado ni que el ruido haya desaparecido**. La afirmación causal de la versión anterior fue prematura.
- **Pruebas previas:** `APEX.Device` pasó 51 aserciones en Debug y Release; `routing-buffer.prepare.v1` pasó 15 y `plugin.automation.large-block-smoothing.v1` 11 en Debug. Esas pruebas no renderizan el escenario auditivo exacto del usuario. Release app compiló/enlazó con firma omitida; el build Release de toda la solución de pruebas falló por C1002 del compilador en `PluginSandboxPhaseE2CTests.cpp`.
- **Nueva evidencia de investigación:** `My DAW/DAW_Core/evidence/track-reprepare-2048-case-Debug-2026-10-05.{json,txt}`: el clip normal con plugin de ganancia de prueba continúa en el primer bloque de 2048 (RMS 0.141; plugin input RMS 0.200; cero infracciones; máximo salto entre muestras 0.0125 dentro del flujo reanudado). En la secuencia de Apply mientras está parado y Play inmediato, RMS del primer bloque = 0.114 y no hay infracciones. El clip con time-stretch sí cae a RMS ≈0 durante tres bloques y recupera nivel en el cuarto; aún no establece que la sesión del usuario use stretch ni que explique su ruido. No se usaron plugins comerciales ni hardware Realtek. La corrida completa de esta suite termina con 4,512 pases y dos fallos: el cursor de clip hot-play-stop y la audibilidad del primer bloque del test de stretch a 1024.
- **Estado:** ISSUE OPEN / ROOT CAUSE UNCONFIRMED. No declarar FIX-018 resuelto hasta que el render de 2048 y la reproducción Windows Audio/Realtek relevantes no presenten el ruido.

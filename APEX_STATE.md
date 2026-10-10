# APEX Repository State

**Actualización:** 2026-10-05 · **Validación:** enfocada; puertas generales pendientes.

Referencia actual: [APEX_PROJECT_AUDIT.md](APEX_PROJECT_AUDIT.md). Detalles de arquitectura, persistencia, prioridades y evidencia: [docs/apex-audit](docs/apex-audit/).

## Actualización Trim / VU

El Trim ahora mide VU sobre cada muestra de audio: rectificación de onda
completa y movimiento de segundo orden, 99% en unos 300 ms. Sustituye el RMS
del último bloque más animación de interfaz. Referencia ajustable (por defecto
0 VU = -18 dBFS para seno), lectura actual/máxima VU, pico dBFS separado y
reset de máximos. Referencia y modo de canal se guardan por track. El gain
Trim sigue pre-FX/pre-fader y su calibración visual no cambia la ganancia.

También se eliminó una segunda alimentación silenciosa del medidor desde
LiveInputMonitorEngine para los canales que ya mide el grafo. El modo armado
sin monitoreo conserva su preview. La regresión VU pasó en Debug y Release,
incluyendo buffers hasta 2048, calibración, ráfagas, estéreo, reset y XML.
Por configuración también pasaron 36 assertions de señal/meter/Trim, 25,417
de DSP y 66 de grabación. App y tests Debug/Release compilados; Release SHA-256:
`8E6591671A5605505DF110CFCE6FF944F389E36DC0787C89619C98BF2D7F10BC`.
La corrección WASAPI de 2048 conserva el mismo hash de fuente. Alcance y evidencia:
[Trim / VU](My%20DAW/DAW_Core/evidence/trim-classic-vu-2026-10-05.md).

## Actualización 2048 / Windows Audio (anterior al cambio de VU)

Se reprodujo el vaciado de la cola de salida en Speakers (Realtek(R) Audio):
el callback de 2048 muestras a 48 kHz tenía 42.67 ms de presupuesto, pero la
cola compartida de Windows solo cubría 1056 muestras (22 ms). Con 29.867 ms
de trabajo por bloque hubo 24 vaciados en la prueba controlada; al dimensionar
la cola para el bloque más un período de Windows, hubo cero. La corrección
vive en el backend WASAPI fijado por el parche de dependencia 0004.

Regresó el menú 32/64/128/256/512/1024/2048. El backend real pasó 14/14 casos
de salida con esos tamaños a 44,100 y 48,000 Hz; 2048 pasó además 12 segundos
por tasa con carga del 85%. Se reconstruyeron aplicación y tests Debug/Release;
cada configuración pasó 51 assertions de dispositivo y 25,417 de DSP por
bloques. El Release habitual tiene SHA-256
`F1C1B868226C61835962B096102AF9967088616F4E2C80CC77E3799398232916`.

Esto reemplaza la limitación anterior sobre falta de acceso al hardware de
salida: se pudo medir mediante WASAPI nativo. No acredita la canción del
usuario ni grabación física; el probe no pudo abrir el micrófono. La puerta
global de dependencias sigue sin certificar; el parche individual sí se
reconstruyó y coincidió byte por byte con el SDK instalado. Detalles, fuentes,
comandos y límites:
[informe 2048](My%20DAW/DAW_Core/evidence/wasapi-shared-buffer-2048-root-cause-2026-10-05.md).

## Identidad y alcance

- Workspace oficial: `C:\Users\1993v\OneDrive\Desktop\Apex backup`.
- Repositorio de aplicación: `My DAW/DAW_Core`.
- Rama core: `feature/apex-windows-baseline-evidence`.
- HEAD core: `000816f2d1338d5e97728fdacb72c75e0fe9c24a`.
- HEAD exterior: `9538337efb49155a37ec47a2af30ee5cc4dfb70f`; es otro historial Git.
- Framework: JUCE / Projucer → Visual Studio 2026, Windows x64.
- Lenguaje efectivo: C++17 (`stdcpp17` en app/test vcxproj). El antiguo C++20 del registro no correspondía a estos flags.
- Source: 104 directorios inmediatos y 901 archivos físicos, incluidos recursos y terceros. Projucer lista 447 archivos: 290 Source y 157 ArrangementEditor; 37 cpp del editor se compilan.
- **Builds contiene fuente activa** en `Builds/VisualStudio2026/ArrangementEditor`; no limpiar globalmente.
- Brain canónico: archivo de raíz, SHA-256 `ce92abd24f6ed246004047a374cbf791dedc8fe5b2c2ee98d7154cfa6bc9a7ca`. El Brain dentro de core es un antecedente diferente.

## Cambios y pruebas actuales

- Corrección de plugin reorder/manual gain/automation identity/Undo en 9 archivos de implementación/regresión; décimo archivo conserva la vida GUI del runner para el crash de APEXTests. Cambios locales sin commit.
- Regresión previa: salida 0.0125821 frente a 0.25 esperada, ~−25.96 dB; 12 fallos antes del cambio.
- PluginHost Debug y Release: 417 comprobaciones correctas y 0 fallos en cada configuración.
- Mixer fader Debug y Release: 42 correctas y 0 fallos en cada configuración.
- Para plugin reorder: aplicación/tests Debug y Release compilados, unsigned; evidencia preservada en `docs/apex-audit/evidence`. Para FIX-017 de clip/Trim/VU: App y tests Debug y Release compilados; la suite enfocada de medidor pasó 8/8 en cada configuración. No se ejecutó full run Release.
- FIX-017 de mixer: `mixer.clip-input-meter-signal.v1` pasa 8 casos en Debug y Release; los builds de app Debug y Release pasan. Release app SHA-256 EEE64C936367120B20BAD1E0B29CB654109B34D3CDA937FD0F61DC789C41231F; Release tests SHA-256 E11F977EA2AE8643ECAF9EB98BBB7D23B7BB7744EC1D3C2E9B294D5B576FC9B8. Transcript: docs/apex-audit/evidence/clip-input-meter-signal-Release.txt. Transcript, hashes y alcance en `docs/apex-audit/evidence/clip-vu-meter-verification-Debug.txt` y `clip-input-meter-signal-Debug.txt`.
- Patch recuperable de FIX-017: `docs/apex-audit/evidence/clip-vu-gain-staging-fix.patch`, SHA-256 `2A6285E74BE59181DD7BBE6AC52766D7A1E9C2BC22181CFA704D66FF5CF44573`; `git apply --reverse --check` pasa contra el árbol actual.
- El Trim sigue siendo gain staging pre-fader/pre-FX: el VU sigue la señal del canal tras Trim (clips, sends entrantes y hardware monitoreado). En track armado sin monitoreo muestra una copia del micrófono post-Trim porque ese input no entra al buffer del canal. `0 VU = -18 dBFS` para seno de referencia; peak máximo aparte en dBFS. El setter notifica el cambio y el test confirma el Track state snapshot; no hubo ciclo completo de save/reopen. El clip box muestra el exceso sobre 0 dBFS y se puede resetear en strips normales y Master.
- FIX-018 sigue abierto. El cambio de `setAudioChannels` evita una reapertura innecesaria si Windows Audio ya concedió entradas activas; las pruebas de Device verifican esa condición, pero no que fuera la causa del zipper ni que el síntoma se haya resuelto. La afirmación causal anterior fue prematura. A 2048, un clip normal con plugin de prueba pasa la reanudación enfocada (RMS 0.141, cero infracciones, salto máximo interno 0.0125); un clip time-stretch queda casi mudo tres bloques y recupera en el cuarto. Sin plugins comerciales ni hardware Realtek, el vínculo con el síntoma sigue sin probarse. Evidencia: `My DAW/DAW_Core/evidence/track-reprepare-2048-case-Debug-2026-10-05.{json,txt}`.
- No se probó una interfaz/micrófono físico ni una canción del usuario; el gate de dependencias y la batería completa siguen pendientes.
- Falta sesión real con plugins comerciales; el estrés unit-gain no acredita continuidad de efectos/tails durante un reorder.

## Pendientes que guían el próximo desarrollo

Primero reproducir y corregir preservación de plugins ausentes, save/load de patterns, revisiones y cambio de documento durante autosave, y publicación de plantillas. Reparar contadores/transcript/fixtures de evidencia; revisar allocations posibles de routing/PDC/live input en audio. El backlog AUD-01…12 y sus criterios de cierre están en la auditoría.

No está aprobado el conjunto global: `verify_dependencies` falló por ownership Git; build foundation por fixture de patches; la batería completa Debug registró fallos y fue interrumpida; no hay full run Release actual. Los manifests con contadores cero no sustituyen results JSON. No se declara que los riesgos nuevos ya causaron pérdida en canciones del usuario.

## Memoria y autoridad

La auditoría actualiza la fotografía documental de julio; mantiene el historial en registros/handoffs, no instala nuevas funciones en el Brain. Cada siguiente cambio debe registrar reproducción, causa, símbolos, patch/commit, resultados y límites.

- `BRAIN_PROSE != CURRENT_APEX_REPOSITORY_STATE`
- `BUILD_PASS != FEATURE_VERIFIED`
- `FOCUSED_TEST_PASS != GLOBAL_RELEASE_PASS`
- `IMPLEMENTED != VERIFIED`

# Auditoría de calidad, correcciones y verificación de Apex

Fecha de inspección: 3 de octubre de 2026, America/New_York. Auditoría de solo lectura de la copia oficial `C:/Users/1993v/OneDrive/Desktop/Apex backup`. No se ejecutaron builds ni la batería completa durante esta auditoría. Se leyeron AGENTS, el DAW Brain canónico (identidad de plugins y cadena de evidencia), registros, código cambiado, scripts, logs y resultados ya existentes. Los únicos checks nuevos fueron validaciones de manifiestos y hashes de binarios.

Para las referencias siguientes, **raíz** es la carpeta oficial; **core** es `My DAW/DAW_Core` dentro de ella. Las prioridades describen qué cerrar antes de afirmar que la versión está completamente validada; no constituyen autorización para cambiar el código.

## Estado comprobado

- Git exterior: HEAD `9538337`, con modificaciones y archivos no registrados anteriores a esta auditoría. El repositorio real de la aplicación tiene HEAD `000816f2d1338d5e97728fdacb72c75e0fe9c24a`, rama `feature/apex-windows-baseline-evidence`.
- Core: 10 archivos modificados, 483 líneas añadidas y 13 retiradas; son la corrección de reordenamiento y la vida GUI del runner. Los cinco resultados JSON de la corrección están sin registrar. No se hizo commit ni PR de estos cambios.
- Hay ejecutables actuales de aplicación y tests Debug y Release. La verificación del hash confirmó los mismos artefactos que los logs de compilación. Las aplicaciones son builds unsigned según los logs; compilar no acredita toda la conducta del DAW.
- Hay evidencia roja antes de la corrección y resultados verdes enfocados después. La batería completa actual no terminó y ya había reportado fallos.

## Correcciones recientes y evidencia

| Cambio comprobado en el código | Evidencia exacta | Verificación disponible |
|---|---|---|
| El valor manual del plugin vuelve a ser la autoridad cuando no hay un lane activo; se evita alisar desde historial viejo y realimentar el valor atenuado. Las escrituras DSP van al parámetro de esa instancia. | core `Source/PluginHostCore/PluginInstanceCore.h`, `applyAutomationAtSample`, líneas 1666–1707 | `evidence/plugin-reorder-before-Debug.json`: salida esperada 0.25, observada 0.0125821 (~−25.96 dB); cinco valores manuales 1.0 pasaban a 0.55. Después pasa la regresión. |
| El movimiento permuta las direcciones de automatización, bindings y buses junto con la instancia existente. No recrea ni re-prepara el plugin. | core `Source/PluginHostCore/PluginChainCore.h`, `moveSlot`, línea 1025; `AutomationParameterKeyCore.h`, `remapPluginSlots`, 183; `AutomationManagerCore.h`, `remapPluginSlots`, 59 | La regresión comprueba parámetros independientes, ParameterID y puntero vinculado, lane legacy/wet-dry, snapshot anterior y 200 movimientos concurrentes. |
| Los movimientos del panel y Undo/Redo usan una operación de mover las instancias. | core `Source/CommandCore/GeneralCommands.h`, `PluginChainMoveCommand`, 170; `MixerPluginSidePanel.h`, `requestSlotMove`, 97; `MainComponent.cpp`, callback 3325 | Casos enfocados de identidad y comando; la interfaz real con plugins comerciales todavía necesita prueba de sesión. |
| La aplicación proporciona al chain el manager actual de automatización. | core `Source/AppCore/ApplicationCore.h`, línea 208; `PluginChainCore.h`, `setAutomationManager`, 197, y lectura del snapshot dentro de `applyAutomationAtSample`, 973 | Caso con snapshot anterior al movimiento. |
| El runner conserva una vida GUI JUCE durante todos los suites. | core `Tests/Source/Main.cpp`, `guiLifetime`, 89–93 | Diagnóstico de la captura: excepción `c0000005`, lectura de `0000013e67aa4aec`, primer frame Apex `MixerStrip::initChildren` en `Source/UICore/MixerPanel.h:2527`. `Theme` es `DeletedAtShutdown` (`Theme.h:6`) y guarda un puntero singleton (`Theme.cpp:156`). Evidencia en `.apex-debug/plugin-reorder/full-suite-original-exception.txt:64`, `full-suite-fault-location.txt:64`, `full-suite-live-debug.txt:69`. Después el recorrido global pasó ese punto y las pruebas de fader pasaron; no prueba ausencia universal de crashes. |

Los archivos `evidence/plugin-reorder-PluginHost-Debug.json` y `...Release.json` contienen cada uno **33 grupos, 417 assertions passed, 0 failed**. De esos pases, la nueva regresión `plugin.reorder.gain-preservation.v1` aporta 40. Los archivos `plugin-reorder-mixer-fader-Debug.json` y `...Release.json` contienen **5 grupos, 42 passed, 0 failed** cada uno. El total enfocado es 459 por configuración; no equivale a ejecutar todo el repositorio.

## Hallazgos priorizados

### P1 — Los manifiestos publicados no conservan los resultados ni el transcript real

**Observado en los artefactos y confirmado en el script.** `core/Scripts/run_apex_tests.ps1:467–472` fija `resultGroups`, `assertionsPassed` y `assertionsFailed` en cero y deja `artifacts` vacío. Las líneas 501–503 escriben `stdout.txt` con una sola cabecera, sin capturar el proceso ejecutado en 406. No importa el JSON real aunque `-ResultsJson` se pase al runner en 336.

Ejemplo: `core/evidence/runs/PluginHost/20261003T210236Z-Debug-000816f/manifest.json` dice 0 grupos / 0 pases; el JSON real asociado dice 33 / 417. El `stdout.txt` asociado contiene solo `[test] APEXTests Debug`. Los resultados reales usados en la conclusión anterior sí existen; el manifiesto no los respalda por sí solo.

Se ejecutó el validador oficial en Windows PowerShell 5.1: acepta tanto ese manifiesto como `runner-smoke/20261003T205338Z-Debug-000816f`, que registra salida −1 y contadores en cero. Esta aceptación es de estructura; no significa que el proceso fallido pasó. `validate_test_evidence.ps1:330–335` únicamente exige cero fallos cuando exitCode es cero. El mismo script llamado directamente en PowerShell 7.6.5 rechaza exitCode/duration porque pide tipos `[int]`; la documentación exige Windows PowerShell 5.1, de modo que ese rechazo es una incompatibilidad fuera del camino documentado, no un fallo de las pruebas Apex.

**Trabajo recomendado:** producir resultados JSON siempre, verificar que hubo grupos/assertions, importar sus valores, capturar stdout/stderr y publicar todos los artefactos con tamaño/hash. Un crash o interrupción debe quedar `INCOMPLETE`, sin fabricarle cero fallos como resultado medido. El nombre de directorio final usa segundos/config/commit y puede sobrescribir un run del mismo segundo (`run_apex_tests.ps1:507–513`); incluir el runId evita perder evidencia.

### P1 — Las puertas de dependencias y suite completa no están verdes

**Observado.** Los logs de aplicación Debug y Release empiezan con fallo de ownership de Git en `signalsmith-stretch` y después imprimen `dependency verification failed - continuing build anyway`. El comportamiento está en `core/Scripts/build_apex.ps1:18–22`. El build compiló los artefactos; no acredita que el dependency gate pasó. No se modificó la confianza global de Git.

`core/evidence/plugin-reorder-build-foundation.log:7–25` falla porque el fixture no incluye `0002-apex-vst3-null-editcontroller-guard.patch`. `test_build_foundation.ps1:169–174` copia solo el patch original, pero `Dependencies/apex-windows-dependencies.json` declara compatibilityPatch, compatibilityPatch2 y compatibilityPatch3. Se trata de una discrepancia demostrable entre fixture y contrato. El log también informa que la cobertura de symlinks no está disponible por privilegios; no se debe convertir esa ausencia en PASS de esa cobertura.

`core/evidence/plugin-reorder-all-Debug.log` contiene fallos concretos: UI presentation ownership (901, 907), supervivencia de reprepare (1197: tres cursors donde esperaba cero; 1262: stretch inaudible en primer bloque), sandbox enum diagnostics (2186), milestones de PluginCreate (2201 en adelante), DenseE2B fixture ausente (2548 en adelante). Terminó interrumpido con código −1 después de entrar en benchmarks G10; no hay JSON final de resultados completo. No se observó un full run Release actual. Estos fallos necesitan clasificación como defecto de producto, test/oracle o entorno; esta auditoría no establece que fueran preexistentes ni que provengan de la corrección reciente.

**Causa estática confirmada de una parte de los fallos:** el runner corre todas las suites si no recibe filtro (`Tests/Source/Main.cpp:116–121`), pero el script solo arma DenseE2B cuando recibe Category/Name E2B (`Scripts/run_apex_tests.ps1:27–30, 267`), por lo que el camino completo no proporciona ese fixture.

**Trabajo recomendado:** reparar el fixture de build foundation, resolver ownership de los repositorios específicos sin un wildcard global, hacer que el camino de validación falle claramente si falta la puerta de dependencias, preparar DenseE2B cuando se corre todo, y separar benchmarks costosos de regresiones funcionales. Después ejecutar grupos fallidos con evidencia completa y solo entonces Debug/Release completos. Las reglas oficiales requieren exit 0 para todas las puertas.

### P2 — Los registros de estado y correcciones están desactualizados

**Observado.** `raíz/APEX_STATE.md:3,7,9` sigue fechado 24-julio, apunta al HEAD exterior `9538337` aunque su campo Repository señala al core, y afirma C++20. El contrato de dependencias dice C++17 y ambos `.vcxproj` de app/test usan `stdcpp17` (app líneas 82/130; tests 82/126). `APEX_BUILD_REGISTRY.md:3,33` conserva el resumen de julio y 120+ tests. `APEX_FIX_REGISTRY.md` termina en FIX-014 y no registra la corrección actual ni el crash del runner. `APEX_TEST_REGISTRY.md:141–162` afirma NO TEST para automatización, persistencia, plugin hosting, routing y mixer aunque hay suites en `Tests/Source/Diagnostics/AutomationRtAccessTests.cpp`, `ProjectPersistenceTransactionTests.cpp`, `RoutingBufferPrepareTests.cpp`, `PluginHost/PluginBusLayoutMatrixTests.cpp` y otras. La existencia de esas suites no demuestra cobertura completa.

**Trabajo recomendado:** renovar entradas con repositorio/commit correctos, estado `IMPLEMENTED / FOCUSED_VERIFIED / RUNTIME_PENDING / FAILED / INCOMPLETE`, IDs de test, hashes y enlaces de evidencia. Mantener fechas de historial; marcar lo antiguo STALE. No convertir los hallazgos del repositorio en texto canónico del Brain sin revisión independiente. DAW_BRAIN raíz 63100–63114 y 63681–63738 exige evidencia aplicable y cadena trazable.

### P2 — La regresión concurrente no cubre el bypass transitorio durante un movimiento

**Riesgo inferido de código, no defecto audible observado.** La nueva puerta niega lectores mientras se reconfigura. `core/PluginChainCore.h` (ruta completa `Source/PluginHostCore/PluginChainCore.h`) devuelve el buffer de entrada sin procesar en `processBlock:1280–1281`, `processBlockWithMidi:1372–1373` y `processBlockWithSidechain:1436–1437`. `ControlEditScope:123–133` mantiene la puerta cerrada mientras drena lectores y, en un plugin atascado, espera hasta dos segundos en el control plane. Esto evita esperar en el callback, pero pueden existir bloques secos durante el cambio.

El test de concurrencia usa seis gains 1.0; entrada y salida con bypass son iguales (`PluginBusLayoutMatrixTests.cpp:1341–1378`). Por eso demuestra que no aparece la pérdida permanente de esos gains y que no se re-preparan las instancias, pero no puede detectar saltos secos con reducción de ganancia, reverb, instrumentos, latencia o sidechain. Tampoco prueba duración de la operación bajo un plugin real lento.

**Trabajo recomendado:** añadir un escenario con ganancia no unitaria y uno con estado/tail/latencia, observar la transición y recuperación definida, y repetir arrastrar/subir/bajar/Undo/Redo en una sesión real con plugins comerciales. La corrección enfocada sigue sustentada; la continuidad audible y los límites de interacción permanecen pendientes.

### P2 — El estado modificado no tiene una identidad reproducible completa

**Observado.** Los manifiestos actuales conservan HEAD `000816f...` y applicationDirty=true, pero no un hash de diff; outerCommit es cero, os/cpu unknown. El script intenta el repositorio exterior desde `My DAW`, no desde raíz (`run_apex_tests.ps1:19,351–367`), y cae en commit cero al fallar. Un commit más dirty=true no identifica qué cambios fueron compilados. Hay un patch de la corrección en los outputs de este chat, pero aún no está enlazado desde el registro oficial y el manifest tiene artifacts vacío. `core/.gitignore` excluye evidence/runs, logs, dumps, binarios y autosaves: es válido separar datos generados, pero la evidencia importante requiere archivo durable explícito.

**Trabajo recomendado:** guardar patch/diff con hash y lista de archivos hasta que exista un commit revisado; registrar ambos Git roots explícitamente; enlazar evidencia preservada fuera de la limpieza de builds; capturar entorno real cuando sea posible y `unavailable` con causa cuando no lo sea. No usar all-zero como un commit comprobado.

## Orden de trabajo propuesto

1. Completar la trazabilidad de la corrección y renovar registros con estado actual y alcance exacto, preservando patch, resultados JSON, logs, EXE/PDB/dump relevantes.
2. Corregir publicación de evidencia y preparación de fixtures/gates; validar que una ejecución conocida mala y una interrupción no se publican como verificaciones completas.
3. Clasificar y cerrar los grupos fallidos actuales con escenarios cortos, reproducciones deterministas y datos completos.
4. Ampliar la prueba de reorder a efectos no unitarios/tails/PDC/sidechain y verificar la sesión del usuario; conservar el resultado de 417+42 por configuración como evidencia enfocada.
5. Ejecutar Debug/Release completos con benchmarks separados; solo promover a release evidence cuando dependencias, tests y campañas de hardware exigidas tengan evidencia suficiente.

## Hashes actuales comprobados

| Binario | SHA-256 |
|---|---|
| Application Debug | `B98C221BD66954BEB361F5D8EB58D7D0DB6450450C616B01C26F1686877959A9` |
| Application Release | `EEE64C936367120B20BAD1E0B29CB654109B34D3CDA937FD0F61DC789C41231F` |
| APEXTests Debug | `11A1A516E554FB3C28A4D2A2812AFF6611BC0E34A7D5F07BFA915DED927507C5` |
| APEXTests Release | `E11F977EA2AE8643ECAF9EB98BBB7D23B7BB7744EC1D3C2E9B294D5B576FC9B8` |

## Actualización posterior a la auditoría: clip, Trim y VU (2026-10-03)

Después de cerrar la inspección de solo lectura, se implementó FIX-017 en el core. El clip box ahora tiene lectura `+x.x dB` y reset utilizable en strips normales y Master; el hold del medidor ya no repone el clip borrado; los picos de la barra se agregan entre frames en vez de atenuarse por callback; los colores corresponden a dBFS. El input Trim sigue pre-fader/pre-FX y el VU sigue el canal post-Trim que entra a plugins; solo para un track armado sin monitoreo usa el hardware en una copia separada. La aguja usa RMS y Peak Max conserva peak dBFS. Para la referencia del panel, `0 VU` corresponde a `−18 dBFS` de pico equivalente en un seno.

La nueva regresión Debug `mixer.clip-input-meter-signal.v1` pasó 8 casos, incluyendo que +6 dB de Trim mueve el VU pre-FX en +6 dB y que un micrófono armado sin monitoreo se mide en una copia post-Trim. El build de aplicación Debug también pasó y ambos transcripts/hashes se guardaron en `evidence/clip-input-meter-signal-Debug.txt` y `evidence/clip-vu-meter-verification-Debug.txt`. El patch de los archivos FIX-017 está en `evidence/clip-vu-gain-staging-fix.patch`; su hash y hashes actuales de App/Tests quedan en el informe de verificación enlazado. `git apply --reverse --check` confirmó que el patch coincide con el árbol actual. Release app y tests se reconstruyeron; mixer.clip-input-meter-signal.v1 pasó 8 casos, con transcript en evidence/clip-input-meter-signal-Release.txt. No se validó con hardware real o una canción y no se ejecutó la suite completa Release. La advertencia de ownership del submódulo Signalsmith y los fallos de suite completa listados arriba siguen pendientes.


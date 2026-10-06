# Auditoría de persistencia de Apex

Fecha: 3 de octubre de 2026. Auditoría estática de solo lectura en la copia oficial `C:\Users\1993v\OneDrive\Desktop\Apex backup`. Base de rutas de código: `My DAW/DAW_Core/`. No se ejecutaron nuevos proyectos, guardados, recuperaciones ni pruebas dinámicas para este informe. Los riesgos indicados se desprenden del flujo de código; requieren reproducción y pruebas antes de declarar una corrección.

Se leyeron ambos `AGENTS.md`, la sección 9.3 y 9.4 del `DAW_BRAIN.md` de raíz y el volumen 29 (§463–479). El Brain prescribe integridad, identidad y preservación; varias recomendaciones todavía no son implementación real.

## Qué es autoridad y cómo se guarda

| Información | Autoridad viva / representación | Persistencia real y evidencia |
|---|---|---|
| Documento de proyecto | `ProjectManager` coordina snapshots de los subsistemas de `ApplicationCore` | Un `ValueTree("DAWProject")` convertido a XML, extensión de UI `.dawproj`, `version=9`, `name`, `savedAt`, `projectSampleRate` solamente si existe tasa probada. `Source/ProjectCore/ProjectManager.cpp:11,103–123`; descripción pública `ProjectManager.h:22–35`. No hay ID UUID ni contador de revisión top-level en ese constructor. |
| Tracks | `TrackManager` y `Track` | `Tracks` con IDs, nombre, mute/solo/arm/monitor, volumen/pan, organización/parent, rol y configuración de automatización. Plugins se guardan aparte. `Source/TrackCore/Track.cpp:144,177,474,491`. Contador TRK se reseedea tras restore, línea547; se crean tracks con IDs del documento. |
| Clips de audio | `ClipManager`, `AudioClip` | `Clips` contiene IDs/track/timing en muestras, ganancias/pan, fuente, pitch/stretch/formant/fades. `sourceFile` es ruta absoluta; audio no se embebe en XML. `Source/ClipCore/Clip.cpp:241,258,280,283,554`. IDs vacíos/duplicados de clips se rechazan con log, líneas575–582. |
| MIDI | `MidiClip`, `PianoRollClipModel` | `MidiClip::getState` embebe `PianoRollClip`, tempo y tasa; notas en ticks del modelo. `Source/MidiCore/MidiClip.cpp:76,92`; `Source/MidiCore/PianoRollClipModel.cpp:239,265`. No son archivos SMF dentro del proyecto. |
| Patrones/step sequencer | `StepSequencerModel` dentro de ventana UI, `PatternManagerCore` | Existe serializer `StepSequencerModel::toValueTree/fromValueTree` (`Source/StepSequencerCore/StepSequencerModel.cpp:225,264`), pero el coordinador de proyectos no lo llama. `PatternClip` guarda solo referencia `patternId` y propiedades del clip (`Source/ClipCore/PatternClip.cpp:12,25`). Hallazgo P1 abajo. |
| Automatización heredada | `AutomationManagerCore` de ApplicationCore | Árbol `Automation`: trackId, parameterId string, enabled/visible, puntos `timeSamples`, valor, curva/tensión y regiones de clips. `Source/AutomationCore/AutomationManagerCore.h:645,650,692`. Filtra automatización huérfana de clips al salvar. |
| Automatización APEX | `AutomationLaneStore` singleton, registry de keys y modos | Árbol `APEXAutomation version=1`; lanes con ParameterID numérico y parameterKey, puntos PPQ normalizados. Incluye `KeyRegistry` y `ModeState`; restore registry primero, luego lanes, publicación final agrupada. `Source/Automation/AutomationLaneStoreCore.h:289,302,303,320,326,336`. `ProjectManager.cpp:190,575` incorpora/restaura este subsistema. |
| Plugins | `ApplicationCore::pluginChains_`, `PluginChainCore`, `PluginInstanceCore` | `PluginChains/TrackPlugins(trackId)/PluginChain/Slot`. Guarda índice, bypass, mix slot/cadena, `pluginInstanceId`, modo ejecución, Description, Layout, State.data Base64. `Source/AppCore/ApplicationCore.cpp:1978`; `Source/PluginHostCore/PluginChainCore.h:1610,1617–1661`. Captura fallida invalida snapshot completo en vez de publicar parcialmente. |
| Identidad del plugin | `PluginDescriptionPersistenceCore` | `identitySchemaVersion=2`; fabricante, formato, versión, fileOrIdentifier, `uniqueId` y `deprecatedUid` separados. Interpreta documento legado ≤6 que almacenaba deprecatedUid bajo nombre uniqueId. `Source/PluginStorageCore/PluginDescriptionPersistenceCore.h:13,15,37,49–60`. Plugin blobs siguen siendo opacos dependientes del plugin y versión. |
| Routing/folders/master | RoutingGraph/FolderBusCore/MasterRouteStateCore y engine | Copias de `RoutingGraph`, `FolderBuses`, ceiling del master; restore por etapas después tracks/clips y antes/tras plugins según necesidad. `ProjectManager.cpp:173,176,523,535,679,770`. `MasterPhaseD` guarda además campos hardcoded masterGain1, ditherOff,24bits y meterPostFader, líneas125–145: no debe asumirse que toda configuración del panel está cableada. |
| UI/apariencia | `ApplicationState` y MainComponent | `State` guarda selección, zoom/scroll, grid, Bubblegum y preferencias de vista; `ProjectManager.cpp:175`, `ApplicationState.cpp:176` (`restoreState`). CollapsedFolders se agrega SOLO en save manual de MainComponent (`Source/MainComponent.cpp:4866,4889`); no aparece en el snapshot básico de autosave. |
| Vocal Tune | ApexTuneIntegrationCore/estado por clip | `ApexTuneClipState schemaVersion=1` anidado por clip con notas/edit decisions, root/scale/analysis/render versions. `ProjectManager.cpp:152–165,480–498`; `Source/VocalTuneCore/ApexTuneProjectStateCore.cpp:138,160` y `.h:48`. Se agenda rerender tras carga. |
| Click/colores QuickTrack | ApplicationCore | `ClickState`, `QuickTrackColors` incluidos en snapshot. `ProjectManager.cpp:198,208–214`; `ApplicationCore.cpp:2096,2108`. |

## Flujos y hilos

**Guardado manual:** MainComponent elige ruta y llama `saveProjectToFile` → `ProjectManager::saveToFile` → `buildState` → capturar todos plugins → callback de augmentation UI → XML candidato temporal único en mismo directorio → write/flush/close → parse XML → backup histórico opcional → `FilePublicationCore::publishFileTransactionally` → solo tras éxito cambia projectFile/name y limpia dirty. Evidencia: `Source/MainComponent.cpp:4889`; `Source/ProjectCore/ProjectManager.h:118–204`; `Source/ProjectCore/FilePublicationCore.h:36,58,178`. Guardado manual y captura son control-plane/GUI; escritura manual también ocurre en ese hilo. No se ejecuta desde callback de audio.

**Autosave de producción:** `ApplicationCore::initialize` selecciona UNA autoridad y desactiva timer legacy (`Source/AppCore/ApplicationCore.cpp:180–201`). `AutosaveManagerCore` timer GUI,120s por defecto,10 históricos, pool de un worker (`.h:138–148`, `.cpp:18,174`). Guarda solo si dirty y gate permite: no grabación/export/scan/restore incompleto (`.cpp:200–218`). Snapshot y getState de plugins se hacen GUI; XML/I/O/rotación/publicación en `detail::AutosaveWriteJob`, worker. Completion vuelve GUI con WeakReference y generación. `AutosavePublicationAuthority` protege commits frente shutdown/reprepare del MANAGER; OPEN→COMMITTING es punto de admisión, la vida de autoridad no depende del objeto GUI (`Source/ProjectCore/AutosavePublicationAuthority.h:15–26,110,132,184`). Esta garantía no contiene revisión de documento o cambio de proyecto.

**Carga:** `ProjectManager::loadFromFile` exige GUI, parsea XML, valida raíz y rechaza versión futura>9 ANTES de mutar (`.cpp:240–269`). Restore cierra editores de plugins, intenta gate completo, rechaza grabación/export, drena callbacks máximo2s. Restauración destructiva de subsistemas por etapas, errores agregados; si una etapa falla, latch de audio queda cerrado y save/autosave rechazados (`.cpp:370–436,876`; `ApplicationCore.cpp:1377,1405`). No restaura automáticamente el antiguo modelo tras fallo parcial. Fuente anterior en disco no se toca en abrir.

**Medios:** restore borra caché de AudioFileManager y decodifica fuentes presentes, una directamente o varias con pool1–4; publica resultados GUI antes de abrir gate (`Source/AppCore/ApplicationCore.cpp:1894–1975`). `waitForJobToFinish(...,-1)` puede bloquear GUI durante carga; audio queda suprimido por gate. Fuentes inexistentes simplemente no se añaden al decode request. No se encontró resolver de relocalización ni manifest portable en Source.

**Migración:** v9 corre reparaciones tape-stop a clip-local y bus sidechain incluso si documento dicev9, para sanar v9 emitido antes de fix. Reporte `ProjectUpgradeReport` y rutina `upgradeProjectFile` respaldan y muestran cambios (`ProjectManager.cpp:582–666,708–729`; `Source/MainComponent.cpp:4969–5031`). Upgrade genera sibling `.backup_YYYYMMDD_HHMMSS` primero, carga, guarda original solo después; resto saves produce `.backups`. No hay registry de migraciones puras consecutivas para todas las versiones dentro del coordinador; reparaciones están en restores de diversos módulos. PPQ no se escala con tasa; otros campos de tiempo sí tras tasa distinta probada (`ProjectManager.cpp:790–844`).

## Dónde quedan los archivos

`%APPDATA%` significa `juce::File::userApplicationDataDirectory` según Windows, no una ruta literal inventada.

| Ubicación | Formato / propósito / conservación |
|---|---|
| Ruta elegida por usuario `*.dawproj` | XML del proyecto; referencias a medios externos. No confundir con ZIP portable. |
| Directorio proyecto `\.backups\<nombre>_<fecha>.dawproj` | Copia del previo manual save; máximo20 archivos `*.dawproj` del directorio completo; no por proyecto. `ProjectManager.h:358–387`. |
| Directorio proyecto `\.apex_autosaves\<nombre>.autosave.latest.apex` | Autosave de producción, también XML DAWProject. Históricos `.autosave.001.apex`…`.010.apex`; `.cpp:258`, `AutosaveWriteJobCore.h:245,362`. |
| `%APPDATA%\APEX\UnsavedProjectAutosaves\Untitled.autosave.latest.apex` | Proyecto sin nombre; nombre no tiene sessionId/projectId. `.cpp:261–266`. |
| `%APPDATA%\APEX\session.lock` y `session.lock.previous` | XML `APEX_SESSION/SESSION` con session UUID, appVersion, cleanShutdown, ruta proyecto, latestAutosave y wasUnsavedProject. `RecoverySessionCore.cpp:15,20,38,51,73`. |
| `.discarded` bajo autosave folder | Recovery descartado se mueve aquí; `CrashRecoveryCore.cpp:69–79`. Comentario habla de sweep/hard-delete pero no se encontró en implementación del helper. |
| `.backups\autosave.dawproj` / `%TEMP%\DAWCore_Backups\autosave.dawproj` | Solo timer legacy independiente de ProjectManager; producción lo desactiva. `ProjectManager.cpp:37–100`, `.h:263,288`. `AutoSaveCore.h` adicional es callback Timer sin worker y sin uso encontrado fuera de definición. |
| `%APPDATA%\DAW_Core\plugin_cache.xml` | Índice plugin classes schema1, rutas/modTime/identity/version/capabilities; regenerable escaneando plugins. `Source/PluginStorageCore/PluginCacheCore.h:180,186,254`. No es plugin state de canción. |
| `%APPDATA%\DAW_Core\plugin_blacklist.xml`, `plugin_scan_failures.xml`, logs escaneo | Estado de seguridad/diagnóstico global; `Source/PluginSafetyCore/PluginBlacklistCore.h:130`; `Source/PluginScanCore/PluginScanFailureStoreCore.h:112`; `PluginScanAuditLogCore.h:80–89`. |
| `%APPDATA%\DAW_Core\plugin_window_scales.json` | UniversalScale y pluginScales; guardado directo replaceWithText sin chequear resultado. `Source/PluginHostCore/PluginWindowScaleStore.h:91–113`. |
| `%APPDATA%\DAW_Core\recent_projects.txt` | Recientes hasta10; rutas por línea; `Source/UICore/StartupPanel.h:94–100,355–381`. |
| `%APPDATA%\APEX\QuickTrackTemplates\*.qttpl` | Plantillas XML version1, tracks/routing, globales sobreviven NewProject. `Source/QuickTrackCore/QuickTrackTemplate.h:30–74,117,143,153`. |
| `%APPDATA%\APEX\SeedHistory.dat` / `APEX\Sequences\*.apexseq` | Historial seeds y curvas/patrones de automatización, XML preset version1; `Source/AutomationSequence/AutomationSequenceHistoryCore.h:125–161,236–249,273`. |
| `%APPDATA%\APEX\VocalTuneCache\<fingerprint>.apxan`, `<fp>_rN.wav` | Derivados análisis binario APXA/version1 y renders WAV; fingerprint FNV de ruta+tamaño+mtime+parámetros, no hash completo contenido. `Source/VocalTuneCore/ApexTuneCacheCore.cpp:14–15,44–84,88,116,154`. |
| Proyecto `Recordings` o Documentos `DAW_Core_Projects\Recordings` | Takes WAV separados `rec_<trackId>_<fecha>_<index>.wav`; `.dawproj` referencia path. `Source/RecordingCore/RecordingEngine.h:213–239`. No se encontró caller de `setProjectDirectory` en Source (solo definición108); probable fallback actual requiere prueba. |

## Hallazgos priorizados

### P1 — Preservación del estado de plugin ausente al volver a guardar

Verificado estáticamente: un plugin que no puede instanciarse no obtiene Slot vivo; `PluginChainCore::restoreState` agrega diagnóstico y continúa (`Source/PluginHostCore/PluginChainCore.h:1805–1819`). `ApplicationCore::restorePluginChainsState` agrega error pero devuelve true al final (`Source/AppCore/ApplicationCore.cpp:2063–2090`); ProjectManager lo clasifica warning, mantiene proyecto usable (`ProjectManager.cpp:685–701`). Save recorre únicamente slots vivos (`PluginChainCore.h:1617–1661`). No hay placeholder o copia bounded de Slot/Description/State original en ese camino. Próximo save puede eliminar de nuevo documento el plugin que estaba ausente y su preset/estado; además se compactan índices de slots siguientes. El original en disco todavía existe hasta ese save, y backup manual suele conservarlo, pero eso no equivale a preservación semántica.

Distinguir: un plugin SÍ instanciado cuya `setStateInformation` falla queda representado y marcado fuera del procesamiento (`PluginChainCore.h:1775–1797`; `PluginInstanceCore.h:1428`). No demuestra que el blob original fallido se vuelva a guardar intacto: captura obtiene estado actual de instancia/proxy. Acción recomendada: placeholder con identidad, índice/instanceId y blob original; guardar sin pérdida y warning visible; fixtures missingplugin-save-reopen y restoreblobfailed-save-reopen.

### P1 — Step sequencer/patrones no incluidos en el proyecto

`MainComponent` crea modelo de StepSequencer y PatternManager (`Source/MainComponent.cpp:2689–2695`), los clips guardan referencia UUID de patrón (`PatternClip.cpp:12–23`). `ProjectManager::buildState` no toma modelo; Subsystems no lo contiene (`ProjectManager.h:42–54`, `.cpp:103–225`), ni MainComponent augmentation lo agrega (`MainComponent.cpp:4866–4887`). Búsqueda completa de llamadas serializer en Source deja solo definición/declaración (`StepSequencerModel.cpp:225,264`). Además serializer existente no persiste Pattern/Lane/Event UUIDs de los structs (`StepSequencerTypes.h:7–12,61,91`; `StepSequencerModel.cpp:225–302`). Riesgo: patrones editados y samples del drum sampler no sobreviven cerrar/abrir, y referencias PatternClip quedan sin destino. Reproducción recomendada: crear dos patterns, clip linked, save/cerrar/app abrir/reopen; luego idempotencia de IDs y sample paths. No declarar esta capacidad persistente por existir `toValueTree`.

### P1 — Autosave borra cambios posteriores a su snapshot

`markDirty` únicamente setea bool (`AutosaveManagerCore.cpp:102–119`). Snapshot GUI se toma antes del I/O (`.cpp:233–245`); completion GUI de éxito borra `autosaveDirty_` sin comparar revisión (`.cpp:280–289`). Si usuario edita durante write, ese nuevo cambio se marca y luego se borra al completion del snapshot viejo; timer siguiente no guarda hasta otro cambio. La memoria no pierde la edición ni userDirty se limpia, pero esa revisión queda sin recovery. Acción: contador revisión monotónico capturado por job; solo limpiar hasta revisión publicada, conservar dirty si modelRevision>snapshotRevision. Test con worker pausado y edición intermedia.

### P1 — Completion de autosave anterior puede mutar sesión del proyecto siguiente

Generación de autoridad cambia únicamente prepare/shutdown del manager (`AutosaveManagerCore.cpp:29–78`). Hay único prepare en initialize (`ApplicationCore.cpp:201`). Abrir/NewProject no revoca ni drena job del antiguo documento y `MainComponent::projectLoaded` no limpia coherentemente manager (`ProjectManager.cpp:240–366`, `MainComponent.cpp:5033`). Callback captura projectFile antiguo y después pone latestAutosaveFile/limpia dirty/actualiza session.lock (`AutosaveManagerCore.cpp:272–305`), siempre que generación del manager siga abierta. Puede borrar dirty del siguiente proyecto o hacer recovery apuntar al anterior. No es fallo de shutdown (la máquina autoridad sí protege shutdown), es ausencia de identidad/generación por documento. Test recomendado job pausado proyectoA→NewProject/B→editarB→completarA.

### P1/P2 — Sobrescribir plantilla QuickTrack hace delete-before-move

`QuickTrackTemplateStore::saveTemplate` se presenta como atómico (`Source/QuickTrackCore/QuickTrackTemplate.h:153`), valida candidato, elimina target (`:208`) y luego move (`:214`). Si finalización falla tras delete, elimina también temporal216 y pierde plantilla anterior. Difícil reconstruir plantilla del usuario: no tratarla como cache. Usar misma publicación reemplazable que proyecto, fallo inyectado en reemplazo. `PluginCacheCore::saveToFile` también usa `.tmp` fijo+move (`:206–210`) sin misma boundary ni read-back; para cache regenerable la gravedad es menor, pero comentario crash-proof no está probado.

### P2 — Retención autosave no guarda las últimas10 versiones

Cuando slots están llenos, `rotateBackups` siempre elige y elimina001 (`AutosaveWriteJobCore.h:369–380`) sin desplazamiento/mtime/contador. Así002…010 permanecen como antiguas iniciales y001 se reemplaza cada vez. Conserva límites/canonical latest y comprueba fallos, pero no una ventana cronológica de últimasN. Test recomendado llenar>12 versiones y comparar contents; existentes cubren fallos de copy/delete no este orden.

### P2 — Recovery metadata menos fuerte que el proyecto

`RecoverySessionCore` elimina previous y move lock sin revisar resultados (`.cpp:27–34`), escribe lock directamente XML y no comprueba éxito (`:68,90`). `CrashRecoveryCore::findRecoveryInfo` exige unclean flag y mera existencia latest (`.cpp:25–66`); no parsea candidato, valida schema/ID, checksum, ni compara newer-than-named-save. El lock solo se actualiza con autosave completion (`AutosaveManagerCore.cpp:299–303`): manual Save/Load no actualizan currentProject. Puede ofrecer snapshot viejo después de guardar nueva revisión manual, o quedar sin info si lock corrupto. Unsaved autosaves usan nombre Untitled global; sin project/session key pueden sobrescribir latest de otra canción no guardada. Acción: lock transactionally published, projectId/sessionId+revisiones, cambios manuales actualizan base save, scan fallback recovery y validación fuerte.

### P2 — Proyecto no portable y faltantes de medios sin reporte completo

Fuentes se serializan por absolute `sourceFile`, no por assetId/hash (`AudioClip::getState`, Clip.cpp:258), sin manifest/copia media. `reloadAudioFiles` solo programa fuentes existentes (`ApplicationCore.cpp:1913–1919`), no agrega error de missing media; la carga puede reportar éxito con clips silenciosos. Guardar XML no respalda audio. Brain9.6/§471–473 son recomendaciones, no garantía actual. Acción: reporte MissingMedia y relink, assets IDs/path hints, CollectAndSave portable con pruebas de mover folder completo.

### P2 — Validación de proyecto incompleta antes de mutar

Candidate read-back es parse XML (`ProjectManager.h:177`) no production `loadFromFile`+referencial/integridad. Load valida raíz/version (`ProjectManager.cpp:256–269`) pero no required subtrees/counts/checksums/refs globales. Tracks restore no rechaza duplicate/empty IDs (`Track.cpp:491–544`) mientras clips sí. `restoreFromState` puede omitir subtree faltante y retener parte de modelo anterior según módulo; ya limpió folder/plugins y mutado otros. Acción: validador documento único pre-mutation y candidate validator reutilizado, fixtures malformed/duplicates/missing-required; bounded opaque lengths. No afirmar data loss runtime sin fixture, pero el contrato de validación es limitado.

### P2 — Preferencias prometidas solo en memoria o desconectadas

`ShortcutPersistenceManager` doc dice salva/carga perfiles/plataforma/GUI/bounds (`.h:7–13`) pero solo posee `juce::PropertySet props_` (`:78`) sin archivo ni load/flush. `MasterStripFlipState` permite PropertiesFile (`.h:92`) pero no se encontró caller `setPropertiesFile` en Source. DeviceManager initialize usa nullptr de saved XML (`MainComponent.cpp:7892,7900`); no se encontró createStateXml/caller persistente en Source, por tanto configuración dispositivo no tiene persistencia cross-session verificada. En cambio apariencia de ApplicationState sí va dentro de canción, y escala plugins tiene archivo global. Acción: contrato explícito por cada setting; tests restart. No confundir propiedad viva con dato persistido.

### P2 — Campos futuros se pierden al reconstruir

Coordinador construye nuevo DAWProject desde módulos conocidos cada save (`ProjectManager.cpp:103–225`), sin almacenar original extras. Top-level future version>9 se rechaza correctamente. Extensiones desconocidas dentro dev9 no se preservan; VocalTune schema futuro se acepta best-effort e ignora unknown (`ApexTuneProjectStateCore.cpp:166–174`) y siguiente save emite schema1 conocido. Brain9.4 recomienda opaque preservation; no implementado universalmente. Acción: extensión bounded retained/read-only policy según compatibilidad.

### P3 — Backups manuales por folder completo y fuentes fuera carpeta

`createBackup` busca todos `.dawproj` en `.backups`, sort por pathname y recorta a20 (`ProjectManager.h:378–387`), no últimos20 por proyecto/mtime. Múltiples canciones mismas carpeta compiten por retención y orden alfabético puede descartar todas de un nombre antes otro. `RecordingEngine::setProjectDirectory` no tiene caller encontrado, probable recordings fuera folder canción; validar UI y conectar autoridad. Recomendación: retención por projectId/revisión y guardar/media layout explícito.

## Protecciones ya presentes y que deben conservarse

- No delete-before-move en publicación de proyectos/autosaves: `FilePublicationCore` exige candidato cerrado, sibling directorio, usa JUCE replace, comprueba resultado y no destruye destino primero. Candidato nunca autoridad tras fallo. Candidatos parses y error paths ya existen.
- Captura de sandbox fallida no puede introducir documento parcial en save/autosave; `ProjectManager::buildState` y `ApplicationCore::capturePluginChainsState` result-bearing.
- Cierre autosave revoca autoridad con objeto shared, WeakReference en callback y generación con permiso atomic commit; una tarea vieja del manager no reabre la siguiente generación. La brecha pendiente es por documento.
- Restauración gate completo asegura callback no recorre medio modelo; si falla mantiene audio/save suprimidos en vez de continuar con estado parcial.
- Legacy/current uniqueId de plugins separados, pluginInstanceId persistido, clipping sample-rate reconcile y reparaciones idempotentesv9 existentes.
- Manual UI collapsed folders augment ahora entra ANTES de publicación única; no segundo overwrite fragmentario del XML.

## Pruebas existentes y alcance

No ejecutadas en esta auditoría; existencia verificada en código. `Tests/Source/Diagnostics/ProjectPersistenceTransactionTests.cpp` cubre authority shutdown/acquire/reprepare, vida del manager/callback, fallos de reemplazo manual, historical copy/delete, autosave worker real, snapshot sandboxfailed, único writer production, legacy path y augmentation. Casos líneas231/245/262,290/318/347/382/431/470/502,562/596/626/649/685/720/750/761/791/806/834/869/895/928/967/995/1020. Los casos de rotación verifican failure-safety y history válida, no ventana de últimasN cuando ya llena. No se encontró en esa suite revisión dirty durante job, cambio documento durante job, recovery newer-than-save, ni preservación missing plugin tras resave.

`Tests/Source/Diagnostics/PluginIdentityPersistenceTests.cpp:14–96` cubre modern/legacy identidad y cache/IPC; no prueba preservar Slot ausente. `Tests/Source/StepSequencerTests/PatternModelTests.cpp` y `PatternClipTests.cpp` existen; el hecho de tener model/clip tests no demuestra integration save-close-reopen. `Tests/Source/Project/ProjectSampleRateReconcileTests.cpp` existe para tasa. No confundir test local de ValueTree con documento completo/hardware/plugin comercial.

## Orden propuesto para siguiente desarrollo

1. Reproducciones P1 de missing-plugin resave, patterns save/reopen, dirty intermedio y cambio documento con autosave in-flight.
2. Correcciones pequeñas con fixtures antes/después: placeholders, modelo patterns/IDs, revisión y generación del documento. Mantener boundary publicación y gates actuales.
3. Plantillas delete-before-move, retención y metadata recovery; tests fault-injection y>N backups.
4. Assets/media report+portable; contrato preferencias; validación completa y extension policy.
5. Debug/Release gates oficiales, save/reopen comparando semántica y prueba sesión real del usuario. La auditoría no declara estas correcciones hechas ni pone all-green release.

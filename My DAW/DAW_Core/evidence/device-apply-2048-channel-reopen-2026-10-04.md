# Buffer 2048: reapertura innecesaria, causa del zipper no demostrada

Fecha: 2026-10-04

## Síntoma reportado

Con Windows Audio y el micrófono y salida Realtek seleccionados, cambiar el buffer a 2048, presionar Apply y comenzar playback rápidamente produce un ruido fuerte tipo zipper.

## Corrección de alcance de la conclusión anterior

El código sí podía comparar la capacidad solicitada de hasta 64 entradas con los canales activos concedidos por el driver y solicitar una segunda configuración. El cambio y las pruebas confirmaron que ya no se pide esa reconfiguración cuando hay entradas válidas.

Eso demuestra que se elimina una reapertura redundante. No demuestra que esta reapertura causara el zipper del usuario ni que quitarla resuelva el ruido. La conclusión anterior sobre causa raíz fue prematura.

## Verificación

- Build Debug de `APEXTests.sln`: completado.
- Suite `APEX.Device`: 51 aserciones correctas, 0 fallos en Debug y Release.
- En Debug también pasan `routing-buffer.prepare.v1` (15 correctas, 0 fallos; el callback de 2048 no realoca) y `plugin.automation.large-block-smoothing.v1` (11 correctas, 0 fallos; automation sobre bloques de 2048).
- App Release x64: compilada y enlazada. Para evitar el conflicto de variables `PATH/Path` de esta sesión se compiló con el PATH del proceso retirado; el paso local de firma se omitió.
- El build Release de toda la solución de pruebas encontró un error de memoria del compilador C1002 en `PluginSandboxPhaseE2CTests.cpp`; ese fallo no pertenece al código del cambio. No se declara una compilación limpia de la batería Release.
- No se ejecutó playback en el dispositivo Realtek físico. La regresión automatizada verifica que no se solicita un segundo reopen con una entrada válida; la desaparición del ruido necesita confirmación de audio real.

Resultados JSON: `device-apply-2048-channel-reopen-Debug-2026-10-04.json` y `device-apply-2048-channel-reopen-Release-2026-10-04.json`.

SHA-256 de artefactos verificados:

- App Release `DAW_Core.exe`: `1F7EE5A7FED72372CEC8ED30FB34D272CFCC956B02B19374844411F3BF6EA76B`.
- Runner Debug `APEXTests.exe`: `3A808D1C2195DFF1B0B530A95928F598FE4AA353CCB41787900A723EB213E56F`.
- JSON Debug: `0CD5652DDF6D24404C1A7F6E8E162277D8A3750C3F872CA289A20C77C3A4184E`.
- JSON Release: `5211DD86952307C0DD5EFDE02CE0F490ECEFAA039C3DB3AE55C2DB7004A5D610`.

Estado de este cambio: prueba enfocada de la condición de reconfiguración; relación causal con el zipper: no demostrada. FIX-018 permanece abierto.

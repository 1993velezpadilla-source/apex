# APEX

APEX es una estación de trabajo de audio para Windows, escrita en C++ con JUCE.
Esta rama contiene el código actual exportado del Desktop, sus recursos,
dependencias fijadas, pruebas, scripts y documentación de arquitectura.

## Estructura

| Ruta | Contenido |
| --- | --- |
| `My DAW/DAW_Core/Source/` | Aplicación y motor de audio |
| `My DAW/DAW_Core/Builds/VisualStudio2026/ArrangementEditor/` | Código activo del editor |
| `My DAW/DAW_Core/Tests/` | Pruebas y plugins de prueba |
| `My DAW/DAW_Core/Scripts/` | Dependencias, compilación y pruebas |
| `My DAW/DAW_Core/Dependencies/` | Versiones y parches fijados |
| `DAW_BRAIN.md`, `docs/`, `Daw Brain/` | Brain actual, auditorías y contexto técnico |

## Compilar desde una copia nueva

Se requieren Windows, Visual Studio 2026 (versión 18), C++ v145 y Windows SDK.

```powershell
git clone --recurse-submodules https://github.com/1993velezpadilla-source/apex.git
cd "apex/My DAW/DAW_Core"
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\prepare_ci.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Release -Rebuild -Unsigned
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Release -Seed 0xA9E12026
```

Mientras la migración esté en un PR, clona su rama para probar esta versión.
`prepare_ci.ps1` descarga JUCE 8.0.12 del release oficial, comprueba su SHA-256
y aplica los parches del contrato. No requiere copiar el SDK instalado del Desktop.
Consulta [BUILDING_WINDOWS.md](My%20DAW/DAW_Core/docs/BUILDING_WINDOWS.md) y
[TESTING_WINDOWS.md](My%20DAW/DAW_Core/docs/TESTING_WINDOWS.md).

## GitHub Actions

El workflow `Windows build and tests` compila Debug y Release, ejecuta la suite
completa y conserva los resultados reales. El EXE y su PDB se publican como
artifacts descargables solo después de que el job termine con éxito.
Los ejecutables producidos por CI son builds sin firma.

La carpeta original del Desktop se conserva. La migración seguirá como candidata
hasta que pasen las dos compilaciones limpias con sus pruebas. Los dispositivos
de audio físicos y los plugins externos requieren también pruebas locales.
Consulta [SOURCE_MIGRATION.md](docs/SOURCE_MIGRATION.md) para el contenido y los
límites de esta migración.
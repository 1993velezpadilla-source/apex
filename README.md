# APEX

APEX es una estaciÃ³n de trabajo de audio (DAW) para Windows, desarrollada en C++ con JUCE.
Este repositorio reÃºne el cÃ³digo de la aplicaciÃ³n, las pruebas, los recursos de diseÃ±o,
las herramientas de desarrollo y la documentaciÃ³n del espacio de trabajo APEX.

## Estructura

| Ruta | Contenido |
| --- | --- |
| `My DAW/DAW_Core/Source/` | CÃ³digo de la aplicaciÃ³n y del motor de audio |
| `My DAW/DAW_Core/Tests/` | Pruebas y proyectos de plugins de prueba |
| `My DAW/DAW_Core/Scripts/` | PreparaciÃ³n de dependencias, compilaciÃ³n y pruebas |
| `My DAW/DAW_Core/Dependencies/` | Versiones fijadas y parches de las dependencias |
| `My DAW/DAW_Core/docs/` | DocumentaciÃ³n tÃ©cnica y guÃ­as de desarrollo |
| `DAW_BRAIN.md` y `Daw Brain/` | Referencias de arquitectura y paquetes de agentes |
| `docs/`, `tools/`, `DesignReferences/` | Planes, herramientas y referencias del proyecto |

## Clonar

```powershell
git clone --recurse-submodules https://github.com/1993velezpadilla-source/apex.git
cd apex
```

Si ya clonaste el repositorio, inicializa las dependencias desde su raÃ­z:

```powershell
git submodule update --init --recursive
```

## Compilar en Windows

La guÃ­a completa y el contrato de dependencias estÃ¡n en
[`BUILDING_WINDOWS.md`](My%20DAW/DAW_Core/docs/BUILDING_WINDOWS.md) y
[`apex-windows-dependencies.json`](My%20DAW/DAW_Core/Dependencies/apex-windows-dependencies.json).
Se requieren Visual Studio 18, las herramientas C++ v145, Windows SDK y PowerShell 5.1.

Los SDK instalados y sus instaladores se mantienen fuera de Git. ObtÃ©n el archivo
oficial `juce-8.0.12-windows.zip`, colÃ³calo en `My DAW/Sdk setups/` y sigue la guÃ­a
para verificar sus hashes y aplicar los parches fijados por APEX.

Desde `My DAW/DAW_Core`:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\materialize_dependencies.ps1 -JuceArchive "..\Sdk setups\juce-8.0.12-windows.zip"
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
```

Consulta tambiÃ©n [`TESTING_WINDOWS.md`](My%20DAW/DAW_Core/docs/TESTING_WINDOWS.md).
Las comprobaciones de GitHub validan la polÃ­tica del repositorio y los formatos de
evidencia; la compilaciÃ³n y las pruebas de audio se realizan con el entorno Windows
documentado. Los resultados de compilaciÃ³n no sustituyen las pruebas de funcionamiento.

## Trabajar con GitHub

Usa Issues para registrar errores y tareas, ramas para cada cambio y pull requests
para revisar el cÃ³digo. El repositorio incluye plantillas para errores, tareas y
pull requests, junto con comprobaciones automÃ¡ticas de la estructura del proyecto.

Los archivos compilados, las credenciales, las grabaciones locales y los volcados
de errores se excluyen de la publicaciÃ³n. Los archivos existentes permanecen en la
carpeta local. La publicaciÃ³n parte de una captura de la versiÃ³n actual; los
historiales de los dos repositorios originales se conservan localmente.

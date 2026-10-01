# APEX

APEX es una estación de trabajo de audio (DAW) para Windows, desarrollada en C++ con JUCE.
Este repositorio reúne el código de la aplicación, las pruebas, los recursos de diseño,
las herramientas de desarrollo y la documentación del espacio de trabajo APEX.

## Estructura

| Ruta | Contenido |
| --- | --- |
| `My DAW/DAW_Core/Source/` | Código de la aplicación y del motor de audio |
| `My DAW/DAW_Core/Tests/` | Pruebas y proyectos de plugins de prueba |
| `My DAW/DAW_Core/Scripts/` | Preparación de dependencias, compilación y pruebas |
| `My DAW/DAW_Core/Dependencies/` | Versiones fijadas y parches de las dependencias |
| `My DAW/DAW_Core/docs/` | Documentación técnica y guías de desarrollo |
| `DAW_BRAIN.md` y `Daw Brain/` | Referencias de arquitectura y paquetes de agentes |
| `docs/`, `tools/`, `DesignReferences/` | Planes, herramientas y referencias del proyecto |

## Clonar

```powershell
git clone --recurse-submodules https://github.com/1993velezpadilla-source/apex.git
cd apex
```

Si ya clonaste el repositorio, inicializa las dependencias desde su raíz:

```powershell
git submodule update --init --recursive
```

## Compilar en Windows

La guía completa y el contrato de dependencias están en
[`BUILDING_WINDOWS.md`](My%20DAW/DAW_Core/docs/BUILDING_WINDOWS.md) y
[`apex-windows-dependencies.json`](My%20DAW/DAW_Core/Dependencies/apex-windows-dependencies.json).
Se requieren Visual Studio 18, las herramientas C++ v145, Windows SDK y PowerShell 5.1.

Los SDK instalados y sus instaladores se mantienen fuera de Git. Obtén el archivo
oficial `juce-8.0.12-windows.zip`, colócalo en `My DAW/Sdk setups/` y sigue la guía
para verificar sus hashes y aplicar los parches fijados por APEX.

Desde `My DAW/DAW_Core`:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\materialize_dependencies.ps1 -JuceArchive "..\Sdk setups\juce-8.0.12-windows.zip"
powershell -ExecutionPolicy Bypass -File Scripts\verify_dependencies.ps1
powershell -ExecutionPolicy Bypass -File Scripts\build_apex.ps1 -Configuration Debug -Rebuild -Unsigned
powershell -ExecutionPolicy Bypass -File Scripts\run_apex_tests.ps1 -Configuration Debug -Seed 0xA9E12026
```

Consulta también [`TESTING_WINDOWS.md`](My%20DAW/DAW_Core/docs/TESTING_WINDOWS.md).
Las comprobaciones de GitHub validan la política del repositorio y los formatos de
evidencia; la compilación y las pruebas de audio se realizan con el entorno Windows
documentado. Los resultados de compilación no sustituyen las pruebas de funcionamiento.

## Trabajar con GitHub

Usa Issues para registrar errores y tareas, ramas para cada cambio y pull requests
para revisar el código. El repositorio incluye plantillas para errores, tareas y
pull requests, junto con comprobaciones automáticas de la estructura del proyecto.

Los archivos compilados, las credenciales, las grabaciones locales y los volcados
de errores se excluyen de la publicación. Los archivos existentes permanecen en la
carpeta local. La publicación parte de una captura de la versión actual; los
historiales de los dos repositorios originales se conservan localmente.

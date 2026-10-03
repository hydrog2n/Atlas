# Building Atlas

Atlas v0.5.1 uses C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, and GoogleTest as defined by [ADR-0001](.DESIGN/adr/ADR-0001-technology-baseline.md). The supported Windows desktop build uses the official Qt SDK; the vcpkg Qt build is deprecated because Qt configuration through that path is unreliable in the current environment.

## Prerequisites

- Visual Studio with a C++23-capable compiler
- CMake 3.25 or newer
- Ninja
- vcpkg with `VCPKG_ROOT` set
- Official Qt 6 SDK with the MSVC 64-bit kit installed

On Windows, use one `cmd.exe` process initialized by
`VsDevCmd.bat -arch=amd64 -host_arch=amd64`. The CMake cache records the
compiler path but does not provide MSVC's standard-library include and library
environment on its own. The VS Code tasks in `.vscode/tasks.json` are the
canonical working examples.

## Configure and build

From the repository root, in an initialized Visual Studio x64 command process:

```powershell
cmake --preset headless-vcpkg
cmake --build --preset headless-vcpkg
ctest --preset headless-vcpkg
```

Equivalent one-shot command from PowerShell:

```text
cmd.exe /d /s /c "call C:\PROGRA~1\MICROS~3\18\Community\Common7\Tools\VsDevCmd.bat -arch=amd64 -host_arch=amd64 && set VCPKG_ROOT=C:\Users\The Big H\vcpkg && cmake --preset headless-vcpkg && cmake --build --preset headless-vcpkg && ctest --preset headless-vcpkg --output-on-failure"
```

The headless preset builds the domain library, CLI, and tests without Qt. The CLI emits a deterministic normalized empty project:

```powershell
build/headless-vcpkg/atlas_cli.exe
```

To build the Qt Widgets shell with the supported official Qt SDK:

```powershell
$env:QT_ROOT = "C:\Qt\6.11.2\msvc2022_64"
$env:VCPKG_ROOT = "C:\Users\The Big H\vcpkg"
cmake --preset windows-qt-sdk
cmake --build --preset windows-qt-sdk
```

The desktop executable is `build/windows-qt-sdk/atlas.exe`. Deploy its Qt runtime with:

```powershell
& "$env:QT_ROOT\bin\windeployqt.exe" "build/windows-qt-sdk/atlas.exe"
```

The complete one-shot command used by the GUI launch task is:

```text
cmd.exe /d /s /c "call C:\PROGRA~1\MICROS~3\18\Community\Common7\Tools\VsDevCmd.bat -arch=amd64 -host_arch=amd64 && set QT_ROOT=C:\Qt\6.11.2\msvc2022_64 && set VCPKG_ROOT=C:\Users\The Big H\vcpkg && cmake --preset windows-qt-sdk && cmake --build --preset windows-qt-sdk && C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe build\windows-qt-sdk\atlas.exe"
```

To exercise the v0.2 package workflows from the headless CLI:

```powershell
build/headless-vcpkg/atlas_cli.exe create "$env:TEMP\atlas-project.atlas"
build/headless-vcpkg/atlas_cli.exe inspect "$env:TEMP\atlas-project.atlas"
build/headless-vcpkg/atlas_cli.exe recover "$env:TEMP\atlas-project.atlas"
build/headless-vcpkg/atlas_cli.exe migrate "$env:TEMP\atlas-project.atlas" 2
```

The old `windows-vcpkg` preset is retained only as a hidden deprecated compatibility record. Do not use it for new builds.

If MSVC reports that a standard header such as `algorithm`, `array`, or `string`
cannot be found, stop and repeat the build from the initialized `cmd.exe`
process. Do not repair source includes or CMake targets until that environment
check succeeds.

The CLI remains a package utility for create, inspect, recover, and migrate operations. Interactive generic and RoadSpline authoring is provided by the Qt desktop executable; lane-native editing, connectivity, and production export remain future roadmap capabilities.

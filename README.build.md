# Building Atlas

Atlas uses C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, and GoogleTest as defined by [ADR-0001](.DESIGN/adr/ADR-0001-technology-baseline.md). The supported Windows desktop build uses the official Qt SDK; the vcpkg Qt build is deprecated because Qt configuration through that path is unreliable in the current environment.

## Prerequisites

- Visual Studio with a C++23-capable compiler
- CMake 3.25 or newer
- Ninja
- vcpkg with `VCPKG_ROOT` set
- Official Qt 6 SDK with the MSVC 64-bit kit installed

## Configure and build

From the repository root:

```powershell
cmake --preset headless-vcpkg
cmake --build --preset headless-vcpkg
ctest --preset headless-vcpkg
```

The headless preset builds the domain library, CLI, and tests without Qt. The CLI emits a deterministic normalized empty project:

```powershell
build/headless-vcpkg/atlas_cli.exe
```

To build the initial Qt Widgets shell:

```powershell
$env:QT_ROOT = "C:\Qt\6.11.2\msvc2022_64"
cmake --preset windows-qt-sdk
cmake --build --preset windows-qt-sdk
```

The desktop executable is `build/windows-qt-sdk/atlas.exe`. Run `windeployqt` from the Qt SDK against the executable before launching it outside a Qt-configured environment.

The old `windows-vcpkg` preset is retained only as a hidden deprecated compatibility record. Do not use it for new builds.

The first executable is intentionally a foundation shell. It does not claim persistence, editing, road authoring, or production export; those arrive through the roadmap packets.

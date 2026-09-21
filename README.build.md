# Building Atlas

Atlas uses C++23, Qt 6 Widgets, CMake, vcpkg manifest mode, and GoogleTest as defined by [ADR-0001](.DESIGN/adr/ADR-0001-technology-baseline.md).

## Prerequisites

- Visual Studio 2022 with a C++23-capable compiler
- CMake 3.25 or newer
- Ninja
- vcpkg with `VCPKG_ROOT` set

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
cmake --preset windows-vcpkg
cmake --build --preset windows-vcpkg
```

The desktop executable is `build/windows-vcpkg/atlas.exe`.

The first executable is intentionally a foundation shell. It does not claim persistence, editing, road authoring, or production export; those arrive through the roadmap packets.

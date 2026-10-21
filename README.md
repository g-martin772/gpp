# gpp

Modern C++23 framework. Module-based. Linux-first.
Used by [MoleHole](https://github.com/g-martin772/MoleHole).

## Features
- **Core**: DI container, app builder, hosted services, events, logger, thread pool, YAML/JSON IO
- **Hot reload**: UI layers, themes and shaders as `.so` plugins, rebuild = live update
- **Graphics**: Vulkan (VMA, swapchain, ray query / acceleration structures), render graph, GPU profiler
- **UI**: Dear ImGui, node editor, gizmos, notifications, log panel
- **Assets**: glTF + PBR, images, fonts, shader compile and reload
- **Simulation**: EnTT ECS, scene + YAML, undoable commands, sim thread with published snapshots
- **Physics**: PhysX
- **Particles**: CPU and GPU backends
- **Scripting**: sandboxed Luau, coroutine scheduler, component bindings
- **Units**: SI types and constants

## Requirements
- Linux, Vulkan drivers
- GCC with C++23 `import std`
- CMake >= 4.4, Ninja
- [vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` set)

## Quick start
```sh
git clone https://github.com/g-martin772/gpp
cd gpp
cmake --preset vcpkg
cmake --build --preset vcpkg
```

```sh
./build/scratch
./build/unit_tests
```

## Use in your project
```cmake
add_subdirectory(path/to/gpp)
target_link_libraries(app PRIVATE gpp)
```
```cpp
import gpp;
```
Add `path/to/gpp/vcpkg-overlays` to `VCPKG_OVERLAY_PORTS`. See `tests/scratch/` for a minimal demo.

## Layout
```
src/core/        DI, app, assets, hotreload, threading, units
src/graphics/    vulkan, renderer, shaders, ui, assets, particles
src/simulation/  scene, ECS, runner, commands, physics
src/scripting/   Luau VM and bindings
tests/           unit tests, scratch demo
```

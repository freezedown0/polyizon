# Polyizon

A C++20 / Vulkan 1.3 3D engine: a Qt-based editor (scene hierarchy, inspector, content browser, console, Play/Pause/Stop) and a standalone GLFW game client, sharing one `core` static library — EnTT for the scene graph, Assimp for mesh import, Lua (via sol2) for scripting, nlohmann/json for scene and project serialization.

## Features

- Vulkan 1.3 dynamic rendering, no legacy render passes
- Realistic (raymarched atmosphere + volumetric clouds, PCF shadows) and Voxel (flat sky, blocky 4x4x4 shadow grid) lighting modes, switchable per scene
- Directional, point, and spot lights
- EnTT-based scenes with Assimp mesh import (.obj/.fbx/...), JSON serialization
- Lua scripting per-entity via sol2
- Play/Pause/Stop with scene snapshot/restore, so testing a scene never permanently mutates it
- Move/Rotate viewport gizmos for manipulating entities directly with the mouse
- Content Browser with file import
- Build Game: exports the currently open project into a standalone, self-contained executable that runs without the editor

## Building

Requires CMake 3.25+, a C++20 compiler (MSVC), the Vulkan SDK, and vcpkg for dependencies (Qt6, GLFW, Assimp, Lua, sol2, nlohmann-json, EnTT, glm, ImGui, VulkanMemoryAllocator). Configure and build with the provided CMake presets:

```bash
cmake --preset windows-ninja
cmake --build build/windows-ninja
```

This produces `polyizon_editor.exe` (the editor) and `polyizon.exe` (the standalone game client) under `build/windows-ninja/bin/`.

## Credits

See Help > Credits in the editor.

## A note on the code

Spaghetti tastes too good, that it is applied to code too.

In other words: this codebase is not always clean, and that's fine. It's here to be forked, torn apart, and rebuilt into your own engine — messy code you can actually read and bend to your will beats a pristine architecture you're afraid to touch. Take it, make it yours.

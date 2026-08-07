<p align="center">
  <img src="temporaryassets/Horizontal%20Lockup.png" alt="Polyizon" width="640">
</p>

<p align="center">
  <strong>POLYIZON ENGINE 2 - PREVIEW</strong><br>
  A modern, realistic Vulkan engine and editor in active development.
</p>

> [!IMPORTANT]
> This branch is a development preview. Scene serialization is backward-compatible,
> but rendering APIs and editor workflows may still change before the stable Engine 2 release.

## What is Polyizon?

Polyizon is a C++20 game engine built around Vulkan 1.3. It includes a Qt-based
editor and a standalone GLFW client that share the same scene, lighting, material,
and rendering code.

The Engine 2 direction replaces the original voxel-style prototype with one
realistic rendering path and a cleaner authoring experience.

## Preview highlights

- Modern dark editor with Hierarchy, Inspector, Content Browser, Console, and viewport tools
- Project-aware `.scene` workflow with versioned serialization and stable entity IDs
- Physically based metallic/roughness materials with emissive support
- Directional, point, and spot lights with Realtime, Mixed, and Baked mobility
- PCF directional shadows and editable Sky Environment
- Optional raymarched volumetric cloud layer
- Shared Render Graph frame structure for Editor and Client
- Automatic Vulkan image-layout and synchronization tracking
- Forward+ GPU light-buffer foundation for up to 1,024 active local lights
- Play, Pause, and Stop with scene snapshot restoration
- Standalone project builds from the Editor

## Engine 2 status

| Area | Preview status |
| --- | --- |
| Authoring and PBR foundation | In progress |
| Shared Render Graph | Partially implemented |
| Automatic Vulkan image transitions | Implemented in the scene renderers |
| Forward+ lighting | GPU light buffers implemented; compute tile culling in progress |
| Light baking | Planned |
| C# scripting API | Planned; Lua remains as a compatibility layer |
| Production diagnostics | Planned |

The detailed milestone list lives in
[`docs/ENGINE_2_ROADMAP.md`](docs/ENGINE_2_ROADMAP.md).

## Building on Windows

Requirements:

- CMake 3.25 or newer
- A C++20-capable MSVC toolchain
- Vulkan SDK
- vcpkg dependencies configured for the included preset

```powershell
cmake --preset windows-ninja
cmake --build build/windows-ninja
```

The build produces:

- `build/windows-ninja/bin/polyizon_editor.exe` - Polyizon Editor
- `build/windows-ninja/bin/polyizon.exe` - standalone client

## Current technology

- Vulkan 1.3 dynamic rendering and synchronization2
- Qt 6 editor UI and GLFW standalone runtime
- EnTT entity-component system
- Assimp mesh import
- GLM mathematics
- Lua/sol2 legacy scripting compatibility
- nlohmann/json scene and project serialization

## Contributing

Polyizon Engine 2 is being developed in visible milestones. Please keep changes
compatible with both Editor and Client, preserve older `.scene` loading, and add
tests for persisted formats or shared rendering infrastructure where practical.

## Credits

See **Help > Credits** inside the Editor.

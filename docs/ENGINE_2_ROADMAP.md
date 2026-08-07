# Polyizon Engine 2 Roadmap

## Product direction

Polyizon Engine 2 is a staged replacement of the prototype renderer and editor.
Every milestone must leave both the editor and the standalone client usable. Old
`.scene` files remain loadable while new files declare `schemaVersion: 2`.

The target is a modern, realistic Vulkan engine with a productive editor rather
than a direct clone of Unity. Unity's familiar authoring patterns are useful
references, but Polyizon should keep a smaller, clearer surface and one coherent
rendering path.

## Decisions

- Rendering: physically based Forward+ first, built on a render graph. Deferred
  and ray-traced features can become optional passes instead of separate engines.
- Scripting: C# on .NET 10 LTS using the native `nethost`/`hostfxr` API. Lua stays
  as a compatibility layer until projects migrate.
- Scene model: EnTT remains the runtime ECS, with stable UUIDs, parent/child
  relationships, enabled state, prefab links, and versioned serialization.
- Assets: imported source files receive stable asset IDs and metadata. Scenes
  reference IDs instead of installation-relative paths.
- Lighting: Directional Light is an entity component; Sky Environment is scene
  data. Realtime, Mixed, and Baked mobility are shared light concepts.
- UI: components and assets are represented by a consistent icon registry.

Official scripting-host references:

- https://learn.microsoft.com/dotnet/core/tutorials/netcore-hosting
- https://dotnet.microsoft.com/platform/support/policy

## Milestones

### 1. Authoring and PBR foundation — in progress

- [x] Modern editor theme and project-aware `.scene` workflow.
- [x] Component icons in the Hierarchy, Create menu, and Inspector.
- [x] Inspector shows only attached components and has one Add Component menu.
- [x] Directional Light component separated from Sky Environment.
- [x] Metallic/roughness/emissive material data and Cook-Torrance direct lighting.
- [x] Version 2 scene serialization with backward-compatible material defaults.
- [x] Increase the prototype four-light caps to sixteen point and spot lights.
- [x] Stable entity UUIDs, active state, and baked-lighting contribution flags.
- [ ] Stable asset UUIDs.
- [ ] Parent/child transforms and editor multi-select.
- [ ] Undo/redo command stack for all scene edits.

Exit criteria: old scenes open without data loss; new scenes save deterministically;
editor and client render the same PBR result.

### 2. Render architecture

- [x] Remove the dormant voxel/classic rendering resources.
- [~] Consolidate duplicate editor/game render code behind shared passes. The
  common Directional Shadow and Forward PBR frame structure is shared; resource
  setup, light extraction, and draw submission still need consolidation.
- [~] Introduce a render graph with explicit resources and passes. Core pass
  ordering, shared resource declarations, and automatic Vulkan image-state
  transitions are implemented; graph-owned allocation and the editor pass
  inspector remain.
- [~] Add clustered Forward+ light culling, GPU light buffers, and warnings instead
  of silently dropping lights. Shared storage-buffer lights and overflow warnings
  are implemented for up to 1,024 local lights; compute cluster generation and
  per-tile light lists remain.
- Add texture-backed PBR materials: base color, normal, metallic/roughness,
  ambient occlusion, emissive, alpha modes, two-sided rendering.
- Add HDR render targets, exposure, ACES tone mapping, bloom, SSAO, TAA/FXAA,
  color grading, fog, and post-process volumes.
- Add cascaded directional shadows; point and spot shadow atlases; bias, filter,
  distance, and resolution controls.
- Add environment maps, image-based lighting, reflection probes, and light probes.

Exit criteria: a standard material test scene matches editor and client, scales to
hundreds of lights, and has frame/debug views for every render pass.

### 3. Light baking

Light baking is a pipeline, not a single button. It requires these stages:

1. Import or generate non-overlapping lightmap UV2 data.
2. Mark renderers Static, Dynamic, or Contribute GI and lights Realtime, Mixed,
   or Baked.
3. Build a bake scene using stable asset/entity IDs.
4. Progressively trace direct and indirect light on the GPU, with configurable
   samples and bounce count.
5. Denoise, dilate chart borders, compress, and write versioned `.lightbake`
   assets without modifying source meshes.
6. Bind directional lightmaps and shadow masks at runtime; sample light probes for
   dynamic objects.
7. Show progress, cancellation, stale-bake detection, texel-density overlays, and
   per-object bake diagnostics in the editor.

First supported bake: static meshes, emissive materials, Directional/Point/Spot
lights, indirect diffuse, mixed shadow masks, and probe volumes. Reflection-probe
baking follows after image-based lighting is complete.

Exit criteria: a Cornell-box reference scene survives restart/build, shows baked
indirect color transfer, and falls back safely when bake assets are stale/missing.

### 4. C# developer API

- Host .NET 10 through `nethost` and `hostfxr`; keep managed code behind a narrow
  C ABI so the native runtime does not depend on managed implementation details.
- Ship `Polyizon.Core` with typed Entity, Transform, Renderer, Material, Camera,
  Light, Input, Time, Physics, Audio, and logging APIs.
- Provide lifecycle methods: `Awake`, `OnEnable`, `Start`, `Update`, `FixedUpdate`,
  `OnDisable`, and `OnDestroy`.
- Serialize public/annotated fields into `.scene`, draw them automatically in the
  Inspector, preserve values across reload, and report compile errors in Console.
- Add per-project managed assemblies, IDE project generation, debugger attach,
  and collectible load contexts for hot reload.
- Provide a Lua migration guide and keep legacy Lua execution opt-in per project.

Exit criteria: create a C# component from the editor, compile it, expose fields,
hot-reload it, run it in editor/client, and debug it without restarting Polyizon.

### 5. Asset and world authoring

- Asset database, metadata files, dependency graph, background importing,
  thumbnails, search, labels, favorites, and drag/drop.
- Prefabs with variants and overrides; scene hierarchy reparenting and duplication.
- Cameras, render textures, decals, terrain, sky/environment profiles, particle
  systems, animation graphs, and timeline/sequencing.
- Physics (rigid bodies, colliders, triggers, raycasts), audio sources/listeners,
  input actions, navigation meshes, UI canvas, localization, and save data.
- Build profiles, development/release builds, asset cooking, startup scenes, and
  platform-specific settings.

### 6. Production quality

- CPU/GPU profiler, frame debugger, render graph viewer, memory/asset diagnostics,
  crash reports, validation presets, automated performance captures, and scene
  validation.
- Unit tests for serialization/ECS/math, golden-image tests for rendering, and
  editor interaction tests for authoring workflows.
- Example projects: material laboratory, indoor baked-lighting scene, outdoor
  atmospheric scene, physics playground, and C# gameplay sample.

## Migration rules

1. Do not delete a working subsystem until its replacement passes editor and
   client tests.
2. Every persisted format change increments a schema version and includes a
   loader for the previous format.
3. Editor-only code never becomes a runtime dependency.
4. No hidden fixed limits: expose limits, warn clearly, or move the data to
   dynamically sized GPU buffers.
5. A feature is complete only when it is authorable, serializable, renderable in
   editor and client, diagnosable, and covered by an automated test.

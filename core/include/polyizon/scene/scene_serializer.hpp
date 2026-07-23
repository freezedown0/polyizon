#pragma once

#include "polyizon/scene/scene.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>

namespace polyizon {

class VulkanContext;

// JSON scene save/load — the engine's first scene file format. Hand-written
// per-component (de)serialization for the known component types
// (Tag/Transform/Mesh/Material/Script/PointLight/SpotLight, see
// polyizon/scene/components.hpp), not a generic reflection system: not worth
// building for a handful of known types (see the Phase 16 plan). Lives in
// core (Phase 20) rather than editor-only — the standalone game client loads
// a scene at startup too, to run a compiled/exported project without the
// editor (see Application's project mode).

// Writes every entity that has at least a TransformComponent. Mesh/Material/
// Script/PointLight/SpotLight are each written only if present on that
// entity. Creates any missing parent directories for `path`.
void SaveScene(const Scene& scene, const std::filesystem::path& path);

// Re-imports each referenced mesh via polyizon/assets/mesh_import.hpp's
// LoadMesh (paths are resolved relative to the running executable's own
// directory, same convention as MeshComponent::sourcePath elsewhere) — a
// small in-memory cache keyed by path avoids reloading the same file for
// multiple entities that reference it. context must outlive every Mesh the
// returned Scene's MeshComponents reference.
Scene LoadScene(VulkanContext& context, const std::filesystem::path& path);

// In-memory equivalents of the above, with no disk I/O — SaveScene/LoadScene
// are thin file-reading/writing wrappers around these. Added in Phase 19 so
// EditorViewportRenderer::Play()/Stop() can snapshot and restore a scene
// (for Stop's "revert to how it looked before Play" behavior) without
// round-tripping through a temp file.
nlohmann::json SerializeSceneToJson(const Scene& scene);
Scene DeserializeSceneFromJson(VulkanContext& context, const nlohmann::json& root);

} // namespace polyizon

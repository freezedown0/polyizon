#pragma once

#include "polyizon/scene/scene.hpp"

#include <filesystem>

namespace polyizon {
class VulkanContext;
}

// JSON scene save/load — the engine's first scene file format. Hand-written
// per-component (de)serialization for the four known component types
// (Transform/Mesh/Material/Script, see polyizon/scene/components.hpp), not a
// generic reflection system: not worth building for four known types (see
// the Phase 16 plan). Global functions, not wrapped in `namespace polyizon`,
// matching mesh_import.hpp's identical editor-only-utility convention.

// Writes every entity that has at least a TransformComponent. Mesh/Material/
// Script are each written only if present on that entity. Creates any
// missing parent directories for `path`.
void SaveScene(const polyizon::Scene& scene, const std::filesystem::path& path);

// Re-imports each referenced mesh via mesh_import.hpp's LoadMesh (paths are
// resolved relative to the running executable's own directory, same
// convention as MeshComponent::sourcePath elsewhere) — a small in-memory
// cache keyed by path avoids reloading the same file for multiple entities
// that reference it. context must outlive every Mesh the returned Scene's
// MeshComponents reference.
polyizon::Scene LoadScene(polyizon::VulkanContext& context, const std::filesystem::path& path);

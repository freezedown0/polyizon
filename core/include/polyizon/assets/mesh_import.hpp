#pragma once

#include "polyizon/vulkan/mesh.hpp"

#include <filesystem>
#include <memory>

namespace polyizon {

class VulkanContext;

// Assimp-based mesh import. Supports .obj/.fbx (and every other format
// Assimp's importer registry handles) through one code path; no format-
// specific branching here. Lives in core (Phase 20) rather than editor-only —
// the standalone game client needs to load meshes at runtime too, to render
// a compiled/exported project without the editor (see Application's project
// mode).
//
// Extracts only position/normal/index data from the file's first mesh (see
// mesh_import.cpp) — no material/UV/multi-mesh handling this phase (flat
// colors are assigned in code via MaterialComponent, not read from the
// file).
std::shared_ptr<Mesh> LoadMesh(VulkanContext& context, const std::filesystem::path& path);

} // namespace polyizon

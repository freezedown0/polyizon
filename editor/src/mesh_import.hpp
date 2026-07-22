#pragma once

#include "polyizon/vulkan/mesh.hpp"

#include <filesystem>
#include <memory>

namespace polyizon {
class VulkanContext;
}

// Assimp-based mesh import — the only place in the editor that touches
// Assimp directly (kept out of core/ since Assimp is scoped to the editor
// target only, see editor/CMakeLists.txt). Supports .obj/.fbx (and every
// other format Assimp's importer registry handles) through one code path;
// no format-specific branching here.
//
// Extracts only position/normal/index data from the file's first mesh (see
// mesh_import.cpp) — no material/UV/multi-mesh handling this phase (flat
// colors are assigned in code via MaterialComponent, not read from the
// file).
std::shared_ptr<polyizon::Mesh> LoadMesh(polyizon::VulkanContext& context, const std::filesystem::path& path);

#pragma once

#include "polyizon/vulkan/mesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>

namespace polyizon {

// Human-readable entity identity, shown by the editor's Hierarchy panel (see
// editor/src/hierarchy_panel.hpp) — entities have no other name/label
// anywhere else in the engine. Every entity gets one; there's no notion of
// an unnamed entity.
struct TagComponent {
    std::string name = "Entity";
};

// Entity transform: position/rotation/scale, composed into a model matrix on
// demand rather than cached — this scene's entities (see EditorViewportRenderer)
// are static for now, so there's no dirty-tracking/invalidation to get wrong
// by always recomputing.
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::vec3 rotationEulerDegrees{0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 GetMatrix() const {
        glm::mat4 matrix = glm::translate(glm::mat4(1.0f), position);
        matrix = glm::rotate(matrix, glm::radians(rotationEulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        matrix = glm::rotate(matrix, glm::radians(rotationEulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        matrix = glm::rotate(matrix, glm::radians(rotationEulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::scale(matrix, scale);
    }
};

// Non-owning reference to a GPU mesh loaded elsewhere (see
// editor/src/mesh_import.hpp) — shared_ptr rather than a raw pointer since
// multiple entities could reasonably reference the same imported Mesh
// (not the case for this phase's plane+cube sample, but cheap to allow).
struct MeshComponent {
    std::shared_ptr<Mesh> mesh;
    // The file path mesh was imported from (see editor/src/mesh_import.hpp's
    // LoadMesh) — kept alongside the GPU mesh itself purely so
    // SceneSerializer (editor/src/scene_serializer.hpp) has something to
    // write out; Mesh itself has no notion of "where it came from."
    std::string sourcePath;
};

// Flat per-entity color for LitPipeline's shading (see lit.frag) — this
// phase has no per-vertex color or textured materials (see mesh_vertex.hpp).
struct MaterialComponent {
    glm::vec3 baseColor{1.0f};
};

// Optional: a Lua script file (see editor/src/script_engine.hpp) driving this
// entity's TransformComponent each frame. Just a path — the loaded/compiled
// Lua chunk is cached inside ScriptEngine itself, keyed by this path, not
// stored per-entity (entities are cheap to duplicate; script state should
// have a single lifetime tied to ScriptEngine, not to whichever entity
// happens to reference the file).
struct ScriptComponent {
    std::string scriptPath;
};

} // namespace polyizon

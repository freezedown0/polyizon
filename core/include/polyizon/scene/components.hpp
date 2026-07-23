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

    // Rotation-only (no translation/scale) local -Z axis, world-spaced — used
    // by SpotLightComponent to derive which way a light entity points from
    // its Transform alone, rather than a separate direction field. Same
    // X-then-Y-then-Z rotation composition as GetMatrix(), just without the
    // translate/scale that would distort a direction vector under non-uniform
    // scale.
    glm::vec3 GetForward() const {
        glm::mat4 rotation(1.0f);
        rotation = glm::rotate(rotation, glm::radians(rotationEulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotation = glm::rotate(rotation, glm::radians(rotationEulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotation = glm::rotate(rotation, glm::radians(rotationEulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::normalize(glm::vec3(rotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
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

// Omnidirectional light at this entity's TransformComponent::position — no
// shadow casting (only the scene's sun does, see ShadowMap/ShadowPipeline),
// just a forward-additive Lambertian contribution with linear-squared
// distance falloff to `range` (see EditorViewportRenderer::
// UpdateLitUniformBuffer and lit.frag/voxel_lit.frag's
// ComputePointLightContribution). Capped at kMaxPointLights simultaneously
// active lights per scene (see lit_uniform_buffer_object.hpp) — a scene with
// more than that just has the extras silently ignored, same "no soft cap
// warning" precedent as other fixed-size-array limits in this engine
// (kMaxFramesInFlight, kMaxPointLights itself).
struct PointLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
};

// Same falloff model as PointLightComponent, additionally narrowed to a cone
// pointing along this entity's TransformComponent::GetForward() — no
// separate direction field, so rotating the entity in the Inspector aims the
// light. innerConeDegrees is the fully-lit cone half-angle; the light fades
// to zero between inner and outerConeDegrees (outer should be >=
// inner — not enforced, an inverted cone just fades over zero degrees,
// i.e. a hard edge, rather than crashing).
struct SpotLightComponent {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 30.0f;
};

} // namespace polyizon

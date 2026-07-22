#pragma once

#include <volk.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace polyizon {

// Vertex layout for imported 3D meshes (see mesh.hpp/mesh_import.hpp) —
// deliberately separate from the existing 2D Vertex (vertex.hpp, used by the
// untouched quad demo pipeline) rather than extending it: that struct's 2D
// position and per-vertex color don't apply here, and touching it would risk
// the working quad pipeline. No texCoord/color: LitPipeline uses one flat
// base color per entity (pushed as a constant, see MaterialComponent), not
// per-vertex color or textures, this phase.
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;

    static VkVertexInputBindingDescription GetBindingDescription() noexcept {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex3D);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions() noexcept {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex3D, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex3D, normal);
        return attributes;
    }
};

} // namespace polyizon

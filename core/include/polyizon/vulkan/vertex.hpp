#pragma once

#include <volk.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace polyizon {

// Single fixed vertex layout for the current quad geometry — matched 1:1 by
// GraphicsPipeline::CreatePipeline's vertex input state and by
// triangle.vert's `in` attributes. Not a generic vertex-layout system:
// there's exactly one vertex format needed right now (pipeline.cpp already
// hardcodes shader filenames the same way).
struct Vertex {
    glm::vec2 position;
    glm::vec3 color;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription GetBindingDescription() noexcept {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions() noexcept {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, color);

        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, texCoord);
        return attributes;
    }
};

} // namespace polyizon

#include "polyizon/vulkan/mesh.hpp"

#include "polyizon/vulkan/buffer.hpp"

namespace polyizon {

Mesh::Mesh(VulkanContext& context, const std::vector<Vertex3D>& vertices, const std::vector<std::uint32_t>& indices)
    : m_IndexCount(static_cast<std::uint32_t>(indices.size())) {
    m_VertexBuffer = Buffer::CreateDeviceLocal(context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertices.data(), sizeof(Vertex3D) * vertices.size());
    m_IndexBuffer = Buffer::CreateDeviceLocal(context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indices.data(), sizeof(std::uint32_t) * indices.size());
}

} // namespace polyizon

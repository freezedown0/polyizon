#pragma once

#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/mesh_vertex.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace polyizon {

class VulkanContext;

// RAII wrapper over a single static, device-local 3D mesh: a Vertex3D vertex
// buffer + a uint32 index buffer, built once via Buffer::CreateDeviceLocal's
// staging-upload path (same path Application's quad vertex/index buffers
// already use). Indices are uint32_t, not uint16_t like the quad demo's
// Vertex/index buffers (vertex.hpp) — imported models (see mesh_import.hpp)
// aren't guaranteed to stay under 65536 vertices.
class Mesh {
public:
    Mesh(VulkanContext& context, const std::vector<Vertex3D>& vertices, const std::vector<std::uint32_t>& indices);

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = delete;
    Mesh& operator=(Mesh&&) = delete;

    VkBuffer GetVertexBuffer() const noexcept { return m_VertexBuffer->GetBuffer(); }
    VkBuffer GetIndexBuffer() const noexcept { return m_IndexBuffer->GetBuffer(); }
    std::uint32_t GetIndexCount() const noexcept { return m_IndexCount; }

private:
    std::unique_ptr<Buffer> m_VertexBuffer;
    std::unique_ptr<Buffer> m_IndexBuffer;
    std::uint32_t m_IndexCount = 0;
};

} // namespace polyizon

#pragma once

#include <volk.h>

#include <memory>

// Both VmaAllocator and VmaAllocation are VK_DEFINE_HANDLE'd (i.e.
// `typedef struct X_T* X;`) at GLOBAL namespace scope in vk_mem_alloc.h.
// Forward-declared here at matching scope so this header stays
// self-sufficient without requiring callers to include vk_mem_alloc.h or
// context.hpp first (same reasoning as context.hpp's own VmaAllocator
// forward-declare — getting this wrong creates a distinct, incompatible
// type, a real bug already hit once).
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace polyizon {

class VulkanContext;

// RAII wrapper over a VkBuffer + its VmaAllocation — VMA is this project's
// only sanctioned path to GPU memory, never manual vkAllocateMemory.
//
// Two ways to end up with a Buffer:
//  - The public constructor: host-visible, persistently mapped, written via
//    Upload(). Used directly for transient staging buffers, and is the
//    right constructor for future host-writable-per-frame data (uniform
//    buffers) once that exists.
//  - CreateDeviceLocal(): the path for static, GPU-read-heavy geometry.
//    Builds a temporary staging Buffer via the constructor above, uploads
//    the initial data into it, then records+submits+waits on a one-time
//    copy into a device-local buffer before the staging buffer is
//    destroyed.
class Buffer {
public:
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    VkBuffer GetBuffer() const noexcept { return m_Buffer; }

    // Only valid on a buffer built via the public constructor (host-visible
    // + persistently mapped) — throws otherwise.
    void Upload(const void* data, VkDeviceSize size);

    static std::unique_ptr<Buffer> CreateDeviceLocal(
        VulkanContext& context, VkBufferUsageFlags usage, const void* data, VkDeviceSize size);

private:
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void Destroy();

    // Non-owning: whoever constructs this (Application, or CreateDeviceLocal
    // itself) guarantees the VulkanContext that owns this allocator outlives
    // the Buffer — same reasoning as GraphicsPipeline caching a bare
    // VkDevice instead of a VulkanContext&.
    VmaAllocator m_Allocator = VK_NULL_HANDLE;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    void* m_MappedData = nullptr; // non-null only for host-visible buffers
};

} // namespace polyizon

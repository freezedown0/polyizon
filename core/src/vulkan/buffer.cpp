#include "polyizon/vulkan/buffer.hpp"

#include "polyizon/vulkan/context.hpp"

#include <vk_mem_alloc.h>

#include <cstring>
#include <stdexcept>

namespace polyizon {

namespace {

// One-time staging->device-local copy. Uses its own short-lived transient
// pool, not Application's per-frame command pool: this runs during
// Application's constructor, before CreateFrameSyncObjects() has even
// created that pool, so keeping it self-contained avoids any dependency on
// Application's frame-sync setup order.
void CopyBufferAndWait(VulkanContext& context, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.GetGraphicsQueueFamily();

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(context.GetDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transient command pool for buffer upload");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
        throw std::runtime_error("Failed to allocate buffer upload command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);

    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    // No fence: this runs once during Application's constructor, well before
    // the render loop or its frame-in-flight fences exist. A blocking
    // vkQueueWaitIdle is simplest and correct here, not a pattern to repeat
    // per-frame.
    if (vkQueueSubmit2(context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
        throw std::runtime_error("Failed to submit buffer upload command buffer");
    }
    vkQueueWaitIdle(context.GetGraphicsQueue());

    vkDestroyCommandPool(context.GetDevice(), pool, nullptr); // also frees cmd
}

} // namespace

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage)
    : Buffer(allocator, size, usage, /*hostVisible=*/true) {}

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible)
    : m_Allocator(allocator) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) {
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    // Device-local case: no host-access flags, so VMA_MEMORY_USAGE_AUTO
    // prefers VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT with no mapping possible.

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &allocationInfo) !=
        VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }
    m_MappedData = hostVisible ? allocationInfo.pMappedData : nullptr;
}

Buffer::~Buffer() {
    Destroy();
}

void Buffer::Upload(const void* data, VkDeviceSize size) {
    if (m_MappedData == nullptr) {
        throw std::runtime_error("Buffer::Upload called on a non-host-visible buffer");
    }
    std::memcpy(m_MappedData, data, static_cast<std::size_t>(size));
}

std::unique_ptr<Buffer> Buffer::CreateDeviceLocal(
    VulkanContext& context, VkBufferUsageFlags usage, const void* data, VkDeviceSize size) {
    Buffer staging(context.GetAllocator(), size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staging.Upload(data, size);

    auto result = std::unique_ptr<Buffer>(
        new Buffer(context.GetAllocator(), size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, /*hostVisible=*/false));

    CopyBufferAndWait(context, staging.GetBuffer(), result->GetBuffer(), size);
    return result;
}

void Buffer::Destroy() {
    if (m_Buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation); // unmaps automatically if mapped
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        m_MappedData = nullptr;
    }
}

} // namespace polyizon

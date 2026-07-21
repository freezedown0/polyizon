#include "polyizon/vulkan/texture3d.hpp"

#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"

#include <vk_mem_alloc.h>

#include <stdexcept>

namespace polyizon {

namespace {

// One-time staging-buffer->device-local-3D-image copy. Near-duplicate of
// image.cpp's private CopyBufferToImageAndWait (only imageExtent's depth
// differs from 1) — duplicated rather than shared, consistent with
// image.cpp itself not being touched for a small shared-utility savings.
void CopyBufferToImage3DAndWait(VulkanContext& context, VkBuffer src, VkImage dst,
    std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.GetGraphicsQueueFamily();

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(context.GetDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transient command pool for 3D texture upload");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
        throw std::runtime_error("Failed to allocate 3D texture upload command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier2 toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toTransferDst.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toTransferDst.srcAccessMask = VK_ACCESS_2_NONE;
    toTransferDst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toTransferDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = dst;
    toTransferDst.subresourceRange = range;

    VkDependencyInfo toTransferDstDep{};
    toTransferDstDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toTransferDstDep.imageMemoryBarrierCount = 1;
    toTransferDstDep.pImageMemoryBarriers = &toTransferDst;
    vkCmdPipelineBarrier2(cmd, &toTransferDstDep);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, depth };
    vkCmdCopyBufferToImage(cmd, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier2 toShaderRead = toTransferDst;
    toShaderRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toShaderRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDependencyInfo toShaderReadDep{};
    toShaderReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toShaderReadDep.imageMemoryBarrierCount = 1;
    toShaderReadDep.pImageMemoryBarriers = &toShaderRead;
    vkCmdPipelineBarrier2(cmd, &toShaderReadDep);

    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    if (vkQueueSubmit2(context.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
        throw std::runtime_error("Failed to submit 3D texture upload command buffer");
    }
    vkQueueWaitIdle(context.GetGraphicsQueue());

    vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
}

} // namespace

Texture3D::Texture3D(VulkanContext& context, const void* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t depth)
    : m_Device(context.GetDevice())
    , m_Allocator(context.GetAllocator()) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * depth * 4;

    Buffer staging(m_Allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staging.Upload(pixels, imageSize);

    CreateImage(m_Allocator, width, height, depth);
    CopyBufferToImage3DAndWait(context, staging.GetBuffer(), m_Image, width, height, depth);

    CreateImageView();
    CreateSampler();
}

Texture3D::~Texture3D() {
    Destroy();
}

void Texture3D::CreateImage(VmaAllocator allocator, std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // raw density/noise data, not sRGB color
    imageInfo.extent = { width, height, depth };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan 3D image");
    }
}

void Texture3D::CreateImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan 3D image view");
    }
}

void Texture3D::CreateSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // REPEAT: the cloud shader wind-scrolls UVs continuously and relies on
    // this texture tiling seamlessly (see noise.hpp) rather than clamping.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan 3D texture sampler");
    }
}

void Texture3D::Destroy() {
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }
    if (m_Image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
    }
}

} // namespace polyizon

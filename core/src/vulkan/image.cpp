#include "polyizon/vulkan/image.hpp"

#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"

#include <vk_mem_alloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <stdexcept>

namespace polyizon {

namespace {

// Resolved relative to the running executable's own directory, matching
// GraphicsPipeline's identical private helper for shaders — duplicated here
// rather than shared, since extracting a common utility would mean touching
// already-verified code for a one-time savings of a few lines.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

// One-time staging-buffer->device-local-image copy: a buffer-to-image copy
// needs layout transitions around it (UNDEFINED -> TRANSFER_DST_OPTIMAL ->
// SHADER_READ_ONLY_OPTIMAL), so this can't reuse buffer.cpp's
// CopyBufferAndWait verbatim (that one only moves data between two
// buffers). Same transient-pool, no-fence, vkQueueWaitIdle shape though —
// this runs once during Application's constructor, before any frame-sync
// fences exist.
void CopyBufferToImageAndWait(VulkanContext& context, VkBuffer src, VkImage dst, std::uint32_t width, std::uint32_t height) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = context.GetGraphicsQueueFamily();

    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(context.GetDevice(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transient command pool for texture upload");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(context.GetDevice(), &allocInfo, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(context.GetDevice(), pool, nullptr);
        throw std::runtime_error("Failed to allocate texture upload command buffer");
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
    region.bufferRowLength = 0;   // tightly packed
    region.bufferImageHeight = 0; // tightly packed
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };
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
        throw std::runtime_error("Failed to submit texture upload command buffer");
    }
    vkQueueWaitIdle(context.GetGraphicsQueue());

    vkDestroyCommandPool(context.GetDevice(), pool, nullptr); // also frees cmd
}

} // namespace

Image::Image(VulkanContext& context, const void* pixels, std::uint32_t width, std::uint32_t height)
    : m_Device(context.GetDevice())
    , m_Allocator(context.GetAllocator()) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    Buffer staging(m_Allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staging.Upload(pixels, imageSize);

    CreateImage(m_Allocator, width, height);
    CopyBufferToImageAndWait(context, staging.GetBuffer(), m_Image, width, height);

    CreateImageView();
    CreateSampler();
}

Image::~Image() {
    Destroy();
}

std::unique_ptr<Image> Image::CreateFromFile(VulkanContext& context, const std::string& fileName) {
    const std::filesystem::path path = GetExecutableDirectory() / "textures" / fileName;

    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channelsInFile, STBI_rgb_alpha);
    if (pixels == nullptr) {
        throw std::runtime_error("Failed to load texture file: " + path.string());
    }

    auto image = std::make_unique<Image>(
        context, pixels, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    stbi_image_free(pixels);
    return image;
}

void Image::CreateImage(VmaAllocator allocator, std::uint32_t width, std::uint32_t height) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO; // no host-access flags: device-local, same as Buffer's device-local path

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image");
    }
}

void Image::CreateImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image view");
    }
}

void Image::CreateSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE; // one demo texture: not worth querying device limits for
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f; // no mips

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan sampler");
    }
}

void Image::Destroy() {
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

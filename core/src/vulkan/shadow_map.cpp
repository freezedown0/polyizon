#include "polyizon/vulkan/shadow_map.hpp"

#include "polyizon/vulkan/context.hpp"

#include <vk_mem_alloc.h>

#include <stdexcept>

namespace polyizon {

ShadowMap::ShadowMap(VulkanContext& context, VkFormat depthFormat, std::uint32_t resolution)
    : m_Device(context.GetDevice())
    , m_Allocator(context.GetAllocator())
    , m_Format(depthFormat)
    , m_Resolution(resolution) {
    CreateImage();
    CreateImageView();
    CreateSampler();
}

ShadowMap::~ShadowMap() {
    Destroy();
}

void ShadowMap::CreateImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_Format;
    imageInfo.extent = { m_Resolution, m_Resolution, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // DEPTH_STENCIL_ATTACHMENT: ShadowPipeline renders depth into this every
    // frame. SAMPLED: LitPipeline reads it back via a comparison sampler.
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map image");
    }
}

void ShadowMap::CreateImageView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;
    // Depth-only aspect, same reasoning as Swapchain's own depth image view:
    // ChooseDepthFormat-selected formats here are always pure-depth
    // candidates too (see the constructor's depthFormat parameter comment).
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map image view");
    }
}

void ShadowMap::CreateSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // LINEAR filtering enables hardware percentage-closer filtering for
    // smooth shadow visibility at the sample boundary.
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // CLAMP_TO_BORDER + opaque-white border: world positions outside the
    // light's orthographic frustum (see LitUniformBufferObject::lightSpaceMatrix)
    // sample the border color instead of wrapping/clamping into unrelated
    // shadow data — an out-of-frustum depth of 1.0 compares as "closer than
    // the light" against any in-scene fragment depth, i.e. fully lit.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    // Sampling this in-shader via a `sampler2DShadow` (see
    // lit.frag) returns a single-tap [0,1] visibility factor directly,
    // comparing the interpolated reference depth (3rd texture coordinate)
    // against the stored depth with this compareOp — no manual PCF loop
    // needed.
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow map sampler");
    }
}

void ShadowMap::Destroy() {
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

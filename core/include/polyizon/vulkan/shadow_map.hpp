#pragma once

#include <volk.h>

#include <cstdint>

// Both VK_DEFINE_HANDLE'd at GLOBAL namespace scope in vk_mem_alloc.h —
// forward-declared here at matching scope, mirroring Image's/Texture3D's
// identical comment.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace polyizon {

class VulkanContext;

// RAII depth-only render target for shadow mapping: a single VkImage +
// VkImageView + a hardware comparison sampler,
// sized independently of the swapchain (fixed resolution, doesn't need
// recreating on window resize — unlike Swapchain's own depth buffer).
// Mirrors Texture3D/Image's shape (image + view + sampler, no separate
// wrapper class needed) but this one is written to by ShadowPipeline's
// depth-only pass rather than uploaded once from CPU data. The same
// addressMode is CLAMP_TO_BORDER with an opaque-white border,
// so world positions that fall outside the light's orthographic frustum
// sample as "fully lit" rather than wrapping/clamping into unrelated shadow
// data.
class ShadowMap {
public:
    // depthFormat should be the same format Swapchain chose for its own
    // depth buffer (VulkanContext/GPU support for optional depth formats
    // like D32_SFLOAT vs X8_D24_UNORM_PACK32 varies — reusing the already-
    // queried result avoids a second, duplicate format-support query).
    ShadowMap(VulkanContext& context, VkFormat depthFormat, std::uint32_t resolution);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&&) = delete;
    ShadowMap& operator=(ShadowMap&&) = delete;

    VkImage GetImage() const noexcept { return m_Image; }
    VkImageView GetImageView() const noexcept { return m_ImageView; }
    VkSampler GetSampler() const noexcept { return m_Sampler; }
    VkFormat GetFormat() const noexcept { return m_Format; }
    std::uint32_t GetResolution() const noexcept { return m_Resolution; }

private:
    void CreateImage();
    void CreateImageView();
    void CreateSampler();
    void Destroy();

    // Non-owning: same "cache only what's needed post-construction"
    // precedent as Image/Texture3D/Buffer/GraphicsPipeline.
    VkDevice m_Device = VK_NULL_HANDLE;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    std::uint32_t m_Resolution = 0;
    VkImage m_Image = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};

} // namespace polyizon

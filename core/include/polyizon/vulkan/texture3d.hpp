#pragma once

#include <volk.h>

#include <cstdint>

// Both VmaAllocator and VmaAllocation are VK_DEFINE_HANDLE'd (i.e.
// `typedef struct X_T* X;`) at GLOBAL namespace scope in vk_mem_alloc.h.
// Forward-declared here at matching scope, mirroring Image's/Buffer's
// identical comment.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace polyizon {

class VulkanContext;

// RAII wrapper over a single static, device-local, shader-sampled 3D
// texture: a VkImage + VmaAllocation + VkImageView + VkSampler. Mirrors
// Image's exact shape (staging-buffer upload, one mip level, one array
// layer, no reload/streaming) but is a distinct class rather than an
// Image extension — Image's own doc comment scopes it to "one static 2D
// texture... not a generic texture system," and its format is hardcoded
// VK_FORMAT_R8G8B8A8_SRGB, which would corrupt this class's raw
// density/noise data (not display-referred color). Used for the cloud
// noise volume (see noise.hpp) sampled by sky.frag.
class Texture3D {
public:
    // pixels must be tightly packed RGBA8 (4 bytes/texel),
    // width*height*depth*4 bytes total. Format is always
    // VK_FORMAT_R8G8B8A8_UNORM (see .cpp) — raw data, not sRGB color.
    Texture3D(VulkanContext& context, const void* pixels, std::uint32_t width, std::uint32_t height, std::uint32_t depth);
    ~Texture3D();

    Texture3D(const Texture3D&) = delete;
    Texture3D& operator=(const Texture3D&) = delete;
    Texture3D(Texture3D&&) = delete;
    Texture3D& operator=(Texture3D&&) = delete;

    VkImageView GetImageView() const noexcept { return m_ImageView; }
    VkSampler GetSampler() const noexcept { return m_Sampler; }

private:
    void CreateImage(VmaAllocator allocator, std::uint32_t width, std::uint32_t height, std::uint32_t depth);
    void CreateImageView();
    void CreateSampler();
    void Destroy();

    // Non-owning: same "cache only what's needed post-construction"
    // precedent as Image/Buffer/GraphicsPipeline.
    VkDevice m_Device = VK_NULL_HANDLE;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    VkImage m_Image = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};

} // namespace polyizon

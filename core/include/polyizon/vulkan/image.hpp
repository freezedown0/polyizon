#pragma once

#include <volk.h>

#include <cstdint>
#include <memory>
#include <string>

// Both VmaAllocator and VmaAllocation are VK_DEFINE_HANDLE'd (i.e.
// `typedef struct X_T* X;`) at GLOBAL namespace scope in vk_mem_alloc.h.
// Forward-declared here at matching scope, mirroring buffer.hpp's identical
// comment — getting this wrong creates a distinct, incompatible type, a
// real bug already hit once.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace polyizon {

class VulkanContext;

// RAII wrapper over a single static, device-local, shader-sampled 2D
// texture: a VkImage + VmaAllocation + VkImageView + VkSampler. Mirrors
// Buffer's staging-upload pattern (CreateDeviceLocal) but for images, plus
// the image layout transitions a buffer-to-image copy requires. One
// format, one mip level, one array layer, no reload/streaming — matches
// the "one static texture" scope of this phase, not a generic texture
// system.
class Image {
public:
    // pixels must be tightly packed RGBA8 (4 bytes/texel), width*height*4
    // bytes total. Format is always VK_FORMAT_R8G8B8A8_SRGB (see .cpp).
    Image(VulkanContext& context, const void* pixels, std::uint32_t width, std::uint32_t height);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) = delete;
    Image& operator=(Image&&) = delete;

    // Loads fileName from <executable-dir>/textures/ via stb_image (forcing
    // RGBA8), then delegates to the constructor above — mirrors
    // GraphicsPipeline resolving shader filenames relative to the
    // executable's own directory.
    static std::unique_ptr<Image> CreateFromFile(VulkanContext& context, const std::string& fileName);

    VkImageView GetImageView() const noexcept { return m_ImageView; }
    VkSampler GetSampler() const noexcept { return m_Sampler; }

private:
    void CreateImage(VmaAllocator allocator, std::uint32_t width, std::uint32_t height);
    void CreateImageView();
    void CreateSampler();
    void Destroy();

    // Non-owning: same "cache only what's needed post-construction"
    // precedent as Buffer/GraphicsPipeline. Whoever constructs this
    // guarantees VulkanContext outlives it.
    VkDevice m_Device = VK_NULL_HANDLE;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    VkImage m_Image = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
};

} // namespace polyizon

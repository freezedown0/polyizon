#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

// VmaAllocation is VK_DEFINE_HANDLE'd at GLOBAL namespace scope in
// vk_mem_alloc.h. Forward-declared here at matching scope (same pattern as
// context.hpp/buffer.hpp/image.hpp) now that Swapchain owns a depth image's
// VmaAllocation too — getting this wrong creates a distinct, incompatible
// type, a real bug already hit once.
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace polyizon {

class VulkanContext;
class Window;

// Owns the VkSwapchainKHR, its VkImages, one VkImageView per image, and one
// render-finished VkSemaphore per image. Per-image semaphores (not per
// frame-in-flight) are used for render-finished signaling: with
// VK_PRESENT_MODE_MAILBOX_KHR, a frame-in-flight-indexed semaphore can still
// be pending when a later frame reuses that slot, since presentation timing
// doesn't line up 1:1 with frame-in-flight slots. Indexing by acquired image
// index (which this class already tracks) avoids that hazard.
//
// Frame-in-flight synchronization (command pool/buffers, per-frame
// image-available semaphores + fences) lives in Application, not here —
// this class's job stops at "manage the presentable images."
class Swapchain {
public:
    Swapchain(VulkanContext& context, Window& window);

    // Additive: width/height supplied directly by the caller instead of a
    // live Window& query — used by the Qt-hosted editor viewport, which has
    // no Window/GLFW object at all. Same "which windowing system provided
    // the surface" split as VulkanContext's two constructors; the GLFW
    // constructor above is untouched.
    Swapchain(VulkanContext& context, std::uint32_t width, std::uint32_t height);

    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    enum class AcquireResult {
        Success,
        Recreated, // swapchain was (re)built this call — caller should skip this frame
        NotReady,  // window is minimized (zero framebuffer extent) — nothing to draw
    };

    AcquireResult AcquireNextImage(VkSemaphore imageAvailableSemaphore, std::uint32_t& outImageIndex);
    void Present(VkSemaphore waitSemaphore, std::uint32_t imageIndex);

    // Called from Application::OnWindowResize. Recreation itself is deferred
    // to the next AcquireNextImage() call, never done synchronously here. The
    // GLFW-constructed path re-reads the live Window& at that later point, so
    // no new size needs to travel through this call.
    void NotifyResized() noexcept { m_NeedsRecreate = true; }

    // Win32/Qt-path equivalent of NotifyResized(): there's no Window& to
    // re-query later, so the caller (VulkanViewportWindow::resizeEvent)
    // passes the new size directly. Also deferred to the next
    // AcquireNextImage() call, same as the GLFW path.
    void Resize(std::uint32_t width, std::uint32_t height) noexcept {
        m_Width = width;
        m_Height = height;
        m_NeedsRecreate = true;
    }

    VkFormat GetImageFormat() const noexcept { return m_ImageFormat; }
    VkExtent2D GetExtent() const noexcept { return m_Extent; }
    std::uint32_t GetImageCount() const noexcept { return static_cast<std::uint32_t>(m_Images.size()); }
    VkImage GetImage(std::uint32_t index) const { return m_Images[index]; }
    VkImageView GetImageView(std::uint32_t index) const { return m_ImageViews[index]; }
    VkSemaphore GetRenderFinishedSemaphore(std::uint32_t index) const { return m_RenderFinishedSemaphores[index]; }

    VkImage GetDepthImage() const noexcept { return m_DepthImage; }
    VkImageView GetDepthImageView() const noexcept { return m_DepthImageView; }
    VkFormat GetDepthFormat() const noexcept { return m_DepthFormat; }

private:
    void CreateSwapchain();
    void CreateImageViews();
    void CreateDepthResources();
    void CreateRenderFinishedSemaphores();
    void Recreate();
    void DestroySwapchainResources();
    void DestroyDepthResources();
    void Destroy();

    // Current width/height, regardless of which constructor was used: the
    // GLFW path re-reads the live Window& (m_Window non-null); the Win32/Qt
    // path returns the last size passed to Resize() (m_Window null, see the
    // second constructor). Defined in the .cpp, not inline here: calling
    // Window::GetWidth()/GetHeight() needs its complete type, and this header
    // only forward-declares Window.
    std::uint32_t GetCurrentWidth() const noexcept;
    std::uint32_t GetCurrentHeight() const noexcept;

    VulkanContext& m_Context;
    // Non-owning; null when constructed via the Win32/Qt-facing overload,
    // which has no Window/GLFW object to point at (see GetCurrentWidth/Height
    // and the Resize()-based path above).
    Window* m_Window = nullptr;
    std::uint32_t m_Width = 0;
    std::uint32_t m_Height = 0;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};

    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_ImageViews;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;

    // Depth buffer: extent-dependent like m_Images/m_ImageViews, so it's
    // recreated alongside them in Recreate(). Format is chosen once, from
    // what the physical device actually supports, and never changes across
    // recreations (same physical device/surface pair) — same reasoning as
    // m_ImageFormat's stability.
    VkImage m_DepthImage = VK_NULL_HANDLE;
    VmaAllocation m_DepthImageAllocation = VK_NULL_HANDLE;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;
    VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

    bool m_NeedsRecreate = false;
};

} // namespace polyizon

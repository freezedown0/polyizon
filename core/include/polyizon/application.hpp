#pragma once

#include "polyizon/camera.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/image.hpp"
#include "polyizon/vulkan/pipeline.hpp"
#include "polyizon/vulkan/swapchain.hpp"
#include "polyizon/vulkan/uniform_buffer_object.hpp"
#include "polyizon/vulkan/vertex.hpp"
#include "polyizon/window.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace polyizon {

struct ApplicationSpec {
    std::string name = "Polyizon Application";
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;
};

// Owns the window and drives the main loop. Derive from this and override
// OnUpdate() once there's per-frame logic to run (ECS registry tick, renderer
// submission, etc.) — none of that exists yet, so the base loop just pumps
// window events and tracks delta time.
class Application {
public:
    explicit Application(const ApplicationSpec& spec = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Stop();

    Window& GetWindow() noexcept { return *m_Window; }
    const Window& GetWindow() const noexcept { return *m_Window; }

protected:
    virtual void OnUpdate(float deltaTime);

private:
    void OnWindowResize(std::uint32_t width, std::uint32_t height);
    void OnWindowClose();
    void OnKey(int key, int scancode, int action, int mods);
    void OnCursorPos(double x, double y);
    void ProcessCameraKeyboardInput(float deltaTime);

    void CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void RenderFrame();

    static constexpr std::uint32_t kMaxFramesInFlight = 2;
    static constexpr float kMaxDeltaTime = 0.1f; // clamp for stalls (window drag-resize, debugger pause, etc.)

    std::unique_ptr<Window> m_Window;
    // Declared after m_Window so it's destroyed first (reverse declaration
    // order): the surface must not outlive the window it was created from.
    std::unique_ptr<VulkanContext> m_VulkanContext;
    // Declared after m_VulkanContext for the same reason: the swapchain must
    // not outlive the device/surface it was created from.
    std::unique_ptr<Swapchain> m_Swapchain;
    // Declared after m_Swapchain (needs its color AND depth format at
    // construction); destroyed before m_VulkanContext per
    // reverse-declaration-order destruction. Not coupled to
    // Swapchain::Recreate() — viewport/scissor are dynamic state, and both
    // formats are stable across recreations (same physical device/surface
    // pair; the depth format is chosen once too, see Swapchain), so the
    // formats this was built with never go stale. The swapchain's depth
    // *image* itself does get recreated on resize (inside Swapchain), just
    // not this pipeline.
    std::unique_ptr<GraphicsPipeline> m_Pipeline;

    // The one static texture (checkerboard.png), uploaded once via
    // Image::CreateFromFile. Must be constructed BEFORE
    // CreateDescriptorResources() — unlike m_VertexBuffer/m_IndexBuffer
    // below, which aren't referenced by any descriptor and can be created
    // after — because its VkImageView/VkSampler are written into the
    // descriptor sets there; if they don't exist yet, vkUpdateDescriptorSets
    // writes garbage handles. Declared here, before m_VertexBuffer, to match
    // construction order.
    std::unique_ptr<Image> m_Texture;

    // Static quad geometry, uploaded once via Buffer::CreateDeviceLocal's
    // staging-buffer path. Declared after m_VulkanContext (needs its
    // allocator/device/queue) so these are destroyed before it, per
    // reverse-declaration-order destruction.
    std::unique_ptr<Buffer> m_VertexBuffer;
    std::unique_ptr<Buffer> m_IndexBuffer;

    // Per-frame-in-flight UBO buffers (host-visible, re-Upload()ed every
    // frame in RenderFrame()) and the descriptor sets that point at them.
    // Declared after m_VulkanContext (needs its allocator) so these are
    // destroyed before it, per reverse-declaration-order destruction.
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_UniformBuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_DescriptorSets{};

    // Frame-in-flight sync state, indexed by m_CurrentFrame (not swapchain
    // image index — see Swapchain for why render-finished semaphores differ).
    // Raw handles + a private Destroy() helper, matching VulkanContext's
    // style, rather than a dedicated wrapper type: not promoted to a
    // Renderer class yet since there's no rendering work beyond a clear.
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_CommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> m_ImageAvailableSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> m_InFlightFences{};
    std::uint32_t m_CurrentFrame = 0;

    bool m_Running = true;
    float m_LastFrameTime = 0.0f;

    // Free-fly camera driving the view matrix (see UpdateUniformBuffer()).
    // Owns no GPU/GLFW resource, so its declaration position here has no
    // destruction-order implications. Default-constructed state reproduces
    // the previous hardcoded eye=(0,0,2) lookAt(origin) exactly.
    Camera m_Camera;
    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;
    bool m_FirstMouseSample = true;
};

} // namespace polyizon

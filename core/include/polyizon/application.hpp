#pragma once

#include "polyizon/camera.hpp"
#include "polyizon/game_scene_renderer.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/image.hpp"
#include "polyizon/vulkan/pipeline.hpp"
#include "polyizon/vulkan/sky_pipeline.hpp"
#include "polyizon/vulkan/sky_uniform_buffer_object.hpp"
#include "polyizon/vulkan/swapchain.hpp"
#include "polyizon/vulkan/texture3d.hpp"
#include "polyizon/vulkan/uniform_buffer_object.hpp"
#include "polyizon/vulkan/vertex.hpp"
#include "polyizon/window.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace polyizon {

struct ApplicationSpec {
    std::string name = "Polyizon Application";
    std::uint32_t windowWidth = 1280;
    std::uint32_t windowHeight = 720;

    // Phase 20: if set, Application runs in "project mode" — it loads and
    // renders this project's default scene (via GameSceneRenderer) instead
    // of the hardcoded quad/instancing demo, and runs every entity's Lua
    // script each frame. This is what makes a "Build Game" export (see the
    // editor's game_builder.hpp) actually playable: the exported folder's
    // own project.json sits next to the game exe, and main.cpp points this
    // at it. Left unset, Application behaves exactly as before (the
    // hardcoded demo) — this field is purely additive.
    std::optional<std::filesystem::path> projectDir;
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
    void OnMouseButton(int button, int action, int mods);
    void OnScroll(double xOffset, double yOffset);
    void OnWindowFocus(int focused);
    void OnCursorEnter(int entered);
    void ProcessCameraKeyboardInput(float deltaTime);

    void CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void UpdateInstanceBuffer(std::uint32_t frameIndex, float time);
    void UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time);
    void RenderFrame(float deltaTime);
    // The original hardcoded quad/instancing/sky demo, unchanged since
    // before Phase 20 — see RenderFrame()'s m_ProjectMode branch.
    void RenderDemoFrame(VkCommandBuffer cmd, std::uint32_t imageIndex, VkExtent2D extent, float time);
    void InitImGui();
    void ShutdownImGui();
    void BuildDebugOverlay();

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

    // Background sky/cloud pass, drawn first each frame (see RenderFrame()).
    // Same construction-order reasoning as m_Pipeline above: needs the
    // swapchain's color/depth formats, doesn't touch Swapchain::Recreate().
    std::unique_ptr<SkyPipeline> m_SkyPipeline;

    // The one static texture (checkerboard.png), uploaded once via
    // Image::CreateFromFile. Must be constructed BEFORE
    // CreateDescriptorResources() — unlike m_VertexBuffer/m_IndexBuffer
    // below, which aren't referenced by any descriptor and can be created
    // after — because its VkImageView/VkSampler are written into the
    // descriptor sets there; if they don't exist yet, vkUpdateDescriptorSets
    // writes garbage handles. Declared here, before m_VertexBuffer, to match
    // construction order.
    std::unique_ptr<Image> m_Texture;

    // Cloud noise volume (see noise.hpp), uploaded once via Texture3D. Same
    // "must precede CreateDescriptorResources()" ordering requirement as
    // m_Texture above — its view/sampler get written into the sky
    // descriptor sets there.
    std::unique_ptr<Texture3D> m_CloudNoiseTexture;

    // Static quad geometry, uploaded once via Buffer::CreateDeviceLocal's
    // staging-buffer path. Declared after m_VulkanContext (needs its
    // allocator/device/queue) so these are destroyed before it, per
    // reverse-declaration-order destruction.
    std::unique_ptr<Buffer> m_VertexBuffer;
    std::unique_ptr<Buffer> m_IndexBuffer;

    // Per-frame-in-flight instance-transform buffers (pipeline binding 1),
    // host-visible and re-Upload()ed every frame in RenderFrame() — grouped
    // here with the other buffers bound at draw time, even though its
    // upload pattern (rewritten every frame) matches m_UniformBuffers below
    // rather than the static geometry beside it. Same destruction-order
    // requirement as m_VertexBuffer/m_IndexBuffer.
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_InstanceBuffers;

    // Per-frame-in-flight UBO buffers (host-visible, re-Upload()ed every
    // frame in RenderFrame()) and the descriptor sets that point at them.
    // Declared after m_VulkanContext (needs its allocator) so these are
    // destroyed before it, per reverse-declaration-order destruction.
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_UniformBuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_DescriptorSets{};

    // Sky pass's own per-frame-in-flight UBOs + descriptor sets, allocated
    // from the same m_DescriptorPool (grown to fit both sets of resources —
    // see CreateDescriptorResources()) rather than a second pool.
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_SkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_SkyDescriptorSets{};

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

    // Sky/atmosphere tunables, bound directly to ImGui sliders in
    // BuildDebugOverlay() — same "public field + SliderFloat" pattern as
    // Camera's movementSpeed/mouseSensitivity above.
    float m_SunElevationDegrees = 25.0f;
    float m_SunAzimuthDegrees = 0.0f;
    float m_SkyExposure = 1.2f; // tonemap input scale (see sky.frag's ACESFilm) — fixes the sun/sky blowing out to flat white
    int m_AtmospherePrimarySteps = 16;
    int m_AtmosphereSunSteps = 8;

    // Cloud tunables (see UpdateSkyUniformBuffer()/sky.frag's cloud
    // raymarch). Layer altitude, HG lobes, powder/ambient strength, and
    // noise UV scale are fixed constants (not exposed) — only the ones worth
    // live-tweaking for the coverage/density/wind/performance tradeoff are
    // sliders.
    float m_CloudCoverage = 0.4f;
    float m_CloudDensityMultiplier = 1.1f;
    float m_CloudWindSpeed = 0.02f;
    float m_CloudWindDirectionDegrees = 0.0f;
    int m_CloudPrimarySteps = 64;
    int m_CloudSunShadowSteps = 8;

    // Distance fog for the quad scene (see UpdateUniformBuffer()/triangle.frag).
    float m_FogDensity = 0.06f;

    // Phase 20: project mode (see ApplicationSpec::projectDir). Constructed
    // only if a project directory was given; RenderFrame() branches between
    // this and the hardcoded demo above entirely based on m_ProjectMode, so
    // the demo path (including every member above) is completely unaffected
    // when this is unset.
    bool m_ProjectMode = false;
    std::unique_ptr<GameSceneRenderer> m_GameSceneRenderer;
};

} // namespace polyizon

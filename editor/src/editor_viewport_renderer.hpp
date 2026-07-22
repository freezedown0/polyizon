#pragma once

#include "polyizon/camera.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/image.hpp"
#include "polyizon/vulkan/pipeline.hpp"
#include "polyizon/vulkan/sky_pipeline.hpp"
#include "polyizon/vulkan/sky_uniform_buffer_object.hpp"
#include "polyizon/vulkan/swapchain.hpp"
#include "polyizon/vulkan/texture3d.hpp"
#include "polyizon/vulkan/uniform_buffer_object.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace polyizon {

// Sibling to Application, not a refactor of it: owns the same Vulkan object
// graph (context/swapchain/pipelines/buffers/frame-sync) and reproduces the
// relevant subset of Application's constructor body and
// RenderFrame()/Update*Buffer() methods, minus GLFW input-callback glue and
// ImGui (Qt owns input and the eventual editor UI on this path instead).
// Renders the same hardcoded demo scene as the game client (5 instanced
// quads + sky/clouds) — no editor-specific content yet, this phase only
// proves the embedding mechanism works. See the Phase 14 plan for why this
// duplication (rather than a shared RenderCore base extracted from
// Application) is the intentional choice for now.
class EditorViewportRenderer {
public:
    // hwnd/hinstance come from VulkanViewportWindow's winId()/
    // GetModuleHandle(nullptr) once its underlying native window exists
    // (QWindow::create() has run) — see VulkanContext's Win32 constructor.
    // width/height are the viewport's initial size in physical pixels.
    EditorViewportRenderer(HWND hwnd, HINSTANCE hinstance, std::uint32_t width, std::uint32_t height);
    ~EditorViewportRenderer();

    EditorViewportRenderer(const EditorViewportRenderer&) = delete;
    EditorViewportRenderer& operator=(const EditorViewportRenderer&) = delete;
    EditorViewportRenderer(EditorViewportRenderer&&) = delete;
    EditorViewportRenderer& operator=(EditorViewportRenderer&&) = delete;

    // Deferred to the next RenderFrame()'s AcquireNextImage() call, same as
    // the GLFW path's Swapchain::NotifyResized() — never recreates
    // synchronously here (see Swapchain::Resize()).
    void Resize(std::uint32_t width, std::uint32_t height);

    // Caller (VulkanViewportWindow) tracks its own held-key state from Qt key
    // events and passes it in each frame — mirrors
    // Application::ProcessCameraKeyboardInput, just with the bools resolved
    // by a different input system upstream (see Camera's GLFW-agnostic
    // ProcessKeyboard overload).
    void UpdateCamera(bool forward, bool backward, bool left, bool right, bool up, bool down, float deltaTime) {
        m_Camera.ProcessKeyboard(forward, backward, left, right, up, down, deltaTime);
    }

    // Mouse-look: caller computes raw pixel deltas from consecutive Qt mouse
    // events (own capture scheme, not GLFW's hidden/unbounded-cursor mode —
    // see VulkanViewportWindow) and drives the same Camera directly.
    Camera& GetCamera() noexcept { return m_Camera; }

    void RenderFrame();

private:
    void CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void UpdateInstanceBuffer(std::uint32_t frameIndex, float time);
    void UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time);
    float GetElapsedSeconds() const;

    static constexpr std::uint32_t kMaxFramesInFlight = 2;

    // Declaration order matches Application's exactly, for the same
    // reverse-declaration-order destruction reasoning (see application.hpp) —
    // just without a Window member, since VulkanContext's Win32 constructor
    // doesn't need one.
    std::unique_ptr<VulkanContext> m_VulkanContext;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<GraphicsPipeline> m_Pipeline;
    std::unique_ptr<SkyPipeline> m_SkyPipeline;
    std::unique_ptr<Image> m_Texture;
    std::unique_ptr<Texture3D> m_CloudNoiseTexture;
    std::unique_ptr<Buffer> m_VertexBuffer;
    std::unique_ptr<Buffer> m_IndexBuffer;
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_InstanceBuffers;

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_UniformBuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_DescriptorSets{};

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_SkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_SkyDescriptorSets{};

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_CommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> m_ImageAvailableSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> m_InFlightFences{};
    std::uint32_t m_CurrentFrame = 0;

    // No GLFWwindow/glfwGetTime() on this path — tracks its own elapsed-time
    // clock instead, used for the same cloud wind-scroll/sun-disk shader
    // params Application derives from glfwGetTime().
    std::chrono::steady_clock::time_point m_ClockStart = std::chrono::steady_clock::now();

    Camera m_Camera;

    // Same defaults as Application's tunables (application.hpp) — no Qt
    // panel exists yet to edit these (that's Phase 15+), so they're fixed at
    // construction, matching the "renders today's hardcoded demo scene, no
    // editing yet" scope of this phase.
    float m_SunElevationDegrees = 25.0f;
    float m_SunAzimuthDegrees = 0.0f;
    float m_SkyExposure = 1.2f;
    int m_AtmospherePrimarySteps = 16;
    int m_AtmosphereSunSteps = 8;
    float m_CloudCoverage = 0.4f;
    float m_CloudDensityMultiplier = 1.1f;
    float m_CloudWindSpeed = 0.02f;
    float m_CloudWindDirectionDegrees = 0.0f;
    int m_CloudPrimarySteps = 64;
    int m_CloudSunShadowSteps = 8;
    float m_FogDensity = 0.06f;
};

} // namespace polyizon

#pragma once

#include "polyizon/camera.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/scene/scene.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/lit_pipeline.hpp"
#include "polyizon/vulkan/lit_uniform_buffer_object.hpp"
#include "polyizon/vulkan/mesh.hpp"
#include "polyizon/vulkan/shadow_map.hpp"
#include "polyizon/vulkan/shadow_pipeline.hpp"
#include "polyizon/vulkan/sky_pipeline.hpp"
#include "polyizon/vulkan/sky_uniform_buffer_object.hpp"
#include "polyizon/vulkan/swapchain.hpp"
#include "polyizon/vulkan/texture3d.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace polyizon {

// Sibling to Application, not a refactor of it: owns the same Vulkan object
// graph (context/swapchain/pipelines/frame-sync) and reproduces the relevant
// subset of Application's constructor body and RenderFrame()/Update*Buffer()
// methods, minus GLFW input-callback glue and ImGui (Qt owns input and the
// eventual editor UI on this path instead). See the Phase 14 plan for why
// this duplication (rather than a shared RenderCore base extracted from
// Application) is the intentional choice.
//
// Phase 15: no longer renders the game client's hardcoded quad demo (that
// stays exclusively on the GLFW/Application path, untouched by this phase) —
// this class now renders its own EnTT Scene (a plane + a cube, see the
// constructor) through a real lit-shading + shadow-mapping pipeline instead
// of GraphicsPipeline's unlit textured quads. The sky/cloud background
// (SkyPipeline) is unchanged.
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
    // synchronously here (see Swapchain::Resize()). Only the swapchain's
    // color/depth targets are resize-dependent; ShadowMap is a fixed
    // resolution, independent of the viewport's window size.
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
    void BuildSampleScene();
    void CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateLitUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time);
    void RenderShadowPass(VkCommandBuffer cmd);
    void RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, std::uint32_t imageIndex, VkExtent2D extent);
    float GetElapsedSeconds() const;
    glm::vec3 GetSunDirection() const;
    glm::mat4 GetLightSpaceMatrix() const;

    static constexpr std::uint32_t kMaxFramesInFlight = 2;
    static constexpr std::uint32_t kShadowMapResolution = 2048;

    // Declaration order matches Application's exactly, for the same
    // reverse-declaration-order destruction reasoning (see application.hpp) —
    // just without a Window member, since VulkanContext's Win32 constructor
    // doesn't need one.
    std::unique_ptr<VulkanContext> m_VulkanContext;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<SkyPipeline> m_SkyPipeline;
    std::unique_ptr<LitPipeline> m_LitPipeline;
    std::unique_ptr<ShadowPipeline> m_ShadowPipeline;
    std::unique_ptr<Texture3D> m_CloudNoiseTexture;
    std::unique_ptr<ShadowMap> m_ShadowMap;

    // The plane+cube sample scene (see BuildSampleScene()) and the meshes
    // its MeshComponents reference. Meshes are owned here (not per-entity
    // unique_ptrs) since MeshComponent holds a shared_ptr — see
    // scene/components.hpp.
    Scene m_Scene;
    std::shared_ptr<Mesh> m_PlaneMesh;
    std::shared_ptr<Mesh> m_CubeMesh;

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_LitUniformBuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_LitDescriptorSets{};

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_SkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_SkyDescriptorSets{};

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_CommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> m_ImageAvailableSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> m_InFlightFences{};
    std::uint32_t m_CurrentFrame = 0;
    // True only before the shadow map's very first render: its initial
    // layout is UNDEFINED (see ShadowMap::CreateImage()), so the first
    // frame's pre-shadow-pass barrier must transition FROM UNDEFINED rather
    // than the SHADER_READ_ONLY_OPTIMAL every subsequent frame leaves it in.
    bool m_FirstFrame = true;

    // No GLFWwindow/glfwGetTime() on this path — tracks its own elapsed-time
    // clock instead, used for the same cloud wind-scroll/sun-disk shader
    // params Application derives from glfwGetTime().
    std::chrono::steady_clock::time_point m_ClockStart = std::chrono::steady_clock::now();

    Camera m_Camera;

    // Same defaults as Application's sky/cloud tunables (application.hpp) —
    // no Qt panel exists yet to edit these. Also drives the lit scene's
    // shadow-casting directional light (see GetSunDirection()/
    // GetLightSpaceMatrix()) so the sun position and the shadow it casts
    // stay visually consistent.
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

    // Ambient term added to the lit scene's N.L diffuse term (see lit.frag)
    // so shadowed/back faces read as dim rather than pure black.
    float m_AmbientStrength = 0.15f;
};

} // namespace polyizon

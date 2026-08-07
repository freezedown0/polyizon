#pragma once

#include "polyizon/camera.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/scene/lighting_settings.hpp"
#include "polyizon/scene/scene.hpp"
#include "polyizon/scripting/script_engine.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/classic_sky_pipeline.hpp"
#include "polyizon/vulkan/classic_sky_uniform_buffer_object.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/gizmo_pipeline.hpp"
#include "polyizon/vulkan/lit_pipeline.hpp"
#include "polyizon/vulkan/lit_uniform_buffer_object.hpp"
#include "polyizon/vulkan/mesh.hpp"
#include "polyizon/vulkan/shadow_map.hpp"
#include "polyizon/vulkan/shadow_pipeline.hpp"
#include "polyizon/vulkan/sky_pipeline.hpp"
#include "polyizon/vulkan/sky_uniform_buffer_object.hpp"
#include "polyizon/vulkan/swapchain.hpp"
#include "polyizon/vulkan/texture3d.hpp"
#include "polyizon/vulkan/voxel_lit_pipeline.hpp"

#include <entt/entt.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace polyizon {

// Phase 19: the editor's Play/Pause/Stop state (see EditorViewportRenderer::
// Play()/Pause()/Stop()). Stopped is the default "editing" state — scripts
// never run outside Playing (see RenderFrame()'s ScriptEngine::Update() gate).
enum class PlayState { Stopped, Playing, Paused };

// Phase 20: which viewport gizmo is drawn/interactive for the currently
// selected entity (see SetGizmoMode()/SetSelectedEntity()) — Move draws
// three axis arrows the user drags along; Rotate draws three axis rings the
// user drags around. Always world-axis-aligned ("global" gizmo), never the
// selected entity's own local rotation — simpler to pick/drag and matches
// most editors' default mode.
enum class GizmoMode { Move, Rotate };

// Sibling to Application, not a refactor of it: owns the same Vulkan object
// graph (context/swapchain/pipelines/frame-sync) and reproduces the relevant
// subset of Application's constructor body and RenderFrame()/Update*Buffer()
// methods, minus GLFW input-callback glue and ImGui (Qt owns input and the
// eventual editor UI on this path instead). See the Phase 14 plan for why
// this duplication (rather than a shared RenderCore base extracted from
// Application) is the intentional choice.
//
// Phase 15 gave this class its own EnTT Scene through a real lit-shading +
// shadow-mapping pipeline instead of GraphicsPipeline's unlit textured
// quads (still never rendering the game client's hardcoded quad demo, which
// stays exclusively on the GLFW/Application path, untouched by this phase
// too). Phase 16: m_Scene starts empty and is only ever populated via
// LoadScene() (called from MainWindow's File > New/Open Project handlers,
// see project.hpp/scene_serializer.hpp) — there is no hardcoded sample scene
// built directly in this class anymore.
//
// Phase 18 gave this class a SECOND full rendering path (Realistic: the
// original SkyPipeline/ShadowMap/LitPipeline described above; Voxel: a flat
// ClassicSkyPipeline sky and a deliberately coarse ShadowSamplerMode::
// PlainNearest ShadowMap sampled by VoxelLitPipeline for blocky shadows) —
// both stay resident at all times, and RenderShadowPass()/RenderMainPass()
// pick which one to draw each frame from whichever scene is currently
// loaded (m_Scene.GetLightingSettings().mode), so switching a scene's mode
// or loading a different scene with a different mode never needs to
// recreate any GPU resource. Sun direction/ambient strength are also owned
// by the scene now (SceneLightingSettings), not this class.
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
        // Cached for RenderFrame()'s ScriptEngine::Update() call — the
        // per-frame deltaTime scripts advance by should match the camera's,
        // not a second independently-derived value (see RenderFrame()).
        m_LastDeltaTime = deltaTime;
        m_Camera.ProcessKeyboard(forward, backward, left, right, up, down, deltaTime);
    }

    // Mouse-look: caller computes raw pixel deltas from consecutive Qt mouse
    // events (own capture scheme, not GLFW's hidden/unbounded-cursor mode —
    // see VulkanViewportWindow) and drives the same Camera directly.
    Camera& GetCamera() noexcept { return m_Camera; }

    void RenderFrame();

    // Called from VulkanViewportWindow (in turn from MainWindow's File >
    // New/Open Project handlers). Waits for the GPU to finish with whatever
    // scene is currently loaded (its entities' Mesh GPU buffers must not be
    // in flight) before replacing m_Scene outright — see scene_serializer.hpp.
    // Always leaves PlayState::Stopped (see Play()/Stop()) — a snapshot taken
    // against a scene that's about to be replaced wouldn't mean anything.
    void LoadScene(const std::filesystem::path& sceneFile);

    // Phase 19 Play/Pause/Stop. Stopped->Playing snapshots the current scene
    // (via SerializeSceneToJson, see scene_serializer.hpp) so Stop() can
    // revert to it; Paused->Playing (resume) does NOT re-snapshot, so Stop
    // always reverts to how the scene looked right before Play was first
    // pressed, not wherever Pause happened to catch it. Pause() only takes
    // effect from Playing. Stop() only takes effect from Playing/Paused,
    // restoring m_Scene from the snapshot (vkDeviceWaitIdle first, same
    // "GPU must be done with the old scene's Mesh buffers" precedent as
    // LoadScene()) and clearing it. See RenderFrame() for how PlayState gates
    // ScriptEngine::Update().
    void Play();
    void Pause();
    void Stop();
    PlayState GetPlayState() const noexcept { return m_PlayState; }

    // Phase 17: editor panels (Hierarchy/Inspector/ContentBrowser, see
    // editor/src/*_panel.hpp) read and mutate the live scene directly through
    // these — no separate "editor scene" copy. Callers that destroy a Mesh's
    // GPU buffers (deleting an entity, replacing a MeshComponent) must
    // vkDeviceWaitIdle(GetVulkanContext().GetDevice()) first, same as
    // LoadScene() does internally.
    Scene& GetScene() noexcept { return m_Scene; }
    VulkanContext& GetVulkanContext() noexcept { return *m_VulkanContext; }

    // Phase 20: Move/Rotate viewport gizmos. The renderer tracks the current
    // selection itself (kept in sync with HierarchyPanel/InspectorPanel's own
    // selection via VulkanViewportWindow — see its SetSelectedEntity) purely
    // so it knows what to draw the gizmo for and what PickGizmoAxis/
    // UpdateGizmoDrag should mutate; it's not itself a second source of
    // truth for "what's selected in the UI."
    void SetGizmoMode(GizmoMode mode) noexcept { m_GizmoMode = mode; }
    GizmoMode GetGizmoMode() const noexcept { return m_GizmoMode; }
    void SetSelectedEntity(entt::entity entity) noexcept { m_SelectedEntity = entity; }
    entt::entity GetSelectedEntity() const noexcept { return m_SelectedEntity; }

    // Viewport-pixel coordinates in, matching Qt's QMouseEvent::position()
    // convention (origin top-left, Y down) — same space RenderGizmo()
    // projects its handles into. Returns the axis (0=X, 1=Y, 2=Z) whose
    // handle is closest to (mouseX, mouseY) within a small pick threshold,
    // or -1 if nothing is close enough (or nothing selected).
    int PickGizmoAxis(float mouseX, float mouseY) const;
    void BeginGizmoDrag(int axis, float mouseX, float mouseY);
    void UpdateGizmoDrag(float mouseX, float mouseY);
    void EndGizmoDrag() noexcept { m_DraggingAxis = -1; }
    bool IsDraggingGizmo() const noexcept { return m_DraggingAxis >= 0; }

private:
    void CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateLitUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time);
    void UpdateClassicSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent);
    void RenderShadowPass(VkCommandBuffer cmd);
    void RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, std::uint32_t imageIndex, VkExtent2D extent);
    // Shared by both RenderMainPass branches (Realistic/Voxel): identical
    // per-entity draw loop, only the bound pipeline/descriptor set differs
    // (which the caller has already bound before calling this).
    void DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout);
    // Rebuilds this frame's gizmo line geometry for the current selection/
    // mode (CPU-side, in world space) and draws it — a no-op if nothing
    // valid is selected. Drawn last, on top of the already-rendered scene
    // (see GizmoPipeline: depth test/write both disabled).
    void RenderGizmo(VkCommandBuffer cmd, VkExtent2D extent);
    float GetElapsedSeconds() const;
    glm::vec3 GetSunDirection() const;
    glm::mat4 GetLightSpaceMatrix() const;
    // Shared by RenderGizmo/PickGizmoAxis/UpdateGizmoDrag so all three agree
    // on exactly the same camera transform each frame.
    glm::mat4 GetViewProjMatrix(VkExtent2D extent) const;

    static constexpr std::uint32_t kMaxFramesInFlight = 2;
    static constexpr std::uint32_t kRealisticShadowMapResolution = 2048;
    // Deliberately tiny — see ShadowSamplerMode::PlainNearest and
    // voxel_lit.frag's kVoxelDepthSlices for how this becomes a genuinely
    // blocky "4x4x4" shadow rather than a low-res-but-smooth one.
    static constexpr std::uint32_t kVoxelShadowMapResolution = 4;

    // Declaration order matches Application's exactly, for the same
    // reverse-declaration-order destruction reasoning (see application.hpp) —
    // just without a Window member, since VulkanContext's Win32 constructor
    // doesn't need one.
    std::unique_ptr<VulkanContext> m_VulkanContext;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<SkyPipeline> m_SkyPipeline;
    std::unique_ptr<ClassicSkyPipeline> m_ClassicSkyPipeline;
    std::unique_ptr<LitPipeline> m_LitPipeline;
    std::unique_ptr<VoxelLitPipeline> m_VoxelLitPipeline;
    // ShadowPipeline is shared: it's a depth-only raster pass independent of
    // which ShadowMap it renders into (see ShadowMap's ShadowSamplerMode) —
    // no Voxel-specific variant needed.
    std::unique_ptr<ShadowPipeline> m_ShadowPipeline;
    std::unique_ptr<Texture3D> m_CloudNoiseTexture;
    std::unique_ptr<ShadowMap> m_RealisticShadowMap;
    std::unique_ptr<ShadowMap> m_VoxelShadowMap;

    // Starts empty; populated only via LoadScene() (see its doc comment
    // above). Each entity's MeshComponent owns its own Mesh shared_ptr (see
    // scene/components.hpp) — no plane/cube-specific members here anymore.
    Scene m_Scene;

    // Lua scripting (Phase 16) — ScriptEngine owns the shared Lua state and
    // per-script environments; touches no Vulkan/GPU state, so its
    // declaration position here has no destruction-order implications.
    ScriptEngine m_ScriptEngine;

    // Phase 19 Play/Pause/Stop (see Play()/Pause()/Stop() above). Holds the
    // pre-Play scene snapshot (SerializeSceneToJson's output) only while
    // Playing/Paused — null otherwise. nlohmann::json rather than a second
    // Scene: Scene/entt::registry has no deep-copy support, and the JSON
    // round-trip already exists for SceneSerializer, so reusing it needs no
    // new machinery. unique_ptr (not optional/by-value) so this header only
    // needs nlohmann::json forward-declared (json_fwd.hpp above) rather than
    // its full definition — this class's copy/move are already deleted, so
    // no special member function here needs the complete type; only
    // ~EditorViewportRenderer() does, and it's defined out-of-line in the
    // .cpp, which does see the full type via scene_serializer.hpp.
    PlayState m_PlayState = PlayState::Stopped;
    std::unique_ptr<nlohmann::json> m_PlaySnapshot;

    // LitUniformBufferObject's contents (view/proj/lightSpaceMatrix/
    // sunDirectionAndAmbient) don't differ between Realistic and Voxel — only
    // which ShadowMap is sampled back does — so both modes' lit descriptor
    // sets bind the SAME buffer at binding 0; only binding 1 (the shadow
    // sampler) differs between m_LitDescriptorSets and
    // m_VoxelLitDescriptorSets.
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_LitUniformBuffers;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_LitDescriptorSets{};
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_VoxelLitDescriptorSets{};

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_SkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_SkyDescriptorSets{};

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_ClassicSkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_ClassicSkyDescriptorSets{};

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_CommandBuffers{};
    std::array<VkSemaphore, kMaxFramesInFlight> m_ImageAvailableSemaphores{};
    std::array<VkFence, kMaxFramesInFlight> m_InFlightFences{};
    std::uint32_t m_CurrentFrame = 0;
    // True only before EACH shadow map's own very first render: its initial
    // layout is UNDEFINED (see ShadowMap::CreateImage()), so that map's first
    // frame needs its pre-shadow-pass barrier to transition FROM UNDEFINED
    // rather than the SHADER_READ_ONLY_OPTIMAL every subsequent render of it
    // leaves it in. Tracked separately per map (not one shared flag) because
    // a scene can switch modes — or a newly loaded scene can use the mode
    // that hasn't rendered yet this session — well after the other mode's
    // map has already had its first render.
    bool m_RealisticShadowMapFirstFrame = true;
    bool m_VoxelShadowMapFirstFrame = true;

    // No GLFWwindow/glfwGetTime() on this path — tracks its own elapsed-time
    // clock instead, used for the same cloud wind-scroll/sun-disk shader
    // params Application derives from glfwGetTime().
    std::chrono::steady_clock::time_point m_ClockStart = std::chrono::steady_clock::now();

    // Cached by UpdateCamera() each frame, read by RenderFrame() when
    // calling ScriptEngine::Update() — see UpdateCamera()'s doc comment.
    float m_LastDeltaTime = 0.0f;

    Camera m_Camera;

    // Same defaults as Application's sky/cloud tunables (application.hpp) —
    // no Qt panel exists yet to edit these. Realistic-mode-only (Voxel's
    // ClassicSkyPipeline has no atmosphere/cloud raymarch at all). Sun
    // elevation/azimuth and the lit scene's ambient strength moved to
    // SceneLightingSettings in Phase 18 (see Scene::GetLightingSettings()) —
    // they're scene-owned data now, not fields on this class, since
    // different scenes can want a different sun position/lighting mode.
    float m_SkyExposure = 1.2f;
    int m_AtmospherePrimarySteps = 16;
    int m_AtmosphereSunSteps = 8;
    float m_CloudCoverage = 0.32f;
    float m_CloudDensityMultiplier = 0.9f;
    float m_CloudWindSpeed = 0.002f;
    float m_CloudWindDirectionDegrees = 0.0f;
    int m_CloudPrimarySteps = 96;
    int m_CloudSunShadowSteps = 12;

    // Phase 20: Move/Rotate viewport gizmos. Geometry is rebuilt CPU-side
    // every frame (world-space line list, cheap — see RenderGizmo()) into a
    // per-frame-in-flight host-visible buffer, rather than a static template
    // + per-instance model matrix, so the exact same vertex positions used
    // for rendering are also what PickGizmoAxis/UpdateGizmoDrag project to
    // screen space — no risk of the two subtly disagreeing.
    std::unique_ptr<GizmoPipeline> m_GizmoPipeline;
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_GizmoVertexBuffers;
    GizmoMode m_GizmoMode = GizmoMode::Move;
    entt::entity m_SelectedEntity = entt::null;
    // >=0 while a mouse drag on that axis (0=X/1=Y/2=Z) is in progress; see
    // BeginGizmoDrag()/UpdateGizmoDrag()/EndGizmoDrag().
    int m_DraggingAxis = -1;
    float m_LastDragMouseX = 0.0f;
    float m_LastDragMouseY = 0.0f;
};

} // namespace polyizon

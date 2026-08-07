#pragma once

#include "polyizon/scene/scene.hpp"
#include "polyizon/scripting/script_engine.hpp"
#include "polyizon/vulkan/buffer.hpp"
#include "polyizon/vulkan/lit_pipeline.hpp"
#include "polyizon/vulkan/image_state_tracker.hpp"
#include "polyizon/vulkan/shadow_map.hpp"
#include "polyizon/vulkan/shadow_pipeline.hpp"
#include "polyizon/vulkan/sky_pipeline.hpp"
#include "polyizon/vulkan/texture3d.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace polyizon {

class VulkanContext;

// Renders a loaded project's Scene with the realistic sky, PBR lighting,
// shadows, and authored lights, and drives its Lua scripts every frame — the rendering
// half of a "compiled game" (see Application's project mode / the editor's
// Build Game export, Phase 20). A sibling to EditorViewportRenderer (same
// Sky/Lit/Shadow pipeline set, same per-frame UBO
// population, same shadow-pass/main-pass structure) rather than a shared
// base class with it — see the Phase 14 plan's "duplication over a shared
// RenderCore" precedent. Unlike EditorViewportRenderer, this class does NOT
// own a VulkanContext/Swapchain itself: the caller (Application) already has
// one (GLFW-driven), so this takes a reference to it and is handed the
// caller's current frame's swapchain image/views + view/projection matrices
// each frame instead.
//
// No Play/Pause/Stop state machine (see EditorViewportRenderer::PlayState) —
// a compiled game has no "editing" state to protect scripts from; Update()
// always runs every entity's script, every frame.
class GameSceneRenderer {
public:
    GameSceneRenderer(VulkanContext& context, VkFormat colorFormat, VkFormat depthFormat);
    ~GameSceneRenderer();

    GameSceneRenderer(const GameSceneRenderer&) = delete;
    GameSceneRenderer& operator=(const GameSceneRenderer&) = delete;
    GameSceneRenderer(GameSceneRenderer&&) = delete;
    GameSceneRenderer& operator=(GameSceneRenderer&&) = delete;

    // Reads <projectRootDir>/project.json for its default scene and loads it
    // (see polyizon/scene/scene_serializer.hpp). Throws on failure (missing
    // manifest, missing/invalid scene file, missing mesh/script assets) —
    // the caller (Application) has no fallback path if this fails; a
    // compiled game with a broken project simply can't start.
    void LoadProject(const std::filesystem::path& projectRootDir);

    // Runs every entity's Lua script once (see ScriptEngine) — always,
    // unlike EditorViewportRenderer's PlayState-gated equivalent (no
    // Play/Pause/Stop distinction for a compiled game).
    void Update(float deltaTime);

    // Records the shadow pass + main pass (sky then lit entities) into
    // `cmd`, which the caller has already vkBeginCommandBuffer'd and will
    // vkEndCommandBuffer/submit itself. colorImage/colorImageView are the
    // swapchain's current frame's color target (not owned here); depthImage/
    // depthImageView likewise for its depth target. frameIndex selects which
    // of this class's own per-frame-in-flight resources to use — pass the
    // same frame index the caller uses for its own frame-in-flight
    // resources (must be < 2, see kMaxFramesInFlight).
    void RecordFrame(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorImageView,
        VkImage depthImage, VkImageView depthImageView, VkExtent2D extent, std::uint32_t frameIndex,
        const glm::mat4& view, const glm::mat4& proj, float elapsedSeconds);

private:
    void CreateDescriptorResources();
    void DestroyDescriptorResources();
    void UpdateLitUniformBuffer(std::uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj);
    void UpdateSkyUniformBuffer(
        std::uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, float time);
    void RenderShadowPass(VkCommandBuffer cmd);
    void RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorImageView,
        VkImage depthImage, VkImageView depthImageView, VkExtent2D extent);
    void DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout);
    glm::vec3 GetSunDirection() const;
    glm::vec3 GetDirectionalLightDirection() const;
    glm::vec4 GetDirectionalLightColorAndIntensity() const;
    bool GetDirectionalLightCastsShadows() const;
    glm::mat4 GetLightSpaceMatrix() const;

    // Matches Application's/EditorViewportRenderer's own constant of the
    // same name/value — this class's per-frame-in-flight arrays are sized to
    // it, and RecordFrame()'s frameIndex parameter must stay under it.
    static constexpr std::uint32_t kMaxFramesInFlight = 2;
    static constexpr std::uint32_t kShadowMapResolution = 2048;

    VulkanContext& m_Context;

    std::unique_ptr<SkyPipeline> m_SkyPipeline;
    std::unique_ptr<LitPipeline> m_LitPipeline;
    std::unique_ptr<ShadowPipeline> m_ShadowPipeline;
    std::unique_ptr<Texture3D> m_CloudNoiseTexture;
    std::unique_ptr<ShadowMap> m_ShadowMap;

    Scene m_Scene;
    ScriptEngine m_ScriptEngine;

    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_LitUniformBuffers;
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_LocalLightBuffers;
    int m_LastDroppedLightCount = 0;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_LitDescriptorSets{};
    std::array<std::unique_ptr<Buffer>, kMaxFramesInFlight> m_SkyUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_SkyDescriptorSets{};

    ImageStateTracker m_ImageStates;

    // Set by RecordFrame() at the top of each call, read by RenderMainPass()/
    // DrawLitEntities() to pick which frame-in-flight's descriptor sets to
    // bind — avoids threading frameIndex through every private helper's
    // parameter list individually.
    std::uint32_t m_CurrentFrameIndex = 0;
};

} // namespace polyizon

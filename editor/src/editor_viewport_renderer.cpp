#include "editor_viewport_renderer.hpp"
#include "polyizon/rendering/render_graph.hpp"
#include "polyizon/rendering/forward_plus_lights.hpp"

#include "polyizon/log.hpp"
#include "polyizon/scene/scene_serializer.hpp"
#include "polyizon/noise.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Full nlohmann::json definition — editor_viewport_renderer.hpp only forward-
// declares it (via json_fwd.hpp) for m_PlaySnapshot's unique_ptr<json>, but
// Play()/Stop() below construct/dereference one directly, which needs the
// complete type.
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace polyizon {

namespace {

// Phase 20 gizmo geometry — see RenderGizmo()/PickGizmoAxis()/
// UpdateGizmoDrag(). World-axis-aligned (never the entity's own rotation).
constexpr std::array<glm::vec3, 3> kGizmoAxisDirections = {
    glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
};
constexpr std::array<glm::vec4, 3> kGizmoAxisColors = {
    glm::vec4(0.9f, 0.15f, 0.15f, 1.0f), glm::vec4(0.15f, 0.9f, 0.15f, 1.0f), glm::vec4(0.2f, 0.4f, 0.95f, 1.0f)
};
constexpr glm::vec4 kGizmoHighlightColor(1.0f, 0.9f, 0.15f, 1.0f); // the axis currently being dragged/hovered
constexpr float kGizmoArrowLength = 1.0f;
constexpr float kGizmoArrowHeadFraction = 0.85f; // where the head cross starts, along the shaft
constexpr float kGizmoArrowHeadSize = 0.08f;
constexpr float kGizmoRingRadius = 1.25f;
constexpr int kGizmoRingSegments = 24;
constexpr float kGizmoPickThresholdPixels = 10.0f;
// Move: 3 axes * (1 shaft + 4 head lines) * 2 verts = 30. Rotate: 3 rings *
// 24 segments * 2 verts = 144. Rounded well up for headroom.
constexpr std::size_t kGizmoMaxVertices = 256;

bool IsEntityEnabled(const entt::registry& registry, entt::entity entity) {
    const auto* metadata = registry.try_get<const EntityMetadataComponent>(entity);
    return metadata == nullptr || metadata->enabled;
}

void AppendGizmoArrow(std::vector<Vertex3D>& out, const glm::vec3& origin, int axis) {
    const glm::vec3 dir = kGizmoAxisDirections[axis];
    const glm::vec3 tip = origin + dir * kGizmoArrowLength;
    const glm::vec3 headBase = origin + dir * (kGizmoArrowLength * kGizmoArrowHeadFraction);

    glm::vec3 perpA, perpB;
    switch (axis) {
        case 0: perpA = glm::vec3(0.0f, 1.0f, 0.0f); perpB = glm::vec3(0.0f, 0.0f, 1.0f); break;
        case 1: perpA = glm::vec3(1.0f, 0.0f, 0.0f); perpB = glm::vec3(0.0f, 0.0f, 1.0f); break;
        default: perpA = glm::vec3(1.0f, 0.0f, 0.0f); perpB = glm::vec3(0.0f, 1.0f, 0.0f); break;
    }

    const glm::vec3 zero(0.0f);
    out.push_back({ origin, zero });
    out.push_back({ tip, zero });
    out.push_back({ tip, zero });
    out.push_back({ headBase + perpA * kGizmoArrowHeadSize, zero });
    out.push_back({ tip, zero });
    out.push_back({ headBase - perpA * kGizmoArrowHeadSize, zero });
    out.push_back({ tip, zero });
    out.push_back({ headBase + perpB * kGizmoArrowHeadSize, zero });
    out.push_back({ tip, zero });
    out.push_back({ headBase - perpB * kGizmoArrowHeadSize, zero });
}

// Ring for axis N lies in the plane perpendicular to N (standard rotation-
// gizmo convention: rotating around X moves points within the YZ plane).
void AppendGizmoRing(std::vector<Vertex3D>& out, const glm::vec3& origin, int axis) {
    const glm::vec3 zero(0.0f);
    for (int i = 0; i < kGizmoRingSegments; ++i) {
        const float a0 = (2.0f * glm::pi<float>() * static_cast<float>(i)) / static_cast<float>(kGizmoRingSegments);
        const float a1 = (2.0f * glm::pi<float>() * static_cast<float>(i + 1)) / static_cast<float>(kGizmoRingSegments);
        const float c0 = std::cos(a0) * kGizmoRingRadius, s0 = std::sin(a0) * kGizmoRingRadius;
        const float c1 = std::cos(a1) * kGizmoRingRadius, s1 = std::sin(a1) * kGizmoRingRadius;

        glm::vec3 p0, p1;
        switch (axis) {
            case 0: p0 = origin + glm::vec3(0.0f, c0, s0); p1 = origin + glm::vec3(0.0f, c1, s1); break;
            case 1: p0 = origin + glm::vec3(c0, 0.0f, s0); p1 = origin + glm::vec3(c1, 0.0f, s1); break;
            default: p0 = origin + glm::vec3(c0, s0, 0.0f); p1 = origin + glm::vec3(c1, s1, 0.0f); break;
        }
        out.push_back({ p0, zero });
        out.push_back({ p1, zero });
    }
}

// Returns std::nullopt if worldPos projects behind the camera (w <= 0) —
// callers (PickGizmoAxis/UpdateGizmoDrag) just skip that sample rather than
// dividing by a near-zero/negative w.
std::optional<glm::vec2> ProjectToScreen(const glm::mat4& viewProj, const glm::vec3& worldPos, VkExtent2D extent) {
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return std::nullopt;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2(
        (ndc.x * 0.5f + 0.5f) * static_cast<float>(extent.width),
        (ndc.y * 0.5f + 0.5f) * static_cast<float>(extent.height));
}

} // namespace

EditorViewportRenderer::EditorViewportRenderer(HWND hwnd, HINSTANCE hinstance, std::uint32_t width, std::uint32_t height) {
    m_VulkanContext = std::make_unique<VulkanContext>(hwnd, hinstance, "Polyizon Editor");
    m_Swapchain = std::make_unique<Swapchain>(*m_VulkanContext, width, height);
    m_SkyPipeline = std::make_unique<SkyPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_LitPipeline = std::make_unique<LitPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_ShadowPipeline = std::make_unique<ShadowPipeline>(m_VulkanContext->GetDevice(), m_Swapchain->GetDepthFormat());
    m_GizmoPipeline = std::make_unique<GizmoPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());

    // Host-visible, rebuilt+re-uploaded every RenderGizmo() call (see its doc
    // comment) — same per-frame-in-flight pattern as the UBO buffers below,
    // just holding vertex data instead.
    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_GizmoVertexBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(Vertex3D) * kGizmoMaxVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }

    const CloudNoiseParams cloudNoiseParams{}; // same defaults as Application; not yet developer-editable here either
    const std::vector<std::uint8_t> cloudNoiseData = LoadOrGenerateCloudNoiseVolume(cloudNoiseParams);
    m_CloudNoiseTexture = std::make_unique<Texture3D>(*m_VulkanContext, cloudNoiseData.data(),
        cloudNoiseParams.resolution, cloudNoiseParams.resolution, cloudNoiseParams.resolution);

    // Reuses the swapchain's already-queried supported depth format (see
    // ShadowMap's constructor comment) rather than re-deriving it. Both
    // modes' maps are created up front and stay resident for the whole
    // session (see the class doc comment) — a scene can switch modes, or a
    // newly loaded scene can use whichever mode wasn't active yet, at any
    // time without waiting on GPU resource creation.
    m_ShadowMap = std::make_unique<ShadowMap>(
        *m_VulkanContext, m_Swapchain->GetDepthFormat(), kShadowMapResolution);

    // m_Scene starts empty (default-constructed) — populated only via a
    // later LoadScene() call, once MainWindow's File > New/Open Project
    // handler has a project/scene file to point at. Until then the viewport
    // just shows the sky/cloud background with nothing in it.

    CreateDescriptorResources();
    CreateFrameSyncObjects();
}

EditorViewportRenderer::~EditorViewportRenderer() {
    // Same "let the GPU finish before tearing down" ordering as
    // Application's destructor.
    if (m_VulkanContext) {
        vkDeviceWaitIdle(m_VulkanContext->GetDevice());
    }
    DestroyFrameSyncObjects();
    DestroyDescriptorResources();
}

void EditorViewportRenderer::LoadScene(const std::filesystem::path& sceneFile) {
    // The GPU may still be reading the old scene's Mesh vertex/index buffers
    // (up to kMaxFramesInFlight frames behind) — same "wait before touching
    // GPU resources referenced by in-flight work" reasoning as the
    // destructor above.
    vkDeviceWaitIdle(m_VulkanContext->GetDevice());
    m_Scene = polyizon::LoadScene(*m_VulkanContext, sceneFile);
    // A different scene entirely just replaced m_Scene — any in-progress
    // Play snapshot belonged to the old one and no longer means anything.
    m_PlayState = PlayState::Stopped;
    m_PlaySnapshot.reset();
}

void EditorViewportRenderer::Play() {
    if (m_PlayState == PlayState::Stopped) {
        m_PlaySnapshot = std::make_unique<nlohmann::json>(SerializeSceneToJson(m_Scene));
    }
    m_PlayState = PlayState::Playing;
}

void EditorViewportRenderer::Pause() {
    if (m_PlayState == PlayState::Playing) {
        m_PlayState = PlayState::Paused;
    }
}

void EditorViewportRenderer::Stop() {
    if (m_PlayState == PlayState::Stopped || !m_PlaySnapshot) {
        return;
    }
    // Same "GPU must be done with the scene's Mesh buffers before it's
    // replaced" precedent as LoadScene() above.
    vkDeviceWaitIdle(m_VulkanContext->GetDevice());
    m_Scene = DeserializeSceneFromJson(*m_VulkanContext, *m_PlaySnapshot);
    m_PlaySnapshot.reset();
    m_PlayState = PlayState::Stopped;
}

void EditorViewportRenderer::Resize(std::uint32_t width, std::uint32_t height) {
    m_Swapchain->Resize(width, height);
}

float EditorViewportRenderer::GetElapsedSeconds() const {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_ClockStart).count();
}

glm::vec3 EditorViewportRenderer::GetSunDirection() const {
    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    const float elevation = glm::radians(lighting.sunElevationDegrees);
    const float azimuth = glm::radians(lighting.sunAzimuthDegrees);
    return glm::normalize(glm::vec3(
        std::cos(elevation) * std::cos(azimuth),
        std::sin(elevation),
        std::cos(elevation) * std::sin(azimuth)));
}

glm::vec3 EditorViewportRenderer::GetDirectionalLightDirection() const {
    const entt::registry& registry = m_Scene.GetRegistry();
    const auto view = registry.view<const TransformComponent, const DirectionalLightComponent>();
    for (const entt::entity entity : view) {
        const auto& light = view.get<const DirectionalLightComponent>(entity);
        if (IsEntityEnabled(registry, entity) && light.enabled && light.mobility != LightMobility::Baked) {
            return glm::normalize(-view.get<const TransformComponent>(entity).GetForward());
        }
    }
    return GetSunDirection();
}

glm::vec4 EditorViewportRenderer::GetDirectionalLightColorAndIntensity() const {
    const entt::registry& registry = m_Scene.GetRegistry();
    const auto view = registry.view<const DirectionalLightComponent>();
    for (const entt::entity entity : view) {
        const auto& light = view.get<const DirectionalLightComponent>(entity);
        if (IsEntityEnabled(registry, entity) && light.enabled && light.mobility != LightMobility::Baked) {
            return glm::vec4(light.color, light.intensity);
        }
    }
    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    return glm::vec4(lighting.directionalLightColor, lighting.directionalLightIntensity);
}

bool EditorViewportRenderer::GetDirectionalLightCastsShadows() const {
    const entt::registry& registry = m_Scene.GetRegistry();
    const auto view = registry.view<const DirectionalLightComponent>();
    for (const entt::entity entity : view) {
        const auto& light = view.get<const DirectionalLightComponent>(entity);
        if (IsEntityEnabled(registry, entity) && light.enabled && light.mobility != LightMobility::Baked) {
            return light.castsShadows;
        }
    }
    return true;
}

glm::mat4 EditorViewportRenderer::GetLightSpaceMatrix() const {
    const glm::vec3 sunDir = GetDirectionalLightDirection();
    // Roughly the vertical midpoint between the plane (y=0) and the cube's
    // top (y=1) — comfortably centers both in the light's frustum.
    const glm::vec3 sceneCenter(0.0f, 0.5f, 0.0f);
    constexpr float kLightDistance = 15.0f;
    constexpr float kOrthoHalfExtent = 8.0f; // plane spans -5..5; this gives margin
    constexpr float kNear = 0.1f;
    constexpr float kFar = kLightDistance * 2.0f;

    // Guards the near-vertical-sun degenerate case (view direction parallel
    // to the default up vector, which would make lookAt's cross product
    // collapse) — not reachable with this phase's fixed 25-degree elevation
    // default, but cheap to guard now rather than leave a latent NaN trap for
    // whenever a future phase makes this developer-editable.
    const glm::vec3 up = (std::abs(sunDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 lightView = glm::lookAt(sceneCenter + sunDir * kLightDistance, sceneCenter, up);
    glm::mat4 lightProj = glm::ortho(-kOrthoHalfExtent, kOrthoHalfExtent, -kOrthoHalfExtent, kOrthoHalfExtent, kNear, kFar);
    // Same Y-flip convention as every camera projection in this codebase
    // (Vulkan NDC is Y-down; glm::perspective/ortho assume Y-up) — keeps this
    // matrix's NDC-to-image-space handedness consistent with how
    // vkCmdSetViewport/the rasterizer actually place data into ShadowMap, so
    // lit.frag's plain `ndc.xy * 0.5 + 0.5` UV reconstruction is correct.
    lightProj[1][1] *= -1.0f;

    return lightProj * lightView;
}

void EditorViewportRenderer::CreateFrameSyncObjects() {
    VkDevice device = m_VulkanContext->GetDevice();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_VulkanContext->GetGraphicsQueueFamily();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFramesInFlight;

    if (vkAllocateCommandBuffers(device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffers");
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan frame sync objects");
        }
    }
}

void EditorViewportRenderer::DestroyFrameSyncObjects() {
    if (!m_VulkanContext) {
        return;
    }
    VkDevice device = m_VulkanContext->GetDevice();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_InFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, m_InFlightFences[i], nullptr);
            m_InFlightFences[i] = VK_NULL_HANDLE;
        }
        if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
            m_ImageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
    }

    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_CommandPool, nullptr);
        m_CommandPool = VK_NULL_HANDLE;
    }
}

void EditorViewportRenderer::CreateDescriptorResources() {
    VkDevice device = m_VulkanContext->GetDevice();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_LitUniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(LitUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_LocalLightBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(ForwardPlusLightBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // Two sets per frame-in-flight: lit (shadow map + UBO) and sky
    // (cloud noise volume + UBO).
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight * 2;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxFramesInFlight * 2;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor pool");
    }

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> litLayouts{};
    litLayouts.fill(m_LitPipeline->GetDescriptorSetLayout());

    VkDescriptorSetAllocateInfo litAllocInfo{};
    litAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    litAllocInfo.descriptorPool = m_DescriptorPool;
    litAllocInfo.descriptorSetCount = kMaxFramesInFlight;
    litAllocInfo.pSetLayouts = litLayouts.data();

    if (vkAllocateDescriptorSets(device, &litAllocInfo, m_LitDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan lit descriptor sets");
    }

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> skyLayouts{};
    skyLayouts.fill(m_SkyPipeline->GetDescriptorSetLayout());

    VkDescriptorSetAllocateInfo skyAllocInfo{};
    skyAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skyAllocInfo.descriptorPool = m_DescriptorPool;
    skyAllocInfo.descriptorSetCount = kMaxFramesInFlight;
    skyAllocInfo.pSetLayouts = skyLayouts.data();

    if (vkAllocateDescriptorSets(device, &skyAllocInfo, m_SkyDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan sky descriptor sets");
    }

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo litBufferInfo{};
        litBufferInfo.buffer = m_LitUniformBuffers[i]->GetBuffer();
        litBufferInfo.offset = 0;
        litBufferInfo.range = sizeof(LitUniformBufferObject);

        VkDescriptorImageInfo shadowMapInfo{};
        shadowMapInfo.sampler = m_ShadowMap->GetSampler();
        shadowMapInfo.imageView = m_ShadowMap->GetImageView();
        shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo localLightBufferInfo{};
        localLightBufferInfo.buffer = m_LocalLightBuffers[i]->GetBuffer();
        localLightBufferInfo.offset = 0;
        localLightBufferInfo.range = sizeof(ForwardPlusLightBuffer);

        std::array<VkWriteDescriptorSet, 3> litWrites{};
        litWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        litWrites[0].dstSet = m_LitDescriptorSets[i];
        litWrites[0].dstBinding = 0;
        litWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        litWrites[0].descriptorCount = 1;
        litWrites[0].pBufferInfo = &litBufferInfo;

        litWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        litWrites[1].dstSet = m_LitDescriptorSets[i];
        litWrites[1].dstBinding = 1;
        litWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        litWrites[1].descriptorCount = 1;
        litWrites[1].pImageInfo = &shadowMapInfo;

        litWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        litWrites[2].dstSet = m_LitDescriptorSets[i];
        litWrites[2].dstBinding = 2;
        litWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        litWrites[2].descriptorCount = 1;
        litWrites[2].pBufferInfo = &localLightBufferInfo;

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(litWrites.size()), litWrites.data(), 0, nullptr);

        VkDescriptorBufferInfo skyBufferInfo{};
        skyBufferInfo.buffer = m_SkyUniformBuffers[i]->GetBuffer();
        skyBufferInfo.offset = 0;
        skyBufferInfo.range = sizeof(SkyUniformBufferObject);

        VkDescriptorImageInfo cloudNoiseInfo{};
        cloudNoiseInfo.sampler = m_CloudNoiseTexture->GetSampler();
        cloudNoiseInfo.imageView = m_CloudNoiseTexture->GetImageView();
        cloudNoiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> skyWrites{};
        skyWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        skyWrites[0].dstSet = m_SkyDescriptorSets[i];
        skyWrites[0].dstBinding = 0;
        skyWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyWrites[0].descriptorCount = 1;
        skyWrites[0].pBufferInfo = &skyBufferInfo;

        skyWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        skyWrites[1].dstSet = m_SkyDescriptorSets[i];
        skyWrites[1].dstBinding = 1;
        skyWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        skyWrites[1].descriptorCount = 1;
        skyWrites[1].pImageInfo = &cloudNoiseInfo;

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(skyWrites.size()), skyWrites.data(), 0, nullptr);

    }
}

void EditorViewportRenderer::DestroyDescriptorResources() {
    if (!m_VulkanContext) {
        return;
    }
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_VulkanContext->GetDevice(), m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void EditorViewportRenderer::UpdateLitUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent) {
    LitUniformBufferObject ubo{};
    ubo.view = m_Camera.GetViewMatrix();

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
    ubo.proj[1][1] *= -1.0f;

    ubo.lightSpaceMatrix = GetLightSpaceMatrix();
    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    ubo.sunDirectionAndAmbient = glm::vec4(GetDirectionalLightDirection(), lighting.ambientStrength);
    ubo.directionalLightColorAndIntensity = GetDirectionalLightColorAndIntensity();

    // Phase 19: point/spot lights, capped at kMaxPointLights/kMaxSpotLights
    // (see lit_uniform_buffer_object.hpp) — any beyond the cap are silently
    // skipped rather than erroring, same precedent as other fixed-size-array
    // limits in this engine.
    ubo.renderFlags.x = GetDirectionalLightCastsShadows() ? 1 : 0;
    const ForwardPlusLightBuffer localLights = BuildForwardPlusLightBuffer(m_Scene);
    if (localLights.counts.w != m_LastDroppedLightCount) {
        m_LastDroppedLightCount = localLights.counts.w;
        if (m_LastDroppedLightCount > 0) {
            Log::Warning("Forward+ light buffer full: " + std::to_string(m_LastDroppedLightCount) +
                " local lights were not submitted");
        }
    }
    m_LitUniformBuffers[frameIndex]->Upload(&ubo, sizeof(ubo));
    m_LocalLightBuffers[frameIndex]->Upload(&localLights, sizeof(localLights));
}

void EditorViewportRenderer::UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time) {
    SkyUniformBufferObject sky{};
    sky.invView = glm::inverse(m_Camera.GetViewMatrix());

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
    proj[1][1] *= -1.0f;
    sky.invProj = glm::inverse(proj);

    sky.sunDirection = glm::vec4(GetSunDirection(), 0.0f);

    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    sky.timeAndSun = glm::vec4(time, glm::radians(1.5f), 0.0f, 0.0f);
    sky.atmosphereParams0 = glm::vec4(6360.0f, 60.0f, 0.5f, 8.0f);
    sky.atmosphereParams1 = glm::vec4(1.2f, 0.76f, lighting.skySunIntensity, lighting.skyExposure);

    sky.cloudParams0 = glm::vec4(lighting.cloudLayerBottomKm, lighting.cloudLayerTopKm,
        lighting.cloudCoverage, lighting.cloudDensity);
    sky.cloudParams1 = glm::vec4(lighting.cloudWindSpeed,
        glm::radians(lighting.cloudWindDirectionDegrees), 0.8f, -0.2f);
    sky.cloudParams2 = glm::vec4(lighting.cloudPowderStrength, lighting.cloudAmbientStrength,
        lighting.cloudNoiseScale, lighting.cloudsEnabled ? 1.0f : 0.0f);

    sky.stepCounts = glm::ivec4(16, 8, lighting.cloudPrimarySteps, lighting.cloudShadowSteps);

    m_SkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void EditorViewportRenderer::RenderShadowPass(VkCommandBuffer cmd) {
    ShadowMap& activeShadowMap = *m_ShadowMap;
    m_ImageStates.Transition(cmd, activeShadowMap.GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
        { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT });

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = activeShadowMap.GetImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // read back by the lit pass this same frame
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    const std::uint32_t resolution = activeShadowMap.GetResolution();

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, { resolution, resolution } };
    renderingInfo.layerCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(resolution);
    viewport.height = static_cast<float>(resolution);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, { resolution, resolution } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline->GetPipeline());

    const glm::mat4 lightSpaceMatrix = GetLightSpaceMatrix();
    auto view = m_Scene.GetRegistry().view<TransformComponent, MeshComponent>();
    for (auto entity : view) {
        if (!IsEntityEnabled(m_Scene.GetRegistry(), entity)) {
            continue;
        }
        const auto& [transform, meshComponent] = view.get<TransformComponent, MeshComponent>(entity);
        const glm::mat4 lightSpaceMVP = lightSpaceMatrix * transform.GetMatrix();
        vkCmdPushConstants(cmd, m_ShadowPipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &lightSpaceMVP);

        const VkBuffer vertexBuffer = meshComponent.mesh->GetVertexBuffer();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, meshComponent.mesh->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, meshComponent.mesh->GetIndexCount(), 1, 0, 0, 0);
    }

    vkCmdEndRendering(cmd);

    m_ImageStates.Transition(cmd, activeShadowMap.GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
        { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT });
}

void EditorViewportRenderer::RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, std::uint32_t imageIndex, VkExtent2D extent) {
    m_ImageStates.Transition(cmd, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
        { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT }, true);
    m_ImageStates.Transition(cmd, m_Swapchain->GetDepthImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
        { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT }, true);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_Swapchain->GetImageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_Swapchain->GetDepthImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Sky pass first (background), then the lit scene — which pipeline pair
    // depends on the loaded scene's lighting mode (see the class doc
    // comment). Both pipeline sets stay resident regardless of which is
    // drawn this frame.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetLayout(), 0, 1,
            &m_SkyDescriptorSets[m_CurrentFrame], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetLayout(), 0, 1,
            &m_LitDescriptorSets[m_CurrentFrame], 0, nullptr);
        DrawLitEntities(cmd, m_LitPipeline->GetLayout());

    RenderGizmo(cmd, extent);

    vkCmdEndRendering(cmd);

    m_ImageStates.Transition(cmd, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
        { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE });
}

void EditorViewportRenderer::DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout) {
    // Each entity is a separate draw with its own push-constant model
    // matrix/material (see LitPipeline's LitPushConstants) rather than an
    // instanced draw — this scene has a
    // handful of distinct meshes, not many identical instances. Identical
    // pipeline and descriptor set are already bound by the caller.
    auto view = m_Scene.GetRegistry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        if (!IsEntityEnabled(m_Scene.GetRegistry(), entity)) {
            continue;
        }
        const auto& [transform, meshComponent, material] = view.get<TransformComponent, MeshComponent, MaterialComponent>(entity);

        LitPushConstants pc{};
        const glm::mat4 model = transform.GetMatrix();
        std::memcpy(pc.model, &model, sizeof(pc.model));
        pc.baseColor[0] = material.baseColor.r;
        pc.baseColor[1] = material.baseColor.g;
        pc.baseColor[2] = material.baseColor.b;
        pc.baseColor[3] = 1.0f;
        pc.materialParams[0] = material.metallic;
        pc.materialParams[1] = material.roughness;
        pc.emissiveColorAndIntensity[0] = material.emissiveColor.r;
        pc.emissiveColorAndIntensity[1] = material.emissiveColor.g;
        pc.emissiveColorAndIntensity[2] = material.emissiveColor.b;
        pc.emissiveColorAndIntensity[3] = material.emissiveIntensity;
        vkCmdPushConstants(cmd, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        const VkBuffer vertexBuffer = meshComponent.mesh->GetVertexBuffer();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, meshComponent.mesh->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, meshComponent.mesh->GetIndexCount(), 1, 0, 0, 0);
    }
}

glm::mat4 EditorViewportRenderer::GetViewProjMatrix(VkExtent2D extent) const {
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
    proj[1][1] *= -1.0f;
    return proj * m_Camera.GetViewMatrix();
}

void EditorViewportRenderer::RenderGizmo(VkCommandBuffer cmd, VkExtent2D extent) {
    entt::registry& registry = m_Scene.GetRegistry();
    if (m_SelectedEntity == entt::null || !registry.valid(m_SelectedEntity) ||
        !registry.all_of<TransformComponent>(m_SelectedEntity)) {
        return;
    }
    const auto& transform = registry.get<const TransformComponent>(m_SelectedEntity);

    std::vector<Vertex3D> vertices;
    vertices.reserve(kGizmoMaxVertices);
    std::array<std::uint32_t, 3> firstVertex{};
    std::array<std::uint32_t, 3> vertexCount{};
    for (int axis = 0; axis < 3; ++axis) {
        firstVertex[axis] = static_cast<std::uint32_t>(vertices.size());
        if (m_GizmoMode == GizmoMode::Move) {
            AppendGizmoArrow(vertices, transform.position, axis);
        } else {
            AppendGizmoRing(vertices, transform.position, axis);
        }
        vertexCount[axis] = static_cast<std::uint32_t>(vertices.size()) - firstVertex[axis];
    }

    m_GizmoVertexBuffers[m_CurrentFrame]->Upload(vertices.data(), sizeof(Vertex3D) * vertices.size());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GizmoPipeline->GetPipeline());
    const VkBuffer vertexBuffer = m_GizmoVertexBuffers[m_CurrentFrame]->GetBuffer();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

    const glm::mat4 viewProj = GetViewProjMatrix(extent);
    // Plain float arrays rather than glm::mat4/vec4 directly, same convention
    // as LitPushConstants (see lit_pipeline.hpp) — sidesteps any doubt about
    // glm's struct layout matching the push_constant block's exactly.
    struct GizmoPushConstants {
        float mvp[16];
        float color[4];
    };
    for (int axis = 0; axis < 3; ++axis) {
        GizmoPushConstants pc{};
        std::memcpy(pc.mvp, &viewProj, sizeof(pc.mvp)); // gizmo vertices are already baked in world space, so this is the whole transform
        const glm::vec4& color = (axis == m_DraggingAxis) ? kGizmoHighlightColor : kGizmoAxisColors[axis];
        pc.color[0] = color.r;
        pc.color[1] = color.g;
        pc.color[2] = color.b;
        pc.color[3] = color.a;
        vkCmdPushConstants(cmd, m_GizmoPipeline->GetLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, vertexCount[axis], 1, firstVertex[axis], 0);
    }
}

int EditorViewportRenderer::PickGizmoAxis(float mouseX, float mouseY) const {
    const entt::registry& registry = m_Scene.GetRegistry();
    if (m_SelectedEntity == entt::null || !registry.valid(m_SelectedEntity) ||
        !registry.all_of<TransformComponent>(m_SelectedEntity)) {
        return -1;
    }
    const auto& transform = registry.get<const TransformComponent>(m_SelectedEntity);
    const VkExtent2D extent = m_Swapchain->GetExtent();
    const glm::mat4 viewProj = GetViewProjMatrix(extent);

    int bestAxis = -1;
    float bestDistSq = kGizmoPickThresholdPixels * kGizmoPickThresholdPixels;
    std::vector<Vertex3D> samples;
    for (int axis = 0; axis < 3; ++axis) {
        samples.clear();
        if (m_GizmoMode == GizmoMode::Move) {
            AppendGizmoArrow(samples, transform.position, axis);
        } else {
            AppendGizmoRing(samples, transform.position, axis);
        }
        for (const Vertex3D& vertex : samples) {
            const std::optional<glm::vec2> screen = ProjectToScreen(viewProj, vertex.position, extent);
            if (!screen) {
                continue;
            }
            const float dx = screen->x - mouseX;
            const float dy = screen->y - mouseY;
            const float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestAxis = axis;
            }
        }
    }
    return bestAxis;
}

void EditorViewportRenderer::BeginGizmoDrag(int axis, float mouseX, float mouseY) {
    m_DraggingAxis = axis;
    m_LastDragMouseX = mouseX;
    m_LastDragMouseY = mouseY;
}

void EditorViewportRenderer::UpdateGizmoDrag(float mouseX, float mouseY) {
    entt::registry& registry = m_Scene.GetRegistry();
    if (m_DraggingAxis < 0 || m_SelectedEntity == entt::null || !registry.valid(m_SelectedEntity) ||
        !registry.all_of<TransformComponent>(m_SelectedEntity)) {
        return;
    }
    auto& transform = registry.get<TransformComponent>(m_SelectedEntity);
    const VkExtent2D extent = m_Swapchain->GetExtent();
    const glm::mat4 viewProj = GetViewProjMatrix(extent);

    const float prevMouseX = m_LastDragMouseX;
    const float prevMouseY = m_LastDragMouseY;
    m_LastDragMouseX = mouseX;
    m_LastDragMouseY = mouseY;

    const std::optional<glm::vec2> originScreen = ProjectToScreen(viewProj, transform.position, extent);
    if (!originScreen) {
        return;
    }

    if (m_GizmoMode == GizmoMode::Move) {
        // Reprojected every call (not cached from BeginGizmoDrag) so a drag
        // that moves the entity through a lot of perspective depth still
        // tracks the mouse accurately at every step, not just at the start.
        const std::optional<glm::vec2> tipScreen =
            ProjectToScreen(viewProj, transform.position + kGizmoAxisDirections[m_DraggingAxis], extent);
        if (!tipScreen) {
            return;
        }
        const glm::vec2 axisScreenDelta = *tipScreen - *originScreen;
        const float pixelsPerUnit = glm::length(axisScreenDelta);
        if (pixelsPerUnit < 0.0001f) {
            return; // axis is edge-on to the camera this frame - no reliable screen direction to project onto
        }
        const glm::vec2 axisScreenDir = axisScreenDelta / pixelsPerUnit;
        const glm::vec2 mouseDelta(mouseX - prevMouseX, mouseY - prevMouseY);
        const float moveAmount = glm::dot(mouseDelta, axisScreenDir) / pixelsPerUnit;
        transform.position += kGizmoAxisDirections[m_DraggingAxis] * moveAmount;
    } else {
        const float prevAngle = std::atan2(prevMouseY - originScreen->y, prevMouseX - originScreen->x);
        const float newAngle = std::atan2(mouseY - originScreen->y, mouseX - originScreen->x);
        float deltaDegrees = glm::degrees(newAngle - prevAngle);
        // Wrap to [-180, 180] so a crossing of atan2's +-180 seam doesn't
        // spike the delta into a ~360-degree jump for one frame.
        while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
        while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;

        switch (m_DraggingAxis) {
            case 0: transform.rotationEulerDegrees.x += deltaDegrees; break;
            case 1: transform.rotationEulerDegrees.y += deltaDegrees; break;
            default: transform.rotationEulerDegrees.z += deltaDegrees; break;
        }
    }
}

void EditorViewportRenderer::RenderFrame() {
    VkDevice device = m_VulkanContext->GetDevice();
    VkFence inFlightFence = m_InFlightFences[m_CurrentFrame];

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    std::uint32_t imageIndex = 0;
    const Swapchain::AcquireResult acquireResult =
        m_Swapchain->AcquireNextImage(m_ImageAvailableSemaphores[m_CurrentFrame], imageIndex);
    if (acquireResult != Swapchain::AcquireResult::Success) {
        return;
    }

    vkResetFences(device, 1, &inFlightFence);

    // Runs before the render passes below so any script-driven transform
    // change (see ScriptComponent) is reflected in this same frame's draw,
    // not a frame late. Uses the same deltaTime the camera was just updated
    // with (see UpdateCamera()), not a second independently-derived value.
    //
    // Gated on PlayState::Playing (Phase 19) — Paused freezes scripts in
    // place without discarding the frame's already-mutated state, and
    // Stopped means scripts simply never run at all outside a Play session
    // (see Play()/Pause()/Stop()). The camera above is updated unconditionally
    // regardless of PlayState — it's an editor/dev-navigation concern, not
    // part of the "game," so it keeps working in every state.
    //
    // A Lua runtime error throws (see ScriptEngine::Update) — left uncaught
    // it would unwind straight out of MainWindow's QTimer lambda and crash
    // the editor. Routing it to Log instead (surfaced by the Console panel,
    // see editor/src/console_panel.hpp) means a broken script degrades
    // gracefully rather than taking the whole editor down with it.
    if (m_PlayState == PlayState::Playing) {
        try {
            m_ScriptEngine.Update(m_Scene.GetRegistry(), m_LastDeltaTime);
        } catch (const std::exception& e) {
            Log::Error(e.what());
        }
    }

    const VkExtent2D extent = m_Swapchain->GetExtent();
    const float time = GetElapsedSeconds();
    // All three buffers are updated every frame regardless of which mode is
    // active this frame — cheap (three small mapped-memory uploads) and
    // avoids the buffers ever holding stale data from before the last mode
    // switch if a scene's mode is toggled mid-session.
    UpdateLitUniformBuffer(m_CurrentFrame, extent);
    UpdateSkyUniformBuffer(m_CurrentFrame, extent, time);

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    RenderGraph graph = BuildForwardSceneRenderGraph(
        [this, cmd] { RenderShadowPass(cmd); },
        [this, cmd, imageIndex, extent] {
            RenderMainPass(cmd, m_Swapchain->GetImage(imageIndex), imageIndex, extent);
        });
    graph.Execute();

    vkEndCommandBuffer(cmd);

    VkSemaphore imageAvailable = m_ImageAvailableSemaphores[m_CurrentFrame];
    VkSemaphore renderFinished = m_Swapchain->GetRenderFinishedSemaphore(imageIndex);

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = renderFinished;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    if (vkQueueSubmit2(m_VulkanContext->GetGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit frame command buffer");
    }

    m_Swapchain->Present(renderFinished, imageIndex);

    m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFramesInFlight;
}

} // namespace polyizon

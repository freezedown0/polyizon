#include "polyizon/game_scene_renderer.hpp"

#include "polyizon/project_manifest.hpp"
#include "polyizon/rendering/render_graph.hpp"
#include "polyizon/rendering/forward_plus_lights.hpp"
#include "polyizon/log.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/scene/scene_serializer.hpp"
#include "polyizon/vulkan/context.hpp"
#include "polyizon/vulkan/lit_uniform_buffer_object.hpp"
#include "polyizon/vulkan/sky_uniform_buffer_object.hpp"
#include "polyizon/noise.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace polyizon {

namespace {

bool IsEntityEnabled(const entt::registry& registry, entt::entity entity) {
    const auto* metadata = registry.try_get<const EntityMetadataComponent>(entity);
    return metadata == nullptr || metadata->enabled;
}

} // namespace

GameSceneRenderer::GameSceneRenderer(VulkanContext& context, VkFormat colorFormat, VkFormat depthFormat)
    : m_Context(context) {
    m_SkyPipeline = std::make_unique<SkyPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_LitPipeline = std::make_unique<LitPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_ShadowPipeline = std::make_unique<ShadowPipeline>(context.GetDevice(), depthFormat);

    const CloudNoiseParams cloudNoiseParams{};
    const std::vector<std::uint8_t> cloudNoiseData = LoadOrGenerateCloudNoiseVolume(cloudNoiseParams);
    m_CloudNoiseTexture = std::make_unique<Texture3D>(context, cloudNoiseData.data(),
        cloudNoiseParams.resolution, cloudNoiseParams.resolution, cloudNoiseParams.resolution);

    m_ShadowMap = std::make_unique<ShadowMap>(context, depthFormat, kShadowMapResolution);

    CreateDescriptorResources();
}

GameSceneRenderer::~GameSceneRenderer() {
    // The caller (Application) already vkDeviceWaitIdle's before tearing
    // down its members, so no additional wait is needed here — same
    // precedent as EditorViewportRenderer's individual pipeline/buffer
    // members not each doing their own wait.
    DestroyDescriptorResources();
}

void GameSceneRenderer::LoadProject(const std::filesystem::path& projectRootDir) {
    // Only ever called once, before the render loop starts (see
    // Application's project mode) — unlike EditorViewportRenderer::
    // LoadScene(), which can replace an already-rendering scene and must
    // wait for the GPU first, m_Scene here starts empty with no in-flight
    // GPU work to wait on.
    const ProjectManifest manifest = ReadProjectManifest(projectRootDir);
    m_Scene = LoadScene(m_Context, projectRootDir / manifest.defaultScene);
}

void GameSceneRenderer::Update(float deltaTime) {
    m_ScriptEngine.Update(m_Scene.GetRegistry(), deltaTime);
}

void GameSceneRenderer::CreateDescriptorResources() {
    VkDevice device = m_Context.GetDevice();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_LitUniformBuffers[i] = std::make_unique<Buffer>(
            m_Context.GetAllocator(), sizeof(LitUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_LocalLightBuffers[i] = std::make_unique<Buffer>(
            m_Context.GetAllocator(), sizeof(ForwardPlusLightBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_Context.GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // Same shape as EditorViewportRenderer::CreateDescriptorResources: lit
    // and sky sets per frame, with a shadow map and cloud noise texture.
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

void GameSceneRenderer::DestroyDescriptorResources() {
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Context.GetDevice(), m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

glm::vec3 GameSceneRenderer::GetSunDirection() const {
    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    const float elevation = glm::radians(lighting.sunElevationDegrees);
    const float azimuth = glm::radians(lighting.sunAzimuthDegrees);
    return glm::normalize(glm::vec3(
        std::cos(elevation) * std::cos(azimuth),
        std::sin(elevation),
        std::cos(elevation) * std::sin(azimuth)));
}

glm::vec3 GameSceneRenderer::GetDirectionalLightDirection() const {
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

glm::vec4 GameSceneRenderer::GetDirectionalLightColorAndIntensity() const {
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

bool GameSceneRenderer::GetDirectionalLightCastsShadows() const {
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

glm::mat4 GameSceneRenderer::GetLightSpaceMatrix() const {
    const glm::vec3 sunDir = GetDirectionalLightDirection();
    const glm::vec3 sceneCenter(0.0f, 0.5f, 0.0f);
    constexpr float kLightDistance = 15.0f;
    constexpr float kOrthoHalfExtent = 8.0f;
    constexpr float kNear = 0.1f;
    constexpr float kFar = kLightDistance * 2.0f;

    const glm::vec3 up = (std::abs(sunDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    const glm::mat4 lightView = glm::lookAt(sceneCenter + sunDir * kLightDistance, sceneCenter, up);
    glm::mat4 lightProj = glm::ortho(-kOrthoHalfExtent, kOrthoHalfExtent, -kOrthoHalfExtent, kOrthoHalfExtent, kNear, kFar);
    lightProj[1][1] *= -1.0f;
    return lightProj * lightView;
}

void GameSceneRenderer::UpdateLitUniformBuffer(std::uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj) {
    LitUniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;
    ubo.lightSpaceMatrix = GetLightSpaceMatrix();
    const SceneLightingSettings& lighting = m_Scene.GetLightingSettings();
    ubo.sunDirectionAndAmbient = glm::vec4(GetDirectionalLightDirection(), lighting.ambientStrength);
    ubo.directionalLightColorAndIntensity = GetDirectionalLightColorAndIntensity();

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

void GameSceneRenderer::UpdateSkyUniformBuffer(
    std::uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, float time) {
    SkyUniformBufferObject sky{};
    sky.invView = glm::inverse(view);
    sky.invProj = glm::inverse(proj);
    sky.sunDirection = glm::vec4(GetSunDirection(), 0.0f);

    // Same fixed tunables Application's own hardcoded demo uses — no
    // per-scene authoring for these this phase (matching EditorViewportRenderer's
    // identical "not yet developer-editable" sky/cloud fields).
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

void GameSceneRenderer::RenderShadowPass(VkCommandBuffer cmd) {
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

void GameSceneRenderer::RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorImageView,
    VkImage depthImage, VkImageView depthImageView, VkExtent2D extent) {
    m_ImageStates.Transition(cmd, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
        { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT }, true);
    m_ImageStates.Transition(cmd, depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
        { VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT }, true);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorImageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthImageView;
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetLayout(), 0, 1,
        &m_SkyDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetLayout(), 0, 1,
        &m_LitDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
    DrawLitEntities(cmd, m_LitPipeline->GetLayout());

    vkCmdEndRendering(cmd);

    m_ImageStates.Transition(cmd, colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
        { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE });
}

void GameSceneRenderer::DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout) {
    auto view = m_Scene.GetRegistry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        if (!IsEntityEnabled(m_Scene.GetRegistry(), entity)) {
            continue;
        }
        const auto& [transform, meshComponent, material] =
            view.get<TransformComponent, MeshComponent, MaterialComponent>(entity);

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

void GameSceneRenderer::RecordFrame(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorImageView,
    VkImage depthImage, VkImageView depthImageView, VkExtent2D extent, std::uint32_t frameIndex,
    const glm::mat4& view, const glm::mat4& proj, float elapsedSeconds) {
    m_CurrentFrameIndex = frameIndex;

    UpdateLitUniformBuffer(frameIndex, view, proj);
    UpdateSkyUniformBuffer(frameIndex, view, proj, elapsedSeconds);

    RenderGraph graph = BuildForwardSceneRenderGraph(
        [this, cmd] { RenderShadowPass(cmd); },
        [this, cmd, colorImage, colorImageView, depthImage, depthImageView, extent] {
            RenderMainPass(cmd, colorImage, colorImageView, depthImage, depthImageView, extent);
        });
    graph.Execute();
}

} // namespace polyizon

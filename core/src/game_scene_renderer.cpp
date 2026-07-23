#include "polyizon/game_scene_renderer.hpp"

#include "polyizon/project_manifest.hpp"
#include "polyizon/scene/components.hpp"
#include "polyizon/scene/scene_serializer.hpp"
#include "polyizon/vulkan/classic_sky_uniform_buffer_object.hpp"
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

GameSceneRenderer::GameSceneRenderer(VulkanContext& context, VkFormat colorFormat, VkFormat depthFormat)
    : m_Context(context) {
    m_SkyPipeline = std::make_unique<SkyPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_ClassicSkyPipeline = std::make_unique<ClassicSkyPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_LitPipeline = std::make_unique<LitPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_VoxelLitPipeline = std::make_unique<VoxelLitPipeline>(context.GetDevice(), colorFormat, depthFormat);
    m_ShadowPipeline = std::make_unique<ShadowPipeline>(context.GetDevice(), depthFormat);

    const CloudNoiseParams cloudNoiseParams{};
    const std::vector<std::uint8_t> cloudNoiseData = LoadOrGenerateCloudNoiseVolume(cloudNoiseParams);
    m_CloudNoiseTexture = std::make_unique<Texture3D>(context, cloudNoiseData.data(),
        cloudNoiseParams.resolution, cloudNoiseParams.resolution, cloudNoiseParams.resolution);

    m_RealisticShadowMap = std::make_unique<ShadowMap>(
        context, depthFormat, kRealisticShadowMapResolution, ShadowSamplerMode::HardwarePcf);
    m_VoxelShadowMap = std::make_unique<ShadowMap>(
        context, depthFormat, kVoxelShadowMapResolution, ShadowSamplerMode::PlainNearest);

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
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_Context.GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_ClassicSkyUniformBuffers[i] = std::make_unique<Buffer>(m_Context.GetAllocator(),
            sizeof(ClassicSkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // Same shape as EditorViewportRenderer::CreateDescriptorResources: four
    // sets per frame-in-flight (lit, voxelLit, sky, classicSky). UBO
    // descriptors: one per set, all four. Combined-image-sampler
    // descriptors: lit's shadow map, voxelLit's shadow map, sky's cloud
    // noise volume — classicSky has none.
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 4;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight * 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxFramesInFlight * 4;

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

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> voxelLitLayouts{};
    voxelLitLayouts.fill(m_VoxelLitPipeline->GetDescriptorSetLayout());
    VkDescriptorSetAllocateInfo voxelLitAllocInfo{};
    voxelLitAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    voxelLitAllocInfo.descriptorPool = m_DescriptorPool;
    voxelLitAllocInfo.descriptorSetCount = kMaxFramesInFlight;
    voxelLitAllocInfo.pSetLayouts = voxelLitLayouts.data();
    if (vkAllocateDescriptorSets(device, &voxelLitAllocInfo, m_VoxelLitDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan voxel-lit descriptor sets");
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

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> classicSkyLayouts{};
    classicSkyLayouts.fill(m_ClassicSkyPipeline->GetDescriptorSetLayout());
    VkDescriptorSetAllocateInfo classicSkyAllocInfo{};
    classicSkyAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    classicSkyAllocInfo.descriptorPool = m_DescriptorPool;
    classicSkyAllocInfo.descriptorSetCount = kMaxFramesInFlight;
    classicSkyAllocInfo.pSetLayouts = classicSkyLayouts.data();
    if (vkAllocateDescriptorSets(device, &classicSkyAllocInfo, m_ClassicSkyDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan classic-sky descriptor sets");
    }

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo litBufferInfo{};
        litBufferInfo.buffer = m_LitUniformBuffers[i]->GetBuffer();
        litBufferInfo.offset = 0;
        litBufferInfo.range = sizeof(LitUniformBufferObject);

        VkDescriptorImageInfo realisticShadowMapInfo{};
        realisticShadowMapInfo.sampler = m_RealisticShadowMap->GetSampler();
        realisticShadowMapInfo.imageView = m_RealisticShadowMap->GetImageView();
        realisticShadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> litWrites{};
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
        litWrites[1].pImageInfo = &realisticShadowMapInfo;

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(litWrites.size()), litWrites.data(), 0, nullptr);

        VkDescriptorImageInfo voxelShadowMapInfo{};
        voxelShadowMapInfo.sampler = m_VoxelShadowMap->GetSampler();
        voxelShadowMapInfo.imageView = m_VoxelShadowMap->GetImageView();
        voxelShadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> voxelLitWrites{};
        voxelLitWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        voxelLitWrites[0].dstSet = m_VoxelLitDescriptorSets[i];
        voxelLitWrites[0].dstBinding = 0;
        voxelLitWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        voxelLitWrites[0].descriptorCount = 1;
        voxelLitWrites[0].pBufferInfo = &litBufferInfo;

        voxelLitWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        voxelLitWrites[1].dstSet = m_VoxelLitDescriptorSets[i];
        voxelLitWrites[1].dstBinding = 1;
        voxelLitWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        voxelLitWrites[1].descriptorCount = 1;
        voxelLitWrites[1].pImageInfo = &voxelShadowMapInfo;

        vkUpdateDescriptorSets(
            device, static_cast<std::uint32_t>(voxelLitWrites.size()), voxelLitWrites.data(), 0, nullptr);

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

        VkDescriptorBufferInfo classicSkyBufferInfo{};
        classicSkyBufferInfo.buffer = m_ClassicSkyUniformBuffers[i]->GetBuffer();
        classicSkyBufferInfo.offset = 0;
        classicSkyBufferInfo.range = sizeof(ClassicSkyUniformBufferObject);

        VkWriteDescriptorSet classicSkyWrite{};
        classicSkyWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        classicSkyWrite.dstSet = m_ClassicSkyDescriptorSets[i];
        classicSkyWrite.dstBinding = 0;
        classicSkyWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        classicSkyWrite.descriptorCount = 1;
        classicSkyWrite.pBufferInfo = &classicSkyBufferInfo;

        vkUpdateDescriptorSets(device, 1, &classicSkyWrite, 0, nullptr);
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

glm::mat4 GameSceneRenderer::GetLightSpaceMatrix() const {
    const glm::vec3 sunDir = GetSunDirection();
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
    ubo.sunDirectionAndAmbient = glm::vec4(GetSunDirection(), m_Scene.GetLightingSettings().ambientStrength);

    entt::registry& registry = m_Scene.GetRegistry();

    int pointCount = 0;
    auto pointView = registry.view<const TransformComponent, const PointLightComponent>();
    for (const entt::entity entity : pointView) {
        if (pointCount >= kMaxPointLights) {
            break;
        }
        const auto& transform = pointView.get<const TransformComponent>(entity);
        const auto& light = pointView.get<const PointLightComponent>(entity);
        ubo.pointLightPositionAndRange[pointCount] = glm::vec4(transform.position, light.range);
        ubo.pointLightColorAndIntensity[pointCount] = glm::vec4(light.color, light.intensity);
        ++pointCount;
    }

    int spotCount = 0;
    auto spotView = registry.view<const TransformComponent, const SpotLightComponent>();
    for (const entt::entity entity : spotView) {
        if (spotCount >= kMaxSpotLights) {
            break;
        }
        const auto& transform = spotView.get<const TransformComponent>(entity);
        const auto& light = spotView.get<const SpotLightComponent>(entity);
        ubo.spotLightPositionAndRange[spotCount] = glm::vec4(transform.position, light.range);
        ubo.spotLightColorAndIntensity[spotCount] = glm::vec4(light.color, light.intensity);
        ubo.spotLightDirectionAndInnerCos[spotCount] =
            glm::vec4(transform.GetForward(), std::cos(glm::radians(light.innerConeDegrees)));
        ubo.spotLightOuterCos[spotCount] = glm::vec4(std::cos(glm::radians(light.outerConeDegrees)), 0.0f, 0.0f, 0.0f);
        ++spotCount;
    }

    ubo.lightCounts = glm::ivec4(pointCount, spotCount, 0, 0);

    m_LitUniformBuffers[frameIndex]->Upload(&ubo, sizeof(ubo));
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
    sky.timeAndSun = glm::vec4(time, glm::radians(1.5f), 0.0f, 0.0f);
    sky.atmosphereParams0 = glm::vec4(6360.0f, 60.0f, 0.5f, 8.0f);
    sky.atmosphereParams1 = glm::vec4(1.2f, 0.76f, 10.0f, 1.2f);
    sky.cloudParams0 = glm::vec4(1.5f, 4.0f, 0.4f, 1.1f);
    sky.cloudParams1 = glm::vec4(0.02f, 0.0f, 0.8f, -0.2f);
    sky.cloudParams2 = glm::vec4(1.0f, 0.2f, 0.02f, 0.0f);
    sky.stepCounts = glm::ivec4(16, 8, 64, 8);

    m_SkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void GameSceneRenderer::UpdateClassicSkyUniformBuffer(
    std::uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj) {
    ClassicSkyUniformBufferObject sky{};
    sky.invView = glm::inverse(view);
    sky.invProj = glm::inverse(proj);
    sky.sunDirection = glm::vec4(GetSunDirection(), glm::radians(1.5f));

    sky.dayHorizonColor = glm::vec4(0.75f, 0.85f, 1.0f, 0.0f);
    sky.dayZenithColor = glm::vec4(0.25f, 0.55f, 0.95f, 0.0f);
    sky.nightHorizonColor = glm::vec4(0.05f, 0.06f, 0.12f, 0.0f);
    sky.nightZenithColor = glm::vec4(0.01f, 0.01f, 0.04f, 0.0f);

    m_ClassicSkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void GameSceneRenderer::RenderShadowPass(VkCommandBuffer cmd) {
    const bool voxelMode = (m_Scene.GetLightingSettings().mode == LightingMode::Voxel);
    ShadowMap& activeShadowMap = voxelMode ? *m_VoxelShadowMap : *m_RealisticShadowMap;
    bool& firstFrame = voxelMode ? m_VoxelShadowMapFirstFrame : m_RealisticShadowMapFirstFrame;

    const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier2 toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toDepthAttachment.srcStageMask =
        firstFrame ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toDepthAttachment.srcAccessMask = firstFrame ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_READ_BIT;
    toDepthAttachment.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toDepthAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepthAttachment.oldLayout = firstFrame ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.image = activeShadowMap.GetImage();
    toDepthAttachment.subresourceRange = depthRange;

    VkDependencyInfo toDepthDep{};
    toDepthDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toDepthDep.imageMemoryBarrierCount = 1;
    toDepthDep.pImageMemoryBarriers = &toDepthAttachment;
    vkCmdPipelineBarrier2(cmd, &toDepthDep);

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

    VkImageMemoryBarrier2 toShaderRead = toDepthAttachment;
    toShaderRead.srcStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toShaderRead.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toShaderRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDependencyInfo toShaderReadDep{};
    toShaderReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toShaderReadDep.imageMemoryBarrierCount = 1;
    toShaderReadDep.pImageMemoryBarriers = &toShaderRead;
    vkCmdPipelineBarrier2(cmd, &toShaderReadDep);

    firstFrame = false;
}

void GameSceneRenderer::RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorImageView,
    VkImage depthImage, VkImageView depthImageView, VkExtent2D extent) {
    const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier2 toColorAttachment{};
    toColorAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColorAttachment.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toColorAttachment.srcAccessMask = VK_ACCESS_2_NONE;
    toColorAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColorAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColorAttachment.image = colorImage;
    toColorAttachment.subresourceRange = colorRange;

    const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier2 toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toDepthAttachment.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toDepthAttachment.srcAccessMask = VK_ACCESS_2_NONE;
    toDepthAttachment.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toDepthAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepthAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.image = depthImage;
    toDepthAttachment.subresourceRange = depthRange;

    const std::array<VkImageMemoryBarrier2, 2> toAttachmentBarriers = { toColorAttachment, toDepthAttachment };
    VkDependencyInfo toAttachmentDep{};
    toAttachmentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toAttachmentDep.imageMemoryBarrierCount = static_cast<std::uint32_t>(toAttachmentBarriers.size());
    toAttachmentDep.pImageMemoryBarriers = toAttachmentBarriers.data();
    vkCmdPipelineBarrier2(cmd, &toAttachmentDep);

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

    if (m_Scene.GetLightingSettings().mode == LightingMode::Voxel) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ClassicSkyPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ClassicSkyPipeline->GetLayout(), 0, 1,
            &m_ClassicSkyDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VoxelLitPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VoxelLitPipeline->GetLayout(), 0, 1,
            &m_VoxelLitDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
        DrawLitEntities(cmd, m_VoxelLitPipeline->GetLayout());
    } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetLayout(), 0, 1,
            &m_SkyDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetLayout(), 0, 1,
            &m_LitDescriptorSets[m_CurrentFrameIndex], 0, nullptr);
        DrawLitEntities(cmd, m_LitPipeline->GetLayout());
    }

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toPresent = toColorAttachment;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkDependencyInfo toPresentDep{};
    toPresentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toPresentDep.imageMemoryBarrierCount = 1;
    toPresentDep.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(cmd, &toPresentDep);
}

void GameSceneRenderer::DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout) {
    auto view = m_Scene.GetRegistry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        const auto& [transform, meshComponent, material] =
            view.get<TransformComponent, MeshComponent, MaterialComponent>(entity);

        LitPushConstants pc{};
        const glm::mat4 model = transform.GetMatrix();
        std::memcpy(pc.model, &model, sizeof(pc.model));
        pc.baseColor[0] = material.baseColor.r;
        pc.baseColor[1] = material.baseColor.g;
        pc.baseColor[2] = material.baseColor.b;
        pc.baseColor[3] = 1.0f;
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
    UpdateClassicSkyUniformBuffer(frameIndex, view, proj);

    RenderShadowPass(cmd);
    RenderMainPass(cmd, colorImage, colorImageView, depthImage, depthImageView, extent);
}

} // namespace polyizon

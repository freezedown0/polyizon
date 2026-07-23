#include "editor_viewport_renderer.hpp"

#include "scene_serializer.hpp"

#include "polyizon/log.hpp"
#include "polyizon/noise.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace polyizon {

EditorViewportRenderer::EditorViewportRenderer(HWND hwnd, HINSTANCE hinstance, std::uint32_t width, std::uint32_t height) {
    m_VulkanContext = std::make_unique<VulkanContext>(hwnd, hinstance, "Polyizon Editor");
    m_Swapchain = std::make_unique<Swapchain>(*m_VulkanContext, width, height);
    m_SkyPipeline = std::make_unique<SkyPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_ClassicSkyPipeline = std::make_unique<ClassicSkyPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_LitPipeline = std::make_unique<LitPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_VoxelLitPipeline = std::make_unique<VoxelLitPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_ShadowPipeline = std::make_unique<ShadowPipeline>(m_VulkanContext->GetDevice(), m_Swapchain->GetDepthFormat());

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
    m_RealisticShadowMap = std::make_unique<ShadowMap>(
        *m_VulkanContext, m_Swapchain->GetDepthFormat(), kRealisticShadowMapResolution, ShadowSamplerMode::HardwarePcf);
    m_VoxelShadowMap = std::make_unique<ShadowMap>(
        *m_VulkanContext, m_Swapchain->GetDepthFormat(), kVoxelShadowMapResolution, ShadowSamplerMode::PlainNearest);

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
    m_Scene = ::LoadScene(*m_VulkanContext, sceneFile);
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

glm::mat4 EditorViewportRenderer::GetLightSpaceMatrix() const {
    const glm::vec3 sunDir = GetSunDirection();
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
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_ClassicSkyUniformBuffers[i] = std::make_unique<Buffer>(m_VulkanContext->GetAllocator(),
            sizeof(ClassicSkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // Four sets per frame-in-flight now: lit (Realistic), voxelLit (Voxel —
    // reuses m_LitUniformBuffers, see its member comment), sky (Realistic),
    // classicSky (Voxel). UBO descriptors: one per set, all four. Combined-
    // image-sampler descriptors: lit's shadow map, voxelLit's shadow map,
    // sky's cloud noise volume — classicSky has none (binding 0 only).
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

        // Same m_LitUniformBuffers[i] buffer as above (binding 0) — only the
        // shadow sampler (binding 1) differs, pointing at the coarse voxel
        // ShadowMap instead. See the header's m_LitUniformBuffers comment.
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
    ubo.sunDirectionAndAmbient = glm::vec4(GetSunDirection(), m_Scene.GetLightingSettings().ambientStrength);

    m_LitUniformBuffers[frameIndex]->Upload(&ubo, sizeof(ubo));
}

void EditorViewportRenderer::UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time) {
    SkyUniformBufferObject sky{};
    sky.invView = glm::inverse(m_Camera.GetViewMatrix());

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
    proj[1][1] *= -1.0f;
    sky.invProj = glm::inverse(proj);

    sky.sunDirection = glm::vec4(GetSunDirection(), 0.0f);

    sky.timeAndSun = glm::vec4(time, glm::radians(1.5f), 0.0f, 0.0f);
    sky.atmosphereParams0 = glm::vec4(6360.0f, 60.0f, 0.5f, 8.0f);
    sky.atmosphereParams1 = glm::vec4(1.2f, 0.76f, 10.0f, m_SkyExposure);

    sky.cloudParams0 = glm::vec4(1.5f, 4.0f, m_CloudCoverage, m_CloudDensityMultiplier);
    sky.cloudParams1 = glm::vec4(m_CloudWindSpeed, glm::radians(m_CloudWindDirectionDegrees), 0.8f, -0.2f);
    sky.cloudParams2 = glm::vec4(1.0f, 0.2f, 0.02f, 0.0f);

    sky.stepCounts = glm::ivec4(m_AtmospherePrimarySteps, m_AtmosphereSunSteps, m_CloudPrimarySteps, m_CloudSunShadowSteps);

    m_SkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void EditorViewportRenderer::UpdateClassicSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent) {
    ClassicSkyUniformBufferObject sky{};
    sky.invView = glm::inverse(m_Camera.GetViewMatrix());

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 50.0f);
    proj[1][1] *= -1.0f;
    sky.invProj = glm::inverse(proj);

    // w = sun angular radius, same value sky.frag's sun disk uses (see
    // timeAndSun.y in UpdateSkyUniformBuffer) so both sky styles show a
    // similarly-sized sun.
    sky.sunDirection = glm::vec4(GetSunDirection(), glm::radians(1.5f));

    // Fixed, unauthored-yet palette — a simple classic day/night gradient.
    // No Qt panel or scene data exposes these this phase (same tier of
    // polish as the Realistic sky's cloud/atmosphere tunables).
    sky.dayHorizonColor = glm::vec4(0.75f, 0.85f, 1.0f, 0.0f);
    sky.dayZenithColor = glm::vec4(0.25f, 0.55f, 0.95f, 0.0f);
    sky.nightHorizonColor = glm::vec4(0.05f, 0.06f, 0.12f, 0.0f);
    sky.nightZenithColor = glm::vec4(0.01f, 0.01f, 0.04f, 0.0f);

    m_ClassicSkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void EditorViewportRenderer::RenderShadowPass(VkCommandBuffer cmd) {
    const bool voxelMode = (m_Scene.GetLightingSettings().mode == LightingMode::Voxel);
    ShadowMap& activeShadowMap = voxelMode ? *m_VoxelShadowMap : *m_RealisticShadowMap;
    bool& firstFrame = voxelMode ? m_VoxelShadowMapFirstFrame : m_RealisticShadowMapFirstFrame;

    const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier2 toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    // First frame THIS MAP has ever rendered: its initial layout is UNDEFINED
    // (see ShadowMap::CreateImage()), transitioned from TOP_OF_PIPE/NONE.
    // Every subsequent render of it: it's coming from a previous frame's
    // shader-read (see the barrier after vkCmdEndRendering below), so the
    // source stage/access must match what actually last touched it. Tracked
    // per-map (see the header's m_RealisticShadowMapFirstFrame/
    // m_VoxelShadowMapFirstFrame comment) since the other map may have
    // already rendered many frames by the time this one gets its first.
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

void EditorViewportRenderer::RenderMainPass(VkCommandBuffer cmd, VkImage colorImage, std::uint32_t imageIndex, VkExtent2D extent) {
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
    toDepthAttachment.image = m_Swapchain->GetDepthImage();
    toDepthAttachment.subresourceRange = depthRange;

    const std::array<VkImageMemoryBarrier2, 2> toAttachmentBarriers = { toColorAttachment, toDepthAttachment };
    VkDependencyInfo toAttachmentDep{};
    toAttachmentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toAttachmentDep.imageMemoryBarrierCount = static_cast<std::uint32_t>(toAttachmentBarriers.size());
    toAttachmentDep.pImageMemoryBarriers = toAttachmentBarriers.data();
    vkCmdPipelineBarrier2(cmd, &toAttachmentDep);

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
    if (m_Scene.GetLightingSettings().mode == LightingMode::Voxel) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ClassicSkyPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ClassicSkyPipeline->GetLayout(), 0, 1,
            &m_ClassicSkyDescriptorSets[m_CurrentFrame], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VoxelLitPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VoxelLitPipeline->GetLayout(), 0, 1,
            &m_VoxelLitDescriptorSets[m_CurrentFrame], 0, nullptr);
        DrawLitEntities(cmd, m_VoxelLitPipeline->GetLayout());
    } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetLayout(), 0, 1,
            &m_SkyDescriptorSets[m_CurrentFrame], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline->GetLayout(), 0, 1,
            &m_LitDescriptorSets[m_CurrentFrame], 0, nullptr);
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

void EditorViewportRenderer::DrawLitEntities(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout) {
    // Each entity is a separate draw with its own push-constant model
    // matrix/color (see LitPipeline/VoxelLitPipeline's shared
    // LitPushConstants) rather than an instanced draw — this scene has a
    // handful of distinct meshes, not many identical instances. Identical
    // for both lighting modes; only the caller's already-bound
    // pipeline/descriptor set differs.
    auto view = m_Scene.GetRegistry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto entity : view) {
        const auto& [transform, meshComponent, material] = view.get<TransformComponent, MeshComponent, MaterialComponent>(entity);

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
    // A Lua runtime error throws (see ScriptEngine::Update) — left uncaught
    // it would unwind straight out of MainWindow's QTimer lambda and crash
    // the editor. Routing it to Log instead (surfaced by the Console panel,
    // see editor/src/console_panel.hpp) means a broken script degrades
    // gracefully rather than taking the whole editor down with it.
    try {
        m_ScriptEngine.Update(m_Scene.GetRegistry(), m_LastDeltaTime);
    } catch (const std::exception& e) {
        Log::Error(e.what());
    }

    const VkExtent2D extent = m_Swapchain->GetExtent();
    const float time = GetElapsedSeconds();
    // All three buffers are updated every frame regardless of which mode is
    // active this frame — cheap (three small mapped-memory uploads) and
    // avoids the buffers ever holding stale data from before the last mode
    // switch if a scene's mode is toggled mid-session.
    UpdateLitUniformBuffer(m_CurrentFrame, extent);
    UpdateSkyUniformBuffer(m_CurrentFrame, extent, time);
    UpdateClassicSkyUniformBuffer(m_CurrentFrame, extent);

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    RenderShadowPass(cmd);
    RenderMainPass(cmd, m_Swapchain->GetImage(imageIndex), imageIndex, extent);

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

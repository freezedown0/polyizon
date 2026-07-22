#include "editor_viewport_renderer.hpp"

#include "polyizon/noise.hpp"
#include "polyizon/vulkan/vertex.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace polyizon {

namespace {

// Identical hardcoded demo scene to Application's (application.cpp) — this
// phase renders the same content on both paths, no editor-specific scene
// authoring exists yet.
const std::array<Vertex, 4> kQuadVertices = {
    Vertex{ {-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },
    Vertex{ { 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },
    Vertex{ { 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },
    Vertex{ {-0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },
};
const std::array<std::uint16_t, 6> kQuadIndices = { 0, 1, 2, 2, 3, 0 };

constexpr std::size_t kInstanceCount = 5;

const std::array<glm::vec3, kInstanceCount> kInstancePositions = {
    glm::vec3(-0.9f, 0.0f,  0.6f),
    glm::vec3(-0.45f, 0.0f, 0.3f),
    glm::vec3( 0.0f, 0.0f,  0.0f),
    glm::vec3( 0.45f, 0.0f, -0.3f),
    glm::vec3( 0.9f, 0.0f, -0.6f),
};

glm::mat4 ComputeInstanceModel(std::size_t index, float time) {
    const float phase = static_cast<float>(index) * glm::radians(72.0f);
    const glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f), time * glm::radians(90.0f) + phase, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::translate(glm::mat4(1.0f), kInstancePositions[index]) * rotation;
}

} // namespace

EditorViewportRenderer::EditorViewportRenderer(HWND hwnd, HINSTANCE hinstance, std::uint32_t width, std::uint32_t height) {
    m_VulkanContext = std::make_unique<VulkanContext>(hwnd, hinstance, "Polyizon Editor");
    m_Swapchain = std::make_unique<Swapchain>(*m_VulkanContext, width, height);
    m_Pipeline = std::make_unique<GraphicsPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_SkyPipeline = std::make_unique<SkyPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());

    m_Texture = Image::CreateFromFile(*m_VulkanContext, "checkerboard.png"); // must precede CreateDescriptorResources()

    const CloudNoiseParams cloudNoiseParams{}; // same defaults as Application; not yet developer-editable here either
    const std::vector<std::uint8_t> cloudNoiseData = LoadOrGenerateCloudNoiseVolume(cloudNoiseParams);
    m_CloudNoiseTexture = std::make_unique<Texture3D>(*m_VulkanContext, cloudNoiseData.data(),
        cloudNoiseParams.resolution, cloudNoiseParams.resolution, cloudNoiseParams.resolution);

    CreateDescriptorResources();

    m_VertexBuffer = Buffer::CreateDeviceLocal(*m_VulkanContext, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        kQuadVertices.data(), sizeof(Vertex) * kQuadVertices.size());
    m_IndexBuffer = Buffer::CreateDeviceLocal(*m_VulkanContext, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        kQuadIndices.data(), sizeof(std::uint16_t) * kQuadIndices.size());

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_InstanceBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(glm::mat4) * kInstanceCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }

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

void EditorViewportRenderer::Resize(std::uint32_t width, std::uint32_t height) {
    m_Swapchain->Resize(width, height);
}

float EditorViewportRenderer::GetElapsedSeconds() const {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_ClockStart).count();
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
        m_UniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxFramesInFlight * 2;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor pool");
    }

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(m_Pipeline->GetDescriptorSetLayout());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan descriptor sets");
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
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_UniformBuffers[i]->GetBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_Texture->GetSampler();
        imageInfo.imageView = m_Texture->GetImageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_DescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_DescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

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

void EditorViewportRenderer::UpdateUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent) {
    UniformBufferObject ubo{};
    ubo.view = m_Camera.GetViewMatrix();

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1.0f;

    ubo.fogColorAndDensity = glm::vec4(0.75f, 0.8f, 0.85f, m_FogDensity);

    m_UniformBuffers[frameIndex]->Upload(&ubo, sizeof(ubo));
}

void EditorViewportRenderer::UpdateInstanceBuffer(std::uint32_t frameIndex, float time) {
    std::array<glm::mat4, kInstanceCount> models{};
    for (std::size_t i = 0; i < kInstanceCount; ++i) {
        models[i] = ComputeInstanceModel(i, time);
    }
    m_InstanceBuffers[frameIndex]->Upload(models.data(), sizeof(glm::mat4) * kInstanceCount);
}

void EditorViewportRenderer::UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time) {
    SkyUniformBufferObject sky{};
    sky.invView = glm::inverse(m_Camera.GetViewMatrix());

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    proj[1][1] *= -1.0f;
    sky.invProj = glm::inverse(proj);

    const float elevation = glm::radians(m_SunElevationDegrees);
    const float azimuth = glm::radians(m_SunAzimuthDegrees);
    sky.sunDirection = glm::vec4(glm::normalize(glm::vec3(
        std::cos(elevation) * std::cos(azimuth),
        std::sin(elevation),
        std::cos(elevation) * std::sin(azimuth))), 0.0f);

    sky.timeAndSun = glm::vec4(time, glm::radians(1.5f), 0.0f, 0.0f);
    sky.atmosphereParams0 = glm::vec4(6360.0f, 60.0f, 0.5f, 8.0f);
    sky.atmosphereParams1 = glm::vec4(1.2f, 0.76f, 10.0f, m_SkyExposure);

    sky.cloudParams0 = glm::vec4(1.5f, 4.0f, m_CloudCoverage, m_CloudDensityMultiplier);
    sky.cloudParams1 = glm::vec4(m_CloudWindSpeed, glm::radians(m_CloudWindDirectionDegrees), 0.8f, -0.2f);
    sky.cloudParams2 = glm::vec4(1.0f, 0.2f, 0.02f, 0.0f);

    sky.stepCounts = glm::ivec4(m_AtmospherePrimarySteps, m_AtmosphereSunSteps, m_CloudPrimarySteps, m_CloudSunShadowSteps);

    m_SkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
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

    const VkExtent2D extent = m_Swapchain->GetExtent();
    const float time = GetElapsedSeconds();
    UpdateUniformBuffer(m_CurrentFrame, extent);
    UpdateInstanceBuffer(m_CurrentFrame, time);
    UpdateSkyUniformBuffer(m_CurrentFrame, extent, time);

    VkCommandBuffer cmd = m_CommandBuffers[m_CurrentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImage image = m_Swapchain->GetImage(imageIndex);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

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
    toColorAttachment.image = image;
    toColorAttachment.subresourceRange = range;

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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline->GetLayout(), 0, 1,
        &m_SkyDescriptorSets[m_CurrentFrame], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetPipeline());

    const VkBuffer vertexBuffers[] = { m_VertexBuffer->GetBuffer(), m_InstanceBuffers[m_CurrentFrame]->GetBuffer() };
    const VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_IndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1,
        &m_DescriptorSets[m_CurrentFrame], 0, nullptr);

    vkCmdDrawIndexed(cmd, 6, static_cast<std::uint32_t>(kInstanceCount), 0, 0, 0);

    // No ImGui overlay on this path yet (see class doc comment) — this
    // rendering scope just ends here instead of also drawing debug-overlay
    // draw data like Application::RenderFrame() does.
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

#include "polyizon/application.hpp"

#include "polyizon/noise.hpp"

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#pragma warning(push, 0)
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace polyizon {

namespace {

// NDC y is down (no viewport-flip trick used, matching the already-verified
// hardcoded triangle this replaced) — position.y = -0.5 is toward the top
// of the window. Texture coordinates need no V-flip: Vulkan's sampling
// convention puts (0,0) at the image's top-left with V increasing downward,
// the same direction as this quad's screen-space Y, so corners map 1:1.
const std::array<Vertex, 4> kQuadVertices = {
    Vertex{ {-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} }, // top-left, red
    Vertex{ { 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} }, // top-right, green
    Vertex{ { 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} }, // bottom-right, blue
    Vertex{ {-0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f} }, // bottom-left, yellow
};
// Winding is clockwise in screen space for both triangles, matching
// VK_FRONT_FACE_CLOCKWISE (pipeline.cpp) — correct/consistent even though
// cullMode is currently VK_CULL_MODE_NONE.
const std::array<std::uint16_t, 6> kQuadIndices = { 0, 1, 2, 2, 3, 0 };

constexpr std::size_t kInstanceCount = 5;

// X shift (0.45) is smaller than the quad's half-width (0.5), so
// neighboring instances overlap in screen space before perspective is even
// applied — makes the depth test's effect on occlusion unambiguous in a
// screenshot. Z recedes from +0.6 (nearest) to -0.6 (farthest).
const std::array<glm::vec3, kInstanceCount> kInstancePositions = {
    glm::vec3(-0.9f, 0.0f,  0.6f),
    glm::vec3(-0.45f, 0.0f, 0.3f),
    glm::vec3( 0.0f, 0.0f,  0.0f),
    glm::vec3( 0.45f, 0.0f, -0.3f),
    glm::vec3( 0.9f, 0.0f, -0.6f),
};

// Same Z-axis spin as before, phase-shifted per instance (2pi/5 apart) so
// the row doesn't read as one rigid block — proves per-draw-call push
// constants are actually taking effect, not just translation.
glm::mat4 ComputeInstanceModel(std::size_t index, float time) {
    const float phase = static_cast<float>(index) * glm::radians(72.0f);
    const glm::mat4 rotation = glm::rotate(
        glm::mat4(1.0f), time * glm::radians(90.0f) + phase, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::translate(glm::mat4(1.0f), kInstancePositions[index]) * rotation;
}

void CheckImGuiVulkanResult(VkResult result) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan backend call failed");
    }
}

} // namespace

Application::Application(const ApplicationSpec& spec) {
    WindowProps props;
    props.title = spec.name;
    props.width = spec.windowWidth;
    props.height = spec.windowHeight;

    m_Window = std::make_unique<Window>(props);

    m_Window->SetCloseCallback([this]() { OnWindowClose(); });
    m_Window->SetResizeCallback([this](std::uint32_t width, std::uint32_t height) {
        OnWindowResize(width, height);
    });
    m_Window->SetKeyCallback([this](int key, int scancode, int action, int mods) {
        OnKey(key, scancode, action, mods);
    });
    m_Window->SetCursorPosCallback([this](double x, double y) { OnCursorPos(x, y); });
    m_Window->SetMouseButtonCallback([this](int button, int action, int mods) { OnMouseButton(button, action, mods); });
    m_Window->SetScrollCallback([this](double xOffset, double yOffset) { OnScroll(xOffset, yOffset); });
    m_Window->SetFocusCallback([this](int focused) { OnWindowFocus(focused); });
    m_Window->SetCursorEnterCallback([this](int entered) { OnCursorEnter(entered); });
    m_Window->SetCursorCaptured(true); // start in FPS mouse-look mode; Escape toggles capture

    m_VulkanContext = std::make_unique<VulkanContext>(*m_Window, spec.name);
    m_Swapchain = std::make_unique<Swapchain>(*m_VulkanContext, *m_Window);
    m_Pipeline = std::make_unique<GraphicsPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());
    m_SkyPipeline = std::make_unique<SkyPipeline>(
        m_VulkanContext->GetDevice(), m_Swapchain->GetImageFormat(), m_Swapchain->GetDepthFormat());

    m_Texture = Image::CreateFromFile(*m_VulkanContext, "checkerboard.png"); // must precede CreateDescriptorResources()

    const CloudNoiseParams cloudNoiseParams{}; // defaults; not yet developer-editable (see noise.hpp)
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
    InitImGui();
}

Application::~Application() {
    // GPU work may still be in flight; must finish before the swapchain,
    // sync objects, and device are torn down (by this destructor and then
    // the member unique_ptrs, in that order).
    if (m_VulkanContext) {
        vkDeviceWaitIdle(m_VulkanContext->GetDevice());
    }
    ShutdownImGui();
    DestroyFrameSyncObjects();
    DestroyDescriptorResources();
}

void Application::Run() {
    m_LastFrameTime = static_cast<float>(glfwGetTime());

    while (m_Running && !m_Window->ShouldClose()) {
        const float time = static_cast<float>(glfwGetTime());
        const float deltaTime = std::min(time - m_LastFrameTime, kMaxDeltaTime);
        m_LastFrameTime = time;

        m_Window->PollEvents();
        ProcessCameraKeyboardInput(deltaTime);
        OnUpdate(deltaTime);
        RenderFrame();
    }
}

void Application::Stop() {
    m_Running = false;
}

void Application::OnUpdate(float /*deltaTime*/) {
    // Base loop is intentionally empty. Once the EnTT registry exists, a
    // derived Application (or this method directly) ticks it here, followed
    // by renderer frame submission.
}

void Application::OnWindowResize(std::uint32_t /*width*/, std::uint32_t /*height*/) {
    // Guarded: GLFW can fire the framebuffer-size callback before the
    // swapchain is constructed. Actual recreation is deferred to the next
    // AcquireNextImage() call, never done synchronously here.
    if (m_Swapchain) {
        m_Swapchain->NotifyResized();
    }
}

void Application::OnWindowClose() {
    Stop();
}

void Application::OnKey(int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        m_Window->SetCursorCaptured(!m_Window->IsCursorCaptured());
        m_FirstMouseSample = true; // avoid a look-jump when re-capturing
    }
    ImGui_ImplGlfw_KeyCallback(m_Window->GetNativeHandle(), key, scancode, action, mods);
}

void Application::OnCursorPos(double x, double y) {
    // While released (post-Escape), the cursor reports real absolute
    // positions — forward those to ImGui and skip the camera entirely.
    // While captured, GLFW_CURSOR_DISABLED gives virtual/unbounded deltas
    // meant only for the camera's relative look, which would look like
    // garbage absolute coordinates to ImGui.
    if (!m_Window->IsCursorCaptured()) {
        ImGui_ImplGlfw_CursorPosCallback(m_Window->GetNativeHandle(), x, y);
        return;
    }

    if (m_FirstMouseSample) {
        m_LastMouseX = x;
        m_LastMouseY = y;
        m_FirstMouseSample = false;
        return;
    }

    const float xOffset = static_cast<float>(x - m_LastMouseX);
    const float yOffset = static_cast<float>(m_LastMouseY - y); // inverted: screen Y grows downward
    m_LastMouseX = x;
    m_LastMouseY = y;
    m_Camera.ProcessMouseMovement(xOffset, yOffset);
}

void Application::OnMouseButton(int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(m_Window->GetNativeHandle(), button, action, mods);
}

void Application::OnScroll(double xOffset, double yOffset) {
    ImGui_ImplGlfw_ScrollCallback(m_Window->GetNativeHandle(), xOffset, yOffset);
}

void Application::OnWindowFocus(int focused) {
    ImGui_ImplGlfw_WindowFocusCallback(m_Window->GetNativeHandle(), focused);
}

void Application::OnCursorEnter(int entered) {
    ImGui_ImplGlfw_CursorEnterCallback(m_Window->GetNativeHandle(), entered);
}

void Application::ProcessCameraKeyboardInput(float deltaTime) {
    // Cursor released: the user is interacting with the ImGui overlay, not
    // flying the camera.
    if (!m_Window->IsCursorCaptured()) {
        return;
    }
    // Skip while unfocused: GLFW can leave glfwGetKey() reporting a stale
    // GLFW_PRESS for a key released while the window lacked focus (no
    // release event was ever delivered), which would otherwise look like a
    // stuck movement key when focus returns.
    if (glfwGetWindowAttrib(m_Window->GetNativeHandle(), GLFW_FOCUSED) == GLFW_FALSE) {
        return;
    }
    m_Camera.ProcessKeyboard(m_Window->GetNativeHandle(), deltaTime);
}

void Application::CreateFrameSyncObjects() {
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
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // so frame 0's wait doesn't block forever

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan frame sync objects");
        }
    }
}

void Application::DestroyFrameSyncObjects() {
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
        vkDestroyCommandPool(device, m_CommandPool, nullptr); // also frees the allocated command buffers
        m_CommandPool = VK_NULL_HANDLE;
    }
}

void Application::CreateDescriptorResources() {
    VkDevice device = m_VulkanContext->GetDevice();

    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_UniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_SkyUniformBuffers[i] = std::make_unique<Buffer>(
            m_VulkanContext->GetAllocator(), sizeof(SkyUniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // poolSizes[0] (UBO) covers both the quad pipeline's per-frame UBO and
    // the sky pipeline's per-frame UBO — kMaxFramesInFlight * 2 total sets
    // now come out of this one pool (quad set + sky set per frame-in-flight)
    // rather than adding a second pool. poolSizes[1] (combined image
    // sampler) similarly covers both the quad pipeline's checkerboard
    // texture and the sky pipeline's cloud noise volume.
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

    // Bind each frame-slot's descriptor set to its own persistent UBO buffer
    // and the (single, static) texture once, here — only the UBO's contents
    // change per frame (via Upload() in RenderFrame()), never the handles
    // either binding points at.
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

void Application::DestroyDescriptorResources() {
    if (!m_VulkanContext) {
        return;
    }
    // Destroying the pool implicitly frees all sets allocated from it; the
    // UBO buffers themselves are unique_ptr and destruct via normal
    // reverse-declaration-order teardown, not from here.
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_VulkanContext->GetDevice(), m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
}

void Application::UpdateUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent) {
    UniformBufferObject ubo{};
    ubo.view = m_Camera.GetViewMatrix();

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1.0f; // Vulkan NDC is Y-down; glm::perspective assumes Y-up.

    // A pale sky-blue-gray, close to a typical daytime horizon haze tone —
    // kept fixed rather than sampled from the sky shader (the two passes
    // don't share data), tuned by eye to blend plausibly with it.
    ubo.fogColorAndDensity = glm::vec4(0.75f, 0.8f, 0.85f, m_FogDensity);

    m_UniformBuffers[frameIndex]->Upload(&ubo, sizeof(ubo));
}

void Application::UpdateInstanceBuffer(std::uint32_t frameIndex, float time) {
    std::array<glm::mat4, kInstanceCount> models{};
    for (std::size_t i = 0; i < kInstanceCount; ++i) {
        models[i] = ComputeInstanceModel(i, time);
    }
    m_InstanceBuffers[frameIndex]->Upload(models.data(), sizeof(glm::mat4) * kInstanceCount);
}

void Application::UpdateSkyUniformBuffer(std::uint32_t frameIndex, VkExtent2D extent, float time) {
    SkyUniformBufferObject sky{};
    sky.invView = glm::inverse(m_Camera.GetViewMatrix());

    // Same Y-flipped perspective as UpdateUniformBuffer() above — the sky
    // pass needs the identical projection to reconstruct rays that line up
    // with what the quads are drawn with.
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

    sky.timeAndSun = glm::vec4(time, glm::radians(1.5f), 0.0f, 0.0f); // wider angular radius than the real sun (0.5deg) so it reads as a clear disk on screen
    sky.atmosphereParams0 = glm::vec4(6360.0f, 60.0f, 0.5f, 8.0f);  // planetRadius, atmosphereHeight, eyeHeight, rayleighScaleHeight (km)
    // mieScaleHeight (km), mieG, sunIntensity, exposure. sunIntensity/exposure
    // are tuned together with sky.frag's ACESFilm tonemap — lower intensity
    // than before (was 20) since the tonemap now does the brightness
    // compression instead of relying on values clipping straight to white.
    sky.atmosphereParams1 = glm::vec4(1.2f, 0.76f, 10.0f, m_SkyExposure);

    // Cloud layer: bottom/top are km above the planet surface (well within
    // the 60km atmosphere shell above); forwardG/backG give the dual-lobe
    // Henyey-Greenstein silver-lining look; noiseUvScale controls how many
    // times the 128^3 noise volume tiles across the cloud layer.
    sky.cloudParams0 = glm::vec4(1.5f, 4.0f, m_CloudCoverage, m_CloudDensityMultiplier);
    sky.cloudParams1 = glm::vec4(m_CloudWindSpeed, glm::radians(m_CloudWindDirectionDegrees), 0.8f, -0.2f);
    sky.cloudParams2 = glm::vec4(1.0f, 0.2f, 0.02f, 0.0f); // powderStrength, ambientStrength, noiseUvScale

    sky.stepCounts = glm::ivec4(m_AtmospherePrimarySteps, m_AtmosphereSunSteps, m_CloudPrimarySteps, m_CloudSunShadowSteps);

    m_SkyUniformBuffers[frameIndex]->Upload(&sky, sizeof(sky));
}

void Application::RenderFrame() {
    VkDevice device = m_VulkanContext->GetDevice();
    VkFence inFlightFence = m_InFlightFences[m_CurrentFrame];

    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    std::uint32_t imageIndex = 0;
    const Swapchain::AcquireResult acquireResult =
        m_Swapchain->AcquireNextImage(m_ImageAvailableSemaphores[m_CurrentFrame], imageIndex);
    if (acquireResult != Swapchain::AcquireResult::Success) {
        // Minimized, or the swapchain was just rebuilt: nothing to draw this
        // iteration. The fence is intentionally left signaled (not reset) —
        // next iteration's wait on it returns immediately, harmlessly.
        return;
    }

    vkResetFences(device, 1, &inFlightFence); // only after a confirmed acquire, see header comment

    const VkExtent2D extent = m_Swapchain->GetExtent();
    const float time = static_cast<float>(glfwGetTime());
    UpdateUniformBuffer(m_CurrentFrame, extent);
    UpdateInstanceBuffer(m_CurrentFrame, time);
    UpdateSkyUniformBuffer(m_CurrentFrame, extent, time);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    BuildDebugOverlay();
    ImGui::Render();

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

    // Depth, like color, is transitioned UNDEFINED -> *_ATTACHMENT_OPTIMAL
    // unconditionally every frame: it's cleared (loadOp=CLEAR) and never
    // stored (storeOp=DONT_CARE) below, so its prior contents are always
    // discarded — UNDEFINED as oldLayout is the correct, spec-legal way to
    // say that, exactly like the color image's barrier above.
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
    // DONT_CARE, not CLEAR: the sky pass (drawn first below) is a fullscreen
    // triangle that unconditionally overwrites every pixel itself, making the
    // old hardcoded clear color dead code.
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_Swapchain->GetDepthImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // never sampled/presented after this frame
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    // Viewport/scissor are dynamic state shared by every pipeline bound this
    // pass (sky, then quads) — set once here rather than per-pipeline.
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Sky pass: fullscreen triangle, no vertex/index buffer, drawn first so
    // it paints the background every pixel that scene geometry doesn't cover
    // (depth test/write are both off in this pipeline — see SkyPipeline).
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

    // All instances share the single bound vertex/index buffer and
    // descriptor set (only view/proj/texture live there now); per-instance
    // model matrices come from the bound instance buffer (binding 1),
    // advancing automatically per gl_InstanceIndex — one real instanced draw.
    vkCmdDrawIndexed(cmd, 6, static_cast<std::uint32_t>(kInstanceCount), 0, 0, 0);

    // Shares the already-open rendering scope/attachments — no separate
    // begin/end needed for the debug overlay.
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

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

void Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForVulkan(m_Window->GetNativeHandle(), /*install_callbacks=*/false)) {
        throw std::runtime_error("Failed to initialize ImGui GLFW backend");
    }

    const VkFormat colorFormat = m_Swapchain->GetImageFormat();
    VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 1;
    pipelineRenderingInfo.pColorAttachmentFormats = &colorFormat;
    pipelineRenderingInfo.depthAttachmentFormat = m_Swapchain->GetDepthFormat();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = m_VulkanContext->GetInstance();
    initInfo.PhysicalDevice = m_VulkanContext->GetPhysicalDevice();
    initInfo.Device = m_VulkanContext->GetDevice();
    initInfo.QueueFamily = m_VulkanContext->GetGraphicsQueueFamily();
    initInfo.Queue = m_VulkanContext->GetGraphicsQueue();
    initInfo.DescriptorPoolSize = 16; // convenience: backend creates+owns its own small pool, comfortably above the documented 8/2 minimums
    initInfo.MinImageCount = m_Swapchain->GetImageCount();
    initInfo.ImageCount = m_Swapchain->GetImageCount();
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
    initInfo.CheckVkResultFn = CheckImGuiVulkanResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }
}

void Application::ShutdownImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::BuildDebugOverlay() {
    ImGui::Begin("Debug Overlay");
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
    const glm::vec3 pos = m_Camera.GetPosition();
    ImGui::Text("Camera position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    ImGui::SliderFloat("Move speed", &m_Camera.movementSpeed, 0.5f, 10.0f);
    ImGui::SliderFloat("Mouse sensitivity", &m_Camera.mouseSensitivity, 0.01f, 0.5f);
    ImGui::SliderFloat("Fog density", &m_FogDensity, 0.0f, 0.5f);
    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Sun elevation", &m_SunElevationDegrees, -20.0f, 90.0f);
        ImGui::SliderFloat("Sun azimuth", &m_SunAzimuthDegrees, 0.0f, 360.0f);
        ImGui::SliderFloat("Exposure", &m_SkyExposure, 0.2f, 3.0f);
        ImGui::SliderInt("Atmosphere steps", &m_AtmospherePrimarySteps, 4, 32);
        ImGui::SliderInt("Atmosphere sun steps", &m_AtmosphereSunSteps, 2, 16);
        ImGui::SliderFloat("Cloud coverage", &m_CloudCoverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Cloud density", &m_CloudDensityMultiplier, 0.0f, 3.0f);
        ImGui::SliderFloat("Cloud wind speed", &m_CloudWindSpeed, 0.0f, 0.2f);
        ImGui::SliderFloat("Cloud wind direction", &m_CloudWindDirectionDegrees, 0.0f, 360.0f);
        ImGui::SliderInt("Cloud steps", &m_CloudPrimarySteps, 16, 128);
        ImGui::SliderInt("Cloud shadow steps", &m_CloudSunShadowSteps, 2, 12);
    }
    ImGui::End();
}

} // namespace polyizon

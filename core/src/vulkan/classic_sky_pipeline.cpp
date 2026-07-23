#include "polyizon/vulkan/classic_sky_pipeline.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace polyizon {

namespace {

// Reuses SkyPipeline's vertex shader unchanged — the fullscreen-triangle
// trick has no mode-specific vertex logic, see sky.vert.
constexpr const char* kVertexShaderFileName = "sky.vert.spv";
constexpr const char* kFragmentShaderFileName = "classic_sky.frag.spv";

// Duplicated from every other pipeline class rather than shared — same
// reasoning each of them already gives for duplicating it from each other.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to resolve executable directory");
    }
    return std::filesystem::path(buffer).parent_path();
}

std::vector<char> ReadShaderFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

} // namespace

ClassicSkyPipeline::ClassicSkyPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat)
    : m_Device(device) {
    CreateDescriptorSetLayout();
    CreatePipelineLayout();
    CreatePipeline(colorAttachmentFormat, depthAttachmentFormat);
}

ClassicSkyPipeline::~ClassicSkyPipeline() {
    Destroy();
}

void ClassicSkyPipeline::CreateDescriptorSetLayout() {
    // Just the UBO — no cloud noise sampler, unlike SkyPipeline's binding 1.
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor set layout");
    }
}

void ClassicSkyPipeline::CreatePipelineLayout() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_DescriptorSetLayout;

    if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan pipeline layout");
    }
}

void ClassicSkyPipeline::CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat) {
    const std::filesystem::path shaderDir = GetExecutableDirectory() / "shaders";
    const std::vector<char> vertCode = ReadShaderFile(shaderDir / kVertexShaderFileName);
    const std::vector<char> fragCode = ReadShaderFile(shaderDir / kFragmentShaderFileName);

    VkShaderModule vertModule = CreateShaderModule(vertCode);
    VkShaderModule fragModule = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = { vertStage, fragStage };

    // Fullscreen-triangle trick: no vertex/instance buffer, same as SkyPipeline.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Depth test/write both off — same "background, never occludes/is
    // occluded" reasoning as SkyPipeline.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &colorAttachmentFormat;
    renderingCreateInfo.depthAttachmentFormat = depthAttachmentFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingCreateInfo;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_Layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    const VkResult result =
        vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline);

    vkDestroyShaderModule(m_Device, fragModule, nullptr);
    vkDestroyShaderModule(m_Device, vertModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan graphics pipeline");
    }
}

VkShaderModule ClassicSkyPipeline::CreateShaderModule(const std::vector<char>& spirv) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(spirv.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module");
    }
    return module;
}

void ClassicSkyPipeline::Destroy() {
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
    if (m_Layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
        m_Layout = VK_NULL_HANDLE;
    }
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
}

} // namespace polyizon

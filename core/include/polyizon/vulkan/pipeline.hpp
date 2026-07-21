#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Fixed graphics pipeline for the current multi-instance quad scene: one
// descriptor set binding a shared view/proj uniform buffer + texture
// sampler (see uniform_buffer_object.hpp), and two vertex input bindings —
// binding 0 is per-vertex Vertex data (see vertex.hpp), binding 1 is
// per-instance model-matrix data (4 vec4 attributes, one per mat4 column,
// reassembled in the shader) advancing once per instance
// (VK_VERTEX_INPUT_RATE_INSTANCE). All instances are drawn with a single
// vkCmdDrawIndexed call whose instanceCount covers them — no push constants
// involved. Built once for dynamic rendering (VkPipelineRenderingCreateInfo,
// no VkRenderPass/VkFramebuffer) and never recreated on resize:
// viewport/scissor are dynamic pipeline state, set per-frame in
// Application::RenderFrame() from the current swapchain extent — only the
// swapchain itself (and its depth buffer) reacts to resize.
class GraphicsPipeline {
public:
    GraphicsPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept { return m_DescriptorSetLayout; }

private:
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    // Non-owning: Application guarantees VulkanContext outlives this (see
    // member declaration order in application.hpp).
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

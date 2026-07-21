#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Fixed graphics pipeline for the current multi-instance quad scene: one
// descriptor set binding a shared view/proj uniform buffer + texture
// sampler (see uniform_buffer_object.hpp), plus a per-object model-matrix
// push constant so multiple instances can share this one pipeline/descriptor
// set while each still gets its own transform. Fixed Vertex layout (see
// vertex.hpp) bound as a single vertex buffer. Built once for dynamic
// rendering (VkPipelineRenderingCreateInfo, no VkRenderPass/VkFramebuffer)
// and never recreated on resize: viewport/scissor are dynamic pipeline
// state, set per-frame in Application::RenderFrame() from the current
// swapchain extent — only the swapchain itself (and its depth buffer)
// reacts to resize.
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

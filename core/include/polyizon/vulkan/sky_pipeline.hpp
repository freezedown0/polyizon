#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Fixed graphics pipeline for the background sky/cloud pass (see
// sky.vert/sky.frag): a single fullscreen triangle (no vertex buffer, no
// index buffer — positions come from gl_VertexIndex) that raymarches the
// atmosphere (and, once Stage 2 lands, volumetric clouds) into every pixel.
// One descriptor set: binding 0 is the fragment-only SkyUniformBufferObject
// (see sky_uniform_buffer_object.hpp).
//
// Drawn FIRST in Application::RenderFrame(), before the instanced quads, with
// depth test/write both disabled — it must never occlude or be occluded by
// scene geometry; it just paints the background every pixel touches anyway.
// Mirrors GraphicsPipeline's construction shape (own descriptor-set-layout /
// pipeline-layout / pipeline creation, dynamic rendering via
// VkPipelineRenderingCreateInfo, dynamic viewport/scissor) rather than
// sharing code with it — the vertex input state and depth/blend state differ
// enough that a shared base class would need more conditionals than the
// duplication it'd save.
class SkyPipeline {
public:
    SkyPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~SkyPipeline();

    SkyPipeline(const SkyPipeline&) = delete;
    SkyPipeline& operator=(const SkyPipeline&) = delete;
    SkyPipeline(SkyPipeline&&) = delete;
    SkyPipeline& operator=(SkyPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept { return m_DescriptorSetLayout; }

private:
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    // Non-owning: Application guarantees VulkanContext outlives this (same
    // precedent as GraphicsPipeline caching a bare VkDevice).
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

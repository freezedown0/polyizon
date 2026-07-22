#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Depth-only pipeline for the shadow pass (shadow.vert, no fragment shader
// at all — valid per the Vulkan spec for a pipeline with zero color
// attachments): renders each scene entity's Vertex3D positions from the
// shadow-casting light's point of view into a ShadowMap. No descriptor set —
// the light-space MVP matrix for the entity being drawn arrives via a single
// push constant (mat4, well under the guaranteed 128-byte minimum) instead,
// since this is a handful of draws per frame, not worth a UBO+descriptor set
// for one matrix.
//
// Mirrors GraphicsPipeline/SkyPipeline's construction shape (own pipeline-
// layout / pipeline creation, dynamic rendering via
// VkPipelineRenderingCreateInfo with depthAttachmentFormat only and
// colorAttachmentCount=0, dynamic viewport/scissor) rather than sharing code
// with them.
class ShadowPipeline {
public:
    explicit ShadowPipeline(VkDevice device, VkFormat depthAttachmentFormat);
    ~ShadowPipeline();

    ShadowPipeline(const ShadowPipeline&) = delete;
    ShadowPipeline& operator=(const ShadowPipeline&) = delete;
    ShadowPipeline(ShadowPipeline&&) = delete;
    ShadowPipeline& operator=(ShadowPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }

private:
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    // Non-owning: same precedent as GraphicsPipeline/SkyPipeline caching a
    // bare VkDevice instead of a VulkanContext&.
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

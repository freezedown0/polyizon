#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Voxel lighting mode's sky pipeline (classic_sky.frag): the same
// fullscreen-triangle trick as SkyPipeline, reusing sky.vert unchanged (no
// vertex-side logic differs between the two sky styles) paired with a much
// simpler fragment shader — one descriptor binding (ClassicSkyUniformBufferObject
// only; no cloud noise sampler, since Voxel mode has no clouds at all).
//
// Mirrors SkyPipeline's construction shape (own descriptor-set-layout /
// pipeline-layout / pipeline creation, dynamic rendering, dynamic viewport/
// scissor, depth test/write both off) — kept as a fully separate class
// rather than a runtime branch inside SkyPipeline/sky.frag, same "own
// everything" precedent as every other pipeline in this codebase.
class ClassicSkyPipeline {
public:
    ClassicSkyPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~ClassicSkyPipeline();

    ClassicSkyPipeline(const ClassicSkyPipeline&) = delete;
    ClassicSkyPipeline& operator=(const ClassicSkyPipeline&) = delete;
    ClassicSkyPipeline(ClassicSkyPipeline&&) = delete;
    ClassicSkyPipeline& operator=(ClassicSkyPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept { return m_DescriptorSetLayout; }

private:
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

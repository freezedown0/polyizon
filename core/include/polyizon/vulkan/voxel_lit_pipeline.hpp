#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Voxel lighting mode's lit-scene pipeline (voxel_lit.frag): reuses lit.vert
// and LitPushConstants/LitUniformBufferObject unchanged (see
// lit_pipeline.hpp) — the vertex-side logic and per-entity/per-frame data
// are identical between modes. Only binding 1 differs: a plain sampler2D
// over a low-resolution ShadowMap (ShadowSamplerMode::PlainNearest) instead
// of LitPipeline's sampler2DShadow, since voxel_lit.frag compares the
// quantized depth manually rather than relying on hardware PCF.
//
// Kept as a fully separate pipeline class (rather than a runtime branch
// inside LitPipeline/lit.frag) because the two binding-1 sampler types are
// genuinely different GLSL sampler kinds (sampler2DShadow vs sampler2D) —
// same "own everything" precedent as every other pipeline in this codebase.
class VoxelLitPipeline {
public:
    VoxelLitPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~VoxelLitPipeline();

    VoxelLitPipeline(const VoxelLitPipeline&) = delete;
    VoxelLitPipeline& operator=(const VoxelLitPipeline&) = delete;
    VoxelLitPipeline(VoxelLitPipeline&&) = delete;
    VoxelLitPipeline& operator=(VoxelLitPipeline&&) = delete;

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

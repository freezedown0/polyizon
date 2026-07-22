#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Push constants for each entity drawn by LitPipeline: the model matrix
// (vertex stage — transforms Vertex3D position/normal to world space and,
// combined with the UBO's lightSpaceMatrix, into shadow-map space) and a
// flat base color (fragment stage — this phase has no per-vertex color or
// textures, see mesh_vertex.hpp). One combined push-constant range/struct
// rather than two separate ones: simpler pipeline-layout setup for 80 bytes
// total, well under the guaranteed 128-byte minimum.
struct LitPushConstants {
    alignas(16) float model[16]; // glm::mat4, column-major — avoids a glm include in this header
    alignas(16) float baseColor[4];
};

// Main lit-scene pipeline (lit.vert/lit.frag): Lambertian (N.L) diffuse
// shading against the sun direction, modulated by a shadow-map visibility
// factor, plus a fixed ambient term. Descriptor set: binding 0 =
// LitUniformBufferObject (view/proj/light-space matrix/sun direction,
// shared across every entity drawn this frame), binding 1 = the ShadowMap's
// comparison sampler (sampler2DShadow). Vertex input = Vertex3D only — no
// per-instance buffer like GraphicsPipeline: this scene has a handful of
// distinct meshes/entities, not many identical instances, so each is drawn
// with its own model matrix/color via LitPushConstants instead.
//
// Mirrors GraphicsPipeline/SkyPipeline's construction shape (own
// descriptor-set-layout / pipeline-layout / pipeline creation, dynamic
// rendering, dynamic viewport/scissor, depth test+write on) rather than
// sharing code with them.
class LitPipeline {
public:
    LitPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~LitPipeline();

    LitPipeline(const LitPipeline&) = delete;
    LitPipeline& operator=(const LitPipeline&) = delete;
    LitPipeline(LitPipeline&&) = delete;
    LitPipeline& operator=(LitPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }
    VkDescriptorSetLayout GetDescriptorSetLayout() const noexcept { return m_DescriptorSetLayout; }

private:
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    // Non-owning: same precedent as GraphicsPipeline/SkyPipeline caching a
    // bare VkDevice instead of a VulkanContext&.
    VkDevice m_Device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

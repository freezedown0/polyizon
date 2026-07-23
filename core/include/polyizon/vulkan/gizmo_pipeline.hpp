#pragma once

#include <volk.h>

#include <vector>

namespace polyizon {

// Line-list pipeline for the editor's Move/Rotate viewport gizmos (Phase 20):
// draws colored line segments (arrow shafts/heads for Move, ring outlines for
// Rotate) directly on top of the scene, with depth testing disabled so the
// gizmo stays visible/interactable regardless of what geometry is behind it —
// exactly the standard behavior for a 3D editor's manipulator widget.
//
// Reuses Vertex3D's vertex input (position only, same as ShadowPipeline —
// the normal attribute is simply unused) rather than introducing a dedicated
// vertex format, since the gizmo's line geometry is built and uploaded via
// the same Buffer/Vertex3D machinery as everything else. A single push
// constant carries the entity-space-to-clip-space MVP plus a flat RGBA color
// per draw call (one draw call per axis/ring), so each axis can be tinted
// (red/green/blue) and highlighted (yellow) independently.
//
// Mirrors ShadowPipeline/SkyPipeline's construction shape (own pipeline
// layout/pipeline creation, dynamic rendering via
// VkPipelineRenderingCreateInfo, dynamic viewport/scissor) rather than
// sharing code with them.
class GizmoPipeline {
public:
    explicit GizmoPipeline(VkDevice device, VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    ~GizmoPipeline();

    GizmoPipeline(const GizmoPipeline&) = delete;
    GizmoPipeline& operator=(const GizmoPipeline&) = delete;
    GizmoPipeline(GizmoPipeline&&) = delete;
    GizmoPipeline& operator=(GizmoPipeline&&) = delete;

    VkPipeline GetPipeline() const noexcept { return m_Pipeline; }
    VkPipelineLayout GetLayout() const noexcept { return m_Layout; }

private:
    void CreatePipelineLayout();
    void CreatePipeline(VkFormat colorAttachmentFormat, VkFormat depthAttachmentFormat);
    VkShaderModule CreateShaderModule(const std::vector<char>& spirv) const;
    void Destroy();

    // Non-owning: same precedent as GraphicsPipeline/SkyPipeline/ShadowPipeline
    // caching a bare VkDevice instead of a VulkanContext&.
    VkDevice m_Device = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
};

} // namespace polyizon

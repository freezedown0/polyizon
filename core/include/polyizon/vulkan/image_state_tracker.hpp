#pragma once

#include <volk.h>

#include <unordered_map>

namespace polyizon {

struct TrackedImageState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;
};

// Records synchronization2 barriers from the last known use of each image.
// Imported images begin at UNDEFINED and become tracked after their first use.
class ImageStateTracker {
public:
    void Transition(VkCommandBuffer commandBuffer, VkImage image, VkImageAspectFlags aspectMask,
        TrackedImageState nextState, bool discardContents = false);
    void Forget(VkImage image);
    void Clear();

    [[nodiscard]] TrackedImageState GetState(VkImage image) const;

private:
    std::unordered_map<VkImage, TrackedImageState> m_States;
};

} // namespace polyizon

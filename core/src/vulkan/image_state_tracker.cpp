#include "polyizon/vulkan/image_state_tracker.hpp"

namespace polyizon {

void ImageStateTracker::Transition(VkCommandBuffer commandBuffer, VkImage image,
    VkImageAspectFlags aspectMask, TrackedImageState nextState, bool discardContents) {
    const TrackedImageState previous = discardContents ? TrackedImageState{} : GetState(image);
    if (!discardContents && previous.layout == nextState.layout &&
        previous.stageMask == nextState.stageMask && previous.accessMask == nextState.accessMask) {
        return;
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = previous.stageMask;
    barrier.srcAccessMask = previous.accessMask;
    barrier.dstStageMask = nextState.stageMask;
    barrier.dstAccessMask = nextState.accessMask;
    barrier.oldLayout = previous.layout;
    barrier.newLayout = nextState.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { aspectMask, 0, 1, 0, 1 };

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
    m_States[image] = nextState;
}

void ImageStateTracker::Forget(VkImage image) {
    m_States.erase(image);
}

void ImageStateTracker::Clear() {
    m_States.clear();
}

TrackedImageState ImageStateTracker::GetState(VkImage image) const {
    const auto found = m_States.find(image);
    return found == m_States.end() ? TrackedImageState{} : found->second;
}

} // namespace polyizon

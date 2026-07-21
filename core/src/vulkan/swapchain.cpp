#include "polyizon/vulkan/swapchain.hpp"

#include "polyizon/vulkan/context.hpp"
#include "polyizon/window.hpp"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace polyizon {

namespace {

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) {
    for (const auto& format : available) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return available[0];
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& available) {
    for (VkPresentModeKHR mode : available) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR; // spec-guaranteed to always be available
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t windowWidth, std::uint32_t windowHeight) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{ windowWidth, windowHeight };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

// Only pure-depth candidates (no stencil aspect) are considered: transitioning
// only the depth aspect of a combined depth+stencil image via
// VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL requires the separateDepthStencilLayouts
// feature, which isn't enabled anywhere in this project's device creation, and
// stencil is never used here anyway. The Vulkan spec guarantees at least one of
// these two supports VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, so this
// loop is guaranteed to succeed on any conformant implementation.
VkFormat ChooseDepthFormat(VkPhysicalDevice physicalDevice) {
    constexpr std::array<VkFormat, 2> candidates = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32 };
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    throw std::runtime_error("No supported depth format found");
}

} // namespace

Swapchain::Swapchain(VulkanContext& context, Window& window)
    : m_Context(context)
    , m_Window(window) {
    CreateSwapchain();
    CreateImageViews();
    m_DepthFormat = ChooseDepthFormat(m_Context.GetPhysicalDevice());
    CreateDepthResources();
    CreateRenderFinishedSemaphores();
}

Swapchain::~Swapchain() {
    Destroy();
}

void Swapchain::CreateSwapchain() {
    VkPhysicalDevice physicalDevice = m_Context.GetPhysicalDevice();
    VkSurfaceKHR surface = m_Context.GetSurface();

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = ChoosePresentMode(presentModes);
    const VkExtent2D extent = ChooseExtent(capabilities, m_Window.GetWidth(), m_Window.GetHeight());

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_Context.GetDevice(), &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }

    m_ImageFormat = surfaceFormat.format;
    m_Extent = extent;

    std::uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &actualImageCount, nullptr);
    m_Images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_Context.GetDevice(), m_Swapchain, &actualImageCount, m_Images.data());
}

void Swapchain::CreateImageViews() {
    m_ImageViews.resize(m_Images.size());

    for (std::size_t i = 0; i < m_Images.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_Images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_ImageFormat;
        createInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        createInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        if (vkCreateImageView(m_Context.GetDevice(), &createInfo, nullptr, &m_ImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }
}

void Swapchain::CreateDepthResources() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_DepthFormat;
    imageInfo.extent = { m_Extent.width, m_Extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO; // device-local, same as Image's texture path

    if (vmaCreateImage(m_Context.GetAllocator(), &imageInfo, &allocInfo,
            &m_DepthImage, &m_DepthImageAllocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan depth image");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_DepthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_DepthFormat;
    // Depth-only aspect even if the chosen format happened to carry a
    // stencil component — it never does here, since ChooseDepthFormat only
    // considers pure-depth candidates (see its comment).
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_Context.GetDevice(), &viewInfo, nullptr, &m_DepthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan depth image view");
    }
}

void Swapchain::CreateRenderFinishedSemaphores() {
    m_RenderFinishedSemaphores.resize(m_Images.size());

    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (auto& semaphore : m_RenderFinishedSemaphores) {
        if (vkCreateSemaphore(m_Context.GetDevice(), &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain render-finished semaphore");
        }
    }
}

Swapchain::AcquireResult Swapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore, std::uint32_t& outImageIndex) {
    if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0) {
        m_NeedsRecreate = true;
        return AcquireResult::NotReady; // never touch Vulkan with a zero extent
    }

    if (m_NeedsRecreate) {
        Recreate();
        return AcquireResult::Recreated;
    }

    const VkResult result = vkAcquireNextImageKHR(
        m_Context.GetDevice(), m_Swapchain, std::numeric_limits<std::uint64_t>::max(),
        imageAvailableSemaphore, VK_NULL_HANDLE, &outImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_NeedsRecreate = true;
        Recreate();
        return AcquireResult::Recreated;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        m_NeedsRecreate = true; // usable this frame; recreate on the next acquire
    }

    return AcquireResult::Success;
}

void Swapchain::Present(VkSemaphore waitSemaphore, std::uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult result = vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_NeedsRecreate = true;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }
}

void Swapchain::Recreate() {
    vkDeviceWaitIdle(m_Context.GetDevice());
    DestroySwapchainResources();

    if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0) {
        m_NeedsRecreate = true; // stay minimized; AcquireNextImage's guard keeps returning NotReady
        return;
    }

    CreateSwapchain();
    CreateImageViews();
    CreateDepthResources(); // m_DepthFormat already chosen; only extent changed
    CreateRenderFinishedSemaphores();
    m_NeedsRecreate = false;
}

void Swapchain::DestroySwapchainResources() {
    for (VkSemaphore semaphore : m_RenderFinishedSemaphores) {
        vkDestroySemaphore(m_Context.GetDevice(), semaphore, nullptr);
    }
    m_RenderFinishedSemaphores.clear();

    DestroyDepthResources();

    for (VkImageView view : m_ImageViews) {
        vkDestroyImageView(m_Context.GetDevice(), view, nullptr);
    }
    m_ImageViews.clear();
    m_Images.clear();

    if (m_Swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_Context.GetDevice(), m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::DestroyDepthResources() {
    if (m_DepthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_Context.GetDevice(), m_DepthImageView, nullptr);
        m_DepthImageView = VK_NULL_HANDLE;
    }
    if (m_DepthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Context.GetAllocator(), m_DepthImage, m_DepthImageAllocation);
        m_DepthImage = VK_NULL_HANDLE;
        m_DepthImageAllocation = VK_NULL_HANDLE;
    }
}

void Swapchain::Destroy() {
    DestroySwapchainResources();
}

} // namespace polyizon

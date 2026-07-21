#pragma once

#include <volk.h>

#include <cstdint>
#include <string>

// VmaAllocator is an opaque-pointer typedef (VK_DEFINE_HANDLE-style) that
// vk_mem_alloc.h declares in the global namespace; forward-declared here at
// matching global scope (same pattern as GLFWwindow above) so consumers of
// this header don't need to see vk_mem_alloc.h.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

namespace polyizon {

class Window;

// Owns the foundational Vulkan 1.3 objects: instance, (validation-only) debug
// messenger, surface, physical/logical device, graphics+present queue, and a
// VMA allocator. Scope stops here — no swapchain, no pipelines; those build
// on top of what this exposes in later phases.
class VulkanContext {
public:
    VulkanContext(Window& window, const std::string& appName);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    VkInstance GetInstance() const noexcept { return m_Instance; }
    VkPhysicalDevice GetPhysicalDevice() const noexcept { return m_PhysicalDevice; }
    VkDevice GetDevice() const noexcept { return m_Device; }
    VkSurfaceKHR GetSurface() const noexcept { return m_Surface; }
    VmaAllocator GetAllocator() const noexcept { return m_Allocator; }

    // Present queue is always the same as the graphics queue: physical
    // device selection only accepts devices with one queue family that
    // supports both (see SelectPhysicalDevice() in the .cpp for why that's
    // the right tradeoff at this stage).
    VkQueue GetGraphicsQueue() const noexcept { return m_GraphicsQueue; }
    std::uint32_t GetGraphicsQueueFamily() const noexcept { return m_QueueFamily; }

private:
    void CreateInstance(const std::string& appName);
    void SetupDebugMessenger();
    void CreateSurface(Window& window);
    void SelectPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    void Destroy();

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    std::uint32_t m_QueueFamily = 0;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;
};

} // namespace polyizon

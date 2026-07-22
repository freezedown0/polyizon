#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

// VmaAllocator is an opaque-pointer typedef (VK_DEFINE_HANDLE-style) that
// vk_mem_alloc.h declares in the global namespace; forward-declared here at
// matching global scope (same pattern as GLFWwindow above) so consumers of
// this header don't need to see vk_mem_alloc.h.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

// HWND/HINSTANCE are opaque-pointer typedefs in WinDef.h (`typedef struct
// HWND__* HWND;` etc) — forward-declared here at matching global scope
// (identical trick to VmaAllocator above) so this header doesn't need to
// pull in <Windows.h> just to name these two types in the Win32-surface
// constructor overload below. context.cpp includes the real <Windows.h>/
// <vulkan/vulkan_win32.h> to implement that overload.
struct HWND__;
using HWND = HWND__*;
struct HINSTANCE__;
using HINSTANCE = HINSTANCE__*;

namespace polyizon {

class Window;

// Owns the foundational Vulkan 1.3 objects: instance, (validation-only) debug
// messenger, surface, physical/logical device, graphics+present queue, and a
// VMA allocator. Scope stops here — no swapchain, no pipelines; those build
// on top of what this exposes in later phases.
class VulkanContext {
public:
    VulkanContext(Window& window, const std::string& appName);

    // Additive Win32 surface path: used by the Qt-hosted editor viewport,
    // which owns its own native window/event loop (not GLFW) — hwnd/hinstance
    // come from a QWindow's winId()/GetModuleHandle(nullptr). Everything past
    // instance/surface creation (physical device, logical device, allocator)
    // is shared with the GLFW constructor via InitializeCommon(); it only
    // ever touches m_Instance/m_Surface/m_PhysicalDevice, never Window/GLFW.
    VulkanContext(HWND hwnd, HINSTANCE hinstance, const std::string& appName);

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
    void CreateInstance(const std::string& appName, const std::vector<const char*>& extensions);
    void SetupDebugMessenger();
    void CreateSurface(Window& window);
    void CreateSurfaceWin32(HWND hwnd, HINSTANCE hinstance);
    void SelectPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    // Shared tail of both constructors — physical device selection through
    // VMA allocator creation, identical regardless of which windowing system
    // provided the surface.
    void InitializeCommon();
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

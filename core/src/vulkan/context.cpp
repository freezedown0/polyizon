// <Windows.h> must be included before volk.h/<vulkan/vulkan.h> in this TU:
// VK_USE_PLATFORM_WIN32_KHR (below) makes vulkan.h pull in
// <vulkan/vulkan_win32.h>, which assumes HWND/HINSTANCE/etc. are already
// declared by a prior <Windows.h> include — this is the Vulkan headers'
// own documented requirement for that platform macro, not a project
// convention. NOMINMAX avoids Windows.h's min/max macros shadowing
// std::min/std::max used elsewhere in this file.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// Enables the Win32 surface path (vkCreateWin32SurfaceKHR,
// VkWin32SurfaceCreateInfoKHR) used by CreateSurfaceWin32() below, for the
// Qt-hosted editor viewport. Scoped to this TU only (not a project-wide
// compile definition) so no other translation unit's view of <volk.h>
// changes.
#define VK_USE_PLATFORM_WIN32_KHR

// VOLK_IMPLEMENTATION must be defined before volk.h's *first* inclusion in
// this translation unit — context.hpp itself includes <volk.h> (for the
// public VkInstance/etc. types), so this #define has to come first here.
// This compiles volk directly from source into this TU instead of linking
// vcpkg's precompiled volk::volk static lib, which reliably crashed inside
// its own genload code in this environment (see core/CMakeLists.txt).
#define VOLK_IMPLEMENTATION
#include "polyizon/vulkan/context.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <GLFW/glfw3.h>

#include "polyizon/window.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace polyizon {

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr std::array<const char*, 1> kDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#ifdef POLYIZON_ENABLE_VALIDATION
VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[Vulkan] %s\n", callbackData->pMessage);
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugMessengerCallback;
    return createInfo;
}

bool CheckValidationLayerSupport() {
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, kValidationLayerName) == 0;
    });
}
#endif

std::vector<const char*> GetRequiredInstanceExtensionsGlfw() {
    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifdef POLYIZON_ENABLE_VALIDATION
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    return extensions;
}

// Static list, not queried from any windowing library: these are exactly
// what glfwGetRequiredInstanceExtensions() also returns on Windows (surface
// + the Win32-specific surface extension) — not actually GLFW-derived
// information, just always-true facts about presenting to a Win32 HWND.
std::vector<const char*> GetRequiredInstanceExtensionsWin32() {
    std::vector<const char*> extensions = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

#ifdef POLYIZON_ENABLE_VALIDATION
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    return extensions;
}

bool DeviceSupportsExtensions(VkPhysicalDevice device) {
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    return std::all_of(kDeviceExtensions.begin(), kDeviceExtensions.end(), [&available](const char* required) {
        return std::any_of(available.begin(), available.end(), [required](const VkExtensionProperties& ext) {
            return std::strcmp(ext.extensionName, required) == 0;
        });
    });
}

bool DeviceSupportsVulkan13Features(VkPhysicalDevice device) {
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    return features13.dynamicRendering == VK_TRUE && features13.synchronization2 == VK_TRUE;
}

// Only physical devices exposing a single queue family that supports both
// graphics and presentation are considered — see the header comment on
// GetGraphicsQueueFamily() for why split families are out of scope here.
bool FindCombinedGraphicsPresentQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface, std::uint32_t& outFamily) {
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    for (std::uint32_t i = 0; i < familyCount; ++i) {
        if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport == VK_TRUE) {
            outFamily = i;
            return true;
        }
    }
    return false;
}

int ScoreDeviceType(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 100;
        default:
            return 0;
    }
}

struct PhysicalDeviceCandidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    int score = 0;
};

} // namespace

VulkanContext::VulkanContext(Window& window, const std::string& appName) {
    // Source vkGetInstanceProcAddr from GLFW (whose loader path is already
    // proven to work via window/surface creation) rather than volk's own
    // volkInitialize(), which does its own internal LoadLibrary+genload.
    auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) {
        throw std::runtime_error("Failed to retrieve vkGetInstanceProcAddr via GLFW");
    }
    volkInitializeCustom(getInstanceProcAddr);

    CreateInstance(appName, GetRequiredInstanceExtensionsGlfw());
    SetupDebugMessenger();
    CreateSurface(window);
    InitializeCommon();
}

VulkanContext::VulkanContext(HWND hwnd, HINSTANCE hinstance, const std::string& appName) {
    // No GLFW involved on this path at all: volk's own bootstrap (which the
    // GLFW constructor avoids specifically because vcpkg's precompiled
    // volk::volk crashed in its genload code — see core/CMakeLists.txt) is
    // fine here because this project never links that precompiled lib;
    // volkInitialize() just does the same LoadLibrary+GetProcAddress dance
    // against vulkan-1.dll using the volk source compiled directly into
    // this TU (VOLK_IMPLEMENTATION above), which has never been the
    // problematic path.
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize volk");
    }

    CreateInstance(appName, GetRequiredInstanceExtensionsWin32());
    SetupDebugMessenger();
    CreateSurfaceWin32(hwnd, hinstance);
    InitializeCommon();
}

VulkanContext::~VulkanContext() {
    Destroy();
}

void VulkanContext::CreateInstance(const std::string& appName, const std::vector<const char*>& extensions) {
#ifdef POLYIZON_ENABLE_VALIDATION
    if (!CheckValidationLayerSupport()) {
        throw std::runtime_error(
            "POLYIZON_ENABLE_VALIDATION is set but VK_LAYER_KHRONOS_validation is not available "
            "(install the Vulkan SDK, which provides validation layers)");
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Polyizon";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef POLYIZON_ENABLE_VALIDATION
    const std::array<const char*, 1> layers = { kValidationLayerName };
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    // Chained so vkCreateInstance/vkDestroyInstance themselves are validated;
    // the permanent messenger (SetupDebugMessenger) only covers everything
    // created after the instance exists.
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = MakeDebugMessengerCreateInfo();
    createInfo.pNext = &debugCreateInfo;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    volkLoadInstance(m_Instance);
}

void VulkanContext::SetupDebugMessenger() {
#ifdef POLYIZON_ENABLE_VALIDATION
    VkDebugUtilsMessengerCreateInfoEXT createInfo = MakeDebugMessengerCreateInfo();
    if (vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan debug messenger");
    }
#endif
}

void VulkanContext::CreateSurface(Window& window) {
    if (glfwCreateWindowSurface(m_Instance, window.GetNativeHandle(), nullptr, &m_Surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
}

void VulkanContext::CreateSurfaceWin32(HWND hwnd, HINSTANCE hinstance) {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = hinstance;
    createInfo.hwnd = hwnd;

    if (vkCreateWin32SurfaceKHR(m_Instance, &createInfo, nullptr, &m_Surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Win32 surface");
    }
}

void VulkanContext::InitializeCommon() {
    SelectPhysicalDevice();
    CreateLogicalDevice();
    CreateAllocator();
}

void VulkanContext::SelectPhysicalDevice() {
    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan-capable physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    PhysicalDeviceCandidate best;

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }
        if (!DeviceSupportsExtensions(device)) {
            continue;
        }
        if (!DeviceSupportsVulkan13Features(device)) {
            continue;
        }

        std::uint32_t queueFamily = 0;
        if (!FindCombinedGraphicsPresentQueueFamily(device, m_Surface, queueFamily)) {
            continue;
        }

        const int score = ScoreDeviceType(properties.deviceType);
        if (best.device == VK_NULL_HANDLE || score > best.score) {
            best = PhysicalDeviceCandidate{ device, queueFamily, score };
        }
    }

    if (best.device == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "No suitable Vulkan 1.3 physical device found (needs VK_KHR_swapchain, dynamicRendering, "
            "synchronization2, and a queue family supporting both graphics and present)");
    }

    m_PhysicalDevice = best.device;
    m_QueueFamily = best.queueFamily;
}

void VulkanContext::CreateLogicalDevice() {
    constexpr float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_QueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    createInfo.pEnabledFeatures = nullptr; // features come via the pNext chain above

    if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device");
    }

    volkLoadDevice(m_Device);

    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_GraphicsQueue);
}

void VulkanContext::CreateAllocator() {
    VmaVulkanFunctions vulkanFunctions{};

    VmaAllocatorCreateInfo createInfo{};
    createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    createInfo.physicalDevice = m_PhysicalDevice;
    createInfo.device = m_Device;
    createInfo.instance = m_Instance;

    vmaImportVulkanFunctionsFromVolk(&createInfo, &vulkanFunctions);
    createInfo.pVulkanFunctions = &vulkanFunctions;

    if (vmaCreateAllocator(&createInfo, &m_Allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

void VulkanContext::Destroy() {
    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }
#ifdef POLYIZON_ENABLE_VALIDATION
    if (m_DebugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        m_DebugMessenger = VK_NULL_HANDLE;
    }
#endif
    if (m_Surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }
    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

} // namespace polyizon

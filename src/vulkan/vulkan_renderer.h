#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

// Forward declarations
struct GLFWwindow;

namespace pong {

struct VulkanInitInfo {
    const char* appName = "Pong";
    uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0);
    bool enableValidationLayers = false;
    uint32_t windowWidth = 800;
    uint32_t windowHeight = 600;
#ifdef _WIN32
    HWND hwnd = nullptr;
    HINSTANCE hinstance = nullptr;
#elif defined(__linux__)
    Display* display = nullptr;
    Window window = 0;
#endif
};

class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    bool initialize(const VulkanInitInfo& initInfo);
    void shutdown();
    
    bool beginFrame();
    void endFrame();
    
    // Basic drawing operations
    void clear(float r, float g, float b, float a = 1.0f);
    void drawRect(float x, float y, float width, float height, float r, float g, float b, float a = 1.0f);
    void drawText(const std::string& text, float x, float y, float r, float g, float b, float a = 1.0f);
    
    // Get current framebuffer dimensions
    uint32_t getFramebufferWidth() const { return m_swapchainExtent.width; }
    uint32_t getFramebufferHeight() const { return m_swapchainExtent.height; }
    
    bool isInitialized() const { return m_initialized; }

private:
    bool createInstance(const VulkanInitInfo& initInfo);
    bool setupDebugMessenger();
    bool createSurface(const VulkanInitInfo& initInfo);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createGraphicsPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    
    void cleanup();
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions(bool enableValidationLayers);
    
    // Vulkan objects
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    
    // Queue family indices
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        bool isComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };
    
    // Swapchain support details
    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
    
    uint32_t m_currentFrame = 0;
    const int MAX_FRAMES_IN_FLIGHT = 2;
    
    bool m_initialized = false;
    bool m_validationLayersEnabled = false;
    
    static const std::vector<const char*> s_validationLayers;
    static const std::vector<const char*> s_deviceExtensions;
};

} // namespace pong
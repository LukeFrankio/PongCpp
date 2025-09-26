/**
 * @file vulkan_context.h
 * @brief Vulkan context initialization and management
 * 
 * This file provides a complete Vulkan context setup including instance creation,
 * device selection, surface creation, and validation layer management.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

/**
 * @brief Queue family indices for Vulkan operations
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/**
 * @brief Swapchain support details
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

/**
 * @brief Vulkan context management class
 * 
 * Handles all Vulkan initialization including:
 * - Instance creation with validation layers (debug builds)
 * - Physical device selection
 * - Logical device creation
 * - Surface creation (Win32 specific)
 * - Queue management
 * - Extension and validation layer handling
 */
class VulkanContext {
public:
    /**
     * @brief Initialize Vulkan context
     * @param hwnd Window handle (Win32)
     * @param hinstance Application instance (Win32)
     * @param enableValidation Enable validation layers
     * @return true if successful
     */
    bool initialize(void* hwnd, void* hinstance, bool enableValidation = false);
    
    /**
     * @brief Cleanup and shutdown Vulkan context
     */
    void cleanup();
    
    /**
     * @brief Check if context is initialized
     * @return true if ready for use
     */
    bool isInitialized() const { return m_device != VK_NULL_HANDLE; }
    
    // Getters
    VkInstance getInstance() const { return m_instance; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getDevice() const { return m_device; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    
    /**
     * @brief Get queue family indices
     * @return Queue family indices structure
     */
    QueueFamilyIndices getQueueFamilies() const { return m_queueFamilyIndices; }
    
    /**
     * @brief Query swapchain support for current device and surface
     * @return Swapchain support details
     */
    SwapChainSupportDetails querySwapChainSupport() const;
    
    /**
     * @brief Find memory type index for allocation
     * @param typeFilter Memory type requirements
     * @param properties Desired memory properties
     * @return Memory type index, or UINT32_MAX if not found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    
    /**
     * @brief Get last error message
     * @return Error string from last operation
     */
    const std::string& getLastError() const { return m_lastError; }
    
private:
    // Vulkan objects
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    
    // Debug and validation
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    bool m_validationEnabled = false;
    
    // Queue families
    QueueFamilyIndices m_queueFamilyIndices;
    
    // Error handling
    std::string m_lastError;
    
    // Helper methods
    SwapChainSupportDetails querySwapChainSupportForDevice(VkPhysicalDevice device) const;
    
    // Required extensions and validation layers
    std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    
    /**
     * @brief Create Vulkan instance
     * @return true if successful
     */
    bool createInstance();
    
    /**
     * @brief Setup debug messenger for validation layers
     * @return true if successful
     */
    bool setupDebugMessenger();
    
    /**
     * @brief Create window surface
     * @param hwnd Window handle
     * @param hinstance Application instance
     * @return true if successful
     */
    bool createSurface(void* hwnd, void* hinstance);
    
    /**
     * @brief Select physical device
     * @return true if suitable device found
     */
    bool pickPhysicalDevice();
    
    /**
     * @brief Create logical device
     * @return true if successful
     */
    bool createLogicalDevice();
    
    /**
     * @brief Check if device is suitable for our needs
     * @param device Physical device to check
     * @return true if suitable
     */
    bool isDeviceSuitable(VkPhysicalDevice device) const;
    
    /**
     * @brief Find queue families for device
     * @param device Physical device to query
     * @return Queue family indices
     */
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    
    /**
     * @brief Check if device supports required extensions
     * @param device Physical device to check
     * @return true if all extensions supported
     */
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    
    /**
     * @brief Check validation layer support
     * @return true if all layers available
     */
    bool checkValidationLayerSupport() const;
    
    /**
     * @brief Get required instance extensions
     * @return Vector of extension names
     */
    std::vector<const char*> getRequiredExtensions() const;
    
    /**
     * @brief Set error message
     * @param error Error string to store
     */
    void setError(const std::string& error) { m_lastError = error; }
    
    /**
     * @brief Debug messenger callback
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );
};

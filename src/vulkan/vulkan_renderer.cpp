#include "vulkan_renderer.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstring>

namespace pong {

const std::vector<const char*> VulkanRenderer::s_validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> VulkanRenderer::s_deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

VulkanRenderer::VulkanRenderer() {
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

bool VulkanRenderer::initialize(const VulkanInitInfo& initInfo) {
    if (m_initialized) {
        return true;
    }
    
    m_validationLayersEnabled = initInfo.enableValidationLayers;
    
    if (!createInstance(initInfo)) {
        std::cerr << "Failed to create Vulkan instance" << std::endl;
        return false;
    }
    
    if (m_validationLayersEnabled && !setupDebugMessenger()) {
        std::cerr << "Failed to setup debug messenger" << std::endl;
        return false;
    }
    
    if (!createSurface(initInfo)) {
        std::cerr << "Failed to create surface" << std::endl;
        return false;
    }
    
    if (!pickPhysicalDevice()) {
        std::cerr << "Failed to find suitable GPU" << std::endl;
        return false;
    }
    
    if (!createLogicalDevice()) {
        std::cerr << "Failed to create logical device" << std::endl;
        return false;
    }
    
    if (!createSwapchain()) {
        std::cerr << "Failed to create swapchain" << std::endl;
        return false;
    }
    
    if (!createImageViews()) {
        std::cerr << "Failed to create image views" << std::endl;
        return false;
    }
    
    if (!createRenderPass()) {
        std::cerr << "Failed to create render pass" << std::endl;
        return false;
    }
    
    if (!createGraphicsPipeline()) {
        std::cerr << "Failed to create graphics pipeline" << std::endl;
        return false;
    }
    
    if (!createFramebuffers()) {
        std::cerr << "Failed to create framebuffers" << std::endl;
        return false;
    }
    
    if (!createCommandPool()) {
        std::cerr << "Failed to create command pool" << std::endl;
        return false;
    }
    
    if (!createCommandBuffers()) {
        std::cerr << "Failed to create command buffers" << std::endl;
        return false;
    }
    
    if (!createSyncObjects()) {
        std::cerr << "Failed to create sync objects" << std::endl;
        return false;
    }
    
    m_initialized = true;
    return true;
}

void VulkanRenderer::shutdown() {
    cleanup();
}

bool VulkanRenderer::beginFrame() {
    if (!m_initialized) return false;
    
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                           m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain needs to be recreated
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }
    
    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    if (vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    return true;
}

void VulkanRenderer::endFrame() {
    if (!m_initialized) return;
    
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    
    if (vkEndCommandBuffer(m_commandBuffers[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];
    
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }
    
    uint32_t imageIndex = 0; // This should be stored from beginFrame
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    
    vkQueuePresentKHR(m_presentQueue, &presentInfo);
    
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::clear(float r, float g, float b, float a) {
    // Clear is handled in beginFrame for now
    // TODO: Implement runtime clear color changes
}

void VulkanRenderer::drawRect(float x, float y, float width, float height, float r, float g, float b, float a) {
    // TODO: Implement rectangle drawing
    // This will require vertex buffers and proper pipeline setup
}

void VulkanRenderer::drawText(const std::string& text, float x, float y, float r, float g, float b, float a) {
    // TODO: Implement text rendering
    // This will require font atlas and text rendering pipeline
}

bool VulkanRenderer::createInstance(const VulkanInitInfo& initInfo) {
    if (m_validationLayersEnabled && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }
    
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = initInfo.appName;
    appInfo.applicationVersion = initInfo.appVersion;
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    auto extensions = getRequiredExtensions(m_validationLayersEnabled);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_validationLayersEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(s_validationLayers.size());
        createInfo.ppEnabledLayerNames = s_validationLayers.data();
        
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    
    return vkCreateInstance(&createInfo, nullptr, &m_instance) == VK_SUCCESS;
}

bool VulkanRenderer::setupDebugMessenger() {
    // TODO: Implement debug messenger setup
    return true;
}

bool VulkanRenderer::createSurface(const VulkanInitInfo& initInfo) {
#ifdef _WIN32
    if (initInfo.hwnd && initInfo.hinstance) {
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = initInfo.hwnd;
        createInfo.hinstance = initInfo.hinstance;
        
        return vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface) == VK_SUCCESS;
    }
#elif defined(__linux__)
    if (initInfo.display && initInfo.window) {
        VkXlibSurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.dpy = initInfo.display;
        createInfo.window = initInfo.window;
        
        return vkCreateXlibSurfaceKHR(m_instance, &createInfo, nullptr, &m_surface) == VK_SUCCESS;
    }
#endif
    return false;
}

bool VulkanRenderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    for (const auto& device : devices) {
        QueueFamilyIndices indices = findQueueFamilies(device);
        if (indices.isComplete()) {
            m_physicalDevice = device;
            break;
        }
    }
    
    return m_physicalDevice != VK_NULL_HANDLE;
}

bool VulkanRenderer::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(s_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();
    
    if (m_validationLayersEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(s_validationLayers.size());
        createInfo.ppEnabledLayerNames = s_validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }
    
    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);
    
    return true;
}

bool VulkanRenderer::createSwapchain() {
    // TODO: Implement swapchain creation
    return true;
}

bool VulkanRenderer::createImageViews() {
    // TODO: Implement image views creation
    return true;
}

bool VulkanRenderer::createRenderPass() {
    // TODO: Implement render pass creation
    return true;
}

bool VulkanRenderer::createGraphicsPipeline() {
    // TODO: Implement graphics pipeline creation
    return true;
}

bool VulkanRenderer::createFramebuffers() {
    // TODO: Implement framebuffers creation
    return true;
}

bool VulkanRenderer::createCommandPool() {
    // TODO: Implement command pool creation
    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    // TODO: Implement command buffers creation
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    // TODO: Implement sync objects creation
    return true;
}

void VulkanRenderer::cleanup() {
    if (m_device) {
        vkDeviceWaitIdle(m_device);
        
        // Cleanup sync objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (m_renderFinishedSemaphores.size() > i && m_renderFinishedSemaphores[i]) {
                vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            }
            if (m_imageAvailableSemaphores.size() > i && m_imageAvailableSemaphores[i]) {
                vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            }
            if (m_inFlightFences.size() > i && m_inFlightFences[i]) {
                vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
            }
        }
        
        if (m_commandPool) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        }
        
        for (auto framebuffer : m_swapchainFramebuffers) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        
        if (m_graphicsPipeline) {
            vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        }
        
        if (m_pipelineLayout) {
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        }
        
        if (m_renderPass) {
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        }
        
        for (auto imageView : m_swapchainImageViews) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
        
        if (m_swapchain) {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        }
        
        vkDestroyDevice(m_device, nullptr);
    }
    
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    
    if (m_debugMessenger) {
        // TODO: Destroy debug messenger
    }
    
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
    }
    
    m_initialized = false;
}

bool VulkanRenderer::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    
    for (const char* layerName : s_validationLayers) {
        bool layerFound = false;
        
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        
        if (!layerFound) {
            return false;
        }
    }
    
    return true;
}

std::vector<const char*> VulkanRenderer::getRequiredExtensions(bool enableValidationLayers) {
    std::vector<const char*> extensions;
    
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
    
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    return extensions;
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        
        VkBool32 presentSupport = false;
        if (m_surface) {
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        }
        
        if (presentSupport) {
            indices.presentFamily = i;
        }
        
        if (indices.isComplete()) {
            break;
        }
        
        i++;
    }
    
    return indices;
}

VulkanRenderer::SwapchainSupportDetails VulkanRenderer::querySwapchainSupport(VkPhysicalDevice device) {
    SwapchainSupportDetails details;
    
    if (m_surface) {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);
        
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
        
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
        }
        
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
        
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
        }
    }
    
    return details;
}

} // namespace pong
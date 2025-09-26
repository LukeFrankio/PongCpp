/**
 * @file vulkan_renderer.cpp
 * @brief Implementation of Vulkan renderer with swapchain management
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "vulkan_renderer.h"
#include "vulkan_debug.h"

// Note: do not attempt to redefine 'std::cout' via macro (invalid token '::').
// Include the debug header so files can use VULKAN_DBG directly where desired.

#include <algorithm>
#include <iostream>
#include <array>

bool VulkanRenderer::initialize(
    VulkanContext* context,
    VulkanMemoryManager* memoryManager,
    SlangCompiler* compiler,
    uint32_t initialWidth,
    uint32_t initialHeight) {
    
    std::cout << "[DEBUG] VulkanRenderer::initialize() starting..." << std::endl;
    std::cout << "[DEBUG] Parameters: size=" << initialWidth << "x" << initialHeight << std::endl;
    
    if (!context || !context->isInitialized()) {
        std::cerr << "[ERROR] Invalid Vulkan context" << std::endl;
        setError("Invalid Vulkan context");
        return false;
    }
    std::cout << "[DEBUG] Vulkan context validation passed." << std::endl;
    
    if (!memoryManager) {
        std::cerr << "[ERROR] Invalid memory manager" << std::endl;
        setError("Invalid memory manager");
        return false;
    }
    std::cout << "[DEBUG] Memory manager validation passed." << std::endl;
    
    if (!compiler || !compiler->isInitialized()) {
        std::cerr << "[ERROR] Invalid or uninitialized Slang compiler" << std::endl;
        setError("Invalid Slang compiler");
        return false;
    }
    std::cout << "[DEBUG] Slang compiler validation passed." << std::endl;
    
    std::cout << "[DEBUG] Assigning member variables..." << std::endl;
    m_context = context;
    m_memoryManager = memoryManager;
    m_compiler = compiler;
    m_device = context->getDevice();
    std::cout << "[DEBUG] Member variables assigned." << std::endl;
    
    // Set initial swapchain configuration
    std::cout << "[DEBUG] Setting swapchain configuration..." << std::endl;
    m_swapchainConfig.extent = {initialWidth, initialHeight};
    std::cout << "[DEBUG] Swapchain configuration set." << std::endl;
    
    // Create swapchain
    std::cout << "[DEBUG] Creating swapchain..." << std::endl;
    if (!createSwapchain()) {
        std::cerr << "[ERROR] Failed to create swapchain" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Swapchain created successfully." << std::endl;
    
    // Create image views
    if (!createImageViews()) {
        return false;
    }
    
    // Create render pass
    std::cout << "[DEBUG] Creating render pass..." << std::endl;
    std::cout.flush();
    if (!createRenderPass()) {
        std::cerr << "[ERROR] Failed to create render pass" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Render pass created successfully." << std::endl;
    
    // Create framebuffers
    std::cout << "[DEBUG] Creating framebuffers..." << std::endl;
    std::cout.flush();
    if (!createFramebuffers()) {
        std::cerr << "[ERROR] Failed to create framebuffers" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Framebuffers created successfully." << std::endl;
    
    // Create command objects
    std::cout << "[DEBUG] Creating command objects..." << std::endl;
    std::cout.flush();
    if (!createCommandObjects()) {
        std::cerr << "[ERROR] Failed to create command objects" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Command objects created successfully." << std::endl;
    
    // Create synchronization objects
    std::cout << "[DEBUG] Creating synchronization objects..." << std::endl;
    std::cout.flush();
    if (!createSyncObjects()) {
        std::cerr << "[ERROR] Failed to create synchronization objects" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Synchronization objects created successfully." << std::endl;
    
    // Create graphics pipelines
    std::cout << "[DEBUG] Creating graphics pipelines..." << std::endl;
    std::cout.flush();
    if (!createGraphicsPipelines()) {
        std::cerr << "[ERROR] Failed to create graphics pipelines" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Graphics pipelines created successfully." << std::endl;
    
    // Create vertex buffers
    std::cout << "[DEBUG] Creating vertex buffers..." << std::endl;
    std::cout.flush();
    if (!createVertexBuffers()) {
        std::cerr << "[ERROR] Failed to create vertex buffers" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Vertex buffers created successfully." << std::endl;
    
    // Create uniform buffers
    std::cout << "[DEBUG] Creating uniform buffers..." << std::endl;
    std::cout.flush();
    if (!createUniformBuffers()) {
        std::cerr << "[ERROR] Failed to create uniform buffers" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Uniform buffers created successfully." << std::endl;
    
    // Create descriptor sets
    std::cout << "[DEBUG] Creating descriptor sets..." << std::endl;
    std::cout.flush();
    if (!createDescriptorSets()) {
        std::cerr << "[ERROR] Failed to create descriptor sets" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Descriptor sets created successfully." << std::endl;
    
    // Initialize game coordinates
    std::cout << "[DEBUG] Initializing game coordinates..." << std::endl;
    std::cout.flush();
    setGameCoordinates(m_gameWidth, m_gameHeight);
    std::cout << "[DEBUG] Game coordinates initialized successfully." << std::endl;
    
    std::cout << "[DEBUG] VulkanRenderer::initialize() completed successfully!" << std::endl;
    return true;
}

void VulkanRenderer::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    
    // Wait for device to finish
    vkDeviceWaitIdle(m_device);
    
    // Cleanup synchronization objects
    for (auto& frame : m_frames) {
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
        }
        if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.renderFinishedSemaphore, nullptr);
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
        }
    }
    m_frames.clear();
    
    // Cleanup command pool
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    
    // Cleanup descriptor pool
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    
    // Cleanup uniform buffers
    for (auto& buffer : m_globalUniformBuffers) {
        m_memoryManager->destroyBuffer(buffer);
    }
    m_globalUniformBuffers.clear();
    
    for (auto& buffer : m_objectUniformBuffers) {
        m_memoryManager->destroyBuffer(buffer);
    }
    m_objectUniformBuffers.clear();
    
    // Cleanup vertex buffers
    if (m_vertexBuffer.isValid()) {
        m_memoryManager->destroyBuffer(m_vertexBuffer);
    }
    if (m_indexBuffer.isValid()) {
        m_memoryManager->destroyBuffer(m_indexBuffer);
    }
    
    // Cleanup pipelines
    cleanupPipelines();
    
    // Cleanup swapchain
    cleanupSwapchain();
    
    // Cleanup render pass
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    
    // Reset state
    m_device = VK_NULL_HANDLE;
    m_context = nullptr;
    m_memoryManager = nullptr;
    m_compiler = nullptr;
}

bool VulkanRenderer::resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == 0 || newHeight == 0) {
        return true; // Minimized, don't resize
    }
    
    m_swapchainConfig.extent = {newWidth, newHeight};
    m_framebufferResized = true;
    
    return recreateSwapchain();
}

bool VulkanRenderer::beginFrame() {
    if (!isInitialized()) {
        setError("Renderer not initialized");
        return false;
    }
    
    FrameData& frame = m_frames[m_currentFrame];
    
    // Wait for previous frame to finish
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    
    // Acquire next image
    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        frame.imageAvailableSemaphore, VK_NULL_HANDLE, &m_imageIndex
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return recreateSwapchain();
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        setError("Failed to acquire swapchain image: " + std::to_string(result));
        return false;
    }
    
    // Reset fence
    vkResetFences(m_device, 1, &frame.inFlightFence);
    
    // Reset command buffer
    vkResetCommandBuffer(frame.commandBuffer, 0);
    
    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    result = vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        setError("Failed to begin command buffer: " + std::to_string(result));
        return false;
    }
    
    // Update uniform buffers
    updateUniformBuffers();
    
    // Begin render pass
    beginRenderPass();
    
    return true;
}

bool VulkanRenderer::endFrame() {
    if (!isInitialized()) {
        setError("Renderer not initialized");
        return false;
    }
    
    FrameData& frame = m_frames[m_currentFrame];
    
    // Flush any pending batches
    flushBatches();
    
    // End render pass
    endRenderPass();
    
    // End command buffer
    VkResult result = vkEndCommandBuffer(frame.commandBuffer);
    if (result != VK_SUCCESS) {
        setError("Failed to end command buffer: " + std::to_string(result));
        return false;
    }
    
    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    
    VkSemaphore signalSemaphores[] = {frame.renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    std::cout << "[DEBUG] Submitting command buffer to graphics queue..." << std::endl;
    result = vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, frame.inFlightFence);
    if (result != VK_SUCCESS) {
        std::cout << "[DEBUG] ERROR: vkQueueSubmit failed with result: " << result << std::endl;
        setError("Failed to submit command buffer: " + std::to_string(result));
        return false;
    }
    std::cout << "[DEBUG] Command buffer submitted successfully." << std::endl;
    
    // Present frame
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    
    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_imageIndex;
    
    std::cout << "[DEBUG] Presenting frame to swapchain..." << std::endl;
    result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
    std::cout << "[DEBUG] Present result: " << result << std::endl;
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        setError("Failed to present frame: " + std::to_string(result));
        return false;
    }
    
    // Advance to next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    
    return true;
}

VkCommandBuffer VulkanRenderer::getCurrentCommandBuffer() const {
    if (m_currentFrame < m_frames.size()) {
        return m_frames[m_currentFrame].commandBuffer;
    }
    return VK_NULL_HANDLE;
}

void VulkanRenderer::setVSync(bool enable) {
    if (m_swapchainConfig.vsync != enable) {
        m_swapchainConfig.vsync = enable;
        recreateSwapchain();
    }
}

bool VulkanRenderer::createSwapchain() {
    SwapChainSupportDetails swapChainSupport = m_context->querySwapChainSupport();
    
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
    
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && 
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_context->getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    QueueFamilyIndices indices = m_context->getQueueFamilies();
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };
    
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain);
    if (result != VK_SUCCESS) {
        setError("Failed to create swapchain: " + std::to_string(result));
        return false;
    }
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, images.data());
    
    m_swapchainImages.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        m_swapchainImages[i].image = images[i];
    }
    
    // Store configuration
    m_swapchainConfig.extent = extent;
    m_swapchainConfig.surfaceFormat = surfaceFormat;
    m_swapchainConfig.presentMode = presentMode;
    m_swapchainConfig.imageCount = imageCount;
    
    return true;
}

bool VulkanRenderer::recreateSwapchain() {
    vkDeviceWaitIdle(m_device);
    
    cleanupSwapchain();
    
    if (!createSwapchain() ||
        !createImageViews() ||
        !createFramebuffers()) {
        return false;
    }
    
    // Recreate pipelines (viewport dependent)
    cleanupPipelines();
    if (!createGraphicsPipelines()) {
        return false;
    }
    
    return true;
}

void VulkanRenderer::cleanupSwapchain() {
    // Cleanup framebuffers
    for (auto& swapchainImage : m_swapchainImages) {
        if (swapchainImage.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, swapchainImage.framebuffer, nullptr);
            swapchainImage.framebuffer = VK_NULL_HANDLE;
        }
        if (swapchainImage.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, swapchainImage.imageView, nullptr);
            swapchainImage.imageView = VK_NULL_HANDLE;
        }
    }
    
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanRenderer::createImageViews() {
    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapchainImages[i].image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapchainConfig.surfaceFormat.format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        VkResult result = vkCreateImageView(m_device, &createInfo, nullptr, 
                                          &m_swapchainImages[i].imageView);
        if (result != VK_SUCCESS) {
            setError("Failed to create image view: " + std::to_string(result));
            return false;
        }
    }
    
    return true;
}

bool VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchainConfig.surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
    if (result != VK_SUCCESS) {
        setError("Failed to create render pass: " + std::to_string(result));
        return false;
    }
    
    return true;
}

bool VulkanRenderer::createFramebuffers() {
    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageView attachments[] = {m_swapchainImages[i].imageView};
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainConfig.extent.width;
        framebufferInfo.height = m_swapchainConfig.extent.height;
        framebufferInfo.layers = 1;
        
        VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
                                            &m_swapchainImages[i].framebuffer);
        if (result != VK_SUCCESS) {
            setError("Failed to create framebuffer: " + std::to_string(result));
            return false;
        }
    }
    
    return true;
}

bool VulkanRenderer::createCommandObjects() {
    QueueFamilyIndices queueFamilyIndices = m_context->getQueueFamilies();
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    
    VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        setError("Failed to create command pool: " + std::to_string(result));
        return false;
    }
    
    // Allocate command buffers
    m_frames.resize(MAX_FRAMES_IN_FLIGHT);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    
    std::vector<VkCommandBuffer> commandBuffers(MAX_FRAMES_IN_FLIGHT);
    result = vkAllocateCommandBuffers(m_device, &allocInfo, commandBuffers.data());
    if (result != VK_SUCCESS) {
        setError("Failed to allocate command buffers: " + std::to_string(result));
        return false;
    }
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_frames[i].commandBuffer = commandBuffers[i];
    }
    
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkResult result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                                          &m_frames[i].imageAvailableSemaphore);
        if (result != VK_SUCCESS) {
            setError("Failed to create image available semaphore: " + std::to_string(result));
            return false;
        }
        
        result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr,
                                 &m_frames[i].renderFinishedSemaphore);
        if (result != VK_SUCCESS) {
            setError("Failed to create render finished semaphore: " + std::to_string(result));
            return false;
        }
        
        result = vkCreateFence(m_device, &fenceInfo, nullptr, &m_frames[i].inFlightFence);
        if (result != VK_SUCCESS) {
            setError("Failed to create fence: " + std::to_string(result));
            return false;
        }
    }
    
    return true;
}

VkSurfaceFormatKHR VulkanRenderer::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) const {
    
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) const {
    
    if (!m_swapchainConfig.vsync) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return availablePresentMode;
            }
        }
    }
    
    return VK_PRESENT_MODE_FIFO_KHR; // Always available, VSync guaranteed
}

VkExtent2D VulkanRenderer::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities) const {
    
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = m_swapchainConfig.extent;
        
        actualExtent.width = std::clamp(actualExtent.width,
                                      capabilities.minImageExtent.width,
                                      capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height,
                                       capabilities.minImageExtent.height,
                                       capabilities.maxImageExtent.height);
        
        return actualExtent;
    }
}

void VulkanRenderer::beginRenderPass() {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainImages[m_imageIndex].framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainConfig.extent;
    
    VkClearValue clearColor = {{{0.0f, 1.0f, 0.0f, 1.0f}}}; // BRIGHT GREEN background for debugging
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(getCurrentCommandBuffer(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Set viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchainConfig.extent.width);
    viewport.height = static_cast<float>(m_swapchainConfig.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    // Debug: Log viewport settings every 120 frames
    static int viewport_debug_counter = 0;
    if (viewport_debug_counter++ % 120 == 0) {
        std::cout << "[DEBUG] Viewport: x=" << viewport.x << ", y=" << viewport.y 
                  << ", w=" << viewport.width << ", h=" << viewport.height 
                  << ", depth=" << viewport.minDepth << "-" << viewport.maxDepth << std::endl;
    }
    
    vkCmdSetViewport(getCurrentCommandBuffer(), 0, 1, &viewport);
    
    // Set scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchainConfig.extent;
    vkCmdSetScissor(getCurrentCommandBuffer(), 0, 1, &scissor);
}

void VulkanRenderer::endRenderPass() {
    vkCmdEndRenderPass(getCurrentCommandBuffer());
}

void VulkanRenderer::bindPipeline(const std::string& pipelineType) {
    auto it = m_pipelines.find(pipelineType);
    if (it != m_pipelines.end() && it->second.isValid()) {
        vkCmdBindPipeline(getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, it->second.pipeline);
        m_currentPipelineType = pipelineType;
    }
}

VkPipelineLayout VulkanRenderer::getPipelineLayout(const std::string& pipelineType) const {
    auto it = m_pipelines.find(pipelineType);
    if (it != m_pipelines.end()) {
        return it->second.pipelineLayout;
    }
    return VK_NULL_HANDLE;
}

bool VulkanRenderer::createGraphicsPipelines() {
    std::cout << "[DEBUG] Starting graphics pipeline creation..." << std::endl;
    std::cout.flush();
    
    // Create standard pipelines for different rendering modes
    
    // Solid pipeline (use full shaders that consume uniform buffers and projection matrix)
    std::cout << "[DEBUG] Creating solid pipeline (full shader path)..." << std::endl;
    std::cout.flush();
    if (!createPipeline("solid", "vertexMain", "fragmentSolid")) {
        std::cerr << "[ERROR] Failed to create solid pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Solid pipeline (full shader path) created successfully." << std::endl;
    
    // Also create a separate minimal debug pipeline for low-level rasterization checks
    std::cout << "[DEBUG] Creating minimal debug pipeline..." << std::endl;
    std::cout.flush();
    if (!createMinimalPipeline("debug_minimal", "vertexMinimal", "fragmentMinimal")) {
        std::cerr << "[ERROR] Failed to create minimal debug pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Minimal debug pipeline created successfully." << std::endl;
    
    // Glow effect pipeline
    std::cout << "[DEBUG] Creating glow pipeline..." << std::endl;
    std::cout.flush();
    if (!createPipeline("glow", "vertexMain", "fragmentGlow")) {
        std::cerr << "[ERROR] Failed to create glow pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Glow pipeline created successfully." << std::endl;
    
    // Animated pipeline
    std::cout << "[DEBUG] Creating animated pipeline..." << std::endl;
    std::cout.flush();
    if (!createPipeline("animated", "vertexMain", "fragmentAnimated")) {
        std::cerr << "[ERROR] Failed to create animated pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Animated pipeline created successfully." << std::endl;
    
    // Gradient pipeline
    std::cout << "[DEBUG] Creating gradient pipeline..." << std::endl;
    std::cout.flush();
    if (!createPipeline("gradient", "vertexMain", "fragmentGradient")) {
        std::cerr << "[ERROR] Failed to create gradient pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Gradient pipeline created successfully." << std::endl;
    
    // Debug pipeline
    std::cout << "[DEBUG] Creating debug pipeline..." << std::endl;
    std::cout.flush();
    if (!createPipeline("debug", "vertexMain", "fragmentDebug")) {
        std::cerr << "[ERROR] Failed to create debug pipeline" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Debug pipeline created successfully." << std::endl;
    
    std::cout << "[DEBUG] All graphics pipelines created successfully!" << std::endl;
    return true;
}

bool VulkanRenderer::createPipeline(
    const std::string& name,
    const std::string& vertexShaderEntry,
    const std::string& fragmentShaderEntry) {
    
    std::cout << "[DEBUG] Creating pipeline '" << name << "' with vertex='" << vertexShaderEntry << "', fragment='" << fragmentShaderEntry << "'" << std::endl;
    std::cout.flush();
    
    GraphicsPipeline pipeline;
    pipeline.vertexShader = vertexShaderEntry;
    pipeline.fragmentShader = fragmentShaderEntry;
    
    // Compile shaders
    std::cout << "[DEBUG] Compiling vertex shader from primitives.slang, entry: " << vertexShaderEntry << std::endl;
    std::cout.flush();
    CompiledShader vertexShader = m_compiler->compileVertexShader("primitives.slang", vertexShaderEntry);
    if (!vertexShader.success) {
        std::cerr << "[ERROR] Failed to compile vertex shader for pipeline " << name << ": " << vertexShader.errors << std::endl;
        setError("Failed to compile vertex shader for pipeline " + name + ": " + vertexShader.errors);
        return false;
    }
    std::cout << "[DEBUG] Vertex shader compiled successfully (" << vertexShader.spirv.size() << " bytes SPIR-V)" << std::endl;
    
    std::cout << "[DEBUG] Compiling fragment shader from primitives.slang, entry: " << fragmentShaderEntry << std::endl;
    std::cout.flush();
    CompiledShader fragmentShader = m_compiler->compileFragmentShader("primitives.slang", fragmentShaderEntry);
    if (!fragmentShader.success) {
        std::cerr << "[ERROR] Failed to compile fragment shader for pipeline " << name << ": " << fragmentShader.errors << std::endl;
        setError("Failed to compile fragment shader for pipeline " + name + ": " + fragmentShader.errors);
        return false;
    }
    std::cout << "[DEBUG] Fragment shader compiled successfully (" << fragmentShader.spirv.size() << " bytes SPIR-V)" << std::endl;
    
    // Create shader modules
    std::cout << "[DEBUG] Creating Vulkan shader modules..." << std::endl;
    std::cout.flush();
    VkShaderModule vertShaderModule = createShaderModule(vertexShader.spirv);
    if (vertShaderModule == VK_NULL_HANDLE) {
        std::cerr << "[ERROR] Failed to create vertex shader module" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Vertex shader module created successfully." << std::endl;
    
    VkShaderModule fragShaderModule = createShaderModule(fragmentShader.spirv);
    if (fragShaderModule == VK_NULL_HANDLE) {
        std::cerr << "[ERROR] Failed to create fragment shader module" << std::endl;
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        return false;
    }
    std::cout << "[DEBUG] Fragment shader module created successfully." << std::endl;
    
    // Shader stage create info
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main"; // Slang compiles to main
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input description
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VulkanMath::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VulkanMath::Vertex, position);
    
    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VulkanMath::Vertex, color);
    
    // UV
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VulkanMath::Vertex, uv);
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Disable culling to avoid accidental triangle dismissal
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Color blending (alpha blending)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Create descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    
    // Global uniforms (b0)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Object uniforms (b1)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Game state uniforms (b2)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Rendering uniforms (b3)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkResult result = vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &pipeline.descriptorSetLayout);
    if (result != VK_SUCCESS) {
        setError("Failed to create descriptor set layout: " + std::to_string(result));
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &pipeline.descriptorSetLayout;
    
    result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &pipeline.pipelineLayout);
    if (result != VK_SUCCESS) {
        setError("Failed to create pipeline layout: " + std::to_string(result));
        vkDestroyDescriptorSetLayout(m_device, pipeline.descriptorSetLayout, nullptr);
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }
    
    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline);
    
    // Cleanup shader modules
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    
    if (result != VK_SUCCESS) {
        setError("Failed to create graphics pipeline: " + std::to_string(result));
        vkDestroyPipelineLayout(m_device, pipeline.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, pipeline.descriptorSetLayout, nullptr);
        return false;
    }
    
    // Store pipeline
    m_pipelines[name] = std::move(pipeline);
    
    return true;
}

bool VulkanRenderer::createMinimalPipeline(
    const std::string& name,
    const std::string& vertexShaderEntry,
    const std::string& fragmentShaderEntry) {
    
    std::cout << "[DEBUG] Creating MINIMAL pipeline '" << name << "' with vertex='" << vertexShaderEntry << "', fragment='" << fragmentShaderEntry << "'" << std::endl;
    std::cout.flush();
    
    GraphicsPipeline pipeline;
    pipeline.vertexShader = vertexShaderEntry;
    pipeline.fragmentShader = fragmentShaderEntry;
    
    // Compile shaders from minimal test file
    std::cout << "[DEBUG] Compiling minimal vertex shader from test_minimal.slang, entry: " << vertexShaderEntry << std::endl;
    std::cout.flush();
    CompiledShader vertexShader = m_compiler->compileVertexShader("test_minimal.slang", vertexShaderEntry);
    if (!vertexShader.success) {
        std::cerr << "[ERROR] Failed to compile minimal vertex shader for pipeline " << name << ": " << vertexShader.errors << std::endl;
        setError("Failed to compile minimal vertex shader for pipeline " + name + ": " + vertexShader.errors);
        return false;
    }
    std::cout << "[DEBUG] Minimal vertex shader compiled successfully (" << vertexShader.spirv.size() << " bytes SPIR-V)" << std::endl;
    
    std::cout << "[DEBUG] Compiling minimal fragment shader from test_minimal.slang, entry: " << fragmentShaderEntry << std::endl;
    std::cout.flush();
    CompiledShader fragmentShader = m_compiler->compileFragmentShader("test_minimal.slang", fragmentShaderEntry);
    if (!fragmentShader.success) {
        std::cerr << "[ERROR] Failed to compile minimal fragment shader for pipeline " << name << ": " << fragmentShader.errors << std::endl;
        setError("Failed to compile minimal fragment shader for pipeline " + name + ": " + fragmentShader.errors);
        return false;
    }
    std::cout << "[DEBUG] Minimal fragment shader compiled successfully (" << fragmentShader.spirv.size() << " bytes SPIR-V)" << std::endl;
    
    // Create shader modules
    std::cout << "[DEBUG] Creating minimal Vulkan shader modules..." << std::endl;
    std::cout.flush();
    VkShaderModule vertShaderModule = createShaderModule(vertexShader.spirv);
    if (vertShaderModule == VK_NULL_HANDLE) {
        std::cerr << "[ERROR] Failed to create minimal vertex shader module" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] Minimal vertex shader module created successfully." << std::endl;
    
    VkShaderModule fragShaderModule = createShaderModule(fragmentShader.spirv);
    if (fragShaderModule == VK_NULL_HANDLE) {
        std::cerr << "[ERROR] Failed to create minimal fragment shader module" << std::endl;
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        return false;
    }
    std::cout << "[DEBUG] Minimal fragment shader module created successfully." << std::endl;
    
    // Shader stage create info
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main"; // Slang compiles to main
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    
    // Vertex input description (must match full Vertex struct layout)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VulkanMath::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VulkanMath::Vertex, position);
    
    // Color
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VulkanMath::Vertex, color);
    
    // UV (required for proper vertex layout alignment)
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(VulkanMath::Vertex, uv);
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    // Standard pipeline state (same as regular pipeline)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // Disable culling to avoid accidental triangle dismissal
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;  // DISABLE blending for debugging
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Depth/stencil state - DISABLED since render pass has no depth buffer
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;  // No depth testing
    depthStencil.depthWriteEnable = VK_FALSE; // No depth writing
    depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER; // Not used since depth test disabled
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE; // No stencil testing
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // NO DESCRIPTOR SETS for minimal pipeline - just empty layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;  // No descriptor sets
    pipelineLayoutInfo.pSetLayouts = nullptr;
    
    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &pipeline.pipelineLayout);
    if (result != VK_SUCCESS) {
        setError("Failed to create minimal pipeline layout: " + std::to_string(result));
        vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
        vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
        return false;
    }
    
    // Graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;  // Add depth/stencil state
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    
    result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline);
    
    // Cleanup shader modules
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    
    if (result != VK_SUCCESS) {
        setError("Failed to create minimal graphics pipeline: " + std::to_string(result));
        vkDestroyPipelineLayout(m_device, pipeline.pipelineLayout, nullptr);
        return false;
    }
    
    // Store pipeline
    m_pipelines[name] = std::move(pipeline);
    
    std::cout << "[DEBUG] Minimal pipeline '" << name << "' created successfully!" << std::endl;
    return true;
}

VkShaderModule VulkanRenderer::createShaderModule(const std::vector<uint8_t>& spirvCode) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(spirvCode.data());
    
    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        setError("Failed to create shader module: " + std::to_string(result));
        return VK_NULL_HANDLE;
    }
    
    return shaderModule;
}

void VulkanRenderer::cleanupPipelines() {
    for (auto& pair : m_pipelines) {
        GraphicsPipeline& pipeline = pair.second;
        
        if (pipeline.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
            pipeline.pipeline = VK_NULL_HANDLE;
        }
        
        if (pipeline.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, pipeline.pipelineLayout, nullptr);
            pipeline.pipelineLayout = VK_NULL_HANDLE;
        }
        
        if (pipeline.descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_device, pipeline.descriptorSetLayout, nullptr);
            pipeline.descriptorSetLayout = VK_NULL_HANDLE;
        }
    }
    
    m_pipelines.clear();
    m_currentPipelineType.clear();
}

void VulkanRenderer::drawRectangle(
    VulkanMath::Vec2 center,
    VulkanMath::Vec2 size,
    VulkanMath::Vec3 color,
    const std::string& pipelineType) {
    
    auto vertices = generateRectangleVertices(center, size, color);
    addToBatch(pipelineType, vertices);
}

void VulkanRenderer::drawRawTriangles(
    const std::vector<VulkanMath::Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const std::string& pipelineType) {
    
    addToBatch(pipelineType, vertices, indices);
}

void VulkanRenderer::drawCircle(
    VulkanMath::Vec2 center,
    float radius,
    VulkanMath::Vec3 color,
    uint32_t segments,
    const std::string& pipelineType) {
    
    auto vertices = generateCircleVertices(center, radius, color, segments);
    auto indices = generateCircleIndices(segments);
    addToBatch(pipelineType, vertices, indices);
}

void VulkanRenderer::drawLine(
    VulkanMath::Vec2 start,
    VulkanMath::Vec2 end,
    float thickness,
    VulkanMath::Vec3 color,
    const std::string& pipelineType) {
    
    // Create a rectangle representing the line
    VulkanMath::Vec2 center = (start + end) * 0.5f;
    VulkanMath::Vec2 direction = end - start;
    float length = direction.length();
    
    if (length < 0.001f) return; // Degenerate line
    
    // For simplicity, create axis-aligned rectangle
    // TODO: Add rotation for arbitrary line angles
    VulkanMath::Vec2 size = VulkanMath::Vec2(length, thickness);
    auto vertices = generateRectangleVertices(center, size, color);
    addToBatch(pipelineType, vertices);
}

void VulkanRenderer::flushBatches() {
    static int flushCounter = 0;
    if (flushCounter < 5) {  // Log first few flushes
        std::cout << "[DEBUG] flushBatches: Processing " << m_batches.size() << " pipeline batches" << std::endl;
        flushCounter++;
    }
    
    for (auto& pair : m_batches) {
        if (!pair.second.empty()) {
            if (flushCounter <= 5) {
                std::cout << "[DEBUG] Rendering batch '" << pair.first << "' with " << pair.second.vertices.size() << " vertices" << std::endl;
            }
            renderBatch(pair.second);
            pair.second.clear();
        }
    }
}

void VulkanRenderer::setGameCoordinates(float gameWidth, float gameHeight) {
    m_gameWidth = gameWidth;
    m_gameHeight = gameHeight;
    
    // Update projection matrix
    m_globalUniforms.projectionMatrix = VulkanMath::gameProjectionMatrix(gameWidth, gameHeight);
    m_globalUniforms.viewMatrix = VulkanMath::Mat4(); // Identity
    m_globalUniforms.modelMatrix = VulkanMath::Mat4(); // Identity
    m_globalUniforms.screenSize = VulkanMath::Vec2(
        static_cast<float>(m_swapchainConfig.extent.width),
        static_cast<float>(m_swapchainConfig.extent.height)
    );
    m_globalUniforms.gameSize = VulkanMath::Vec2(gameWidth, gameHeight);
}

void VulkanRenderer::updateTime(float currentTime, float deltaTime) {
    m_globalUniforms.time = currentTime;
    m_globalUniforms.deltaTime = deltaTime;
}

bool VulkanRenderer::createVertexBuffers() {
    // Create vertex buffer
    VkDeviceSize vertexBufferSize = sizeof(VulkanMath::Vertex) * m_maxVertices;
    m_vertexBuffer = m_memoryManager->createVertexBuffer(vertexBufferSize);
    if (!m_vertexBuffer.isValid()) {
        setError("Failed to create vertex buffer: " + m_memoryManager->getLastError());
        return false;
    }
    
    // Create index buffer
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * m_maxIndices;
    m_indexBuffer = m_memoryManager->createIndexBuffer(indexBufferSize);
    if (!m_indexBuffer.isValid()) {
        setError("Failed to create index buffer: " + m_memoryManager->getLastError());
        return false;
    }
    
    return true;
}

bool VulkanRenderer::createUniformBuffers() {
    m_globalUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_objectUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_globalUniformBuffers[i] = m_memoryManager->createUniformBuffer(sizeof(GlobalUniforms));
        if (!m_globalUniformBuffers[i].isValid()) {
            setError("Failed to create global uniform buffer: " + m_memoryManager->getLastError());
            return false;
        }
        
        m_objectUniformBuffers[i] = m_memoryManager->createUniformBuffer(sizeof(ObjectUniforms));
        if (!m_objectUniformBuffers[i].isValid()) {
            setError("Failed to create object uniform buffer: " + m_memoryManager->getLastError());
            return false;
        }
    }
    
    return true;
}

bool VulkanRenderer::createDescriptorSets() {
    // Count pipelines with descriptor sets (skip minimal pipelines)
    size_t pipelinesWithDescriptors = 0;
    for (const auto& pair : m_pipelines) {
        if (pair.second.descriptorSetLayout != VK_NULL_HANDLE) {
            pipelinesWithDescriptors++;
        }
    }
    
    // Skip creating descriptor pool if no pipelines need descriptors
    if (pipelinesWithDescriptors == 0) {
        std::cout << "[DEBUG] No pipelines need descriptor sets, skipping descriptor pool creation." << std::endl;
        return true;
    }
    
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * pipelinesWithDescriptors * 4); // 4 UBOs per pipeline
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * pipelinesWithDescriptors);
    
    VkResult result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (result != VK_SUCCESS) {
        setError("Failed to create descriptor pool: " + std::to_string(result));
        return false;
    }
    
    // Allocate descriptor sets
    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++) {
        m_descriptorSets[frame].resize(m_pipelines.size());
        
        size_t pipelineIndex = 0;
        for (const auto& pair : m_pipelines) {
            const GraphicsPipeline& pipeline = pair.second;
            
            // Skip pipelines without descriptor set layouts (minimal pipelines)
            if (pipeline.descriptorSetLayout == VK_NULL_HANDLE) {
                pipelineIndex++;
                continue;
            }
            
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &pipeline.descriptorSetLayout;
            
            result = vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSets[frame][pipelineIndex]);
            if (result != VK_SUCCESS) {
                setError("Failed to allocate descriptor sets: " + std::to_string(result));
                return false;
            }
            
            // Update descriptor sets
            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            
            VkDescriptorBufferInfo globalBufferInfo{};
            globalBufferInfo.buffer = m_globalUniformBuffers[frame].buffer;
            globalBufferInfo.offset = 0;
            globalBufferInfo.range = sizeof(GlobalUniforms);
            
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = m_descriptorSets[frame][pipelineIndex];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &globalBufferInfo;
            
            VkDescriptorBufferInfo objectBufferInfo{};
            objectBufferInfo.buffer = m_objectUniformBuffers[frame].buffer;
            objectBufferInfo.offset = 0;
            objectBufferInfo.range = sizeof(ObjectUniforms);
            
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = m_descriptorSets[frame][pipelineIndex];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pBufferInfo = &objectBufferInfo;
            
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), 
                                 descriptorWrites.data(), 0, nullptr);
            
            pipelineIndex++;
        }
    }
    
    return true;
}

void VulkanRenderer::updateUniformBuffers() {
    // Debug: Print projection matrix once
    static bool debugPrinted = false;
    if (!debugPrinted) {
        std::cout << "[DEBUG] Projection Matrix:" << std::endl;
        for (int i = 0; i < 4; i++) {
            std::cout << "[DEBUG]   [" << m_globalUniforms.projectionMatrix.m[i][0] 
                      << ", " << m_globalUniforms.projectionMatrix.m[i][1]
                      << ", " << m_globalUniforms.projectionMatrix.m[i][2]
                      << ", " << m_globalUniforms.projectionMatrix.m[i][3] << "]" << std::endl;
        }
        debugPrinted = true;
    }
    
    // Update global uniforms
    m_memoryManager->updateBuffer(m_globalUniformBuffers[m_currentFrame], 
                                 &m_globalUniforms, sizeof(GlobalUniforms));
    
    // Update object uniforms (basic defaults so shaders have valid data)
    m_objectUniforms.objectTransform = VulkanMath::Mat4(); // Identity
    // Material defaults
    m_objectUniforms.material.baseColor = VulkanMath::Vec3(1.0f, 1.0f, 1.0f); // White
    m_objectUniforms.material.roughness = 0.5f;
    m_objectUniforms.material.metallic = 0.0f;
    m_objectUniforms.material.emission = 0.0f;
    // Shape defaults (rectangle) - shaders interpret UV space [0,1]
    m_objectUniforms.shape.size = VulkanMath::Vec2(1.0f, 1.0f);
    m_objectUniforms.shape.cornerRadius = 0.0f;
    m_objectUniforms.shape.thickness = 0.0f;
    m_objectUniforms.shape.type = 0; // PRIMITIVE_RECTANGLE
    // Effect defaults
    m_objectUniforms.effects.glowIntensity = 0.0f;
    m_objectUniforms.effects.glowRadius = 1.0f;
    m_objectUniforms.effects.pulseSpeed = 1.0f;
    m_objectUniforms.effects.time = m_globalUniforms.time;
    
    m_memoryManager->updateBuffer(m_objectUniformBuffers[m_currentFrame], 
                                 &m_objectUniforms, sizeof(ObjectUniforms));
}

void VulkanRenderer::addToBatch(
    const std::string& pipelineType,
    const std::vector<VulkanMath::Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    VkPrimitiveTopology topology) {
    
    static int batchCounter = 0;
    if (batchCounter < 10) {  // Only log first 10 batch additions to avoid spam
        std::cout << "[DEBUG] addToBatch: pipeline=" << pipelineType << ", vertices=" << vertices.size() << ", indices=" << indices.size() << std::endl;
        if (!vertices.empty()) {
            std::cout << "[DEBUG] First vertex: pos=(" << vertices[0].position.x << "," << vertices[0].position.y 
                      << "), color=(" << vertices[0].color.x << "," << vertices[0].color.y << "," << vertices[0].color.z << ")" << std::endl;
        }
        batchCounter++;
    }
    
    RenderBatch& batch = m_batches[pipelineType];
    
    if (batch.pipelineType.empty()) {
        batch.pipelineType = pipelineType;
        batch.topology = topology;
    }
    
    // Add vertices
    batch.vertices.insert(batch.vertices.end(), vertices.begin(), vertices.end());
    
    // Add indices (adjust for current vertex count)
    if (!indices.empty()) {
        uint32_t vertexOffset = static_cast<uint32_t>(batch.vertices.size() - vertices.size());
        for (uint32_t index : indices) {
            batch.indices.push_back(index + vertexOffset);
        }
    }
}

void VulkanRenderer::renderBatch(const RenderBatch& batch) {
    if (batch.empty()) return;
    
    VkCommandBuffer commandBuffer = getCurrentCommandBuffer();
    
    // Use the batch's requested pipeline by default. Diagnostic overrides removed so game
    // rendering uses the intended shaders (e.g., 'solid', 'glow', 'animated').
    std::string pipelineToUse = batch.pipelineType;
    
    // Bind pipeline
    bindPipeline(pipelineToUse);
    
    // Find pipeline index for descriptor set (use the original pipeline index mapping)
    size_t pipelineIndex = 0;
    for (const auto& pair : m_pipelines) {
        if (pair.first == batch.pipelineType) break;
        pipelineIndex++;
    }
    
    // Bind descriptor sets (skip for minimal pipelines which have no descriptor sets)
    auto pipelineIt = m_pipelines.find(batch.pipelineType);
    bool hasDescriptorSets = (pipelineIt != m_pipelines.end() && pipelineIt->second.descriptorSetLayout != VK_NULL_HANDLE);
    
    if (hasDescriptorSets && pipelineIndex < m_descriptorSets[m_currentFrame].size()) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              getPipelineLayout(batch.pipelineType), 0, 1,
                              &m_descriptorSets[m_currentFrame][pipelineIndex], 0, nullptr);
    }
    
    // Update vertex buffer
    size_t vertexDataSize = batch.vertices.size() * sizeof(VulkanMath::Vertex);
    if (vertexDataSize > 0) {
        m_memoryManager->updateBuffer(m_vertexBuffer, batch.vertices.data(), vertexDataSize);
        
        VkBuffer vertexBuffers[] = {m_vertexBuffer.buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    }
    
    // Draw
    if (!batch.indices.empty()) {
        // Update index buffer and draw indexed
        size_t indexDataSize = batch.indices.size() * sizeof(uint32_t);
        m_memoryManager->updateBuffer(m_indexBuffer, batch.indices.data(), indexDataSize);
        
        std::cout << "[DEBUG] DRAW INDEXED: " << batch.indices.size() << " indices, pipeline=" << batch.pipelineType << std::endl;
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(batch.indices.size()), 1, 0, 0, 0);
    } else {
        // Draw non-indexed
        uint32_t vertexCount = static_cast<uint32_t>(batch.vertices.size());
        std::cout << "[DEBUG] DRAW NON-INDEXED: " << vertexCount << " vertices, pipeline=" << batch.pipelineType << std::endl;
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
        std::cout << "[DEBUG] vkCmdDraw issued: vertexCount=" << vertexCount << ", instanceCount=1, firstVertex=0, firstInstance=0" << std::endl;
    }
}

VulkanMath::Vec2 VulkanRenderer::gameToNDC(VulkanMath::Vec2 gamePos) const {
    return VulkanMath::Vec2(
        (gamePos.x / m_gameWidth) * 2.0f - 1.0f,
        (gamePos.y / m_gameHeight) * 2.0f - 1.0f
    );
}

std::vector<VulkanMath::Vertex> VulkanRenderer::generateRectangleVertices(
    VulkanMath::Vec2 center,
    VulkanMath::Vec2 size,
    VulkanMath::Vec3 color) const {
    
    VulkanMath::Vec2 halfSize = size * 0.5f;
    VulkanMath::Vec2 topLeft = center + VulkanMath::Vec2(-halfSize.x, -halfSize.y);
    VulkanMath::Vec2 topRight = center + VulkanMath::Vec2(halfSize.x, -halfSize.y);
    VulkanMath::Vec2 bottomLeft = center + VulkanMath::Vec2(-halfSize.x, halfSize.y);
    VulkanMath::Vec2 bottomRight = center + VulkanMath::Vec2(halfSize.x, halfSize.y);
    
    return {
        // First triangle
        VulkanMath::Vertex(topLeft, color, VulkanMath::Vec2(0.0f, 0.0f)),
        VulkanMath::Vertex(bottomLeft, color, VulkanMath::Vec2(0.0f, 1.0f)),
        VulkanMath::Vertex(topRight, color, VulkanMath::Vec2(1.0f, 0.0f)),
        // Second triangle
        VulkanMath::Vertex(topRight, color, VulkanMath::Vec2(1.0f, 0.0f)),
        VulkanMath::Vertex(bottomLeft, color, VulkanMath::Vec2(0.0f, 1.0f)),
        VulkanMath::Vertex(bottomRight, color, VulkanMath::Vec2(1.0f, 1.0f))
    };
}

std::vector<VulkanMath::Vertex> VulkanRenderer::generateCircleVertices(
    VulkanMath::Vec2 center,
    float radius,
    VulkanMath::Vec3 color,
    uint32_t segments) const {
    
    std::vector<VulkanMath::Vertex> vertices;
    vertices.reserve(segments + 1);
    
    // Center vertex
    vertices.emplace_back(center, color, VulkanMath::Vec2(0.5f, 0.5f));
    
    // Edge vertices
    for (uint32_t i = 0; i < segments; i++) {

        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * 3.14159f;
        VulkanMath::Vec2 pos = center + VulkanMath::Vec2(
            std::cos(angle) * radius,
            std::sin(angle) * radius
        );
        VulkanMath::Vec2 uv = VulkanMath::Vec2(
            (std::cos(angle) + 1.0f) * 0.5f,
            (std::sin(angle) + 1.0f) * 0.5f
        );
        vertices.emplace_back(pos, color, uv);
    }
    
    return vertices;
}

std::vector<uint32_t> VulkanRenderer::generateCircleIndices(uint32_t segments) const {
    std::vector<uint32_t> indices;
    indices.reserve(segments * 3);
    
    for (uint32_t i = 0; i < segments; i++) {
        uint32_t next = (i + 1) % segments;
        indices.push_back(0);       // Center
        indices.push_back(i + 1);   // Current edge
        indices.push_back(next + 1); // Next edge
    }
    
    return indices;
}

void VulkanRenderer::debugSamplePixelColors(int frameNumber) {
    // Run debug output every few frames for testing
    if (frameNumber % 5 != 0) return;
    
    std::cout << "[DEBUG] Frame " << frameNumber << " - Render State Debug:" << std::endl;
    
    if (!isInitialized() || m_swapchain == VK_NULL_HANDLE) {
        std::cout << "[DEBUG] Cannot debug - renderer not initialized" << std::endl;
        return;
    }
    
    VkExtent2D extent = m_swapchainConfig.extent;
    std::cout << "[DEBUG] Swapchain: " << extent.width << "x" << extent.height 
              << ", format: " << m_swapchainConfig.surfaceFormat.format << std::endl;
    std::cout << "[DEBUG] Current image index: " << m_imageIndex << " of " << m_swapchainImages.size() << std::endl;
    
    // Check pipeline state
    auto solidPipeline = m_pipelines.find("solid");
    if (solidPipeline != m_pipelines.end()) {
        std::cout << "[DEBUG] Solid pipeline: " << (solidPipeline->second.pipeline != VK_NULL_HANDLE ? "VALID" : "NULL") << std::endl;
        std::cout << "[DEBUG] Pipeline layout: " << (solidPipeline->second.pipelineLayout != VK_NULL_HANDLE ? "VALID" : "NULL") << std::endl;
        std::cout << "[DEBUG] Descriptor layout: " << (solidPipeline->second.descriptorSetLayout != VK_NULL_HANDLE ? "HAS_DESCRIPTORS" : "NO_DESCRIPTORS") << std::endl;
    } else {
        std::cout << "[DEBUG] ERROR: Solid pipeline not found!" << std::endl;
    }
    
    // Check batch state
    auto solidBatch = m_batches.find("solid");
    if (solidBatch != m_batches.end()) {
        std::cout << "[DEBUG] Solid batch vertices: " << solidBatch->second.vertices.size() 
                  << ", indices: " << solidBatch->second.indices.size() << std::endl;
        if (!solidBatch->second.vertices.empty()) {
            auto& firstVertex = solidBatch->second.vertices[0];
            std::cout << "[DEBUG] First batch vertex: pos=(" << firstVertex.position.x << "," << firstVertex.position.y 
                      << "), color=(" << firstVertex.color.x << "," << firstVertex.color.y << "," << firstVertex.color.z << ")" << std::endl;
        }
    }
    
    // Check current frame state
    if (m_currentFrame < m_frames.size()) {
        auto& frame = m_frames[m_currentFrame];
        std::cout << "[DEBUG] Frame " << m_currentFrame << " command buffer: " << (frame.commandBuffer != VK_NULL_HANDLE ? "VALID" : "NULL") << std::endl;
    }
    
    // Estimate expected NDC coordinates
    std::cout << "[DEBUG] CONFIRMED: Render pass working (green background visible)" << std::endl;
    std::cout << "[DEBUG] Triangle vertices processed: " << (solidBatch != m_batches.end() ? "YES" : "NO") << std::endl;
    std::cout << "[DEBUG] Issue: Fragment shader output not reaching framebuffer" << std::endl;
    std::cout << "[DEBUG] Possible causes: viewport clipping, depth test, blending, or fragment culling" << std::endl;
}

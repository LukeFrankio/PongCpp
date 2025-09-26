/**
 * @file vulkan_memory.cpp
 * @brief Implementation of manual Vulkan memory management
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "vulkan_memory.h"
#include "vulkan_context.h"
#include "vulkan_debug.h"
#include <iostream>
#include <algorithm>
#include <sstream>

// Note: do not attempt to redefine 'std::cout' via macro (invalid token '::').
// Files may use VULKAN_DBG from vulkan_debug.h directly if they are updated.

bool VulkanMemoryManager::initialize(VulkanContext* context) {
    if (!context || !context->isInitialized()) {
        setError("Invalid Vulkan context");
        return false;
    }
    
    m_context = context;
    m_device = context->getDevice();
    m_physicalDevice = context->getPhysicalDevice();
    
    // Create command pool for memory operations
    if (!createCommandPool()) {
        return false;
    }
    
    return true;
}

void VulkanMemoryManager::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    
    // Wait for device to be idle
    vkDeviceWaitIdle(m_device);
    
    // Destroy all allocated buffers
    for (auto& buffer : m_allocatedBuffers) {
        if (buffer.isValid()) {
            destroyBuffer(buffer);
        }
    }
    m_allocatedBuffers.clear();
    
    // Destroy all allocated images
    for (auto& image : m_allocatedImages) {
        if (image.isValid()) {
            destroyImage(image);
        }
    }
    m_allocatedImages.clear();
    
    // Destroy command pool
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    
    // Reset state
    m_totalAllocatedMemory = 0;
    m_allocationCount = 0;
    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_context = nullptr;
}

VulkanBuffer VulkanMemoryManager::createVertexBuffer(VkDeviceSize size, const void* data) {
    std::cout << "[DEBUG] VulkanMemoryManager::createVertexBuffer() size=" << size << std::endl;
    std::cout.flush();
    
    // Vertex buffers must allow transfer dst so staging copies can be used
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (data) {
        // If we have initial data, transfer bit already present
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VulkanBuffer result = createBuffer(size, usage, properties, data);
    return result;
}

VulkanBuffer VulkanMemoryManager::createIndexBuffer(VkDeviceSize size, const void* data) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (data) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return createBuffer(size, usage, properties, data);
}

VulkanBuffer VulkanMemoryManager::createUniformBuffer(VkDeviceSize size, bool persistentMap) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkMemoryPropertyFlags properties = 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    
    VulkanBuffer buffer = createBuffer(size, usage, properties);
    
    // Map persistently if requested
    if (buffer.isValid() && persistentMap) {
        buffer.mapped = mapBuffer(buffer);
        buffer.isPersistentlyMapped = true;
    }
    
    return buffer;
}

VulkanBuffer VulkanMemoryManager::createStagingBuffer(VkDeviceSize size) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkMemoryPropertyFlags properties = 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    
    return createBuffer(size, usage, properties);
}

VulkanBuffer VulkanMemoryManager::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    const void* data) {
    
    std::cout << "[DEBUG] VulkanMemoryManager::createBuffer() starting..." << std::endl;
    std::cout << "[DEBUG] Device: " << m_device << ", Size: " << size << ", Usage: " << usage << std::endl;
    std::cout.flush();
    
    VulkanBuffer result;
    result.size = size;
    result.usage = usage;
    result.properties = properties;
    
    // Create buffer
    std::cout << "[DEBUG] Creating VkBuffer..." << std::endl;
    std::cout.flush();
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult vkResult = vkCreateBuffer(m_device, &bufferInfo, nullptr, &result.buffer);
    if (vkResult != VK_SUCCESS) {
        setError("Failed to create buffer: " + std::to_string(vkResult));
        return result;
    }
    std::cout << "[DEBUG] VkBuffer created successfully." << std::endl;
    std::cout.flush();
    
    // Get memory requirements
    std::cout << "[DEBUG] Getting memory requirements..." << std::endl;
    std::cout.flush();
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, result.buffer, &memRequirements);
    std::cout << "[DEBUG] Memory requirements: size=" << memRequirements.size << ", alignment=" << memRequirements.alignment << std::endl;
    std::cout.flush();
    
    // Allocate memory
    std::cout << "[DEBUG] Allocating memory..." << std::endl;
    std::cout.flush();
    result.memory = allocateMemory(memRequirements, properties);
    if (result.memory == VK_NULL_HANDLE) {
        std::cout << "[DEBUG] Memory allocation failed!" << std::endl;
        std::cout.flush();
        vkDestroyBuffer(m_device, result.buffer, nullptr);
        result.buffer = VK_NULL_HANDLE;
        return result;
    }
    std::cout << "[DEBUG] Memory allocated successfully." << std::endl;
    std::cout.flush();
    
    // Bind buffer to memory
    vkResult = vkBindBufferMemory(m_device, result.buffer, result.memory, 0);
    if (vkResult != VK_SUCCESS) {
        setError("Failed to bind buffer memory: " + std::to_string(vkResult));
        vkFreeMemory(m_device, result.memory, nullptr);
        vkDestroyBuffer(m_device, result.buffer, nullptr);
        result.buffer = VK_NULL_HANDLE;
        result.memory = VK_NULL_HANDLE;
        return result;
    }
    
    // Copy initial data if provided
    if (data && result.isMappable()) {
        // Direct copy for host-visible memory
        updateBuffer(result, data, size);
    } else if (data) {
        // Use staging buffer for device-local memory
        VulkanBuffer stagingBuffer = createStagingBuffer(size);
        if (stagingBuffer.isValid()) {
            updateBuffer(stagingBuffer, data, size);
            copyBuffer(stagingBuffer, result, size);
            destroyBuffer(stagingBuffer);
        }
    }
    
    // Track allocation
    m_allocatedBuffers.push_back(result);
    m_totalAllocatedMemory += memRequirements.size;
    m_allocationCount++;
    
    return result;
}

VulkanImage VulkanMemoryManager::createImage2D(
    uint32_t width, uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties) {
    
    VulkanImage result;
    result.format = format;
    result.extent = {width, height};
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult vkResult = vkCreateImage(m_device, &imageInfo, nullptr, &result.image);
    if (vkResult != VK_SUCCESS) {
        setError("Failed to create image: " + std::to_string(vkResult));
        return result;
    }
    
    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, result.image, &memRequirements);
    
    // Allocate memory
    result.memory = allocateMemory(memRequirements, properties);
    if (result.memory == VK_NULL_HANDLE) {
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        return result;
    }
    
    // Bind image to memory
    vkResult = vkBindImageMemory(m_device, result.image, result.memory, 0);
    if (vkResult != VK_SUCCESS) {
        setError("Failed to bind image memory: " + std::to_string(vkResult));
        vkFreeMemory(m_device, result.memory, nullptr);
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        result.memory = VK_NULL_HANDLE;
        return result;
    }
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkResult = vkCreateImageView(m_device, &viewInfo, nullptr, &result.view);
    if (vkResult != VK_SUCCESS) {
        setError("Failed to create image view: " + std::to_string(vkResult));
        vkFreeMemory(m_device, result.memory, nullptr);
        vkDestroyImage(m_device, result.image, nullptr);
        result.image = VK_NULL_HANDLE;
        result.memory = VK_NULL_HANDLE;
        return result;
    }
    
    // Track allocation
    m_allocatedImages.push_back(result);
    m_totalAllocatedMemory += memRequirements.size;
    m_allocationCount++;
    
    return result;
}

bool VulkanMemoryManager::updateBuffer(VulkanBuffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!buffer.isValid() || !data) {
        setError("Invalid buffer or data");
        return false;
    }

    if (size == 0) size = buffer.size;
    if (offset + size > buffer.size) {
        setError("Update size exceeds buffer bounds");
        return false;
    }

    // If the destination buffer is host-visible, map and copy directly
    if (buffer.isMappable()) {
        void* mappedData = buffer.mapped;
        bool needsUnmap = false;

        // Map if not already mapped
        if (!mappedData) {
            mappedData = mapBuffer(buffer);
            if (!mappedData) {
                return false;
            }
            needsUnmap = true;
        }

        // Copy data
        std::memcpy(static_cast<char*>(mappedData) + offset, data, size);

        // Unmap if we mapped it
        if (needsUnmap) {
            unmapBuffer(buffer);
        }

        return true;
    }

    // If the destination buffer is NOT host-visible (typical for device-local vertex/index buffers),
    // create a staging buffer, upload into it, then issue a GPU-side copy into the destination.
    std::cout << "[DEBUG] updateBuffer: destination buffer not mappable - using staging buffer upload (size=" << size << ")" << std::endl;

    VulkanBuffer staging = createStagingBuffer(size);
    if (!staging.isValid()) {
        setError("Failed to create staging buffer for update");
        return false;
    }

    // Update staging buffer (this will take the host-visible path above)
    if (!updateBuffer(staging, data, size, 0)) {
        std::string prev = m_lastError;
        destroyBuffer(staging);
        setError("Failed to update staging buffer: " + prev);
        return false;
    }

    // Copy staging -> destination buffer
    bool copyOk = copyBuffer(staging, buffer, size);

    // Destroy temporary staging buffer
    destroyBuffer(staging);

    if (!copyOk) {
        setError("Failed to copy staging buffer to destination");
        return false;
    }

    return true;
}

bool VulkanMemoryManager::copyBuffer(const VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size) {
    if (!src.isValid() || !dst.isValid()) {
        setError("Invalid source or destination buffer");
        return false;
    }
    
    if (size == 0) size = std::min(src.size, dst.size);
    
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, src.buffer, dst.buffer, 1, &copyRegion);
    
    endSingleTimeCommands(commandBuffer);
    return true;
}

void* VulkanMemoryManager::mapBuffer(VulkanBuffer& buffer) {
    if (!buffer.isValid() || !buffer.isMappable()) {
        setError("Buffer is not mappable");
        return nullptr;
    }
    
    if (buffer.mapped) {
        return buffer.mapped;
    }
    
    VkResult result = vkMapMemory(m_device, buffer.memory, 0, buffer.size, 0, &buffer.mapped);
    if (result != VK_SUCCESS) {
        setError("Failed to map buffer memory: " + std::to_string(result));
        return nullptr;
    }
    
    return buffer.mapped;
}

void VulkanMemoryManager::unmapBuffer(VulkanBuffer& buffer) {
    if (!buffer.isValid() || !buffer.mapped || buffer.isPersistentlyMapped) {
        return;
    }
    
    vkUnmapMemory(m_device, buffer.memory);
    buffer.mapped = nullptr;
}

void VulkanMemoryManager::destroyBuffer(VulkanBuffer& buffer) {
    if (!buffer.isValid()) return;
    
    // Unmap if mapped
    if (buffer.mapped && !buffer.isPersistentlyMapped) {
        vkUnmapMemory(m_device, buffer.memory);
    }
    
    // Destroy buffer and free memory
    vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    vkFreeMemory(m_device, buffer.memory, nullptr);
    
    // Reset struct
    buffer = VulkanBuffer{};
}

void VulkanMemoryManager::destroyImage(VulkanImage& image) {
    if (!image.isValid()) return;
    
    // Destroy image view
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, image.view, nullptr);
    }
    
    // Destroy image and free memory
    vkDestroyImage(m_device, image.image, nullptr);
    vkFreeMemory(m_device, image.memory, nullptr);
    
    // Reset struct
    image = VulkanImage{};
}

VkDeviceMemory VulkanMemoryManager::allocateMemory(
    const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags properties) {
    
    uint32_t memoryType = findMemoryType(requirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        setError("Failed to find suitable memory type");
        return VK_NULL_HANDLE;
    }
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryType;
    
    VkDeviceMemory memory;
    VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        setError("Failed to allocate device memory: " + std::to_string(result));
        return VK_NULL_HANDLE;
    }
    
    return memory;
}

uint32_t VulkanMemoryManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    return m_context->findMemoryType(typeFilter, properties);
}

VkCommandBuffer VulkanMemoryManager::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanMemoryManager::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context->getGraphicsQueue());
    
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

bool VulkanMemoryManager::createCommandPool() {
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
    
    return true;
}

std::string VulkanMemoryManager::getMemoryStats() const {
    std::ostringstream oss;
    oss << "Memory Manager Statistics:\n";
    oss << "  Total Allocated: " << (m_totalAllocatedMemory / 1024 / 1024) << " MB\n";
    oss << "  Allocation Count: " << m_allocationCount << "\n";
    oss << "  Active Buffers: " << m_allocatedBuffers.size() << "\n";
    oss << "  Active Images: " << m_allocatedImages.size() << "\n";
    return oss.str();
}

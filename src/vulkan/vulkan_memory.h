/**
 * @file vulkan_memory.h
 * @brief Manual Vulkan memory management without external libraries
 * 
 * This file provides simple but effective Vulkan memory management for the
 * PongCpp renderer. Designed to be self-contained without VMA or other libs.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class VulkanContext; // Forward declaration

/**
 * @brief Vulkan buffer wrapper
 */
struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;           ///< Vulkan buffer handle
    VkDeviceMemory memory = VK_NULL_HANDLE;     ///< Device memory handle
    VkDeviceSize size = 0;                      ///< Buffer size in bytes
    VkBufferUsageFlags usage = 0;               ///< Buffer usage flags
    VkMemoryPropertyFlags properties = 0;       ///< Memory property flags
    void* mapped = nullptr;                     ///< Mapped memory pointer (if host-visible)
    bool isPersistentlyMapped = false;          ///< Whether memory stays mapped
    
    /**
     * @brief Check if buffer is valid
     * @return true if buffer and memory are allocated
     */
    bool isValid() const {
        return buffer != VK_NULL_HANDLE && memory != VK_NULL_HANDLE;
    }
    
    /**
     * @brief Check if buffer is mappable
     * @return true if memory is host-visible
     */
    bool isMappable() const {
        return (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }
    
    /**
     * @brief Check if buffer is device-local
     * @return true if memory is device-local
     */
    bool isDeviceLocal() const {
        return (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    }
};

/**
 * @brief Vulkan image wrapper
 */
struct VulkanImage {
    VkImage image = VK_NULL_HANDLE;             ///< Vulkan image handle
    VkDeviceMemory memory = VK_NULL_HANDLE;     ///< Device memory handle
    VkImageView view = VK_NULL_HANDLE;          ///< Image view handle
    VkFormat format = VK_FORMAT_UNDEFINED;      ///< Image format
    VkExtent2D extent = {0, 0};                 ///< Image dimensions
    uint32_t mipLevels = 1;                     ///< Number of mip levels
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT; ///< Sample count
    
    /**
     * @brief Check if image is valid
     * @return true if image and memory are allocated
     */
    bool isValid() const {
        return image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE;
    }
};

/**
 * @brief Manual Vulkan memory manager
 * 
 * Provides simple memory management for buffers and images without
 * external dependencies. Handles allocation, mapping, and cleanup.
 */
class VulkanMemoryManager {
public:
    /**
     * @brief Initialize memory manager
     * @param context Vulkan context to use
     * @return true if successful
     */
    bool initialize(VulkanContext* context);
    
    /**
     * @brief Cleanup all allocated resources
     */
    void cleanup();
    
    /**
     * @brief Create vertex buffer
     * @param size Buffer size in bytes
     * @param data Initial data (optional)
     * @return Allocated buffer
     */
    VulkanBuffer createVertexBuffer(VkDeviceSize size, const void* data = nullptr);
    
    /**
     * @brief Create index buffer
     * @param size Buffer size in bytes
     * @param data Initial data (optional)
     * @return Allocated buffer
     */
    VulkanBuffer createIndexBuffer(VkDeviceSize size, const void* data = nullptr);
    
    /**
     * @brief Create uniform buffer
     * @param size Buffer size in bytes
     * @param persistentMap Keep memory mapped for frequent updates
     * @return Allocated buffer
     */
    VulkanBuffer createUniformBuffer(VkDeviceSize size, bool persistentMap = true);
    
    /**
     * @brief Create staging buffer for data transfer
     * @param size Buffer size in bytes
     * @return Allocated buffer
     */
    VulkanBuffer createStagingBuffer(VkDeviceSize size);
    
    /**
     * @brief Create 2D image
     * @param width Image width
     * @param height Image height
     * @param format Image format
     * @param usage Image usage flags
     * @param properties Memory property flags
     * @return Allocated image
     */
    VulkanImage createImage2D(
        uint32_t width, uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties
    );
    
    /**
     * @brief Update buffer data
     * @param buffer Buffer to update
     * @param data New data
     * @param size Data size (0 = use buffer size)
     * @param offset Offset in buffer
     * @return true if successful
     */
    bool updateBuffer(VulkanBuffer& buffer, const void* data, VkDeviceSize size = 0, VkDeviceSize offset = 0);
    
    /**
     * @brief Copy data between buffers
     * @param src Source buffer
     * @param dst Destination buffer
     * @param size Size to copy (0 = entire source buffer)
     * @return true if successful
     */
    bool copyBuffer(const VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size = 0);
    
    /**
     * @brief Map buffer memory
     * @param buffer Buffer to map
     * @return Mapped pointer, or nullptr on failure
     */
    void* mapBuffer(VulkanBuffer& buffer);
    
    /**
     * @brief Unmap buffer memory
     * @param buffer Buffer to unmap
     */
    void unmapBuffer(VulkanBuffer& buffer);
    
    /**
     * @brief Destroy buffer and free memory
     * @param buffer Buffer to destroy
     */
    void destroyBuffer(VulkanBuffer& buffer);
    
    /**
     * @brief Destroy image and free memory
     * @param image Image to destroy
     */
    void destroyImage(VulkanImage& image);
    
    /**
     * @brief Begin single-time command buffer
     * @return Command buffer handle
     */
    VkCommandBuffer beginSingleTimeCommands();
    
    /**
     * @brief End and submit single-time command buffer
     * @param commandBuffer Command buffer to submit
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    
    /**
     * @brief Get memory usage statistics
     * @return String with memory usage info
     */
    std::string getMemoryStats() const;
    
    /**
     * @brief Get last error message
     * @return Error string from last operation
     */
    const std::string& getLastError() const { return m_lastError; }
    
private:
    VulkanContext* m_context = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    
    // Command pool for memory operations
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    
    // Track allocated resources
    std::vector<VulkanBuffer> m_allocatedBuffers;
    std::vector<VulkanImage> m_allocatedImages;
    
    // Memory statistics
    VkDeviceSize m_totalAllocatedMemory = 0;
    uint32_t m_allocationCount = 0;
    
    // Error handling
    std::string m_lastError;
    
    /**
     * @brief Create buffer with specified parameters
     * @param size Buffer size
     * @param usage Buffer usage flags
     * @param properties Memory property flags
     * @param data Initial data (optional)
     * @return Allocated buffer
     */
    VulkanBuffer createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        const void* data = nullptr
    );
    
    /**
     * @brief Allocate device memory for buffer or image
     * @param requirements Memory requirements
     * @param properties Desired memory properties
     * @return Allocated memory handle
     */
    VkDeviceMemory allocateMemory(
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags properties
    );
    
    /**
     * @brief Find suitable memory type
     * @param typeFilter Memory type requirements
     * @param properties Desired properties
     * @return Memory type index, or UINT32_MAX if not found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    
    /**
     * @brief Set error message
     * @param error Error string to store
     */
    void setError(const std::string& error) { m_lastError = error; }
    
    /**
     * @brief Create command pool for memory operations
     * @return true if successful
     */
    bool createCommandPool();
};

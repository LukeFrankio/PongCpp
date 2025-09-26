/**
 * @file vulkan_renderer.h
 * @brief Vulkan renderer with swapchain management for PongCpp
 * 
 * This file provides the main Vulkan renderer class that handles swapchain
 * creation, recreation, image acquisition, presentation, and render pass management.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <map>
#include <array>
#include "vulkan_context.h"
#include "vulkan_memory.h"
#include "slang_compiler.h"
#include "vulkan_math.h"

/**
 * @brief Swapchain configuration
 */
struct SwapchainConfig {
    VkExtent2D extent = {800, 600};         ///< Swapchain dimensions
    VkSurfaceFormatKHR surfaceFormat = {};  ///< Color format and color space
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; ///< Presentation mode
    uint32_t imageCount = 2;                ///< Number of swapchain images
    bool vsync = true;                      ///< Enable VSync
};

/**
 * @brief Render batch for efficient drawing
 */
struct RenderBatch {
    std::vector<VulkanMath::Vertex> vertices;      ///< Vertex data
    std::vector<uint32_t> indices;                ///< Index data (optional)
    std::string pipelineType;                     ///< Pipeline to use
    VkPrimitiveTopology topology;                 ///< Primitive topology
    
    void clear() {
        vertices.clear();
        indices.clear();
    }
    
    bool empty() const {
        return vertices.empty();
    }
};

/**
 * @brief Uniform buffer data structures
 */
struct GlobalUniforms {
    VulkanMath::Mat4 projectionMatrix;
    VulkanMath::Mat4 viewMatrix;
    VulkanMath::Mat4 modelMatrix;
    VulkanMath::Vec2 screenSize;
    VulkanMath::Vec2 gameSize;
    float time;
    float deltaTime;
    VulkanMath::Vec2 _padding0;
};

// Material structure matches Slang Material layout (std140-like alignment)
struct MaterialUniform {
    VulkanMath::Vec3 baseColor;    // vec3 -> padded to vec4
    float roughness;
    float metallic;
    float emission;
};

// Shape parameters used by fragment shaders
struct ShapeParamsUniform {
    VulkanMath::Vec2 size;         // vec2
    float cornerRadius;
    float thickness;
    uint32_t type;                 // PrimitiveType
    // pad to 16 bytes boundary
    float _pad0;
};

// Effect parameters (glow, pulse, etc.)
struct EffectParamsUniform {
    float glowIntensity;
    float glowRadius;
    float pulseSpeed;
    float time;
};

struct ObjectUniforms {
    VulkanMath::Mat4 objectTransform;
    MaterialUniform material;
    ShapeParamsUniform shape;
    EffectParamsUniform effects;
    // Ensure 16-byte alignment
    float _padding1[1];
};

/**
 * @brief Graphics pipeline data
 */
struct GraphicsPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;          ///< Pipeline handle
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; ///< Pipeline layout
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE; ///< Descriptor set layout
    std::string vertexShader;                     ///< Vertex shader entry point
    std::string fragmentShader;                   ///< Fragment shader entry point
    
    bool isValid() const {
        return pipeline != VK_NULL_HANDLE && pipelineLayout != VK_NULL_HANDLE;
    }
};

/**
 * @brief Frame-in-flight data
 */
struct FrameData {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;    ///< Command buffer for this frame
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE; ///< Image acquisition semaphore
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE; ///< Render completion semaphore
    VkFence inFlightFence = VK_NULL_HANDLE;            ///< CPU-GPU synchronization fence
    
    bool isValid() const {
        return commandBuffer != VK_NULL_HANDLE && 
               imageAvailableSemaphore != VK_NULL_HANDLE &&
               renderFinishedSemaphore != VK_NULL_HANDLE &&
               inFlightFence != VK_NULL_HANDLE;
    }
};

/**
 * @brief Swapchain image data
 */
struct SwapchainImage {
    VkImage image = VK_NULL_HANDLE;         ///< Swapchain image
    VkImageView imageView = VK_NULL_HANDLE; ///< Image view for rendering
    VkFramebuffer framebuffer = VK_NULL_HANDLE; ///< Framebuffer for render pass
};

/**
 * @brief Main Vulkan renderer class
 * 
 * Handles all aspects of Vulkan rendering including:
 * - Swapchain creation and management
 * - Render pass and framebuffer setup
 * - Command buffer recording and submission
 * - Frame synchronization
 * - Window resize handling
 */
class VulkanRenderer {
public:
    /**
     * @brief Initialize the Vulkan renderer
     * @param context Initialized Vulkan context
     * @param memoryManager Initialized memory manager
     * @param compiler Initialized Slang compiler
     * @param initialWidth Initial window width
     * @param initialHeight Initial window height
     * @return true if successful
     */
    bool initialize(
        VulkanContext* context,
        VulkanMemoryManager* memoryManager,
        SlangCompiler* compiler,
        uint32_t initialWidth,
        uint32_t initialHeight
    );
    
    /**
     * @brief Cleanup and shutdown renderer
     */
    void cleanup();
    
    /**
     * @brief Handle window resize
     * @param newWidth New window width
     * @param newHeight New window height
     * @return true if successful
     */
    bool resize(uint32_t newWidth, uint32_t newHeight);
    
    /**
     * @brief Begin a new frame
     * @return true if frame can be rendered
     */
    bool beginFrame();
    
    /**
     * @brief End current frame and present
     * @return true if successful
     */
    bool endFrame();
    
    /**
     * @brief Debug: Sample pixel colors at specific locations and log to console
     * @param frameNumber Current frame number for debug output
     */
    void debugSamplePixelColors(int frameNumber);
    
    /**
     * @brief Get current command buffer for recording
     * @return Active command buffer
     */
    VkCommandBuffer getCurrentCommandBuffer() const;
    
    /**
     * @brief Get current render pass
     * @return Active render pass
     */
    VkRenderPass getRenderPass() const { return m_renderPass; }
    
    /**
     * @brief Get swapchain extent
     * @return Current swapchain dimensions
     */
    VkExtent2D getSwapchainExtent() const { return m_swapchainConfig.extent; }
    
    /**
     * @brief Get swapchain format
     * @return Current swapchain format
     */
    VkFormat getSwapchainFormat() const { return m_swapchainConfig.surfaceFormat.format; }
    
    /**
     * @brief Check if renderer is initialized
     * @return true if ready for rendering
     */
    bool isInitialized() const { return m_swapchain != VK_NULL_HANDLE; }
    
    /**
     * @brief Enable/disable VSync
     * @param enable True to enable VSync
     */
    void setVSync(bool enable);
    
    /**
     * @brief Get current frame index
     * @return Frame index (0 to MAX_FRAMES_IN_FLIGHT-1)
     */
    uint32_t getCurrentFrameIndex() const { return m_currentFrame; }
    
    /**
     * @brief Get last error message
     * @return Error string from last operation
     */
    const std::string& getLastError() const { return m_lastError; }
    
    /**
     * @brief Bind graphics pipeline for rendering
     * @param pipelineType Type of pipeline to bind
     */
    void bindPipeline(const std::string& pipelineType);
    
    /**
     * @brief Get pipeline layout for descriptor sets
     * @param pipelineType Type of pipeline
     * @return Pipeline layout handle
     */
    VkPipelineLayout getPipelineLayout(const std::string& pipelineType) const;
    
    /**
     * @brief Draw a rectangle
     * @param center Rectangle center in game coordinates
     * @param size Rectangle dimensions in game coordinates
     * @param color Rectangle color
     * @param pipelineType Pipeline to use for rendering
     */
    void drawRectangle(
        VulkanMath::Vec2 center,
        VulkanMath::Vec2 size,
        VulkanMath::Vec3 color,
        const std::string& pipelineType = "solid"
    );
    
    /**
     * @brief Draw a circle
     * @param center Circle center in game coordinates
     * @param radius Circle radius in game coordinates
     * @param color Circle color
     * @param segments Number of segments (quality)
     * @param pipelineType Pipeline to use for rendering
     */
    void drawCircle(
        VulkanMath::Vec2 center,
        float radius,
        VulkanMath::Vec3 color,
        uint32_t segments = 32,
        const std::string& pipelineType = "solid"
    );
    
    /**
     * @brief Draw a line
     * @param start Line start point in game coordinates
     * @param end Line end point in game coordinates
     * @param thickness Line thickness
     * @param color Line color
     * @param pipelineType Pipeline to use for rendering
     */
    void drawLine(
        VulkanMath::Vec2 start,
        VulkanMath::Vec2 end,
        float thickness,
        VulkanMath::Vec3 color,
        const std::string& pipelineType = "solid"
    );
    
    /**
     * @brief Flush all pending draw calls
     */
    void flushBatches();
    
    /**
     * @brief Set game coordinate system
     * @param gameWidth Width of game world
     * @param gameHeight Height of game world
     */
    void setGameCoordinates(float gameWidth, float gameHeight);
    
    /**
     * @brief Update time for animations
     * @param currentTime Current time in seconds
     * @param deltaTime Frame delta time
     */
    void updateTime(float currentTime, float deltaTime);
    
    /**
     * @brief Draw raw triangles with NDC coordinates (for debugging)
     * @param vertices Triangle vertices in NDC space (-1 to +1)
     * @param indices Triangle indices
     * @param pipelineType Pipeline to use
     */
    void drawRawTriangles(
        const std::vector<VulkanMath::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const std::string& pipelineType = "solid"
    );
    
private:
    // Constants
    static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    // Core Vulkan objects
    VulkanContext* m_context = nullptr;
    VulkanMemoryManager* m_memoryManager = nullptr;
    SlangCompiler* m_compiler = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    
    // Swapchain objects
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<SwapchainImage> m_swapchainImages;
    SwapchainConfig m_swapchainConfig;
    
    // Render pass and synchronization
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<FrameData> m_frames;
    
    // Graphics pipelines
    std::map<std::string, GraphicsPipeline> m_pipelines;
    std::string m_currentPipelineType;
    
    // Rendering batches
    std::map<std::string, RenderBatch> m_batches;
    
    // Vertex and index buffers
    VulkanBuffer m_vertexBuffer;
    VulkanBuffer m_indexBuffer;
    uint32_t m_maxVertices = 1000;     ///< Maximum vertices per frame  
    uint32_t m_maxIndices = 3000;      ///< Maximum indices per frame
    
    // Uniform buffers
    std::vector<VulkanBuffer> m_globalUniformBuffers;  ///< Per-frame global uniforms
    std::vector<VulkanBuffer> m_objectUniformBuffers;  ///< Per-frame object uniforms
    
    // Descriptor sets
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<std::vector<VkDescriptorSet>> m_descriptorSets; ///< [frame][pipeline]
    
    // Game coordinate system
    float m_gameWidth = 80.0f;
    float m_gameHeight = 24.0f;
    GlobalUniforms m_globalUniforms;
    ObjectUniforms m_objectUniforms;
    
    // Frame management
    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex = 0;
    bool m_framebufferResized = false;
    
    // Error handling
    std::string m_lastError;
    
    /**
     * @brief Create swapchain
     * @return true if successful
     */
    bool createSwapchain();
    
    /**
     * @brief Recreate swapchain (for resize)
     * @return true if successful
     */
    bool recreateSwapchain();
    
    /**
     * @brief Cleanup swapchain objects
     */
    void cleanupSwapchain();
    
    /**
     * @brief Create image views for swapchain images
     * @return true if successful
     */
    bool createImageViews();
    
    /**
     * @brief Create render pass
     * @return true if successful
     */
    bool createRenderPass();
    
    /**
     * @brief Create framebuffers
     * @return true if successful
     */
    bool createFramebuffers();
    
    /**
     * @brief Create command pool and buffers
     * @return true if successful
     */
    bool createCommandObjects();
    
    /**
     * @brief Create synchronization objects
     * @return true if successful
     */
    bool createSyncObjects();
    
    /**
     * @brief Choose swapchain surface format
     * @param availableFormats Available surface formats
     * @return Best surface format
     */
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats
    ) const;
    
    /**
     * @brief Choose swapchain present mode
     * @param availablePresentModes Available present modes
     * @return Best present mode
     */
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes
    ) const;
    
    /**
     * @brief Choose swapchain extent
     * @param capabilities Surface capabilities
     * @return Optimal swapchain extent
     */
    VkExtent2D chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities
    ) const;
    
    /**
     * @brief Begin render pass for current frame
     */
    void beginRenderPass();
    
    /**
     * @brief End render pass for current frame
     */
    void endRenderPass();
    
    /**
     * @brief Create graphics pipelines
     * @return true if successful
     */
    bool createGraphicsPipelines();
    
    /**
     * @brief Create a specific graphics pipeline
     * @param name Pipeline name/type
     * @param vertexShaderEntry Vertex shader entry point
     * @param fragmentShaderEntry Fragment shader entry point
     * @return true if successful
     */
    bool createPipeline(
        const std::string& name,
        const std::string& vertexShaderEntry,
        const std::string& fragmentShaderEntry
    );
    
    /**
     * @brief Create minimal pipeline without uniform buffers
     * @param name Pipeline name
     * @param vertexShaderEntry Vertex shader entry point
     * @param fragmentShaderEntry Fragment shader entry point
     * @return True if successful
     */
    bool createMinimalPipeline(
        const std::string& name,
        const std::string& vertexShaderEntry,
        const std::string& fragmentShaderEntry
    );
    
    /**
     * @brief Create shader module from SPIR-V
     * @param spirvCode SPIR-V bytecode
     * @return Shader module handle
     */
    VkShaderModule createShaderModule(const std::vector<uint8_t>& spirvCode);
    
    /**
     * @brief Cleanup pipelines
     */
    void cleanupPipelines();
    
    /**
     * @brief Create vertex and index buffers
     * @return true if successful
     */
    bool createVertexBuffers();
    
    /**
     * @brief Create uniform buffers
     * @return true if successful
     */
    bool createUniformBuffers();
    
    /**
     * @brief Create descriptor pool and sets
     * @return true if successful
     */
    bool createDescriptorSets();
    
    /**
     * @brief Update uniform buffers for current frame
     */
    void updateUniformBuffers();
    
    /**
     * @brief Add vertices to a batch
     * @param pipelineType Pipeline to use
     * @param vertices Vertices to add
     * @param indices Indices to add (optional)
     * @param topology Primitive topology
     */
    void addToBatch(
        const std::string& pipelineType,
        const std::vector<VulkanMath::Vertex>& vertices,
        const std::vector<uint32_t>& indices = {},
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    );
    
    /**
     * @brief Render a specific batch
     * @param batch Batch to render
     */
    void renderBatch(const RenderBatch& batch);
    
    /**
     * @brief Transform point from game coordinates to NDC
     * @param gamePos Position in game coordinates
     * @return Position in normalized device coordinates
     */
    VulkanMath::Vec2 gameToNDC(VulkanMath::Vec2 gamePos) const;
    
    /**
     * @brief Generate rectangle vertices
     * @param center Rectangle center
     * @param size Rectangle size
     * @param color Rectangle color
     * @return Array of 6 vertices (2 triangles)
     */
    std::vector<VulkanMath::Vertex> generateRectangleVertices(
        VulkanMath::Vec2 center,
        VulkanMath::Vec2 size,
        VulkanMath::Vec3 color
    ) const;
    
    /**
     * @brief Generate circle vertices
     * @param center Circle center
     * @param radius Circle radius
     * @param color Circle color
     * @param segments Number of segments
     * @return Vector of vertices
     */
    std::vector<VulkanMath::Vertex> generateCircleVertices(
        VulkanMath::Vec2 center,
        float radius,
        VulkanMath::Vec3 color,
        uint32_t segments
    ) const;
    
    /**
     * @brief Generate circle indices for triangle fan
     * @param segments Number of segments
     * @return Vector of indices
     */
    std::vector<uint32_t> generateCircleIndices(uint32_t segments) const;
    
    /**
     * @brief Set error message
     * @param error Error string to store
     */
    void setError(const std::string& error) { m_lastError = error; }
};

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include "../math/vec3.h"
#include "../voxels/world_constants.h"
#include "../voxels/tree27.h"

namespace chonk {
namespace voxels {
    class Chunk;
    class ChunkArray;
    struct GPUChunkUpdate;
}

namespace graphics {

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool initialize(GLFWwindow* window);
    void shutdown();
    
    // Render with GPU update from ChunkManager (non-blocking, preferred method)
    void render(const voxels::GPUChunkUpdate* gpuUpdate,
                const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                const math::Vec3& cameraUp);
    
    // Render with chunk array (legacy, may block)
    void render(const voxels::ChunkArray& chunkArray,
                const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                const math::Vec3& cameraUp);
    
    // Legacy render with chunk list
    void render(const std::vector<const voxels::Chunk*>& chunks, 
                const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                const math::Vec3& cameraUp);
    
    void resize(uint32_t width, uint32_t height);
    
    // Check if renderer has valid chunk data to render
    bool hasValidChunkData() const { return gpuBuffersValid; }
    
    // Text overlay API
    void setDebugText(const std::vector<std::string>& texts, const std::vector<std::pair<int, int>>& positions);
    void setDebugInfo(float fps, bool showText, bool showCrosshair, float textScale = 2.0f);
    
    // Debug mode: 0=normal, 1=LOD colors, 2=chunk grid
    void setDebugMode(uint32_t mode) { debugMode = mode; }
    
private:
    GLFWwindow* window;
    
    // Vulkan objects
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue presentQueue;
    uint32_t graphicsQueueFamily;
    
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    
    VkImage storageImage;
    VkDeviceMemory storageImageMemory;
    VkImageView storageImageView;
    
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    
    // Compute pipeline
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkPipelineLayout computePipelineLayout;
    VkPipeline computePipeline;
    
    // Chunk data buffers
    VkBuffer chunkDataBuffer;           // All nodes concatenated
    VkDeviceMemory chunkDataBufferMemory;
    VkBuffer chunkInfoBuffer;           // ChunkMeta array (17³ = 4913 entries)
    VkDeviceMemory chunkInfoBufferMemory;
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    
    // Text overlay pipeline
    VkDescriptorSetLayout textOverlayDescriptorSetLayout;
    VkDescriptorPool textOverlayDescriptorPool;
    VkDescriptorSet textOverlayDescriptorSet;
    VkPipelineLayout textOverlayPipelineLayout;
    VkPipeline textOverlayPipeline;
    
    // Font texture
    VkImage fontTexture;
    VkDeviceMemory fontTextureMemory;
    VkImageView fontTextureView;
    VkSampler fontSampler;
    
    // Text buffers
    VkBuffer debugInfoBuffer;
    VkDeviceMemory debugInfoBufferMemory;
    VkBuffer textBuffer;
    VkDeviceMemory textBufferMemory;
    VkBuffer charBuffer;
    VkDeviceMemory charBufferMemory;
    
    struct DebugInfo {
        float cameraPos[4];
        float fps;
        float textScale;
        uint32_t textEnabled;
        uint32_t crosshairEnabled;
        uint32_t _pad1;
        uint32_t _pad2;
    };
    
    // TextEntry must match GLSL std430 layout
    struct TextEntry {
        int32_t position[2];     // 8 bytes at offset 0
        uint32_t charCount;      // 4 bytes at offset 8
        uint32_t charStartIndex; // 4 bytes at offset 12
        uint32_t _pad1;          // 4 bytes at offset 16
        uint32_t _pad2;          // 4 bytes at offset 20
    };
    
    // ========================================================================
    // GPU Structures (must match shader layout)
    // ========================================================================
    
    // ChunkMeta for 17³ array (matches shader)
    struct ChunkMeta {
        uint32_t nodeOffset;      // Start index in global node array
        uint32_t nodeCount;       // Number of nodes for this chunk
        uint32_t maxDepth;        // LOD depth limit
        uint32_t rootMaterial;    // For quick air check (0 = air)
    };
    
    // Uniform buffer data - must match GLSL std140 layout
    struct UniformBufferData {
        float cameraPos[4];       // xyz = position (offset 0)
        float cameraDir[4];       // xyz = direction (offset 16)
        float cameraUp[4];        // xyz = up vector (offset 32)
        float cameraRightFov[4];  // xyz = right, w = fov (offset 48)
        float arrayOrigin[4];     // xyz = world pos of chunk[0,0,0], w = chunk size (offset 64)
        float sunDir[4];          // xyz = sun direction (offset 80)
        uint32_t chunkCount;      // Number of loaded chunks (offset 96)
        uint32_t debugMode;       // 0=normal, 1=LOD, 2=grid (offset 100)
        uint32_t _pad1;           // (offset 104)
        uint32_t _pad2;           // (offset 108)
    };
    
    // State
    bool framebufferResized;
    uint32_t debugMode = 0;
    math::Vec3 lastArrayOrigin{0, 0, 0};
    
    // Buffer caching
    size_t lastChunkCount = 0;
    size_t lastTotalNodes = 0;
    VkDeviceSize currentChunkDataBufferSize = 0;  // Track allocated size
    VkDeviceSize currentChunkMetaBufferSize = 0;  // Track meta buffer size
    
    // GPU buffer state
    bool gpuBuffersValid = false;
    uint64_t lastGPUUpdateId = 0;
    
    // Pre-allocated buffer sizes (can grow but not shrink)
    static constexpr VkDeviceSize INITIAL_NODE_BUFFER_SIZE = 50 * 1024 * 1024;  // 50MB initial
    static constexpr VkDeviceSize META_BUFFER_SIZE = voxels::CHUNK_ARRAY_TOTAL * sizeof(ChunkMeta);
    
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void recreateSwapchain();
    void createImageViews();
    void createStorageImage();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSet();
    void createComputePipeline();
    void createCommandPool();
    void allocateCommandBuffers();
    void createSyncObjects();
    void createUniformBuffer();
    
    // Pre-allocate GPU buffers (called once during init or when size needs to grow)
    void ensureChunkBuffersAllocated(VkDeviceSize requiredNodeSize);
    
    // Update GPU buffers from GPUChunkUpdate (non-blocking, just memcpy)
    void updateChunkBuffersFromUpdate(const voxels::GPUChunkUpdate& update);
    
    // New chunk array buffer creation/update
    void createChunkBuffers(const voxels::ChunkArray& chunkArray);
    void updateChunkBuffers(const voxels::ChunkArray& chunkArray);
    
    // Helper to collect chunk data
    static void collectChunkData(const voxels::ChunkArray& chunkArray,
                                  std::vector<voxels::Tree27Node>& allNodes,
                                  std::vector<ChunkMeta>& chunkMetas);
    
    // Legacy chunk buffer creation
    void createChunkBuffers(const std::vector<const voxels::Chunk*>& chunks, const math::Vec3& cameraPos);
    
    void updateUniformBuffer(const math::Vec3& cameraPos, const math::Vec3& cameraDir, 
                             const math::Vec3& cameraUp, const math::Vec3& arrayOrigin);
    
    // Text overlay methods
    void createFontTexture();
    void createTextOverlayPipeline();
    void createTextBuffers();
    void updateTextBuffers(const std::vector<std::string>& texts, const std::vector<std::pair<int, int>>& positions);
    void updateDebugInfo(float fps, bool showText, bool showCrosshair, float textScale);
    void updateTextOverlayDescriptorSet();
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void transitionImageLayoutInCommandBuffer(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace graphics
} // namespace chonk

#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include "../math/vec3.h"

namespace chonk {
namespace voxels {
    class Chunk;
}

namespace graphics {

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool initialize(GLFWwindow* window);
    void shutdown();
    
    void render(const std::vector<const voxels::Chunk*>& chunks, 
                const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                const math::Vec3& cameraUp);
    
    void resize(uint32_t width, uint32_t height);
    
    // Text overlay API
    void setDebugText(const std::vector<std::string>& texts, const std::vector<std::pair<int, int>>& positions);
    void setDebugInfo(float fps, bool showText, bool showCrosshair, float textScale = 2.0f);
    
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
    VkBuffer chunkDataBuffer;
    VkDeviceMemory chunkDataBufferMemory;
    VkBuffer chunkInfoBuffer;
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
    // ivec2 has 8-byte alignment, so struct has 8-byte alignment = 24 bytes total
    struct TextEntry {
        int32_t position[2];     // 8 bytes at offset 0
        uint32_t charCount;      // 4 bytes at offset 8
        uint32_t charStartIndex; // 4 bytes at offset 12
        uint32_t _pad1;          // 4 bytes at offset 16
        uint32_t _pad2;          // 4 bytes at offset 20 (to reach 24 byte alignment)
    };
    
    // Uniform buffer data - must match GLSL std140 layout!
    // Use vec4 equivalents to avoid std140 alignment complexity
    // In std140, arrays of floats have 16-byte stride per element!
    struct UniformBufferData {
        float cameraPos[4];     // xyz = position, w = unused (offset 0)
        float cameraDir[4];     // xyz = direction, w = unused (offset 16)
        float cameraUp[4];      // xyz = up vector, w = unused (offset 32)
        float cameraRight[4];   // xyz = right vector, w = fov (offset 48)
        uint32_t chunkCount;    // offset 64
        uint32_t _pad1;         // offset 68
        uint32_t _pad2;         // offset 72
        uint32_t _pad3;         // offset 76
        float sunDir[4];        // xyz = sun direction, w = unused (offset 80)
    };
    
    bool framebufferResized;
    
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
    void createChunkBuffers(const std::vector<const voxels::Chunk*>& chunks);
    void updateUniformBuffer(const math::Vec3& cameraPos, const math::Vec3& cameraDir, const math::Vec3& cameraUp);
    
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


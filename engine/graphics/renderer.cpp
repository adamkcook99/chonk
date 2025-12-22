#include "renderer.h"
#include "../voxels/chunk.h"
#include "../voxels/chunk_array.h"
#include "../voxels/chunk_manager.h"
#include "../voxels/tree27.h"
#include "../voxels/world_constants.h"
#include "../math/vec3.h"
#include "../core/logging.h"
#include <fstream>
#include <cstring>
#include <limits>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __APPLE__
extern "C" void* getMetalLayer(GLFWwindow* window);

typedef VkFlags VkMetalSurfaceCreateFlagsEXT;
typedef struct VkMetalSurfaceCreateInfoEXT {
    VkStructureType               sType;
    const void*                   pNext;
    VkMetalSurfaceCreateFlagsEXT  flags;
    const void*                   pLayer;
} VkMetalSurfaceCreateInfoEXT;
typedef VkResult (VKAPI_PTR *PFN_vkCreateMetalSurfaceEXT)(VkInstance instance, const VkMetalSurfaceCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
#define VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT ((VkStructureType)1000217000)
#endif

namespace chonk {
namespace graphics {

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
    size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    file.close();
    return buffer;
}

Renderer::Renderer() : window(nullptr), instance(VK_NULL_HANDLE), surface(VK_NULL_HANDLE),
    physicalDevice(VK_NULL_HANDLE), device(VK_NULL_HANDLE), graphicsQueue(VK_NULL_HANDLE),
    computeQueue(VK_NULL_HANDLE), presentQueue(VK_NULL_HANDLE), swapchain(VK_NULL_HANDLE),
    storageImage(VK_NULL_HANDLE), storageImageMemory(VK_NULL_HANDLE), storageImageView(VK_NULL_HANDLE),
    commandPool(VK_NULL_HANDLE), imageAvailableSemaphore(VK_NULL_HANDLE),
    renderFinishedSemaphore(VK_NULL_HANDLE), descriptorSetLayout(VK_NULL_HANDLE),
    descriptorPool(VK_NULL_HANDLE), descriptorSet(VK_NULL_HANDLE),
    computePipelineLayout(VK_NULL_HANDLE), computePipeline(VK_NULL_HANDLE),
    chunkDataBuffer(VK_NULL_HANDLE), chunkDataBufferMemory(VK_NULL_HANDLE),
    chunkInfoBuffer(VK_NULL_HANDLE), chunkInfoBufferMemory(VK_NULL_HANDLE),
    uniformBuffer(VK_NULL_HANDLE), uniformBufferMemory(VK_NULL_HANDLE),
    textOverlayDescriptorSetLayout(VK_NULL_HANDLE), textOverlayDescriptorPool(VK_NULL_HANDLE),
    textOverlayDescriptorSet(VK_NULL_HANDLE), textOverlayPipelineLayout(VK_NULL_HANDLE),
    textOverlayPipeline(VK_NULL_HANDLE), fontTexture(VK_NULL_HANDLE),
    fontTextureMemory(VK_NULL_HANDLE), fontTextureView(VK_NULL_HANDLE), fontSampler(VK_NULL_HANDLE),
    debugInfoBuffer(VK_NULL_HANDLE), debugInfoBufferMemory(VK_NULL_HANDLE),
    textBuffer(VK_NULL_HANDLE), textBufferMemory(VK_NULL_HANDLE),
    charBuffer(VK_NULL_HANDLE), charBufferMemory(VK_NULL_HANDLE),
    graphicsQueueFamily(0), framebufferResized(false) {
}

Renderer::~Renderer() {
    shutdown();
}

void Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->framebufferResized = true;
    }
}

bool Renderer::initialize(GLFWwindow* win) {
    window = win;
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    
    try {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createCommandPool();
        createStorageImage();
        createDescriptorSetLayout();
        createDescriptorPool();
        createDescriptorSet();
        createUniformBuffer(); // Create uniform buffer during initialization
        createComputePipeline();
        createFontTexture();
        createTextOverlayPipeline();
        createTextBuffers();
        allocateCommandBuffers();
        createSyncObjects();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize renderer: " + std::string(e.what()));
        return false;
    }
}

void Renderer::shutdown() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        
        // Text overlay cleanup
        if (charBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, charBufferMemory, nullptr);
            charBuffer = VK_NULL_HANDLE;
        }
        if (charBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, charBuffer, nullptr);
            charBuffer = VK_NULL_HANDLE;
        }
        if (textBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, textBufferMemory, nullptr);
            textBuffer = VK_NULL_HANDLE;
        }
        if (textBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, textBuffer, nullptr);
            textBuffer = VK_NULL_HANDLE;
        }
        if (debugInfoBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, debugInfoBufferMemory, nullptr);
            debugInfoBuffer = VK_NULL_HANDLE;
        }
        if (debugInfoBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, debugInfoBuffer, nullptr);
            debugInfoBuffer = VK_NULL_HANDLE;
        }
        if (fontSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, fontSampler, nullptr);
        }
        if (fontTextureView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, fontTextureView, nullptr);
        }
        if (fontTexture != VK_NULL_HANDLE) {
            vkDestroyImage(device, fontTexture, nullptr);
        }
        if (fontTextureMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, fontTextureMemory, nullptr);
        }
        if (textOverlayPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, textOverlayPipeline, nullptr);
        }
        if (textOverlayPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, textOverlayPipelineLayout, nullptr);
        }
        if (textOverlayDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, textOverlayDescriptorPool, nullptr);
        }
        if (textOverlayDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, textOverlayDescriptorSetLayout, nullptr);
        }
        
        if (uniformBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, uniformBufferMemory, nullptr);
            uniformBuffer = VK_NULL_HANDLE;
        }
        if (uniformBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, uniformBuffer, nullptr);
            uniformBuffer = VK_NULL_HANDLE;
        }
        
        if (chunkInfoBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, chunkInfoBufferMemory, nullptr);
            chunkInfoBuffer = VK_NULL_HANDLE;
        }
        if (chunkInfoBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, chunkInfoBuffer, nullptr);
            chunkInfoBuffer = VK_NULL_HANDLE;
        }
        
        if (chunkDataBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, chunkDataBufferMemory, nullptr);
            chunkDataBuffer = VK_NULL_HANDLE;
        }
        if (chunkDataBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, chunkDataBuffer, nullptr);
            chunkDataBuffer = VK_NULL_HANDLE;
        }
        
        vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyPipeline(device, computePipeline, nullptr);
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        if (storageImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, storageImageView, nullptr);
        }
        if (storageImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, storageImage, nullptr);
        }
        if (storageImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, storageImageMemory, nullptr);
        }
        for (auto view : swapchainImageViews) vkDestroyImageView(device, view, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

void Renderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "CHONK";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "CHONK Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    
    std::vector<const char*> extensions;
    if (glfwExtensionCount > 0) {
        extensions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
    } else {
        extensions.push_back("VK_KHR_surface");
        extensions.push_back("VK_EXT_metal_surface");
    }
    
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    
    bool portabilityEnumerationAvailable = false;
    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            portabilityEnumerationAvailable = true;
        }
    }
    
    if (portabilityEnumerationAvailable) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void Renderer::createSurface() {
#ifdef __APPLE__
    PFN_vkCreateMetalSurfaceEXT vkCreateMetalSurfaceEXT = 
        (PFN_vkCreateMetalSurfaceEXT)vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT");
    if (!vkCreateMetalSurfaceEXT) {
        throw std::runtime_error("vkCreateMetalSurfaceEXT not available");
    }
    void* metalLayer = getMetalLayer(window);
    if (!metalLayer) {
        throw std::runtime_error("failed to get Metal layer from GLFW window");
    }
    VkMetalSurfaceCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;
    createInfo.pLayer = metalLayer;
    if (vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create metal surface");
    }
#else
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface");
    }
#endif
}

void Renderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) throw std::runtime_error("No Vulkan GPU available");
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0];
}

void Renderer::createLogicalDevice() {
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"
    };
    
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &queuePriority;
    
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qci;
    di.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    di.ppEnabledExtensionNames = deviceExtensions.data();
    
    if (vkCreateDevice(physicalDevice, &di, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device");
    }
    
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
    presentQueue = graphicsQueue;
    computeQueue = graphicsQueue;
    graphicsQueueFamily = 0;
}

void Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    
    swapchainExtent = caps.currentExtent;
    if (swapchainExtent.width == (uint32_t)-1) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        swapchainExtent.width = width;
        swapchainExtent.height = height;
    }
    
    swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = caps.minImageCount + 1;
    sci.imageFormat = swapchainFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = swapchainExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    
    if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain");
    }
    
    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());
}

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    
    vkDeviceWaitIdle(device);
    
    for (auto view : swapchainImageViews) vkDestroyImageView(device, view, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    
    createSwapchain();
    createImageViews();
    allocateCommandBuffers();
}

void Renderer::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        VkImageViewCreateInfo iv{};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = swapchainImages[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = swapchainFormat;
        iv.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel = 0;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device, &iv, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image view");
        }
    }
}

void Renderer::createStorageImage() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &storageImage) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage image");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, storageImage, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &storageImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate storage image memory");
    }
    
    vkBindImageMemory(device, storageImage, storageImageMemory, 0);
    
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = storageImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &storageImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create storage image view");
    }
    
    transitionImageLayout(storageImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}

void Renderer::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings(4);
    
    // Binding 0: Storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 1: Chunk data buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 2: Chunk info buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 3: Uniform buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = static_cast<uint32_t>(bindings.size());
    dslci.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }
}

void Renderer::createDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes(4);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 2;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    dpci.pPoolSizes = poolSizes.data();
    
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }
}

void Renderer::createDescriptorSet() {
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &dsai, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set");
    }
    
    // Will be updated in createChunkBuffers
}

void Renderer::createUniformBuffer() {
    // Create uniform buffer during initialization (not in createChunkBuffers)
    if (uniformBuffer != VK_NULL_HANDLE) {
        return; // Already created
    }
    
    VkDeviceSize uniformBufferSize = sizeof(UniformBufferData);
    VkBufferCreateInfo uniformBufferCreateInfo{};
    uniformBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    uniformBufferCreateInfo.size = uniformBufferSize;
    uniformBufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uniformBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &uniformBufferCreateInfo, nullptr, &uniformBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create uniform buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, uniformBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &uniformBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate uniform buffer memory");
    }
    
    vkBindBufferMemory(device, uniformBuffer, uniformBufferMemory, 0);
    
    // Initialize with default values
    UniformBufferData uboData{};
    std::memset(&uboData, 0, sizeof(UniformBufferData));
    uboData.cameraRightFov[3] = 0.5f;  // FOV is packed in w component
    uboData.sunDir[0] = 0.0f;
    uboData.sunDir[1] = 1.0f;  // Sun pointing up
    uboData.sunDir[2] = 0.0f;
    uboData.sunDir[3] = 0.0f;
    
    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
    std::memcpy(data, &uboData, sizeof(UniformBufferData));
    vkUnmapMemory(device, uniformBufferMemory);
}

void Renderer::createComputePipeline() {
    // Try multiple paths to find the compiled shader
    std::vector<std::string> shaderPaths = {
        "shaders/raytrace.comp.spv",           // Current directory
        "../shaders/raytrace.comp.spv",        // Parent directory
        "../../shaders/raytrace.comp.spv",     // Build root
        "../../../shaders/raytrace.comp.spv"    // Further up
    };
    
    std::string shaderPath;
    bool found = false;
    for (const auto& path : shaderPaths) {
        std::ifstream testFile(path);
        if (testFile.good()) {
            shaderPath = path;
            found = true;
            break;
        }
    }
    
    if (!found) {
        throw std::runtime_error("Failed to find compiled shader raytrace.comp.spv. Make sure shaders are compiled.");
    }
    
    auto computeCode = readFile(shaderPath);
    VkShaderModule computeModule = createShaderModule(computeCode);
    
    VkPipelineShaderStageCreateInfo computeStage{};
    computeStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeStage.module = computeModule;
    computeStage.pName = "main";
    
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(device, &plci, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout");
    }
    
    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = computeStage;
    cpci.layout = computePipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline");
    }
    
    vkDestroyShaderModule(device, computeModule, nullptr);
}

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = graphicsQueueFamily;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &cpci, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }
}

void Renderer::allocateCommandBuffers() {
    commandBuffers.resize(swapchainImages.size());
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    if (vkAllocateCommandBuffers(device, &cbai, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(device, &sci, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &sci, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphores");
    }
}

void Renderer::createChunkBuffers(const std::vector<const voxels::Chunk*>& chunks, const math::Vec3& cameraPos) {
    // Destroy old buffers if they exist (but only chunk buffers, not uniform buffer)
    if (chunkDataBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunkDataBufferMemory, nullptr);
        chunkDataBufferMemory = VK_NULL_HANDLE;
    }
    if (chunkDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunkDataBuffer, nullptr);
        chunkDataBuffer = VK_NULL_HANDLE;
    }
    if (chunkInfoBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunkInfoBufferMemory, nullptr);
        chunkInfoBufferMemory = VK_NULL_HANDLE;
    }
    if (chunkInfoBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunkInfoBuffer, nullptr);
        chunkInfoBuffer = VK_NULL_HANDLE;
    }
    
    // Handle empty chunks - create minimal buffers
    if (chunks.empty()) {
        // Create empty chunk info buffer (at least 1 element for shader)
        struct ChunkInfo {
            float aabbMin[4];
            float aabbMax[4];
            uint32_t nodeOffset;
            uint32_t nodeCount;
            uint32_t visible;
            uint32_t padding;
        };
        
        ChunkInfo emptyChunkInfo{};
        std::memset(&emptyChunkInfo, 0, sizeof(ChunkInfo));
        
        VkDeviceSize infoBufferSize = sizeof(ChunkInfo);
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = infoBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkInfoBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create empty chunk info buffer");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, chunkInfoBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkInfoBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate empty chunk info buffer memory");
        }
        
        vkBindBufferMemory(device, chunkInfoBuffer, chunkInfoBufferMemory, 0);
        
        void* data;
        vkMapMemory(device, chunkInfoBufferMemory, 0, infoBufferSize, 0, &data);
        std::memcpy(data, &emptyChunkInfo, infoBufferSize);
        vkUnmapMemory(device, chunkInfoBufferMemory);
        
        // Create empty chunk data buffer
        VkDeviceSize dataBufferSize = sizeof(voxels::Tree27Node); // At least 1 node
        bufferInfo.size = dataBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkDataBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create empty chunk data buffer");
        }
        
        vkGetBufferMemoryRequirements(device, chunkDataBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkDataBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate empty chunk data buffer memory");
        }
        
        vkBindBufferMemory(device, chunkDataBuffer, chunkDataBufferMemory, 0);
        
        // Initialize with empty node
        voxels::Tree27Node emptyNode{};
        std::memset(&emptyNode, 0, sizeof(voxels::Tree27Node));
        vkMapMemory(device, chunkDataBufferMemory, 0, dataBufferSize, 0, &data);
        std::memcpy(data, &emptyNode, dataBufferSize);
        vkUnmapMemory(device, chunkDataBufferMemory);
        
        // Update descriptor set with empty buffers
        std::vector<VkWriteDescriptorSet> descriptorWrites(4);
        
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = storageImageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfo;
        
        VkDescriptorBufferInfo chunkDataBufferInfo{};
        chunkDataBufferInfo.buffer = chunkDataBuffer;
        chunkDataBufferInfo.offset = 0;
        chunkDataBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &chunkDataBufferInfo;
        
        VkDescriptorBufferInfo chunkInfoBufferInfo{};
        chunkInfoBufferInfo.buffer = chunkInfoBuffer;
        chunkInfoBufferInfo.offset = 0;
        chunkInfoBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &chunkInfoBufferInfo;
        
        VkDescriptorBufferInfo uniformBufferDescriptorInfo{};
        uniformBufferDescriptorInfo.buffer = uniformBuffer;
        uniformBufferDescriptorInfo.offset = 0;
        uniformBufferDescriptorInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSet;
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &uniformBufferDescriptorInfo;
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        return; // Done with empty buffers
    }
    
    // Collect all tree nodes
    size_t totalNodes = 0;
    std::vector<uint32_t> nodeOffsets;
    for (const auto* chunk : chunks) {
        nodeOffsets.push_back(static_cast<uint32_t>(totalNodes));
        totalNodes += chunk->getTree().getNodeCount();
    }
    
    // Create chunk data buffer
    if (totalNodes > 0) {
        VkDeviceSize bufferSize = totalNodes * sizeof(voxels::Tree27Node);
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkDataBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create chunk data buffer");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, chunkDataBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkDataBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate chunk data buffer memory");
        }
        
        vkBindBufferMemory(device, chunkDataBuffer, chunkDataBufferMemory, 0);
        
        // Copy data
        void* data;
        vkMapMemory(device, chunkDataBufferMemory, 0, bufferSize, 0, &data);
        size_t offset = 0;
        for (const auto* chunk : chunks) {
            const auto& nodes = chunk->getTree().getNodes();
            std::memcpy(static_cast<char*>(data) + offset, nodes.data(), nodes.size() * sizeof(voxels::Tree27Node));
            offset += nodes.size() * sizeof(voxels::Tree27Node);
        }
        vkUnmapMemory(device, chunkDataBufferMemory);
    }
    
    // Create chunk meta buffer - must match shader ChunkMeta struct
    std::vector<ChunkMeta> chunkMetas;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto* chunk = chunks[i];
        ChunkMeta meta;
        meta.nodeOffset = nodeOffsets[i];
        meta.nodeCount = static_cast<uint32_t>(chunk->getTree().getNodeCount());
        meta.maxDepth = chunk->getCurrentDepth();
        meta.rootMaterial = chunk->getTree().getRootMaterial();
        chunkMetas.push_back(meta);
    }
    
    // Debug: Log chunk info occasionally
    static int debugCounter = 0;
    debugCounter++;
    if (debugCounter % 60 == 0 && !chunks.empty()) {
        LOG_INFO("GPU Chunk Debug: Sending " + std::to_string(chunks.size()) + " chunks, " + 
                 std::to_string(totalNodes) + " total nodes");
    }
    
    VkDeviceSize infoBufferSize = chunkMetas.size() * sizeof(ChunkMeta);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = infoBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkInfoBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create chunk info buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, chunkInfoBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkInfoBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate chunk info buffer memory");
    }
    
    vkBindBufferMemory(device, chunkInfoBuffer, chunkInfoBufferMemory, 0);
    
    void* data;
    vkMapMemory(device, chunkInfoBufferMemory, 0, infoBufferSize, 0, &data);
    std::memcpy(data, chunkMetas.data(), infoBufferSize);
    vkUnmapMemory(device, chunkInfoBufferMemory);
    
    // Uniform buffer is created during initialization, just update descriptor set
    
    // Update descriptor set
    std::vector<VkWriteDescriptorSet> descriptorWrites(4);
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = storageImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfo;
    
    VkDescriptorBufferInfo chunkDataBufferInfo{};
    chunkDataBufferInfo.buffer = chunkDataBuffer;
    chunkDataBufferInfo.offset = 0;
    chunkDataBufferInfo.range = totalNodes > 0 ? totalNodes * sizeof(voxels::Tree27Node) : VK_WHOLE_SIZE;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &chunkDataBufferInfo;
    
    VkDescriptorBufferInfo chunkInfoBufferInfo{};
    chunkInfoBufferInfo.buffer = chunkInfoBuffer;
    chunkInfoBufferInfo.offset = 0;
    chunkInfoBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &chunkInfoBufferInfo;
    
    VkDescriptorBufferInfo uniformBufferDescriptorInfo{};
    uniformBufferDescriptorInfo.buffer = uniformBuffer;
    uniformBufferDescriptorInfo.offset = 0;
    uniformBufferDescriptorInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &uniformBufferDescriptorInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

// Helper function to collect chunk data
void Renderer::collectChunkData(const voxels::ChunkArray& chunkArray,
                                 std::vector<voxels::Tree27Node>& allNodes,
                                 std::vector<ChunkMeta>& chunkMetas) {
    // Initialize all metas to empty
    chunkMetas.resize(voxels::CHUNK_ARRAY_TOTAL);
    for (auto& meta : chunkMetas) {
        meta.nodeOffset = 0;
        meta.nodeCount = 0;
        meta.maxDepth = 0;
        meta.rootMaterial = 0;
    }
    
    allNodes.clear();
    
    // Iterate through all chunks in the array
    for (uint32_t z = 0; z < voxels::CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < voxels::CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < voxels::CHUNK_ARRAY_SIZE; x++) {
                uint32_t flatIdx = x + y * voxels::CHUNK_ARRAY_SIZE + z * voxels::CHUNK_ARRAY_SIZE * voxels::CHUNK_ARRAY_SIZE;
                
                const voxels::Chunk* chunk = chunkArray.getByIndex(x, y, z);
                if (chunk && chunk->isLoaded()) {
                    const auto& nodes = chunk->getTree().getNodes();
                    
                    chunkMetas[flatIdx].nodeOffset = static_cast<uint32_t>(allNodes.size());
                    chunkMetas[flatIdx].nodeCount = static_cast<uint32_t>(nodes.size());
                    chunkMetas[flatIdx].maxDepth = chunk->getCurrentDepth();
                    chunkMetas[flatIdx].rootMaterial = chunk->getTree().getRootMaterial();
                    
                    // Append nodes
                    allNodes.insert(allNodes.end(), nodes.begin(), nodes.end());
                }
            }
        }
    }
    
    // Ensure at least one node exists
    if (allNodes.empty()) {
        voxels::Tree27Node emptyNode{};
        std::memset(&emptyNode, 0, sizeof(voxels::Tree27Node));
        allNodes.push_back(emptyNode);
    }
}

// New createChunkBuffers for 17x17x17 ChunkArray
// Only creates buffers if they don't exist or are too small
void Renderer::createChunkBuffers(const voxels::ChunkArray& chunkArray) {
    // Collect chunk data
    std::vector<voxels::Tree27Node> allNodes;
    std::vector<ChunkMeta> chunkMetas;
    collectChunkData(chunkArray, allNodes, chunkMetas);
    
    VkDeviceSize requiredNodeBufferSize = allNodes.size() * sizeof(voxels::Tree27Node);
    VkDeviceSize requiredMetaBufferSize = chunkMetas.size() * sizeof(ChunkMeta);
    
    // Create chunk data buffer if it doesn't exist or is too small
    if (chunkDataBuffer == VK_NULL_HANDLE || requiredNodeBufferSize > currentChunkDataBufferSize) {
        // Destroy old buffer if it exists
        if (chunkDataBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, chunkDataBufferMemory, nullptr);
            chunkDataBufferMemory = VK_NULL_HANDLE;
        }
        if (chunkDataBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, chunkDataBuffer, nullptr);
            chunkDataBuffer = VK_NULL_HANDLE;
        }
        
        // Allocate with some headroom (1.5x) to reduce recreations
        VkDeviceSize allocatedSize = std::max(requiredNodeBufferSize, static_cast<VkDeviceSize>(requiredNodeBufferSize * 1.5));
        currentChunkDataBufferSize = allocatedSize;
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = allocatedSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkDataBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create chunk data buffer");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, chunkDataBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkDataBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate chunk data buffer memory");
        }
        
        vkBindBufferMemory(device, chunkDataBuffer, chunkDataBufferMemory, 0);
    }
    
    // Create chunk meta buffer if it doesn't exist (always 17^3 = 4913 entries)
    if (chunkInfoBuffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = requiredMetaBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkInfoBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create chunk info buffer");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, chunkInfoBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkInfoBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate chunk info buffer memory");
        }
        
        vkBindBufferMemory(device, chunkInfoBuffer, chunkInfoBufferMemory, 0);
    }
    
    // Update buffer data
    void* data;
    vkMapMemory(device, chunkDataBufferMemory, 0, requiredNodeBufferSize, 0, &data);
    std::memcpy(data, allNodes.data(), requiredNodeBufferSize);
    vkUnmapMemory(device, chunkDataBufferMemory);
    
    vkMapMemory(device, chunkInfoBufferMemory, 0, requiredMetaBufferSize, 0, &data);
    std::memcpy(data, chunkMetas.data(), requiredMetaBufferSize);
    vkUnmapMemory(device, chunkInfoBufferMemory);
    
    // Update descriptor set (only needed if buffers were recreated)
    if (chunkDataBuffer != VK_NULL_HANDLE && chunkInfoBuffer != VK_NULL_HANDLE) {
        std::vector<VkWriteDescriptorSet> descriptorWrites(4);
        
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = storageImageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfo;
        
        VkDescriptorBufferInfo chunkDataBufferInfo{};
        chunkDataBufferInfo.buffer = chunkDataBuffer;
        chunkDataBufferInfo.offset = 0;
        chunkDataBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &chunkDataBufferInfo;
        
        VkDescriptorBufferInfo chunkInfoBufferInfo{};
        chunkInfoBufferInfo.buffer = chunkInfoBuffer;
        chunkInfoBufferInfo.offset = 0;
        chunkInfoBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &chunkInfoBufferInfo;
        
        VkDescriptorBufferInfo uniformBufferDescriptorInfo{};
        uniformBufferDescriptorInfo.buffer = uniformBuffer;
        uniformBufferDescriptorInfo.offset = 0;
        uniformBufferDescriptorInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSet;
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &uniformBufferDescriptorInfo;
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

// Update chunk buffers in place (no recreation)
void Renderer::updateChunkBuffers(const voxels::ChunkArray& chunkArray) {
    // Ensure buffers exist
    if (chunkDataBuffer == VK_NULL_HANDLE || chunkInfoBuffer == VK_NULL_HANDLE) {
        createChunkBuffers(chunkArray);
        return;
    }
    
    // Collect chunk data
    std::vector<voxels::Tree27Node> allNodes;
    std::vector<ChunkMeta> chunkMetas;
    collectChunkData(chunkArray, allNodes, chunkMetas);
    
    VkDeviceSize requiredNodeBufferSize = allNodes.size() * sizeof(voxels::Tree27Node);
    VkDeviceSize requiredMetaBufferSize = chunkMetas.size() * sizeof(ChunkMeta);
    
    // Check if we need to grow the buffer
    if (requiredNodeBufferSize > currentChunkDataBufferSize) {
        // Buffer too small, need to recreate
        createChunkBuffers(chunkArray);
        return;
    }
    
    // Update data in place
    void* data;
    vkMapMemory(device, chunkDataBufferMemory, 0, requiredNodeBufferSize, 0, &data);
    std::memcpy(data, allNodes.data(), requiredNodeBufferSize);
    vkUnmapMemory(device, chunkDataBufferMemory);
    
    vkMapMemory(device, chunkInfoBufferMemory, 0, requiredMetaBufferSize, 0, &data);
    std::memcpy(data, chunkMetas.data(), requiredMetaBufferSize);
    vkUnmapMemory(device, chunkInfoBufferMemory);
}

void Renderer::updateUniformBuffer(const math::Vec3& cameraPos, const math::Vec3& cameraDir, 
                                    const math::Vec3& cameraUp, const math::Vec3& arrayOrigin) {
    if (uniformBufferMemory == VK_NULL_HANDLE) {
        return;
    }
    
    UniformBufferData uboData{};
    std::memset(&uboData, 0, sizeof(UniformBufferData));
    
    uboData.cameraPos[0] = cameraPos.x;
    uboData.cameraPos[1] = cameraPos.y;
    uboData.cameraPos[2] = cameraPos.z;
    uboData.cameraPos[3] = 0.0f;
    
    math::Vec3 normalizedDir = cameraDir.normalize();
    uboData.cameraDir[0] = normalizedDir.x;
    uboData.cameraDir[1] = normalizedDir.y;
    uboData.cameraDir[2] = normalizedDir.z;
    uboData.cameraDir[3] = 0.0f;
    
    math::Vec3 normalizedUp = cameraUp.normalize();
    
    // Compute proper camera basis vectors
    math::Vec3 right = normalizedDir.cross(normalizedUp).normalize();
    math::Vec3 actualUp = right.cross(normalizedDir).normalize();
    
    uboData.cameraRightFov[0] = right.x;
    uboData.cameraRightFov[1] = right.y;
    uboData.cameraRightFov[2] = right.z;
    uboData.cameraRightFov[3] = 1.0f; // FOV tangent
    
    uboData.cameraUp[0] = actualUp.x;
    uboData.cameraUp[1] = actualUp.y;
    uboData.cameraUp[2] = actualUp.z;
    uboData.cameraUp[3] = 0.0f;
    
    // Array origin for direct chunk indexing
    uboData.arrayOrigin[0] = arrayOrigin.x;
    uboData.arrayOrigin[1] = arrayOrigin.y;
    uboData.arrayOrigin[2] = arrayOrigin.z;
    uboData.arrayOrigin[3] = voxels::CHUNK_SIZE_METERS;  // 729.0f
    
    uboData.chunkCount = 0;  // Set in render()
    uboData.debugMode = debugMode;
    uboData._pad1 = 0;
    uboData._pad2 = 0;
    
    // Sun direction
    uboData.sunDir[0] = 0.0f;
    uboData.sunDir[1] = 1.0f;
    uboData.sunDir[2] = 0.0f;
    uboData.sunDir[3] = 0.0f;
    
    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
    std::memcpy(data, &uboData, sizeof(UniformBufferData));
    vkUnmapMemory(device, uniformBufferMemory);
}

// ============================================================================
// Non-blocking render using GPUChunkUpdate from ChunkManager
// ============================================================================
void Renderer::render(const voxels::GPUChunkUpdate* gpuUpdate,
                     const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                     const math::Vec3& cameraUp) {
    if (framebufferResized) {
        recreateSwapchain();
        framebufferResized = false;
    }
    
    // Update GPU buffers if we have new data
    if (gpuUpdate && gpuUpdate->ready) {
        updateChunkBuffersFromUpdate(*gpuUpdate);
        lastArrayOrigin = gpuUpdate->arrayOrigin;
        lastChunkCount = gpuUpdate->loadedChunkCount;
        gpuBuffersValid = true;
    }
    
    // Can't render without valid buffers
    if (!gpuBuffersValid || chunkDataBuffer == VK_NULL_HANDLE || chunkInfoBuffer == VK_NULL_HANDLE) {
        return;
    }
    
    // Update uniform buffer
    updateUniformBuffer(cameraPos, cameraDir, cameraUp, lastArrayOrigin);
    
    // Update chunk count in uniform buffer
    if (uniformBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
        UniformBufferData* uboData = static_cast<UniformBufferData*>(data);
        uboData->chunkCount = static_cast<uint32_t>(lastChunkCount);
        uboData->debugMode = debugMode;
        vkUnmapMemory(device, uniformBufferMemory);
    }
    
    // Update debug info
    if (debugInfoBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, debugInfoBufferMemory, 0, sizeof(DebugInfo), 0, &data);
        DebugInfo* debugInfo = static_cast<DebugInfo*>(data);
        debugInfo->cameraPos[0] = cameraPos.x;
        debugInfo->cameraPos[1] = cameraPos.y;
        debugInfo->cameraPos[2] = cameraPos.z;
        debugInfo->cameraPos[3] = 0.0f;
        vkUnmapMemory(device, debugInfoBufferMemory);
    }
    
    // Acquire swapchain image and render
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    
    recordCommandBuffer(commandBuffers[0], imageIndex);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[0];
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        framebufferResized = true;
    }
}

// ============================================================================
// Buffer allocation and update methods
// ============================================================================

void Renderer::ensureChunkBuffersAllocated(VkDeviceSize requiredNodeSize) {
    // Check if we need to grow the node buffer
    if (chunkDataBuffer != VK_NULL_HANDLE && currentChunkDataBufferSize >= requiredNodeSize) {
        return;  // Already have enough space
    }
    
    // Wait for GPU to finish before reallocating
    vkDeviceWaitIdle(device);
    
    // Destroy old buffers
    if (chunkDataBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunkDataBufferMemory, nullptr);
        chunkDataBufferMemory = VK_NULL_HANDLE;
    }
    if (chunkDataBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunkDataBuffer, nullptr);
        chunkDataBuffer = VK_NULL_HANDLE;
    }
    
    // Allocate with 2x headroom to reduce future reallocations
    VkDeviceSize allocSize = std::max(requiredNodeSize * 2, INITIAL_NODE_BUFFER_SIZE);
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = allocSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkDataBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create chunk data buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, chunkDataBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkDataBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate chunk data buffer memory");
    }
    
    vkBindBufferMemory(device, chunkDataBuffer, chunkDataBufferMemory, 0);
    currentChunkDataBufferSize = allocSize;
    
    LOG_INFO("Allocated chunk data buffer: " + std::to_string(allocSize / (1024 * 1024)) + " MB");
    
    // Also ensure meta buffer exists
    if (chunkInfoBuffer == VK_NULL_HANDLE) {
        bufferInfo.size = META_BUFFER_SIZE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunkInfoBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create chunk info buffer");
        }
        
        vkGetBufferMemoryRequirements(device, chunkInfoBuffer, &memRequirements);
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunkInfoBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate chunk info buffer memory");
        }
        
        vkBindBufferMemory(device, chunkInfoBuffer, chunkInfoBufferMemory, 0);
        currentChunkMetaBufferSize = META_BUFFER_SIZE;
    }
    
    // Update descriptor set with new buffers
    std::vector<VkWriteDescriptorSet> descriptorWrites(4);
    
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = storageImageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfo;
    
    VkDescriptorBufferInfo chunkDataInfo{};
    chunkDataInfo.buffer = chunkDataBuffer;
    chunkDataInfo.offset = 0;
    chunkDataInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &chunkDataInfo;
    
    VkDescriptorBufferInfo chunkInfoInfo{};
    chunkInfoInfo.buffer = chunkInfoBuffer;
    chunkInfoInfo.offset = 0;
    chunkInfoInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &chunkInfoInfo;
    
    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = uniformBuffer;
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(UniformBufferData);
    
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &uniformInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::updateChunkBuffersFromUpdate(const voxels::GPUChunkUpdate& update) {
    // Calculate required size
    VkDeviceSize requiredNodeSize = update.nodes.size() * sizeof(voxels::Tree27Node);
    VkDeviceSize requiredMetaSize = update.metas.size() * sizeof(ChunkMeta);
    
    // Ensure buffers are allocated and large enough
    ensureChunkBuffersAllocated(requiredNodeSize);
    
    // Update node data (fast memcpy, no allocation)
    if (!update.nodes.empty()) {
        void* data;
        vkMapMemory(device, chunkDataBufferMemory, 0, requiredNodeSize, 0, &data);
        std::memcpy(data, update.nodes.data(), requiredNodeSize);
        vkUnmapMemory(device, chunkDataBufferMemory);
    }
    
    // Update meta data
    // Convert GPUChunkMeta to ChunkMeta (they should be compatible)
    if (!update.metas.empty()) {
        void* data;
        vkMapMemory(device, chunkInfoBufferMemory, 0, requiredMetaSize, 0, &data);
        
        // Direct copy since GPUChunkMeta and ChunkMeta have same layout
        static_assert(sizeof(voxels::GPUChunkMeta) == sizeof(ChunkMeta), "GPUChunkMeta and ChunkMeta must have same size");
        std::memcpy(data, update.metas.data(), requiredMetaSize);
        
        vkUnmapMemory(device, chunkInfoBufferMemory);
    }
    
    lastTotalNodes = update.nodes.size();
}

// Legacy chunk array based render
void Renderer::render(const voxels::ChunkArray& chunkArray,
                     const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                     const math::Vec3& cameraUp) {
    if (framebufferResized) {
        recreateSwapchain();
        framebufferResized = false;
    }
    
    // Get array origin for shader
    math::Vec3 arrayOrigin = chunkArray.getArrayOrigin();
    
    // Always count nodes to check if buffer needs updating
    size_t totalNodes = 0;
    size_t loadedCount = 0;
    
    for (uint32_t z = 0; z < voxels::CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < voxels::CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < voxels::CHUNK_ARRAY_SIZE; x++) {
                const voxels::Chunk* chunk = chunkArray.getByIndex(x, y, z);
                if (chunk && chunk->isLoaded()) {
                    totalNodes += chunk->getTree().getNodeCount();
                    loadedCount++;
                }
            }
        }
    }
    
    // Check if buffer needs updating:
    // 1. Array origin changed (player moved to new chunk) - requires full recreation
    // 2. Buffers don't exist - need to create
    // 3. Otherwise - just update in place
    bool arrayOriginChanged = (arrayOrigin.x != lastArrayOrigin.x || 
                               arrayOrigin.y != lastArrayOrigin.y || 
                               arrayOrigin.z != lastArrayOrigin.z);
    bool buffersNeedCreation = (chunkDataBuffer == VK_NULL_HANDLE || chunkInfoBuffer == VK_NULL_HANDLE);
    
    if (arrayOriginChanged || buffersNeedCreation || (lastChunkCount == 0 && loadedCount > 0)) {
        // Full recreation needed (origin changed or first time)
        createChunkBuffers(chunkArray);
        lastChunkCount = loadedCount;
        lastTotalNodes = totalNodes;
        lastArrayOrigin = arrayOrigin;
    } else if (loadedCount != lastChunkCount || totalNodes != lastTotalNodes) {
        // Data changed but buffers exist - update in place
        updateChunkBuffers(chunkArray);
        lastChunkCount = loadedCount;
        lastTotalNodes = totalNodes;
    }
    
    // Update uniform buffer
    updateUniformBuffer(cameraPos, cameraDir, cameraUp, arrayOrigin);
    
    // Update chunk count in uniform buffer
    if (uniformBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
        UniformBufferData* uboData = static_cast<UniformBufferData*>(data);
        uboData->chunkCount = static_cast<uint32_t>(loadedCount);
        uboData->debugMode = debugMode;
        vkUnmapMemory(device, uniformBufferMemory);
    }
    
    // Update debug info
    if (debugInfoBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, debugInfoBufferMemory, 0, sizeof(DebugInfo), 0, &data);
        DebugInfo* debugInfo = static_cast<DebugInfo*>(data);
        debugInfo->cameraPos[0] = cameraPos.x;
        debugInfo->cameraPos[1] = cameraPos.y;
        debugInfo->cameraPos[2] = cameraPos.z;
        debugInfo->cameraPos[3] = 0.0f;
        vkUnmapMemory(device, debugInfoBufferMemory);
    }
    
    // Acquire swapchain image and render (same as legacy)
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    
    recordCommandBuffer(commandBuffers[0], imageIndex);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[0];
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        framebufferResized = true;
    }
}

// Legacy render with chunk list
void Renderer::render(const std::vector<const voxels::Chunk*>& chunks, 
                     const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                     const math::Vec3& cameraUp) {
    if (framebufferResized) {
        recreateSwapchain();
        framebufferResized = false;
    }
    
    // Calculate array origin from first chunk (legacy mode)
    math::Vec3 arrayOrigin(0, 0, 0);
    if (!chunks.empty()) {
        arrayOrigin = chunks[0]->getWorldPosition();
    }
    
    size_t totalNodes = 0;
    for (const auto* chunk : chunks) {
        totalNodes += chunk->getTree().getNodeCount();
    }
    
    // Only recreate if significant change (avoid constant recreation)
    bool significantChange = (chunks.size() != lastChunkCount) ||
                             (lastTotalNodes > 0 && (totalNodes > lastTotalNodes * 1.2f || totalNodes < lastTotalNodes * 0.8f));
    
    if (significantChange) {
        // Don't wait for GPU - semaphores handle synchronization
        createChunkBuffers(chunks, cameraPos);
        lastChunkCount = chunks.size();
        lastTotalNodes = totalNodes;
    }
    
    updateUniformBuffer(cameraPos, cameraDir, cameraUp, arrayOrigin);
    
    if (uniformBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
        UniformBufferData* uboData = static_cast<UniformBufferData*>(data);
        uboData->chunkCount = static_cast<uint32_t>(chunks.size());
        vkUnmapMemory(device, uniformBufferMemory);
    }
    
    // Update debug info camera position
    if (debugInfoBufferMemory != VK_NULL_HANDLE) {
        void* data;
        vkMapMemory(device, debugInfoBufferMemory, 0, sizeof(DebugInfo), 0, &data);
        DebugInfo* debugInfo = static_cast<DebugInfo*>(data);
        debugInfo->cameraPos[0] = cameraPos.x;
        debugInfo->cameraPos[1] = cameraPos.y;
        debugInfo->cameraPos[2] = cameraPos.z;
        debugInfo->cameraPos[3] = 0.0f;
        vkUnmapMemory(device, debugInfoBufferMemory);
    }
    
    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        throw std::runtime_error("failed to acquire swapchain image");
    }
    
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    recordCommandBuffer(commandBuffers[imageIndex], imageIndex);
    
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer");
    }
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    vkQueuePresentKHR(presentQueue, &presentInfo);
}

void Renderer::resize(uint32_t width, uint32_t height) {
    framebufferResized = true;
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = 0;
    if (vkBeginCommandBuffer(cmd, &cbbi) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    
    uint32_t groupCountX = (swapchainExtent.width + 15) / 16;
    uint32_t groupCountY = (swapchainExtent.height + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    
    // Memory barrier between raytrace and text overlay
    VkMemoryBarrier memoryBarrier1{};
    memoryBarrier1.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier1.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, &memoryBarrier1, 0, nullptr, 0, nullptr);
    
    // Dispatch text overlay shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, textOverlayPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, textOverlayPipelineLayout, 0, 1, &textOverlayDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);
    
    // Memory barrier before transfer
    VkMemoryBarrier memoryBarrier2{};
    memoryBarrier2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier2.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 1, &memoryBarrier2, 0, nullptr, 0, nullptr);
    
    transitionImageLayoutInCommandBuffer(cmd, storageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transitionImageLayoutInCommandBuffer(cmd, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = swapchainExtent.width;
    copyRegion.extent.height = swapchainExtent.height;
    copyRegion.extent.depth = 1;
    
    vkCmdCopyImage(cmd, storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    
    transitionImageLayoutInCommandBuffer(cmd, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    transitionImageLayoutInCommandBuffer(cmd, storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }
}

void Renderer::transitionImageLayoutInCommandBuffer(VkCommandBuffer cmd, VkImage image, 
                                                     VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags sourceStage, destinationStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        return; // Unsupported transition
    }
    
    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type");
}

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = code.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule module;
    if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }
    return module;
}

void Renderer::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags sourceStage, destinationStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        return;
    }
    
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void Renderer::createFontTexture() {
    // Load font PNG using stb_image
    int width, height, channels;
    std::string fontPath = "resources/font.png";
    
    // Try multiple paths
    std::vector<std::string> paths = {
        fontPath,
        "../" + fontPath,
        "../../" + fontPath,
        "../../../" + fontPath
    };
    
    unsigned char* pixels = nullptr;
    std::string usedPath;
    for (const auto& path : paths) {
        pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels) {
            usedPath = path;
            break;
        }
    }
    
    if (!pixels) {
        throw std::runtime_error("Failed to load font texture from resources/font.png");
    }
    
    LOG_INFO("Loaded font texture: " + usedPath + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    
    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(device, &imageInfo, nullptr, &fontTexture) != VK_SUCCESS) {
        stbi_image_free(pixels);
        throw std::runtime_error("failed to create font texture image");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, fontTexture, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &fontTextureMemory) != VK_SUCCESS) {
        stbi_image_free(pixels);
        throw std::runtime_error("failed to allocate font texture memory");
    }
    
    vkBindImageMemory(device, fontTexture, fontTextureMemory, 0);
    
    // Create staging buffer to upload pixels
    VkDeviceSize imageSize = width * height * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        throw std::runtime_error("failed to create staging buffer");
    }
    
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS) {
        stbi_image_free(pixels);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        throw std::runtime_error("failed to allocate staging buffer memory");
    }
    
    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);
    
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    std::memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);
    
    stbi_image_free(pixels);
    
    // Transition image layout and copy
    transitionImageLayout(fontTexture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
    VkCommandBufferAllocateInfo allocInfo2{};
    allocInfo2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo2.commandPool = commandPool;
    allocInfo2.commandBufferCount = 1;
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo2, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, fontTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    
    transitionImageLayout(fontTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = fontTexture;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device, &viewInfo, nullptr, &fontTextureView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create font texture view");
    }
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    
    if (vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create font sampler");
    }
}

void Renderer::createTextOverlayPipeline() {
    // Create descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings(6);
    
    // Binding 0: Input image (readonly storage image)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 1: Output image (writeonly storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 2: Font texture
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 3: Debug info uniform
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 4: Text buffer
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 5: Character buffer
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textOverlayDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create text overlay descriptor set layout");
    }
    
    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes(4);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = 1;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[3].descriptorCount = 2;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &textOverlayDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create text overlay descriptor pool");
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = textOverlayDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textOverlayDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device, &allocInfo, &textOverlayDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate text overlay descriptor set");
    }
    
    // Load shader
    std::vector<std::string> shaderPaths = {
        "shaders/text_overlay.comp.spv",
        "../shaders/text_overlay.comp.spv",
        "../../shaders/text_overlay.comp.spv",
        "../../../shaders/text_overlay.comp.spv"
    };
    
    std::string shaderPath;
    bool found = false;
    for (const auto& path : shaderPaths) {
        std::ifstream testFile(path);
        if (testFile.good()) {
            shaderPath = path;
            found = true;
            break;
        }
    }
    
    if (!found) {
        throw std::runtime_error("Failed to find compiled shader text_overlay.comp.spv. Make sure shaders are compiled.");
    }
    
    auto shaderCode = readFile(shaderPath);
    VkShaderModule shaderModule = createShaderModule(shaderCode);
    
    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &textOverlayDescriptorSetLayout;
    
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &textOverlayPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        throw std::runtime_error("failed to create text overlay pipeline layout");
    }
    
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = textOverlayPipelineLayout;
    
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &textOverlayPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        throw std::runtime_error("failed to create text overlay pipeline");
    }
    
    vkDestroyShaderModule(device, shaderModule, nullptr);
}

void Renderer::createTextBuffers() {
    // Create debug info buffer
    VkDeviceSize bufferSize = sizeof(DebugInfo);
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &debugInfoBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create debug info buffer");
    }
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, debugInfoBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &debugInfoBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate debug info buffer memory");
    }
    
    vkBindBufferMemory(device, debugInfoBuffer, debugInfoBufferMemory, 0);
    
    // Initialize with default values
    DebugInfo debugInfo{};
    std::memset(&debugInfo, 0, sizeof(DebugInfo));
    debugInfo.textScale = 2.0f;
    debugInfo.textEnabled = 1;
    debugInfo.crosshairEnabled = 1;
    
    void* data;
    vkMapMemory(device, debugInfoBufferMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, &debugInfo, bufferSize);
    vkUnmapMemory(device, debugInfoBufferMemory);
    
    // Create text and char buffers (will be resized as needed)
    // Start with small buffers, will be recreated when text is set
    // Layout: [entryCount(4)] [padding(4)] [entries[](24 each)]
    bufferSize = 8 + sizeof(TextEntry) * 16; // header + 16 entries max initially
    
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &textBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create text buffer");
    }
    
    vkGetBufferMemoryRequirements(device, textBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &textBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate text buffer memory");
    }
    
    vkBindBufferMemory(device, textBuffer, textBufferMemory, 0);
    
    // Initialize text buffer with zero entries (8 bytes: entryCount + padding)
    vkMapMemory(device, textBufferMemory, 0, 8, 0, &data);
    uint32_t* headerPtr = static_cast<uint32_t*>(data);
    headerPtr[0] = 0;  // entryCount
    headerPtr[1] = 0;  // padding
    vkUnmapMemory(device, textBufferMemory);
    
    // Char buffer - start with 256 characters
    bufferSize = sizeof(uint32_t) * 256;
    
    bufferInfo.size = bufferSize;
    
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &charBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create char buffer");
    }
    
    vkGetBufferMemoryRequirements(device, charBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    
    if (vkAllocateMemory(device, &allocInfo, nullptr, &charBufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate char buffer memory");
    }
    
    vkBindBufferMemory(device, charBuffer, charBufferMemory, 0);
    
    // Update descriptor set (will be updated again when text is set)
    updateTextOverlayDescriptorSet();
}

void Renderer::updateTextOverlayDescriptorSet() {
    std::vector<VkWriteDescriptorSet> descriptorWrites(6);
    
    // Binding 0: Input image
    VkDescriptorImageInfo inputImageInfo{};
    inputImageInfo.imageView = storageImageView;
    inputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = textOverlayDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &inputImageInfo;
    
    // Binding 1: Output image (same as input for overlay)
    VkDescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageView = storageImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = textOverlayDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &outputImageInfo;
    
    // Binding 2: Font texture
    VkDescriptorImageInfo fontImageInfo{};
    fontImageInfo.imageView = fontTextureView;
    fontImageInfo.sampler = fontSampler;
    fontImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = textOverlayDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &fontImageInfo;
    
    // Binding 3: Debug info buffer
    VkDescriptorBufferInfo debugInfoBufferInfo{};
    debugInfoBufferInfo.buffer = debugInfoBuffer;
    debugInfoBufferInfo.offset = 0;
    debugInfoBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = textOverlayDescriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &debugInfoBufferInfo;
    
    // Binding 4: Text buffer
    VkDescriptorBufferInfo textBufferInfo{};
    textBufferInfo.buffer = textBuffer;
    textBufferInfo.offset = 0;
    textBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = textOverlayDescriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pBufferInfo = &textBufferInfo;
    
    // Binding 5: Char buffer
    VkDescriptorBufferInfo charBufferInfo{};
    charBufferInfo.buffer = charBuffer;
    charBufferInfo.offset = 0;
    charBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = textOverlayDescriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pBufferInfo = &charBufferInfo;
    
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Renderer::updateTextBuffers(const std::vector<std::string>& texts, const std::vector<std::pair<int, int>>& positions) {
    if (texts.size() != positions.size()) {
        LOG_WARNING("Text and position vectors must have same size");
        return;
    }
    
    if (textBuffer == VK_NULL_HANDLE || charBuffer == VK_NULL_HANDLE) {
        // Buffers not created yet
        return;
    }
    
    if (texts.empty()) {
        // Clear text buffer (set entryCount to 0)
        if (textBufferMemory != VK_NULL_HANDLE) {
            void* data;
            vkMapMemory(device, textBufferMemory, 0, 8, 0, &data);
            uint32_t* headerPtr = static_cast<uint32_t*>(data);
            headerPtr[0] = 0;  // entryCount
            headerPtr[1] = 0;  // padding
            vkUnmapMemory(device, textBufferMemory);
        }
        return;
    }
    
    // Collect all characters
    std::vector<uint32_t> allChars;
    std::vector<TextEntry> entries;
    uint32_t charStartIndex = 0;
    
    for (size_t i = 0; i < texts.size(); i++) {
        TextEntry entry;
        entry.position[0] = positions[i].first;
        entry.position[1] = positions[i].second;
        entry.charCount = static_cast<uint32_t>(texts[i].length());
        entry.charStartIndex = charStartIndex;
        entry._pad1 = 0;
        entry._pad2 = 0;
        
        for (char c : texts[i]) {
            allChars.push_back(static_cast<uint32_t>(c));
        }
        
        charStartIndex += entry.charCount;
        entries.push_back(entry);
    }
    
    // Resize buffers if needed
    // Text buffer layout: [entryCount(4)] [padding(4)] [entries[](24 each)]
    // Entries need 8-byte alignment due to ivec2, so we add 4 bytes padding after entryCount
    VkDeviceSize textBufferHeaderSize = 8; // entryCount + padding
    VkDeviceSize textBufferSize = textBufferHeaderSize + entries.size() * sizeof(TextEntry);
    VkDeviceSize charBufferSize = allChars.size() * sizeof(uint32_t);
    
    // Check if we need to recreate buffers
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, textBuffer, &memRequirements);
    bool needTextResize = memRequirements.size < textBufferSize;
    
    vkGetBufferMemoryRequirements(device, charBuffer, &memRequirements);
    bool needCharResize = memRequirements.size < charBufferSize;
    
    if (needTextResize) {
        if (textBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, textBufferMemory, nullptr);
        }
        if (textBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, textBuffer, nullptr);
        }
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = textBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &textBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to recreate text buffer");
        }
        
        vkGetBufferMemoryRequirements(device, textBuffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &textBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate text buffer memory");
        }
        
        vkBindBufferMemory(device, textBuffer, textBufferMemory, 0);
    }
    
    if (needCharResize) {
        if (charBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, charBufferMemory, nullptr);
        }
        if (charBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, charBuffer, nullptr);
        }
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = charBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &charBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to recreate char buffer");
        }
        
        vkGetBufferMemoryRequirements(device, charBuffer, &memRequirements);
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device, &allocInfo, nullptr, &charBufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate char buffer memory");
        }
        
        vkBindBufferMemory(device, charBuffer, charBufferMemory, 0);
    }
    
    // Upload data
    void* data;
    
    // Upload text buffer: [entryCount(4)] [padding(4)] [entries[]]
    vkMapMemory(device, textBufferMemory, 0, textBufferSize, 0, &data);
    uint32_t* headerPtr = static_cast<uint32_t*>(data);
    headerPtr[0] = static_cast<uint32_t>(entries.size());  // entryCount
    headerPtr[1] = 0;  // padding
    std::memcpy(static_cast<char*>(data) + textBufferHeaderSize, entries.data(), entries.size() * sizeof(TextEntry));
    vkUnmapMemory(device, textBufferMemory);
    
    // Upload char buffer
    if (charBufferSize > 0) {
        vkMapMemory(device, charBufferMemory, 0, charBufferSize, 0, &data);
        std::memcpy(data, allChars.data(), charBufferSize);
        vkUnmapMemory(device, charBufferMemory);
    }
    
    // Update descriptor set if buffers were resized
    if (needTextResize || needCharResize) {
        updateTextOverlayDescriptorSet();
    }
}

void Renderer::updateDebugInfo(float fps, bool showText, bool showCrosshair, float textScale) {
    if (debugInfoBufferMemory == VK_NULL_HANDLE) return;
    
    DebugInfo debugInfo{};
    std::memset(&debugInfo, 0, sizeof(DebugInfo));
    debugInfo.fps = fps;
    debugInfo.textScale = textScale;
    debugInfo.textEnabled = showText ? 1u : 0u;
    debugInfo.crosshairEnabled = showCrosshair ? 1u : 0u;
    
    void* data;
    vkMapMemory(device, debugInfoBufferMemory, 0, sizeof(DebugInfo), 0, &data);
    std::memcpy(data, &debugInfo, sizeof(DebugInfo));
    vkUnmapMemory(device, debugInfoBufferMemory);
}

void Renderer::setDebugText(const std::vector<std::string>& texts, const std::vector<std::pair<int, int>>& positions) {
    updateTextBuffers(texts, positions);
}

void Renderer::setDebugInfo(float fps, bool showText, bool showCrosshair, float textScale) {
    updateDebugInfo(fps, showText, showCrosshair, textScale);
}

} // namespace graphics
} // namespace chonk


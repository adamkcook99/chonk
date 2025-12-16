#include "renderer.h"
#include "../voxels/chunk.h"
#include "../voxels/svo.h"
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
    uboData.cameraRight[3] = 0.5f;  // FOV is packed in w component
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
        VkDeviceSize dataBufferSize = sizeof(voxels::SVONode); // At least 1 node
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
        voxels::SVONode emptyNode{};
        std::memset(&emptyNode, 0, sizeof(voxels::SVONode));
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
    
    // Collect all SVO nodes
    size_t totalNodes = 0;
    std::vector<uint32_t> nodeOffsets;
    for (const auto* chunk : chunks) {
        nodeOffsets.push_back(static_cast<uint32_t>(totalNodes));
        totalNodes += chunk->getSVO().getNodeCount();
    }
    
    // Create chunk data buffer
    if (totalNodes > 0) {
        VkDeviceSize bufferSize = totalNodes * sizeof(voxels::SVONode);
        
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
            const auto& nodes = chunk->getSVO().getNodes();
            std::memcpy(static_cast<char*>(data) + offset, nodes.data(), nodes.size() * sizeof(voxels::SVONode));
            offset += nodes.size() * sizeof(voxels::SVONode);
        }
        vkUnmapMemory(device, chunkDataBufferMemory);
    }
    
    // Create chunk info buffer - must match shader struct
    struct ChunkInfo {
        float aabbMin[4];   // xyz = min, w = unused
        float aabbMax[4];   // xyz = max, w = chunk size
        uint32_t nodeOffset;
        uint32_t nodeCount;
        uint32_t maxDepth;  // LOD depth for this chunk
        uint32_t padding;
    };
    
    std::vector<ChunkInfo> chunkInfos;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto* chunk = chunks[i];
        const auto& aabb = chunk->getAABB();
        ChunkInfo info;
        info.aabbMin[0] = aabb.min.x;
        info.aabbMin[1] = aabb.min.y;
        info.aabbMin[2] = aabb.min.z;
        info.aabbMin[3] = 0.0f;  // Unused
        info.aabbMax[0] = aabb.max.x;
        info.aabbMax[1] = aabb.max.y;
        info.aabbMax[2] = aabb.max.z;
        info.aabbMax[3] = voxels::CHUNK_SIZE_METERS;  // 256.0f
        info.nodeOffset = nodeOffsets[i];
        info.nodeCount = static_cast<uint32_t>(chunk->getSVO().getNodeCount());
        
        // Calculate LOD depth based on distance from camera
        math::Vec3 chunkCenter = aabb.getCenter();
        float distance = (chunkCenter - cameraPos).length();
        info.maxDepth = voxels::getMaxDepthForDistance(distance);
        
        info.padding = 0;
        chunkInfos.push_back(info);
    }
    
    // Debug: Log chunk info occasionally
    static int debugCounter = 0;
    debugCounter++;
    if (debugCounter % 60 == 0 && !chunks.empty()) {
        LOG_INFO("GPU Chunk Debug: Sending " + std::to_string(chunks.size()) + " chunks, " + 
                 std::to_string(totalNodes) + " total nodes");
        if (chunks.size() > 0) {
            const auto* firstChunk = chunks[0];
            LOG_INFO("First chunk: AABB min=(" + 
                     std::to_string(chunkInfos[0].aabbMin[0]) + "," +
                     std::to_string(chunkInfos[0].aabbMin[1]) + "," +
                     std::to_string(chunkInfos[0].aabbMin[2]) + "), " +
                     "nodes=" + std::to_string(chunkInfos[0].nodeCount));
        }
    }
    
    VkDeviceSize infoBufferSize = chunkInfos.size() * sizeof(ChunkInfo);
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
    std::memcpy(data, chunkInfos.data(), infoBufferSize);
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
    chunkDataBufferInfo.range = totalNodes > 0 ? totalNodes * sizeof(voxels::SVONode) : VK_WHOLE_SIZE;
    
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

void Renderer::updateUniformBuffer(const math::Vec3& cameraPos, const math::Vec3& cameraDir, const math::Vec3& cameraUp) {
    if (uniformBufferMemory == VK_NULL_HANDLE) {
        // Uniform buffer not created yet - this shouldn't happen, but handle gracefully
        return;
    }
    
    UniformBufferData uboData{};
    std::memset(&uboData, 0, sizeof(UniformBufferData)); // Zero all padding
    
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
    uboData.cameraUp[0] = normalizedUp.x;
    uboData.cameraUp[1] = normalizedUp.y;
    uboData.cameraUp[2] = normalizedUp.z;
    uboData.cameraUp[3] = 0.0f;
    
    // Compute proper camera basis vectors
    // Right = forward cross world_up (gives horizontal direction)
    math::Vec3 right = normalizedDir.cross(normalizedUp).normalize();
    // Actual camera up = right cross forward (perpendicular to both)
    math::Vec3 actualUp = right.cross(normalizedDir).normalize();
    
    uboData.cameraRight[0] = right.x;
    uboData.cameraRight[1] = right.y;
    uboData.cameraRight[2] = right.z;
    uboData.cameraRight[3] = 1.0f; // FOV tangent (tan(45°) ≈ 1.0 for ~90° FOV)
    
    // Use actual camera up, not world up
    uboData.cameraUp[0] = actualUp.x;
    uboData.cameraUp[1] = actualUp.y;
    uboData.cameraUp[2] = actualUp.z;
    
    // chunkCount is set in render() after this call
    uboData.chunkCount = 0;
    uboData._pad1 = 0;
    uboData._pad2 = 0;
    uboData._pad3 = 0;
    
    // Sun direction - pointing straight up for now (can be adjusted for time of day)
    uboData.sunDir[0] = 0.0f;
    uboData.sunDir[1] = 1.0f;  // Up
    uboData.sunDir[2] = 0.0f;
    uboData.sunDir[3] = 0.0f;
    
    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferData), 0, &data);
    std::memcpy(data, &uboData, sizeof(UniformBufferData));
    vkUnmapMemory(device, uniformBufferMemory);
}

void Renderer::render(const std::vector<const voxels::Chunk*>& chunks, 
                     const math::Vec3& cameraPos, const math::Vec3& cameraDir,
                     const math::Vec3& cameraUp) {
    if (framebufferResized) {
        recreateSwapchain();
        framebufferResized = false;
    }
    
    // CRITICAL: Wait for GPU to finish before modifying any buffers
    // This prevents race conditions where we destroy buffers the GPU is still using
    vkDeviceWaitIdle(device);
    
    // Only recreate chunk buffers if chunks have changed
    static size_t lastChunkCount = 0;
    static size_t lastTotalNodes = 0;
    size_t totalNodes = 0;
    for (const auto* chunk : chunks) {
        totalNodes += chunk->getSVO().getNodeCount();
    }
    
    if (chunks.size() != lastChunkCount || totalNodes != lastTotalNodes) {
        createChunkBuffers(chunks, cameraPos);
        lastChunkCount = chunks.size();
        lastTotalNodes = totalNodes;
    }
    
    // Update uniform buffer (camera data changes every frame)
    updateUniformBuffer(cameraPos, cameraDir, cameraUp);
    
    // Update uniform buffer chunk count
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


#include "../engine/graphics/renderer.h"
#include "../engine/voxels/chunk_manager.h"
#include "../engine/voxels/chunk.h"
#include "../engine/voxels/chunk_array.h"
#include "../engine/voxels/world_constants.h"
#include "../engine/networking/network.h"
#include "../game/world/world_generator.h"
#include "../engine/math/vec3.h"
#include "../engine/core/logging.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <chrono>
#include <unordered_set>
#include <cmath>

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "CHONK Client", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    // Initialize systems
    chonk::graphics::Renderer renderer;
    if (!renderer.initialize(window)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    
    chonk::game::world::WorldGenerator worldGen;
    chonk::voxels::ChunkManager chunkManager;
    chunkManager.initialize(&worldGen);
    
    chonk::networking::NetworkManager network;
    if (!network.initialize(false, 7777)) {
        LOG_WARNING("Failed to initialize network - running in single player mode");
    }
    
    // Track which chunks we've already requested
    std::unordered_set<chonk::voxels::ChunkCoord, chonk::voxels::ChunkCoordHash> requestedChunks;
    
    // Set up network callbacks
    network.onChunkReceived = [&chunkManager, &requestedChunks](const chonk::voxels::ChunkCoord& coord, const std::vector<uint8_t>& data, uint32_t depth) {
        chunkManager.addChunkFromNetwork(coord, data, depth);
        requestedChunks.erase(coord);
    };
    
    // Player position - spawn at terrain level (ground level at half world height)
    chonk::math::Vec3 playerPos(0, chonk::voxels::GROUND_LEVEL + 50.0f, 0);
    
    // Camera rotation (yaw and pitch in radians)
    float yaw = 0.0f;
    float pitch = -0.2f;  // Slightly looking down
    
    // Calculate camera direction from yaw and pitch
    auto updateCameraDir = [&yaw, &pitch]() -> chonk::math::Vec3 {
        chonk::math::Vec3 dir;
        dir.x = std::cos(pitch) * std::sin(yaw);
        dir.y = std::sin(pitch);
        dir.z = -std::cos(pitch) * std::cos(yaw);
        return dir.normalize();
    };
    
    chonk::math::Vec3 cameraDir = updateCameraDir();
    chonk::math::Vec3 cameraUp(0, 1, 0);
    
    // Mouse look state
    bool mouseCaptured = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool firstMouse = true;
    const float mouseSensitivity = 0.002f;
    const float maxPitch = 1.57f;
    
    // Capture mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    mouseCaptured = true;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    
    auto lastTime = std::chrono::steady_clock::now();
    int updateCounter = 0;
    
    // Debug mode toggle
    uint32_t debugMode = 0;  // 0=normal, 1=LOD colors, 2=chunk grid
    bool debugKeyPressed = false;
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // Handle ESC to toggle mouse capture
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            mouseCaptured = !mouseCaptured;
            glfwSetInputMode(window, GLFW_CURSOR, 
                           mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (mouseCaptured) {
                glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
                firstMouse = true;
            }
            while (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwPollEvents();
            }
        }
        
        // Toggle debug mode with F1
        if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS) {
            if (!debugKeyPressed) {
                debugMode = (debugMode + 1) % 3;
                renderer.setDebugMode(debugMode);
                debugKeyPressed = true;
            }
        } else {
            debugKeyPressed = false;
        }
        
        // Handle mouse look
        if (mouseCaptured) {
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            if (firstMouse) {
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                firstMouse = false;
            }
            
            double deltaX = mouseX - lastMouseX;
            double deltaY = mouseY - lastMouseY;
            
            yaw += static_cast<float>(deltaX) * mouseSensitivity;
            pitch -= static_cast<float>(deltaY) * mouseSensitivity;
            
            if (pitch > maxPitch) pitch = maxPitch;
            if (pitch < -maxPitch) pitch = -maxPitch;
            
            cameraDir = updateCameraDir();
            
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
        
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // Movement speed (with shift boost)
        float speedMultiplier = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
                                 glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) ? 50.0f : 1.0f;
        float moveSpeed = 20.0f * deltaTime * speedMultiplier;
        
        // WASD movement
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            playerPos = playerPos + cameraDir * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            playerPos = playerPos - cameraDir * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            chonk::math::Vec3 right = cameraDir.cross(cameraUp).normalize();
            playerPos = playerPos - right * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            chonk::math::Vec3 right = cameraDir.cross(cameraUp).normalize();
            playerPos = playerPos + right * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            playerPos = playerPos + cameraUp * moveSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            playerPos = playerPos - cameraUp * moveSpeed;
        }
        
        // Log position occasionally
        updateCounter++;
        if (updateCounter % 1000 == 0) {
            LOG_INFO("Player pos: (" + 
                     std::to_string((int)playerPos.x) + ", " +
                     std::to_string((int)playerPos.y) + ", " +
                     std::to_string((int)playerPos.z) + ")");
        }
        
        // Update chunk manager (non-blocking, queues work for background threads)
        static uint32_t frameCounter = 0;
        frameCounter++;
        chunkManager.update(playerPos, frameCounter);
        
        // Update network
        network.update();
        
        // Debug info
        if (updateCounter % 1000 == 0) {
            LOG_INFO("Loaded: " + std::to_string(chunkManager.getLoadedChunkCount()) + 
                     " chunks, Pending: " + std::to_string(chunkManager.getPendingLoadCount()));
        }
        
        // Calculate FPS
        static float fps = 0.0f;
        static int fpsFrameCount = 0;
        static float fpsAccumulator = 0.0f;
        fpsFrameCount++;
        fpsAccumulator += deltaTime;
        if (fpsFrameCount >= 60) {
            fps = 60.0f / fpsAccumulator;
            fpsFrameCount = 0;
            fpsAccumulator = 0.0f;
        }
        
        // Update debug text
        std::vector<std::string> debugTexts;
        std::vector<std::pair<int, int>> debugPositions;
        
        debugTexts.push_back("pos " + std::to_string((int)playerPos.x) + ", " + 
                            std::to_string((int)playerPos.y) + ", " + 
                            std::to_string((int)playerPos.z));
        debugPositions.push_back({10, 10});
        
        debugTexts.push_back("fps " + std::to_string((int)fps));
        debugPositions.push_back({10, 40});
        
        std::string modeStr = debugMode == 0 ? "normal" : (debugMode == 1 ? "lod" : "grid");
        debugTexts.push_back("f1 mode, " + modeStr);
        debugPositions.push_back({10, 70});
        
        renderer.setDebugText(debugTexts, debugPositions);
        renderer.setDebugInfo(fps, true, true, 2.0f);
        
        // Get GPU update from chunk manager (non-blocking)
        // This returns the latest prepared chunk data without waiting
        const chonk::voxels::GPUChunkUpdate* gpuUpdate = chunkManager.getGPUUpdate();
        
        // Render using non-blocking GPU update path
        // Movement is never blocked - we render with whatever data is ready
        renderer.render(gpuUpdate, playerPos, cameraDir, cameraUp);
        
        // Send position to server
        if (!network.isServer()) {
            network.sendPlayerPosition(playerPos);
        }
    }
    
    // Cleanup
    network.shutdown();
    chunkManager.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}

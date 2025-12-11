#include "../engine/graphics/renderer.h"
#include "../engine/voxels/chunk_manager.h"
#include "../engine/voxels/chunk.h"  // For ChunkCoordHash
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
    
    // Track which chunks we've already requested to avoid duplicate requests
    std::unordered_set<chonk::voxels::ChunkCoord, chonk::voxels::ChunkCoordHash> requestedChunks;
    
    // Set up network callbacks
    network.onChunkReceived = [&chunkManager, &requestedChunks](const chonk::voxels::ChunkCoord& coord, const std::vector<uint8_t>& data) {
        // Deserialize and add chunk to chunk manager
        chunkManager.addChunkFromNetwork(coord, data);
        // Remove from requested set since we received it
        requestedChunks.erase(coord);
    };
    
    // Player position - spawn at terrain level (grass is at y=5488, spawn slightly above at y=5490)
    // World coordinates: floats in meters, can be negative
    // Chunk coordinates: int32_t, calculated as floor(worldPos / chunkSize)
    // For RES_0_25M chunks (16m size): chunk y = floor(5490 / 16) = 343
    chonk::math::Vec3 playerPos(0, 4100, 0); // Start at terrain level
    
    // Camera rotation (yaw and pitch in radians)
    float yaw = 0.0f;      // Horizontal rotation (around Y axis)
    float pitch = -0.2f;   // Vertical rotation (slightly down to see ground)
    
    // Calculate initial camera direction from yaw and pitch
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
    const float maxPitch = 1.57f; // ~90 degrees
    
    // Set up mouse callbacks
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    mouseCaptured = true;
    glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    
    auto lastTime = std::chrono::steady_clock::now();
    
    // Update counter for player position logging
    int updateCounter = 0;
    
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
            // Wait for key release to avoid toggling multiple times
            while (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwPollEvents();
            }
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
            
            // Clamp pitch to prevent flipping
            if (pitch > maxPitch) pitch = maxPitch;
            if (pitch < -maxPitch) pitch = -maxPitch;
            
            // Update camera direction
            cameraDir = updateCameraDir();
            
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
        
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // Simple camera movement (WASD + QE for up/down)
        // Check if Shift is held for 10x speed boost
        float speedMultiplier = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
                                 glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) ? 10.0f : 1.0f;
        float moveSpeed = 10.0f * deltaTime * speedMultiplier;
        
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
        
        // Log player position every 1000 updates
        updateCounter++;
        if (updateCounter % 1000 == 0) {
            LOG_INFO("Player position: (" + 
                     std::to_string(playerPos.x) + ", " +
                     std::to_string(playerPos.y) + ", " +
                     std::to_string(playerPos.z) + ")");
        }
        
        // Update chunk manager
        chunkManager.update(playerPos);
        
        // Update network
        network.update();
        
        // Request chunks from server if needed (only once per chunk)
        // Only check every few frames to avoid spamming requests
        static int requestFrameCounter = 0;
        requestFrameCounter++;
        if (requestFrameCounter % 10 == 0) { // Check every 10 frames
            auto requiredChunks = chunkManager.getRequiredChunks(playerPos);
            for (const auto& coord : requiredChunks) {
                const auto* chunk = chunkManager.getChunk(coord);
                if (!chunk || !chunk->isLoaded()) {
                    // Only request if we haven't already requested it
                    if (requestedChunks.find(coord) == requestedChunks.end()) {
                        network.requestChunk(coord);
                        requestedChunks.insert(coord);
                    }
                }
            }
        }
        
        // Get visible chunks (frustum culled) for rendering
        // Client keeps 13x13x13 region in memory, but only sends visible chunks to GPU
        float fov = 0.5f; // Field of view
        auto visibleChunks = chunkManager.getVisibleChunks(playerPos, cameraDir, cameraUp, fov);
        
        // Debug: Print chunk count occasionally (every 1000 updates, same as player position)
        if (updateCounter % 1000 == 0) {
            auto renderChunks = chunkManager.getRenderChunks();
            LOG_INFO("Loaded chunks: " + std::to_string(renderChunks.size()) + 
                     ", Visible chunks: " + std::to_string(visibleChunks.size()) +
                     ", Requested chunks: " + std::to_string(requestedChunks.size()));
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
        
        // Position text
        debugTexts.push_back("pos " + std::to_string((int)playerPos.x) + ", " + 
                            std::to_string((int)playerPos.y) + ", " + 
                            std::to_string((int)playerPos.z));
        debugPositions.push_back({10, 10});
        
        // FPS text
        debugTexts.push_back("fps " + std::to_string((int)fps));
        debugPositions.push_back({10, 40});
        
        renderer.setDebugText(debugTexts, debugPositions);
        renderer.setDebugInfo(fps, true, true, 2.0f);
        
        // Render only visible chunks
        renderer.render(visibleChunks, playerPos, cameraDir, cameraUp);
        
        // Send player position to server
        if (network.isServer() == false) {
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


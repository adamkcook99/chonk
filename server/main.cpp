#include "../engine/voxels/chunk_manager.h"
#include "../engine/voxels/chunk.h"
#include "../engine/voxels/world_constants.h"
#include "../engine/networking/network.h"
#include "../game/world/world_generator.h"
#include "../engine/math/vec3.h"
#include "../engine/core/logging.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

struct Player {
    chonk::math::Vec3 position;
    ENetPeer* peer;
};

int main() {
    std::cout << "Starting CHONK Server..." << std::endl;
    
    chonk::game::world::WorldGenerator worldGen;
    chonk::voxels::ChunkManager chunkManager;
    chunkManager.initialize(&worldGen);
    
    chonk::networking::NetworkManager network;
    if (!network.initialize(true, 7777)) {
        std::cerr << "Failed to initialize network" << std::endl;
        return -1;
    }
    
    std::cout << "Server running on port 7777" << std::endl;
    
    // Track connected players
    std::unordered_map<ENetPeer*, Player> players;
    
    // Track which chunks have been sent to each player
    std::unordered_map<ENetPeer*, std::unordered_set<chonk::voxels::ChunkCoord, chonk::voxels::ChunkCoordHash>> sentChunks;
    
    uint32_t frameCounter = 0;
    
    // Set up network callbacks
    network.onClientConnected = [&players, &sentChunks](ENetPeer* peer) {
        Player player;
        player.position = chonk::math::Vec3(0, chonk::voxels::GROUND_LEVEL + 50.0f, 0);
        player.peer = peer;
        players[peer] = player;
        sentChunks[peer] = std::unordered_set<chonk::voxels::ChunkCoord, chonk::voxels::ChunkCoordHash>();
        LOG_INFO("Client connected");
    };
    
    network.onClientDisconnected = [&players, &sentChunks](ENetPeer* peer) {
        players.erase(peer);
        sentChunks.erase(peer);
        LOG_INFO("Client disconnected");
    };
    
    // Handle chunk requests (full detail)
    network.onChunkRequestReceived = [&players, &chunkManager, &network, &worldGen, &sentChunks](ENetPeer* peer, const chonk::voxels::ChunkCoord& coord) {
        if (players.find(peer) == players.end()) return;
        
        auto& sent = sentChunks[peer];
        if (sent.find(coord) != sent.end()) return;
        
        chonk::math::Vec3 worldPos = coord.toWorldPos();
        auto* chunk = chunkManager.getChunkByCoord(coord);
        
        if (!chunk) {
            // Generate chunk on demand
            chonk::voxels::Chunk tempChunk(coord);
            worldGen.generateChunk(tempChunk, chonk::voxels::CHUNK_TREE_MAX_DEPTH);
            network.sendChunk(peer, tempChunk, chonk::voxels::CHUNK_TREE_MAX_DEPTH);
            sent.insert(coord);
        } else if (chunk->isLoaded()) {
            network.sendChunk(peer, *chunk, chunk->getCurrentDepth());
            sent.insert(coord);
        }
    };
    
    // Handle LOD chunk requests
    network.onChunkRequestLODReceived = [&players, &chunkManager, &network, &worldGen, &sentChunks](ENetPeer* peer, const chonk::voxels::ChunkCoord& coord, uint32_t depth) {
        if (players.find(peer) == players.end()) return;
        
        auto& sent = sentChunks[peer];
        // For LOD requests, we might re-send at different depths, so don't skip if already sent
        
        auto* chunk = chunkManager.getChunkByCoord(coord);
        
        if (!chunk) {
            // Generate chunk at requested depth
            chonk::voxels::Chunk tempChunk(coord);
            worldGen.generateChunk(tempChunk, depth);
            network.sendChunkAtLOD(peer, tempChunk, depth);
            sent.insert(coord);
        } else if (chunk->isLoaded()) {
            network.sendChunkAtLOD(peer, *chunk, depth);
            sent.insert(coord);
        }
    };
    
    network.onPlayerPositionReceived = [&players, &chunkManager, &frameCounter](ENetPeer* peer, const chonk::math::Vec3& pos) {
        if (players.find(peer) != players.end()) {
            players[peer].position = pos;
            chunkManager.update(pos, frameCounter);
        }
    };
    
    // Main loop
    bool running = true;
    auto lastUpdate = std::chrono::steady_clock::now();
    
    while (running) {
        frameCounter++;
        
        // Update network
        network.update();
        
        // Update chunk manager for all players periodically
        auto currentTime = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(currentTime - lastUpdate).count() > 0.1f) {
            for (auto& pair : players) {
                chunkManager.update(pair.second.position, frameCounter);
            }
            lastUpdate = currentTime;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
    }
    
    // Cleanup
    network.shutdown();
    chunkManager.shutdown();
    
    return 0;
}

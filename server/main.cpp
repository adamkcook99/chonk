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
    
    // Track which chunks have been sent to each player to avoid duplicates
    std::unordered_map<ENetPeer*, std::unordered_set<chonk::voxels::ChunkCoord, chonk::voxels::ChunkCoordHash>> sentChunks;
    
    // Frame counter
    uint32_t frameCounter = 0;
    
    // Set up network callbacks
    network.onClientConnected = [&players, &sentChunks](ENetPeer* peer) {
        Player player;
        player.position = chonk::math::Vec3(0, 5120, 0); // Spawn at terrain level
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
    
    // Handle chunk requests from clients
    network.onChunkRequestReceived = [&players, &chunkManager, &network, &worldGen, &sentChunks](ENetPeer* peer, const chonk::voxels::ChunkCoord& coord) {
        if (players.find(peer) == players.end()) return;
        
        // Check if we've already sent this chunk to this player
        auto& sent = sentChunks[peer];
        if (sent.find(coord) != sent.end()) {
            return; // Already sent, skip
        }
        
        // Get or generate the chunk using world position
        chonk::math::Vec3 worldPos = coord.toWorldPos();
        auto* chunk = chunkManager.getChunkAtWorld(worldPos);
        
        if (!chunk) {
            // Chunk doesn't exist, create it temporarily to generate and send
            chonk::voxels::Chunk tempChunk(coord);
            tempChunk.generate(&worldGen);
            network.sendChunk(peer, tempChunk);
            sent.insert(coord);
        } else {
            // Chunk exists
            if (!chunk->isLoaded()) {
                // Can't modify const chunk, skip
            } else {
                network.sendChunk(peer, *chunk);
                sent.insert(coord);
            }
        }
    };
    
    network.onPlayerPositionReceived = [&players, &chunkManager, &frameCounter](ENetPeer* peer, const chonk::math::Vec3& pos) {
        if (players.find(peer) != players.end()) {
            players[peer].position = pos;
            // Server updates chunk manager to generate/load chunks in simulation distance
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
        
        // Update chunk manager for all players
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

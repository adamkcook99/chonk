#pragma once

#include <enet/enet.h>
#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace chonk {
namespace voxels {
    struct ChunkCoord;
    class Chunk;
}

namespace math {
    struct Vec3;
}

namespace networking {

// ============================================================================
// Message Types
// ============================================================================

enum class MessageType : uint8_t {
    CHUNK_DATA = 0,
    PLAYER_POSITION = 1,
    CHUNK_REQUEST = 2,
    CHUNK_UNLOAD = 3,
    CHUNK_REQUEST_LOD = 4,    // Request chunk at specific LOD depth
    WORLD_TREE_DATA = 5,      // World tree update for distant LOD
};

// ============================================================================
// Message Structures
// ============================================================================

#pragma pack(push, 1)

// Chunk data with LOD info
struct ChunkDataHeader {
    MessageType type;
    int32_t x, y, z;          // Chunk coordinate
    uint32_t nodeCount;        // Number of Tree27 nodes
    uint32_t requestedDepth;   // Depth this was requested at
    uint32_t actualDepth;      // Actual depth of data sent
    uint8_t rootMaterial;      // For quick air checks
    uint8_t rootFillPercent;   // Fill percentage at root
    uint16_t _pad;
    // Followed by Tree27Node array (nodeCount * 10 bytes)
};

// Player position update
struct PlayerPositionMessage {
    MessageType type;
    float x, y, z;
};

// Basic chunk request (full detail)
struct ChunkRequestMessage {
    MessageType type;
    int32_t x, y, z;
};

// Request chunk at specific LOD depth
struct ChunkRequestLODMessage {
    MessageType type;
    int32_t x, y, z;
    uint32_t requestedDepth;   // 0-7: desired tree depth
};

// World tree data for distant terrain
struct WorldTreeHeader {
    MessageType type;
    float originX, originY, originZ;  // World origin of tree
    float worldSize;                   // Total size covered
    uint32_t nodeCount;                // Number of Tree27 nodes
    // Followed by Tree27Node array
};

#pragma pack(pop)

// ============================================================================
// Network Manager
// ============================================================================

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    bool initialize(bool isServer, uint16_t port = 7777, uint32_t maxClients = 32);
    void shutdown();
    
    // ========================================================================
    // Server Functions
    // ========================================================================
    
    // Send chunk to all clients
    void broadcastChunk(const voxels::Chunk& chunk, uint32_t depth);
    
    // Send chunk to specific client
    void sendChunk(ENetPeer* peer, const voxels::Chunk& chunk, uint32_t depth);
    
    // Send chunk at specific LOD depth
    void sendChunkAtLOD(ENetPeer* peer, const voxels::Chunk& chunk, uint32_t requestedDepth);
    
    // ========================================================================
    // Client Functions
    // ========================================================================
    
    // Request chunk at full detail
    void requestChunk(const voxels::ChunkCoord& coord);
    
    // Request chunk at specific LOD depth
    void requestChunkLOD(const voxels::ChunkCoord& coord, uint32_t depth);
    
    // Send player position
    void sendPlayerPosition(const math::Vec3& pos);
    
    // ========================================================================
    // Update
    // ========================================================================
    
    void update();
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    // Client: received chunk data
    std::function<void(const voxels::ChunkCoord&, const std::vector<uint8_t>& data, uint32_t depth)> onChunkReceived;
    
    // Server: received player position
    std::function<void(ENetPeer* peer, const math::Vec3& pos)> onPlayerPositionReceived;
    
    // Server: received chunk request
    std::function<void(ENetPeer* peer, const voxels::ChunkCoord& coord)> onChunkRequestReceived;
    
    // Server: received chunk LOD request
    std::function<void(ENetPeer* peer, const voxels::ChunkCoord& coord, uint32_t depth)> onChunkRequestLODReceived;
    
    // Connection events
    std::function<void(ENetPeer* peer)> onClientConnected;
    std::function<void(ENetPeer* peer)> onClientDisconnected;
    
    // ========================================================================
    // State
    // ========================================================================
    
    bool isServer() const { return serverMode; }
    ENetHost* getHost() const { return host; }
    
private:
    ENetHost* host;
    bool serverMode;
    ENetPeer* serverPeer;  // For client: connection to server
    
    void processPacket(ENetEvent& event);
    void serializeChunk(const voxels::Chunk& chunk, uint32_t depth, std::vector<uint8_t>& data);
};

} // namespace networking
} // namespace chonk

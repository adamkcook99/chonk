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

enum class MessageType : uint8_t {
    CHUNK_DATA = 0,
    PLAYER_POSITION = 1,
    CHUNK_REQUEST = 2,
    CHUNK_UNLOAD = 3
};

struct ChunkDataHeader {
    MessageType type;
    int32_t x, y, z;
    uint32_t nodeCount;
    uint32_t maxDepth;  // LOD depth for this chunk
    // Followed by node data
};

struct PlayerPositionMessage {
    MessageType type;
    float x, y, z;
};

struct ChunkRequestMessage {
    MessageType type;
    int32_t x, y, z;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();
    
    bool initialize(bool isServer, uint16_t port = 7777, uint32_t maxClients = 32);
    void shutdown();
    
    // Server functions
    void broadcastChunk(const voxels::Chunk& chunk);
    void sendChunk(ENetPeer* peer, const voxels::Chunk& chunk);
    
    // Client functions
    void requestChunk(const voxels::ChunkCoord& coord);
    void sendPlayerPosition(const math::Vec3& pos);
    
    // Update (process incoming messages)
    void update();
    
    // Callbacks
    std::function<void(const voxels::ChunkCoord&, const std::vector<uint8_t>& data)> onChunkReceived;
    std::function<void(ENetPeer* peer, const math::Vec3& pos)> onPlayerPositionReceived;
    std::function<void(ENetPeer* peer, const voxels::ChunkCoord& coord)> onChunkRequestReceived; // Server callback
    std::function<void(ENetPeer* peer)> onClientConnected;
    std::function<void(ENetPeer* peer)> onClientDisconnected;
    
    bool isServer() const { return serverMode; }
    ENetHost* getHost() const { return host; }
    
private:
    ENetHost* host;
    bool serverMode;
    ENetPeer* serverPeer; // For client
    
    void processPacket(ENetEvent& event);
    void serializeChunk(const voxels::Chunk& chunk, std::vector<uint8_t>& data);
};

} // namespace networking
} // namespace chonk


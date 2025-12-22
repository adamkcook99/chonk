#include "network.h"
#include "../voxels/chunk.h"
#include "../voxels/tree27.h"
#include "../voxels/world_constants.h"
#include "../math/vec3.h"
#include "../core/logging.h"
#include <cstring>

namespace chonk {
namespace networking {

NetworkManager::NetworkManager() : host(nullptr), serverMode(false), serverPeer(nullptr) {
}

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initialize(bool isServer, uint16_t port, uint32_t maxClients) {
    serverMode = isServer;
    
    if (enet_initialize() != 0) {
        LOG_ERROR("Failed to initialize ENet");
        return false;
    }
    
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    
    if (isServer) {
        host = enet_host_create(&address, maxClients, 2, 0, 0);
        if (host == nullptr) {
            LOG_ERROR("Failed to create ENet server host");
            enet_deinitialize();
            return false;
        }
        LOG_INFO("Server started on port " + std::to_string(port));
    } else {
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        if (host == nullptr) {
            LOG_ERROR("Failed to create ENet client host");
            enet_deinitialize();
            return false;
        }
        
        ENetAddress serverAddress;
        enet_address_set_host(&serverAddress, "127.0.0.1");
        serverAddress.port = port;
        
        serverPeer = enet_host_connect(host, &serverAddress, 2, 0);
        if (serverPeer == nullptr) {
            LOG_ERROR("Failed to connect to server");
            enet_host_destroy(host);
            enet_deinitialize();
            return false;
        }
        
        LOG_INFO("Connecting to server on port " + std::to_string(port));
    }
    
    return true;
}

void NetworkManager::shutdown() {
    if (host) {
        if (serverPeer && !serverMode) {
            enet_peer_disconnect(serverPeer, 0);
        }
        
        enet_host_flush(host);
        enet_host_destroy(host);
        host = nullptr;
    }
    
    enet_deinitialize();
}

void NetworkManager::update() {
    if (!host) return;
    
    ENetEvent event;
    while (enet_host_service(host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (serverMode) {
                    LOG_INFO("Client connected");
                    if (onClientConnected) {
                        onClientConnected(event.peer);
                    }
                } else {
                    LOG_INFO("Connected to server");
                }
                break;
                
            case ENET_EVENT_TYPE_DISCONNECT:
                if (serverMode) {
                    LOG_INFO("Client disconnected");
                    if (onClientDisconnected) {
                        onClientDisconnected(event.peer);
                    }
                } else {
                    LOG_INFO("Disconnected from server");
                    serverPeer = nullptr;
                }
                break;
                
            case ENET_EVENT_TYPE_RECEIVE:
                processPacket(event);
                enet_packet_destroy(event.packet);
                break;
                
            default:
                break;
        }
    }
}

void NetworkManager::processPacket(ENetEvent& event) {
    if (event.packet->dataLength < sizeof(MessageType)) {
        return;
    }
    
    MessageType type = static_cast<MessageType>(event.packet->data[0]);
    
    switch (type) {
        case MessageType::CHUNK_DATA: {
            if (event.packet->dataLength < sizeof(ChunkDataHeader)) {
                break;
            }
            
            ChunkDataHeader* header = reinterpret_cast<ChunkDataHeader*>(event.packet->data);
            voxels::ChunkCoord coord(header->x, header->y, header->z);
            
            // Extract node data
            size_t nodeDataOffset = sizeof(ChunkDataHeader);
            size_t nodeDataSize = event.packet->dataLength - nodeDataOffset;
            
            std::vector<uint8_t> data(nodeDataSize);
            if (nodeDataSize > 0) {
                std::memcpy(data.data(), event.packet->data + nodeDataOffset, nodeDataSize);
            }
            
            if (onChunkReceived) {
                onChunkReceived(coord, data, header->actualDepth);
            }
            break;
        }
        
        case MessageType::PLAYER_POSITION: {
            if (event.packet->dataLength < sizeof(PlayerPositionMessage)) {
                break;
            }
            
            PlayerPositionMessage* msg = reinterpret_cast<PlayerPositionMessage*>(event.packet->data);
            math::Vec3 pos(msg->x, msg->y, msg->z);
            
            if (onPlayerPositionReceived) {
                onPlayerPositionReceived(event.peer, pos);
            }
            break;
        }
        
        case MessageType::CHUNK_REQUEST: {
            if (event.packet->dataLength < sizeof(ChunkRequestMessage)) {
                break;
            }
            
            ChunkRequestMessage* msg = reinterpret_cast<ChunkRequestMessage*>(event.packet->data);
            voxels::ChunkCoord coord(msg->x, msg->y, msg->z);
            
            if (onChunkRequestReceived) {
                onChunkRequestReceived(event.peer, coord);
            }
            break;
        }
        
        case MessageType::CHUNK_REQUEST_LOD: {
            if (event.packet->dataLength < sizeof(ChunkRequestLODMessage)) {
                break;
            }
            
            ChunkRequestLODMessage* msg = reinterpret_cast<ChunkRequestLODMessage*>(event.packet->data);
            voxels::ChunkCoord coord(msg->x, msg->y, msg->z);
            
            if (onChunkRequestLODReceived) {
                onChunkRequestLODReceived(event.peer, coord, msg->requestedDepth);
            }
            break;
        }
        
        default:
            break;
    }
}

// ============================================================================
// Server Functions
// ============================================================================

void NetworkManager::serializeChunk(const voxels::Chunk& chunk, uint32_t depth, std::vector<uint8_t>& data) {
    const auto& tree = chunk.getTree();
    
    // Get nodes trimmed to requested depth
    auto nodes = tree.getNodesTrimmedToDepth(depth);
    
    ChunkDataHeader header;
    header.type = MessageType::CHUNK_DATA;
    const auto& coord = chunk.getCoord();
    header.x = coord.x;
    header.y = coord.y;
    header.z = coord.z;
    header.nodeCount = static_cast<uint32_t>(nodes.size());
    header.requestedDepth = depth;
    header.actualDepth = std::min(depth, chunk.getCurrentDepth());
    header.rootMaterial = tree.getRootMaterial();
    header.rootFillPercent = tree.getRootFillPercent();
    header._pad = 0;
    
    size_t headerSize = sizeof(ChunkDataHeader);
    size_t nodeSize = nodes.size() * sizeof(voxels::Tree27Node);
    data.resize(headerSize + nodeSize);
    
    std::memcpy(data.data(), &header, headerSize);
    if (nodeSize > 0) {
        std::memcpy(data.data() + headerSize, nodes.data(), nodeSize);
    }
}

void NetworkManager::broadcastChunk(const voxels::Chunk& chunk, uint32_t depth) {
    if (!host || !serverMode) return;
    
    std::vector<uint8_t> data;
    serializeChunk(chunk, depth, data);
    
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(host, 0, packet);
}

void NetworkManager::sendChunk(ENetPeer* peer, const voxels::Chunk& chunk, uint32_t depth) {
    if (!host || !peer) return;
    
    std::vector<uint8_t> data;
    serializeChunk(chunk, depth, data);
    
    ENetPacket* packet = enet_packet_create(data.data(), data.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, packet);
}

void NetworkManager::sendChunkAtLOD(ENetPeer* peer, const voxels::Chunk& chunk, uint32_t requestedDepth) {
    sendChunk(peer, chunk, requestedDepth);
}

// ============================================================================
// Client Functions
// ============================================================================

void NetworkManager::requestChunk(const voxels::ChunkCoord& coord) {
    if (!host || serverMode || !serverPeer) return;
    
    ChunkRequestMessage msg;
    msg.type = MessageType::CHUNK_REQUEST;
    msg.x = coord.x;
    msg.y = coord.y;
    msg.z = coord.z;
    
    ENetPacket* packet = enet_packet_create(&msg, sizeof(msg), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(serverPeer, 0, packet);
}

void NetworkManager::requestChunkLOD(const voxels::ChunkCoord& coord, uint32_t depth) {
    if (!host || serverMode || !serverPeer) return;
    
    ChunkRequestLODMessage msg;
    msg.type = MessageType::CHUNK_REQUEST_LOD;
    msg.x = coord.x;
    msg.y = coord.y;
    msg.z = coord.z;
    msg.requestedDepth = depth;
    
    ENetPacket* packet = enet_packet_create(&msg, sizeof(msg), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(serverPeer, 0, packet);
}

void NetworkManager::sendPlayerPosition(const math::Vec3& pos) {
    if (!host || serverMode || !serverPeer) return;
    
    PlayerPositionMessage msg;
    msg.type = MessageType::PLAYER_POSITION;
    msg.x = pos.x;
    msg.y = pos.y;
    msg.z = pos.z;
    
    ENetPacket* packet = enet_packet_create(&msg, sizeof(msg), ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(serverPeer, 0, packet);
}

} // namespace networking
} // namespace chonk

#include "chunk.h"
#include "../../game/world/world_generator.h"
#include "../core/logging.h"
#include <fstream>
#include <cstring>

namespace chonk {
namespace voxels {

Chunk::Chunk() : visible(false), loaded(false) {
}

Chunk::Chunk(const ChunkCoord& coord) : coord(coord), visible(false), loaded(false) {
    updateAABB();
}

Chunk::~Chunk() {
}

float Chunk::getResolution() const {
    // Returns the size of each block/leaf in meters
    switch (coord.resolution) {
        case ChunkResolution::RES_0_25M: return 0.25f;   // Full resolution
        case ChunkResolution::RES_0_5M:  return 0.5f;
        case ChunkResolution::RES_1M:    return 1.0f;
        case ChunkResolution::RES_2M:    return 2.0f;
        case ChunkResolution::RES_4M:    return 4.0f;
        case ChunkResolution::RES_8M:    return 8.0f;
        case ChunkResolution::RES_16M:   return 16.0f;
        case ChunkResolution::RES_32M:   return 32.0f;   // Lowest resolution
        default: return 0.25f;
    }
}

float Chunk::getSize() const {
    // Returns the chunk size in meters (always 64 blocks per side)
    switch (coord.resolution) {
        case ChunkResolution::RES_0_25M: return 16.0f;    // 64 * 0.25m
        case ChunkResolution::RES_0_5M:  return 32.0f;    // 64 * 0.5m
        case ChunkResolution::RES_1M:    return 64.0f;    // 64 * 1m
        case ChunkResolution::RES_2M:    return 128.0f;   // 64 * 2m
        case ChunkResolution::RES_4M:    return 256.0f;   // 64 * 4m
        case ChunkResolution::RES_8M:    return 512.0f;   // 64 * 8m
        case ChunkResolution::RES_16M:   return 1024.0f;  // 64 * 16m
        case ChunkResolution::RES_32M:   return 2048.0f;  // 64 * 32m
        default: return 16.0f;
    }
}

math::Vec3 Chunk::getWorldPosition() const {
    float size = getSize();
    return math::Vec3(
        coord.x * size,
        coord.y * size,
        coord.z * size
    );
}

uint8_t Chunk::getMaterialAtChunk(float x, float y, float z) const {
    // x,y,z are voxel indices (0..63)
    return svo.getMaterial(x, y, z, 0.0f);
}

uint8_t Chunk::getMaterialAtWorld(const math::Vec3& worldPos) const {
    math::Vec3 local = worldPos - getWorldPosition();
    float res = getResolution();
    return getMaterialAtChunk(
        std::floor(local.x / res),
        std::floor(local.y / res),
        std::floor(local.z / res)
    );
}

uint8_t Chunk::getMaterialAtMeters(float x, float y, float z) const {
    // chunk-local meters -> voxel indices
    float res = getResolution();
    return getMaterialAtChunk(
        std::floor(x / res),
        std::floor(y / res),
        std::floor(z / res)
    );
}

void Chunk::updateAABB() {
    math::Vec3 minPos = getWorldPosition();
    float size = getSize();
    aabb = math::AABB(minPos, minPos + math::Vec3(size, size, size));
}

void Chunk::generate(game::world::WorldGenerator* generator) {
    if (!generator) {
        LOG_ERROR("Cannot generate chunk: no world generator");
        return;
    }
    
    // All chunks are 64x64x64 blocks regardless of resolution
    // Resolution determines the size of each block (0.25m to 32m)
    constexpr uint32_t CHUNK_SIZE = 64;
    float resolution = getResolution();
    math::Vec3 worldPos = getWorldPosition();
    
        std::vector<uint8_t> voxels(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE, 0);
        
    // Generate terrain for every block in the chunk
    // Each block samples the world at its center position
        for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
            for (uint32_t y = 0; y < CHUNK_SIZE; ++y) {
                for (uint32_t x = 0; x < CHUNK_SIZE; ++x) {
                // Sample at the center of each block
                    math::Vec3 blockWorldPos = worldPos + math::Vec3(
                    (x + 0.5f) * resolution,
                    (y + 0.5f) * resolution,
                    (z + 0.5f) * resolution
                    );
                    
                    uint8_t material = generator->getMaterialAt(blockWorldPos, resolution);
                    voxels[z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x] = material;
                }
            }
        }
        
    // Build SVO from dense voxel data
    // The SVO will propagate dominant materials up the tree automatically
        svo.buildFromDense(voxels.data(), CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE);
    
    loaded = true;
    updateAABB();
}

bool Chunk::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read header
    ChunkCoord fileCoord;
    file.read(reinterpret_cast<char*>(&fileCoord), sizeof(ChunkCoord));
    
    if (fileCoord != coord) {
        LOG_WARNING("Chunk coordinate mismatch in file");
        return false;
    }
    
    // Read node count
    uint32_t nodeCount;
    file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    
    if (nodeCount == 0) {
        // Empty chunk (all air)
        loaded = true;
        updateAABB();
        return true;
    }
    
    // Read nodes
    std::vector<SVONode> nodes(nodeCount);
    file.read(reinterpret_cast<char*>(nodes.data()), nodeCount * sizeof(SVONode));
    
    if (file.fail()) {
        LOG_ERROR("Failed to read chunk nodes from file");
        return false;
    }
    
    // Reconstruct SVO from nodes
    // The nodes are stored in a flat array in the order they were built
    // We can directly load them into the SVO
    svo.clear();
    svo.loadNodes(nodes);
    
    loaded = true;
    updateAABB();
    
    return true;
}

bool Chunk::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    file.write(reinterpret_cast<const char*>(&coord), sizeof(ChunkCoord));
    
    // Write node count
    uint32_t nodeCount = svo.getNodeCount();
    file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    
    // Write nodes
    const auto& nodes = svo.getNodes();
    file.write(reinterpret_cast<const char*>(nodes.data()), nodeCount * sizeof(SVONode));
    
    return true;
}

} // namespace voxels
} // namespace chonk


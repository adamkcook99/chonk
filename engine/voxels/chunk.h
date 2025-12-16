#pragma once

#include "svo.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include <cstdint>
#include <string>
#include <functional>

namespace chonk {
namespace game {
namespace world {
    class WorldGenerator;
}
}

namespace voxels {

// ============================================================================
// Chunk coordinate (fixed 256m grid)
// ============================================================================
struct ChunkCoord {
    int32_t x, y, z;
    
    ChunkCoord() : x(0), y(0), z(0) {}
    ChunkCoord(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}
    
    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const ChunkCoord& other) const {
        return !(*this == other);
    }
    
    bool operator<(const ChunkCoord& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
    
    std::string toString() const {
        return std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
    }
    
    // Get world position (corner of chunk)
    math::Vec3 toWorldPos() const {
        return math::Vec3(
            x * CHUNK_SIZE_METERS,
            y * CHUNK_SIZE_METERS,
            z * CHUNK_SIZE_METERS
        );
    }
    
    // Get chunk coord from world position
    static ChunkCoord fromWorldPos(const math::Vec3& pos) {
        return ChunkCoord(
            static_cast<int32_t>(std::floor(pos.x / CHUNK_SIZE_METERS)),
            static_cast<int32_t>(std::floor(pos.y / CHUNK_SIZE_METERS)),
            static_cast<int32_t>(std::floor(pos.z / CHUNK_SIZE_METERS))
        );
    }
};

// Hash function for ChunkCoord
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        std::size_t h1 = std::hash<int32_t>{}(coord.x);
        std::size_t h2 = std::hash<int32_t>{}(coord.y);
        std::size_t h3 = std::hash<int32_t>{}(coord.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// ============================================================================
// Super chunk coordinate (2048m grid, 8x8x8 chunks each)
// ============================================================================
struct SuperChunkCoord {
    int32_t x, y, z;
    
    SuperChunkCoord() : x(0), y(0), z(0) {}
    SuperChunkCoord(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}
    
    bool operator==(const SuperChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    
    bool operator!=(const SuperChunkCoord& other) const {
        return !(*this == other);
    }
    
    std::string toString() const {
        return "S" + std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z);
    }
    
    // Get world position (corner of super chunk)
    math::Vec3 toWorldPos() const {
        return math::Vec3(
            x * SUPER_CHUNK_SIZE_METERS,
            y * SUPER_CHUNK_SIZE_METERS,
            z * SUPER_CHUNK_SIZE_METERS
        );
    }
    
    // Get super chunk coord from world position
    static SuperChunkCoord fromWorldPos(const math::Vec3& pos) {
        return SuperChunkCoord(
            static_cast<int32_t>(std::floor(pos.x / SUPER_CHUNK_SIZE_METERS)),
            static_cast<int32_t>(std::floor(pos.y / SUPER_CHUNK_SIZE_METERS)),
            static_cast<int32_t>(std::floor(pos.z / SUPER_CHUNK_SIZE_METERS))
        );
    }
    
    // Get the chunk coord of the first chunk in this super chunk
    ChunkCoord getFirstChunkCoord() const {
        return ChunkCoord(
            x * static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
            y * static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
            z * static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS)
        );
    }
    
    // Check if a chunk is in this super chunk
    bool containsChunk(const ChunkCoord& chunk) const {
        ChunkCoord first = getFirstChunkCoord();
        int32_t size = static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS);
        return chunk.x >= first.x && chunk.x < first.x + size &&
               chunk.y >= first.y && chunk.y < first.y + size &&
               chunk.z >= first.z && chunk.z < first.z + size;
    }
};

struct SuperChunkCoordHash {
    std::size_t operator()(const SuperChunkCoord& coord) const {
        std::size_t h1 = std::hash<int32_t>{}(coord.x);
        std::size_t h2 = std::hash<int32_t>{}(coord.y);
        std::size_t h3 = std::hash<int32_t>{}(coord.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// ============================================================================
// Chunk class (256m, SVO with variable depth based on LOD)
// ============================================================================
class Chunk {
public:
    Chunk();
    explicit Chunk(const ChunkCoord& coord);
    ~Chunk();
    
    // Generate chunk data to a specific depth
    // If maxDepth < CHUNK_SVO_DEPTH, the chunk has more potential detail
    void generate(game::world::WorldGenerator* generator, uint32_t maxDepth = CHUNK_SVO_DEPTH);
    
    // Refine chunk to a higher detail level (if hasMoreDetail is true)
    void refineDetail(game::world::WorldGenerator* generator, uint32_t newMaxDepth);
    
    // Load/save from file
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    
    // Getters
    const ChunkCoord& getCoord() const { return coord; }
    const math::AABB& getAABB() const { return aabb; }
    const SVO& getSVO() const { return svo; }
    SVO& getSVO() { return svo; }
    
    // Query material at world position with LOD
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos, uint32_t maxDepth = CHUNK_SVO_DEPTH) const;
    
    // Query material at chunk-local position (meters) with LOD
    uint8_t getMaterialAtLocal(float x, float y, float z, uint32_t maxDepth = CHUNK_SVO_DEPTH) const;
    
    // Get world position (min corner of chunk)
    math::Vec3 getWorldPosition() const { return coord.toWorldPos(); }
    
    // Chunk size in meters
    static constexpr float getSize() { return CHUNK_SIZE_METERS; }
    
    bool isLoaded() const { return loaded; }
    void setLoaded(bool l) { loaded = l; }
    
    // Check if entirely air
    bool isAir() const { return svo.isCollapsed() && svo.getRootMaterial() == 0; }
    
    // LOD detail tracking
    bool hasMoreDetail() const { return hasMoreDetailFlag; }
    uint32_t getCurrentDepth() const { return currentDepth; }
    
private:
    ChunkCoord coord;
    math::AABB aabb;
    bool loaded = false;
    bool hasMoreDetailFlag = false;  // True if chunk was generated with limited depth
    uint32_t currentDepth = 0;       // Current SVO depth
    SVO svo;
    
    void updateAABB();
};

} // namespace voxels
} // namespace chonk

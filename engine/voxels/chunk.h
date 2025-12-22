#pragma once

#include "tree27.h"
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
// Chunk Coordinate (729m grid)
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
    
    // Get world position (min corner of chunk)
    math::Vec3 toWorldPos() const {
        return math::Vec3(
            x * CHUNK_SIZE_METERS,
            y * CHUNK_SIZE_METERS,
            z * CHUNK_SIZE_METERS
        );
    }
    
    // Get center world position
    math::Vec3 toCenterWorldPos() const {
        float half = CHUNK_SIZE_METERS * 0.5f;
        return math::Vec3(
            x * CHUNK_SIZE_METERS + half,
            y * CHUNK_SIZE_METERS + half,
            z * CHUNK_SIZE_METERS + half
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
    
    // Manhattan distance to another coord
    int32_t manhattanDistance(const ChunkCoord& other) const {
        return std::abs(x - other.x) + std::abs(y - other.y) + std::abs(z - other.z);
    }
    
    // Chebyshev distance (max of axis distances)
    int32_t chebyshevDistance(const ChunkCoord& other) const {
        return std::max({std::abs(x - other.x), std::abs(y - other.y), std::abs(z - other.z)});
    }
};

// Hash function for ChunkCoord
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        // Use a better hash combining method
        std::size_t h1 = std::hash<int32_t>{}(coord.x);
        std::size_t h2 = std::hash<int32_t>{}(coord.y);
        std::size_t h3 = std::hash<int32_t>{}(coord.z);
        return h1 ^ (h2 * 31) ^ (h3 * 997);
    }
};

// ============================================================================
// Chunk Class (729m, 27-ary tree with variable depth based on LOD)
// ============================================================================
class Chunk {
public:
    Chunk();
    explicit Chunk(const ChunkCoord& coord);
    ~Chunk();
    
    // ========================================================================
    // Generation
    // ========================================================================
    
    // Generate chunk to specific depth using world generator
    void generate(game::world::WorldGenerator* generator, uint32_t maxDepth = CHUNK_TREE_MAX_DEPTH);
    
    // Generate using terrain function directly
    void generateWithFunction(
        std::function<uint8_t(const math::Vec3&)> terrainFn,
        uint32_t maxDepth = CHUNK_TREE_MAX_DEPTH
    );
    
    // Generate using layered terrain (fast path)
    void generateLayered(
        uint32_t maxDepth,
        float stoneHeight,
        float dirtHeight,
        float grassHeight
    );
    
    // Refine to higher detail level (if hasMoreDetail is true)
    void refineDetail(game::world::WorldGenerator* generator, uint32_t newMaxDepth);
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    const ChunkCoord& getCoord() const { return coord; }
    
    const Tree27& getTree() const { return tree; }
    Tree27& getTree() { return tree; }
    
    math::Vec3 getWorldPosition() const { return coord.toWorldPos(); }
    math::Vec3 getCenterWorldPosition() const { return coord.toCenterWorldPos(); }
    
    static constexpr float getSize() { return CHUNK_SIZE_METERS; }
    
    // ========================================================================
    // State
    // ========================================================================
    
    bool isLoaded() const { return loaded; }
    void setLoaded(bool l) { loaded = l; }
    
    bool isAir() const { 
        return tree.isEmpty() || (tree.isCollapsed() && tree.getRootMaterial() == MAT_AIR);
    }
    
    uint32_t getCurrentDepth() const { return currentDepth; }
    void setCurrentDepth(uint32_t d) { currentDepth = d; }
    
    bool hasMoreDetail() const { return currentDepth < CHUNK_TREE_MAX_DEPTH; }
    
    // ========================================================================
    // Material Queries
    // ========================================================================
    
    // Query material at world position with LOD
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos, uint32_t maxDepth = CHUNK_TREE_MAX_DEPTH) const;
    
    // Query material at chunk-local position (meters, 0 to CHUNK_SIZE_METERS)
    uint8_t getMaterialAtLocal(float x, float y, float z, uint32_t maxDepth = CHUNK_TREE_MAX_DEPTH) const;
    
    // ========================================================================
    // File I/O
    // ========================================================================
    
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    
    // ========================================================================
    // Network serialization
    // ========================================================================
    
    std::vector<uint8_t> serialize() const;
    void deserialize(const uint8_t* data, size_t size, uint32_t depth);
    
private:
    ChunkCoord coord;
    Tree27 tree;
    bool loaded = false;
    uint32_t currentDepth = 0;
};

} // namespace voxels
} // namespace chonk

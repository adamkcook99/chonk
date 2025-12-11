#pragma once

#include "svo.h"
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

// Chunk resolution levels (LOD)
// Each chunk is always 64x64x64 blocks
// Resolution determines the size of each block/leaf
enum class ChunkResolution {
    RES_0_25M = 0,  // 0.25m blocks, 16m chunks (full resolution)
    RES_0_5M = 1,   // 0.5m blocks, 32m chunks
    RES_1M = 2,     // 1m blocks, 64m chunks
    RES_2M = 3,     // 2m blocks, 128m chunks
    RES_4M = 4,     // 4m blocks, 256m chunks
    RES_8M = 5,     // 8m blocks, 512m chunks
    RES_16M = 6,    // 16m blocks, 1024m chunks
    RES_32M = 7     // 32m blocks, 2048m chunks (lowest resolution)
};

struct ChunkCoord {
    int32_t x, y, z;
    ChunkResolution resolution;
    
    ChunkCoord() : x(0), y(0), z(0), resolution(ChunkResolution::RES_0_25M) {}
    ChunkCoord(int32_t x, int32_t y, int32_t z, ChunkResolution res) 
        : x(x), y(y), z(z), resolution(res) {}
    
    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z && resolution == other.resolution;
    }
    
    bool operator!=(const ChunkCoord& other) const {
        return !(*this == other);
    }
    
    bool operator<(const ChunkCoord& other) const {
        if (resolution != other.resolution) return resolution < other.resolution;
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
    
    std::string toString() const {
        return std::to_string(x) + "_" + std::to_string(y) + "_" + std::to_string(z) + "_" + 
               std::to_string(static_cast<int>(resolution));
    }
};

// Hash function for ChunkCoord to use in unordered_map
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& coord) const {
        // Combine all fields into a hash
        std::size_t h1 = std::hash<int32_t>{}(coord.x);
        std::size_t h2 = std::hash<int32_t>{}(coord.y);
        std::size_t h3 = std::hash<int32_t>{}(coord.z);
        std::size_t h4 = std::hash<int>{}(static_cast<int>(coord.resolution));
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

class Chunk {
public:
    Chunk();
    Chunk(const ChunkCoord& coord);
    ~Chunk();
    
    // Generate chunk data
    void generate(class game::world::WorldGenerator* generator);
    
    // Load/save from file
    bool loadFromFile(const std::string& filename);
    bool saveToFile(const std::string& filename) const;
    
    // Getters
    const ChunkCoord& getCoord() const { return coord; }
    const math::AABB& getAABB() const { return aabb; }
    bool isVisible() const { return visible; }
    const SVO& getSVO() const { return svo; }
    SVO& getSVO() { return svo; }
    
    // Query material at chunk-local position (meters)
    uint8_t getMaterialAtChunk(float x, float y, float z) const;
    // Query material at world position (meters)
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos) const;
    
    
    void setVisible(bool v) { visible = v; }
    
    // Get resolution in meters
    float getResolution() const;
    float getSize() const; // Chunk size in meters
    
    // Get world position (min corner of chunk)
    math::Vec3 getWorldPosition() const;
    
    // Material queries
    uint8_t getMaterialAtMeters(float x, float y, float z) const;         // meters (chunk-local)
    
    bool isLoaded() const { return loaded; }
    void setLoaded(bool l) { loaded = l; }
    
private:
    ChunkCoord coord;
    math::AABB aabb;
    bool visible;
    bool loaded;
    SVO svo;
    
    void updateAABB();
};

} // namespace voxels
} // namespace chonk


#pragma once

#include "chunk.h"
#include "svo.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include <array>
#include <memory>
#include <vector>

namespace chonk {
namespace voxels {

// ============================================================================
// Super Chunk (2048m, contains 8x8x8 = 512 chunks)
// ============================================================================
// The super chunk has a 3-level SVO for coarse culling/LOD
// Each leaf in the super chunk SVO corresponds to a 256m chunk
// ============================================================================

class SuperChunk {
public:
    SuperChunk();
    explicit SuperChunk(const SuperChunkCoord& coord);
    ~SuperChunk();
    
    // Accessors
    const SuperChunkCoord& getCoord() const { return coord; }
    const math::AABB& getAABB() const { return aabb; }
    math::Vec3 getWorldPosition() const { return coord.toWorldPos(); }
    
    // Chunk management
    Chunk* getChunk(uint32_t localX, uint32_t localY, uint32_t localZ);
    const Chunk* getChunk(uint32_t localX, uint32_t localY, uint32_t localZ) const;
    Chunk* getChunkAtWorldPos(const math::Vec3& worldPos);
    
    // Set a chunk (takes ownership)
    void setChunk(uint32_t localX, uint32_t localY, uint32_t localZ, std::unique_ptr<Chunk> chunk);
    
    // Get all loaded chunks
    std::vector<Chunk*> getLoadedChunks();
    std::vector<const Chunk*> getLoadedChunks() const;
    
    // Get chunks within a distance from a point (for rendering)
    std::vector<const Chunk*> getChunksInRange(const math::Vec3& center, float maxDistance) const;
    
    // Check if fully loaded
    bool isFullyLoaded() const;
    uint32_t getLoadedChunkCount() const;
    
    // Super chunk SVO (for coarse culling)
    const SVO& getSuperSVO() const { return superSVO; }
    void buildSuperSVO(); // Build from loaded chunks
    
    // Material query with LOD
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos, uint32_t chunkMaxDepth) const;
    
    // Popularity tracking for cache ordering
    void recordHit() { hits++; }
    uint32_t getHits() const { return hits; }
    void resetHits() { hits = 0; }
    
    uint32_t getLastAccessFrame() const { return lastAccessFrame; }
    void setLastAccessFrame(uint32_t frame) { lastAccessFrame = frame; }
    
private:
    SuperChunkCoord coord;
    math::AABB aabb;
    
    // 8x8x8 = 512 chunks, indexed as z*64 + y*8 + x
    std::array<std::unique_ptr<Chunk>, CHUNKS_PER_SUPER> chunks;
    
    // Coarse SVO for super chunk level culling (3 levels)
    SVO superSVO;
    
    // Cache statistics
    uint32_t hits = 0;
    uint32_t lastAccessFrame = 0;
    
    // Helper to get linear index
    static uint32_t getIndex(uint32_t x, uint32_t y, uint32_t z) {
        return z * CHUNKS_PER_SUPER_AXIS * CHUNKS_PER_SUPER_AXIS + 
               y * CHUNKS_PER_SUPER_AXIS + x;
    }
    
    void updateAABB();
};

// ============================================================================
// Super Chunk Cache (popularity-ordered list)
// ============================================================================
class SuperChunkCache {
public:
    SuperChunkCache(size_t maxSize = 150);
    ~SuperChunkCache();
    
    // Get or create super chunk (returns null if not cached and can't create)
    SuperChunk* get(const SuperChunkCoord& coord);
    const SuperChunk* get(const SuperChunkCoord& coord) const;
    
    // Add super chunk to cache (takes ownership)
    void add(std::unique_ptr<SuperChunk> superChunk);
    
    // Touch super chunk (move toward front on access)
    void touch(const SuperChunkCoord& coord, uint32_t frame);
    
    // Evict least-used super chunks to make room
    void evictIfNeeded();
    
    // Get all cached super chunks
    std::vector<SuperChunk*> getAll();
    std::vector<const SuperChunk*> getAll() const;
    
    // Get super chunks near a position (sorted by distance)
    std::vector<SuperChunk*> getNear(const math::Vec3& pos, uint32_t count);
    
    size_t size() const { return superList.size(); }
    size_t maxSize() const { return maxCacheSize; }
    
private:
    struct Entry {
        SuperChunkCoord coord;
        std::unique_ptr<SuperChunk> ptr;
    };
    
    std::vector<Entry> superList;  // Ordered by popularity (front = most used)
    std::unordered_map<SuperChunkCoord, size_t, SuperChunkCoordHash> indexMap;
    size_t maxCacheSize;
    
    void moveTowardFront(size_t idx);
};

} // namespace voxels
} // namespace chonk


#pragma once

#include "chunk.h"
#include "world_constants.h"
#include <array>
#include <memory>
#include <vector>

namespace chonk {
namespace voxels {

// ============================================================================
// ChunkArray: 17×17×17 chunks centered on player
// ============================================================================
// The array is indexed 0-16 on each axis
// Center chunk is at index (8, 8, 8)
// World coordinates map to array indices based on the center chunk's world coord
// ============================================================================

class ChunkArray {
public:
    ChunkArray();
    ~ChunkArray();
    
    // ========================================================================
    // Recentering
    // ========================================================================
    
    // Recenter array on new player chunk position
    // Shifts contents so chunks can be reused when possible
    // Returns list of chunk coords that need to be loaded
    std::vector<ChunkCoord> recenter(const ChunkCoord& newCenter);
    
    // Get current center chunk coordinate
    const ChunkCoord& getCenter() const { return centerCoord; }
    
    // Check if array has been initialized (centered at least once)
    bool isInitialized() const { return initialized; }
    
    // ========================================================================
    // Chunk Access by Index
    // ========================================================================
    
    // Get chunk by array index (0-16 each axis)
    Chunk* getByIndex(uint32_t ix, uint32_t iy, uint32_t iz);
    const Chunk* getByIndex(uint32_t ix, uint32_t iy, uint32_t iz) const;
    
    // Set chunk at array index (takes ownership)
    void setChunk(uint32_t ix, uint32_t iy, uint32_t iz, std::unique_ptr<Chunk> chunk);
    
    // Remove chunk at index
    std::unique_ptr<Chunk> removeChunk(uint32_t ix, uint32_t iy, uint32_t iz);
    
    // ========================================================================
    // Chunk Access by World Position
    // ========================================================================
    
    // Get chunk by world position (returns nullptr if outside array bounds)
    Chunk* getByWorldPos(const math::Vec3& worldPos);
    const Chunk* getByWorldPos(const math::Vec3& worldPos) const;
    
    // Get chunk by chunk coordinate (returns nullptr if outside array bounds)
    Chunk* getByChunkCoord(const ChunkCoord& coord);
    const Chunk* getByChunkCoord(const ChunkCoord& coord) const;
    
    // ========================================================================
    // Coordinate Conversion
    // ========================================================================
    
    // Convert world position to array index (returns false if outside bounds)
    bool worldPosToIndex(const math::Vec3& worldPos, uint32_t& ix, uint32_t& iy, uint32_t& iz) const;
    
    // Convert chunk coordinate to array index (returns false if outside bounds)
    bool chunkCoordToIndex(const ChunkCoord& coord, uint32_t& ix, uint32_t& iy, uint32_t& iz) const;
    
    // Convert array index to chunk coordinate
    ChunkCoord indexToChunkCoord(uint32_t ix, uint32_t iy, uint32_t iz) const;
    
    // Check if a chunk coordinate is within the array bounds
    bool isInBounds(const ChunkCoord& coord) const;
    
    // ========================================================================
    // LOD Information
    // ========================================================================
    
    // Get required LOD depth for an array index
    uint32_t getRequiredDepth(uint32_t ix, uint32_t iy, uint32_t iz) const;
    
    // Get required LOD depth for a chunk coordinate
    uint32_t getRequiredDepthForCoord(const ChunkCoord& coord) const;
    
    // ========================================================================
    // Loading Management
    // ========================================================================
    
    struct ChunkLoadInfo {
        ChunkCoord coord;
        uint32_t ix, iy, iz;       // Array indices
        uint32_t requiredDepth;
        float priority;            // Lower = higher priority (distance-based)
    };
    
    // Get list of chunks that need loading or depth upgrade
    std::vector<ChunkLoadInfo> getChunksNeedingLoad() const;
    
    // Get all loaded chunks for rendering
    std::vector<const Chunk*> getAllLoadedChunks() const;
    
    // Get array origin (world position of chunk at index 0,0,0)
    math::Vec3 getArrayOrigin() const;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    size_t getLoadedCount() const;
    size_t getTotalSlots() const { return CHUNK_ARRAY_TOTAL; }
    
private:
    std::array<std::unique_ptr<Chunk>, CHUNK_ARRAY_TOTAL> chunks;
    ChunkCoord centerCoord{0, 0, 0};
    bool initialized = false;
    
    // Convert 3D index to flat array index
    static uint32_t toFlatIndex(uint32_t x, uint32_t y, uint32_t z) {
        return x + y * CHUNK_ARRAY_SIZE + z * CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE;
    }
    
    // Convert flat index to 3D indices
    static void fromFlatIndex(uint32_t flat, uint32_t& x, uint32_t& y, uint32_t& z) {
        x = flat % CHUNK_ARRAY_SIZE;
        y = (flat / CHUNK_ARRAY_SIZE) % CHUNK_ARRAY_SIZE;
        z = flat / (CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE);
    }
};

} // namespace voxels
} // namespace chonk


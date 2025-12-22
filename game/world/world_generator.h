#pragma once

#include "../../engine/voxels/chunk.h"
#include "../../engine/voxels/world_constants.h"
#include "../../engine/math/vec3.h"
#include <functional>

namespace chonk {
namespace game {
namespace world {

// ============================================================================
// WorldGenerator: Procedural terrain generation
// ============================================================================
// Uses lazy evaluation - only generates nodes down to the requested depth
// Never iterates through all voxels in a chunk
// ============================================================================

class WorldGenerator {
public:
    WorldGenerator();
    ~WorldGenerator() = default;
    
    // ========================================================================
    // Chunk Generation
    // ========================================================================
    
    // Generate chunk to specific depth (for LOD)
    // Uses analytical fast path for layered terrain
    void generateChunk(voxels::Chunk& chunk, uint32_t maxDepth = voxels::CHUNK_TREE_MAX_DEPTH);
    
    // Generate chunk using generic terrain function (slower but flexible)
    void generateChunkProcedural(voxels::Chunk& chunk, uint32_t maxDepth = voxels::CHUNK_TREE_MAX_DEPTH);
    
    // ========================================================================
    // Point Query
    // ========================================================================
    
    // Get material at world position (for generic terrain queries)
    uint8_t getMaterialAt(const math::Vec3& worldPos) const;
    
    // Get terrain function (for custom generation)
    std::function<uint8_t(const math::Vec3&)> getTerrainFunction() const;
    
    // ========================================================================
    // Terrain Configuration
    // ========================================================================
    
    // Height thresholds
    float getStoneHeight() const { return stoneHeight; }
    float getDirtHeight() const { return dirtHeight; }
    float getGrassHeight() const { return grassHeight; }
    
    void setStoneHeight(float h) { stoneHeight = h; }
    void setDirtHeight(float h) { dirtHeight = h; }
    void setGrassHeight(float h) { grassHeight = h; }
    
    // Ground level (where grass meets air)
    float getGroundLevel() const { return grassHeight; }
    
private:
    // Terrain layer heights
    float stoneHeight;
    float dirtHeight;
    float grassHeight;
};

} // namespace world
} // namespace game
} // namespace chonk

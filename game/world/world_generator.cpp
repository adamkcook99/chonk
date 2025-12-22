#include "world_generator.h"
#include "../../engine/voxels/chunk.h"
#include "../../engine/voxels/world_constants.h"

namespace chonk {
namespace game {
namespace world {

WorldGenerator::WorldGenerator()
    : stoneHeight(voxels::STONE_HEIGHT)
    , dirtHeight(voxels::DIRT_HEIGHT)
    , grassHeight(voxels::GRASS_HEIGHT)
{
}

// ============================================================================
// Point Query
// ============================================================================

uint8_t WorldGenerator::getMaterialAt(const math::Vec3& worldPos) const {
    float y = worldPos.y;
    
    // Simple layered terrain
    // Stone → Dirt → Grass → Air (from bottom to top)
    if (y < stoneHeight) {
        return voxels::MAT_STONE;
    } else if (y < dirtHeight) {
        return voxels::MAT_DIRT;
    } else if (y < grassHeight) {
        return voxels::MAT_GRASS;
    } else {
        return voxels::MAT_AIR;
    }
}

std::function<uint8_t(const math::Vec3&)> WorldGenerator::getTerrainFunction() const {
    // Capture heights by value for thread safety
    float stone = stoneHeight;
    float dirt = dirtHeight;
    float grass = grassHeight;
    
    return [stone, dirt, grass](const math::Vec3& pos) -> uint8_t {
        float y = pos.y;
        if (y < stone) return voxels::MAT_STONE;
        else if (y < dirt) return voxels::MAT_DIRT;
        else if (y < grass) return voxels::MAT_GRASS;
        else return voxels::MAT_AIR;
    };
}

// ============================================================================
// Chunk Generation
// ============================================================================

void WorldGenerator::generateChunk(voxels::Chunk& chunk, uint32_t maxDepth) {
    // Use the fast analytical path for layered terrain
    // This never iterates through all voxels - only samples boundaries
    chunk.generateLayered(maxDepth, stoneHeight, dirtHeight, grassHeight);
}

void WorldGenerator::generateChunkProcedural(voxels::Chunk& chunk, uint32_t maxDepth) {
    // Use the generic procedural path (slower but flexible)
    auto terrainFn = getTerrainFunction();
    chunk.generateWithFunction(terrainFn, maxDepth);
}

} // namespace world
} // namespace game
} // namespace chonk

#include "world_generator.h"
#include "../../engine/voxels/chunk.h"

namespace chonk {
namespace game {
namespace world {

WorldGenerator::WorldGenerator() {
}

uint8_t WorldGenerator::getMaterialAt(const math::Vec3& worldPos, float resolution) {
    float y = worldPos.y;
    
    // Generate layered terrain (ground level at ~4096m, middle of 8192m world)
    // Stone: y < 4032m
    // Dirt: 4032m <= y < 4080m (48m layer)
    // Grass: 4080m <= y < 4096m (16m layer)
    // Air: y >= 4096m
    if (y < STONE_HEIGHT) {
        return 3; // Stone
    } else if (y < DIRT_HEIGHT) {
        return 2; // Dirt
    } else if (y < GRASS_HEIGHT) {
        return 1; // Grass
    } else {
        return 0; // Air
    }
}

void WorldGenerator::generateChunk(voxels::Chunk& chunk) {
    chunk.generate(this);
}

} // namespace world
} // namespace game
} // namespace chonk


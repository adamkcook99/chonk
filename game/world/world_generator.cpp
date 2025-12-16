#include "world_generator.h"
#include "../../engine/voxels/chunk.h"
#include "../../engine/voxels/world_constants.h"

namespace chonk {
namespace game {
namespace world {

WorldGenerator::WorldGenerator() {
}

uint8_t WorldGenerator::getMaterialAt(const math::Vec3& worldPos, float /*resolution*/) {
    float y = worldPos.y;
    
    // Generate layered terrain
    // World height: 10240m
    // Ground level around 5120m (middle of world)
    //
    // Stone: y < 4096m
    // Dirt: 4096m <= y < 4864m (768m layer)
    // Grass: 4864m <= y < 5120m (256m layer)
    // Air: y >= 5120m
    
    if (y < voxels::STONE_HEIGHT) {
        return 3; // Stone
    } else if (y < voxels::DIRT_HEIGHT) {
        return 2; // Dirt
    } else if (y < voxels::GRASS_HEIGHT) {
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

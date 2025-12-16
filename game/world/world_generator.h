#pragma once

#include "../../engine/voxels/chunk.h"
#include "../../engine/voxels/world_constants.h"
#include "../../engine/math/vec3.h"

namespace chonk {
namespace game {
namespace world {

class WorldGenerator {
public:
    WorldGenerator();
    
    // Generate chunk data at given coordinate
    void generateChunk(voxels::Chunk& chunk);
    
    // Get material at world position
    virtual uint8_t getMaterialAt(const math::Vec3& worldPos, float resolution);
    
private:
    // Using constants from world_constants.h:
    // STONE_HEIGHT = 4096.0f
    // DIRT_HEIGHT = 4864.0f  
    // GRASS_HEIGHT = 5120.0f
    // WORLD_HEIGHT_METERS = 10240.0f
};

} // namespace world
} // namespace game
} // namespace chonk

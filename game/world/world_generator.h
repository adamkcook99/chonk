#pragma once

#include "../../engine/voxels/chunk.h"
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
    // World dimensions based on LOD system:
    // 18x18x18 chunk render distance with center 4x4x4 at 16m
    // Layers double outward: 32, 64, 128, 256, 512, 1024, 2048
    // Total: 64 + 2*(32+64+128+256+512+1024+2048) = 8192m
    static constexpr float WORLD_HEIGHT = 8192.0f; // meters
    
    // Terrain layers (ground level around 4096m - middle of world)
    static constexpr float STONE_HEIGHT = 4032.0f;  // Stone from 0 to 4032m
    static constexpr float DIRT_HEIGHT = 4080.0f;   // Dirt from 4032 to 4080m (48m layer)
    static constexpr float GRASS_HEIGHT = 4096.0f;  // Grass from 4080 to 4096m (16m layer)
};

} // namespace world
} // namespace game
} // namespace chonk


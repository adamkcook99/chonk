#pragma once

#include <cstdint>

namespace chonk {
namespace voxels {

// ============================================================================
// World Structure Constants
// ============================================================================
// 
// Hierarchy (bottom-up):
//   Block: 0.25m (smallest unit)
//   Leaf:  4x4x4 blocks = 1m (64 blocks per leaf array)
//   Chunk: 256m (SVO with 8 levels, each leaf points to 4x4x4 block array)
//   Super Chunk: 2048m (8x8x8 chunks = 512 chunks, SVO with 3 levels)
//   World: 5x5x5 super chunks = 10240m view distance
//
// ============================================================================

// Block constants
static constexpr float BLOCK_SIZE_METERS = 0.25f;

// Leaf constants (4x4x4 block arrays at SVO leaves)
static constexpr uint32_t LEAF_BLOCKS_PER_AXIS = 4;
static constexpr uint32_t LEAF_BLOCK_COUNT = LEAF_BLOCKS_PER_AXIS * LEAF_BLOCKS_PER_AXIS * LEAF_BLOCKS_PER_AXIS; // 64
static constexpr float LEAF_SIZE_METERS = LEAF_BLOCKS_PER_AXIS * BLOCK_SIZE_METERS; // 1m

// Chunk constants (256m, SVO with 8 levels)
static constexpr uint32_t CHUNK_SVO_DEPTH = 8;
static constexpr uint32_t CHUNK_CELLS_PER_AXIS = 1 << CHUNK_SVO_DEPTH; // 256
static constexpr float CHUNK_SIZE_METERS = 256.0f;

// Super chunk constants (2048m, 8x8x8 chunks)
static constexpr uint32_t SUPER_CHUNK_SVO_DEPTH = 3;
static constexpr uint32_t CHUNKS_PER_SUPER_AXIS = 1 << SUPER_CHUNK_SVO_DEPTH; // 8
static constexpr uint32_t CHUNKS_PER_SUPER = CHUNKS_PER_SUPER_AXIS * CHUNKS_PER_SUPER_AXIS * CHUNKS_PER_SUPER_AXIS; // 512
static constexpr float SUPER_CHUNK_SIZE_METERS = CHUNK_SIZE_METERS * CHUNKS_PER_SUPER_AXIS; // 2048m

// View distance - keep super chunk grid but limit actual render distance
static constexpr uint32_t SUPER_CHUNKS_PER_AXIS = 5;
static constexpr uint32_t TOTAL_SUPER_CHUNKS = SUPER_CHUNKS_PER_AXIS * SUPER_CHUNKS_PER_AXIS * SUPER_CHUNKS_PER_AXIS; // 125
static constexpr float VIEW_DISTANCE_METERS = 10240.0f;  // Match LOD thresholds

// World height
static constexpr float WORLD_HEIGHT_METERS = 10240.0f;

// ============================================================================
// Distance-based LOD thresholds
// ============================================================================
// maxDepth determines how deep to traverse the SVO (8 = full detail)
// These thresholds can be tuned for visual quality vs performance

struct LODThreshold {
    float distance;
    uint32_t maxDepth;
};

// Default LOD thresholds (distance in meters -> max SVO depth)
static constexpr LODThreshold DEFAULT_LOD_THRESHOLDS[] = {
    { 300.0f,  8 },   // < 300m: full detail
    { 750.0f,  7 },   // 300-750m
    { 1000.0f, 6 },   // 750-1000m
    { 1250.0f, 5 },   // 1000-1250m
    { 1500.0f, 4 },   // 1250-1500m
    { 2000.0f, 3 },   // 1500-2000m
    { 2500.0f, 2 },   // 2000-2500m
    { 110000.0f, 1 },   // 2500-3500m: root only
};

static constexpr uint32_t NUM_LOD_THRESHOLDS = sizeof(DEFAULT_LOD_THRESHOLDS) / sizeof(LODThreshold);

// Get max depth for a given distance
inline uint32_t getMaxDepthForDistance(float distance) {
    for (uint32_t i = 0; i < NUM_LOD_THRESHOLDS; ++i) {
        if (distance < DEFAULT_LOD_THRESHOLDS[i].distance) {
            return DEFAULT_LOD_THRESHOLDS[i].maxDepth;
        }
    }
    return 1; // Root only for very far
}

// ============================================================================
// Terrain heights (ground level around 5000m)
// ============================================================================
static constexpr float STONE_HEIGHT = 4096.0f;   // Stone: 0 to 4096m
static constexpr float DIRT_HEIGHT = 4864.0f;    // Dirt: 4096 to 4864m (768m layer)
static constexpr float GRASS_HEIGHT = 5120.0f;   // Grass: 4864 to 5120m (256m layer)
// Air: >= 5120m

} // namespace voxels
} // namespace chonk


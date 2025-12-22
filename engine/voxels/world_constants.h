#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace chonk {
namespace voxels {

// ============================================================================
// 27-ary Tree Constants
// ============================================================================
static constexpr uint32_t CHILDREN_PER_NODE = 27;      // 3x3x3
static constexpr uint32_t CHILDREN_PER_AXIS = 3;

// Node flags (5 bits available in child_mask uint32, bits 27-31)
static constexpr uint32_t FLAG_HAS_MORE_DETAIL = (1u << 27);
static constexpr uint32_t FLAG_IS_LEAF         = (1u << 28);
static constexpr uint32_t FLAG_IS_AIR          = (1u << 29);
static constexpr uint32_t FLAG_RESERVED1       = (1u << 30);
static constexpr uint32_t FLAG_RESERVED2       = (1u << 31);
static constexpr uint32_t CHILD_MASK_BITS      = 0x07FFFFFFu;  // Lower 27 bits

// ============================================================================
// Voxel & Chunk Dimensions
// ============================================================================
// Smallest voxel: 1/3 meter (0.333m)
// Chunk size: 729m (3^6 meters)
// At full depth 7: 3^7 = 2187 voxels per axis → 729/2187 = 0.333m per voxel

static constexpr float SMALLEST_VOXEL_METERS = 1.0f / 3.0f;  // 0.333m
static constexpr float CHUNK_SIZE_METERS = 729.0f;           // 3^6 meters

// Chunk tree: depth 7 max
// Each depth divides by 3: 729 → 243 → 81 → 27 → 9 → 3 → 1 → 0.33m
static constexpr uint32_t CHUNK_TREE_MAX_DEPTH = 7;

// World tree: depth 6
// Each leaf in world tree = 1 chunk (729m)
// Full world tree covers 3^6 = 729 chunks per axis = 531,441m
static constexpr uint32_t WORLD_TREE_MAX_DEPTH = 6;

// ============================================================================
// Chunk Array (17×17×17 centered on player)
// ============================================================================
static constexpr uint32_t CHUNK_ARRAY_SIZE = 17;
static constexpr uint32_t CHUNK_ARRAY_CENTER = 8;  // Index of center chunk (0-indexed)
static constexpr uint32_t CHUNK_ARRAY_TOTAL = CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE;  // 4913

// Full resolution region (3×3×3 center)
static constexpr uint32_t FULL_RES_REGION_SIZE = 3;
static constexpr uint32_t FULL_RES_REGION_MIN = 7;   // (17-3)/2
static constexpr uint32_t FULL_RES_REGION_MAX = 9;   // 7+3-1

// ============================================================================
// LOD Depth by Layer
// ============================================================================
// Layer 0 (center 3×3×3): full depth 7
// Layer 1 (5×5×5 shell): depth 6
// Layer 2 (7×7×7 shell): depth 5
// ...
// Layer 7 (17×17×17 outer shell): depth 0 (root only)

inline uint32_t getChunkDepthForLayer(uint32_t layer) {
    if (layer == 0) return CHUNK_TREE_MAX_DEPTH;  // 7
    if (layer >= CHUNK_TREE_MAX_DEPTH) return 0;
    return CHUNK_TREE_MAX_DEPTH - layer;
}

// Calculate layer from chunk offset relative to center
// dx, dy, dz are signed offsets from center chunk (-8 to +8)
inline uint32_t getLayerFromOffset(int32_t dx, int32_t dy, int32_t dz) {
    uint32_t ax = static_cast<uint32_t>(std::abs(dx));
    uint32_t ay = static_cast<uint32_t>(std::abs(dy));
    uint32_t az = static_cast<uint32_t>(std::abs(dz));
    uint32_t maxDist = std::max({ax, ay, az});
    
    // Center 3×3×3 (offsets -1 to +1) is layer 0
    if (maxDist <= 1) return 0;
    // Each shell beyond is one layer higher
    return maxDist - 1;
}

// Get required depth for array index
inline uint32_t getRequiredDepthForIndex(uint32_t ix, uint32_t iy, uint32_t iz) {
    int32_t dx = static_cast<int32_t>(ix) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    int32_t dy = static_cast<int32_t>(iy) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    int32_t dz = static_cast<int32_t>(iz) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    uint32_t layer = getLayerFromOffset(dx, dy, dz);
    return getChunkDepthForLayer(layer);
}

// ============================================================================
// World Bounds
// ============================================================================
static constexpr float WORLD_HEIGHT_METERS = CHUNK_SIZE_METERS * CHUNK_ARRAY_SIZE;  // 12,393m
static constexpr float VIEW_DISTANCE_METERS = CHUNK_SIZE_METERS * CHUNK_ARRAY_CENTER;  // ~5,832m

// ============================================================================
// Node byte sizes (for networking/GPU)
// ============================================================================
static constexpr size_t NODE_SIZE_BYTES = 12;  // firstChild(4) + childMask(4) + material(1) + fill(1) + pad(2)

// ============================================================================
// Terrain Heights (ground level at half world height)
// ============================================================================
static constexpr float GROUND_LEVEL = WORLD_HEIGHT_METERS * 0.5f;  // ~6196m
static constexpr float STONE_HEIGHT = GROUND_LEVEL - 1000.0f;      // ~5196m
static constexpr float DIRT_HEIGHT = GROUND_LEVEL - 100.0f;        // ~6096m  
static constexpr float GRASS_HEIGHT = GROUND_LEVEL;                // ~6196m
// Air: above GRASS_HEIGHT

// ============================================================================
// Material IDs
// ============================================================================
static constexpr uint8_t MAT_AIR = 0;
static constexpr uint8_t MAT_GRASS = 1;
static constexpr uint8_t MAT_DIRT = 2;
static constexpr uint8_t MAT_STONE = 3;

} // namespace voxels
} // namespace chonk

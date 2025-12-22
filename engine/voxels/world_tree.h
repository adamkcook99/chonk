#pragma once

#include "tree27.h"
#include "chunk.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include <unordered_map>
#include <mutex>

namespace chonk {
namespace voxels {

// ============================================================================
// WorldTree: Global 27-ary tree where leaves are chunks
// ============================================================================
// Depth 6 → each leaf represents one 729m chunk
// Covers 3^6 = 729 chunks per axis at full depth
// Total coverage: 729 * 729m = 531,441m per axis
//
// The world tree is sparse - only regions with actual chunks are populated.
// It stores aggregated material/fill data for distant LOD rendering.
// ============================================================================

class WorldTree {
public:
    WorldTree();
    ~WorldTree();
    
    // ========================================================================
    // Chunk Integration
    // ========================================================================
    
    // Update tree when a chunk is loaded (integrates chunk's root stats)
    void updateFromChunk(const Chunk& chunk);
    
    // Remove chunk from tree (when chunk is unloaded)
    void removeChunk(const ChunkCoord& coord);
    
    // ========================================================================
    // Queries
    // ========================================================================
    
    // Query material at world position (for rays beyond chunk array)
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos, uint32_t maxDepth = WORLD_TREE_MAX_DEPTH) const;
    
    // Check if position is within world tree bounds
    bool isInBounds(const math::Vec3& worldPos) const;
    
    // Check if a chunk coordinate has been integrated
    bool hasChunk(const ChunkCoord& coord) const;
    
    // ========================================================================
    // Tree Access (for GPU upload)
    // ========================================================================
    
    const Tree27& getTree() const { return tree; }
    Tree27& getTree() { return tree; }
    
    // Get world origin (min corner of tree coverage)
    const math::Vec3& getOrigin() const { return worldOrigin; }
    
    // Get total size covered by tree
    float getWorldSize() const { return worldSize; }
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    // Initialize tree centered on a position
    // This sets up the world origin and bounds
    void initialize(const math::Vec3& centerPos);
    
    // Recenter tree (when player moves significantly)
    void recenter(const math::Vec3& newCenter);
    
    // Clear all data
    void clear();
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    size_t getIntegratedChunkCount() const { return integratedChunks.size(); }
    size_t getNodeCount() const { return tree.getNodeCount(); }
    
private:
    Tree27 tree;
    
    // World bounds
    math::Vec3 worldOrigin;  // Min corner of covered region
    float worldSize;         // Total size (729 * 729 = 531,441m at depth 6)
    
    // Track which chunks have been integrated
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> integratedChunks;  // coord → node index
    
    // Mutex for thread-safe updates
    mutable std::mutex treeMutex;
    
    // ========================================================================
    // Internal helpers
    // ========================================================================
    
    // Convert chunk coord to world tree local position
    math::Vec3 chunkCoordToTreePos(const ChunkCoord& coord) const;
    
    // Insert or update a leaf for a chunk
    void insertChunkLeaf(const ChunkCoord& coord, uint8_t material, uint8_t fillPercent);
    
    // Propagate changes up from a modified leaf
    void propagateUp(const ChunkCoord& coord);
    
    // Build tree structure down to chunk level
    void ensurePathExists(const ChunkCoord& coord);
};

} // namespace voxels
} // namespace chonk


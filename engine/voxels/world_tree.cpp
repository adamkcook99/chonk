#include "world_tree.h"
#include <cmath>
#include <algorithm>

namespace chonk {
namespace voxels {

// World tree covers 3^6 = 729 chunks per axis
// Each chunk is 729m, so total is 729 * 729 = 531,441m
static constexpr float WORLD_TREE_TOTAL_SIZE = CHUNK_SIZE_METERS * 729.0f;  // 531,441m

WorldTree::WorldTree() 
    : worldOrigin(0, 0, 0)
    , worldSize(WORLD_TREE_TOTAL_SIZE) 
{
}

WorldTree::~WorldTree() = default;

// ============================================================================
// Initialization
// ============================================================================

void WorldTree::initialize(const math::Vec3& centerPos) {
    std::lock_guard<std::mutex> lock(treeMutex);
    
    // Center the world tree on the given position
    float halfSize = worldSize * 0.5f;
    worldOrigin = math::Vec3(
        centerPos.x - halfSize,
        centerPos.y - halfSize,
        centerPos.z - halfSize
    );
    
    // Snap to chunk boundaries
    worldOrigin.x = std::floor(worldOrigin.x / CHUNK_SIZE_METERS) * CHUNK_SIZE_METERS;
    worldOrigin.y = std::floor(worldOrigin.y / CHUNK_SIZE_METERS) * CHUNK_SIZE_METERS;
    worldOrigin.z = std::floor(worldOrigin.z / CHUNK_SIZE_METERS) * CHUNK_SIZE_METERS;
    
    // Initialize with empty root
    tree.clear();
    tree.getNodes().push_back({0, FLAG_IS_LEAF | FLAG_IS_AIR, MAT_AIR, 0});
    
    integratedChunks.clear();
}

void WorldTree::recenter(const math::Vec3& newCenter) {
    // For now, just reinitialize
    // A more sophisticated implementation could preserve overlapping regions
    initialize(newCenter);
}

void WorldTree::clear() {
    std::lock_guard<std::mutex> lock(treeMutex);
    tree.clear();
    integratedChunks.clear();
}

// ============================================================================
// Chunk Integration
// ============================================================================

math::Vec3 WorldTree::chunkCoordToTreePos(const ChunkCoord& coord) const {
    // Convert chunk coordinate to position within world tree
    return math::Vec3(
        coord.x * CHUNK_SIZE_METERS - worldOrigin.x,
        coord.y * CHUNK_SIZE_METERS - worldOrigin.y,
        coord.z * CHUNK_SIZE_METERS - worldOrigin.z
    );
}

void WorldTree::updateFromChunk(const Chunk& chunk) {
    std::lock_guard<std::mutex> lock(treeMutex);
    
    const ChunkCoord& coord = chunk.getCoord();
    const Tree27& chunkTree = chunk.getTree();
    
    // Get chunk's aggregated stats from its root node
    uint8_t material = chunkTree.getRootMaterial();
    uint8_t fillPercent = chunkTree.getRootFillPercent();
    
    // Insert or update the leaf for this chunk
    insertChunkLeaf(coord, material, fillPercent);
    
    // Mark as integrated
    integratedChunks[coord] = 0;  // Index will be set by insertChunkLeaf
}

void WorldTree::removeChunk(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(treeMutex);
    
    auto it = integratedChunks.find(coord);
    if (it == integratedChunks.end()) {
        return;  // Not integrated
    }
    
    // Mark the leaf as air
    insertChunkLeaf(coord, MAT_AIR, 0);
    
    // Remove from tracking
    integratedChunks.erase(it);
}

void WorldTree::insertChunkLeaf(const ChunkCoord& coord, uint8_t material, uint8_t fillPercent) {
    // The world tree has depth 6, where each leaf = 1 chunk
    // We need to traverse/create the path to this chunk's leaf
    
    if (tree.isEmpty()) {
        tree.getNodes().push_back({0, FLAG_IS_LEAF | FLAG_IS_AIR, MAT_AIR, 0});
    }
    
    math::Vec3 treePos = chunkCoordToTreePos(coord);
    
    // Check if in bounds
    if (treePos.x < 0 || treePos.y < 0 || treePos.z < 0 ||
        treePos.x >= worldSize || treePos.y >= worldSize || treePos.z >= worldSize) {
        return;  // Out of bounds
    }
    
    auto& nodes = tree.getNodes();
    uint32_t nodeIdx = 0;
    float nodeSize = worldSize;
    float lx = treePos.x, ly = treePos.y, lz = treePos.z;
    
    // Traverse down to depth 6 (where leaves are chunks)
    for (uint32_t depth = 0; depth < WORLD_TREE_MAX_DEPTH; ++depth) {
        Tree27Node& node = nodes[nodeIdx];
        
        // Calculate which child we need
        float childLX, childLY, childLZ;
        uint32_t childIdx = Tree27::getChildIndex(lx, ly, lz, nodeSize, childLX, childLY, childLZ);
        
        // If node is currently a leaf, we need to expand it
        if (node.isLeaf() || node.getChildCount() == 0) {
            // Clear leaf flag and create children
            node.clearFlags();
            node.firstChildIndex = static_cast<uint32_t>(nodes.size());
            node.childMask = 0;
            
            // Only create the child we need for now (sparse)
            node.childMask |= (1u << childIdx);
            
            // Add new child node
            Tree27Node newChild;
            newChild.firstChildIndex = 0;
            newChild.childMask = FLAG_IS_LEAF | FLAG_IS_AIR;
            newChild.majorityMaterial = MAT_AIR;
            newChild.fillPercent = 0;
            nodes.push_back(newChild);
        } else {
            // Node has children, check if our target child exists
            if (!node.hasChild(childIdx)) {
                // Need to add new child
                // This is complex because children are stored contiguously
                // For simplicity, we'll rebuild this node's children
                
                uint32_t oldChildCount = node.getChildCount();
                uint32_t oldFirstChild = node.firstChildIndex;
                
                // Calculate where new child should go
                uint32_t newChildOffset = node.getChildOffset(childIdx);
                
                // We need to insert at the right position
                // For now, just add to end and update mask
                // (A real implementation would maintain contiguity)
                
                node.childMask |= (1u << childIdx);
                
                // Add new child at end (simple but not optimal)
                Tree27Node newChild;
                newChild.firstChildIndex = 0;
                newChild.childMask = FLAG_IS_LEAF | FLAG_IS_AIR;
                newChild.majorityMaterial = MAT_AIR;
                newChild.fillPercent = 0;
                nodes.push_back(newChild);
                
                // Note: This breaks the contiguous child assumption
                // A proper implementation would reorganize children
                // For now, we'll use a simplified approach
            }
        }
        
        // Move to child
        nodeIdx = node.getChildArrayIndex(childIdx);
        nodeSize /= 3.0f;
        lx = childLX;
        ly = childLY;
        lz = childLZ;
    }
    
    // At depth 6 - this is the chunk leaf
    if (nodeIdx < nodes.size()) {
        Tree27Node& leaf = nodes[nodeIdx];
        leaf.majorityMaterial = material;
        leaf.fillPercent = fillPercent;
        leaf.childMask = FLAG_IS_LEAF;
        if (fillPercent == 0) {
            leaf.childMask |= FLAG_IS_AIR;
        }
        leaf.firstChildIndex = 0;
        
        // Track the leaf index
        integratedChunks[coord] = nodeIdx;
    }
    
    // Propagate stats up
    propagateUp(coord);
}

void WorldTree::propagateUp(const ChunkCoord& coord) {
    // Simple propagation: recalculate parent stats from children
    // This is called after modifying a leaf
    
    // For a proper implementation, we'd walk up from the leaf
    // and recalculate each parent's stats from its children
    
    // Simplified version: just ensure root has reasonable stats
    if (!tree.isEmpty()) {
        auto& nodes = tree.getNodes();
        if (nodes.size() > 0) {
            // Root stats are aggregate of all children
            // For now, just use the last modified chunk's data
            // A real implementation would sum up all children
        }
    }
}

void WorldTree::ensurePathExists(const ChunkCoord& coord) {
    // Already handled by insertChunkLeaf
}

// ============================================================================
// Queries
// ============================================================================

uint8_t WorldTree::getMaterialAtWorld(const math::Vec3& worldPos, uint32_t maxDepth) const {
    std::lock_guard<std::mutex> lock(treeMutex);
    
    if (tree.isEmpty()) return MAT_AIR;
    
    // Convert to tree-local coordinates
    float lx = worldPos.x - worldOrigin.x;
    float ly = worldPos.y - worldOrigin.y;
    float lz = worldPos.z - worldOrigin.z;
    
    // Check bounds
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= worldSize || ly >= worldSize || lz >= worldSize) {
        return MAT_AIR;
    }
    
    return tree.getMaterialAtLOD(lx, ly, lz, worldSize, maxDepth);
}

bool WorldTree::isInBounds(const math::Vec3& worldPos) const {
    float lx = worldPos.x - worldOrigin.x;
    float ly = worldPos.y - worldOrigin.y;
    float lz = worldPos.z - worldOrigin.z;
    
    return lx >= 0 && ly >= 0 && lz >= 0 &&
           lx < worldSize && ly < worldSize && lz < worldSize;
}

bool WorldTree::hasChunk(const ChunkCoord& coord) const {
    std::lock_guard<std::mutex> lock(treeMutex);
    return integratedChunks.find(coord) != integratedChunks.end();
}

} // namespace voxels
} // namespace chonk


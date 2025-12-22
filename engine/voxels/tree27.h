#pragma once

#include "world_constants.h"
#include "../math/vec3.h"
#include <cstdint>
#include <vector>
#include <functional>

namespace chonk {
namespace voxels {

// ============================================================================
// 27-ary Tree Node (12 bytes to match GPU alignment)
// ============================================================================
// Structure for GPU-friendly layout
// Children are stored contiguously; firstChildIndex points to first child
// To find child N, count set bits before bit N in childMask

struct Tree27Node {
    uint32_t firstChildIndex;  // Index of first child in node array
    uint32_t childMask;        // Bits 0-26: which children exist, Bits 27-31: flags
    uint8_t  majorityMaterial; // Dominant material for LOD rendering
    uint8_t  fillPercent;      // 0-255 → 0-100% solid (for early-out and LOD)
    uint8_t  _pad1;            // Padding for alignment
    uint8_t  _pad2;            // Padding for alignment
    
    // ========================================================================
    // Child mask helpers
    // ========================================================================
    bool hasChild(uint32_t idx) const { 
        return (childMask & (1u << idx)) != 0; 
    }
    
    void setChild(uint32_t idx, bool exists) {
        if (exists) childMask |= (1u << idx);
        else childMask &= ~(1u << idx);
    }
    
    uint32_t getChildCount() const {
        return static_cast<uint32_t>(__builtin_popcount(childMask & CHILD_MASK_BITS));
    }
    
    // Get offset for child N in contiguous child array
    // (count set bits before position N)
    uint32_t getChildOffset(uint32_t childIdx) const {
        uint32_t mask = (1u << childIdx) - 1;
        return static_cast<uint32_t>(__builtin_popcount(childMask & mask & CHILD_MASK_BITS));
    }
    
    // Get actual array index for child N
    uint32_t getChildArrayIndex(uint32_t childIdx) const {
        return firstChildIndex + getChildOffset(childIdx);
    }
    
    // ========================================================================
    // Flag helpers
    // ========================================================================
    bool hasMoreDetail() const { return (childMask & FLAG_HAS_MORE_DETAIL) != 0; }
    bool isLeaf() const { return (childMask & FLAG_IS_LEAF) != 0 || getChildCount() == 0; }
    bool isAir() const { return (childMask & FLAG_IS_AIR) != 0; }
    
    void setFlag(uint32_t flag, bool value) {
        if (value) childMask |= flag;
        else childMask &= ~flag;
    }
    
    void clearFlags() {
        childMask &= CHILD_MASK_BITS;
    }
};

// Ensure proper size for GPU (4 + 4 + 1 + 1 + 1 + 1 = 12 bytes, aligned)
static_assert(sizeof(Tree27Node) == 12, "Tree27Node must be 12 bytes");

// ============================================================================
// 27-ary Tree Class
// ============================================================================
class Tree27 {
public:
    Tree27();
    ~Tree27();
    
    // ========================================================================
    // Generation Methods
    // ========================================================================
    
    // Generate using arbitrary terrain function (samples at boundaries)
    void generateProcedural(
        std::function<uint8_t(const math::Vec3&)> terrainFn,
        const math::Vec3& origin,      // World position of tree origin (min corner)
        float treeSize,                // Size in meters (729 for chunks)
        uint32_t maxDepth              // How deep to generate (0-7)
    );
    
    // Fast path for simple layered terrain (analytical bounds)
    void generateLayered(
        const math::Vec3& origin,
        float treeSize,
        uint32_t maxDepth,
        float stoneHeight,
        float dirtHeight,
        float grassHeight
    );
    
    // Build from dense voxel array (for testing/importing)
    void buildFromDense(const uint8_t* voxels, uint32_t sizePerAxis, uint32_t maxDepth);
    
    // ========================================================================
    // Query Methods
    // ========================================================================
    
    // Query material at position with LOD depth limit
    // localPos: coordinates within tree (0 to treeSize)
    uint8_t getMaterialAtLOD(float localX, float localY, float localZ, 
                              float treeSize, uint32_t maxDepth) const;
    
    // Get child index from local position within a node
    // Also outputs the local position within the child
    static uint32_t getChildIndex(float localX, float localY, float localZ, 
                                   float nodeSize, 
                                   float& childLocalX, float& childLocalY, float& childLocalZ);
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    const std::vector<Tree27Node>& getNodes() const { return nodes; }
    std::vector<Tree27Node>& getNodes() { return nodes; }
    size_t getNodeCount() const { return nodes.size(); }
    
    bool isEmpty() const { return nodes.empty(); }
    bool isCollapsed() const { return nodes.size() <= 1 && (nodes.empty() || nodes[0].isLeaf()); }
    
    uint8_t getRootMaterial() const { 
        return nodes.empty() ? MAT_AIR : nodes[0].majorityMaterial; 
    }
    uint8_t getRootFillPercent() const {
        return nodes.empty() ? 0 : nodes[0].fillPercent;
    }
    
    void clear() { nodes.clear(); }
    void setNodes(std::vector<Tree27Node>&& newNodes) { nodes = std::move(newNodes); }
    void setNodes(const std::vector<Tree27Node>& newNodes) { nodes = newNodes; }
    
    // ========================================================================
    // Serialization / LOD trimming
    // ========================================================================
    
    // Get tree trimmed to specific depth (for network LOD transmission)
    std::vector<Tree27Node> getNodesTrimmedToDepth(uint32_t maxDepth) const;
    
    // Serialize nodes for network/file (just raw bytes)
    std::vector<uint8_t> serialize() const;
    void deserialize(const uint8_t* data, size_t nodeCount);
    
private:
    std::vector<Tree27Node> nodes;
    
    // ========================================================================
    // Generation helpers (recursive)
    // ========================================================================
    
    // Returns {majorityMaterial, fillPercent}
    std::pair<uint8_t, uint8_t> generateNodeProcedural(
        std::function<uint8_t(const math::Vec3&)>& terrainFn,
        const math::Vec3& nodeOrigin,
        float nodeSize,
        uint32_t depth,
        uint32_t maxDepth
    );
    
    std::pair<uint8_t, uint8_t> generateNodeLayered(
        const math::Vec3& nodeOrigin,
        float nodeSize,
        uint32_t depth,
        uint32_t maxDepth,
        float stoneHeight,
        float dirtHeight,
        float grassHeight
    );
    
    // Dense build helper
    void buildNodeDense(uint32_t nodeIdx, const uint8_t* voxels, uint32_t sizePerAxis,
                        uint32_t x, uint32_t y, uint32_t z, uint32_t nodeSize, 
                        uint32_t depth, uint32_t maxDepth);
    
    // Trimming helper
    uint32_t trimNodeToDepth(uint32_t srcIdx, uint32_t depth, uint32_t maxDepth,
                              std::vector<Tree27Node>& outNodes) const;
};

} // namespace voxels
} // namespace chonk


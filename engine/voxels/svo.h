#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

namespace chonk {
namespace math {
    struct Vec3;
}

namespace voxels {

// ============================================================================
// SVO Node structure
// ============================================================================
// For internal nodes: childBase points to first child, childMask indicates which exist
// For leaf nodes: childMask == 0, material is the block material
//                 leafBlockIndex points to 4x4x4 block array (if using block arrays)
struct SVONode {
    uint32_t childBase;      // Index of first child in node array (children are contiguous)
    uint8_t  childMask;      // 8 bits: which children exist (0 == leaf)
    uint8_t  material;       // Material if leaf, or dominant material for LOD
    uint16_t leafBlockIndex; // For leaves: index into block array storage (0 = no array, use material)
};

// 4x4x4 block array at SVO leaves (64 blocks)
struct LeafBlockArray {
    uint8_t blocks[64]; // 4x4x4 = 64 blocks, indexed as z*16 + y*4 + x
    
    uint8_t get(uint32_t x, uint32_t y, uint32_t z) const {
        return blocks[z * 16 + y * 4 + x];
    }
    
    void set(uint32_t x, uint32_t y, uint32_t z, uint8_t mat) {
        blocks[z * 16 + y * 4 + x] = mat;
    }
    
    // Check if all blocks are the same material
    bool isUniform(uint8_t& outMaterial) const {
        outMaterial = blocks[0];
        for (int i = 1; i < 64; ++i) {
            if (blocks[i] != outMaterial) return false;
        }
        return true;
    }
    
    // Get dominant material (most frequent)
    uint8_t getDominant() const {
        uint32_t counts[256] = {0};
        for (int i = 0; i < 64; ++i) {
            counts[blocks[i]]++;
        }
        uint8_t best = 0;
        uint32_t bestCount = 0;
        for (int i = 0; i < 256; ++i) {
            if (counts[i] > bestCount) {
                bestCount = counts[i];
                best = static_cast<uint8_t>(i);
            }
        }
        return best;
    }
};

// ============================================================================
// SVO class - Sparse Voxel Octree
// ============================================================================
class SVO {
public:
    SVO();
    ~SVO();
    
    // Build SVO from dense voxel data
    // For 256m chunks with 0.25m blocks: size = 1024^3 voxels
    // For 64m chunks with 0.25m blocks: size = 256^3 voxels (legacy)
    void buildFromDense(const uint8_t* voxels, uint32_t sizePerAxis, uint32_t maxDepth = 8);
    
    // Stackless material query with LOD depth limit
    // Returns material at position, stopping at maxDepth
    // chunkSizeMeters: size of the chunk in meters (e.g., 256.0f)
    // localX/Y/Z: position within chunk in meters (0 to chunkSizeMeters)
    uint8_t getMaterialAtLOD(float localX, float localY, float localZ, 
                             float chunkSizeMeters, uint32_t maxDepth) const;
    
    // Legacy interface (full depth)
    uint8_t getMaterial(float x, float y, float z, float resolution) const;
    
    // Ray intersection with LOD
    bool intersectRay(const math::Vec3& origin, const math::Vec3& dir, 
                     float& t, uint8_t& material, float chunkSizeMeters, uint32_t maxDepth) const;
    
    // Get packed data for GPU
    const std::vector<SVONode>& getNodes() const { return nodes; }
    size_t getNodeCount() const { return nodes.size(); }
    
    // Get leaf block arrays (for GPU upload if needed)
    const std::vector<LeafBlockArray>& getLeafBlocks() const { return leafBlocks; }
    size_t getLeafBlockCount() const { return leafBlocks.size(); }
    
    // Check if SVO is collapsed to a single node
    bool isCollapsed() const;
    
    // Get root node material
    uint8_t getRootMaterial() const;
    
    void clear();
    
    // Load/save nodes
    void loadNodes(const std::vector<SVONode>& loadedNodes);
    void loadNodes(const std::vector<SVONode>& loadedNodes, const std::vector<LeafBlockArray>& loadedBlocks);
    
    // Get trimmed nodes up to a certain depth (for GPU upload with LOD)
    std::vector<SVONode> getNodesTrimmedToDepth(uint32_t maxDepth) const;
    
private:
    std::vector<SVONode> nodes;
    std::vector<LeafBlockArray> leafBlocks;
    uint32_t rootIndex = 0;
    uint32_t treeDepth = 0;  // Actual depth of the tree
    
    // Build helpers
    void buildNode(uint32_t nodeIdx, const uint8_t* voxels, uint32_t sizePerAxis,
                   uint32_t x, uint32_t y, uint32_t z, uint32_t nodeSize, uint32_t depth, uint32_t maxDepth);
    
    // Recursive trimming helper
    void trimNodeToDepth(uint32_t srcIdx, uint32_t depth, uint32_t maxDepth,
                        std::vector<SVONode>& outNodes, 
                        std::unordered_map<uint32_t, uint32_t>& indexMap) const;
};

} // namespace voxels
} // namespace chonk

#include "svo.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>

namespace chonk {
namespace voxels {

SVO::SVO() : rootIndex(0), treeDepth(0) {
}

SVO::~SVO() {
}

void SVO::clear() {
    nodes.clear();
    leafBlocks.clear();
    rootIndex = 0;
    treeDepth = 0;
}

bool SVO::isCollapsed() const {
    if (nodes.empty() || rootIndex >= nodes.size()) {
        return true;
    }
    return nodes[rootIndex].childMask == 0;
}

uint8_t SVO::getRootMaterial() const {
    if (nodes.empty() || rootIndex >= nodes.size()) {
        return 0;
    }
    return nodes[rootIndex].material;
}

void SVO::loadNodes(const std::vector<SVONode>& loadedNodes) {
    nodes = loadedNodes;
    leafBlocks.clear();
    rootIndex = nodes.empty() ? 0 : 0;
}

void SVO::loadNodes(const std::vector<SVONode>& loadedNodes, const std::vector<LeafBlockArray>& loadedBlocks) {
    nodes = loadedNodes;
    leafBlocks = loadedBlocks;
    rootIndex = nodes.empty() ? 0 : 0;
}

// ============================================================================
// Stackless material query with LOD
// ============================================================================
uint8_t SVO::getMaterialAtLOD(float localX, float localY, float localZ, 
                               float chunkSizeMeters, uint32_t maxDepth) const {
    if (nodes.empty()) return 0;
    
    // Clamp to valid range
    localX = std::max(0.0f, std::min(localX, chunkSizeMeters - 0.001f));
    localY = std::max(0.0f, std::min(localY, chunkSizeMeters - 0.001f));
    localZ = std::max(0.0f, std::min(localZ, chunkSizeMeters - 0.001f));
    
    uint32_t nodeIdx = rootIndex;
    float size = chunkSizeMeters;
    float lx = localX, ly = localY, lz = localZ;
    
    for (uint32_t depth = 0; depth <= maxDepth && depth <= treeDepth; ++depth) {
        if (nodeIdx >= nodes.size()) return 0;
        const SVONode& node = nodes[nodeIdx];
        
        // If leaf node or at max depth, return material
        if (node.childMask == 0 || depth == maxDepth) {
            // If this leaf has a block array and we want full detail, look up in array
            if (node.childMask == 0 && node.leafBlockIndex > 0 && depth == treeDepth) {
                uint32_t blockIdx = node.leafBlockIndex - 1; // 1-indexed, 0 = no array
                if (blockIdx < leafBlocks.size()) {
                    // Calculate position within 4x4x4 block array
                    float leafSize = size; // Current node size is the leaf size
                    float blockSize = leafSize / LEAF_BLOCKS_PER_AXIS;
                    uint32_t bx = static_cast<uint32_t>(lx / blockSize) % LEAF_BLOCKS_PER_AXIS;
                    uint32_t by = static_cast<uint32_t>(ly / blockSize) % LEAF_BLOCKS_PER_AXIS;
                    uint32_t bz = static_cast<uint32_t>(lz / blockSize) % LEAF_BLOCKS_PER_AXIS;
                    return leafBlocks[blockIdx].get(bx, by, bz);
                }
            }
            return node.material;
        }
        
        // Find which child octant contains the point
        float childSize = size * 0.5f;
        uint32_t childIdx = 0;
        
        if (lx >= childSize) { childIdx |= 1; lx -= childSize; }
        if (ly >= childSize) { childIdx |= 2; ly -= childSize; }
        if (lz >= childSize) { childIdx |= 4; lz -= childSize; }
        
        // Check if child exists
        if ((node.childMask & (1u << childIdx)) == 0) {
            // Child doesn't exist - use parent's dominant material
            return node.material;
        }
        
        // Move to child
        nodeIdx = node.childBase + childIdx;
        size = childSize;
    }
    
    return 0;
}

// Legacy interface
uint8_t SVO::getMaterial(float x, float y, float z, float /*resolution*/) const {
    // Assume 64-voxel chunks for legacy compatibility
    return getMaterialAtLOD(x * 0.25f, y * 0.25f, z * 0.25f, 16.0f, 8);
}

// ============================================================================
// Ray intersection with LOD
// ============================================================================
bool SVO::intersectRay(const math::Vec3& origin, const math::Vec3& dir, 
                       float& t, uint8_t& material, float chunkSizeMeters, uint32_t maxDepth) const {
    if (nodes.empty()) return false;
    
    // Ray-AABB intersection with the chunk bounds
    math::AABB aabb(math::Vec3(0, 0, 0), math::Vec3(chunkSizeMeters, chunkSizeMeters, chunkSizeMeters));
    float tmin, tmax;
    if (!aabb.intersects(origin, dir, tmin, tmax) || tmax < 0) {
        return false;
    }
    
    // Start marching from entry point
    float stepSize = chunkSizeMeters / (1 << maxDepth);
    float currentT = std::max(tmin, 0.0f) + stepSize * 0.1f;
    
    while (currentT < tmax) {
        math::Vec3 pos = origin + dir * currentT;
        
        // Check if inside bounds
        if (pos.x >= 0 && pos.x < chunkSizeMeters &&
            pos.y >= 0 && pos.y < chunkSizeMeters &&
            pos.z >= 0 && pos.z < chunkSizeMeters) {
            
            uint8_t mat = getMaterialAtLOD(pos.x, pos.y, pos.z, chunkSizeMeters, maxDepth);
            if (mat != 0) {
                t = currentT;
                material = mat;
                return true;
            }
        }
        
        currentT += stepSize;
    }
    
    return false;
}

// ============================================================================
// Build SVO from dense voxel data
// ============================================================================
void SVO::buildFromDense(const uint8_t* voxels, uint32_t sizePerAxis, uint32_t maxDepth) {
    clear();
    
    if (sizePerAxis == 0 || voxels == nullptr) {
        // Create empty air node
        SVONode airNode{0, 0, 0, 0};
        nodes.push_back(airNode);
        return;
    }
    
    // Calculate tree depth from size (size must be power of 2)
    treeDepth = 0;
    uint32_t s = sizePerAxis;
    while (s > 1) { s >>= 1; ++treeDepth; }
    treeDepth = std::min(treeDepth, maxDepth);
    
    // Prepare root node
    nodes.resize(1);
    rootIndex = 0;
    
    // Build recursively
    buildNode(0, voxels, sizePerAxis, 0, 0, 0, sizePerAxis, 0, treeDepth);
}

void SVO::buildNode(uint32_t nodeIdx, const uint8_t* voxels, uint32_t sizePerAxis,
                    uint32_t x, uint32_t y, uint32_t z, uint32_t nodeSize, 
                    uint32_t depth, uint32_t maxDepth) {
    
    // Initialize node
    nodes[nodeIdx].childBase = 0;
    nodes[nodeIdx].childMask = 0;
    nodes[nodeIdx].material = 0;
    nodes[nodeIdx].leafBlockIndex = 0;
    
    // If at max depth or node size is 1, this is a leaf
    if (depth >= maxDepth || nodeSize == 1) {
        // Sample material (use center voxel)
        uint32_t idx = z * sizePerAxis * sizePerAxis + y * sizePerAxis + x;
        if (idx < sizePerAxis * sizePerAxis * sizePerAxis) {
            nodes[nodeIdx].material = voxels[idx];
        }
        return;
    }
    
    // Check if all voxels in this region are the same
    uint8_t firstMat = 0;
    bool uniform = true;
    bool hasFirstMat = false;
    
    // Sample corners and center for quick uniformity check
    uint32_t samples[8][3] = {
        {x, y, z},
        {x + nodeSize - 1, y, z},
        {x, y + nodeSize - 1, z},
        {x + nodeSize - 1, y + nodeSize - 1, z},
        {x, y, z + nodeSize - 1},
        {x + nodeSize - 1, y, z + nodeSize - 1},
        {x, y + nodeSize - 1, z + nodeSize - 1},
        {x + nodeSize - 1, y + nodeSize - 1, z + nodeSize - 1}
    };
    
    for (int i = 0; i < 8 && uniform; ++i) {
        uint32_t sx = std::min(samples[i][0], sizePerAxis - 1);
        uint32_t sy = std::min(samples[i][1], sizePerAxis - 1);
        uint32_t sz = std::min(samples[i][2], sizePerAxis - 1);
        uint32_t idx = sz * sizePerAxis * sizePerAxis + sy * sizePerAxis + sx;
        uint8_t mat = voxels[idx];
        
        if (!hasFirstMat) {
            firstMat = mat;
            hasFirstMat = true;
        } else if (mat != firstMat) {
            uniform = false;
        }
    }
    
    // If uniform, collapse to leaf
    if (uniform) {
        nodes[nodeIdx].material = firstMat;
        nodes[nodeIdx].childMask = 0;
        return;
    }
    
    // Not uniform - create children
    uint32_t childSize = nodeSize / 2;
    uint32_t childBase = nodes.size();
    nodes.resize(childBase + 8);
    
    nodes[nodeIdx].childBase = childBase;
    
    uint8_t childMask = 0;
    uint32_t materialCounts[256] = {0};
    
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t cx = x + ((i & 1) ? childSize : 0);
        uint32_t cy = y + ((i & 2) ? childSize : 0);
        uint32_t cz = z + ((i & 4) ? childSize : 0);
        
        uint32_t childIdx = childBase + i;
        buildNode(childIdx, voxels, sizePerAxis, cx, cy, cz, childSize, depth + 1, maxDepth);
        
        const SVONode& child = nodes[childIdx];
        if (child.material != 0 || child.childMask != 0) {
            childMask |= (1 << i);
        }
        
        // Track materials for dominant calculation
        if (child.childMask == 0) {
            materialCounts[child.material]++;
        }
    }
    
    // If all children are empty (air), collapse
    if (childMask == 0) {
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].material = 0;
        nodes.resize(childBase); // Remove children
        return;
    }
    
    // Check if all children are leaves with same material
    bool allSameLeaves = true;
    uint8_t leafMat = 0;
    bool hasLeafMat = false;
    
    for (uint32_t i = 0; i < 8; ++i) {
        const SVONode& child = nodes[childBase + i];
        if (child.childMask != 0) {
            allSameLeaves = false;
            break;
        }
        if (!hasLeafMat) {
            leafMat = child.material;
            hasLeafMat = true;
        } else if (child.material != leafMat) {
            allSameLeaves = false;
            break;
        }
    }
    
    if (allSameLeaves && hasLeafMat) {
        // Collapse to single leaf
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].material = leafMat;
        nodes.resize(childBase);
        return;
    }
    
    // Calculate dominant material for LOD
    uint8_t dominant = 0;
    uint32_t bestCount = 0;
    for (int i = 0; i < 256; ++i) {
        if (materialCounts[i] > bestCount) {
            bestCount = materialCounts[i];
            dominant = static_cast<uint8_t>(i);
        }
    }
    
    nodes[nodeIdx].childMask = childMask;
    nodes[nodeIdx].material = dominant;
}

// ============================================================================
// Get nodes trimmed to a certain depth for GPU
// ============================================================================
std::vector<SVONode> SVO::getNodesTrimmedToDepth(uint32_t maxDepth) const {
    if (nodes.empty() || maxDepth >= treeDepth) {
        return nodes; // No trimming needed
    }
    
    std::vector<SVONode> result;
    std::unordered_map<uint32_t, uint32_t> indexMap;
    
    // Start with root
    result.push_back(nodes[rootIndex]);
    indexMap[rootIndex] = 0;
    
    // Process nodes level by level
    std::vector<std::pair<uint32_t, uint32_t>> currentLevel; // (srcIdx, dstIdx)
    currentLevel.push_back({rootIndex, 0});
    
    for (uint32_t depth = 0; depth < maxDepth && !currentLevel.empty(); ++depth) {
        std::vector<std::pair<uint32_t, uint32_t>> nextLevel;
        
        for (auto& [srcIdx, dstIdx] : currentLevel) {
            const SVONode& srcNode = nodes[srcIdx];
            
            if (srcNode.childMask == 0) continue; // Leaf, no children
            
            // Copy children
            uint32_t newChildBase = result.size();
            result[dstIdx].childBase = newChildBase;
            
            for (uint32_t i = 0; i < 8; ++i) {
                if (srcNode.childMask & (1 << i)) {
                    uint32_t srcChildIdx = srcNode.childBase + i;
                    uint32_t dstChildIdx = result.size();
                    
                    SVONode child = nodes[srcChildIdx];
                    
                    // If at max depth - 1, make children into leaves
                    if (depth == maxDepth - 1) {
                        child.childMask = 0;
                        // Keep material as-is (dominant)
                    }
                    
                    result.push_back(child);
                    indexMap[srcChildIdx] = dstChildIdx;
                    nextLevel.push_back({srcChildIdx, dstChildIdx});
                } else {
                    // Placeholder for missing child
                    SVONode empty{0, 0, 0, 0};
                    result.push_back(empty);
                }
            }
        }
        
        currentLevel = std::move(nextLevel);
    }
    
    return result;
}

} // namespace voxels
} // namespace chonk

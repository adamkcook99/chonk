#include "tree27.h"
#include "../core/logging.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace chonk {
namespace voxels {

Tree27::Tree27() = default;
Tree27::~Tree27() = default;

// ============================================================================
// Static helper: Get child index from local position
// ============================================================================
uint32_t Tree27::getChildIndex(float localX, float localY, float localZ,
                                float nodeSize,
                                float& childLocalX, float& childLocalY, float& childLocalZ) {
    float third = nodeSize / 3.0f;
    
    // Calculate which third we're in (0, 1, or 2) using floor
    // Clamp to ensure we're in [0, 2] range
    float fx = localX / third;
    float fy = localY / third;
    float fz = localZ / third;
    
    // Use floor to get integer index, clamp to [0, 2]
    uint32_t ix = std::min(2u, static_cast<uint32_t>(std::floor(std::max(0.0f, fx))));
    uint32_t iy = std::min(2u, static_cast<uint32_t>(std::floor(std::max(0.0f, fy))));
    uint32_t iz = std::min(2u, static_cast<uint32_t>(std::floor(std::max(0.0f, fz))));
    
    // Calculate position within the child
    childLocalX = localX - static_cast<float>(ix) * third;
    childLocalY = localY - static_cast<float>(iy) * third;
    childLocalZ = localZ - static_cast<float>(iz) * third;
    
    // Child index: x + y*3 + z*9 (0-26)
    return ix + iy * 3 + iz * 9;
}

// ============================================================================
// Query material at position with LOD
// ============================================================================
uint8_t Tree27::getMaterialAtLOD(float localX, float localY, float localZ,
                                  float treeSize, uint32_t maxDepth) const {
    if (nodes.empty()) return MAT_AIR;
    
    // Clamp input to valid range [0, treeSize)
    float lx = std::max(0.0f, std::min(localX, treeSize - 0.001f));
    float ly = std::max(0.0f, std::min(localY, treeSize - 0.001f));
    float lz = std::max(0.0f, std::min(localZ, treeSize - 0.001f));
    
    uint32_t nodeIdx = 0;
    float nodeSize = treeSize;
    
    for (uint32_t depth = 0; depth <= maxDepth; ++depth) {
        if (nodeIdx >= nodes.size()) return MAT_AIR;
        
        const Tree27Node& node = nodes[nodeIdx];
        
        // If leaf or at max depth, return this node's material
        if (node.isLeaf() || depth == maxDepth) {
            return node.majorityMaterial;
        }
        
        // Find which child contains this position
        float childLX, childLY, childLZ;
        uint32_t childIdx = getChildIndex(lx, ly, lz, nodeSize, childLX, childLY, childLZ);
        
        // Clamp child local position to valid range
        float childSize = nodeSize / 3.0f;
        childLX = std::max(0.0f, std::min(childLX, childSize - 0.001f));
        childLY = std::max(0.0f, std::min(childLY, childSize - 0.001f));
        childLZ = std::max(0.0f, std::min(childLZ, childSize - 0.001f));
        
        // Check if child exists
        if (!node.hasChild(childIdx)) {
            return node.majorityMaterial;
        }
        
        // Move to child
        nodeIdx = node.getChildArrayIndex(childIdx);
        nodeSize = childSize;
        lx = childLX;
        ly = childLY;
        lz = childLZ;
    }
    
    return nodes[nodeIdx].majorityMaterial;
}

// ============================================================================
// Procedural Generation (generic terrain function)
// ============================================================================
void Tree27::generateProcedural(
    std::function<uint8_t(const math::Vec3&)> terrainFn,
    const math::Vec3& origin,
    float treeSize,
    uint32_t maxDepth
) {
    nodes.clear();
    if (treeSize <= 0.0f) return;
    
    nodes.reserve(1000);  // Initial estimate
    nodes.push_back({});  // Root placeholder
    
    auto [material, fill] = generateNodeProcedural(terrainFn, origin, treeSize, 0, maxDepth);
    // Root stats are set by the recursive function
}

std::pair<uint8_t, uint8_t> Tree27::generateNodeProcedural(
    std::function<uint8_t(const math::Vec3&)>& terrainFn,
    const math::Vec3& nodeOrigin,
    float nodeSize,
    uint32_t depth,
    uint32_t maxDepth
) {
    uint32_t nodeIdx = static_cast<uint32_t>(nodes.size()) - 1;
    
    // =========================================================================
    // Sample terrain at strategic points (8 corners + center)
    // =========================================================================
    uint8_t samples[9];
    uint8_t sampleCounts[256] = {0};
    
    // 8 corners
    for (int i = 0; i < 8; i++) {
        float cx = (i & 1) ? nodeSize : 0.0f;
        float cy = (i & 2) ? nodeSize : 0.0f;
        float cz = (i & 4) ? nodeSize : 0.0f;
        samples[i] = terrainFn(nodeOrigin + math::Vec3(cx, cy, cz));
        sampleCounts[samples[i]]++;
    }
    // Center
    float half = nodeSize * 0.5f;
    samples[8] = terrainFn(nodeOrigin + math::Vec3(half, half, half));
    sampleCounts[samples[8]]++;
    
    // =========================================================================
    // Check uniformity
    // =========================================================================
    bool uniform = true;
    uint8_t firstMat = samples[0];
    for (int i = 1; i < 9 && uniform; i++) {
        if (samples[i] != firstMat) uniform = false;
    }
    
    // Find majority material (prefer non-air)
    uint8_t majorityMat = MAT_AIR;
    uint8_t maxCount = 0;
    uint8_t solidCount = 0;
    for (int i = 1; i < 256; i++) {  // Start at 1 to prefer non-air
        if (sampleCounts[i] > maxCount) {
            maxCount = sampleCounts[i];
            majorityMat = static_cast<uint8_t>(i);
        }
        solidCount += sampleCounts[i];
    }
    if (maxCount == 0) majorityMat = MAT_AIR;
    
    // Estimate fill percent from samples
    uint8_t fillPercent = static_cast<uint8_t>((solidCount * 255) / 9);
    
    // =========================================================================
    // If uniform or at max depth → leaf
    // =========================================================================
    if (uniform || depth >= maxDepth) {
        nodes[nodeIdx].majorityMaterial = uniform ? firstMat : majorityMat;
        nodes[nodeIdx].fillPercent = uniform ? (firstMat != MAT_AIR ? 255 : 0) : fillPercent;
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].firstChildIndex = 0;
        
        nodes[nodeIdx].setFlag(FLAG_IS_LEAF, true);
        if (nodes[nodeIdx].fillPercent == 0) {
            nodes[nodeIdx].setFlag(FLAG_IS_AIR, true);
        }
        if (!uniform && depth < CHUNK_TREE_MAX_DEPTH) {
            nodes[nodeIdx].setFlag(FLAG_HAS_MORE_DETAIL, true);
        }
        
        return {nodes[nodeIdx].majorityMaterial, nodes[nodeIdx].fillPercent};
    }
    
    // =========================================================================
    // Subdivide - create children
    // =========================================================================
    float childSize = nodeSize / 3.0f;
    
    // Store firstChildIndex before any push_back operations
    uint32_t firstChildIndex = static_cast<uint32_t>(nodes.size());
    
    // Set up the node BEFORE pushing any children (to avoid reallocation issues)
    nodes[nodeIdx].firstChildIndex = firstChildIndex;
    nodes[nodeIdx].childMask = 0;
    
    uint32_t totalFill = 0;
    uint8_t childMaterials[256] = {0};
    
    // Track which children we need to create (to avoid accessing nodes[nodeIdx] after push_back)
    uint32_t childMask = 0;
    
    for (uint32_t cz = 0; cz < 3; cz++) {
        for (uint32_t cy = 0; cy < 3; cy++) {
            for (uint32_t cx = 0; cx < 3; cx++) {
                uint32_t childIdx = cx + cy * 3 + cz * 9;
                
                math::Vec3 childOrigin = nodeOrigin + math::Vec3(
                    cx * childSize, cy * childSize, cz * childSize
                );
                
                // Always create child node (sparse storage happens at next level)
                childMask |= (1u << childIdx);
                nodes.push_back({});  // Child placeholder
                
                auto [childMat, childFill] = generateNodeProcedural(
                    terrainFn, childOrigin, childSize, depth + 1, maxDepth
                );
                
                totalFill += childFill;
                childMaterials[childMat]++;
            }
        }
    }
    
    // Now update the node with the child mask (after all push_back operations)
    nodes[nodeIdx].childMask = childMask;
    
    // =========================================================================
    // Propagate stats UP from children
    // =========================================================================
    nodes[nodeIdx].fillPercent = static_cast<uint8_t>(totalFill / 27);
    
    // Majority from children (prefer non-air)
    uint8_t bestMat = MAT_AIR;
    uint8_t bestCount = 0;
    for (int i = 1; i < 256; i++) {
        if (childMaterials[i] > bestCount) {
            bestCount = childMaterials[i];
            bestMat = static_cast<uint8_t>(i);
        }
    }
    nodes[nodeIdx].majorityMaterial = (bestCount > 0) ? bestMat : MAT_AIR;
    
    return {nodes[nodeIdx].majorityMaterial, nodes[nodeIdx].fillPercent};
}

// ============================================================================
// Layered Terrain Generation (analytical fast path)
// ============================================================================
void Tree27::generateLayered(
    const math::Vec3& origin,
    float treeSize,
    uint32_t maxDepth,
    float stoneHeight,
    float dirtHeight,
    float grassHeight
) {
    nodes.clear();
    if (treeSize <= 0.0f) return;
    
    nodes.reserve(1000);
    nodes.push_back({});  // Root placeholder
    
    // Debug: Track initial state
    static uint32_t chunkGenCount = 0;
    chunkGenCount++;
    if (chunkGenCount % 100 == 0) {
        LOG_INFO("Tree27: Generated " + std::to_string(chunkGenCount) + " chunks");
    }
    
    generateNodeLayered(origin, treeSize, 0, maxDepth, stoneHeight, dirtHeight, grassHeight);
    
    // Debug: Log if chunk is uniform (root only)
    if (nodes.size() == 1) {
        static uint32_t uniformChunkCount = 0;
        uniformChunkCount++;
        if (uniformChunkCount % 100 == 0) {
            LOG_INFO("Tree27: " + std::to_string(uniformChunkCount) + " uniform chunks (root only)");
        }
    } else if (nodes.size() > 1 && chunkGenCount % 50 == 0) {
        LOG_DEBUG("Tree27: Non-uniform chunk created with " + std::to_string(nodes.size()) + " nodes");
    }
}

std::pair<uint8_t, uint8_t> Tree27::generateNodeLayered(
    const math::Vec3& nodeOrigin,
    float nodeSize,
    uint32_t depth,
    uint32_t maxDepth,
    float stoneHeight,
    float dirtHeight,
    float grassHeight
) {
    // Store nodeIdx at the start - this is safe because we know the node exists
    // (it was pushed before this function was called)
    uint32_t nodeIdx = static_cast<uint32_t>(nodes.size()) - 1;
    
    float yMin = nodeOrigin.y;
    float yMax = nodeOrigin.y + nodeSize;
    
    // =========================================================================
    // Analytical uniformity check - no sampling needed!
    // =========================================================================
    uint8_t material = MAT_AIR;
    uint8_t fillPercent = 0;
    bool uniform = false;
    
    if (yMax <= stoneHeight) {
        // Entirely below stone line → all stone
        material = MAT_STONE;
        fillPercent = 255;
        uniform = true;
    }
    else if (yMin >= grassHeight) {
        // Entirely above grass line → all air
        material = MAT_AIR;
        fillPercent = 0;
        uniform = true;
    }
    else if (yMin >= dirtHeight && yMax <= grassHeight) {
        // Entirely in grass layer
        material = MAT_GRASS;
        fillPercent = 255;
        uniform = true;
    }
    else if (yMin >= stoneHeight && yMax <= dirtHeight) {
        // Entirely in dirt layer
        material = MAT_DIRT;
        fillPercent = 255;
        uniform = true;
    }
    else {
        // Crosses a boundary → needs subdivision (if allowed)
        // Estimate fill: portion below grass height
        float solidHeight = std::max(0.0f, std::min(yMax, grassHeight) - yMin);
        fillPercent = static_cast<uint8_t>(255.0f * solidHeight / nodeSize);
        
        // Material is whatever layer the center is in
        float centerY = yMin + nodeSize * 0.5f;
        if (centerY < stoneHeight) material = MAT_STONE;
        else if (centerY < dirtHeight) material = MAT_DIRT;
        else if (centerY < grassHeight) material = MAT_GRASS;
        else material = MAT_AIR;
    }
    
    // =========================================================================
    // If uniform or at max depth → leaf
    // =========================================================================
    if (uniform || depth >= maxDepth) {
        // Access node immediately - safe because no vector modifications yet
        nodes[nodeIdx].majorityMaterial = material;
        nodes[nodeIdx].fillPercent = fillPercent;
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].firstChildIndex = 0;
        
        nodes[nodeIdx].setFlag(FLAG_IS_LEAF, true);
        if (fillPercent == 0) {
            nodes[nodeIdx].setFlag(FLAG_IS_AIR, true);
        }
        if (!uniform && depth < CHUNK_TREE_MAX_DEPTH) {
            nodes[nodeIdx].setFlag(FLAG_HAS_MORE_DETAIL, true);
        }
        
        // Debug: Log when we stop early due to uniformity (especially at root)
        if (uniform && depth == 0) {
            static uint32_t rootUniformCount = 0;
            rootUniformCount++;
            if (rootUniformCount % 100 == 0) {
                LOG_DEBUG("Tree27: Root uniform check passed " + std::to_string(rootUniformCount) + 
                         " times (material=" + std::to_string(material) + ")");
            }
        }
        
        return {material, fillPercent};
    }
    
    // =========================================================================
    // Subdivide children - but only create nodes for non-uniform children
    // =========================================================================
    float childSize = nodeSize / 3.0f;
    
    // Store firstChildIndex before any push_back operations
    uint32_t firstChildIndex = static_cast<uint32_t>(nodes.size());
    
    // Set up the node BEFORE pushing any children (to avoid reallocation issues)
    nodes[nodeIdx].firstChildIndex = firstChildIndex;
    nodes[nodeIdx].childMask = 0;
    
    uint32_t totalFill = 0;
    uint8_t childMaterials[256] = {0};
    uint32_t uniformChildCount = 0;
    uint32_t nonUniformChildCount = 0;
    
    // Debug: Log when we subdivide (especially at root)
    if (depth == 0) {
        static uint32_t rootSubdivideCount = 0;
        rootSubdivideCount++;
        if (rootSubdivideCount % 50 == 0) {
            LOG_DEBUG("Tree27: Subdividing root node (chunk not uniform) - count: " + 
                     std::to_string(rootSubdivideCount));
        }
    }
    
    // Track which children we need to create (to avoid accessing nodes[nodeIdx] after push_back)
    uint32_t childMask = 0;
    
    for (uint32_t cz = 0; cz < 3; cz++) {
        for (uint32_t cy = 0; cy < 3; cy++) {
            for (uint32_t cx = 0; cx < 3; cx++) {
                uint32_t childIdx = cx + cy * 3 + cz * 9;
                
                math::Vec3 childOrigin = nodeOrigin + math::Vec3(
                    cx * childSize, cy * childSize, cz * childSize
                );
                
                float childYMin = childOrigin.y;
                float childYMax = childOrigin.y + childSize;
                
                // Check if this child is uniform (fast analytical check)
                bool childUniform = false;
                uint8_t childMat = MAT_AIR;
                uint8_t childFill = 0;
                
                if (childYMax <= stoneHeight) {
                    childMat = MAT_STONE;
                    childFill = 255;
                    childUniform = true;
                }
                else if (childYMin >= grassHeight) {
                    childMat = MAT_AIR;
                    childFill = 0;
                    childUniform = true;
                }
                else if (childYMin >= dirtHeight && childYMax <= grassHeight) {
                    childMat = MAT_GRASS;
                    childFill = 255;
                    childUniform = true;
                }
                else if (childYMin >= stoneHeight && childYMax <= dirtHeight) {
                    childMat = MAT_DIRT;
                    childFill = 255;
                    childUniform = true;
                }
                
                if (childUniform) {
                    // Uniform child - don't create a node, just count it
                    totalFill += childFill;
                    childMaterials[childMat]++;
                    uniformChildCount++;
                    // Don't set child bit - sparse storage!
                } else {
                    // Non-uniform child - needs subdivision
                    childMask |= (1u << childIdx);
                    nodes.push_back({});
                    nonUniformChildCount++;
                    
                    auto [recursiveMat, recursiveFill] = generateNodeLayered(
                        childOrigin, childSize, depth + 1, maxDepth,
                        stoneHeight, dirtHeight, grassHeight
                    );
                    
                    totalFill += recursiveFill;
                    childMaterials[recursiveMat]++;
                }
            }
        }
    }
    
    // Now update the node with the child mask (after all push_back operations)
    // nodeIdx should still be valid even if vector reallocated (indices are stable)
    nodes[nodeIdx].childMask = childMask;
    
    // Debug: Log child uniformity stats at root level
    if (depth == 0 && nonUniformChildCount > 0) {
        static uint32_t rootNonUniformStats = 0;
        rootNonUniformStats++;
        if (rootNonUniformStats % 20 == 0) {
            LOG_DEBUG("Tree27: Root subdivision - " + std::to_string(uniformChildCount) + 
                     " uniform children, " + std::to_string(nonUniformChildCount) + 
                     " non-uniform children");
        }
    }
    
    // If all children were uniform, this node should also be uniform
    // (This shouldn't happen if we got here, but handle it anyway)
    if (uniformChildCount == 27) {
        // All children uniform - collapse this node
        uint8_t bestMat = MAT_AIR;
        uint8_t bestCount = 0;
        for (int i = 1; i < 256; i++) {
            if (childMaterials[i] > bestCount) {
                bestCount = childMaterials[i];
                bestMat = static_cast<uint8_t>(i);
            }
        }
        nodes[nodeIdx].majorityMaterial = (bestCount > 0) ? bestMat : MAT_AIR;
        nodes[nodeIdx].fillPercent = static_cast<uint8_t>(totalFill / 27);
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].firstChildIndex = 0;
        nodes[nodeIdx].setFlag(FLAG_IS_LEAF, true);
        if (nodes[nodeIdx].fillPercent == 0) {
            nodes[nodeIdx].setFlag(FLAG_IS_AIR, true);
        }
        return {nodes[nodeIdx].majorityMaterial, nodes[nodeIdx].fillPercent};
    }
    
    // Propagate stats from children
    nodes[nodeIdx].fillPercent = static_cast<uint8_t>(totalFill / 27);
    
    uint8_t bestMat = MAT_AIR;
    uint8_t bestCount = 0;
    for (int i = 1; i < 256; i++) {
        if (childMaterials[i] > bestCount) {
            bestCount = childMaterials[i];
            bestMat = static_cast<uint8_t>(i);
        }
    }
    nodes[nodeIdx].majorityMaterial = (bestCount > 0) ? bestMat : MAT_AIR;
    
    return {nodes[nodeIdx].majorityMaterial, nodes[nodeIdx].fillPercent};
}

// ============================================================================
// Dense voxel array build (for testing/importing)
// ============================================================================
void Tree27::buildFromDense(const uint8_t* voxels, uint32_t sizePerAxis, uint32_t maxDepth) {
    nodes.clear();
    if (sizePerAxis == 0 || voxels == nullptr) return;
    
    nodes.reserve(1000);
    nodes.push_back({});  // Root
    
    buildNodeDense(0, voxels, sizePerAxis, 0, 0, 0, sizePerAxis, 0, maxDepth);
}

void Tree27::buildNodeDense(uint32_t nodeIdx, const uint8_t* voxels, uint32_t sizePerAxis,
                             uint32_t x, uint32_t y, uint32_t z, uint32_t nodeSize,
                             uint32_t depth, uint32_t maxDepth) {
    // Calculate stats for this region
    uint8_t counts[256] = {0};
    uint32_t total = 0;
    uint32_t solidCount = 0;
    
    for (uint32_t dz = 0; dz < nodeSize && z + dz < sizePerAxis; ++dz) {
        for (uint32_t dy = 0; dy < nodeSize && y + dy < sizePerAxis; ++dy) {
            for (uint32_t dx = 0; dx < nodeSize && x + dx < sizePerAxis; ++dx) {
                uint32_t idx = (z + dz) * sizePerAxis * sizePerAxis + 
                              (y + dy) * sizePerAxis + (x + dx);
                uint8_t mat = voxels[idx];
                counts[mat]++;
                total++;
                if (mat != MAT_AIR) solidCount++;
            }
        }
    }
    
    // Find majority
    uint8_t majorityMat = MAT_AIR;
    uint32_t maxCount = 0;
    for (int i = 1; i < 256; i++) {
        if (counts[i] > maxCount) {
            maxCount = counts[i];
            majorityMat = static_cast<uint8_t>(i);
        }
    }
    
    uint8_t fillPercent = total > 0 ? static_cast<uint8_t>((solidCount * 255) / total) : 0;
    
    // Check uniformity
    bool uniform = (solidCount == total && maxCount == total) || solidCount == 0;
    
    nodes[nodeIdx].majorityMaterial = majorityMat;
    nodes[nodeIdx].fillPercent = fillPercent;
    
    if (uniform || depth >= maxDepth || nodeSize <= 3) {
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].firstChildIndex = 0;
        nodes[nodeIdx].setFlag(FLAG_IS_LEAF, true);
        if (fillPercent == 0) nodes[nodeIdx].setFlag(FLAG_IS_AIR, true);
        return;
    }
    
    // Subdivide
    uint32_t childSize = nodeSize / 3;
    nodes[nodeIdx].firstChildIndex = static_cast<uint32_t>(nodes.size());
    nodes[nodeIdx].childMask = 0;
    
    for (uint32_t cz = 0; cz < 3; cz++) {
        for (uint32_t cy = 0; cy < 3; cy++) {
            for (uint32_t cx = 0; cx < 3; cx++) {
                uint32_t childIdx = cx + cy * 3 + cz * 9;
                nodes[nodeIdx].childMask |= (1u << childIdx);
                
                uint32_t newNodeIdx = static_cast<uint32_t>(nodes.size());
                nodes.push_back({});
                
                buildNodeDense(newNodeIdx, voxels, sizePerAxis,
                              x + cx * childSize, y + cy * childSize, z + cz * childSize,
                              childSize, depth + 1, maxDepth);
            }
        }
    }
}

// ============================================================================
// Trimming for LOD transmission
// ============================================================================
std::vector<Tree27Node> Tree27::getNodesTrimmedToDepth(uint32_t maxDepth) const {
    if (nodes.empty()) return {};
    
    std::vector<Tree27Node> result;
    result.reserve(nodes.size());  // Upper bound
    
    trimNodeToDepth(0, 0, maxDepth, result);
    
    return result;
}

uint32_t Tree27::trimNodeToDepth(uint32_t srcIdx, uint32_t depth, uint32_t maxDepth,
                                  std::vector<Tree27Node>& outNodes) const {
    if (srcIdx >= nodes.size()) return 0;
    
    uint32_t destIdx = static_cast<uint32_t>(outNodes.size());
    outNodes.push_back(nodes[srcIdx]);
    
    const Tree27Node& srcNode = nodes[srcIdx];
    Tree27Node& destNode = outNodes[destIdx];
    
    // If at max depth or already leaf, make it a leaf
    if (depth >= maxDepth || srcNode.isLeaf()) {
        destNode.childMask &= ~CHILD_MASK_BITS;  // Clear children, keep flags
        destNode.setFlag(FLAG_IS_LEAF, true);
        if (depth < CHUNK_TREE_MAX_DEPTH && !srcNode.isLeaf()) {
            destNode.setFlag(FLAG_HAS_MORE_DETAIL, true);
        }
        destNode.firstChildIndex = 0;
        return destIdx;
    }
    
    // Copy children
    destNode.firstChildIndex = static_cast<uint32_t>(outNodes.size());
    
    for (uint32_t childIdx = 0; childIdx < 27; childIdx++) {
        if (srcNode.hasChild(childIdx)) {
            uint32_t srcChildIdx = srcNode.getChildArrayIndex(childIdx);
            trimNodeToDepth(srcChildIdx, depth + 1, maxDepth, outNodes);
        }
    }
    
    return destIdx;
}

// ============================================================================
// Serialization
// ============================================================================
std::vector<uint8_t> Tree27::serialize() const {
    std::vector<uint8_t> data(nodes.size() * sizeof(Tree27Node));
    std::memcpy(data.data(), nodes.data(), data.size());
    return data;
}

void Tree27::deserialize(const uint8_t* data, size_t nodeCount) {
    nodes.resize(nodeCount);
    std::memcpy(nodes.data(), data, nodeCount * sizeof(Tree27Node));
}

} // namespace voxels
} // namespace chonk


#include "svo.h"
#include "../math/vec3.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <cmath>

namespace chonk {
namespace voxels {

SVO::SVO() : rootIndex(0) {
}

SVO::~SVO() {
}

void SVO::clear() {
    nodes.clear();
    rootIndex = 0;
}

bool SVO::isCollapsed() const {
    if (nodes.empty() || rootIndex >= nodes.size()) {
        return true; // Empty SVO is considered collapsed (all air)
    }
    const SVONode& root = nodes[rootIndex];
    return root.childMask == 0; // No children means collapsed to single node
}

uint8_t SVO::getRootMaterial() const {
    if (nodes.empty() || rootIndex >= nodes.size()) {
        return 0; // Air
    }
    const SVONode& root = nodes[rootIndex];
    return root.material;
}

void SVO::loadNodes(const std::vector<SVONode>& loadedNodes) {
    nodes = loadedNodes;
    if (!nodes.empty()) {
        rootIndex = 0;
    } else {
        rootIndex = 0;
    }
}

void SVO::buildFromDense(const uint8_t* voxels, uint32_t size) {
    nodes.clear();
    if (size != CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE) {
        // Invalid size - create empty SVO with single air node
        SVONode airNode;
        airNode.childBase = 0;
        airNode.childMask = 0;
        airNode.material = 0;
        airNode.padding = 0;
        nodes.push_back(airNode);
        rootIndex = 0;
        return;
    }
    
    // Prepare root
    nodes.resize(1);
    rootIndex = 0;
    buildNode(rootIndex, voxels, CHUNK_SIZE, CHUNK_SIZE, 0, 0, 0, 0, 0);
}

void SVO::buildNode(uint32_t nodeIdx, const uint8_t* voxels, uint32_t size, uint32_t chunkSize, uint32_t level, 
                    uint32_t x, uint32_t y, uint32_t z, uint32_t offset) {
    if (level >= 6) { // log2(64) = 6 levels
        // Leaf node - return material at this position
        uint32_t idx = offset;
        uint8_t mat = 0;
        if (idx < chunkSize * chunkSize * chunkSize) {
            mat = voxels[idx];
        }
        nodes[nodeIdx].childBase = 0;
        nodes[nodeIdx].childMask = 0; // Leaf
        nodes[nodeIdx].material = mat;
        nodes[nodeIdx].padding = 0;
        return;
    }
    
    // Debug: Verify offset calculation matches coordinates
    uint32_t calculatedOffset = z * chunkSize * chunkSize + y * chunkSize + x;
    if (calculatedOffset != offset && level == 0) {
        // Only log at root level to avoid spam
        // This should never happen if our offset calculation is correct
    }
    
    uint32_t childSize = size / 2;
    
    // Set up current node placeholder
    nodes[nodeIdx].childBase = 0;
    nodes[nodeIdx].childMask = 0;
    nodes[nodeIdx].material = 0;
    nodes[nodeIdx].padding = 0;
    
    // Allocate space for 8 children contiguously
    uint32_t childBase = nodes.size();
    size_t prevSize = childBase;
    nodes.resize(childBase + 8);
    
    uint8_t childMask = 0;
    uint8_t uniformMaterial = 0xFF; // track uniform leaf material; 0xFF = unknown/not uniform, 0xFE = mixed
    bool allChildrenAreLeaves = true;

    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t dx = ((i & 1) ? childSize : 0);
        uint32_t dy = ((i & 2) ? childSize : 0);
        uint32_t dz = ((i & 4) ? childSize : 0);
        
        uint32_t cx = x + dx;
        uint32_t cy = y + dy;
        uint32_t cz = z + dz;
        
        // Calculate child offset: use absolute coordinates (cx, cy, cz) to compute absolute offset
        // The voxel array is indexed as: voxels[z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x]
        uint32_t childOffset = cz * chunkSize * chunkSize + cy * chunkSize + cx;
        
        uint32_t childIdx = childBase + i;
        buildNode(childIdx, voxels, childSize, chunkSize, level + 1, cx, cy, cz, childOffset);
        
        // Check if child is non-empty (has non-air material or has children)
        const SVONode& childNode = nodes[childIdx];
        if (childNode.material != 0 || childNode.childMask != 0) {
            childMask |= (1 << i);

            // Track uniformity across leaf children
            if (childNode.childMask == 0) {
                if (uniformMaterial == 0xFF) {
                    uniformMaterial = childNode.material;
                } else if (uniformMaterial != childNode.material) {
                    uniformMaterial = 0xFE; // mark as non-uniform
                }
            } else {
                allChildrenAreLeaves = false;
                uniformMaterial = 0xFE; // not uniform
            }
        }
    }

    // If no child carries material or children, entire region is air: collapse to single air leaf
    if (childMask == 0) {
        nodes[nodeIdx].childBase = 0;
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].material = 0;
        nodes[nodeIdx].padding = 0;
        // discard allocated children
        nodes.resize(prevSize);
        return;
    }

    // Collapse if all 8 children exist, are leaves, and share the same material
    if (childMask == 0xFF && allChildrenAreLeaves && uniformMaterial != 0xFF && uniformMaterial != 0xFE) {
        nodes[nodeIdx].childBase = 0;
        nodes[nodeIdx].childMask = 0;
        nodes[nodeIdx].material = uniformMaterial;
        nodes[nodeIdx].padding = 0;
        // discard allocated children
        nodes.resize(prevSize);
        return;
    }

    // For mixed children: set dominant material (most frequent leaf material). Ties => 0 (unknown)
    if (childMask != 0) {
        uint32_t counts[256] = {0};
        for (uint32_t i = 0; i < 8; ++i) {
            if (!(childMask & (1u << i))) continue;
            const SVONode& c = nodes[childBase + i];
            if (c.childMask == 0) {
                counts[c.material]++;
            }
        }
        uint8_t dominant = 0;
        uint32_t best = 0;
        bool tie = false;
        for (int m = 0; m < 256; ++m) {
            if (counts[m] > best) {
                best = counts[m];
                dominant = static_cast<uint8_t>(m);
                tie = false;
            } else if (counts[m] == best && counts[m] != 0) {
                tie = true;
            }
        }
        nodes[nodeIdx].material = tie ? 0 : dominant;
    }

    // Update node with child info
    nodes[nodeIdx].childBase = childBase;
    nodes[nodeIdx].childMask = childMask;
}

uint8_t SVO::getMaterial(float x, float y, float z, float /*resolution*/) const {
    if (nodes.empty()) return 0;
    
    // Inputs are chunk-local voxel indices (0..63)
    const float sizeVoxels = static_cast<float>(CHUNK_SIZE); // 64
    return getMaterialRecursive(rootIndex, x, y, z, 0.0f, 0, sizeVoxels);
}

uint8_t SVO::getMaterialRecursive(uint32_t nodeIdx, float x, float y, float z, 
                                   float /*resolution*/, uint32_t level, float size) const {
    if (nodeIdx >= nodes.size()) return 0;
    if (x < 0.0f || y < 0.0f || z < 0.0f || x >= size || y >= size || z >= size) return 0;
    
    const SVONode& node = nodes[nodeIdx];
    
    // Leaf node
    if (node.childMask == 0) {
        return node.material;
    }
    
    // Internal node - traverse to child
    float childSize = size * 0.5f;
    uint32_t childIdx = 0;
    
    if (x >= childSize) { childIdx |= 1; x -= childSize; }
    if (y >= childSize) { childIdx |= 2; y -= childSize; }
    if (z >= childSize) { childIdx |= 4; z -= childSize; }
    
    // If child missing, it's air
    if (!(node.childMask & (1 << childIdx))) {
        return 0;
    }
    
    // Children are stored densely in build order; use direct offset by childIdx
    uint32_t actualChildIdx = node.childBase + childIdx;
    return getMaterialRecursive(actualChildIdx, x, y, z, 0.0f, level + 1, childSize);
}

bool SVO::intersectRay(const math::Vec3& origin, const math::Vec3& dir, 
                       float& t, uint8_t& material, float resolution) const {
    if (nodes.empty()) return false;
    
    // Convert ray from meters to voxel space
    const float sizeMeters = 16.0f;
    const float voxelSize = sizeMeters / static_cast<float>(CHUNK_SIZE); // 0.25m
    math::Vec3 oVoxel = origin * (1.0f / voxelSize);
    math::Vec3 dVoxel = dir * (1.0f / voxelSize);
    
    math::Vec3 nodeMin(0, 0, 0);
    float size = static_cast<float>(CHUNK_SIZE); // voxel units
    bool hit = intersectRayRecursive(rootIndex, oVoxel, dVoxel, t, material, 0.0f, 0, size, nodeMin);
    if (hit) {
        t = t * voxelSize; // convert back to meters distance
    }
    return hit;
}

bool SVO::intersectRayRecursive(uint32_t nodeIdx, const math::Vec3& origin, const math::Vec3& dir,
                                float& t, uint8_t& material, float resolution, 
                                uint32_t level, float size, const math::Vec3& nodeMin) const {
    if (nodeIdx >= nodes.size()) return false;
    
    const SVONode& node = nodes[nodeIdx];
    
    // Check AABB intersection
    math::AABB aabb(nodeMin, nodeMin + math::Vec3(size, size, size));
    float tmin, tmax;
    if (!aabb.intersects(origin, dir, tmin, tmax) || tmax < 0) {
        return false;
    }
    
    // Leaf node
    if (node.childMask == 0) {
        if (node.material == 0) return false;
        float entry = (tmin >= 0.0f) ? tmin : tmax; // if inside, use exit
        if (entry <= 0.0f) return false;
        t = entry;
        material = node.material;
        return true;
    }
    
    // Internal node - check children
    float childSize = size * 0.5f;
    float closestT = std::numeric_limits<float>::max();
    uint8_t hitMaterial = 0;
    bool hit = false;
    
    // Traverse children in order
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(node.childMask & (1 << i))) continue;
        
        math::Vec3 childMin = nodeMin;
        if (i & 1) childMin.x += childSize;
        if (i & 2) childMin.y += childSize;
        if (i & 4) childMin.z += childSize;
        
        float childT;
        uint8_t childMat;
        if (intersectRayRecursive(node.childBase + i, origin, dir, childT, childMat, 
                                 resolution, level + 1, childSize, childMin)) {
            if (childT < closestT) {
                closestT = childT;
                hitMaterial = childMat;
                hit = true;
            }
        }
    }
    
    if (hit) {
        t = closestT;
        material = hitMaterial;
    }
    return hit;
}

} // namespace voxels
} // namespace chonk


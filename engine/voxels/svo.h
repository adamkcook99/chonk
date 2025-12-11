#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace chonk {
namespace math {
    struct Vec3;
}

namespace voxels {

// SVO Node structure as specified in documentation
struct SVONode {
    uint32_t childBase;  // index of first child in compact child array
    uint8_t  childMask;  // 8 bits: which children exist (0 == leaf)
    uint8_t  material;   // material if leaf (or dominant material)
    uint16_t padding;    // alignment
};

class SVO {
public:
    SVO();
    ~SVO();
    
    // Build SVO from dense voxel data (64^3 = 262144 voxels)
    void buildFromDense(const uint8_t* voxels, uint32_t size);
    
    // Get material using chunk-local voxel indices (0..63)
    uint8_t getMaterial(float x, float y, float z, float resolution) const;
    
    // Ray intersection - returns true if hit, t is distance, material is hit material
    bool intersectRay(const math::Vec3& origin, const math::Vec3& dir, 
                     float& t, uint8_t& material, float resolution) const;
    
    // Get packed data for GPU
    const std::vector<SVONode>& getNodes() const { return nodes; }
    size_t getNodeCount() const { return nodes.size(); }
    
    // Check if SVO is collapsed to a single node (no children)
    // Returns true if root node has no children (childMask == 0)
    bool isCollapsed() const;
    
    // Get root node material (only valid if isCollapsed() is true)
    uint8_t getRootMaterial() const;
    
    void clear();
    
    // Load nodes from serialized data (for file loading)
    void loadNodes(const std::vector<SVONode>& loadedNodes);
    
private:
    std::vector<SVONode> nodes;
    uint32_t rootIndex;
    
    void buildNode(uint32_t nodeIdx, const uint8_t* voxels, uint32_t size, uint32_t chunkSize, uint32_t level, 
                   uint32_t x, uint32_t y, uint32_t z, uint32_t offset);
    uint8_t getMaterialRecursive(uint32_t nodeIdx, float x, float y, float z, 
                                float resolution, uint32_t level, float size) const;
    uint8_t getMaterialRecursiveVoxel(uint32_t nodeIdx, float x, float y, float z, 
                                     uint32_t level, float size) const;
    bool intersectRayRecursive(uint32_t nodeIdx, const math::Vec3& origin, const math::Vec3& dir,
                              float& t, uint8_t& material, float resolution, 
                              uint32_t level, float size, const math::Vec3& nodeMin) const;
    
    static constexpr uint32_t CHUNK_SIZE = 64;
};

} // namespace voxels
} // namespace chonk


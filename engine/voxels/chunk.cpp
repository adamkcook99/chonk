#include "chunk.h"
#include "world_constants.h"
#include "../../game/world/world_generator.h"
#include "../core/logging.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace chonk {
namespace voxels {

Chunk::Chunk() : loaded(false), hasMoreDetailFlag(false), currentDepth(0) {
    updateAABB();
}

Chunk::Chunk(const ChunkCoord& coord) : coord(coord), loaded(false), hasMoreDetailFlag(false), currentDepth(0) {
    updateAABB();
}

Chunk::~Chunk() {
}

void Chunk::updateAABB() {
    math::Vec3 minPos = coord.toWorldPos();
    aabb = math::AABB(minPos, minPos + math::Vec3(CHUNK_SIZE_METERS, CHUNK_SIZE_METERS, CHUNK_SIZE_METERS));
}

uint8_t Chunk::getMaterialAtWorld(const math::Vec3& worldPos, uint32_t maxDepth) const {
    math::Vec3 local = worldPos - getWorldPosition();
    return getMaterialAtLocal(local.x, local.y, local.z, maxDepth);
}

uint8_t Chunk::getMaterialAtLocal(float x, float y, float z, uint32_t maxDepth) const {
    return svo.getMaterialAtLOD(x, y, z, CHUNK_SIZE_METERS, maxDepth);
}

void Chunk::generate(game::world::WorldGenerator* generator, uint32_t maxDepth) {
    if (!generator) {
        LOG_ERROR("Cannot generate chunk: no world generator");
        return;
    }
    
    // Clamp maxDepth to valid range
    maxDepth = std::min(maxDepth, CHUNK_SVO_DEPTH);
    maxDepth = std::max(maxDepth, 1u);
    
    // Calculate how many cells we need to sample based on depth
    // Depth 1: 1 cell (root only, samples center of entire chunk)
    // Depth 2: 2 cells per axis
    // Depth 3: 4 cells per axis
    // Depth N: 2^(N-1) cells per axis
    uint32_t cellsPerAxis = 1u << (maxDepth - 1);
    cellsPerAxis = std::min(cellsPerAxis, 256u); // Cap at 256 for depth 8+
    
    const float CELL_SIZE = CHUNK_SIZE_METERS / static_cast<float>(cellsPerAxis);
    
    std::vector<uint8_t> voxels(cellsPerAxis * cellsPerAxis * cellsPerAxis, 0);
    
    math::Vec3 worldPos = getWorldPosition();
    
    // Generate terrain for each cell
    for (uint32_t z = 0; z < cellsPerAxis; ++z) {
        for (uint32_t y = 0; y < cellsPerAxis; ++y) {
            for (uint32_t x = 0; x < cellsPerAxis; ++x) {
                // Sample at center of cell
                math::Vec3 cellWorldPos = worldPos + math::Vec3(
                    (x + 0.5f) * CELL_SIZE,
                    (y + 0.5f) * CELL_SIZE,
                    (z + 0.5f) * CELL_SIZE
                );
                
                uint8_t material = generator->getMaterialAt(cellWorldPos, CELL_SIZE);
                voxels[z * cellsPerAxis * cellsPerAxis + y * cellsPerAxis + x] = material;
            }
        }
    }
    
    // Build SVO from dense data with the specified max depth
    svo.buildFromDense(voxels.data(), cellsPerAxis, maxDepth);
    
    // Track current depth and whether more detail is available
    currentDepth = maxDepth;
    hasMoreDetailFlag = (maxDepth < CHUNK_SVO_DEPTH);
    
    loaded = true;
}

void Chunk::refineDetail(game::world::WorldGenerator* generator, uint32_t newMaxDepth) {
    if (!generator) {
        return;
    }
    
    // Only refine if we need more detail
    if (newMaxDepth <= currentDepth || !hasMoreDetailFlag) {
        return;
    }
    
    // Re-generate with higher detail
    generate(generator, newMaxDepth);
}

bool Chunk::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read header (chunk coord)
    ChunkCoord fileCoord;
    file.read(reinterpret_cast<char*>(&fileCoord), sizeof(ChunkCoord));
    
    if (fileCoord != coord) {
        LOG_WARNING("Chunk coordinate mismatch in file");
        return false;
    }
    
    // Read version marker to detect old vs new format
    uint32_t versionOrNodeCount;
    file.read(reinterpret_cast<char*>(&versionOrNodeCount), sizeof(versionOrNodeCount));
    
    uint32_t nodeCount;
    
    // Magic number for new format: 0xCF01 (ChunkFormat version 1)
    if (versionOrNodeCount == 0xCF01) {
        // New format: version | currentDepth | hasMoreFlag | nodeCount | nodes...
        file.read(reinterpret_cast<char*>(&currentDepth), sizeof(currentDepth));
        uint8_t hasMoreFlag;
        file.read(reinterpret_cast<char*>(&hasMoreFlag), sizeof(hasMoreFlag));
        hasMoreDetailFlag = (hasMoreFlag != 0);
        file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    } else {
        // Old format: nodeCount was already read
        nodeCount = versionOrNodeCount;
        currentDepth = CHUNK_SVO_DEPTH;  // Assume full depth for old files
        hasMoreDetailFlag = false;
    }
    
    if (nodeCount == 0) {
        // Empty chunk (all air)
        loaded = true;
        return true;
    }
    
    // Read nodes
    std::vector<SVONode> nodes(nodeCount);
    file.read(reinterpret_cast<char*>(nodes.data()), nodeCount * sizeof(SVONode));
    
    // Read leaf block count
    uint32_t leafCount;
    file.read(reinterpret_cast<char*>(&leafCount), sizeof(leafCount));
    
    std::vector<LeafBlockArray> leafBlocks;
    if (leafCount > 0) {
        leafBlocks.resize(leafCount);
        file.read(reinterpret_cast<char*>(leafBlocks.data()), leafCount * sizeof(LeafBlockArray));
    }
    
    if (file.fail()) {
        LOG_ERROR("Failed to read chunk from file");
        return false;
    }
    
    svo.loadNodes(nodes, leafBlocks);
    loaded = true;
    
    return true;
}

bool Chunk::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    file.write(reinterpret_cast<const char*>(&coord), sizeof(ChunkCoord));
    
    // Write version marker (0xCF01 = ChunkFormat version 1)
    uint32_t version = 0xCF01;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write depth info
    file.write(reinterpret_cast<const char*>(&currentDepth), sizeof(currentDepth));
    uint8_t hasMoreFlag = hasMoreDetailFlag ? 1 : 0;
    file.write(reinterpret_cast<const char*>(&hasMoreFlag), sizeof(hasMoreFlag));
    
    // Write node count
    uint32_t nodeCount = static_cast<uint32_t>(svo.getNodeCount());
    file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    
    // Write nodes
    const auto& nodes = svo.getNodes();
    file.write(reinterpret_cast<const char*>(nodes.data()), nodeCount * sizeof(SVONode));
    
    // Write leaf blocks
    uint32_t leafCount = static_cast<uint32_t>(svo.getLeafBlockCount());
    file.write(reinterpret_cast<const char*>(&leafCount), sizeof(leafCount));
    
    if (leafCount > 0) {
        const auto& leafBlocks = svo.getLeafBlocks();
        file.write(reinterpret_cast<const char*>(leafBlocks.data()), leafCount * sizeof(LeafBlockArray));
    }
    
    return true;
}

} // namespace voxels
} // namespace chonk

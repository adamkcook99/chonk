#include "chunk.h"
#include "../../game/world/world_generator.h"
#include <cstring>
#include <fstream>

namespace chonk {
namespace voxels {

Chunk::Chunk() : coord(0, 0, 0), loaded(false), currentDepth(0) {}

Chunk::Chunk(const ChunkCoord &coord)
    : coord(coord), loaded(false), currentDepth(0) {}

Chunk::~Chunk() = default;

// ============================================================================
// Generation
// ============================================================================

void Chunk::generate(game::world::WorldGenerator *generator,
                     uint32_t maxDepth) {
  if (!generator)
    return;

  generator->generateChunk(*this, maxDepth);
  currentDepth = maxDepth;
  loaded = true;
}

void Chunk::generateWithFunction(
    std::function<uint8_t(const math::Vec3 &)> terrainFn, uint32_t maxDepth) {
  math::Vec3 origin = getWorldPosition();
  tree.generateProcedural(terrainFn, origin, CHUNK_SIZE_METERS, maxDepth);
  currentDepth = maxDepth;
  loaded = true;
}

void Chunk::generateLayered(uint32_t maxDepth, float stoneHeight,
                            float dirtHeight, float grassHeight) {
  math::Vec3 origin = getWorldPosition();
  tree.generateLayered(origin, CHUNK_SIZE_METERS, maxDepth, stoneHeight,
                       dirtHeight, grassHeight);
  currentDepth = maxDepth;
  loaded = true;
}

void Chunk::refineDetail(game::world::WorldGenerator *generator,
                         uint32_t newMaxDepth) {
  if (!generator || newMaxDepth <= currentDepth)
    return;

  // Regenerate at higher detail
  generator->generateChunk(*this, newMaxDepth);
  currentDepth = newMaxDepth;
}

// ============================================================================
// Material Queries
// ============================================================================

uint8_t Chunk::getMaterialAtWorld(const math::Vec3 &worldPos,
                                  uint32_t maxDepth) const {
  math::Vec3 origin = getWorldPosition();
  float localX = worldPos.x - origin.x;
  float localY = worldPos.y - origin.y;
  float localZ = worldPos.z - origin.z;

  return getMaterialAtLocal(localX, localY, localZ, maxDepth);
}

uint8_t Chunk::getMaterialAtLocal(float x, float y, float z,
                                  uint32_t maxDepth) const {
  // Clamp to chunk bounds
  x = std::max(0.0f, std::min(x, CHUNK_SIZE_METERS - 0.001f));
  y = std::max(0.0f, std::min(y, CHUNK_SIZE_METERS - 0.001f));
  z = std::max(0.0f, std::min(z, CHUNK_SIZE_METERS - 0.001f));

  // Use the actual depth if less than requested
  uint32_t queryDepth = std::min(maxDepth, currentDepth);

  return tree.getMaterialAtLOD(x, y, z, CHUNK_SIZE_METERS, queryDepth);
}

// ============================================================================
// File I/O
// ============================================================================

bool Chunk::loadFromFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file)
    return false;

  // Header: coord (3 int32), depth (uint32), node count (uint32)
  int32_t fx, fy, fz;
  uint32_t depth, nodeCount;

  file.read(reinterpret_cast<char *>(&fx), sizeof(fx));
  file.read(reinterpret_cast<char *>(&fy), sizeof(fy));
  file.read(reinterpret_cast<char *>(&fz), sizeof(fz));
  file.read(reinterpret_cast<char *>(&depth), sizeof(depth));
  file.read(reinterpret_cast<char *>(&nodeCount), sizeof(nodeCount));

  if (!file)
    return false;

  // Verify coord matches
  if (fx != coord.x || fy != coord.y || fz != coord.z) {
    return false;
  }

  // Read nodes
  if (nodeCount > 0) {
    std::vector<uint8_t> nodeData(nodeCount * sizeof(Tree27Node));
    file.read(reinterpret_cast<char *>(nodeData.data()), nodeData.size());
    if (!file)
      return false;

    tree.deserialize(nodeData.data(), nodeCount);
  } else {
    tree.clear();
  }

  currentDepth = depth;
  loaded = true;
  return true;
}

bool Chunk::saveToFile(const std::string &filename) const {
  std::ofstream file(filename, std::ios::binary);
  if (!file)
    return false;

  // Header
  file.write(reinterpret_cast<const char *>(&coord.x), sizeof(coord.x));
  file.write(reinterpret_cast<const char *>(&coord.y), sizeof(coord.y));
  file.write(reinterpret_cast<const char *>(&coord.z), sizeof(coord.z));
  file.write(reinterpret_cast<const char *>(&currentDepth),
             sizeof(currentDepth));

  uint32_t nodeCount = static_cast<uint32_t>(tree.getNodeCount());
  file.write(reinterpret_cast<const char *>(&nodeCount), sizeof(nodeCount));

  // Nodes
  if (nodeCount > 0) {
    auto nodeData = tree.serialize();
    file.write(reinterpret_cast<const char *>(nodeData.data()),
               nodeData.size());
  }

  return file.good();
}

// ============================================================================
// Network Serialization
// ============================================================================

std::vector<uint8_t> Chunk::serialize() const {
  // Format: depth (4 bytes) + node count (4 bytes) + nodes
  auto nodeData = tree.serialize();

  std::vector<uint8_t> result(8 + nodeData.size());

  uint32_t depth = currentDepth;
  uint32_t nodeCount = static_cast<uint32_t>(tree.getNodeCount());

  std::memcpy(result.data(), &depth, 4);
  std::memcpy(result.data() + 4, &nodeCount, 4);

  if (!nodeData.empty()) {
    std::memcpy(result.data() + 8, nodeData.data(), nodeData.size());
  }

  return result;
}

void Chunk::deserialize(const uint8_t *data, size_t size, uint32_t depth) {
  if (size < 8) {
    tree.clear();
    currentDepth = 0;
    loaded = true;
    return;
  }

  uint32_t fileDepth, nodeCount;
  std::memcpy(&fileDepth, data, 4);
  std::memcpy(&nodeCount, data + 4, 4);

  size_t expectedSize = 8 + nodeCount * sizeof(Tree27Node);
  if (size < expectedSize) {
    tree.clear();
    currentDepth = 0;
    loaded = true;
    return;
  }

  if (nodeCount > 0) {
    tree.deserialize(data + 8, nodeCount);
  } else {
    tree.clear();
  }

  currentDepth = depth > 0 ? depth : fileDepth;
  loaded = true;
}

} // namespace voxels
} // namespace chonk

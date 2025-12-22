#include "chunk_array.h"
#include <cmath>
#include <algorithm>

namespace chonk {
namespace voxels {

ChunkArray::ChunkArray() = default;
ChunkArray::~ChunkArray() = default;

// ============================================================================
// Recentering
// ============================================================================

std::vector<ChunkCoord> ChunkArray::recenter(const ChunkCoord& newCenter) {
    std::vector<ChunkCoord> needsLoading;
    
    if (!initialized) {
        // First initialization - all chunks need loading
        centerCoord = newCenter;
        initialized = true;
        
        for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
            for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
                for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                    ChunkCoord coord = indexToChunkCoord(x, y, z);
                    needsLoading.push_back(coord);
                }
            }
        }
        return needsLoading;
    }
    
    // Calculate offset from old center to new center
    int32_t dx = newCenter.x - centerCoord.x;
    int32_t dy = newCenter.y - centerCoord.y;
    int32_t dz = newCenter.z - centerCoord.z;
    
    if (dx == 0 && dy == 0 && dz == 0) {
        // No change
        return needsLoading;
    }
    
    // If shift is larger than array size, just reload everything
    if (std::abs(dx) >= static_cast<int32_t>(CHUNK_ARRAY_SIZE) ||
        std::abs(dy) >= static_cast<int32_t>(CHUNK_ARRAY_SIZE) ||
        std::abs(dz) >= static_cast<int32_t>(CHUNK_ARRAY_SIZE)) {
        
        // Clear all chunks
        for (auto& chunk : chunks) {
            chunk.reset();
        }
        
        centerCoord = newCenter;
        
        // All need loading
        for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
            for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
                for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                    ChunkCoord coord = indexToChunkCoord(x, y, z);
                    needsLoading.push_back(coord);
                }
            }
        }
        return needsLoading;
    }
    
    // Shift chunks within the array
    // We need to shift in the opposite direction of movement
    // If player moved +X, we shift chunks -X (new chunks appear on +X edge)
    
    // Create temporary array for shifted chunks
    std::array<std::unique_ptr<Chunk>, CHUNK_ARRAY_TOTAL> newChunks;
    
    for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                // Where this slot's chunk came from in old array
                int32_t oldX = static_cast<int32_t>(x) + dx;
                int32_t oldY = static_cast<int32_t>(y) + dy;
                int32_t oldZ = static_cast<int32_t>(z) + dz;
                
                uint32_t newFlatIdx = toFlatIndex(x, y, z);
                
                if (oldX >= 0 && oldX < static_cast<int32_t>(CHUNK_ARRAY_SIZE) &&
                    oldY >= 0 && oldY < static_cast<int32_t>(CHUNK_ARRAY_SIZE) &&
                    oldZ >= 0 && oldZ < static_cast<int32_t>(CHUNK_ARRAY_SIZE)) {
                    // Chunk can be reused from old position
                    uint32_t oldFlatIdx = toFlatIndex(oldX, oldY, oldZ);
                    newChunks[newFlatIdx] = std::move(chunks[oldFlatIdx]);
                } else {
                    // New chunk needed
                    newChunks[newFlatIdx] = nullptr;
                }
            }
        }
    }
    
    // Move shifted array back
    chunks = std::move(newChunks);
    centerCoord = newCenter;
    
    // Find all slots that need loading
    for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                uint32_t flatIdx = toFlatIndex(x, y, z);
                if (!chunks[flatIdx]) {
                    ChunkCoord coord = indexToChunkCoord(x, y, z);
                    needsLoading.push_back(coord);
                }
            }
        }
    }
    
    return needsLoading;
}

// ============================================================================
// Chunk Access by Index
// ============================================================================

Chunk* ChunkArray::getByIndex(uint32_t ix, uint32_t iy, uint32_t iz) {
    if (ix >= CHUNK_ARRAY_SIZE || iy >= CHUNK_ARRAY_SIZE || iz >= CHUNK_ARRAY_SIZE) {
        return nullptr;
    }
    return chunks[toFlatIndex(ix, iy, iz)].get();
}

const Chunk* ChunkArray::getByIndex(uint32_t ix, uint32_t iy, uint32_t iz) const {
    if (ix >= CHUNK_ARRAY_SIZE || iy >= CHUNK_ARRAY_SIZE || iz >= CHUNK_ARRAY_SIZE) {
        return nullptr;
    }
    return chunks[toFlatIndex(ix, iy, iz)].get();
}

void ChunkArray::setChunk(uint32_t ix, uint32_t iy, uint32_t iz, std::unique_ptr<Chunk> chunk) {
    if (ix >= CHUNK_ARRAY_SIZE || iy >= CHUNK_ARRAY_SIZE || iz >= CHUNK_ARRAY_SIZE) {
        return;
    }
    chunks[toFlatIndex(ix, iy, iz)] = std::move(chunk);
}

std::unique_ptr<Chunk> ChunkArray::removeChunk(uint32_t ix, uint32_t iy, uint32_t iz) {
    if (ix >= CHUNK_ARRAY_SIZE || iy >= CHUNK_ARRAY_SIZE || iz >= CHUNK_ARRAY_SIZE) {
        return nullptr;
    }
    return std::move(chunks[toFlatIndex(ix, iy, iz)]);
}

// ============================================================================
// Chunk Access by World Position
// ============================================================================

Chunk* ChunkArray::getByWorldPos(const math::Vec3& worldPos) {
    uint32_t ix, iy, iz;
    if (!worldPosToIndex(worldPos, ix, iy, iz)) {
        return nullptr;
    }
    return getByIndex(ix, iy, iz);
}

const Chunk* ChunkArray::getByWorldPos(const math::Vec3& worldPos) const {
    uint32_t ix, iy, iz;
    if (!worldPosToIndex(worldPos, ix, iy, iz)) {
        return nullptr;
    }
    return getByIndex(ix, iy, iz);
}

Chunk* ChunkArray::getByChunkCoord(const ChunkCoord& coord) {
    uint32_t ix, iy, iz;
    if (!chunkCoordToIndex(coord, ix, iy, iz)) {
        return nullptr;
    }
    return getByIndex(ix, iy, iz);
}

const Chunk* ChunkArray::getByChunkCoord(const ChunkCoord& coord) const {
    uint32_t ix, iy, iz;
    if (!chunkCoordToIndex(coord, ix, iy, iz)) {
        return nullptr;
    }
    return getByIndex(ix, iy, iz);
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

bool ChunkArray::worldPosToIndex(const math::Vec3& worldPos, uint32_t& ix, uint32_t& iy, uint32_t& iz) const {
    ChunkCoord chunkCoord = ChunkCoord::fromWorldPos(worldPos);
    return chunkCoordToIndex(chunkCoord, ix, iy, iz);
}

bool ChunkArray::chunkCoordToIndex(const ChunkCoord& coord, uint32_t& ix, uint32_t& iy, uint32_t& iz) const {
    // Calculate offset from center
    int32_t dx = coord.x - centerCoord.x;
    int32_t dy = coord.y - centerCoord.y;
    int32_t dz = coord.z - centerCoord.z;
    
    // Convert to array index (center is at CHUNK_ARRAY_CENTER)
    int32_t ax = static_cast<int32_t>(CHUNK_ARRAY_CENTER) + dx;
    int32_t ay = static_cast<int32_t>(CHUNK_ARRAY_CENTER) + dy;
    int32_t az = static_cast<int32_t>(CHUNK_ARRAY_CENTER) + dz;
    
    // Check bounds
    if (ax < 0 || ax >= static_cast<int32_t>(CHUNK_ARRAY_SIZE) ||
        ay < 0 || ay >= static_cast<int32_t>(CHUNK_ARRAY_SIZE) ||
        az < 0 || az >= static_cast<int32_t>(CHUNK_ARRAY_SIZE)) {
        return false;
    }
    
    ix = static_cast<uint32_t>(ax);
    iy = static_cast<uint32_t>(ay);
    iz = static_cast<uint32_t>(az);
    return true;
}

ChunkCoord ChunkArray::indexToChunkCoord(uint32_t ix, uint32_t iy, uint32_t iz) const {
    // Convert from array index to offset from center
    int32_t dx = static_cast<int32_t>(ix) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    int32_t dy = static_cast<int32_t>(iy) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    int32_t dz = static_cast<int32_t>(iz) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
    
    return ChunkCoord(centerCoord.x + dx, centerCoord.y + dy, centerCoord.z + dz);
}

bool ChunkArray::isInBounds(const ChunkCoord& coord) const {
    uint32_t ix, iy, iz;
    return chunkCoordToIndex(coord, ix, iy, iz);
}

math::Vec3 ChunkArray::getArrayOrigin() const {
    // Chunk at index (0,0,0) world position
    ChunkCoord originCoord = indexToChunkCoord(0, 0, 0);
    return originCoord.toWorldPos();
}

// ============================================================================
// LOD Information
// ============================================================================

uint32_t ChunkArray::getRequiredDepth(uint32_t ix, uint32_t iy, uint32_t iz) const {
    return getRequiredDepthForIndex(ix, iy, iz);
}

uint32_t ChunkArray::getRequiredDepthForCoord(const ChunkCoord& coord) const {
    uint32_t ix, iy, iz;
    if (!chunkCoordToIndex(coord, ix, iy, iz)) {
        return 0;  // Out of bounds - lowest detail
    }
    return getRequiredDepthForIndex(ix, iy, iz);
}

// ============================================================================
// Loading Management
// ============================================================================

std::vector<ChunkArray::ChunkLoadInfo> ChunkArray::getChunksNeedingLoad() const {
    std::vector<ChunkLoadInfo> result;
    result.reserve(CHUNK_ARRAY_TOTAL);
    
    for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                uint32_t flatIdx = toFlatIndex(x, y, z);
                uint32_t requiredDepth = getRequiredDepthForIndex(x, y, z);
                
                bool needsLoad = false;
                
                if (!chunks[flatIdx]) {
                    needsLoad = true;
                } else if (chunks[flatIdx]->getCurrentDepth() < requiredDepth) {
                    // Needs depth upgrade
                    needsLoad = true;
                }
                
                if (needsLoad) {
                    ChunkLoadInfo info;
                    info.coord = indexToChunkCoord(x, y, z);
                    info.ix = x;
                    info.iy = y;
                    info.iz = z;
                    info.requiredDepth = requiredDepth;
                    
                    // Priority: distance from center (lower = higher priority)
                    int32_t dx = static_cast<int32_t>(x) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    int32_t dy = static_cast<int32_t>(y) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    int32_t dz = static_cast<int32_t>(z) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    info.priority = std::sqrt(static_cast<float>(dx*dx + dy*dy + dz*dz));
                    
                    // Boost priority for higher LOD chunks
                    info.priority -= static_cast<float>(requiredDepth);
                    
                    result.push_back(info);
                }
            }
        }
    }
    
    // Sort by priority (lower first)
    std::sort(result.begin(), result.end(), 
              [](const ChunkLoadInfo& a, const ChunkLoadInfo& b) {
                  return a.priority < b.priority;
              });
    
    return result;
}

std::vector<const Chunk*> ChunkArray::getAllLoadedChunks() const {
    std::vector<const Chunk*> result;
    result.reserve(CHUNK_ARRAY_TOTAL);
    
    for (const auto& chunk : chunks) {
        if (chunk && chunk->isLoaded() && !chunk->isAir()) {
            result.push_back(chunk.get());
        }
    }
    
    return result;
}

// ============================================================================
// Statistics
// ============================================================================

size_t ChunkArray::getLoadedCount() const {
    size_t count = 0;
    for (const auto& chunk : chunks) {
        if (chunk && chunk->isLoaded()) {
            count++;
        }
    }
    return count;
}

} // namespace voxels
} // namespace chonk


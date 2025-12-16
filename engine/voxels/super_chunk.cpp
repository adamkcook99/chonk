#include "super_chunk.h"
#include "world_constants.h"
#include <algorithm>
#include <cmath>

namespace chonk {
namespace voxels {

// ============================================================================
// SuperChunk Implementation
// ============================================================================

SuperChunk::SuperChunk() {
    updateAABB();
}

SuperChunk::SuperChunk(const SuperChunkCoord& coord) : coord(coord) {
    updateAABB();
}

SuperChunk::~SuperChunk() {
}

void SuperChunk::updateAABB() {
    math::Vec3 minPos = coord.toWorldPos();
    aabb = math::AABB(minPos, minPos + math::Vec3(SUPER_CHUNK_SIZE_METERS, SUPER_CHUNK_SIZE_METERS, SUPER_CHUNK_SIZE_METERS));
}

Chunk* SuperChunk::getChunk(uint32_t localX, uint32_t localY, uint32_t localZ) {
    if (localX >= CHUNKS_PER_SUPER_AXIS || localY >= CHUNKS_PER_SUPER_AXIS || localZ >= CHUNKS_PER_SUPER_AXIS) {
        return nullptr;
    }
    return chunks[getIndex(localX, localY, localZ)].get();
}

const Chunk* SuperChunk::getChunk(uint32_t localX, uint32_t localY, uint32_t localZ) const {
    if (localX >= CHUNKS_PER_SUPER_AXIS || localY >= CHUNKS_PER_SUPER_AXIS || localZ >= CHUNKS_PER_SUPER_AXIS) {
        return nullptr;
    }
    return chunks[getIndex(localX, localY, localZ)].get();
}

Chunk* SuperChunk::getChunkAtWorldPos(const math::Vec3& worldPos) {
    math::Vec3 local = worldPos - getWorldPosition();
    
    if (local.x < 0 || local.y < 0 || local.z < 0 ||
        local.x >= SUPER_CHUNK_SIZE_METERS || local.y >= SUPER_CHUNK_SIZE_METERS || local.z >= SUPER_CHUNK_SIZE_METERS) {
        return nullptr;
    }
    
    uint32_t lx = static_cast<uint32_t>(local.x / CHUNK_SIZE_METERS);
    uint32_t ly = static_cast<uint32_t>(local.y / CHUNK_SIZE_METERS);
    uint32_t lz = static_cast<uint32_t>(local.z / CHUNK_SIZE_METERS);
    
    return getChunk(lx, ly, lz);
}

void SuperChunk::setChunk(uint32_t localX, uint32_t localY, uint32_t localZ, std::unique_ptr<Chunk> chunk) {
    if (localX >= CHUNKS_PER_SUPER_AXIS || localY >= CHUNKS_PER_SUPER_AXIS || localZ >= CHUNKS_PER_SUPER_AXIS) {
        return;
    }
    chunks[getIndex(localX, localY, localZ)] = std::move(chunk);
}

std::vector<Chunk*> SuperChunk::getLoadedChunks() {
    std::vector<Chunk*> result;
    result.reserve(CHUNKS_PER_SUPER);
    
    for (auto& chunk : chunks) {
        if (chunk && chunk->isLoaded()) {
            result.push_back(chunk.get());
        }
    }
    
    return result;
}

std::vector<const Chunk*> SuperChunk::getLoadedChunks() const {
    std::vector<const Chunk*> result;
    result.reserve(CHUNKS_PER_SUPER);
    
    for (const auto& chunk : chunks) {
        if (chunk && chunk->isLoaded()) {
            result.push_back(chunk.get());
        }
    }
    
    return result;
}

std::vector<const Chunk*> SuperChunk::getChunksInRange(const math::Vec3& center, float maxDistance) const {
    std::vector<const Chunk*> result;
    
    for (const auto& chunk : chunks) {
        if (!chunk || !chunk->isLoaded()) continue;
        
        // Check distance to chunk center
        math::Vec3 chunkCenter = chunk->getWorldPosition() + 
                                  math::Vec3(CHUNK_SIZE_METERS * 0.5f, CHUNK_SIZE_METERS * 0.5f, CHUNK_SIZE_METERS * 0.5f);
        float dist = (chunkCenter - center).length();
        
        if (dist <= maxDistance) {
            result.push_back(chunk.get());
        }
    }
    
    return result;
}

bool SuperChunk::isFullyLoaded() const {
    for (const auto& chunk : chunks) {
        if (!chunk || !chunk->isLoaded()) {
            return false;
        }
    }
    return true;
}

uint32_t SuperChunk::getLoadedChunkCount() const {
    uint32_t count = 0;
    for (const auto& chunk : chunks) {
        if (chunk && chunk->isLoaded()) {
            ++count;
        }
    }
    return count;
}

void SuperChunk::buildSuperSVO() {
    // Build a coarse SVO from the loaded chunks
    // Each cell in the super SVO corresponds to a 256m chunk
    // 8^3 = 512 cells, 3 levels
    
    std::vector<uint8_t> materials(CHUNKS_PER_SUPER, 0);
    
    for (uint32_t z = 0; z < CHUNKS_PER_SUPER_AXIS; ++z) {
        for (uint32_t y = 0; y < CHUNKS_PER_SUPER_AXIS; ++y) {
            for (uint32_t x = 0; x < CHUNKS_PER_SUPER_AXIS; ++x) {
                uint32_t idx = getIndex(x, y, z);
                const Chunk* chunk = chunks[idx].get();
                
                if (chunk && chunk->isLoaded()) {
                    materials[idx] = chunk->getSVO().getRootMaterial();
                }
            }
        }
    }
    
    superSVO.buildFromDense(materials.data(), CHUNKS_PER_SUPER_AXIS, SUPER_CHUNK_SVO_DEPTH);
}

uint8_t SuperChunk::getMaterialAtWorld(const math::Vec3& worldPos, uint32_t chunkMaxDepth) const {
    const Chunk* chunk = const_cast<SuperChunk*>(this)->getChunkAtWorldPos(worldPos);
    if (!chunk || !chunk->isLoaded()) {
        return 0;
    }
    return chunk->getMaterialAtWorld(worldPos, chunkMaxDepth);
}

// ============================================================================
// SuperChunkCache Implementation
// ============================================================================

SuperChunkCache::SuperChunkCache(size_t maxSize) : maxCacheSize(maxSize) {
    superList.reserve(maxSize);
}

SuperChunkCache::~SuperChunkCache() {
}

SuperChunk* SuperChunkCache::get(const SuperChunkCoord& coord) {
    auto it = indexMap.find(coord);
    if (it == indexMap.end()) {
        return nullptr;
    }
    return superList[it->second].ptr.get();
}

const SuperChunk* SuperChunkCache::get(const SuperChunkCoord& coord) const {
    auto it = indexMap.find(coord);
    if (it == indexMap.end()) {
        return nullptr;
    }
    return superList[it->second].ptr.get();
}

void SuperChunkCache::add(std::unique_ptr<SuperChunk> superChunk) {
    if (!superChunk) return;
    
    const SuperChunkCoord& coord = superChunk->getCoord();
    
    // Check if already exists
    if (indexMap.find(coord) != indexMap.end()) {
        return;
    }
    
    // Evict if at capacity
    evictIfNeeded();
    
    // Add at end (new entries start cold)
    size_t idx = superList.size();
    superList.push_back({coord, std::move(superChunk)});
    indexMap[coord] = idx;
}

void SuperChunkCache::touch(const SuperChunkCoord& coord, uint32_t frame) {
    auto it = indexMap.find(coord);
    if (it == indexMap.end()) return;
    
    size_t idx = it->second;
    superList[idx].ptr->setLastAccessFrame(frame);
    superList[idx].ptr->recordHit();
    
    // Move toward front based on popularity
    moveTowardFront(idx);
}

void SuperChunkCache::moveTowardFront(size_t idx) {
    if (idx == 0) return;
    
    Entry& current = superList[idx];
    Entry& prev = superList[idx - 1];
    
    // Move up if more popular (more hits or more recent access)
    uint32_t currentScore = current.ptr->getHits();
    uint32_t prevScore = prev.ptr->getHits();
    
    if (currentScore > prevScore) {
        // Swap
        std::swap(superList[idx], superList[idx - 1]);
        indexMap[superList[idx].coord] = idx;
        indexMap[superList[idx - 1].coord] = idx - 1;
        
        // Continue moving up
        moveTowardFront(idx - 1);
    }
}

void SuperChunkCache::evictIfNeeded() {
    while (superList.size() >= maxCacheSize && !superList.empty()) {
        // Remove from back (least popular)
        const SuperChunkCoord& coord = superList.back().coord;
        indexMap.erase(coord);
        superList.pop_back();
    }
}

std::vector<SuperChunk*> SuperChunkCache::getAll() {
    std::vector<SuperChunk*> result;
    result.reserve(superList.size());
    for (auto& entry : superList) {
        result.push_back(entry.ptr.get());
    }
    return result;
}

std::vector<const SuperChunk*> SuperChunkCache::getAll() const {
    std::vector<const SuperChunk*> result;
    result.reserve(superList.size());
    for (const auto& entry : superList) {
        result.push_back(entry.ptr.get());
    }
    return result;
}

std::vector<SuperChunk*> SuperChunkCache::getNear(const math::Vec3& pos, uint32_t count) {
    // Get all and sort by distance
    std::vector<std::pair<float, SuperChunk*>> distPairs;
    distPairs.reserve(superList.size());
    
    for (auto& entry : superList) {
        math::Vec3 center = entry.ptr->getWorldPosition() + 
                            math::Vec3(SUPER_CHUNK_SIZE_METERS * 0.5f, 
                                       SUPER_CHUNK_SIZE_METERS * 0.5f, 
                                       SUPER_CHUNK_SIZE_METERS * 0.5f);
        float dist = (center - pos).length();
        distPairs.push_back({dist, entry.ptr.get()});
    }
    
    std::sort(distPairs.begin(), distPairs.end(), 
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    std::vector<SuperChunk*> result;
    result.reserve(std::min(static_cast<size_t>(count), distPairs.size()));
    
    for (size_t i = 0; i < count && i < distPairs.size(); ++i) {
        result.push_back(distPairs[i].second);
    }
    
    return result;
}

} // namespace voxels
} // namespace chonk


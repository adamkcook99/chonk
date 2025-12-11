#include "chunk_manager.h"
#include "../../game/world/world_generator.h"
#include "../networking/network.h"
#include "../core/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chonk {
namespace voxels {

// Precomputed grid offsets from anchor point (corner of grid position 7)
static const float GRID_OFFSETS[18] = {
    -4064.0f, // pos 0: layer 7 (2048m)
    -2016.0f, // pos 1: layer 6 (1024m)
    -992.0f,  // pos 2: layer 5 (512m)
    -480.0f,  // pos 3: layer 4 (256m)
    -224.0f,  // pos 4: layer 3 (128m)
    -96.0f,   // pos 5: layer 2 (64m)
    -32.0f,   // pos 6: layer 1 (32m)
    0.0f,     // pos 7: layer 0 (16m) - anchor
    16.0f,    // pos 8: layer 0 (16m)
    32.0f,    // pos 9: layer 0 (16m)
    48.0f,    // pos 10: layer 0 (16m)
    64.0f,    // pos 11: layer 1 (32m)
    96.0f,    // pos 12: layer 2 (64m)
    160.0f,   // pos 13: layer 3 (128m)
    288.0f,   // pos 14: layer 4 (256m)
    544.0f,   // pos 15: layer 5 (512m)
    1056.0f,  // pos 16: layer 6 (1024m)
    2080.0f   // pos 17: layer 7 (2048m)
};

// Layer for each grid position
static const int GRID_LAYERS[18] = {7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7};

ChunkManager::ChunkManager() 
    : worldGenerator(nullptr)
    , running(false)
    , chunkDirectory("chunks")
    , anchorPos(0, 0, 0)
    , playerChunk(0, 0, 0, ChunkResolution::RES_0_25M)
    , anchorInitialized(false) {
}

ChunkManager::~ChunkManager() {
    shutdown();
}

void ChunkManager::initialize(game::world::WorldGenerator* generator) {
    worldGenerator = generator;
    running = true;
    loadThread = std::thread(&ChunkManager::loadThreadFunc, this);
}

void ChunkManager::shutdown() {
    if (running) {
        running = false;
        loadCondition.notify_all();
        if (loadThread.joinable()) {
            loadThread.join();
        }
    }
    
    // Save all chunks
    std::lock_guard<std::mutex> lock(chunksMutex);
    for (auto& pair : chunks) {
        if (pair.second && pair.second->isLoaded()) {
            std::string filename = getChunkFilename(pair.first);
            pair.second->saveToFile(filename);
        }
    }
    chunks.clear();
}

void ChunkManager::update(const math::Vec3& playerPos) {
    // Update anchor position if player moved outside 2x2x2 center
    updateAnchorIfNeeded(playerPos);
    
    // Get all required chunks for current position
    std::vector<ChunkCoord> required = getRequiredChunks(playerPos);
    
    // Queue chunks that need loading
    {
        std::lock_guard<std::mutex> lock(loadQueueMutex);
        for (const auto& coord : required) {
            // Skip if already loaded or pending
            if (pendingLoads.find(coord) != pendingLoads.end()) {
                continue;
            }
            
            {
                std::lock_guard<std::mutex> chunkLock(chunksMutex);
                auto it = chunks.find(coord);
                if (it != chunks.end() && it->second->isLoaded()) {
                    continue;
                }
            }
            
            // Queue with priority based on distance
            float priority = calculateLoadPriority(coord, playerPos);
            queueChunkLoad(coord, priority);
                }
            }
            
    // Notify load thread
    loadCondition.notify_one();
    
    // Unload chunks no longer needed
    unloadDistantChunks(playerPos);
}

void ChunkManager::updateAnchorIfNeeded(const math::Vec3& playerPos) {
    ChunkCoord newPlayerChunk = worldToChunkCoord(playerPos, ChunkResolution::RES_0_25M);
    
    if (!anchorInitialized) {
        // First time: position anchor so player is in center of 4x4x4
        playerChunk = newPlayerChunk;
        anchorPos = math::Vec3(
            (playerChunk.x - 1) * 16.0f,
            (playerChunk.y - 1) * 16.0f,
            (playerChunk.z - 1) * 16.0f
        );
        anchorInitialized = true;
        LOG_INFO("Initialized anchor at (" + std::to_string(anchorPos.x) + ", " +
                 std::to_string(anchorPos.y) + ", " + std::to_string(anchorPos.z) + ")");
        return;
    }
    
    // Check if player moved to a different chunk
    if (newPlayerChunk.x == playerChunk.x && 
        newPlayerChunk.y == playerChunk.y && 
        newPlayerChunk.z == playerChunk.z) {
        return;  // Still in same chunk
    }
    
    // Calculate player's position within the 4x4x4 center (grid positions 7-10)
    // Anchor is at grid position 7, so relative position is:
    int relX = newPlayerChunk.x - static_cast<int32_t>(anchorPos.x / 16.0f);
    int relY = newPlayerChunk.y - static_cast<int32_t>(anchorPos.y / 16.0f);
    int relZ = newPlayerChunk.z - static_cast<int32_t>(anchorPos.z / 16.0f);
    
    // Player should stay in the center 2x2x2 of the 4x4x4 (positions 1,2 relative)
    // If they move outside, shift the anchor
    bool needsShift = false;
    int shiftX = 0, shiftY = 0, shiftZ = 0;
    
    if (relX < 1) { shiftX = relX - 1; needsShift = true; }
    else if (relX > 2) { shiftX = relX - 2; needsShift = true; }
    
    if (relY < 1) { shiftY = relY - 1; needsShift = true; }
    else if (relY > 2) { shiftY = relY - 2; needsShift = true; }
    
    if (relZ < 1) { shiftZ = relZ - 1; needsShift = true; }
    else if (relZ > 2) { shiftZ = relZ - 2; needsShift = true; }
    
    if (needsShift) {
        anchorPos.x += shiftX * 16.0f;
        anchorPos.y += shiftY * 16.0f;
        anchorPos.z += shiftZ * 16.0f;
        LOG_INFO("Shifted anchor by (" + std::to_string(shiftX) + ", " +
                 std::to_string(shiftY) + ", " + std::to_string(shiftZ) + 
                 ") to (" + std::to_string(anchorPos.x) + ", " +
                 std::to_string(anchorPos.y) + ", " + std::to_string(anchorPos.z) + ")");
    }
    
    playerChunk = newPlayerChunk;
}

float ChunkManager::calculateLoadPriority(const ChunkCoord& coord, const math::Vec3& playerPos) const {
    // Priority based on world-space distance from player
    float chunkSize = getChunkSizeForResolution(coord.resolution);
    float chunkCenterX = coord.x * chunkSize + chunkSize * 0.5f;
    float chunkCenterY = coord.y * chunkSize + chunkSize * 0.5f;
    float chunkCenterZ = coord.z * chunkSize + chunkSize * 0.5f;
    
    float dx = chunkCenterX - playerPos.x;
    float dy = chunkCenterY - playerPos.y;
    float dz = chunkCenterZ - playerPos.z;
    
    // Also prioritize higher resolution chunks
    float resolutionBonus = static_cast<float>(coord.resolution) * 100.0f;
    
    return std::sqrt(dx*dx + dy*dy + dz*dz) + resolutionBonus;
}

void ChunkManager::queueChunkLoad(const ChunkCoord& coord, float priority) {
    // Called with loadQueueMutex held
    if (pendingLoads.find(coord) != pendingLoads.end()) {
        return;  // Already in queue
    }
    
    loadQueue.push({coord, priority});
    pendingLoads.insert(coord);
}

Chunk* ChunkManager::getChunk(const ChunkCoord& coord) {
    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find(coord);
    if (it != chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

const Chunk* ChunkManager::getChunk(const ChunkCoord& coord) const {
    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find(coord);
    if (it != chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<const Chunk*> ChunkManager::getRenderChunks() const {
    std::vector<const Chunk*> result;
    result.reserve(512);  // Reasonable initial capacity
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    
    for (const auto& pair : chunks) {
        if (!pair.second || !pair.second->isLoaded()) {
            continue;
        }
        
        // Skip chunks that collapsed to a single air node
        const auto& svo = pair.second->getSVO();
        if (svo.isCollapsed() && svo.getRootMaterial() == 0) {
            continue;  // Entire chunk is air, skip for GPU
        }
        
        result.push_back(pair.second.get());
    }
    
    return result;
}

std::vector<Chunk*> ChunkManager::getSimulationChunks() {
    std::vector<Chunk*> result;
    result.reserve(CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE);
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    
    // Get the 4x4x4 full-resolution chunks at the center
    int32_t anchorChunkX = static_cast<int32_t>(anchorPos.x / 16.0f);
    int32_t anchorChunkY = static_cast<int32_t>(anchorPos.y / 16.0f);
    int32_t anchorChunkZ = static_cast<int32_t>(anchorPos.z / 16.0f);
    
    for (int32_t dx = 0; dx < CENTER_FULL_RES_SIZE; ++dx) {
        for (int32_t dy = 0; dy < CENTER_FULL_RES_SIZE; ++dy) {
            for (int32_t dz = 0; dz < CENTER_FULL_RES_SIZE; ++dz) {
                ChunkCoord coord(
                    anchorChunkX + dx,
                    anchorChunkY + dy,
                    anchorChunkZ + dz,
                    ChunkResolution::RES_0_25M
                );
                
                auto it = chunks.find(coord);
                if (it != chunks.end() && it->second->isLoaded()) {
                    result.push_back(it->second.get());
                }
            }
        }
    }
    
    return result;
}

std::vector<const Chunk*> ChunkManager::getSimulationChunks() const {
    std::vector<const Chunk*> result;
    result.reserve(CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE);
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    
    int32_t anchorChunkX = static_cast<int32_t>(anchorPos.x / 16.0f);
    int32_t anchorChunkY = static_cast<int32_t>(anchorPos.y / 16.0f);
    int32_t anchorChunkZ = static_cast<int32_t>(anchorPos.z / 16.0f);
    
    for (int32_t dx = 0; dx < CENTER_FULL_RES_SIZE; ++dx) {
        for (int32_t dy = 0; dy < CENTER_FULL_RES_SIZE; ++dy) {
            for (int32_t dz = 0; dz < CENTER_FULL_RES_SIZE; ++dz) {
                ChunkCoord coord(
                    anchorChunkX + dx,
                    anchorChunkY + dy,
                    anchorChunkZ + dz,
                    ChunkResolution::RES_0_25M
                );
                
                auto it = chunks.find(coord);
                if (it != chunks.end() && it->second->isLoaded()) {
                    result.push_back(it->second.get());
                }
            }
        }
    }
    
    return result;
}

std::vector<ChunkCoord> ChunkManager::getSimulationChunkCoords(const math::Vec3& playerPos) const {
    std::vector<ChunkCoord> result;
    result.reserve(CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE * CENTER_FULL_RES_SIZE);
    
    // Calculate anchor based on player position
    ChunkCoord pChunk = worldToChunkCoord(playerPos, ChunkResolution::RES_0_25M);
    int32_t anchorX = pChunk.x - 1;
    int32_t anchorY = pChunk.y - 1;
    int32_t anchorZ = pChunk.z - 1;
    
    for (int32_t dx = 0; dx < CENTER_FULL_RES_SIZE; ++dx) {
        for (int32_t dy = 0; dy < CENTER_FULL_RES_SIZE; ++dy) {
            for (int32_t dz = 0; dz < CENTER_FULL_RES_SIZE; ++dz) {
                result.push_back(ChunkCoord(
                    anchorX + dx,
                    anchorY + dy,
                    anchorZ + dz,
                    ChunkResolution::RES_0_25M
                ));
            }
        }
    }
    
    return result;
}

std::vector<ChunkCoord> ChunkManager::getRenderOnlyChunkCoords(const math::Vec3& playerPos) const {
    std::vector<ChunkCoord> result;
    
    // Get all required chunks
    std::vector<ChunkCoord> all = getRequiredChunks(playerPos);
    
    // Get simulation chunks
    std::unordered_set<ChunkCoord, ChunkCoordHash> simChunks;
    for (const auto& coord : getSimulationChunkCoords(playerPos)) {
        simChunks.insert(coord);
    }
    
    // Return only non-simulation chunks
    for (const auto& coord : all) {
        if (simChunks.find(coord) == simChunks.end()) {
            result.push_back(coord);
        }
    }
    
    return result;
}

bool ChunkManager::isSimulationChunk(const ChunkCoord& coord, const math::Vec3& playerPos) const {
    // Only full-resolution chunks can be simulation chunks
    if (coord.resolution != ChunkResolution::RES_0_25M) {
        return false;
    }
    
    // Check if within 4x4x4 center
    ChunkCoord pChunk = worldToChunkCoord(playerPos, ChunkResolution::RES_0_25M);
    int32_t anchorX = pChunk.x - 1;
    int32_t anchorY = pChunk.y - 1;
    int32_t anchorZ = pChunk.z - 1;
    
    return coord.x >= anchorX && coord.x < anchorX + CENTER_FULL_RES_SIZE &&
           coord.y >= anchorY && coord.y < anchorY + CENTER_FULL_RES_SIZE &&
           coord.z >= anchorZ && coord.z < anchorZ + CENTER_FULL_RES_SIZE;
}

void ChunkManager::loadThreadFunc() {
    while (running) {
        ChunkLoadRequest request;
        bool hasWork = false;
        
        {
            std::unique_lock<std::mutex> lock(loadQueueMutex);
            
            // Wait for work or shutdown
            loadCondition.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return !loadQueue.empty() || !running;
            });
            
            if (!running) break;
            
            if (!loadQueue.empty()) {
                request = loadQueue.top();
                loadQueue.pop();
                pendingLoads.erase(request.coord);
                hasWork = true;
            }
        }
        
        if (hasWork) {
            // Check if already loaded
            {
                std::lock_guard<std::mutex> lock(chunksMutex);
                auto it = chunks.find(request.coord);
                if (it != chunks.end() && it->second->isLoaded()) {
                    continue;
                }
            }
            
            // Try to load from file first
            std::string filename = getChunkFilename(request.coord);
            std::unique_ptr<Chunk> chunk = std::make_unique<Chunk>(request.coord);
            
            if (!chunk->loadFromFile(filename)) {
                // Generate new chunk
                chunk->generate(worldGenerator);
                chunk->saveToFile(filename);
            }
            
            chunk->setLoaded(true);
            
            // Add to chunks map
            {
                std::lock_guard<std::mutex> lock(chunksMutex);
                chunks[request.coord] = std::move(chunk);
            }
        }
    }
}

void ChunkManager::unloadDistantChunks(const math::Vec3& playerPos) {
    // Get set of currently required chunks
    std::vector<ChunkCoord> required = getRequiredChunks(playerPos);
    std::unordered_set<ChunkCoord, ChunkCoordHash> requiredSet(required.begin(), required.end());
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    
    auto it = chunks.begin();
    while (it != chunks.end()) {
        const ChunkCoord& coord = it->first;
        
        // Unload if not in required set
        if (requiredSet.find(coord) == requiredSet.end()) {
            // Save before unloading
            if (it->second && it->second->isLoaded()) {
                std::string filename = getChunkFilename(coord);
                it->second->saveToFile(filename);
            }
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
}

float ChunkManager::getChunkSizeForResolution(ChunkResolution res) {
    switch (res) {
        case ChunkResolution::RES_0_25M: return 16.0f;
        case ChunkResolution::RES_0_5M:  return 32.0f;
        case ChunkResolution::RES_1M:    return 64.0f;
        case ChunkResolution::RES_2M:    return 128.0f;
        case ChunkResolution::RES_4M:    return 256.0f;
        case ChunkResolution::RES_8M:    return 512.0f;
        case ChunkResolution::RES_16M:   return 1024.0f;
        case ChunkResolution::RES_32M:   return 2048.0f;
        default: return 16.0f;
    }
}

ChunkResolution ChunkManager::getResolutionForLayer(int layer) {
    switch (layer) {
        case 0: return ChunkResolution::RES_0_25M;
        case 1: return ChunkResolution::RES_0_5M;
        case 2: return ChunkResolution::RES_1M;
        case 3: return ChunkResolution::RES_2M;
        case 4: return ChunkResolution::RES_4M;
        case 5: return ChunkResolution::RES_8M;
        case 6: return ChunkResolution::RES_16M;
        case 7: return ChunkResolution::RES_32M;
        default: return ChunkResolution::RES_32M;
    }
}

int ChunkManager::getLayerForGridPosition(int pos) {
    if (pos >= 0 && pos < 18) {
        return GRID_LAYERS[pos];
    }
    return 7;  // Default to outermost layer
}

ChunkCoord ChunkManager::worldToChunkCoord(const math::Vec3& worldPos, ChunkResolution res) const {
    float size = getChunkSizeForResolution(res);
    
    return ChunkCoord(
        static_cast<int32_t>(std::floor(worldPos.x / size)),
        static_cast<int32_t>(std::floor(worldPos.y / size)),
        static_cast<int32_t>(std::floor(worldPos.z / size)),
        res
    );
}

std::vector<ChunkCoord> ChunkManager::getRequiredChunks(const math::Vec3& playerPos) const {
    std::vector<ChunkCoord> result;
    result.reserve(RENDER_CHUNKS_PER_AXIS * RENDER_CHUNKS_PER_AXIS * RENDER_CHUNKS_PER_AXIS);
    
    // Get anchor position (use current if initialized, otherwise calculate)
    float ax, ay, az;
    if (anchorInitialized) {
        ax = anchorPos.x;
        ay = anchorPos.y;
        az = anchorPos.z;
    } else {
        ChunkCoord pChunk = worldToChunkCoord(playerPos, ChunkResolution::RES_0_25M);
        ax = (pChunk.x - 1) * 16.0f;
        ay = (pChunk.y - 1) * 16.0f;
        az = (pChunk.z - 1) * 16.0f;
    }
    
    // Iterate through all 18x18x18 grid positions
    for (int gx = 0; gx < RENDER_CHUNKS_PER_AXIS; ++gx) {
        for (int gy = 0; gy < RENDER_CHUNKS_PER_AXIS; ++gy) {
            for (int gz = 0; gz < RENDER_CHUNKS_PER_AXIS; ++gz) {
                // Determine layer based on max distance from center in any axis
                int layer = std::max({GRID_LAYERS[gx], GRID_LAYERS[gy], GRID_LAYERS[gz]});
                
                ChunkResolution res = getResolutionForLayer(layer);
                float chunkSize = getChunkSizeForResolution(res);
                
                // Calculate world position for this grid cell
                float worldX = ax + GRID_OFFSETS[gx];
                float worldY = ay + GRID_OFFSETS[gy];
                float worldZ = az + GRID_OFFSETS[gz];
                
                // Convert to chunk coordinate for this resolution
                int32_t cx = static_cast<int32_t>(std::floor(worldX / chunkSize));
                int32_t cy = static_cast<int32_t>(std::floor(worldY / chunkSize));
                int32_t cz = static_cast<int32_t>(std::floor(worldZ / chunkSize));
                
                result.push_back(ChunkCoord(cx, cy, cz, res));
            }
        }
    }
    
    return result;
}

std::string ChunkManager::getChunkFilename(const ChunkCoord& coord) const {
    return chunkDirectory + "/chunk_" + coord.toString() + ".bin";
}

size_t ChunkManager::getLoadedChunkCount() const {
    std::lock_guard<std::mutex> lock(chunksMutex);
    return chunks.size();
}

size_t ChunkManager::getPendingLoadCount() const {
    std::lock_guard<std::mutex> lock(loadQueueMutex);
    return pendingLoads.size();
}

std::vector<const Chunk*> ChunkManager::getVisibleChunks(const math::Vec3& cameraPos, 
                                                         const math::Vec3& cameraDir,
                                                         const math::Vec3& cameraUp,
                                                         float fov) const {
    std::vector<const Chunk*> result;
    result.reserve(512);
    
    std::lock_guard<std::mutex> lock(chunksMutex);
    
    math::Vec3 normalizedDir = cameraDir.normalize();
    
    for (const auto& pair : chunks) {
        if (!pair.second || !pair.second->isLoaded()) {
            continue;
        }
        
        // Skip chunks that collapsed to a single air node
        const auto& svo = pair.second->getSVO();
        if (svo.isCollapsed() && svo.getRootMaterial() == 0) {
            continue;
        }
        
        const auto& aabb = pair.second->getAABB();
        float chunkSize = pair.second->getSize();
        
        // Always include chunks containing the camera
        if (aabb.contains(cameraPos)) {
            result.push_back(pair.second.get());
            continue;
        }
        
        math::Vec3 chunkCenter = aabb.getCenter();
        float distance = (chunkCenter - cameraPos).length();
        
        // Always include nearby chunks
        if (distance < chunkSize * 2.0f) {
            result.push_back(pair.second.get());
            continue;
        }
        
        // Distance-based culling (8192m render distance)
        if (distance > 9000.0f) {
            continue;
        }
        
        // Frustum check
        math::Vec3 toChunk = (chunkCenter - cameraPos).normalize();
        float dot = normalizedDir.dot(toChunk);
        
        // Include if chunk is within ~120 degree FOV
        if (dot > -0.3f) {
            result.push_back(pair.second.get());
        }
    }
    
    return result;
}

void ChunkManager::addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(networking::ChunkDataHeader)) {
        LOG_ERROR("Invalid chunk data from network");
        return;
    }
    
    const networking::ChunkDataHeader* header = 
        reinterpret_cast<const networking::ChunkDataHeader*>(data.data());
    
    if (header->type != networking::MessageType::CHUNK_DATA) {
        LOG_ERROR("Invalid message type in chunk data");
        return;
    }
    
    if (header->x != coord.x || header->y != coord.y || header->z != coord.z ||
        header->resolution != static_cast<uint8_t>(coord.resolution)) {
        LOG_WARNING("Chunk coordinate mismatch in network data");
        return;
    }
    
    size_t headerSize = sizeof(networking::ChunkDataHeader);
    size_t nodeDataSize = header->nodeCount * sizeof(SVONode);
    
    if (data.size() < headerSize + nodeDataSize) {
        LOG_ERROR("Incomplete chunk data from network");
        return;
    }
    
    std::vector<SVONode> nodes(header->nodeCount);
    std::memcpy(nodes.data(), data.data() + headerSize, nodeDataSize);
    
    // Create or update chunk
    std::lock_guard<std::mutex> lock(chunksMutex);
    auto it = chunks.find(coord);
    if (it == chunks.end()) {
        chunks[coord] = std::make_unique<Chunk>(coord);
        it = chunks.find(coord);
    }
    
    Chunk* chunk = it->second.get();
    chunk->getSVO().clear();
    chunk->getSVO().loadNodes(nodes);
    chunk->setLoaded(true);
    
    // Remove from pending if it was there
    {
        std::lock_guard<std::mutex> queueLock(loadQueueMutex);
        pendingLoads.erase(coord);
    }
}

} // namespace voxels
} // namespace chonk

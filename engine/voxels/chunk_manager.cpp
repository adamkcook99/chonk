#include "chunk_manager.h"
#include "world_constants.h"
#include "../../game/world/world_generator.h"
#include "../networking/network.h"
#include "../core/logging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace chonk {
namespace voxels {

ChunkManager::ChunkManager() : superChunkCache(TOTAL_SUPER_CHUNKS + 25) {
}

ChunkManager::~ChunkManager() {
    shutdown();
}

void ChunkManager::initialize(game::world::WorldGenerator* generator) {
    worldGenerator = generator;
    running = true;
    
    // Start multiple load threads for faster chunk generation
    for (int i = 0; i < NUM_LOAD_THREADS; ++i) {
        loadThreads.emplace_back(&ChunkManager::loadThreadFunc, this);
    }
    refineThread = std::thread(&ChunkManager::refineThreadFunc, this);
    
    LOG_INFO("ChunkManager initialized with " + std::to_string(NUM_LOAD_THREADS) + " load threads");
}

void ChunkManager::shutdown() {
    if (running) {
        running = false;
        loadCondition.notify_all();
        refineCondition.notify_all();
        
        for (auto& thread : loadThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        loadThreads.clear();
        
        if (refineThread.joinable()) {
            refineThread.join();
        }
    }
    
    // Save all chunks
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (auto* super : superChunkCache.getAll()) {
        std::string superDir = getSuperChunkDirectory(super->getCoord());
        std::filesystem::create_directories(superDir);
        
        for (auto* chunk : super->getLoadedChunks()) {
            std::string filename = getChunkFilename(chunk->getCoord());
            chunk->saveToFile(filename);
        }
    }
}

uint32_t ChunkManager::calculateTargetDepth(const ChunkCoord& coord, const math::Vec3& playerPos) const {
    math::Vec3 chunkCenter = coord.toWorldPos() + 
                              math::Vec3(CHUNK_SIZE_METERS * 0.5f, 
                                         CHUNK_SIZE_METERS * 0.5f, 
                                         CHUNK_SIZE_METERS * 0.5f);
    float distance = (chunkCenter - playerPos).length();
    
    // Get render depth and add 1 for buffer
    uint32_t renderDepth = getMaxDepthForDistance(distance);
    return std::min(renderDepth + 1, CHUNK_SVO_DEPTH);
}

void ChunkManager::update(const math::Vec3& playerPos, uint32_t frame) {
    currentFrame = frame;
    lastPlayerPos = playerPos;
    
    SuperChunkCoord playerSuperCoord = SuperChunkCoord::fromWorldPos(playerPos);
    
    // Get required super chunks (5x5x5 around player)
    std::vector<SuperChunkCoord> requiredSupers = getRequiredSuperChunks(playerPos);
    
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        // Ensure all required super chunks exist
        for (const auto& superCoord : requiredSupers) {
            SuperChunk* super = getOrCreateSuperChunk(superCoord);
            if (super) {
                superChunkCache.touch(superCoord, frame);
            }
        }
    }
    
    // Queue chunks for loading and refinement
    {
        std::lock_guard<std::mutex> queueLock(loadQueueMutex);
        std::lock_guard<std::mutex> refineLock(refineQueueMutex);
        
        for (const auto& superCoord : requiredSupers) {
            std::vector<ChunkCoord> neededChunks = getRequiredChunksInSuper(superCoord, playerPos);
            
            for (const auto& chunkCoord : neededChunks) {
                float priority = calculateLoadPriority(chunkCoord, playerPos);
                uint32_t targetDepth = calculateTargetDepth(chunkCoord, playerPos);
                
                // Check if already loaded
                Chunk* existingChunk = nullptr;
                {
                    std::lock_guard<std::mutex> cacheLock(cacheMutex);
                    SuperChunk* super = superChunkCache.get(superCoord);
                    if (super) {
                        ChunkCoord first = superCoord.getFirstChunkCoord();
                        uint32_t lx = chunkCoord.x - first.x;
                        uint32_t ly = chunkCoord.y - first.y;
                        uint32_t lz = chunkCoord.z - first.z;
                        existingChunk = super->getChunk(lx, ly, lz);
                    }
                }
                
                if (existingChunk) {
                    // Chunk exists - check if it needs refinement
                    if (existingChunk->hasMoreDetail() && 
                        existingChunk->getCurrentDepth() < targetDepth &&
                        pendingRefines.find(chunkCoord) == pendingRefines.end()) {
                        queueChunkRefine(chunkCoord, superCoord, targetDepth, priority);
                    }
                } else {
                    // Chunk doesn't exist - queue for loading
                    if (pendingLoads.find(chunkCoord) == pendingLoads.end()) {
                        queueChunkLoad(chunkCoord, superCoord, targetDepth, priority);
                    }
                }
            }
        }
    }
    
    // Log queue sizes periodically
    static uint32_t updateCount = 0;
    updateCount++;
    if (updateCount == 1 || updateCount % 500 == 0) {
        std::lock_guard<std::mutex> queueLock2(loadQueueMutex);
        LOG_INFO("Update " + std::to_string(updateCount) + 
                 ": " + std::to_string(pendingLoads.size()) + " pending loads");
    }
    
    loadCondition.notify_all();  // Wake all load threads
    refineCondition.notify_one();
    lastPlayerSuperChunk = playerSuperCoord;
}

std::vector<SuperChunkCoord> ChunkManager::getRequiredSuperChunks(const math::Vec3& playerPos) const {
    std::vector<SuperChunkCoord> result;
    SuperChunkCoord center = SuperChunkCoord::fromWorldPos(playerPos);

    // Keep current horizontal radius (e.g., 5×5 if SUPER_CHUNKS_PER_AXIS=5)
    int radius = SUPER_CHUNKS_PER_AXIS / 2; // was 2 for 5x5

    // Vertical: one layer below the player, the player layer, and all above up to the top
    int minY = std::max(0, center.y - 1);
    int maxY = static_cast<int>(WORLD_HEIGHT_METERS / SUPER_CHUNK_SIZE_METERS) - 1; // e.g., 4 for 10240/2048

    for (int y = minY; y <= maxY; ++y) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                result.push_back(SuperChunkCoord(center.x + dx, y, center.z + dz));
            }
        }
    }
    return result;
}

std::vector<ChunkCoord> ChunkManager::getRequiredChunksInSuper(const SuperChunkCoord& superCoord, 
                                                               const math::Vec3& playerPos) const {
    std::vector<ChunkCoord> result;
    
    ChunkCoord first = superCoord.getFirstChunkCoord();
    
    for (uint32_t z = 0; z < CHUNKS_PER_SUPER_AXIS; ++z) {
        for (uint32_t y = 0; y < CHUNKS_PER_SUPER_AXIS; ++y) {
            for (uint32_t x = 0; x < CHUNKS_PER_SUPER_AXIS; ++x) {
                ChunkCoord coord(first.x + x, first.y + y, first.z + z);
                
                math::Vec3 chunkCenter = coord.toWorldPos() + 
                                          math::Vec3(CHUNK_SIZE_METERS * 0.5f, 
                                                     CHUNK_SIZE_METERS * 0.5f, 
                                                     CHUNK_SIZE_METERS * 0.5f);
                float dist = (chunkCenter - playerPos).length();
                
                if (dist <= VIEW_DISTANCE_METERS) {
                    result.push_back(coord);
                }
            }
        }
    }
    
    return result;
}

SuperChunk* ChunkManager::getOrCreateSuperChunk(const SuperChunkCoord& coord) {
    SuperChunk* existing = superChunkCache.get(coord);
    if (existing) {
        return existing;
    }
    
    auto newSuper = std::make_unique<SuperChunk>(coord);
    SuperChunk* ptr = newSuper.get();
    superChunkCache.add(std::move(newSuper));
    
    return ptr;
}

float ChunkManager::calculateLoadPriority(const ChunkCoord& coord, const math::Vec3& playerPos) const {
    math::Vec3 chunkCenter = coord.toWorldPos() + 
                              math::Vec3(CHUNK_SIZE_METERS * 0.5f, 
                                         CHUNK_SIZE_METERS * 0.5f, 
                                         CHUNK_SIZE_METERS * 0.5f);
    return (chunkCenter - playerPos).length();
}

void ChunkManager::queueChunkLoad(const ChunkCoord& coord, const SuperChunkCoord& superCoord, 
                                   uint32_t targetDepth, float priority) {
    if (pendingLoads.find(coord) != pendingLoads.end()) {
        return;
    }
    
    // Limit queue size to prevent memory issues
    if (pendingLoads.size() >= 500) {
        return;  // Skip, will be queued on next update
    }
    
    loadQueue.push({coord, superCoord, targetDepth, priority});
    pendingLoads.insert(coord);
}

void ChunkManager::queueChunkRefine(const ChunkCoord& coord, const SuperChunkCoord& superCoord,
                                     uint32_t targetDepth, float priority) {
    if (pendingRefines.find(coord) != pendingRefines.end()) {
        return;
    }
    
    refineQueue.push({coord, superCoord, targetDepth, priority});
    pendingRefines.insert(coord);
}

void ChunkManager::loadThreadFunc() {
    static uint32_t chunksGenerated = 0;
    
    while (running) {
        ChunkLoadRequest request;
        bool hasWork = false;
        
        {
            std::unique_lock<std::mutex> lock(loadQueueMutex);
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
            SuperChunk* super = nullptr;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                super = getOrCreateSuperChunk(request.superCoord);
            }
            
            if (!super) continue;
            
            ChunkCoord first = request.superCoord.getFirstChunkCoord();
            uint32_t lx = request.coord.x - first.x;
            uint32_t ly = request.coord.y - first.y;
            uint32_t lz = request.coord.z - first.z;
            
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                if (super->getChunk(lx, ly, lz) != nullptr) {
                    continue;
                }
            }
            
            std::string filename = getChunkFilename(request.coord);
            auto chunk = std::make_unique<Chunk>(request.coord);
            
            if (!chunk->loadFromFile(filename)) {
                // Generate at target depth (renderDepth + 1)
                chunk->generate(worldGenerator, request.targetDepth);
                
                chunksGenerated++;
                if (chunksGenerated <= 10 || chunksGenerated % 50 == 0) {
                    LOG_INFO("Generated chunk " + request.coord.toString() + 
                             " at depth " + std::to_string(request.targetDepth) +
                             " (total: " + std::to_string(chunksGenerated) + ")");
                }
                
                std::string superDir = getSuperChunkDirectory(request.superCoord);
                std::filesystem::create_directories(superDir);
                chunk->saveToFile(filename);
            }
            
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                super->setChunk(lx, ly, lz, std::move(chunk));
            }
        }
    }
}

void ChunkManager::refineThreadFunc() {
    while (running) {
        ChunkRefineRequest request;
        bool hasWork = false;
        
        {
            std::unique_lock<std::mutex> lock(refineQueueMutex);
            refineCondition.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !refineQueue.empty() || !running;
            });
            
            if (!running) break;
            
            if (!refineQueue.empty()) {
                request = refineQueue.top();
                refineQueue.pop();
                pendingRefines.erase(request.coord);
                hasWork = true;
            }
        }
        
        if (hasWork) {
            SuperChunk* super = nullptr;
            Chunk* chunk = nullptr;
            
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                super = superChunkCache.get(request.superCoord);
                if (super) {
                    ChunkCoord first = request.superCoord.getFirstChunkCoord();
                    uint32_t lx = request.coord.x - first.x;
                    uint32_t ly = request.coord.y - first.y;
                    uint32_t lz = request.coord.z - first.z;
                    chunk = super->getChunk(lx, ly, lz);
                }
            }
            
            if (!chunk || !chunk->hasMoreDetail()) continue;
            if (chunk->getCurrentDepth() >= request.targetDepth) continue;
            
            // Refine the chunk to the new depth
            chunk->refineDetail(worldGenerator, request.targetDepth);
            
            // Save updated chunk
            std::string filename = getChunkFilename(request.coord);
            chunk->saveToFile(filename);
        }
    }
}

Chunk* ChunkManager::getChunkAtWorld(const math::Vec3& worldPos) {
    SuperChunkCoord superCoord = SuperChunkCoord::fromWorldPos(worldPos);
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    SuperChunk* super = superChunkCache.get(superCoord);
    if (!super) return nullptr;
    
    return super->getChunkAtWorldPos(worldPos);
}

const Chunk* ChunkManager::getChunkAtWorld(const math::Vec3& worldPos) const {
    return const_cast<ChunkManager*>(this)->getChunkAtWorld(worldPos);
}

std::vector<ChunkManager::ChunkRenderInfo> ChunkManager::getChunksForRendering(
    const math::Vec3& cameraPos, const math::Vec3& cameraDir, float fov) const {
    
    std::vector<ChunkRenderInfo> result;
    result.reserve(1000);
    
    math::Vec3 camDirNorm = cameraDir.normalize();
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (const SuperChunk* super : superChunkCache.getAll()) {
        math::Vec3 superCenter = super->getWorldPosition() + 
                                  math::Vec3(SUPER_CHUNK_SIZE_METERS * 0.5f, 
                                             SUPER_CHUNK_SIZE_METERS * 0.5f, 
                                             SUPER_CHUNK_SIZE_METERS * 0.5f);
        math::Vec3 toSuper = (superCenter - cameraPos).normalize();
        float superDot = camDirNorm.dot(toSuper);
        float superDist = (superCenter - cameraPos).length();
        
        if (superDot < -0.3f && superDist > SUPER_CHUNK_SIZE_METERS * 2) {
            continue;
        }
        
        for (const Chunk* chunk : super->getLoadedChunks()) {
            if (!chunk || chunk->isAir()) continue;
            
            math::Vec3 chunkCenter = chunk->getWorldPosition() + 
                                      math::Vec3(CHUNK_SIZE_METERS * 0.5f, 
                                                 CHUNK_SIZE_METERS * 0.5f, 
                                                 CHUNK_SIZE_METERS * 0.5f);
            float distance = (chunkCenter - cameraPos).length();
            
            if (distance > VIEW_DISTANCE_METERS) continue;
            
            math::Vec3 toChunk = (chunkCenter - cameraPos).normalize();
            float dot = camDirNorm.dot(toChunk);
            
            if (dot < -0.2f && distance > CHUNK_SIZE_METERS * 2) {
                continue;
            }
            
            uint32_t maxDepth = getMaxDepthForDistance(distance);
            uint32_t availableDepth = chunk->getCurrentDepth();
            
            result.push_back({chunk, maxDepth, availableDepth, distance});
        }
    }
    
    std::sort(result.begin(), result.end(), 
              [](const auto& a, const auto& b) { return a.distance < b.distance; });
    
    return result;
}

std::vector<const Chunk*> ChunkManager::getAllLoadedChunks() const {
    std::vector<const Chunk*> result;
    result.reserve(1000);
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (const SuperChunk* super : superChunkCache.getAll()) {
        for (const Chunk* chunk : super->getLoadedChunks()) {
            if (chunk && !chunk->isAir()) {
                result.push_back(chunk);
            }
        }
    }
    
    return result;
}

std::vector<Chunk*> ChunkManager::getSimulationChunks() {
    std::vector<Chunk*> result;
    
    SuperChunkCoord centerSuper = SuperChunkCoord::fromWorldPos(lastPlayerPos);
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    SuperChunk* super = superChunkCache.get(centerSuper);
    if (super) {
        result = super->getLoadedChunks();
    }
    
    return result;
}

uint8_t ChunkManager::getMaterialAtWorld(const math::Vec3& worldPos, const math::Vec3& cameraPos) const {
    const Chunk* chunk = getChunkAtWorld(worldPos);
    if (!chunk) return 0;
    
    float distance = (worldPos - cameraPos).length();
    uint32_t maxDepth = getMaxDepthForDistance(distance);
    
    return chunk->getMaterialAtWorld(worldPos, maxDepth);
}

size_t ChunkManager::getLoadedChunkCount() const {
    size_t count = 0;
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (const SuperChunk* super : superChunkCache.getAll()) {
        count += super->getLoadedChunkCount();
    }
    return count;
}

size_t ChunkManager::getLoadedSuperChunkCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return superChunkCache.size();
}

size_t ChunkManager::getPendingLoadCount() const {
    std::lock_guard<std::mutex> lock(loadQueueMutex);
    return pendingLoads.size();
}

size_t ChunkManager::getPendingRefineCount() const {
    std::lock_guard<std::mutex> lock(refineQueueMutex);
    return pendingRefines.size();
}

std::string ChunkManager::getChunkFilename(const ChunkCoord& coord) const {
    SuperChunkCoord superCoord(
        coord.x / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
        coord.y / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
        coord.z / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS)
    );
    return getSuperChunkDirectory(superCoord) + "/chunk_" + coord.toString() + ".bin";
}

std::string ChunkManager::getSuperChunkDirectory(const SuperChunkCoord& coord) const {
    return chunkDirectory + "/" + coord.toString();
}

void ChunkManager::addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(networking::ChunkDataHeader)) {
        return;
    }
    
    const networking::ChunkDataHeader* header = 
        reinterpret_cast<const networking::ChunkDataHeader*>(data.data());
    
    if (header->type != networking::MessageType::CHUNK_DATA) {
        return;
    }
    
    if (header->x != coord.x || header->y != coord.y || header->z != coord.z) {
        return;
    }
    
    size_t headerSize = sizeof(networking::ChunkDataHeader);
    size_t nodeDataSize = header->nodeCount * sizeof(SVONode);
    
    if (data.size() < headerSize + nodeDataSize) {
        return;
    }
    
    std::vector<SVONode> nodes(header->nodeCount);
    std::memcpy(nodes.data(), data.data() + headerSize, nodeDataSize);
    
    SuperChunkCoord superCoord(
        coord.x / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
        coord.y / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS),
        coord.z / static_cast<int32_t>(CHUNKS_PER_SUPER_AXIS)
    );
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    SuperChunk* super = getOrCreateSuperChunk(superCoord);
    if (!super) return;
    
    ChunkCoord first = superCoord.getFirstChunkCoord();
    uint32_t lx = coord.x - first.x;
    uint32_t ly = coord.y - first.y;
    uint32_t lz = coord.z - first.z;
    
    auto chunk = std::make_unique<Chunk>(coord);
    chunk->getSVO().loadNodes(nodes);
    chunk->setLoaded(true);
    
    super->setChunk(lx, ly, lz, std::move(chunk));
    
    {
        std::lock_guard<std::mutex> queueLock(loadQueueMutex);
        pendingLoads.erase(coord);
    }
}

} // namespace voxels
} // namespace chonk

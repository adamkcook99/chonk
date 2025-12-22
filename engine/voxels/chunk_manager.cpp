#include "chunk_manager.h"
#include "../../game/world/world_generator.h"
#include "../core/logging.h"
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace chonk {
namespace voxels {

ChunkManager::ChunkManager() 
    : loadQueue([](const ChunkArray::ChunkLoadInfo& a, const ChunkArray::ChunkLoadInfo& b) {
        return a.priority > b.priority;  // Min-heap: lower priority first
    })
{
}

ChunkManager::~ChunkManager() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

void ChunkManager::initialize(game::world::WorldGenerator* gen) {
    generator = gen;
    
    // Create chunk directory if needed
    std::filesystem::create_directories(chunkDirectory);
    
    // Initialize world tree centered at origin
    worldTree.initialize(math::Vec3(0, GROUND_LEVEL, 0));
    
    // Pre-allocate GPU update buffers
    gpuUpdateFront.metas.resize(CHUNK_ARRAY_TOTAL);
    gpuUpdateBack.metas.resize(CHUNK_ARRAY_TOTAL);
    gpuUpdateFront.nodes.reserve(CHUNK_ARRAY_TOTAL * 100);  // Estimate initial capacity
    gpuUpdateBack.nodes.reserve(CHUNK_ARRAY_TOTAL * 100);
    
    // Start worker threads
    running = true;
    loadThreads.reserve(NUM_LOAD_THREADS);
    for (int i = 0; i < NUM_LOAD_THREADS; ++i) {
        loadThreads.emplace_back(&ChunkManager::loadThreadFunc, this);
    }
    
    // Start GPU update worker
    startGPUUpdateWorker();
    
    LOG_INFO("ChunkManager initialized with " + std::to_string(NUM_LOAD_THREADS) + " load threads + 1 GPU update thread");
}

void ChunkManager::shutdown() {
    // Stop GPU update worker first
    stopGPUUpdateWorker();
    
    // Stop load worker threads
    {
        std::lock_guard<std::mutex> lock(loadQueueMutex);
        running = false;
    }
    loadCondition.notify_all();
    
    for (auto& thread : loadThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    loadThreads.clear();
    
    LOG_INFO("ChunkManager shutdown");
}

// ============================================================================
// Update
// ============================================================================

void ChunkManager::update(const math::Vec3& playerPos, uint32_t frame) {
    currentFrame = frame;
    
    // Get current player chunk coordinate
    ChunkCoord playerChunkCoord = ChunkCoord::fromWorldPos(playerPos);
    
    // Check if player moved to a new chunk
    bool needsRecenter = firstUpdate || playerChunkCoord != lastPlayerChunkCoord;
    
    if (needsRecenter) {
        std::vector<ChunkCoord> needsLoading;
        size_t lodUpgradeCount = 0;
        
        // Take exclusive lock during recenter and LOD check
        {
            std::unique_lock<std::shared_mutex> arrayLock(chunkArrayMutex);
            
            // Recenter chunk array on player's chunk
            needsLoading = chunkArray.recenter(playerChunkCoord);
            
            // Mark that we need a full GPU update (array shifted)
            if (!needsLoading.empty()) {
                fullGPUUpdateNeeded = true;
            }
            
            // Queue new chunks that need loading
            for (const auto& coord : needsLoading) {
                uint32_t ix, iy, iz;
                if (chunkArray.chunkCoordToIndex(coord, ix, iy, iz)) {
                    ChunkArray::ChunkLoadInfo info;
                    info.coord = coord;
                    info.ix = ix;
                    info.iy = iy;
                    info.iz = iz;
                    info.requiredDepth = chunkArray.getRequiredDepth(ix, iy, iz);
                    
                    int32_t dx = static_cast<int32_t>(ix) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    int32_t dy = static_cast<int32_t>(iy) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    int32_t dz = static_cast<int32_t>(iz) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                    info.priority = std::sqrt(static_cast<float>(dx*dx + dy*dy + dz*dz));
                    
                    queueChunkLoad(info);
                }
            }
            
            // Check existing chunks for LOD upgrades (chunks that moved closer)
            for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
                for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
                    for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                        const Chunk* chunk = chunkArray.getByIndex(x, y, z);
                        if (chunk && chunk->isLoaded()) {
                            uint32_t requiredDepth = chunkArray.getRequiredDepth(x, y, z);
                            
                            // Need LOD upgrade?
                            if (chunk->getCurrentDepth() < requiredDepth) {
                                ChunkArray::ChunkLoadInfo info;
                                info.coord = chunkArray.indexToChunkCoord(x, y, z);
                                info.ix = x;
                                info.iy = y;
                                info.iz = z;
                                info.requiredDepth = requiredDepth;
                                
                                int32_t dx = static_cast<int32_t>(x) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                                int32_t dy = static_cast<int32_t>(y) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                                int32_t dz = static_cast<int32_t>(z) - static_cast<int32_t>(CHUNK_ARRAY_CENTER);
                                info.priority = std::sqrt(static_cast<float>(dx*dx + dy*dy + dz*dz));
                                
                                queueChunkLoad(info);
                                lodUpgradeCount++;
                            }
                        }
                    }
                }
            }
        }
        
        lastPlayerChunkCoord = playerChunkCoord;
        firstUpdate = false;
        
        if (!needsLoading.empty() || lodUpgradeCount > 0) {
            LOG_INFO("Recentered on chunk " + playerChunkCoord.toString() + 
                     ", queued " + std::to_string(needsLoading.size()) + " new + " +
                     std::to_string(lodUpgradeCount) + " LOD upgrades");
        }
    }
    
    // Process any completed chunks (this will mark dirty slots)
    processCompletedChunks();
    
    // Trigger GPU update if needed
    if (fullGPUUpdateNeeded || !dirtyChunkSlots.empty()) {
        triggerGPUUpdate(fullGPUUpdateNeeded);
        fullGPUUpdateNeeded = false;
    }
    
    lastPlayerPos = playerPos;
}

// ============================================================================
// Chunk Access
// ============================================================================

Chunk* ChunkManager::getChunkAtWorld(const math::Vec3& worldPos) {
    return chunkArray.getByWorldPos(worldPos);
}

const Chunk* ChunkManager::getChunkAtWorld(const math::Vec3& worldPos) const {
    return chunkArray.getByWorldPos(worldPos);
}

Chunk* ChunkManager::getChunkByCoord(const ChunkCoord& coord) {
    return chunkArray.getByChunkCoord(coord);
}

const Chunk* ChunkManager::getChunkByCoord(const ChunkCoord& coord) const {
    return chunkArray.getByChunkCoord(coord);
}

// ============================================================================
// Material Query
// ============================================================================

uint8_t ChunkManager::getMaterialAtWorld(const math::Vec3& worldPos) const {
    // First try chunk array
    const Chunk* chunk = chunkArray.getByWorldPos(worldPos);
    if (chunk && chunk->isLoaded()) {
        return chunk->getMaterialAtWorld(worldPos);
    }
    
    // Fall back to world tree for distant terrain
    return worldTree.getMaterialAtWorld(worldPos);
}

// ============================================================================
// Rendering Data
// ============================================================================

std::vector<const Chunk*> ChunkManager::getAllLoadedChunks() const {
    return chunkArray.getAllLoadedChunks();
}

std::vector<ChunkManager::GPUChunkData> ChunkManager::getChunksForGPU() const {
    std::vector<GPUChunkData> result;
    result.reserve(CHUNK_ARRAY_TOTAL);
    
    for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                const Chunk* chunk = chunkArray.getByIndex(x, y, z);
                if (chunk && chunk->isLoaded()) {
                    GPUChunkData data;
                    data.chunk = chunk;
                    data.maxDepth = chunk->getCurrentDepth();
                    data.arrayIndexX = x;
                    data.arrayIndexY = y;
                    data.arrayIndexZ = z;
                    result.push_back(data);
                }
            }
        }
    }
    
    return result;
}

// ============================================================================
// Network Integration
// ============================================================================

void ChunkManager::addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data, uint32_t depth) {
    uint32_t ix, iy, iz;
    
    // Take exclusive lock while modifying chunk array
    std::unique_lock<std::shared_mutex> arrayLock(chunkArrayMutex);
    
    if (!chunkArray.chunkCoordToIndex(coord, ix, iy, iz)) {
        return;  // Out of bounds
    }
    
    auto chunk = std::make_unique<Chunk>(coord);
    chunk->deserialize(data.data(), data.size(), depth);
    
    // Add to world tree
    worldTree.updateFromChunk(*chunk);
    
    // Place in array
    chunkArray.setChunk(ix, iy, iz, std::move(chunk));
    
    // Mark dirty for GPU update
    uint32_t flatIdx = ix + iy * CHUNK_ARRAY_SIZE + iz * CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE;
    dirtyChunkSlots.insert(flatIdx);
    
    arrayLock.unlock();
    
    // Remove from pending
    {
        std::lock_guard<std::mutex> lock(loadQueueMutex);
        pendingLoads.erase(coord);
    }
}

// ============================================================================
// Statistics
// ============================================================================

size_t ChunkManager::getLoadedChunkCount() const {
    return chunkArray.getLoadedCount();
}

size_t ChunkManager::getPendingLoadCount() const {
    std::lock_guard<std::mutex> lock(loadQueueMutex);
    return pendingLoads.size();
}

// ============================================================================
// Async Loading
// ============================================================================

void ChunkManager::queueChunkLoad(const ChunkArray::ChunkLoadInfo& info) {
    std::lock_guard<std::mutex> lock(loadQueueMutex);
    
    // Check if already pending
    auto it = pendingLoads.find(info.coord);
    if (it != pendingLoads.end()) {
        // Already pending - check if we need a higher depth
        if (info.requiredDepth > it->second) {
            // Upgrade the pending depth - add new queue entry
            // (old entries will be filtered out in loadThreadFunc)
            it->second = info.requiredDepth;
            loadQueue.push(info);
            loadCondition.notify_one();
        }
        return;
    }
    
    // Skip if already loaded at sufficient depth
    // Note: This read is safe because we're called from update() which holds the array lock
    // or the check is best-effort (load thread will verify again)
    const Chunk* existing = chunkArray.getByIndex(info.ix, info.iy, info.iz);
    if (existing && existing->isLoaded() && existing->getCurrentDepth() >= info.requiredDepth) {
        return;
    }
    
    pendingLoads[info.coord] = info.requiredDepth;
    loadQueue.push(info);
    loadCondition.notify_one();
}

void ChunkManager::loadThreadFunc() {
    while (true) {
        ChunkArray::ChunkLoadInfo info;
        
        {
            std::unique_lock<std::mutex> lock(loadQueueMutex);
            loadCondition.wait(lock, [this] {
                return !running || !loadQueue.empty();
            });
            
            if (!running && loadQueue.empty()) {
                return;
            }
            
            if (loadQueue.empty()) {
                continue;
            }
            
            info = loadQueue.top();
            loadQueue.pop();
            
            // Check if this request has been superseded by a higher depth request
            auto pendingIt = pendingLoads.find(info.coord);
            if (pendingIt != pendingLoads.end() && pendingIt->second > info.requiredDepth) {
                // A higher depth request is pending - skip this one
                continue;
            }
        }
        
        // Verify chunk is still needed (player might have moved)
        // Take shared lock for reading chunk array state
        {
            std::shared_lock<std::shared_mutex> arrayLock(chunkArrayMutex);
            
            ChunkCoord currentCenter = chunkArray.getCenter();
            int32_t dx = info.coord.x - currentCenter.x;
            int32_t dy = info.coord.y - currentCenter.y;
            int32_t dz = info.coord.z - currentCenter.z;
            
            // Check if chunk is still in bounds
            bool stillNeeded = (std::abs(dx) < static_cast<int32_t>(CHUNK_ARRAY_SIZE / 2) &&
                                std::abs(dy) < static_cast<int32_t>(CHUNK_ARRAY_SIZE / 2) &&
                                std::abs(dz) < static_cast<int32_t>(CHUNK_ARRAY_SIZE / 2));
            
            if (!stillNeeded) {
                // Chunk no longer needed - remove from pending without processing
                arrayLock.unlock();
                std::lock_guard<std::mutex> lock(loadQueueMutex);
                pendingLoads.erase(info.coord);
                continue;
            }
            
            // Double-check that chunk isn't already loaded at sufficient depth
            const Chunk* existing = chunkArray.getByIndex(info.ix, info.iy, info.iz);
            if (existing && existing->isLoaded() && existing->getCurrentDepth() >= info.requiredDepth) {
                // Already loaded at sufficient depth - check if we should remove from pending
                arrayLock.unlock();
                std::lock_guard<std::mutex> lock(loadQueueMutex);
                auto pendingIt = pendingLoads.find(info.coord);
                if (pendingIt != pendingLoads.end() && pendingIt->second <= info.requiredDepth) {
                    pendingLoads.erase(pendingIt);
                }
                continue;
            }
        }
        
        // Generate or load chunk (outside of lock - this is the slow part)
        auto chunk = std::make_unique<Chunk>(info.coord);
        
        // File loading disabled for now
        // bool loaded = tryLoadFromFile(*chunk, info.requiredDepth);
        bool loaded = false;
        
        if (!loaded && generator) {
            // Generate new chunk at requested depth
            generator->generateChunk(*chunk, info.requiredDepth);
            
            // File saving disabled for now
            // if (info.requiredDepth > 0) {
            //     saveChunkToFile(*chunk);
            // }
        } else if (loaded && chunk->getCurrentDepth() < info.requiredDepth && generator) {
            // File had lower depth, upgrade it
            generator->generateChunk(*chunk, info.requiredDepth);
            // File saving disabled for now
            // saveChunkToFile(*chunk);
        }
        
        // Add to completed queue
        {
            std::lock_guard<std::mutex> lock(completedMutex);
            CompletedChunk completed;
            completed.chunk = std::move(chunk);
            completed.ix = info.ix;
            completed.iy = info.iy;
            completed.iz = info.iz;
            completed.depth = info.requiredDepth;
            completedChunks.push(std::move(completed));
        }
    }
}

void ChunkManager::processCompletedChunks() {
    std::lock_guard<std::mutex> lock(completedMutex);
    
    // Take exclusive lock on chunk array while modifying it
    std::unique_lock<std::shared_mutex> arrayLock(chunkArrayMutex);
    
    // Process up to 100 completed chunks per frame (increased for faster loading)
    int processed = 0;
    while (!completedChunks.empty() && processed < 100) {
        CompletedChunk& completed = completedChunks.front();
        
        // Verify chunk is still needed at this position
        ChunkCoord expectedCoord = chunkArray.indexToChunkCoord(completed.ix, completed.iy, completed.iz);
        
        if (completed.chunk->getCoord() == expectedCoord) {
            // Check if there's a higher depth pending - if so, don't overwrite with lower depth
            bool shouldPlace = true;
            {
                std::lock_guard<std::mutex> loadLock(loadQueueMutex);
                auto pendingIt = pendingLoads.find(expectedCoord);
                if (pendingIt != pendingLoads.end() && pendingIt->second > completed.depth) {
                    // A higher depth request is pending - don't place this lower-depth chunk
                    shouldPlace = false;
                } else if (pendingIt != pendingLoads.end() && pendingIt->second <= completed.depth) {
                    // This completes or exceeds the pending request - remove from pending
                    pendingLoads.erase(pendingIt);
                }
            }
            
            if (shouldPlace) {
                // Place in array (fast operation)
                chunkArray.setChunk(completed.ix, completed.iy, completed.iz, std::move(completed.chunk));
                
                // Mark this slot as dirty for GPU update
                uint32_t flatIdx = completed.ix + 
                                   completed.iy * CHUNK_ARRAY_SIZE + 
                                   completed.iz * CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE;
                dirtyChunkSlots.insert(flatIdx);
            }
        } else {
            // Coord doesn't match - chunk was probably discarded during recenter
            // Remove from pending if this was the expected depth
            std::lock_guard<std::mutex> loadLock(loadQueueMutex);
            auto pendingIt = pendingLoads.find(completed.chunk->getCoord());
            if (pendingIt != pendingLoads.end() && pendingIt->second <= completed.depth) {
                pendingLoads.erase(pendingIt);
            }
        }
        
        completedChunks.pop();
        processed++;
    }
}

// ============================================================================
// File Operations
// ============================================================================

std::string ChunkManager::getChunkFilename(const ChunkCoord& coord) const {
    return chunkDirectory + "/" + coord.toString() + ".chunk";
}

bool ChunkManager::tryLoadFromFile(Chunk& chunk, uint32_t depth) {
    std::string filename = getChunkFilename(chunk.getCoord());
    
    if (!std::filesystem::exists(filename)) {
        return false;
    }
    
    if (!chunk.loadFromFile(filename)) {
        return false;
    }
    
    // Check if file has sufficient depth
    if (chunk.getCurrentDepth() < depth) {
        // Need to regenerate at higher detail
        return false;
    }
    
    return true;
}

void ChunkManager::saveChunkToFile(const Chunk& chunk) {
    std::string filename = getChunkFilename(chunk.getCoord());
    chunk.saveToFile(filename);
}

// ============================================================================
// GPU Update System
// ============================================================================

void ChunkManager::startGPUUpdateWorker() {
    gpuUpdateRunning = true;
    gpuUpdateThread = std::thread(&ChunkManager::gpuUpdateWorkerFunc, this);
}

void ChunkManager::stopGPUUpdateWorker() {
    {
        std::lock_guard<std::mutex> lock(gpuUpdateMutex);
        gpuUpdateRunning = false;
    }
    gpuUpdateCondition.notify_one();
    
    if (gpuUpdateThread.joinable()) {
        gpuUpdateThread.join();
    }
}

void ChunkManager::triggerGPUUpdate(bool fullUpdate) {
    {
        std::lock_guard<std::mutex> lock(gpuUpdateMutex);
        if (fullUpdate) {
            fullGPUUpdateNeeded = true;
        }
        // Dirty slots are already tracked
    }
    gpuUpdateCondition.notify_one();
}

void ChunkManager::gpuUpdateWorkerFunc() {
    while (gpuUpdateRunning) {
        bool needsFullUpdate = false;
        
        {
            std::unique_lock<std::mutex> lock(gpuUpdateMutex);
            gpuUpdateCondition.wait(lock, [this] {
                return !gpuUpdateRunning || fullGPUUpdateNeeded || !dirtyChunkSlots.empty();
            });
            
            if (!gpuUpdateRunning) {
                return;
            }
            
            // Consume the full update flag
            needsFullUpdate = fullGPUUpdateNeeded;
            fullGPUUpdateNeeded = false;  // Clear it after consuming
        }
        
        // Collect GPU data with shared lock (doesn't block main thread writes)
        collectGPUData(gpuUpdateFront, needsFullUpdate);
        
        // Swap buffers atomically
        {
            std::lock_guard<std::mutex> lock(gpuUpdateMutex);
            std::swap(gpuUpdateFront, gpuUpdateBack);
            gpuUpdateId++;
            gpuUpdateReady = true;
            dirtyChunkSlots.clear();
        }
    }
}

void ChunkManager::collectGPUData(GPUChunkUpdate& update, bool fullUpdate) {
    update.nodes.clear();
    update.updatedSlots.clear();
    
    // Take shared lock while reading chunk array - allows concurrent reads
    // but blocks during writes (recenter, processCompletedChunks)
    std::shared_lock<std::shared_mutex> arrayLock(chunkArrayMutex);
    
    // Get array origin (must be read under lock to match chunk data)
    update.arrayOrigin = chunkArray.getArrayOrigin();
    
    // Ensure metas array is correctly sized
    if (update.metas.size() != CHUNK_ARRAY_TOTAL) {
        update.metas.resize(CHUNK_ARRAY_TOTAL);
    }
    
    // Initialize all metas to empty if full update
    if (fullUpdate) {
        for (auto& meta : update.metas) {
            meta.nodeOffset = 0;
            meta.nodeCount = 0;
            meta.maxDepth = 0;
            meta.rootMaterial = 0;
        }
    }
    
    size_t loadedCount = 0;
    
    // Iterate through all chunks in the array
    for (uint32_t z = 0; z < CHUNK_ARRAY_SIZE; z++) {
        for (uint32_t y = 0; y < CHUNK_ARRAY_SIZE; y++) {
            for (uint32_t x = 0; x < CHUNK_ARRAY_SIZE; x++) {
                uint32_t flatIdx = x + y * CHUNK_ARRAY_SIZE + z * CHUNK_ARRAY_SIZE * CHUNK_ARRAY_SIZE;
                
                const Chunk* chunk = chunkArray.getByIndex(x, y, z);
                if (chunk && chunk->isLoaded()) {
                    const auto& nodes = chunk->getTree().getNodes();
                    
                    update.metas[flatIdx].nodeOffset = static_cast<uint32_t>(update.nodes.size());
                    update.metas[flatIdx].nodeCount = static_cast<uint32_t>(nodes.size());
                    update.metas[flatIdx].maxDepth = chunk->getCurrentDepth();
                    update.metas[flatIdx].rootMaterial = chunk->getTree().getRootMaterial();
                    
                    // Append nodes
                    update.nodes.insert(update.nodes.end(), nodes.begin(), nodes.end());
                    update.updatedSlots.push_back(flatIdx);
                    loadedCount++;
                } else {
                    // Empty chunk
                    update.metas[flatIdx].nodeOffset = 0;
                    update.metas[flatIdx].nodeCount = 0;
                    update.metas[flatIdx].maxDepth = 0;
                    update.metas[flatIdx].rootMaterial = 0;
                }
            }
        }
    }
    
    // Release lock before final processing
    arrayLock.unlock();
    
    // Ensure at least one node exists for shader
    if (update.nodes.empty()) {
        Tree27Node emptyNode{};
        std::memset(&emptyNode, 0, sizeof(Tree27Node));
        update.nodes.push_back(emptyNode);
    }
    
    update.loadedChunkCount = loadedCount;
    update.updateId = gpuUpdateId + 1;
    update.ready = true;
}

bool ChunkManager::hasGPUUpdate() const {
    return gpuUpdateReady.load();
}

const GPUChunkUpdate* ChunkManager::getGPUUpdate() {
    std::lock_guard<std::mutex> lock(gpuUpdateMutex);
    if (!gpuUpdateReady) {
        return nullptr;
    }
    gpuUpdateReady = false;
    return &gpuUpdateBack;
}

} // namespace voxels
} // namespace chonk

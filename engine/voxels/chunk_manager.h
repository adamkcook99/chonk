#pragma once

#include "chunk.h"
#include "chunk_array.h"
#include "world_tree.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include "tree27.h"
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace chonk {
namespace game {
namespace world {
    class WorldGenerator;
}
}

namespace voxels {

// ============================================================================
// GPUChunkUpdate: Data structure for incremental GPU buffer updates
// ============================================================================
// This is filled by background workers and consumed by the renderer
// without blocking the main thread.
// ============================================================================

struct GPUChunkMeta {
    uint32_t nodeOffset;      // Start index in global node array
    uint32_t nodeCount;       // Number of nodes for this chunk
    uint32_t maxDepth;        // LOD depth limit
    uint32_t rootMaterial;    // For quick air check (0 = air)
};

struct GPUChunkUpdate {
    std::vector<Tree27Node> nodes;                      // All node data
    std::vector<GPUChunkMeta> metas;                    // Metadata for each array slot (17^3)
    std::vector<uint32_t> updatedSlots;                 // Which array slots changed
    uint64_t updateId = 0;                              // Monotonic update ID
    math::Vec3 arrayOrigin{0, 0, 0};                    // World position of chunk[0,0,0]
    size_t loadedChunkCount = 0;                        // Number of loaded chunks
    bool ready = false;                                 // Data is ready for GPU upload
};

// ============================================================================
// ChunkManager: Manages 17×17×17 chunk array + world tree
// ============================================================================
// - Handles chunk loading/unloading based on player position
// - Uses async worker threads for generation
// - Maintains world tree for distant LOD
// ============================================================================

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    void initialize(game::world::WorldGenerator* generator);
    void shutdown();
    
    // ========================================================================
    // Update
    // ========================================================================
    
    // Update chunk loading based on player position
    void update(const math::Vec3& playerPos, uint32_t currentFrame);
    
    // ========================================================================
    // Chunk Access
    // ========================================================================
    
    // Get chunk at world position (from array, nullptr if not loaded/out of bounds)
    Chunk* getChunkAtWorld(const math::Vec3& worldPos);
    const Chunk* getChunkAtWorld(const math::Vec3& worldPos) const;
    
    // Get chunk by coordinate
    Chunk* getChunkByCoord(const ChunkCoord& coord);
    const Chunk* getChunkByCoord(const ChunkCoord& coord) const;
    
    // ========================================================================
    // Material Query
    // ========================================================================
    
    // Query material at world position
    // Uses chunk array for nearby, world tree for distant
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos) const;
    
    // ========================================================================
    // Rendering Data
    // ========================================================================
    
    // Get all loaded non-air chunks for rendering
    std::vector<const Chunk*> getAllLoadedChunks() const;
    
    // Data for GPU upload
    struct GPUChunkData {
        const Chunk* chunk;
        uint32_t maxDepth;
        uint32_t arrayIndexX;
        uint32_t arrayIndexY;
        uint32_t arrayIndexZ;
    };
    std::vector<GPUChunkData> getChunksForGPU() const;
    
    // Get chunk array for direct access
    const ChunkArray& getChunkArray() const { return chunkArray; }
    
    // Get world tree for GPU upload
    const WorldTree& getWorldTree() const { return worldTree; }
    
    // ========================================================================
    // Network Integration
    // ========================================================================
    
    // Add chunk received from server
    void addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data, uint32_t depth);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    size_t getLoadedChunkCount() const;
    size_t getPendingLoadCount() const;
    size_t getWorldTreeNodeCount() const { return worldTree.getNodeCount(); }
    
    // ========================================================================
    // GPU Update System
    // ========================================================================
    
    // Check if there's a new GPU update ready (non-blocking)
    bool hasGPUUpdate() const;
    
    // Get the latest GPU update (swaps internal buffers)
    // Returns nullptr if no update ready
    const GPUChunkUpdate* getGPUUpdate();
    
    // Get current update ID (for checking if update needed)
    uint64_t getCurrentGPUUpdateId() const { return gpuUpdateId.load(); }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void setChunkDirectory(const std::string& dir) { chunkDirectory = dir; }
    const std::string& getChunkDirectory() const { return chunkDirectory; }
    
private:
    // Chunk storage
    ChunkArray chunkArray;
    WorldTree worldTree;
    mutable std::shared_mutex chunkArrayMutex;  // Protects chunk array access
    
    // World generator
    game::world::WorldGenerator* generator = nullptr;
    std::string chunkDirectory = "chunks";
    
    // Player tracking
    math::Vec3 lastPlayerPos{0, 0, 0};
    ChunkCoord lastPlayerChunkCoord{0, 0, 0};
    uint32_t currentFrame = 0;
    bool firstUpdate = true;
    bool initialLoadComplete = false;  // True when all 17^3 chunks have root nodes
    
    // ========================================================================
    // Async Loading System
    // ========================================================================
    
    static constexpr int NUM_LOAD_THREADS = 4;
    std::vector<std::thread> loadThreads;
    std::atomic<bool> running{false};
    
    // Load queue
    std::priority_queue<ChunkArray::ChunkLoadInfo, 
                        std::vector<ChunkArray::ChunkLoadInfo>,
                        std::function<bool(const ChunkArray::ChunkLoadInfo&, const ChunkArray::ChunkLoadInfo&)>> loadQueue;
    std::unordered_map<ChunkCoord, uint32_t, ChunkCoordHash> pendingLoads;  // coord -> requested depth
    mutable std::mutex loadQueueMutex;
    std::condition_variable loadCondition;
    
    // Completed chunks waiting to be added to array
    struct CompletedChunk {
        std::unique_ptr<Chunk> chunk;
        uint32_t ix, iy, iz;
        uint32_t depth;
    };
    std::queue<CompletedChunk> completedChunks;
    mutable std::mutex completedMutex;
    
    // ========================================================================
    // GPU Update System
    // ========================================================================
    
    // Double-buffered GPU updates
    GPUChunkUpdate gpuUpdateFront;      // Being filled by worker
    GPUChunkUpdate gpuUpdateBack;       // Ready for renderer consumption
    mutable std::mutex gpuUpdateMutex;
    std::atomic<uint64_t> gpuUpdateId{0};
    std::atomic<bool> gpuUpdateReady{false};
    
    // Track which chunks need GPU update
    std::unordered_set<uint32_t> dirtyChunkSlots;  // Array indices needing update
    bool fullGPUUpdateNeeded = true;                // Force full rebuild (after recenter)
    
    // GPU update worker
    std::thread gpuUpdateThread;
    std::atomic<bool> gpuUpdateRunning{false};
    std::condition_variable gpuUpdateCondition;
    
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    void loadThreadFunc();
    void queueChunkLoad(const ChunkArray::ChunkLoadInfo& info);
    void processCompletedChunks();
    
    // GPU update worker
    void gpuUpdateWorkerFunc();
    void collectGPUData(GPUChunkUpdate& update, bool fullUpdate);
    void startGPUUpdateWorker();
    void stopGPUUpdateWorker();
    void triggerGPUUpdate(bool fullUpdate = false);
    
    // File operations
    std::string getChunkFilename(const ChunkCoord& coord) const;
    bool tryLoadFromFile(Chunk& chunk, uint32_t depth);
    void saveChunkToFile(const Chunk& chunk);
};

} // namespace voxels
} // namespace chonk

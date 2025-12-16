#pragma once

#include "chunk.h"
#include "super_chunk.h"
#include "world_constants.h"
#include "../math/vec3.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <memory>
#include <string>
#include <condition_variable>

namespace chonk {
namespace networking {
    struct ChunkDataHeader;
}

namespace game {
namespace world {
    class WorldGenerator;
}
}

namespace voxels {

// ============================================================================
// Chunk load request for async loading
// ============================================================================
struct ChunkLoadRequest {
    ChunkCoord coord;
    SuperChunkCoord superCoord;
    uint32_t targetDepth;  // Depth to generate to (renderDepth + 1)
    float priority;        // Lower = higher priority (closer = lower)
    
    bool operator<(const ChunkLoadRequest& other) const {
        return priority > other.priority;  // Min-heap (lower priority first)
    }
};

// ============================================================================
// Chunk refinement request for background detail loading
// ============================================================================
struct ChunkRefineRequest {
    ChunkCoord coord;
    SuperChunkCoord superCoord;
    uint32_t targetDepth;  // New depth to refine to
    float priority;
    
    bool operator<(const ChunkRefineRequest& other) const {
        return priority > other.priority;
    }
};

// ============================================================================
// Chunk Manager - manages super chunks and chunks with LOD
// ============================================================================
class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    
    // Initialize with world generator
    void initialize(game::world::WorldGenerator* generator);
    
    // Update chunk loading based on player position
    void update(const math::Vec3& playerPos, uint32_t currentFrame);
    
    // Get chunk at world position (may return nullptr if not loaded)
    Chunk* getChunkAtWorld(const math::Vec3& worldPos);
    const Chunk* getChunkAtWorld(const math::Vec3& worldPos) const;
    
    // Get chunks for rendering (non-air, within view distance)
    // Returns chunks with their recommended maxDepth based on distance from camera
    struct ChunkRenderInfo {
        const Chunk* chunk;
        uint32_t maxDepth;      // Depth to render at
        uint32_t availableDepth; // Depth available in chunk
        float distance;
    };
    std::vector<ChunkRenderInfo> getChunksForRendering(const math::Vec3& cameraPos, 
                                                        const math::Vec3& cameraDir,
                                                        float fov) const;
    
    // Get all loaded non-air chunks (for simple rendering)
    std::vector<const Chunk*> getAllLoadedChunks() const;
    
    // Get simulation chunks (center super chunk at full detail)
    std::vector<Chunk*> getSimulationChunks();
    
    // Material query with LOD
    uint8_t getMaterialAtWorld(const math::Vec3& worldPos, const math::Vec3& cameraPos) const;
    
    // Add chunk from network (deserialized)
    void addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data);
    
    // Statistics
    size_t getLoadedChunkCount() const;
    size_t getLoadedSuperChunkCount() const;
    size_t getPendingLoadCount() const;
    size_t getPendingRefineCount() const;
    
    void shutdown();
    
    // Set chunk save directory
    void setChunkDirectory(const std::string& dir) { chunkDirectory = dir; }
    
private:
    // Super chunk cache (popularity-ordered)
    SuperChunkCache superChunkCache;
    mutable std::mutex cacheMutex;
    
    // World generator
    game::world::WorldGenerator* worldGenerator = nullptr;
    std::string chunkDirectory = "chunks";
    
    // Async loading threads (multiple for faster loading)
    static constexpr int NUM_LOAD_THREADS = 4;
    std::vector<std::thread> loadThreads;
    std::atomic<bool> running{false};
    std::priority_queue<ChunkLoadRequest> loadQueue;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingLoads;
    mutable std::mutex loadQueueMutex;
    std::condition_variable loadCondition;
    
    // Background refinement thread
    std::thread refineThread;
    std::priority_queue<ChunkRefineRequest> refineQueue;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingRefines;
    mutable std::mutex refineQueueMutex;
    std::condition_variable refineCondition;
    
    // Current player position
    math::Vec3 lastPlayerPos{0, 0, 0};
    SuperChunkCoord lastPlayerSuperChunk{0, 0, 0};
    uint32_t currentFrame = 0;
    
    // Thread functions
    void loadThreadFunc();
    void refineThreadFunc();
    
    void queueChunkLoad(const ChunkCoord& coord, const SuperChunkCoord& superCoord, 
                        uint32_t targetDepth, float priority);
    void queueChunkRefine(const ChunkCoord& coord, const SuperChunkCoord& superCoord,
                          uint32_t targetDepth, float priority);
    
    // Calculate which super chunks are needed (5x5x5 around player)
    std::vector<SuperChunkCoord> getRequiredSuperChunks(const math::Vec3& playerPos) const;
    
    // Calculate which chunks in a super chunk need loading
    std::vector<ChunkCoord> getRequiredChunksInSuper(const SuperChunkCoord& superCoord, 
                                                      const math::Vec3& playerPos) const;
    
    // Get or create super chunk
    SuperChunk* getOrCreateSuperChunk(const SuperChunkCoord& coord);
    
    // Calculate load priority (distance-based)
    float calculateLoadPriority(const ChunkCoord& coord, const math::Vec3& playerPos) const;
    
    // Calculate target depth for a chunk based on distance
    uint32_t calculateTargetDepth(const ChunkCoord& coord, const math::Vec3& playerPos) const;
    
    // Chunk file management
    std::string getChunkFilename(const ChunkCoord& coord) const;
    std::string getSuperChunkDirectory(const SuperChunkCoord& coord) const;
};

} // namespace voxels
} // namespace chonk

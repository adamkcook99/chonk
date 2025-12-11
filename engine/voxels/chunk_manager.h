#pragma once

#include "chunk.h"
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
}

namespace chonk {
namespace game {
namespace world {
    class WorldGenerator;
}
}

namespace voxels {

// Priority queue entry for chunk loading
struct ChunkLoadRequest {
    ChunkCoord coord;
    float priority;  // Lower = higher priority (distance-based)
    
    bool operator<(const ChunkLoadRequest& other) const {
        return priority > other.priority;  // Min-heap
    }
};

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    
    // Initialize with world generator
    void initialize(class game::world::WorldGenerator* generator);
    
    // Update chunk loading/unloading based on player position
    void update(const math::Vec3& playerPos);
    
    // Get chunk (may return nullptr if not loaded)
    Chunk* getChunk(const ChunkCoord& coord);
    const Chunk* getChunk(const ChunkCoord& coord) const;
    
    // Get all non-air chunks for GPU rendering (18x18x18 region)
    std::vector<const Chunk*> getRenderChunks() const;
    
    // Get chunks in simulation distance (4x4x4 full-res center)
    // These are the chunks the server manages for physics/gameplay
    std::vector<Chunk*> getSimulationChunks();
    std::vector<const Chunk*> getSimulationChunks() const;
    
    // Get chunk coordinates that need to be requested from server
    // (render-only chunks outside simulation distance)
    std::vector<ChunkCoord> getRenderOnlyChunkCoords(const math::Vec3& playerPos) const;
    
    // Get visible chunks (frustum culled) for rendering
    std::vector<const Chunk*> getVisibleChunks(const math::Vec3& cameraPos, 
                                               const math::Vec3& cameraDir,
                                               const math::Vec3& cameraUp,
                                               float fov) const;
    
    // Add chunk from network (deserialized)
    void addChunkFromNetwork(const ChunkCoord& coord, const std::vector<uint8_t>& data);
    
    // Get all required chunks for a player position (18x18x18 jawbreaker pattern)
    std::vector<ChunkCoord> getRequiredChunks(const math::Vec3& playerPos) const;
    
    // Get simulation chunks only (4x4x4 full-res center)
    std::vector<ChunkCoord> getSimulationChunkCoords(const math::Vec3& playerPos) const;
    
    // Check if a chunk is within simulation distance
    bool isSimulationChunk(const ChunkCoord& coord, const math::Vec3& playerPos) const;
    
    // Get loading statistics
    size_t getLoadedChunkCount() const;
    size_t getPendingLoadCount() const;
    
    void shutdown();
    
    // Set chunk save directory
    void setChunkDirectory(const std::string& dir) { chunkDirectory = dir; }
    
    // Get current anchor position (corner of center 4x4x4)
    math::Vec3 getAnchorPosition() const { return anchorPos; }
    
private:
    // LOD system constants
    // 18x18x18 render grid with jawbreaker pattern:
    // - Center 4x4x4 at full resolution (16m chunks, 0.25m blocks)
    // - Each layer outward doubles in size
    // - Player stays in 2x2x2 center section; chunks shift when they leave
    static constexpr int32_t RENDER_CHUNKS_PER_AXIS = 18;
    static constexpr int32_t CENTER_FULL_RES_SIZE = 4;  // 4x4x4 full-res chunks
    static constexpr int32_t CENTER_PLAYER_SIZE = 2;    // 2x2x2 player movement zone
    static constexpr int32_t NUM_LOD_LAYERS = 8;        // 0.25m to 32m block sizes
    
    // Chunk storage
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> chunks;
    mutable std::mutex chunksMutex;
    
    // Generation
    game::world::WorldGenerator* worldGenerator;
    std::string chunkDirectory;
    
    // Async loading with priority queue
    std::thread loadThread;
    std::atomic<bool> running;
    std::priority_queue<ChunkLoadRequest> loadQueue;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingLoads;  // Track what's in queue
    mutable std::mutex loadQueueMutex;
    std::condition_variable loadCondition;
    
    void loadThreadFunc();
    void queueChunkLoad(const ChunkCoord& coord, float priority);
    void unloadDistantChunks(const math::Vec3& playerPos);
    ChunkCoord worldToChunkCoord(const math::Vec3& worldPos, ChunkResolution res) const;
    std::string getChunkFilename(const ChunkCoord& coord) const;
    
    // LOD helpers
    static float getChunkSizeForResolution(ChunkResolution res);
    static ChunkResolution getResolutionForLayer(int layer);
    static int getLayerForGridPosition(int pos);
    
    // Player position tracking
    math::Vec3 anchorPos;           // World position of corner of center 4x4x4
    ChunkCoord playerChunk;         // Player's current 16m chunk
    bool anchorInitialized = false;
    
    // Update anchor when player moves outside 2x2x2 center
    void updateAnchorIfNeeded(const math::Vec3& playerPos);
    
    // Calculate priority for loading (lower = higher priority)
    float calculateLoadPriority(const ChunkCoord& coord, const math::Vec3& playerPos) const;
};

} // namespace voxels
} // namespace chonk


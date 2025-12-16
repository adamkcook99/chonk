#include "test_framework.h"
#include "../engine/voxels/chunk.h"
#include "../engine/voxels/world_constants.h"
#include "../engine/math/vec3.h"
#include "../game/world/world_generator.h"
#include <fstream>
#include <filesystem>
#include <cstring>

using namespace chonk::voxels;
using namespace chonk::math;
using namespace chonk::game::world;

// Mock WorldGenerator for testing
class MockWorldGenerator : public WorldGenerator {
public:
    MockWorldGenerator() : WorldGenerator() {}
    
    uint8_t getMaterialAt(const chonk::math::Vec3& worldPos, float resolution) override {
        // Simple test pattern: material based on Y position (in meters)
        if (worldPos.y < 100.0f) return 3;   // Stone
        if (worldPos.y < 200.0f) return 2;   // Dirt
        if (worldPos.y < 250.0f) return 1;   // Grass
        return 0; // Air
    }
};

void runChunkTests() {
    std::cout << "\n=== Running Chunk Tests ===\n" << std::endl;
    
    // Test: Default constructor
    TEST("Chunk: Default constructor") {
        Chunk chunk;
        
        ASSERT_FALSE(chunk.isLoaded());
        
        ChunkCoord coord = chunk.getCoord();
        ASSERT_EQ(coord.x, 0);
        ASSERT_EQ(coord.y, 0);
        ASSERT_EQ(coord.z, 0);
        return true;
    });
    
    // Test: Constructor with coordinate
    TEST("Chunk: Constructor with coordinate") {
        ChunkCoord coord(5, 10, -3);
        Chunk chunk(coord);
        
        ASSERT_EQ(chunk.getCoord().x, 5);
        ASSERT_EQ(chunk.getCoord().y, 10);
        ASSERT_EQ(chunk.getCoord().z, -3);
        
        ASSERT_FALSE(chunk.isLoaded());
        return true;
    });
    
    // Test: Chunk size is 256m
    TEST("Chunk: Size is 256m") {
        ASSERT_FLOAT_EQ(Chunk::getSize(), 256.0f, 0.001f);
        ASSERT_FLOAT_EQ(CHUNK_SIZE_METERS, 256.0f, 0.001f);
        return true;
    });
    
    // Test: World position calculation
    TEST("Chunk: World position calculation") {
        ChunkCoord coord(2, 3, -1);
        Chunk chunk(coord);
        
        Vec3 worldPos = chunk.getWorldPosition();
        
        ASSERT_FLOAT_EQ(worldPos.x, 2.0f * CHUNK_SIZE_METERS, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.y, 3.0f * CHUNK_SIZE_METERS, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.z, -1.0f * CHUNK_SIZE_METERS, 0.001f);
        return true;
    });
    
    // Test: AABB calculation
    TEST("Chunk: AABB calculation") {
        ChunkCoord coord(1, 2, 3);
        Chunk chunk(coord);
        
        const chonk::math::AABB& aabb = chunk.getAABB();
        Vec3 worldPos = chunk.getWorldPosition();
        float size = Chunk::getSize();
        
        ASSERT_FLOAT_EQ(aabb.min.x, worldPos.x, 0.001f);
        ASSERT_FLOAT_EQ(aabb.min.y, worldPos.y, 0.001f);
        ASSERT_FLOAT_EQ(aabb.min.z, worldPos.z, 0.001f);
        
        ASSERT_FLOAT_EQ(aabb.max.x, worldPos.x + size, 0.001f);
        ASSERT_FLOAT_EQ(aabb.max.y, worldPos.y + size, 0.001f);
        ASSERT_FLOAT_EQ(aabb.max.z, worldPos.z + size, 0.001f);
        return true;
    });
    
    // Test: Loaded setter/getter
    TEST("Chunk: Loaded setter/getter") {
        Chunk chunk;
        
        ASSERT_FALSE(chunk.isLoaded());
        
        chunk.setLoaded(true);
        ASSERT_TRUE(chunk.isLoaded());
        
        chunk.setLoaded(false);
        ASSERT_FALSE(chunk.isLoaded());
        return true;
    });
    
    // Test: Generate chunk
    TEST("Chunk: Generate chunk") {
        ChunkCoord coord(0, 0, 0);
        Chunk chunk(coord);
        MockWorldGenerator generator;
        
        chunk.generate(&generator);
        
        ASSERT_TRUE(chunk.isLoaded());
        
        // Check that SVO was built
        const SVO& svo = chunk.getSVO();
        ASSERT_TRUE(svo.getNodeCount() > 0);
        return true;
    });
    
    // Test: Generate with null generator
    TEST("Chunk: Generate with null generator") {
        Chunk chunk;
        
        chunk.generate(nullptr);
        
        // Should not crash, chunk should not be loaded
        ASSERT_FALSE(chunk.isLoaded());
        return true;
    });
    
    // Test: ChunkCoord operators
    TEST("Chunk: ChunkCoord operators") {
        ChunkCoord coord1(1, 2, 3);
        ChunkCoord coord2(1, 2, 3);
        ChunkCoord coord3(4, 5, 6);
        
        ASSERT_TRUE(coord1 == coord2);
        ASSERT_FALSE(coord1 == coord3);
        ASSERT_TRUE(coord1 != coord3);
        ASSERT_FALSE(coord1 != coord2);
        
        // Test ordering
        ChunkCoord coord4(0, 0, 0);
        ChunkCoord coord5(1, 0, 0);
        ASSERT_TRUE(coord4 < coord5);
        return true;
    });
    
    // Test: ChunkCoord toString
    TEST("Chunk: ChunkCoord toString") {
        ChunkCoord coord(5, -10, 3);
        std::string str = coord.toString();
        
        ASSERT_TRUE(str.find("5") != std::string::npos);
        ASSERT_TRUE(str.find("-10") != std::string::npos);
        ASSERT_TRUE(str.find("3") != std::string::npos);
        return true;
    });
    
    // Test: ChunkCoord from world position
    TEST("Chunk: ChunkCoord from world position") {
        Vec3 worldPos(300.0f, 600.0f, -100.0f);
        ChunkCoord coord = ChunkCoord::fromWorldPos(worldPos);
        
        // 300 / 256 = 1.17... -> floor = 1
        // 600 / 256 = 2.34... -> floor = 2
        // -100 / 256 = -0.39... -> floor = -1
        ASSERT_EQ(coord.x, 1);
        ASSERT_EQ(coord.y, 2);
        ASSERT_EQ(coord.z, -1);
        return true;
    });
    
    // Test: ChunkCoord to world position
    TEST("Chunk: ChunkCoord to world position") {
        ChunkCoord coord(2, 3, -1);
        Vec3 worldPos = coord.toWorldPos();
        
        ASSERT_FLOAT_EQ(worldPos.x, 512.0f, 0.001f);  // 2 * 256
        ASSERT_FLOAT_EQ(worldPos.y, 768.0f, 0.001f);  // 3 * 256
        ASSERT_FLOAT_EQ(worldPos.z, -256.0f, 0.001f); // -1 * 256
        return true;
    });
    
    // Test: SuperChunkCoord basics
    TEST("Chunk: SuperChunkCoord basics") {
        SuperChunkCoord coord(1, 2, 3);
        
        ASSERT_EQ(coord.x, 1);
        ASSERT_EQ(coord.y, 2);
        ASSERT_EQ(coord.z, 3);
        
        // Super chunk is 2048m (8 * 256m chunks)
        Vec3 worldPos = coord.toWorldPos();
        ASSERT_FLOAT_EQ(worldPos.x, 2048.0f, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.y, 4096.0f, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.z, 6144.0f, 0.001f);
        return true;
    });
    
    // Test: SuperChunkCoord contains chunk
    TEST("Chunk: SuperChunkCoord contains chunk") {
        SuperChunkCoord super(0, 0, 0);
        
        // Super chunk 0,0,0 contains chunks 0-7 in each dimension
        ChunkCoord inside(3, 5, 7);
        ChunkCoord outside(8, 0, 0);
        ChunkCoord negative(-1, 0, 0);
        
        ASSERT_TRUE(super.containsChunk(inside));
        ASSERT_FALSE(super.containsChunk(outside));
        ASSERT_FALSE(super.containsChunk(negative));
        return true;
    });
    
    // Test: World constants
    TEST("Chunk: World constants") {
        ASSERT_FLOAT_EQ(BLOCK_SIZE_METERS, 0.25f, 0.001f);
        ASSERT_FLOAT_EQ(CHUNK_SIZE_METERS, 256.0f, 0.001f);
        ASSERT_FLOAT_EQ(SUPER_CHUNK_SIZE_METERS, 2048.0f, 0.001f);
        ASSERT_EQ(CHUNK_SVO_DEPTH, 8u);
        ASSERT_EQ(CHUNKS_PER_SUPER_AXIS, 8u);
        ASSERT_EQ(CHUNKS_PER_SUPER, 512u);
        return true;
    });
    
    // Test: LOD depth from distance
    TEST("Chunk: LOD depth from distance") {
        // Close distance should give full depth
        ASSERT_EQ(getMaxDepthForDistance(100.0f), 8u);
        
        // Far distance should give lower depth
        ASSERT_TRUE(getMaxDepthForDistance(5000.0f) < 8u);
        
        // Very far should give minimal depth
        ASSERT_TRUE(getMaxDepthForDistance(10000.0f) <= 2u);
        return true;
    });
    
    // Test: SVO access
    TEST("Chunk: SVO access") {
        Chunk chunk;
        MockWorldGenerator generator;
        
        chunk.generate(&generator);
        
        // Test const access
        const SVO& constSvo = chunk.getSVO();
        ASSERT_TRUE(constSvo.getNodeCount() > 0);
        
        // Test non-const access
        SVO& svo = chunk.getSVO();
        svo.clear();
        ASSERT_EQ(svo.getNodeCount(), 0u);
        return true;
    });
    
    // Test: Is air check
    TEST("Chunk: Is air check") {
        // Create a chunk that should be all air (high in the sky)
        ChunkCoord coord(0, 100, 0);  // y=100 chunks = 25600m, way above grass
        Chunk chunk(coord);
        MockWorldGenerator generator;
        
        chunk.generate(&generator);
        
        // With our mock generator, anything above y=250m is air
        // This chunk starts at y=25600m, so should be all air
        ASSERT_TRUE(chunk.isAir());
        return true;
    });
}

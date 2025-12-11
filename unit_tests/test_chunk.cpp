#include "test_framework.h"
#include "../engine/voxels/chunk.h"
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
    
    uint8_t getMaterialAt(const chonk::math::Vec3& worldPos, float resolution) {
        // Simple test pattern: material based on Y position
        if (worldPos.y < 5.0f) return 3; // Stone
        if (worldPos.y < 10.0f) return 2; // Dirt
        if (worldPos.y < 15.0f) return 1; // Grass
        return 0; // Air
    }
};

void runChunkTests() {
    std::cout << "\n=== Running Chunk Tests ===\n" << std::endl;
    
    // Test: Default constructor
    TEST("Chunk: Default constructor") {
        Chunk chunk;
        
        ASSERT_FALSE(chunk.isLoaded());
        ASSERT_FALSE(chunk.isVisible());
        
        ChunkCoord coord = chunk.getCoord();
        ASSERT_EQ(coord.x, 0);
        ASSERT_EQ(coord.y, 0);
        ASSERT_EQ(coord.z, 0);
        ASSERT_EQ(coord.resolution, ChunkResolution::RES_0_25M);
        return true;
    });
    
    // Test: Constructor with coordinate
    TEST("Chunk: Constructor with coordinate") {
        ChunkCoord coord(5, 10, -3, ChunkResolution::RES_1M);
        Chunk chunk(coord);
        
        ASSERT_EQ(chunk.getCoord().x, 5);
        ASSERT_EQ(chunk.getCoord().y, 10);
        ASSERT_EQ(chunk.getCoord().z, -3);
        ASSERT_EQ(chunk.getCoord().resolution, ChunkResolution::RES_1M);
        
        ASSERT_FALSE(chunk.isLoaded());
        ASSERT_FALSE(chunk.isVisible());
        return true;
    });
    
    // Test: Resolution calculations
    TEST("Chunk: Resolution calculations") {
        // Test all 8 resolution levels (jawbreaker LOD pattern)
        // Each chunk is 64x64x64 blocks, resolution determines block size
        
        Chunk chunk0(ChunkCoord(0, 0, 0, ChunkResolution::RES_0_25M));
        ASSERT_FLOAT_EQ(chunk0.getResolution(), 0.25f, 0.001f);
        ASSERT_FLOAT_EQ(chunk0.getSize(), 16.0f, 0.001f);  // 64 * 0.25m
        
        Chunk chunk1(ChunkCoord(0, 0, 0, ChunkResolution::RES_0_5M));
        ASSERT_FLOAT_EQ(chunk1.getResolution(), 0.5f, 0.001f);
        ASSERT_FLOAT_EQ(chunk1.getSize(), 32.0f, 0.001f);  // 64 * 0.5m
        
        Chunk chunk2(ChunkCoord(0, 0, 0, ChunkResolution::RES_1M));
        ASSERT_FLOAT_EQ(chunk2.getResolution(), 1.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk2.getSize(), 64.0f, 0.001f);  // 64 * 1m
        
        Chunk chunk3(ChunkCoord(0, 0, 0, ChunkResolution::RES_2M));
        ASSERT_FLOAT_EQ(chunk3.getResolution(), 2.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk3.getSize(), 128.0f, 0.001f); // 64 * 2m
        
        Chunk chunk4(ChunkCoord(0, 0, 0, ChunkResolution::RES_4M));
        ASSERT_FLOAT_EQ(chunk4.getResolution(), 4.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk4.getSize(), 256.0f, 0.001f); // 64 * 4m
        
        Chunk chunk5(ChunkCoord(0, 0, 0, ChunkResolution::RES_8M));
        ASSERT_FLOAT_EQ(chunk5.getResolution(), 8.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk5.getSize(), 512.0f, 0.001f); // 64 * 8m
        
        Chunk chunk6(ChunkCoord(0, 0, 0, ChunkResolution::RES_16M));
        ASSERT_FLOAT_EQ(chunk6.getResolution(), 16.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk6.getSize(), 1024.0f, 0.001f); // 64 * 16m
        
        Chunk chunk7(ChunkCoord(0, 0, 0, ChunkResolution::RES_32M));
        ASSERT_FLOAT_EQ(chunk7.getResolution(), 32.0f, 0.001f);
        ASSERT_FLOAT_EQ(chunk7.getSize(), 2048.0f, 0.001f); // 64 * 32m (largest)
        return true;
    });
    
    // Test: World position calculation
    TEST("Chunk: World position calculation") {
        ChunkCoord coord(2, 3, -1, ChunkResolution::RES_1M);
        Chunk chunk(coord);
        
        Vec3 worldPos = chunk.getWorldPosition();
        float expectedSize = 64.0f;
        
        ASSERT_FLOAT_EQ(worldPos.x, 2.0f * expectedSize, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.y, 3.0f * expectedSize, 0.001f);
        ASSERT_FLOAT_EQ(worldPos.z, -1.0f * expectedSize, 0.001f);
        return true;
    });
    
    // Test: AABB calculation
    TEST("Chunk: AABB calculation") {
        ChunkCoord coord(1, 2, 3, ChunkResolution::RES_0_25M);
        Chunk chunk(coord);
        
        const chonk::math::AABB& aabb = chunk.getAABB();
        Vec3 worldPos = chunk.getWorldPosition();
        float size = chunk.getSize();
        
        ASSERT_FLOAT_EQ(aabb.min.x, worldPos.x, 0.001f);
        ASSERT_FLOAT_EQ(aabb.min.y, worldPos.y, 0.001f);
        ASSERT_FLOAT_EQ(aabb.min.z, worldPos.z, 0.001f);
        
        ASSERT_FLOAT_EQ(aabb.max.x, worldPos.x + size, 0.001f);
        ASSERT_FLOAT_EQ(aabb.max.y, worldPos.y + size, 0.001f);
        ASSERT_FLOAT_EQ(aabb.max.z, worldPos.z + size, 0.001f);
        return true;
    });
    
    // Test: Visibility setter/getter
    TEST("Chunk: Visibility setter/getter") {
        Chunk chunk;
        
        ASSERT_FALSE(chunk.isVisible());
        
        chunk.setVisible(true);
        ASSERT_TRUE(chunk.isVisible());
        
        chunk.setVisible(false);
        ASSERT_FALSE(chunk.isVisible());
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
    
    // Test: Generate chunk (RES_0_25M)
    TEST("Chunk: Generate chunk (RES_0_25M)") {
        ChunkCoord coord(0, 0, 0, ChunkResolution::RES_0_25M);
        Chunk chunk(coord);
        MockWorldGenerator generator;
        
        chunk.generate(&generator);
        
        ASSERT_TRUE(chunk.isLoaded());
        
        // Check that SVO was built
        const SVO& svo = chunk.getSVO();
        ASSERT_TRUE(svo.getNodeCount() > 0);
        
        // Check materials at different Y levels using world meters helper
        auto matAtWorld = [&](float xm, float ym, float zm) {
            Vec3 wp = chunk.getWorldPosition() + Vec3(xm, ym, zm);
            return chunk.getMaterialAtWorld(wp);
        };
        
        // Should have stone at low Y
        uint8_t material = matAtWorld(8.0f, 2.0f, 8.0f);
        ASSERT_EQ(material, 3);
        
        // Should have dirt at mid Y
        material = matAtWorld(8.0f, 7.0f, 8.0f);
        ASSERT_EQ(material, 2);
        
        // Should have grass at higher Y
        material = matAtWorld(8.0f, 12.0f, 8.0f);
        ASSERT_EQ(material, 1);
        
        // Should have air at very high Y
        material = matAtWorld(8.0f, 20.0f, 8.0f);
        ASSERT_EQ(material, 0);
        return true;
    });
    
    // Test: Generate chunk (lower resolution)
    TEST("Chunk: Generate chunk (lower resolution)") {
        ChunkCoord coord(0, 0, 0, ChunkResolution::RES_1M);
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
        
        // Should not crash, but chunk may not be loaded
        // (implementation dependent)
        return true;
    });
    
    // Test: Save to file
    TEST("Chunk: Save to file") {
        ChunkCoord coord(5, 10, -3, ChunkResolution::RES_0_25M);
        Chunk chunk(coord);
        MockWorldGenerator generator;
        
        chunk.generate(&generator);
        
        std::string filename = "test_chunk_save.bin";
        bool saved = chunk.saveToFile(filename);
        
        ASSERT_TRUE(saved);
        
        // Verify file exists and has content
        std::ifstream file(filename, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        
        // Read header
        ChunkCoord fileCoord;
        file.read(reinterpret_cast<char*>(&fileCoord), sizeof(ChunkCoord));
        ASSERT_EQ(fileCoord.x, 5);
        ASSERT_EQ(fileCoord.y, 10);
        ASSERT_EQ(fileCoord.z, -3);
        
        // Read node count
        uint32_t nodeCount;
        file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
        ASSERT_TRUE(nodeCount > 0);
        
        file.close();
        
        // Clean up
        std::remove(filename.c_str());
        return true;
    });
    
    // Test: Load from file
    TEST("Chunk: Load from file") {
        // First create and save a chunk
        ChunkCoord coord(7, 8, 9, ChunkResolution::RES_0_25M);
        Chunk chunk1(coord);
        MockWorldGenerator generator;
        chunk1.generate(&generator);
        
        std::string filename = "test_chunk_load.bin";
        ASSERT_TRUE(chunk1.saveToFile(filename));
        
        // Now load it into a new chunk
        Chunk chunk2(coord);
        bool loaded = chunk2.loadFromFile(filename);
        
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(chunk2.isLoaded());
        
        // Verify SVO was loaded correctly
        const SVO& svo1 = chunk1.getSVO();
        const SVO& svo2 = chunk2.getSVO();
        
        ASSERT_EQ(svo1.getNodeCount(), svo2.getNodeCount());
        
        // Verify materials match
        float resolution = chunk1.getResolution();
        uint8_t mat1 = svo1.getMaterial(8.0f, 8.0f, 8.0f, resolution);
        uint8_t mat2 = svo2.getMaterial(8.0f, 8.0f, 8.0f, resolution);
        ASSERT_EQ(mat1, mat2);
        
        // Clean up
        std::remove(filename.c_str());
        return true;
    });
    
    // Test: Load from non-existent file
    TEST("Chunk: Load from non-existent file") {
        Chunk chunk;
        bool loaded = chunk.loadFromFile("nonexistent_file.bin");
        
        ASSERT_FALSE(loaded);
        ASSERT_FALSE(chunk.isLoaded());
        return true;
    });
    
    // Test: Load from file with wrong coordinate
    TEST("Chunk: Load from file with wrong coordinate") {
        // Create and save chunk
        ChunkCoord coord1(1, 2, 3, ChunkResolution::RES_0_25M);
        Chunk chunk1(coord1);
        MockWorldGenerator generator;
        chunk1.generate(&generator);
        
        std::string filename = "test_chunk_wrong_coord.bin";
        ASSERT_TRUE(chunk1.saveToFile(filename));
        
        // Try to load with different coordinate
        ChunkCoord coord2(4, 5, 6, ChunkResolution::RES_0_25M);
        Chunk chunk2(coord2);
        bool loaded = chunk2.loadFromFile(filename);
        
        ASSERT_FALSE(loaded);
        
        // Clean up
        std::remove(filename.c_str());
        return true;
    });
    
    // Test: Save/load empty chunk
    TEST("Chunk: Save/load empty chunk") {
        // Create empty chunk (all air)
        ChunkCoord coord(0, 0, 0, ChunkResolution::RES_0_25M);
        
        // Create chunk with all-air SVO
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        Chunk chunk1(coord);
        chunk1.getSVO().buildFromDense(voxels.data(), SIZE);
        chunk1.setLoaded(true);
        
        std::string filename = "test_chunk_empty.bin";
        ASSERT_TRUE(chunk1.saveToFile(filename));
        
        Chunk chunk2(coord);
        bool loaded = chunk2.loadFromFile(filename);
        
        ASSERT_TRUE(loaded);
        ASSERT_TRUE(chunk2.isLoaded());
        
        // Clean up
        std::remove(filename.c_str());
        return true;
    });
    
    // Test: Multiple chunks with different coordinates
    TEST("Chunk: Multiple chunks with different coordinates") {
        ChunkCoord coord1(0, 0, 0, ChunkResolution::RES_0_25M);
        ChunkCoord coord2(1, 0, 0, ChunkResolution::RES_0_25M);
        ChunkCoord coord3(0, 1, 0, ChunkResolution::RES_0_25M);
        
        Chunk chunk1(coord1);
        Chunk chunk2(coord2);
        Chunk chunk3(coord3);
        
        ASSERT_EQ(chunk1.getCoord().x, 0);
        ASSERT_EQ(chunk2.getCoord().x, 1);
        ASSERT_EQ(chunk3.getCoord().y, 1);
        
        Vec3 pos1 = chunk1.getWorldPosition();
        Vec3 pos2 = chunk2.getWorldPosition();
        Vec3 pos3 = chunk3.getWorldPosition();
        
        float size = chunk1.getSize();
        ASSERT_FLOAT_EQ(pos2.x - pos1.x, size, 0.001f);
        ASSERT_FLOAT_EQ(pos3.y - pos1.y, size, 0.001f);
        return true;
    });
    
    // Test: ChunkCoord operators
    TEST("Chunk: ChunkCoord operators") {
        ChunkCoord coord1(1, 2, 3, ChunkResolution::RES_0_25M);
        ChunkCoord coord2(1, 2, 3, ChunkResolution::RES_0_25M);
        ChunkCoord coord3(4, 5, 6, ChunkResolution::RES_0_25M);
        
        ASSERT_TRUE(coord1 == coord2);
        ASSERT_FALSE(coord1 == coord3);
        ASSERT_TRUE(coord1 != coord3);
        ASSERT_FALSE(coord1 != coord2);
        
        // Test ordering
        ChunkCoord coord4(0, 0, 0, ChunkResolution::RES_0_25M);
        ChunkCoord coord5(1, 0, 0, ChunkResolution::RES_0_25M);
        ASSERT_TRUE(coord4 < coord5);
        
        // Test resolution ordering
        ChunkCoord coord6(0, 0, 0, ChunkResolution::RES_0_25M);
        ChunkCoord coord7(0, 0, 0, ChunkResolution::RES_1M);
        ASSERT_TRUE(coord6 < coord7);
        return true;
    });
    
    // Test: ChunkCoord toString
    TEST("Chunk: ChunkCoord toString") {
        ChunkCoord coord(5, -10, 3, ChunkResolution::RES_1M);
        std::string str = coord.toString();
        
        ASSERT_TRUE(str.find("5") != std::string::npos);
        ASSERT_TRUE(str.find("-10") != std::string::npos);
        ASSERT_TRUE(str.find("3") != std::string::npos);
        ASSERT_TRUE(str.find("2") != std::string::npos); // Resolution enum value (RES_1M = 2)
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
        ASSERT_EQ(svo.getNodeCount(), 0);
        return true;
    });
    
    // Test: AABB update after generation
    TEST("Chunk: AABB update after generation") {
        ChunkCoord coord(2, 3, 4, ChunkResolution::RES_1M);
        Chunk chunk(coord);
        
        const chonk::math::AABB& aabb1 = chunk.getAABB();
        Vec3 pos1 = chunk.getWorldPosition();
        
        MockWorldGenerator generator;
        chunk.generate(&generator);
        
        const chonk::math::AABB& aabb2 = chunk.getAABB();
        Vec3 pos2 = chunk.getWorldPosition();
        
        // AABB should remain the same (based on coordinate, not generation)
        ASSERT_FLOAT_EQ(aabb1.min.x, aabb2.min.x, 0.001f);
        ASSERT_FLOAT_EQ(aabb1.min.y, aabb2.min.y, 0.001f);
        ASSERT_FLOAT_EQ(aabb1.min.z, aabb2.min.z, 0.001f);
        return true;
    });
}

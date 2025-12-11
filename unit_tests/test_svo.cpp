#include "test_framework.h"
#include "../engine/voxels/svo.h"
#include "../engine/math/vec3.h"
#include <vector>
#include <cstring>
#include <random>

using namespace chonk::voxels;
using namespace chonk::math;

// Helper: interpret meters with a given resolution as voxel indices (0..63)
static inline uint8_t matAtMeters(const SVO& svo, float xm, float ym, float zm, float res) {
    uint32_t vx = static_cast<uint32_t>(std::floor(xm / res));
    uint32_t vy = static_cast<uint32_t>(std::floor(ym / res));
    uint32_t vz = static_cast<uint32_t>(std::floor(zm / res));
    return svo.getMaterial(vx, vy, vz, 0.0f);
}

void runSVOTests() {
    std::cout << "\n=== Running SVO Tests ===\n" << std::endl;
    
    // Test: Empty SVO construction
    TEST("SVO: Empty construction") {
        SVO svo;
        ASSERT_TRUE(svo.getNodeCount() == 0 || svo.getNodeCount() == 1);
        ASSERT_TRUE(svo.isCollapsed());
        ASSERT_EQ(svo.getRootMaterial(), 0);
        return true;
    });
    
    // Test: Building from all-air voxel data
    TEST("SVO: Build from all-air data") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0); // All air
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Should collapse to single air node
        ASSERT_TRUE(svo.isCollapsed());
        ASSERT_EQ(svo.getRootMaterial(), 0);
        ASSERT_TRUE(svo.getNodeCount() >= 1);
        return true;
    });
    
    // Test: Building from all-solid voxel data
    TEST("SVO: Build from all-solid data") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 1); // All material 1
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Should collapse to single solid node
        ASSERT_TRUE(svo.isCollapsed());
        ASSERT_EQ(svo.getRootMaterial(), 1);
        return true;
    });
    
    // Test: Building from single voxel
    TEST("SVO: Build from single voxel") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Set single voxel at center
        uint32_t centerX = CHUNK_SIZE / 2;
        uint32_t centerY = CHUNK_SIZE / 2;
        uint32_t centerZ = CHUNK_SIZE / 2;
        uint32_t idx = centerZ * CHUNK_SIZE * CHUNK_SIZE + centerY * CHUNK_SIZE + centerX;
        voxels[idx] = 2; // Material 2
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Should not be collapsed (has structure)
        ASSERT_FALSE(svo.isCollapsed());
        
        // Check material at that position (use voxel indices directly)
        float resolution = 0.25f;
        uint8_t material = svo.getMaterial(centerX, centerY, centerZ, 0.0f);
        ASSERT_EQ(material, 2);
        
        // Check surrounding air
        material = svo.getMaterial(0, 0, 0, 0.0f);
        ASSERT_EQ(material, 0);
        return true;
    });
    
    // Test: Building from corner voxels
    TEST("SVO: Build from corner voxels") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Set all 8 corners
        uint32_t corners[8][3] = {
            {0, 0, 0}, {CHUNK_SIZE-1, 0, 0},
            {0, CHUNK_SIZE-1, 0}, {CHUNK_SIZE-1, CHUNK_SIZE-1, 0},
            {0, 0, CHUNK_SIZE-1}, {CHUNK_SIZE-1, 0, CHUNK_SIZE-1},
            {0, CHUNK_SIZE-1, CHUNK_SIZE-1}, {CHUNK_SIZE-1, CHUNK_SIZE-1, CHUNK_SIZE-1}
        };
        
        for (int i = 0; i < 8; ++i) {
            uint32_t x = corners[i][0];
            uint32_t y = corners[i][1];
            uint32_t z = corners[i][2];
            uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
            voxels[idx] = 3; // Material 3
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Check all corners using voxel indices
        for (int i = 0; i < 8; ++i) {
            uint8_t material = svo.getMaterial(corners[i][0], corners[i][1], corners[i][2], 0.0f);
            ASSERT_EQ(material, 3);
        }
        
        // Check center (should be air)
        uint8_t material = svo.getMaterial(CHUNK_SIZE / 2.0f, CHUNK_SIZE / 2.0f, CHUNK_SIZE / 2.0f, 0.0f);
        ASSERT_EQ(material, 0);
        return true;
    });
    
    // Test: Building from layered structure
    TEST("SVO: Build from layered structure") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Create layers: bottom 10 = stone(3), next 5 = dirt(2), top 5 = grass(1)
        for (uint32_t y = 0; y < 10; ++y) {
            for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
                for (uint32_t x = 0; x < CHUNK_SIZE; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 3; // Stone
                }
            }
        }
        for (uint32_t y = 10; y < 15; ++y) {
            for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
                for (uint32_t x = 0; x < CHUNK_SIZE; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 2; // Dirt
                }
            }
        }
        for (uint32_t y = 15; y < 20; ++y) {
            for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
                for (uint32_t x = 0; x < CHUNK_SIZE; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 1; // Grass
                }
            }
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Use voxel indices directly
        // Check stone layer
        uint8_t material = svo.getMaterial(8, 2, 8, 0.0f);
        ASSERT_EQ(material, 3);
        
        // Check dirt layer
        material = svo.getMaterial(8, 12, 8, 0.0f);
        ASSERT_EQ(material, 2);
        
        // Check grass layer
        material = svo.getMaterial(8, 17, 8, 0.0f);
        ASSERT_EQ(material, 1);
        
        // Check air above
        material = svo.getMaterial(8, 25, 8, 0.0f);
        ASSERT_EQ(material, 0);
        return true;
    });
    
    // Test: Invalid size handling
    TEST("SVO: Invalid size handling") {
        std::vector<uint8_t> voxels(100, 1); // Wrong size
        
        SVO svo;
        svo.buildFromDense(voxels.data(), 100);
        
        // Should create empty/air SVO
        ASSERT_TRUE(svo.isCollapsed());
        ASSERT_EQ(svo.getRootMaterial(), 0);
        return true;
    });
    
    // Test: getMaterial at various positions
    TEST("SVO: getMaterial at various positions") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Create a small cube of material
        for (uint32_t z = 20; z < 30; ++z) {
            for (uint32_t y = 20; y < 30; ++y) {
                for (uint32_t x = 20; x < 30; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 5;
                }
            }
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        float resolution = 0.25f;
        
        // Inside cube
        uint8_t material = matAtMeters(svo, 25.0f * resolution, 25.0f * resolution, 25.0f * resolution, resolution);
        ASSERT_EQ(material, 5);
        
        // Outside cube
        material = matAtMeters(svo, 10.0f * resolution, 10.0f * resolution, 10.0f * resolution, resolution);
        ASSERT_EQ(material, 0);
        
        // Edge of cube
        material = matAtMeters(svo, 20.0f * resolution, 20.0f * resolution, 20.0f * resolution, resolution);
        ASSERT_EQ(material, 5);
        
        // Just outside cube
        material = matAtMeters(svo, 19.0f * resolution, 20.0f * resolution, 20.0f * resolution, resolution);
        ASSERT_EQ(material, 0);
        return true;
    });
    
    // Test: Ray intersection - hit solid
    TEST("SVO: Ray intersection - hit solid") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Create a cube at center
        uint32_t center = CHUNK_SIZE / 2;
        for (uint32_t z = center - 5; z < center + 5; ++z) {
            for (uint32_t y = center - 5; y < center + 5; ++y) {
                for (uint32_t x = center - 5; x < center + 5; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 7;
                }
            }
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        float chunkSize = 16.0f;
        
        // Ray from outside pointing at center (meters)
        Vec3 origin(0, 0, 0);
        Vec3 dir = Vec3(chunkSize/2, chunkSize/2, chunkSize/2).normalize();
        
        float t;
        uint8_t material;
        bool hit = svo.intersectRay(origin, dir, t, material, 0.0f);
        
        ASSERT_TRUE(hit);
        ASSERT_EQ(material, 7);
        ASSERT_TRUE(t > 0);
        return true;
    });
    
    // Test: Ray intersection - miss (all air)
    TEST("SVO: Ray intersection - miss (all air)") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0); // All air
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        float resolution = 0.25f;
        Vec3 origin(0, 0, 0);
        Vec3 dir(1, 1, 1);
        dir = dir.normalize();
        
        float t;
        uint8_t material;
        bool hit = svo.intersectRay(origin, dir, t, material, resolution);
        
        ASSERT_FALSE(hit);
        return true;
    });
    
    // Test: Ray intersection - ray behind origin
    TEST("SVO: Ray intersection - ray behind origin") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 1); // All solid
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        float chunkSize = 16.0f;
        
        // Ray from center pointing away (meters)
        Vec3 origin(chunkSize/2, chunkSize/2, chunkSize/2);
        Vec3 dir(1, 1, 1);
        dir = dir.normalize();
        
        float t;
        uint8_t material;
        bool hit = svo.intersectRay(origin, dir, t, material, 0.0f);
        
        // Should not hit because ray goes away from chunk
        ASSERT_FALSE(hit);
        return true;
    });
    
    // Test: Clear functionality
    TEST("SVO: Clear functionality") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 1);
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        ASSERT_TRUE(svo.getNodeCount() > 0);
        ASSERT_TRUE(svo.isCollapsed() || svo.getRootMaterial() == 1);
        
        svo.clear();
        
        ASSERT_TRUE(svo.getNodeCount() == 0 || svo.isCollapsed());
        return true;
    });
    
    // Test: Load nodes
    TEST("SVO: Load nodes") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 2);
        
        SVO svo1;
        svo1.buildFromDense(voxels.data(), SIZE);
        
        // Get nodes from first SVO
        std::vector<SVONode> nodes = svo1.getNodes();
        
        // Create new SVO and load nodes
        SVO svo2;
        svo2.loadNodes(nodes);
        
        ASSERT_EQ(svo2.getNodeCount(), svo1.getNodeCount());
        
        // Check material consistency (voxel indices)
        uint8_t mat1 = svo1.getMaterial(8, 8, 8, 0.0f);
        uint8_t mat2 = svo2.getMaterial(8, 8, 8, 0.0f);
        ASSERT_EQ(mat1, mat2);
        return true;
    });
    
    // Test: Multiple materials
    TEST("SVO: Multiple materials") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Create regions with different materials
        // Left half = material 1, right half = material 2
        for (uint32_t z = 0; z < CHUNK_SIZE; ++z) {
            for (uint32_t y = 0; y < CHUNK_SIZE; ++y) {
                for (uint32_t x = 0; x < CHUNK_SIZE / 2; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 1;
                }
                for (uint32_t x = CHUNK_SIZE / 2; x < CHUNK_SIZE; ++x) {
                    uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
                    voxels[idx] = 2;
                }
            }
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Check left side
        uint8_t material = svo.getMaterial(2, 8, 8, 0.0f);
        ASSERT_EQ(material, 1);
        
        // Check right side
        material = svo.getMaterial(50, 8, 8, 0.0f); // use right half (x >= 32)
        ASSERT_EQ(material, 2);
        return true;
    });
    
    // Test: Boundary conditions
    TEST("SVO: Boundary conditions") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Set boundary voxels
        for (uint32_t i = 0; i < CHUNK_SIZE; ++i) {
            // X boundaries
            voxels[i * CHUNK_SIZE * CHUNK_SIZE + 0 * CHUNK_SIZE + 0] = 1; // (0,0,i)
            voxels[i * CHUNK_SIZE * CHUNK_SIZE + 0 * CHUNK_SIZE + (CHUNK_SIZE-1)] = 1; // (63,0,i)
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Check boundary positions using voxel indices
        uint8_t material = svo.getMaterial(0, 0, 0, 0.0f);
        ASSERT_EQ(material, 1);
        
        material = svo.getMaterial((CHUNK_SIZE-1), 0, 0, 0.0f);
        ASSERT_EQ(material, 1);
        return true;
    });
    
    // Test: Random voxel pattern
    TEST("SVO: Random voxel pattern") {
        constexpr uint32_t CHUNK_SIZE = 64;
        constexpr uint32_t SIZE = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
        std::vector<uint8_t> voxels(SIZE, 0);
        
        // Fill with random pattern
        std::mt19937 rng(42); // Fixed seed for reproducibility
        std::uniform_int_distribution<uint8_t> dist(0, 5);
        
        for (uint32_t i = 0; i < SIZE; ++i) {
            voxels[i] = dist(rng);
        }
        
        SVO svo;
        svo.buildFromDense(voxels.data(), SIZE);
        
        // Verify random samples
        std::uniform_int_distribution<uint32_t> posDist(0, CHUNK_SIZE - 1);
        
        for (int i = 0; i < 100; ++i) {
            uint32_t x = posDist(rng);
            uint32_t y = posDist(rng);
            uint32_t z = posDist(rng);
            uint32_t idx = z * CHUNK_SIZE * CHUNK_SIZE + y * CHUNK_SIZE + x;
            uint8_t expected = voxels[idx];
            
            uint8_t actual = svo.getMaterial(x, y, z, 0.0f);
            
            ASSERT_EQ(actual, expected);
        }
        return true;
    });
}

// Unit tests for Chunk and ChunkArray
#include "../engine/voxels/chunk.h"
#include "../engine/voxels/chunk_array.h"
#include "../engine/voxels/tree27.h"
#include "../engine/voxels/world_constants.h"
#include "../engine/math/vec3.h"
#include "test_framework.h"

using namespace chonk::voxels;
using namespace chonk::math;

// ============================================================================
// ChunkCoord Tests
// ============================================================================

bool test_ChunkCoordCreation() {
    ChunkCoord coord(1, 2, 3);
    
    ASSERT_EQ(coord.x, 1);
    ASSERT_EQ(coord.y, 2);
    ASSERT_EQ(coord.z, 3);
    
    return true;
}

bool test_ChunkCoordEquality() {
    ChunkCoord a(1, 2, 3);
    ChunkCoord b(1, 2, 3);
    ChunkCoord c(1, 2, 4);
    
    ASSERT_TRUE(a == b);
    ASSERT_FALSE(a == c);
    ASSERT_TRUE(a != c);
    
    return true;
}

bool test_ChunkCoordToString() {
    ChunkCoord coord(1, -2, 3);
    std::string str = coord.toString();
    
    ASSERT_EQ(str, std::string("1_-2_3"));
    
    return true;
}

bool test_ChunkCoordWorldPos() {
    ChunkCoord coord(1, 2, 3);
    Vec3 worldPos = coord.toWorldPos();
    
    ASSERT_FLOAT_EQ(worldPos.x, CHUNK_SIZE_METERS, 0.001f);
    ASSERT_FLOAT_EQ(worldPos.y, 2.0f * CHUNK_SIZE_METERS, 0.001f);
    ASSERT_FLOAT_EQ(worldPos.z, 3.0f * CHUNK_SIZE_METERS, 0.001f);
    
    return true;
}

bool test_ChunkCoordFromWorldPos() {
    Vec3 worldPos(800.0f, 1500.0f, 2200.0f);
    ChunkCoord coord = ChunkCoord::fromWorldPos(worldPos);
    
    // 800 / 729 = 1.09 -> floor = 1
    // 1500 / 729 = 2.05 -> floor = 2
    // 2200 / 729 = 3.01 -> floor = 3
    ASSERT_EQ(coord.x, 1);
    ASSERT_EQ(coord.y, 2);
    ASSERT_EQ(coord.z, 3);
    
    return true;
}

bool test_ChunkCoordDistance() {
    ChunkCoord a(0, 0, 0);
    ChunkCoord b(3, 4, 0);
    
    ASSERT_EQ(a.manhattanDistance(b), 7);
    ASSERT_EQ(a.chebyshevDistance(b), 4);
    
    return true;
}

// ============================================================================
// Chunk Tests
// ============================================================================

bool test_ChunkCreation() {
    ChunkCoord coord(1, 2, 3);
    Chunk chunk(coord);
    
    ASSERT_EQ(chunk.getCoord().x, 1);
    ASSERT_EQ(chunk.getCoord().y, 2);
    ASSERT_EQ(chunk.getCoord().z, 3);
    ASSERT_FALSE(chunk.isLoaded());
    
    return true;
}

bool test_ChunkLayeredGeneration() {
    ChunkCoord coord(0, 0, 0);  // Underground chunk
    Chunk chunk(coord);
    
    // Generate a small test at depth 2
    chunk.generateLayered(2, 1000.0f, 2000.0f, 3000.0f);
    
    ASSERT_TRUE(chunk.isLoaded());
    ASSERT_EQ(chunk.getCurrentDepth(), 2u);
    
    // Should be solid stone since entirely below ground
    ASSERT_FALSE(chunk.isAir());
    
    return true;
}

bool test_ChunkMaterialQuery() {
    // Create a chunk that spans ground level
    ChunkCoord coord(0, 8, 0);  // y=8 -> 8*729 = 5832m (near ground level ~6196)
    Chunk chunk(coord);
    
    chunk.generateLayered(3, STONE_HEIGHT, DIRT_HEIGHT, GRASS_HEIGHT);
    
    // Query material at various heights within the chunk
    Vec3 worldPos = chunk.getWorldPosition();
    
    // The chunk's material depends on where it is relative to terrain layers
    uint8_t mat = chunk.getMaterialAtLocal(CHUNK_SIZE_METERS / 2, CHUNK_SIZE_METERS / 2, CHUNK_SIZE_METERS / 2, 3);
    
    // Just verify we get a valid material
    ASSERT_TRUE(mat == MAT_AIR || mat == MAT_GRASS || mat == MAT_DIRT || mat == MAT_STONE);
    
    return true;
}

bool test_ChunkSerialization() {
    ChunkCoord coord(1, 2, 3);
    Chunk chunk(coord);
    
    chunk.generateLayered(2, 1000.0f, 2000.0f, 3000.0f);
    
    // Serialize
    auto data = chunk.serialize();
    ASSERT_TRUE(data.size() > 0u);
    
    // Deserialize into new chunk
    Chunk chunk2(coord);
    chunk2.deserialize(data.data(), data.size(), 2);
    
    ASSERT_TRUE(chunk2.isLoaded());
    ASSERT_EQ(chunk.getTree().getNodeCount(), chunk2.getTree().getNodeCount());
    
    return true;
}

// ============================================================================
// ChunkArray Tests
// ============================================================================

bool test_ChunkArrayCreation() {
    ChunkArray array;
    
    ASSERT_FALSE(array.isInitialized());
    ASSERT_EQ(array.getLoadedCount(), 0u);
    
    return true;
}

bool test_ChunkArrayRecenter() {
    ChunkArray array;
    
    ChunkCoord center(5, 8, 5);
    auto needsLoading = array.recenter(center);
    
    ASSERT_TRUE(array.isInitialized());
    ASSERT_EQ(array.getCenter().x, center.x);
    ASSERT_EQ(array.getCenter().y, center.y);
    ASSERT_EQ(array.getCenter().z, center.z);
    
    // Should need all 17^3 = 4913 chunks
    ASSERT_EQ(needsLoading.size(), static_cast<size_t>(CHUNK_ARRAY_TOTAL));
    
    return true;
}

bool test_ChunkArrayIndexConversion() {
    ChunkArray array;
    
    ChunkCoord center(10, 10, 10);
    array.recenter(center);
    
    // Center chunk should be at array index (8, 8, 8)
    uint32_t ix, iy, iz;
    ASSERT_TRUE(array.chunkCoordToIndex(center, ix, iy, iz));
    ASSERT_EQ(ix, CHUNK_ARRAY_CENTER);
    ASSERT_EQ(iy, CHUNK_ARRAY_CENTER);
    ASSERT_EQ(iz, CHUNK_ARRAY_CENTER);
    
    // Convert back
    ChunkCoord backCoord = array.indexToChunkCoord(ix, iy, iz);
    ASSERT_EQ(backCoord.x, center.x);
    ASSERT_EQ(backCoord.y, center.y);
    ASSERT_EQ(backCoord.z, center.z);
    
    return true;
}

bool test_ChunkArrayBounds() {
    ChunkArray array;
    
    ChunkCoord center(10, 10, 10);
    array.recenter(center);
    
    // Chunk at center is in bounds
    ASSERT_TRUE(array.isInBounds(center));
    
    // Chunk 8 away is at edge (still in bounds)
    ChunkCoord edge(10 + 8, 10, 10);
    ASSERT_TRUE(array.isInBounds(edge));
    
    // Chunk 9 away is out of bounds
    ChunkCoord outside(10 + 9, 10, 10);
    ASSERT_FALSE(array.isInBounds(outside));
    
    return true;
}

bool test_ChunkArrayRequiredDepth() {
    ChunkArray array;
    
    ChunkCoord center(0, 0, 0);
    array.recenter(center);
    
    // Center (8,8,8) should have full depth
    uint32_t depth = array.getRequiredDepth(8, 8, 8);
    ASSERT_EQ(depth, CHUNK_TREE_MAX_DEPTH);
    
    // Edge chunks should have lower depth
    depth = array.getRequiredDepth(0, 0, 0);
    ASSERT_TRUE(depth < CHUNK_TREE_MAX_DEPTH);
    
    return true;
}

bool test_ChunkArraySetGetChunk() {
    ChunkArray array;
    
    ChunkCoord center(0, 0, 0);
    array.recenter(center);
    
    // Create and set a chunk
    auto chunk = std::make_unique<Chunk>(center);
    chunk->generateLayered(3, 1000.0f, 2000.0f, 3000.0f);
    
    array.setChunk(8, 8, 8, std::move(chunk));
    
    // Get it back
    const Chunk* retrieved = array.getByIndex(8, 8, 8);
    ASSERT_TRUE(retrieved != nullptr);
    ASSERT_TRUE(retrieved->isLoaded());
    
    return true;
}

// ============================================================================
// Constants Tests
// ============================================================================

bool test_WorldConstants() {
    // Verify chunk size
    ASSERT_FLOAT_EQ(CHUNK_SIZE_METERS, 729.0f, 0.001f);
    
    // Verify array size
    ASSERT_EQ(CHUNK_ARRAY_SIZE, 17u);
    ASSERT_EQ(CHUNK_ARRAY_TOTAL, 4913u);
    
    // Verify tree depths
    ASSERT_EQ(CHUNK_TREE_MAX_DEPTH, 7u);
    ASSERT_EQ(WORLD_TREE_MAX_DEPTH, 6u);
    
    // Verify LOD layer calculation
    ASSERT_EQ(getLayerFromOffset(0, 0, 0), 0u);   // Center
    ASSERT_EQ(getLayerFromOffset(1, 0, 0), 0u);   // Still in 3x3x3
    ASSERT_EQ(getLayerFromOffset(2, 0, 0), 1u);   // Layer 1
    ASSERT_EQ(getLayerFromOffset(8, 0, 0), 7u);   // Edge
    
    return true;
}

// Entry point for main.cpp
void runChunkTests() {
    TestFramework& tf = TestFramework::instance();
    
    std::cout << "\nRunning Chunk tests...\n";
    
    // ChunkCoord tests
    tf.runTest("ChunkCoordCreation", test_ChunkCoordCreation);
    tf.runTest("ChunkCoordEquality", test_ChunkCoordEquality);
    tf.runTest("ChunkCoordToString", test_ChunkCoordToString);
    tf.runTest("ChunkCoordWorldPos", test_ChunkCoordWorldPos);
    tf.runTest("ChunkCoordFromWorldPos", test_ChunkCoordFromWorldPos);
    tf.runTest("ChunkCoordDistance", test_ChunkCoordDistance);
    
    // Chunk tests
    tf.runTest("ChunkCreation", test_ChunkCreation);
    tf.runTest("ChunkLayeredGeneration", test_ChunkLayeredGeneration);
    tf.runTest("ChunkMaterialQuery", test_ChunkMaterialQuery);
    tf.runTest("ChunkSerialization", test_ChunkSerialization);
    
    // ChunkArray tests
    tf.runTest("ChunkArrayCreation", test_ChunkArrayCreation);
    tf.runTest("ChunkArrayRecenter", test_ChunkArrayRecenter);
    tf.runTest("ChunkArrayIndexConversion", test_ChunkArrayIndexConversion);
    tf.runTest("ChunkArrayBounds", test_ChunkArrayBounds);
    tf.runTest("ChunkArrayRequiredDepth", test_ChunkArrayRequiredDepth);
    tf.runTest("ChunkArraySetGetChunk", test_ChunkArraySetGetChunk);
    
    // Constants tests
    tf.runTest("WorldConstants", test_WorldConstants);
}

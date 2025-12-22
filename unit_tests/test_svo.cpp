// Unit tests for 27-ary Tree (Tree27)
#include "../engine/voxels/tree27.h"
#include "../engine/voxels/world_constants.h"
#include "test_framework.h"

using namespace chonk::voxels;

// Test Tree27Node structure
bool test_Tree27NodeStructure() {
    Tree27Node node{};
    node.firstChildIndex = 100;
    node.childMask = 0;
    node.majorityMaterial = MAT_STONE;
    node.fillPercent = 255;
    
    ASSERT_EQ(node.firstChildIndex, 100u);
    ASSERT_EQ(node.majorityMaterial, MAT_STONE);
    ASSERT_EQ(node.fillPercent, 255u);
    ASSERT_TRUE(node.isLeaf());
    
    return true;
}

// Test child mask operations
bool test_Tree27ChildMask() {
    Tree27Node node{};
    node.childMask = 0;
    
    // Initially no children
    ASSERT_EQ(node.getChildCount(), 0u);
    
    // Set some children
    node.setChild(0, true);
    node.setChild(13, true);  // Center child
    node.setChild(26, true);
    
    ASSERT_EQ(node.getChildCount(), 3u);
    ASSERT_TRUE(node.hasChild(0));
    ASSERT_TRUE(node.hasChild(13));
    ASSERT_TRUE(node.hasChild(26));
    ASSERT_FALSE(node.hasChild(1));
    
    // Test child offset calculation
    ASSERT_EQ(node.getChildOffset(0), 0u);
    ASSERT_EQ(node.getChildOffset(13), 1u);
    ASSERT_EQ(node.getChildOffset(26), 2u);
    
    return true;
}

// Test flags
bool test_Tree27Flags() {
    Tree27Node node{};
    node.childMask = 0;
    
    ASSERT_FALSE(node.hasMoreDetail());
    ASSERT_TRUE(node.isLeaf());
    ASSERT_FALSE(node.isAir());
    
    node.setFlag(FLAG_HAS_MORE_DETAIL, true);
    ASSERT_TRUE(node.hasMoreDetail());
    
    node.setFlag(FLAG_IS_AIR, true);
    ASSERT_TRUE(node.isAir());
    
    // Clear flags
    node.clearFlags();
    ASSERT_FALSE(node.hasMoreDetail());
    ASSERT_FALSE(node.isAir());
    
    return true;
}

// Test child index calculation
bool test_Tree27ChildIndex() {
    float childX, childY, childZ;
    
    // Corner (0,0,0) -> child 0
    uint32_t idx = Tree27::getChildIndex(0.0f, 0.0f, 0.0f, 9.0f, childX, childY, childZ);
    ASSERT_EQ(idx, 0u);
    
    // Center child (1,1,1) in 3x3x3 -> child 13
    idx = Tree27::getChildIndex(4.5f, 4.5f, 4.5f, 9.0f, childX, childY, childZ);
    ASSERT_EQ(idx, 13u);
    
    // Corner (2,2,2) -> child 26
    idx = Tree27::getChildIndex(8.0f, 8.0f, 8.0f, 9.0f, childX, childY, childZ);
    ASSERT_EQ(idx, 26u);
    
    return true;
}

// Test layered terrain generation
bool test_Tree27LayeredGeneration() {
    Tree27 tree;
    
    // Generate a small test tree (depth 2) for underground chunk
    chonk::math::Vec3 origin(0.0f, 0.0f, 0.0f);  // Below stone height
    tree.generateLayered(origin, 100.0f, 2, 200.0f, 300.0f, 400.0f);
    
    // Should be uniform stone since entirely below stone height
    ASSERT_TRUE(tree.getNodeCount() > 0u);
    ASSERT_EQ(tree.getRootMaterial(), MAT_STONE);
    ASSERT_EQ(tree.getRootFillPercent(), 255u);
    
    return true;
}

// Test layered terrain for air
bool test_Tree27AirGeneration() {
    Tree27 tree;
    
    // Generate for above-ground chunk
    chonk::math::Vec3 origin(0.0f, 500.0f, 0.0f);  // Above grass height
    tree.generateLayered(origin, 100.0f, 2, 100.0f, 200.0f, 300.0f);
    
    // Should be uniform air
    ASSERT_TRUE(tree.getNodeCount() > 0u);
    ASSERT_EQ(tree.getRootMaterial(), MAT_AIR);
    ASSERT_EQ(tree.getRootFillPercent(), 0u);
    ASSERT_TRUE(tree.isCollapsed());
    
    return true;
}

// Test material query
bool test_Tree27MaterialQuery() {
    Tree27 tree;
    
    // Generate an all-stone tree (y=0 to 100, stone below 200)
    chonk::math::Vec3 origin(0.0f, 0.0f, 0.0f);
    tree.generateLayered(origin, 100.0f, 4, 200.0f, 300.0f, 400.0f);
    
    // Query should return stone since entirely below stone height
    uint8_t mat = tree.getMaterialAtLOD(50.0f, 50.0f, 50.0f, 100.0f, 4);
    ASSERT_EQ(mat, MAT_STONE);
    
    // Generate an all-air tree (y=500 to 600, grass at 400)
    tree.generateLayered(chonk::math::Vec3(0.0f, 500.0f, 0.0f), 100.0f, 4, 200.0f, 300.0f, 400.0f);
    
    // Query should return air since entirely above grass height
    mat = tree.getMaterialAtLOD(50.0f, 50.0f, 50.0f, 100.0f, 4);
    ASSERT_EQ(mat, MAT_AIR);
    
    return true;
}

// Test serialization
bool test_Tree27Serialization() {
    Tree27 tree;
    
    chonk::math::Vec3 origin(0.0f, 0.0f, 0.0f);
    tree.generateLayered(origin, 100.0f, 2, 50.0f, 75.0f, 100.0f);
    
    // Serialize
    auto data = tree.serialize();
    ASSERT_TRUE(data.size() > 0u);
    
    // Deserialize into new tree
    Tree27 tree2;
    tree2.deserialize(data.data(), tree.getNodeCount());
    
    ASSERT_EQ(tree.getNodeCount(), tree2.getNodeCount());
    ASSERT_EQ(tree.getRootMaterial(), tree2.getRootMaterial());
    ASSERT_EQ(tree.getRootFillPercent(), tree2.getRootFillPercent());
    
    return true;
}

// Entry point for main.cpp
void runSVOTests() {
    TestFramework& tf = TestFramework::instance();
    
    std::cout << "\nRunning Tree27 tests...\n";
    
    tf.runTest("Tree27NodeStructure", test_Tree27NodeStructure);
    tf.runTest("Tree27ChildMask", test_Tree27ChildMask);
    tf.runTest("Tree27Flags", test_Tree27Flags);
    tf.runTest("Tree27ChildIndex", test_Tree27ChildIndex);
    tf.runTest("Tree27LayeredGeneration", test_Tree27LayeredGeneration);
    tf.runTest("Tree27AirGeneration", test_Tree27AirGeneration);
    tf.runTest("Tree27MaterialQuery", test_Tree27MaterialQuery);
    tf.runTest("Tree27Serialization", test_Tree27Serialization);
}

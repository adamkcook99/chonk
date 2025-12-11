#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

// -----------------------------
// GPU-friendly node structure
// -----------------------------
struct NodeGPU {
    uint32_t children[8]; // indices of children (0xFFFFFFFF if missing)
    uint32_t childMask;
    uint32_t material;    // 0=air,1=grass,2=dirt,3=stone
    uint32_t padding[2];  // pad to 16-byte alignment
};

// -----------------------------
// Voxel LOD levels
// -----------------------------
enum LOD { LOD_025M = 1, LOD_1M = 4, LOD_4M = 16, LOD_16M = 64, LOD_64M = 256 };

// -----------------------------
// Terrain function: simple layered terrain
// -----------------------------
uint32_t terrainMaterial(int y) {
    if (y >= 40 && y <= 63) return 3;  // stone
    if (y >= 33 && y <= 39) return 2;  // dirt
    if (y == 32) return 1;             // grass
    return 0;                           // air
}

// -----------------------------
// Generate a chunk at a given resolution
// -----------------------------
struct Voxel {
    uint32_t material = 0;
};

void generateChunk(int chunkSize, int lod, std::vector<Voxel>& outVoxels) {
    outVoxels.resize(chunkSize * chunkSize * chunkSize);
    int step = lod; // number of base voxels per sample

    for (int z = 0; z < chunkSize; z += step) {
        for (int y = 0; y < chunkSize; ++y) {
            for (int x = 0; x < chunkSize; x += step) {
                int worldY = y; // world coordinates, could offset by chunk position
                uint32_t mat = terrainMaterial(worldY);

                // Fill all LOD^3 fine voxels with same material
                for (int dz = 0; dz < step && (z+dz)<chunkSize; ++dz) {
                    for (int dx = 0; dx < step && (x+dx)<chunkSize; ++dx) {
                        int idx = (z+dz) * chunkSize * chunkSize + y * chunkSize + (x+dx);
                        outVoxels[idx].material = mat;
                    }
                }
            }
        }
    }
}

// -----------------------------
// Flatten voxel array into flat SVO nodes (for GPU)
// Simple example: 1 node per voxel
// -----------------------------
void flattenChunkToSVO(const std::vector<Voxel>& voxels, int chunkSize,
                       std::vector<NodeGPU>& flatNodes) {
    flatNodes.resize(voxels.size());
    for (size_t i = 0; i < voxels.size(); ++i) {
        NodeGPU node{};
        node.material = voxels[i].material;
        node.childMask = 0;                  // single voxel = leaf
        for (int c = 0; c < 8; ++c) node.children[c] = 0xFFFFFFFF;
        flatNodes[i] = node;
    }
}

// -----------------------------
// Demo
// -----------------------------
int main() {
    int chunkSize = 64; // 64×64×64 blocks
    std::vector<Voxel> voxels;
    std::vector<NodeGPU> flatNodes;

    // Generate chunk at multiple LODs
    int lodLevels[] = {1,4,16,64,256}; // 0.25,1,4,16,64 meters
    for (int lod : lodLevels) {
        std::cout << "Generating chunk at LOD " << lod << "...\n";
        generateChunk(chunkSize, lod, voxels);

        flattenChunkToSVO(voxels, chunkSize, flatNodes);
        std::cout << "Flat nodes generated: " << flatNodes.size() << "\n";

        // Optional: print top layer for visualization
        std::cout << "stack of 64 blocks from top to bottom:";

        for (int z = 0; z < chunkSize; ++z) {
            for (int y = 0; y < chunkSize; ++y) {
                int idx = 0 * chunkSize * chunkSize + y * chunkSize;
                std::cout << flatNodes[idx].material << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\n\n";
    }

    return 0;
}

// run with g++ -std=c++17 -O2 level_of_detail.cpp -o level_of_detail
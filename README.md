# CHONK

A voxel game with infinite world and practically infinite view distance.

## Building

### Prerequisites

- CMake 3.31 or higher
- C++17 compatible compiler
- ENet library (install via `brew install enet` on macOS)
- Vulkan SDK (MoltenVK will be fetched automatically)

### Build Instructions

1. Create a build directory:
```bash
mkdir build
cd build
```

2. Configure with CMake:
```bash
cmake ..
```

3. Build:
```bash
cmake --build .
```

4. Build (shaders will be compiled automatically):
```bash
cmake --build .
```

## Running and Testing

### Starting the Server

In one terminal:
```bash
cd build
./server/chonk_server
```

You should see:
```
Starting CHONK Server...
Server running on port 7777
```

The server will:
- Generate chunks as players move around
- Only keep simulation distance (5x5x5 = 125 chunks) in memory
- Send chunks to connected clients
- Save/load chunks from the `chunks/` directory

### Starting the Client

In another terminal:
```bash
cd build
./client/chonk_client
```

You should see a window open. The client will:
- Connect to the server on `localhost:7777`
- Keep the full render distance (13x13x13 = 2197 chunks) in memory
- Perform frustum culling before sending chunks to GPU
- Render using the compute shader ray tracer

### Controls

- **W** - Move forward
- **S** - Move backward  
- **A** - Strafe left
- **D** - Strafe right
- **ESC** - Close window

### What to Expect

1. **First Run**: The server will generate chunks as you move. You should see:
   - A window with a rendered view
   - Console output showing chunk loading/generation
   - The world should render with simple terrain (stone, dirt, grass layers)

2. **Chunk Loading**: As you move around:
   - Server generates chunks at correct resolution
   - Client receives chunks via network
   - Chunks are saved to `chunks/` directory for persistence

3. **Rendering**: The compute shader will:
   - Ray trace through the SVO structure
   - Show materials with simple colors (green=grass, brown=dirt, grey=stone)
   - Sky color when looking at empty space

### Troubleshooting

- **"Failed to connect to server"**: Make sure the server is running first
- **Black screen**: Check that shaders compiled successfully (should be in `build/shaders/`)
- **No chunks rendering**: Check server console for chunk generation messages
- **Network errors**: Ensure port 7777 is not blocked by firewall

## Project Structure

- `engine/` - Core engine code (graphics, voxels, networking, math)
- `game/` - Game-specific logic (world generation)
- `client/` - Client executable
- `server/` - Server executable
- `shaders/` - Compute shaders for ray tracing

## Features Implemented

- ✅ SVO (Sparse Voxel Octree) data structure
- ✅ Variable resolution chunk system
- ✅ Chunk loading and unloading
- ✅ Basic client-server networking with ENet
- ✅ Compute shader rendering foundation
- ✅ Simple world generation

## Next Steps

- Complete SVO traversal in compute shader
- Implement proper ray tracing with shadows and lighting
- Add player movement and camera controls
- Improve chunk serialization/deserialization
- Add chunk request/response protocol


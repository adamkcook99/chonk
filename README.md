# CHONK

A voxel game and engine built around a custom **27-ary tree** data structure,
designed for a practically infinite world and a practically infinite view
distance. Written from scratch in C++ with a Vulkan compute shader renderer.

> **Status:** work in progress. Ray marching through the loaded chunk array
> works; distant world-tree traversal and lighting are the next milestones.
> Screenshots coming soon.

## What's interesting about it

**A 27-ary tree instead of an octree.** Each node has 27 children in a 3×3×3
arrangement. The 27-bit presence mask fits in a `uint32` with 5 bits left over
for flags (leaf, air, has-more-detail), so every node is a compact 12 bytes —
packed for direct upload to the GPU. The 3×3×3 subdivision also has a true
"center" child, which makes LOD transitions clean: a parent's majority material
and fill percent come straight from the center-weighted children.

**Two levels of tree, two scales of world.** Nearby terrain lives in per-chunk
trees (depth 7, down to 1/3-meter voxels inside a 729 m chunk). Distant terrain
is summarized in a single world tree (depth 6) whose leaves *are* chunks. A ray
walks per-chunk detail inside the loaded 17×17×17 region, then falls through to
the world tree beyond it. At the limit that's a view distance of ~95 km with
voxel-accurate close-up detail.

**Ring-based LOD with horizontal-first priority.** The 17×17×17 loaded region
is organized as concentric rings around the player. The center 3×3×3 is
rendered at full depth; each outer shell drops one level. Chunks at the
player's Y-level are prioritized above chunks that are only vertically
distant — you see horizon detail before you see detail directly above you,
which is where it matters.

**Async loading pipeline.** A priority queue feeds 4 load threads plus 2
worker threads for recentering and unloading. The main thread never blocks on
I/O or generation. GPU updates are double-buffered — a dedicated worker
assembles the next node buffer in the background while the renderer draws the
current one.

**Vulkan compute shader ray tracer.** The renderer is a single compute shader
that marches each pixel's ray through the chunk grid and descends the 27-ary
tree on each hit candidate. Runs on macOS through MoltenVK.

**Client-server from day one.** The server owns authoritative chunk state,
streams chunks to clients at the requested LOD depth, and persists them to
disk. The client assembles its chunk array and world tree asynchronously as
the player moves. Networking is ENet (reliable UDP).

## Architecture at a glance

```
┌───────────────────────┐          ┌───────────────────────┐
│       Server          │          │       Client          │
│                       │          │                       │
│  WorldGenerator       │          │  Vulkan Renderer      │
│      │                │          │      │                │
│  ChunkManager ────────┼─ ENet ───┼──► ChunkManager       │
│      │                │  chunks  │      │                │
│  Chunks on disk       │          │  17×17×17 array +     │
│                       │          │  world tree           │
└───────────────────────┘          └──────────┬────────────┘
                                              │
                                   ┌──────────▼────────────┐
                                   │  raytrace.comp        │
                                   │  (ray-march chunks,   │
                                   │   descend 27-ary tree)│
                                   └───────────────────────┘
```

| Thing            | Value                                             |
|------------------|---------------------------------------------------|
| Smallest voxel   | 0.333 m (1/3 m)                                   |
| Chunk size       | 729 m per axis (3⁶ m)                             |
| Chunk tree depth | 7 (729 m → 243 → 81 → 27 → 9 → 3 → 1 → 0.333 m)  |
| World tree depth | 6, chunk-sized leaves                             |
| Loaded region    | 17³ chunks = 4,913 chunks around the player       |
| Render window    | 15³ chunks (2-layer prefetch buffer)              |
| Full-detail core | 3³ chunks centered on the player                  |
| Near view        | ~6 km with full LOD descent                       |
| Far view         | ~95 km through world-tree summarization           |
| Node size on GPU | 12 bytes                                          |

## Layout

```
engine/
  core/        logging
  math/        vec3
  graphics/    Vulkan renderer, swapchain, pipeline setup
  voxels/      Tree27, Chunk, ChunkArray, ChunkManager, WorldTree
  networking/  ENet client/server wrapper
game/
  world/       procedural world generator
client/        client executable (window + renderer + net client)
server/        headless server executable (world gen + net server)
shaders/       raytrace.comp, text_overlay.comp
unit_tests/    SVO and chunk tests
```

## Building

### Prerequisites

- CMake 3.31+
- A C++17 compiler
- [ENet](http://enet.bespin.org/) — `brew install enet` on macOS
- Vulkan SDK (MoltenVK is fetched automatically on macOS)

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Shaders are compiled as part of the CMake build.

## Running

Server (in one terminal):

```bash
./build/server/chonk_server
# Starting CHONK Server...
# Server running on port 7777
```

Client (in another):

```bash
./build/client/chonk_client
```

The client connects to `localhost:7777`, requests chunks around the camera,
and renders them with the compute shader. Chunks are persisted to `chunks/`
and reused on subsequent runs.

### Controls

- `W` / `A` / `S` / `D` — move
- `Esc` — quit

## Notes

The `documentation/REDESIGN` file is the design document the current code is
built against — a useful read if you want more detail on why the 27-ary tree
and the two-tree hierarchy. `documentation/README` is the earlier (octree-era)
vision doc, kept for history.

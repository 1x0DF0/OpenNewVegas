# Open New Vegas — Engine

The native (C++) side of the project. Right now it contains the **terrain
model** — a faithful port of `map/js/terrain.js` — and a **heightmap exporter**
that turns it into engine-ready data.

## Why both JS and C++?

- The **JS version** powers `map/index.html`: a zero-install world map anyone
  can open in a browser. That's the project's front door and design tool.
- The **C++ version** is the start of the game itself: fast enough to generate
  terrain at runtime, link into an engine, or batch-export streaming cells.

Both implement the *same* deterministic `elevation(x, y)` function with the
same seed and feature list. **They must stay in sync** — a change to the
Mojave's shape is made in both files or not at all.

## Build & run

```bash
cd engine
cmake -B build
cmake --build build
./build/export_heightmap 2048 mojave.pgm
```

Output is a 16-bit PGM (0 m → 0, 4500 m → 65535), row 0 = north edge.
GIMP, ImageMagick, Godot, and Unreal all import it directly.

## Next steps (Phase 1)

- Chunked cell export (streaming-friendly tiles + LODs)
- Splatmap export (biome/material weights derived from slope + elevation)
- Location registry import (`map/js/data/locations.js` → spawn markers)

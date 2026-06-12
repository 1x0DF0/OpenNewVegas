# Open New Vegas

**A free, open-source engine reimplementation for Fallout: New Vegas —
the OpenMW approach, aimed at the Mojave.**

The goal: a modern, open engine that plays the *actual* game, one-to-one —
real quests, real dialogue, real world — by loading the **original data files
from your own legally-purchased copy** of Fallout: New Vegas. This repository
ships **zero** Bethesda content. You bring the game; we bring the engine.

> ⚠️ **Legal note:** This is an unofficial fan project, not affiliated with or
> endorsed by Bethesda Softworks, Obsidian Entertainment, or ZeniMax/Microsoft.
> *Fallout* is a trademark of its respective owners. No proprietary assets,
> data, or code are included or distributed — the engine reads file formats
> that are publicly documented by the modding community, from files the user
> already owns. This is the same legal model proven by
> [OpenMW](https://openmw.org/) (Morrowind).

## How "one-to-one" works

Everything that makes New Vegas *New Vegas* lives in its data files:

| File | Contains |
|------|----------|
| `FalloutNV.esm` | The entire world: every cell, quest, NPC, dialogue line, script, item |
| `*.bsa` archives | All assets: meshes, textures, sounds, voice acting, music |

An engine that fully understands those files plays the full game. That's the
project: reimplement the engine, read the originals, render the Mojave.

## Current status

**Walking prototype** ✅ — walk around the Mojave in first person, two ways:

- **Native (C++/SDL2/OpenGL):** `engine/build/walker` — WASD + mouse look,
  sprint, jump, gravity, terrain collision, fog. Renders the engine's
  procedural Mojave terrain model.
- **Browser (zero install):** open `walk/index.html` — same controls, same
  terrain. Load *real* worldspace terrain exported from your own game copy
  with `?src=yourbundle.json` (see below).

**Milestone 2 (partial) — world geometry from your game files** ✅

- `engine/records/` decodes `WRLD` / `CELL` / `LAND` / `REFR` / `STAT` —
  worldspaces, exterior cells, terrain heightmaps, and object placements
- `engine/tools/worldexport` stitches a worldspace's LAND heightmaps into a
  terrain bundle the walker loads — i.e. **walk the real Mojave terrain from
  your own `FalloutNV.esm`**
- `engine/tools/laa_patch` — the standard 4GB (Large Address Aware) patch for
  your own game exe, as a clean open-source tool

Setup guide (4GB patch, NVSE, ultrawide fixes, exporting your worldspace):
[`docs/PLAY_SETUP.md`](docs/PLAY_SETUP.md).

**Milestone 1 — read the game's file formats** ✅ working, tested

- `engine/formats/bsa.*` — BSA v104 archive reader (the FO3/FNV asset format),
  with zlib decompression and embedded-name support
- `engine/formats/esm.*` — ESM/ESP plugin reader: record/GRUP tree walking,
  compressed records, oversized (XXXX) subrecords
- `engine/tools/bsatool` — list and extract archive contents
- `engine/tools/esmdump` — inspect plugins: record counts, list worldspaces /
  quests / NPCs by editor ID
- `engine/tests/format_tests` — verified against synthetic fixture files
  (no game data in the repo, ever)

If you own the game, try it today:

```bash
cd engine && cmake -B build && cmake --build build
./build/esmdump counts "/path/to/FalloutNV/Data/FalloutNV.esm"
./build/esmdump list  "/path/to/FalloutNV/Data/FalloutNV.esm" WRLD
./build/bsatool list  "/path/to/FalloutNV/Data/Fallout - Meshes.bsa"
```

## The world map

`map/index.html` — an interactive, zero-dependency map of the Mojave with
procedural terrain, 42 locations, and the road network. Open it in any
browser. It doubles as the project's reference atlas and will become the
debug/world-inspection overlay. See [`docs/WORLDBUILDING.md`](docs/WORLDBUILDING.md).

## Roadmap

Full plan in [`docs/ROADMAP.md`](docs/ROADMAP.md). Short version:

1. ✅ **File formats** — BSA archives, ESM plugins
2. **Asset decoding** — NIF meshes, DDS textures, record schemas (CELL, REFR, LAND…)
3. **Renderer** — load and draw the real Mojave worldspace
4. **Gameplay systems** — scripts, dialogue, quests, combat, AI
5. **One-to-one parity** — the campaign, start to Hoover Dam

## License

Engine code and original data in this repository: MIT License.
Game data is not included and must be provided by the user from their own copy.

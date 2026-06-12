# Open New Vegas

A project with **two goals** around making Fallout: New Vegas better — one you
can use today, one for the long haul:

- **🎮 Remaster your own copy now (play today).** A staged mod guide that turns
  your owned game into a complete, modern-looking, stable New Vegas — HD art,
  new lighting/weather, ENB, engine fixes. This is the fast path to *playing* a
  "remastered" New Vegas. See **[docs/REMASTER_GUIDE.md](docs/REMASTER_GUIDE.md)**.
- **⚙️ A free, open-source engine reimplementation (long-term).** The OpenMW
  approach: a modern, open engine that plays the *actual* game by loading the
  **original data files from your own legally-purchased copy**. This repository
  ships **zero** Bethesda content. You bring the game; we bring the engine.
  Currently early — renders terrain; everything below describes its progress.

> The engine is years of work (OpenMW took ~a decade); the remaster guide gets
> you a complete, great-looking game this afternoon. Most people want the guide.

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

**Milestone 3 (started) — placed objects in the world** ✅

The engine now turns object *placements* into rendered geometry:

- `engine/assets/` — a Data-directory virtual filesystem that resolves an
  asset path to bytes from the mounted BSA archives **and** loose files (loose
  wins, as in-game)
- `engine/scene/` — scene assembly: every `REFR` placement resolves through its
  base `STAT` to a model path, the NIF is loaded from the VFS and decoded, and
  the object is placed (position / rotation / scale)
- `engine/tools/scenedump` — run it against your install to see what loads:
  `scenedump "<...>/Fallout New Vegas/Data" WastelandNV`
- The native `walker` renders these objects. Try the self-contained demo (no
  game files needed): `walker --demo` drops a scatter of boxes onto the Mojave
  through the real placement→model→render path.

Still early: object streaming is limited to cells near the worldspace origin,
and materials/textures aren't applied yet.

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

# Open New Vegas — Roadmap

## Phase 0 — The World Map ✅

The map is the contract for everything that follows. It defines:

- A **world coordinate system**: 100×100 world units, x increases east,
  y increases north. 1 world unit ≈ 350 m of real Mojave, so the playable
  region is roughly 35×35 km — the area between Mt. Charleston, Primm,
  Cottonwood Cove, and Lake Mead.
- A **deterministic terrain model** (`map/js/terrain.js`): elevation is a pure
  function of (x, y, seed). The same function will later drive engine-side
  terrain generation, so the map viewer and the game world can never drift
  apart.
- The **location registry** (`map/js/data/locations.js`): the single source of
  truth for what exists, where it is, and who controls it.
- The **road network** (`map/js/data/roads.js`).

Deliverable: `map/index.html`, a zero-dependency interactive viewer.

## Phase 1 — Engine Bring-up (started: `engine/`)

- ✅ C++ terrain core (`engine/terrain/`) — port of the JS model, verified to
  produce identical elevations.
- ✅ Heightmap exporter (`engine/tools/export_heightmap.cpp`) — 16-bit PGM.
- Chunked cell export for streaming + LODs.
- Renderer decision: custom C++ (OpenGL/Vulkan) vs. Godot 4 with the C++ core
  as a GDExtension. Either way, the simulation/world code stays in C++.
- Import the location registry as spawn markers.

## Phase 2 — Walkable Worldspace

- Terrain streaming + collision.
- Day/night cycle, Mojave weather (clear, overcast, dust storms).
- Fast travel between discovered map markers.

## Phase 3 — Core RPG Systems

- Dialogue trees with skill checks.
- Quest framework (stages, objectives, journal).
- Faction reputation matrix (NCR ↔ Legion ↔ Strip ↔ independents).
- Character stats: attributes, skills, perks (original implementations).

## Phase 4 — Content

- Original questlines and writing set in the same geography.
- Settlement interiors, NPCs with schedules.
- The Strip, Freeside, and the run for the Dam.

## Non-goals

- **Never** redistributing, converting, or requiring proprietary game assets.
- Engine-level compatibility with Gamebryo `.esm`/`.esp` formats (that's a
  different project's fight).

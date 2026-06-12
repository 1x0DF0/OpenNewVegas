# Open New Vegas

**An open-source recreation of the Mojave Wasteland.**

Open New Vegas is a fan-driven, from-scratch rebuild of the world of the Mojave —
the desert, the towns, the factions, and the stories — using **100% original code,
data, and assets**. No game files are copied, extracted, or redistributed. Ever.

> ⚠️ **Legal note:** This is an unofficial fan project. It is not affiliated with,
> endorsed by, or connected to Bethesda Softworks, Obsidian Entertainment, or
> ZeniMax/Microsoft. *Fallout* is a trademark of its respective owners. This
> project contains no proprietary game assets or data — everything here is
> written from scratch. Most place names (Goodsprings, Primm, Searchlight,
> Hoover Dam, Nellis...) are real locations in the Nevada Mojave.

---

## 🗺️ Phase 0: The World Map (you are here)

The first deliverable is the **world map** — the foundation everything else gets
built on. It's live in this repo right now:

```
map/index.html   ← open this in any browser. No build step, no dependencies.
```

Or serve it locally:

```bash
cd map && python3 -m http.server 8080
# then open http://localhost:8080
```

### What the map includes

- **Procedurally generated Mojave terrain** — the Spring Mountains, the Las Vegas
  valley, the McCullough Range, the Colorado River, and Lake Mead are all
  generated from a deterministic, seeded terrain model (`map/js/terrain.js`).
  Real topography, zero copied heightmaps.
- **40+ mapped locations** — settlements, vaults, faction camps, ruins, and
  hazards, each with coordinates, faction control, and original descriptions
  (`map/js/data/locations.js`).
- **The road network** — I-15 (the Long 15), Highway 95, Highway 93 to the Dam,
  Nipton Road, and Highway 160 (`map/js/data/roads.js`).
- **Interactive viewer** — pan, zoom, hover for details, filter by location type,
  search, and click-to-travel.
- **C++ engine core** — the same terrain model, ported to C++ in
  [`engine/`](engine/), with a 16-bit heightmap exporter for engine import.
  The JS and C++ models are verified to produce identical elevations.

## 🧭 Project Roadmap

| Phase | Goal | Status |
|-------|------|--------|
| 0 | World map: terrain, locations, roads, viewer | ✅ In repo |
| 1 | Engine bring-up (C++) + terrain export pipeline | 🚧 Started — see [`engine/`](engine/) |
| 2 | Walkable worldspace: collision, time-of-day, weather | Planned |
| 3 | Core systems: dialogue, quests, reputation, S.P.E.C.I.A.L.-like RPG stats | Planned |
| 4 | Content: original questlines, characters, and writing | Planned |

Details in [`docs/ROADMAP.md`](docs/ROADMAP.md). How the world is laid out and
how to add locations: [`docs/WORLDBUILDING.md`](docs/WORLDBUILDING.md).

## 🤝 Contributing

The whole point of an open Mojave is that anyone can build on it. Good first
contributions:

- Add missing locations to `map/js/data/locations.js` (the format is documented
  in `docs/WORLDBUILDING.md`)
- Refine terrain features in `map/js/terrain.js`
- Write original descriptions for mapped places

## License

Code and original data in this repository are released under the MIT License.

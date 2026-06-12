# Open New Vegas — Roadmap

**Mission: a free engine that plays Fallout: New Vegas one-to-one from the
user's own game files.** No bundled assets, no copied engine code — a
reimplementation, the way OpenMW did it for Morrowind (a ~decade-scale
community effort; honesty about that scale is part of the plan).

## Milestone 1 — File formats ✅

The engine can read the two container formats everything else lives in:

- ✅ **BSA v104** (`engine/formats/bsa.*`) — asset archives: directory parsing,
  zlib decompression, per-file compression toggle, embedded names,
  case-insensitive lookup, Bethesda name hashing
- ✅ **ESM/ESP** (`engine/formats/esm.*`) — plugin files: record/GRUP tree,
  24-byte FO3/FNV record headers, compressed records, XXXX oversized
  subrecords, streaming walker API
- ✅ CLI tools (`bsatool`, `esmdump`) + synthetic-fixture test suite

## Milestone 2 — Record schemas & asset decoding

Turn raw records into typed game data:

- Decode key record types: `WRLD`/`CELL`/`LAND`/`REFR` (world geometry and
  object placement), `STAT`/`NPC_`/`CREA` (things), `QUST`/`DIAL`/`INFO`
  (quests and dialogue), `SCPT` (scripts)
- FormID resolution and the master-file load order model
- **NIF** mesh decoding (NetImmerse/Gamebryo geometry — community-documented
  via niftools) and **DDS** textures
- Validation harness: load `FalloutNV.esm` end-to-end, report coverage stats

## Milestone 3 — Renderer: walk the real Mojave

- Worldspace terrain from `LAND` records, object placement from `REFR`
- Scene graph + culling; render interiors and the exterior worldspace
- Free camera first, then player controller with collision
- The map viewer (`map/`) becomes the in-engine debug atlas

## Milestone 4 — Gameplay systems

- Script VM (the in-game scripting language used by the original)
- Dialogue + quest state machine, journal, reputation/faction matrices
- Combat, VATS-equivalent targeting, AI packages and schedules
- Save/load (own format; import of original saves is stretch)

## Milestone 5 — One-to-one parity

- Full campaign playable: Goodsprings → endgame at Hoover Dam
- DLC support (Dead Money, Honest Hearts, Old World Blues, Lonesome Road)
- Mod compatibility (`.esp` plugins work as they do in the original)

## Standing rules

- **No Bethesda content in the repo. Ever.** Tests use synthetic fixtures.
- **No decompiled or leaked engine code.** Formats are implemented from
  community documentation (UESP, xEdit, niftools) — clean-room only.
- The `map/` atlas stays: original code/data, useful as reference and debug UI.

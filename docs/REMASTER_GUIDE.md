# Remastering Fallout: New Vegas (your own copy)

This is the **play-it-now** path. You already own the complete game — every
quest, character, and line of dialogue is on your disk. "Remastering" it means
making it **look and run dramatically better** with the modern engine fixes and
art the community has built, installed cleanly on top of your copy.

Done in an afternoon, this gets you the *whole* game, remastered-looking and
stable — legally, because it's your install plus mods, not a recreation.

> Everything here operates on files **you own**. Mods are downloaded by you,
> from the community (mainly Nexus Mods), into your own installation.
> Install path used in examples (adjust to yours):
> `D:\SteamLibrary\steamapps\common\Fallout New Vegas`

---

## How this fits with the engine project

- **This guide (Path A)** = play a remastered New Vegas now, via mods.
- **The `engine/` project (Path B)** = a from-scratch open engine that loads
  your game's data; long-term, currently terrain only. Keeps developing in
  parallel. See the main [README](../README.md) and [ROADMAP](ROADMAP.md).

You don't have to choose. Play with Path A today; the engine grows over time.

---

## Ground rules (read these first)

1. **Use a mod manager — never drag files into the game folder by hand.**
   Install **Mod Organizer 2 (MO2)**. It keeps every mod in its own folder so
   nothing is permanently changed and you can undo anything. This is the single
   most important decision for a stable modded game.
2. **Follow a vetted base guide for the foundation.** The community-maintained
   **Viva New Vegas** guide (vivanewvegas.moddinglinked.com) is the gold
   standard for the stability/engine layer and exact install order. Use it for
   Stage 1–2 below; this document curates the visual layer on top.
3. **Install in stages and launch the game after each stage.** If something
   breaks, you know which stage did it. Don't install 40 mods then launch.
4. **Sort your load order with LOOT** after installing plugins.
5. Keep the **4GB patch + xNVSE** from [`PLAY_SETUP.md`](PLAY_SETUP.md) — they
   are the foundation everything else depends on.

---

## Stage 0 — Prep

- Verify the base game runs once (launch via Steam, reach the main menu, quit).
- Install **Mod Organizer 2** and point it at your FNV install.
- Apply the **4GB patch** and install **xNVSE** (see `PLAY_SETUP.md`).
- Run the game through MO2 once via the NVSE loader to confirm the chain works.

## Stage 1 — Foundation: stability & engine fixes

These make a modded game survive. Install these before any visual mods.

| Mod | What it does |
|-----|--------------|
| **xNVSE** | Script extender other mods require |
| **NVAC – New Vegas Anti Crash** | Catches/recovers from crashes |
| **New Vegas Tick Fix (NVTF)** | Fixes frame-rate and engine timing bugs |
| **lStewieAl's Tweaks** | Hundreds of engine fixes and quality-of-life toggles |
| **JIP LN NVSE** + **JohnnyGuitar NVSE** | Core script-extender plugins many mods need |
| **Mod Configuration Menu (MCM)** | In-game settings menu for mods |
| **Console Paste Support / kNVSE** | Common dependencies |

Launch and reach the main menu before continuing.

## Stage 2 — Bug fixes (the "unofficial patch")

| Mod | What it does |
|-----|--------------|
| **YUP – Yukichigai Unofficial Patch** | Thousands of vanilla bug fixes |
| **Unofficial Patch NVSE Plus** | Script-level fixes layered on YUP |

These are content-safe: they fix the game without changing its design.
Run **LOOT** after this stage to sort plugins.

## Stage 3 — Visual remaster: textures & models

This is where it starts *looking* remastered. Pick a primary texture pack,
then patch in specifics.

| Category | Well-known options |
|----------|--------------------|
| World/landscape textures | **NMC's Texture Pack**, **Ojo Bueno** |
| Character faces/bodies | **Fallout Character Overhaul (FCO)**, **New Vegas Redesigned 3** |
| Weapons & armor meshes | **Weapon Retexture Project (WRP)**, **Millenia's** weapon retextures |
| Clutter / world objects | High-res object/clutter packs of your choice |
| Water | **Improved water / clarity** mods |

Tip: one big texture pack as the base, then smaller packs only for things it
misses. More isn't better — VRAM and load order matter.

## Stage 4 — Lighting & weather (biggest "feel" upgrade)

| Category | Well-known options |
|----------|--------------------|
| Interior lighting | **Interior Lighting Overhaul** |
| Wasteland lighting | **FNV Realistic Wasteland Lighting** |
| Weather & sky | **Nevada Skies** or a similar weather overhaul |

These transform the mood far more than textures alone.

## Stage 5 — ENB (optional, the cinematic layer)

An **ENB** preset adds modern post-processing — real ambient occlusion,
depth of field, color grading, better bloom. It's the closest thing to an
official "remaster" look, and it's the heaviest on performance.

- Install the matching **ENBSeries binary** for Fallout NV, then a **preset**
  built for it (Rudy ENB and others are popular).
- Expect to tune it for your GPU. Turn effects down if you lose frames.

## Stage 6 — Sort, test, enjoy

1. Run **LOOT** for a final load-order sort.
2. Launch through MO2 + NVSE, start a new game, play to Goodsprings.
3. If you crash: disable the **last** stage you added and re-test. That isolates
   the culprit fast — the reason we installed in stages.

---

## What this gets you vs. what it doesn't

- ✅ The **complete** game — all content — looking modern and running stable.
- ✅ Legal and reversible (MO2 changes nothing permanently).
- ❌ Not a new engine: it's still the original engine underneath, so some
   limits remain (that's what Path B / `engine/` aims at long-term).

## Recommended starting point

If you want one link to begin: do **Viva New Vegas** end-to-end for the
foundation (Stages 0–2), then come back here for Stages 3–5. That order
produces a stable, great-looking, complete New Vegas with the least pain.

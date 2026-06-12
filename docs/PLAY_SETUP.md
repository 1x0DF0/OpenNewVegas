# Playing & Modding Setup Guide

How to get the most out of your own copy of Fallout: New Vegas today, and how
to point the Open New Vegas tools at it. Examples below use this install path
(adjust if yours differs):

```
D:\SteamLibrary\steamapps\common\Fallout New Vegas
```

> Everything here operates on files **you own**. Nothing is downloaded from or
> uploaded to this project — we only patch/read your local installation.

---

## 1. The 4GB patch (fixes most crashes)

The game ships as a 32-bit exe without the Large Address Aware flag, capping
it at 2 GB of RAM — the single biggest cause of crashes, especially modded.
Flipping the LAA bit lets it use 4 GB on a 64-bit Windows.

**Option A — this repo's tool.** Build the engine (see `engine/README.md`),
then:

```bat
laa_patch.exe "D:\SteamLibrary\steamapps\common\Fallout New Vegas\FalloutNV.exe"
```

It writes a one-time backup (`FalloutNV.exe.laa.bak`) and flips the flag.
Run it again any time — it detects an already-patched exe and does nothing.

**Option B — the community standard.** The "FNV 4GB Patcher" on Nexus Mods
does the same thing and also chains NVSE loading automatically. If you're
going to install NVSE anyway (you should), Option B is the convenient route.

> ⚠️ Steam's *Verify integrity of game files* restores the original exe.
> Re-apply the patch after verifying or after a game update.

## 2. NVSE — the script extender

NVSE (today maintained as **xNVSE**) extends the game's scripting engine;
most serious mods require it. It's free and open source.

1. Download the latest release zip from the xNVSE GitHub releases page
   (search "xNVSE releases" — it's the actively maintained fork).
2. Extract the contents (the `.dll` files and `nvse_loader.exe`) directly into
   `D:\SteamLibrary\steamapps\common\Fallout New Vegas`
   — next to `FalloutNV.exe`, **not** into `Data`.
3. Steam copies: just launch the game through Steam as normal —
   `nvse_steam_loader.dll` hooks in automatically.
4. Verify: in the in-game console (`~`), type `GetNVSEVersion`.

## 3. Ultrawide / resolution fixes

The engine predates 21:9, so two layers need fixing — the render resolution
and the HUD/menus.

**Resolution + FOV** — edit `FalloutPrefs.ini` in
`Documents\My Games\FalloutNV\` (back it up first):

```ini
[Display]
iSize W=3440
iSize H=1440
```

and in the same `[Display]` section set the FOV to taste (90 is comfortable
at 21:9; the default is 75):

```ini
fDefaultWorldFOV=90
fDefault1stPersonFOV=55
```

If the launcher keeps resetting your values, set the same keys in
`Fallout_default.ini` in the game folder.

**HUD/UI stretch** — ini edits can't fix the stretched menus; use a UI mod.
The community staples are **Vanilla UI Plus** plus a widescreen/ultrawide UI
patch from Nexus (search "FNV ultrawide"). Install with a mod manager
(Mod Organizer 2 recommended).

**Recommended stability companions:** NVAC (New Vegas Anti-Crash) and the
NVTF (New Vegas Tick Fix) — both pair with the 4GB patch.

---

## 4. Point Open New Vegas at your install

Once the engine tools are built you can inspect your own game data:

```bat
esmdump.exe counts "D:\SteamLibrary\steamapps\common\Fallout New Vegas\Data\FalloutNV.esm"
esmdump.exe list   "D:\SteamLibrary\steamapps\common\Fallout New Vegas\Data\FalloutNV.esm" WRLD
bsatool.exe list   "D:\SteamLibrary\steamapps\common\Fallout New Vegas\Data\Fallout - Meshes.bsa"
```

And export the real Mojave terrain for the walking prototype:

```bat
worldexport.exe "D:\SteamLibrary\steamapps\common\Fallout New Vegas\Data\FalloutNV.esm" WastelandNV mojave_real.json 16
```

Copy `mojave_real.json` into the repo's `walk/` folder, serve the repo
locally (`python -m http.server 8080` from the repo root), and open:

```
http://localhost:8080/walk/?src=mojave_real.json
```

You're now walking on the real worldspace's terrain, streamed from data your
own copy of the game defined. The native walker (`engine/build/walker`)
currently renders the procedural Mojave; real-bundle support is next on the
roadmap.

### Building the tools on Windows

- **Visual Studio 2022** (free Community edition): open the `engine/` folder,
  CMake configure, build. Needs `zlib` (use vcpkg: `vcpkg install zlib sdl2`).
- **Or WSL/MSYS2**: `cmake -B build && cmake --build build` as on Linux.

# Download & Play

Want to walk the Mojave without installing a compiler? You're in the right
place. This page gets you from "zero" to "walking around" with a prebuilt
download — no Visual Studio, no CMake, no toolchain.

> ⚠️ **Unofficial fan project.** Open New Vegas is not affiliated with or
> endorsed by Bethesda Softworks, Obsidian Entertainment, or ZeniMax/Microsoft.
> It ships **no** Bethesda assets, data, or code. The engine only *reads* the
> game files **you already legally own**, using publicly documented formats —
> the same legal model proven by [OpenMW](https://openmw.org/) for Morrowind.
> You do **not** need the game to try the walking prototype; without it you
> simply walk a procedurally generated Mojave instead of the real terrain.

---

## 1. Where the downloads live

There are two ways to get prebuilt binaries, both produced by our GitHub
Actions CI (`.github/workflows/build.yml`):

- **Releases (recommended).** Once a version is tagged (`v*`), the CI attaches
  ready-to-play packages to the
  [**GitHub Releases page**](../../releases):
  - `open-new-vegas-<version>-windows-x64.zip` — Windows
  - `open-new-vegas-<version>-linux-x86_64.tar.gz` — Linux
- **CI artifacts (bleeding edge).** *Every* push to `main` also builds and
  uploads artifacts named `open-new-vegas-windows` and
  `open-new-vegas-linux`. Open the latest run under the repo's
  [**Actions tab**](../../actions), scroll to **Artifacts**, and download the
  one for your OS. (Note: downloading CI artifacts requires being signed in to
  GitHub.)

Each package contains the engine tools, the one-click launcher, and the docs:

```
walker(.exe)            the first-person walker — this is "the game"
esmdump(.exe)           inspect your FalloutNV.esm (record counts, worldspaces…)
bsatool(.exe)           list/extract .bsa asset archives
worldexport(.exe)       stitch real Mojave terrain from your .esm
export_heightmap(.exe)  export the procedural heightmap
laa_patch(.exe)         the 4GB / Large-Address-Aware patch for your game exe
play.bat / play.sh      the double-click launcher (see below)
SDL2.dll                (Windows only) runtime library the walker needs
PLAY_SETUP.md           4GB patch, NVSE, ultrawide, exporting your worldspace
DOWNLOAD.md             this guide
```

---

## 2. Play on Windows

1. Download `open-new-vegas-<version>-windows-x64.zip` from the
   [Releases page](../../releases).
2. **Right-click the zip → Extract All…** Extract the *whole* folder somewhere
   you can find it (e.g. your Desktop). Keep every file together — `play.bat`
   needs `walker.exe` and `SDL2.dll` sitting right next to it.
3. **Double-click `play.bat`.**

That's it. The launcher auto-detects your Fallout: New Vegas install from
Steam (it checks the usual library roots across drive letters and parses
`libraryfolders.vdf`), points the engine at it, and starts walking you through
the real Mojave terrain.

> 🛡️ Windows SmartScreen may warn about an unrecognized app the first time
> (the binaries aren't code-signed). Click **More info → Run anyway**. The
> source is fully open — you can build it yourself if you prefer (see
> `engine/README.md`).

### If the game isn't auto-detected

The walker still launches — you just get **procedural** terrain instead of the
real Mojave. To use the real terrain, tell the engine where your install is.
Point `ONV_FNV_PATH` at the install **root** (the folder that *contains* the
`Data` subfolder), for example:

```bat
set "ONV_FNV_PATH=D:\SteamLibrary\steamapps\common\Fallout New Vegas"
```

Then run `play.bat` again from that same Command Prompt window. To make it
permanent across reboots, use `setx` instead:

```bat
setx ONV_FNV_PATH "D:\SteamLibrary\steamapps\common\Fallout New Vegas"
```

(Note the quotes — the path has spaces in it.)

---

## 3. Play on Linux

1. Download `open-new-vegas-<version>-linux-x86_64.tar.gz` from the
   [Releases page](../../releases).
2. Extract and enter it:

   ```bash
   tar -xzf open-new-vegas-*-linux-x86_64.tar.gz
   cd dist   # or wherever it extracted to
   ```

3. Run the launcher:

   ```bash
   ./play.sh
   ```

The launcher resolves itself, finds `./walker`, and auto-detects a native or
Flatpak Steam install (it checks `~/.steam`, `~/.local/share/Steam`, the
Flatpak Steam path, and parses `libraryfolders.vdf`). Proton-run copies are
fine too — the install files still live under `steamapps/common/`.

> If `walker` isn't executable, the launcher tries to `chmod +x` it for you;
> if that fails, run `chmod +x walker play.sh` yourself.

### If the game isn't auto-detected

As on Windows, the walker falls back to procedural terrain and prints how to
fix it. Point `ONV_FNV_PATH` at the install root (the directory containing
`Data`), e.g.:

```bash
export ONV_FNV_PATH="$HOME/.steam/steam/steamapps/common/Fallout New Vegas"
./play.sh
```

Add that `export` line to your `~/.bashrc` to make it stick.

---

## 4. What to expect

You drop into the Mojave in first person. Controls:

- **WASD** — move, **mouse** — look
- **Shift** — sprint, **Space** — jump (gravity + terrain collision are on)

Two flavors of terrain depending on whether the engine found your game:

- **Game found** → you walk the **real** worldspace terrain, stitched live
  from the `LAND` heightmaps in your own `FalloutNV.esm`.
- **Game not found** → you walk a **procedural** Mojave generated by the
  engine's deterministic terrain model. Still a real walk, just not the
  one-to-one world. No game files required.

This is an early **walking prototype** — terrain and movement, not yet NPCs,
quests, or the full game. See the [roadmap](ROADMAP.md) for where it's headed.

---

## 5. Getting the most out of your install

For the full setup — applying the 4GB (Large Address Aware) patch, installing
NVSE, ultrawide/resolution fixes, and exporting a specific worldspace's real
terrain — see [**PLAY_SETUP.md**](PLAY_SETUP.md). Examples there use the same
`D:\SteamLibrary\steamapps\common\Fallout New Vegas` install path.

---

## 6. Prefer to build it yourself?

You don't need the prebuilt downloads at all — the project builds from source
with just CMake, a C++17 compiler, SDL2, and zlib. See the build instructions
in [`README.md`](../README.md) and `engine/README.md`. The CI workflow
(`.github/workflows/build.yml`) is also a working, copy-pasteable reference for
exactly which packages to install on Linux (`apt`) and Windows (`vcpkg`).

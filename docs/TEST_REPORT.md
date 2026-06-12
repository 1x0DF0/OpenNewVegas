# Open New Vegas — Engine Test Report

**Verdict: PASS.** The engine builds clean, all test suites pass, every test
binary is AddressSanitizer/UBSan-clean, all four file-format parsers survive an
adversarial input sweep without crashing or over-allocating, and the walker
runs in all three modes (procedural, demo, synthetic real-terrain).

| | |
|---|---|
| Commit | `7716531` (+ parser hardening in this commit) |
| Compiler | g++ (Ubuntu) 13.3.0 |
| CMake | 3.28.3 |
| Engine source | ~8,900 lines C++ (`engine/**/*.{cpp,hpp}`) |
| `TODO/FIXME` markers | 0 |

Reproduce everything with: `scripts/full_engine_check.sh`.

---

## 1. Clean Release build

All targets build under `-DCMAKE_BUILD_TYPE=Release` with **no compiler
warnings or errors**. Targets: libraries `onv_terrain`, `onv_formats`,
`onv_platform`, `onv_records`, `onv_assets`, `onv_render`, `onv_scene`; tools
`export_heightmap`, `bsatool`, `esmdump`, `worldexport`, `scenedump`,
`laa_patch`; the `walker`; and 9 test binaries.

## 2. Test suite (ctest)

**9 / 9 suites pass.**

| Suite | Covers |
|-------|--------|
| `format_tests` | BSA archive + ESM plugin readers (synthetic fixtures) |
| `records_tests` | WRLD/CELL/LAND/REFR/STAT decoding |
| `nif_tests` | NIF mesh geometry, UVs, diffuse texture path |
| `dds_tests` | DXT1/3/5 + uncompressed decode (exact pixels) |
| `data_files_tests` | Data VFS: BSA mount + loose-file precedence |
| `scene_tests` | scene assembly + per-cell `SceneStreamer` |
| `texture_cache_tests` | texture path → decoded RGBA, caching |
| `integration_tests` | **full pipeline** over a synthetic mini-install |
| `robustness_tests` | adversarial parser inputs (see §4) |

The integration test is the headline: it synthesizes a complete mini "game
install" (ESM worldspace + BSA archive containing a NIF and a DDS) and asserts
every stage of the chain — install locator → world load → archive VFS → scene
streaming → mesh decode (vertices/UVs/texture path) → texture decode to a
sentinel pixel.

## 3. Sanitizers (AddressSanitizer + UBSan)

Every test binary was rebuilt with `-fsanitize=address,undefined` and run
directly. **All 9 are clean** — no memory errors, no undefined behavior
reported.

## 4. Parser robustness (adversarial)

488 adversarial cases across the four parsers — truncation sweeps (every prefix
length) plus targeted corruption (bad magic, absurd counts, offsets past EOF,
garbage compression streams, oversized declared sizes). **Result: no crashes,
no safety violations, 0 findings.**

| Parser | Cases | Exceptions caught |
|--------|------:|------:|
| BSA | 86 | 86 |
| ESM | 91 | 90¹ |
| NIF | 167 | 167 |
| DDS | 144 | 144 |

¹ One ESM case legitimately *succeeds* (a benign truncation the streaming
walker may stop on cleanly) rather than throwing; that is allowed.

### Bugs found and fixed during this audit

The robustness sweep surfaced real hardening gaps, all fixed in this commit:

1. **Unbounded allocation from attacker-controlled decompressed size** — a
   crafted BSA file entry or compressed ESM record could declare a ~2 GB
   decompressed size and force that allocation before zlib failed. Both paths
   now reject any declared size beyond zlib's plausible expansion ratio
   (~1032:1) of the actual compressed bytes. (`bsa.cpp`, `esm.cpp`)
2. **Unbounded allocation from DDS dimensions** — a 128-byte DDS declaring
   65535×65535 forced a ~17 GB pixel buffer before any surface data was read.
   `decode()` now caps dimensions (≤16384) and rejects an output larger than
   the surface bytes can justify. (`dds.cpp`)
3. **BSA header counts unvalidated** — `folderCount`/`fileCount`/
   `totalFileNameLength` are now bounded by the file's physical capacity.
   (`bsa.cpp`)
4. **Dangling `XXXX` size override in ESM** — a record ending on an `XXXX`
   subrecord with no following subrecord now throws instead of silently
   ignoring it. (`esm.cpp`)

A separate **bug in the robustness test itself** was also fixed: three BSA
corruption cases poked the wrong header byte offsets (off by the two
name-length fields), so they weren't exercising the fields they claimed to.

## 5. CLI tools

All six tools run and behave correctly against fixtures: `esmdump`
(counts/list), `bsatool` (list/extract), `worldexport` (emits valid JSON
terrain), `export_heightmap` (valid 16-bit PGM), `scenedump`, and `laa_patch`
(sets the LAA flag, writes a backup, idempotent on re-run).

## 6. Walker runtime (headless via xvfb)

| Mode | Result |
|------|--------|
| Procedural | Renders; reports "install not found — using procedural". |
| `--demo` | Renders textured demo objects (1 material batch). |
| Synthetic real-terrain | Detects `ONV_FNV_PATH`, loads worldspace "TestWasteland" (33×33 posts), reports object streaming ready (1 populated cell). |

Each produced a full-resolution framebuffer (1500×950).

## Recommendations

1. **Wire `scripts/full_engine_check.sh` into CI** so the sanitizer pass and
   robustness sweep gate every change, not just local runs.
2. The NIF diffuse-texture association is a documented approximation
   (file's-first-texture-set); revisit when multi-material meshes matter.
3. Real-game-data paths (actual `FalloutNV.esm`/BSAs) remain verified only
   against synthetic fixtures here — validate on a real install when available.

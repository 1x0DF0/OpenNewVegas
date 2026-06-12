// Open New Vegas — native first-person walker (C++ / SDL2 / OpenGL).
//
// Walk the procedural Mojave with the same deterministic terrain model the
// rest of the engine uses. This is the native counterpart of walk/index.html
// and the seed of the real renderer (fixed-function GL for now; a modern
// pipeline replaces it when NIF/DDS decoding lands).
//
//   walker                         windowed, WASD + mouse look
//   walker --screenshot out.ppm    render the spawn view offscreen and exit
//
// Coordinates: meters. +x east, -z north, +y up. 1 world unit = 350 m.
//
// On startup the walker tries to locate the user's own Fallout: New Vegas
// install and drop them into the REAL Mojave terrain stitched from their
// FalloutNV.esm. If the game isn't found (or fails to load) it falls back to
// the deterministic procedural terrain model.

#include "../assets/data_files.hpp"
#include "../platform/game_locator.hpp"
#include "../records/records.hpp"
#include "../scene/scene.hpp"
#include "../terrain/terrain.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float UNIT_M = 350.0f;
constexpr float EYE = 1.7f;
constexpr float WALK = 5.5f, SPRINT = 13.0f;
constexpr float GRAVITY = -16.0f, JUMP = 5.5f;
constexpr float CHUNK = 256.0f;
constexpr int RES = 32;
constexpr int VIEW_CHUNKS = 7;
constexpr float FOG_END = 1650.0f;
const float SKY[3] = {0.72f, 0.78f, 0.84f};

// Walking-scale detail bumps (identical formula to walk/js/walker.js so the
// two prototypes show the same ground)
float detail(float x, float n) {
    return 1.4f * std::sin(x * 0.041f + std::sin(n * 0.027f)) *
                  std::sin(n * 0.037f + std::sin(x * 0.023f)) +
           0.5f * std::sin(x * 0.13f + n * 0.094f) +
           0.22f * std::sin(x * 0.31f - n * 0.27f);
}

// ── Runtime terrain provider ────────────────────────────────────────────────
// Both the mesh build and the ground/collision sampling go through the active
// provider so the rendered ground and what the player walks on stay consistent.
// Convention: sample(xEast, zNorth) with northing n = -z, matching the
// procedural model and the walker's coordinate system.
struct HeightProvider {
    virtual ~HeightProvider() = default;
    virtual float sample(float xEast, float zNorth) const = 0;
    virtual const char* name() const = 0;
};

// Existing behavior: deterministic procedural Mojave + walking-scale bumps.
struct ProceduralProvider : HeightProvider {
    float sample(float x, float z) const override {
        const float n = -z; // northing
        return static_cast<float>(onv::elevation(x / UNIT_M, n / UNIT_M)) +
               detail(x, n);
    }
    const char* name() const override { return "procedural"; }
};

// Real Mojave terrain: a worldspace's LAND heightmaps stitched into one grid,
// in meters, sampled bilinearly. Stitching mirrors tools/worldexport.cpp.
struct RealTerrainProvider : HeightProvider {
    std::vector<float> grid; // row-major, row 0 = SOUTH edge, in meters
    int width = 0, height = 0;
    float spacing = 1.0f;    // meters between posts
    float originX = 0.0f;    // east-coordinate (m) of the west edge (col 0)
    float originY = 0.0f;    // north-coordinate (m) of the south edge (row 0)
    std::string worldEditorId;
    std::size_t cellCount = 0;

    float at(int col, int row) const {
        col = std::clamp(col, 0, width - 1);
        row = std::clamp(row, 0, height - 1);
        return grid[static_cast<std::size_t>(row) * width + col];
    }

    float sample(float x, float z) const override {
        const float n = -z; // northing, matching worldexport's "north" axis
        const float fc = (x - originX) / spacing;       // column (east)
        const float fr = (n - originY) / spacing;       // row (north)
        const int c0 = static_cast<int>(std::floor(fc));
        const int r0 = static_cast<int>(std::floor(fr));
        const float tx = std::clamp(fc - c0, 0.0f, 1.0f);
        const float tz = std::clamp(fr - r0, 0.0f, 1.0f);
        const float h00 = at(c0, r0), h10 = at(c0 + 1, r0);
        const float h01 = at(c0, r0 + 1), h11 = at(c0 + 1, r0 + 1);
        const float a = h00 + (h10 - h00) * tx;
        const float b = h01 + (h11 - h01) * tx;
        return a + (b - a) * tz;
    }

    const char* name() const override { return "real (FalloutNV.esm)"; }

    // Center of the stitched grid, in walker coordinates (x east, z = -north).
    void centerSpawn(float& outX, float& outZ) const {
        outX = originX + 0.5f * (width - 1) * spacing;
        const float north = originY + 0.5f * (height - 1) * spacing;
        outZ = -north;
    }
};

// Build a RealTerrainProvider from a plugin. Throws on parse / no-terrain.
std::unique_ptr<RealTerrainProvider> buildRealTerrain(const std::string& esmPath) {
    using namespace onv::records;
    const World world = loadWorld(esmPath);

    // Pick the main exterior worldspace: prefer "WastelandNV", else the one
    // with the most cells that carry LAND terrain.
    const Worldspace* chosen = world.findWorldspace("WastelandNV");
    if (!chosen) {
        std::size_t best = 0;
        for (const auto& w : world.worldspaces) {
            std::size_t landCells = 0;
            for (const auto& [grid, cell] : w.cells)
                if (cell.land) ++landCells;
            if (landCells > best) {
                best = landCells;
                chosen = &w;
            }
        }
    }
    if (!chosen)
        throw std::runtime_error("no worldspace with terrain found in plugin");

    // Bounds over cells that have LAND.
    std::int32_t minX = std::numeric_limits<std::int32_t>::max(), minY = minX;
    std::int32_t maxX = std::numeric_limits<std::int32_t>::min(), maxY = maxX;
    std::size_t landCells = 0;
    for (const auto& [grid, cell] : chosen->cells) {
        if (!cell.land) continue;
        ++landCells;
        minX = std::min(minX, grid.first);
        maxX = std::max(maxX, grid.first);
        minY = std::min(minY, grid.second);
        maxY = std::max(maxY, grid.second);
    }
    if (landCells == 0)
        throw std::runtime_error("chosen worldspace has no LAND cells");

    auto rt = std::make_unique<RealTerrainProvider>();
    rt->worldEditorId = chosen->editorId.empty() ? "<unnamed>" : chosen->editorId;
    rt->cellCount = landCells;

    // Stitch: cells share edge posts, so the grid is 32 posts per cell + 1.
    const int cellsX = maxX - minX + 1, cellsY = maxY - minY + 1;
    rt->width = cellsX * (LAND_GRID - 1) + 1;
    rt->height = cellsY * (LAND_GRID - 1) + 1;
    rt->spacing = static_cast<float>(CELL_SIZE_UNITS / (LAND_GRID - 1) *
                                     UNITS_TO_METERS);
    rt->originX = static_cast<float>(minX * CELL_SIZE_UNITS * UNITS_TO_METERS);
    rt->originY = static_cast<float>(minY * CELL_SIZE_UNITS * UNITS_TO_METERS);

    rt->grid.assign(static_cast<std::size_t>(rt->width) * rt->height, 0.0f);
    for (const auto& [grid, cell] : chosen->cells) {
        if (!cell.land) continue;
        const int baseX = (cell.gridX - minX) * (LAND_GRID - 1);
        const int baseY = (cell.gridY - minY) * (LAND_GRID - 1);
        for (int r = 0; r < LAND_GRID; ++r)
            for (int c = 0; c < LAND_GRID; ++c)
                rt->grid[static_cast<std::size_t>(baseY + r) * rt->width +
                         baseX + c] =
                    static_cast<float>(cell.land->at(r, c) * UNITS_TO_METERS);
    }
    return rt;
}

// The active provider (set in main). buildChunk + collision read it.
const HeightProvider* gProvider = nullptr;

float sampleHeight(float x, float z) { return gProvider->sample(x, z); }

void elevColor(float e, float* rgb) {
    static const float stops[6][4] = {
        {330, 196, 174, 132}, {600, 186, 154, 110}, {950, 158, 124, 88},
        {1400, 126, 102, 80}, {1950, 110, 104, 98}, {2400, 235, 236, 240}};
    int i = 0;
    while (i < 4 && e > stops[i + 1][0]) ++i;
    const float t = std::clamp((e - stops[i][0]) / (stops[i + 1][0] - stops[i][0]),
                               0.0f, 1.0f);
    for (int c = 0; c < 3; ++c)
        rgb[c] = (stops[i][c + 1] + (stops[i + 1][c + 1] - stops[i][c + 1]) * t) / 255.0f;
}

// Interleaved chunk mesh: pos(3) normal(3) color(3)
struct Chunk {
    std::vector<float> verts;
};

std::vector<unsigned short> chunkIndices() {
    std::vector<unsigned short> idx;
    for (int j = 0; j < RES; ++j)
        for (int i = 0; i < RES; ++i) {
            const unsigned short a = j * (RES + 1) + i, b = a + 1;
            const unsigned short c = a + RES + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    return idx;
}

Chunk buildChunk(int cx, int cz) {
    Chunk ch;
    ch.verts.reserve((RES + 1) * (RES + 1) * 9);
    const float step = CHUNK / RES, eps = 2.0f;
    for (int j = 0; j <= RES; ++j)
        for (int i = 0; i <= RES; ++i) {
            const float x = cx * CHUNK + i * step;
            const float z = cz * CHUNK + j * step;
            const float h = sampleHeight(x, z);
            const float hx = sampleHeight(x + eps, z) - sampleHeight(x - eps, z);
            const float hz = sampleHeight(x, z + eps) - sampleHeight(x, z - eps);
            float nx = -hx / (2 * eps), ny = 1.0f, nz = -hz / (2 * eps);
            const float il = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            float rgb[3];
            elevColor(h, rgb);
            const float v[9] = {x, h, z, nx * il, ny * il, nz * il,
                                rgb[0], rgb[1], rgb[2]};
            ch.verts.insert(ch.verts.end(), v, v + 9);
        }
    return ch;
}

struct Player {
    float x = 23 * UNIT_M, z = -35 * UNIT_M, y = 0; // spawn: Goodsprings
    float yaw = 0.8f, pitch = -0.04f, vy = 0;
    bool grounded = true;
};

void loadPerspective(float fovY, float aspect, float nearP, float farP) {
    const float f = 1.0f / std::tan(fovY / 2), nf = 1.0f / (nearP - farP);
    const float m[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0,
                         0, 0, (farP + nearP) * nf, -1, 0, 0, 2 * farP * nearP * nf, 0};
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
}

void loadView(const Player& p) {
    const float cy = std::cos(p.yaw), sy = std::sin(p.yaw);
    const float cp = std::cos(p.pitch), sp = std::sin(p.pitch);
    float m[16] = {cy, sy * sp, sy * cp, 0,
                   0, cp, -sp, 0,
                   -sy, cy * sp, cy * cp, 0,
                   0, 0, 0, 1};
    m[12] = -(m[0] * p.x + m[4] * p.y + m[8] * p.z);
    m[13] = -(m[1] * p.x + m[5] * p.y + m[9] * p.z);
    m[14] = -(m[2] * p.x + m[6] * p.y + m[10] * p.z);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

bool writePpm(const std::string& path, int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int row = h - 1; row >= 0; --row) // GL rows are bottom-up
        std::fwrite(px.data() + static_cast<std::size_t>(row) * w * 3, 1, w * 3, f);
    std::fclose(f);
    return true;
}

// ── Placed objects ──────────────────────────────────────────────────────────
// Game space is X east, Y north, Z up, in game units. The walker renders in
// meters with X east, Y up, Z = -north. This maps one to the other.
void gameUnitsToWalker(float ux, float uy, float uz,
                       float& wx, float& wy, float& wz) {
    const float U = static_cast<float>(onv::records::UNITS_TO_METERS);
    wx = ux * U;
    wy = uz * U;
    wz = -uy * U;
}

// Flatten a scene's instances into one lit, flat-shaded triangle soup in
// walker meters: 6 floats per vertex (position3, normal3). Each instance's
// local mesh is scaled, rotated (Rz*Ry*Rx), translated by the REFR position,
// then converted to walker space.
std::vector<float> bakeScene(const onv::scene::Scene& sc) {
    std::vector<float> out;
    for (const auto& inst : sc.instances) {
        const auto it = sc.models.find(inst.modelPath);
        if (it == sc.models.end()) continue;

        const float cx = std::cos(inst.rotX), sx = std::sin(inst.rotX);
        const float cy = std::cos(inst.rotY), sy = std::sin(inst.rotY);
        const float cz = std::cos(inst.rotZ), sz = std::sin(inst.rotZ);
        // R = Rz * Ry * Rx (row-major 3x3)
        const float R[9] = {
            cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
            sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
            -sy,     cy * sx,                cy * cx};

        auto place = [&](float lx, float ly, float lz,
                         float& ox, float& oy, float& oz) {
            lx *= inst.scale; ly *= inst.scale; lz *= inst.scale;
            const float rx = R[0] * lx + R[1] * ly + R[2] * lz;
            const float ry = R[3] * lx + R[4] * ly + R[5] * lz;
            const float rz = R[6] * lx + R[7] * ly + R[8] * lz;
            gameUnitsToWalker(rx + inst.x, ry + inst.y, rz + inst.z, ox, oy, oz);
        };

        for (const auto& mesh : it->second.meshes) {
            for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                float v[3][3];
                for (int k = 0; k < 3; ++k) {
                    const std::uint16_t idx = mesh.indices[t + k];
                    if (static_cast<std::size_t>(idx) * 3 + 2 >= mesh.vertices.size())
                        goto next_tri;
                    place(mesh.vertices[idx * 3], mesh.vertices[idx * 3 + 1],
                          mesh.vertices[idx * 3 + 2], v[k][0], v[k][1], v[k][2]);
                }
                {
                    // Flat face normal.
                    const float ax = v[1][0] - v[0][0], ay = v[1][1] - v[0][1],
                                az = v[1][2] - v[0][2];
                    const float bx = v[2][0] - v[0][0], by = v[2][1] - v[0][1],
                                bz = v[2][2] - v[0][2];
                    float nx = ay * bz - az * by, ny = az * bx - ax * bz,
                          nz = ax * by - ay * bx;
                    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                    for (int k = 0; k < 3; ++k) {
                        out.insert(out.end(),
                                   {v[k][0], v[k][1], v[k][2], nx, ny, nz});
                    }
                }
            next_tri:;
            }
        }
    }
    return out;
}

// A small demo scene: a scatter of boxes resting on the terrain near the
// spawn, fed through the REAL scene/instance/bake path (no game files needed)
// so object rendering can be exercised and screenshotted on its own.
onv::scene::Scene buildDemoScene() {
    const float U = static_cast<float>(onv::records::UNITS_TO_METERS);
    const float halfMeters = 1.25f;          // ~2.5 m boxes
    const float h = halfMeters / U;          // half-extent in game units

    // Cube model: 8 corners, 12 triangles, vertices in game units.
    onv::nif::Mesh cube;
    const float c[8][3] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h}};
    for (auto& corner : c)
        cube.vertices.insert(cube.vertices.end(),
                             {corner[0], corner[1], corner[2]});
    const std::uint16_t faces[12][3] = {
        {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
        {1, 5, 6}, {1, 6, 2}, {2, 6, 7}, {2, 7, 3}, {3, 7, 4}, {3, 4, 0}};
    for (auto& f : faces) cube.indices.insert(cube.indices.end(), {f[0], f[1], f[2]});

    onv::scene::Scene sc;
    onv::scene::Model model;
    model.meshes.push_back(std::move(cube));
    sc.models.emplace("demo\\cube.nif", std::move(model));
    sc.worldspaceEditorId = "DemoLand";

    // Scatter boxes in a patch laid out IN FRONT of the spawn view, each
    // resting on the terrain. Spawn yaw is 0.8 rad; forward and right vectors
    // match the walker's movement basis.
    const float sx = 23 * UNIT_M, sz = -35 * UNIT_M;
    const float yaw = 0.8f;
    const float fwdX = std::sin(yaw), fwdZ = -std::cos(yaw);
    const float rightX = std::cos(yaw), rightZ = std::sin(yaw);
    std::uint32_t id = 0x1000;
    for (int row = 0; row < 8; ++row)        // depth ahead
        for (int col = -3; col <= 3; ++col) { // lateral spread
            const float depth = 8.0f + row * 5.5f;
            const float lateral = col * 5.0f;
            const float wx = sx + fwdX * depth + rightX * lateral;
            const float wz = sz + fwdZ * depth + rightZ * lateral;
            const float ground = sampleHeight(wx, wz);
            const float wy = ground + halfMeters;
            onv::scene::Instance inst;
            inst.refrFormId = id++;
            inst.baseFormId = 0x100;
            inst.modelPath = "demo\\cube.nif";
            // Walker -> game units (inverse of gameUnitsToWalker).
            inst.x = wx / U;
            inst.y = -wz / U;
            inst.z = wy / U;
            inst.rotZ = 0.3f * (row + col); // a little variety
            inst.scale = 1.0f;
            sc.instances.push_back(std::move(inst));
        }
    return sc;
}

} // namespace

int main(int argc, char** argv) {
    std::string screenshotPath;
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshotPath = argv[i + 1];
        if (std::strcmp(argv[i], "--demo") == 0) demo = true;
    }
    const bool headless = !screenshotPath.empty();

    // ── Choose terrain provider: real Mojave if the game is found, else
    //    procedural. Loading is wrapped so a parse error never crashes us. ──
    ProceduralProvider procedural;
    std::unique_ptr<RealTerrainProvider> realTerrain;
    bool spawnAtRealCenter = false;
    if (auto esm = onv::platform::findFalloutNVMasterEsm()) {
        std::printf("Fallout: New Vegas found at %s\n", esm->c_str());
        try {
            realTerrain = buildRealTerrain(*esm);
            gProvider = realTerrain.get();
            spawnAtRealCenter = true;
            std::printf("Loaded real worldspace \"%s\" (%zu LAND cells, "
                        "%d x %d posts) — walking the real Mojave.\n",
                        realTerrain->worldEditorId.c_str(),
                        realTerrain->cellCount, realTerrain->width,
                        realTerrain->height);
        } catch (const std::exception& e) {
            std::printf("Could not load real terrain (%s) — "
                        "using procedural Mojave.\n", e.what());
            realTerrain.reset();
            gProvider = &procedural;
        }
    } else {
        std::printf("Fallout: New Vegas install not found — "
                    "using procedural Mojave (set ONV_FNV_PATH to override).\n");
        gProvider = &procedural;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int W = 1500, H = 950;
    SDL_Window* win = SDL_CreateWindow(
        "Open New Vegas — Walker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H, SDL_WINDOW_OPENGL | (headless ? SDL_WINDOW_HIDDEN : 0));
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    if (!headless) SDL_SetRelativeMouseMode(SDL_TRUE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    const float ambient[4] = {0.45f, 0.45f, 0.45f, 1};
    const float diffuse[4] = {0.75f, 0.73f, 0.68f, 1};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 150.0f);
    glFogf(GL_FOG_END, FOG_END);
    const float fogColor[4] = {SKY[0], SKY[1], SKY[2], 1};
    glFogfv(GL_FOG_COLOR, fogColor);

    const auto indices = chunkIndices();
    std::map<std::pair<int, int>, Chunk> chunks;
    Player p;
    if (spawnAtRealCenter && realTerrain)
        realTerrain->centerSpawn(p.x, p.z); // center of the real terrain grid
    p.y = sampleHeight(p.x, p.z) + EYE;

    auto updateChunks = [&](int budget) {
        const int ccx = static_cast<int>(std::floor(p.x / CHUNK));
        const int ccz = static_cast<int>(std::floor(p.z / CHUNK));
        std::vector<std::pair<int, std::pair<int, int>>> want;
        for (int dz = -VIEW_CHUNKS; dz <= VIEW_CHUNKS; ++dz)
            for (int dx = -VIEW_CHUNKS; dx <= VIEW_CHUNKS; ++dx) {
                if (dx * dx + dz * dz > VIEW_CHUNKS * VIEW_CHUNKS + 2) continue;
                const auto key = std::make_pair(ccx + dx, ccz + dz);
                if (!chunks.count(key)) want.push_back({dx * dx + dz * dz, key});
            }
        std::sort(want.begin(), want.end());
        for (int i = 0; i < std::min<int>(budget, want.size()); ++i)
            chunks.emplace(want[i].second,
                           buildChunk(want[i].second.first, want[i].second.second));
        for (auto it = chunks.begin(); it != chunks.end();) {
            const int dx = it->first.first - ccx, dz = it->first.second - ccz;
            if (dx * dx + dz * dz > (VIEW_CHUNKS + 2) * (VIEW_CHUNKS + 2))
                it = chunks.erase(it);
            else
                ++it;
        }
    };
    updateChunks(1000); // full spawn area

    // ── Placed objects: demo scatter, or real statics from the game files ──
    onv::scene::Scene scene;
    if (demo) {
        scene = buildDemoScene();
    } else if (realTerrain) {
        try {
            const auto dataDir = onv::platform::findFalloutNVData();
            const auto esmPath = onv::platform::findFalloutNVMasterEsm();
            if (dataDir && esmPath) {
                onv::assets::DataFiles vfs(*dataDir);
                const auto world = onv::records::loadWorld(*esmPath);
                const auto loader = onv::scene::makeNifModelLoader(vfs);
                // Limited to cells near the worldspace origin for this first
                // pass (object streaming by player position comes later).
                scene = onv::scene::buildScene(world, realTerrain->worldEditorId,
                                               loader, 3);
            }
        } catch (const std::exception& e) {
            std::printf("Object loading skipped (%s).\n", e.what());
        }
    }
    const std::vector<float> objectVerts = bakeScene(scene);
    if (!objectVerts.empty())
        std::printf("Placed %zu objects (%zu unique models, %zu triangles).\n",
                    scene.instances.size(), scene.models.size(),
                    objectVerts.size() / 18);

    bool running = true;
    Uint32 prev = SDL_GetTicks();
    int frames = 0;
    Uint32 fpsT0 = prev;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            if (ev.type == SDL_MOUSEMOTION && !headless) {
                p.yaw += ev.motion.xrel * 0.0024f;
                p.pitch = std::clamp(p.pitch - ev.motion.yrel * 0.0024f,
                                     -1.5f, 1.5f);
            }
        }

        const Uint32 now = SDL_GetTicks();
        const float dt = std::min(0.05f, (now - prev) / 1000.0f);
        prev = now;

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float speed = keys[SDL_SCANCODE_LSHIFT] ? SPRINT : WALK;
        float mx = 0, mz = 0;
        if (keys[SDL_SCANCODE_W]) mz += 1;
        if (keys[SDL_SCANCODE_S]) mz -= 1;
        if (keys[SDL_SCANCODE_A]) mx -= 1;
        if (keys[SDL_SCANCODE_D]) mx += 1;
        const float ml = std::hypot(mx, mz);
        if (ml > 0) { mx /= ml; mz /= ml; }
        const float sy = std::sin(p.yaw), cy = std::cos(p.yaw);
        p.x += (mx * cy + mz * sy) * speed * dt;
        p.z += (mx * sy - mz * cy) * speed * dt;

        const float ground = sampleHeight(p.x, p.z) + EYE;
        p.vy += GRAVITY * dt;
        if (keys[SDL_SCANCODE_SPACE] && p.grounded) { p.vy = JUMP; p.grounded = false; }
        p.y += p.vy * dt;
        if (p.y <= ground) { p.y = ground; p.vy = 0; p.grounded = true; }

        updateChunks(4);

        glClearColor(SKY[0], SKY[1], SKY[2], 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        loadPerspective(1.15f, static_cast<float>(W) / H, 0.3f, FOG_END * 1.6f);
        loadView(p);

        const float sunDir[4] = {-0.55f, 0.7f, -0.45f, 0}; // directional, world space
        glLightfv(GL_LIGHT0, GL_POSITION, sunDir);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        for (const auto& [key, ch] : chunks) {
            const float* v = ch.verts.data();
            glVertexPointer(3, GL_FLOAT, 36, v);
            glNormalPointer(GL_FLOAT, 36, v + 3);
            glColorPointer(3, GL_FLOAT, 36, v + 6);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()),
                           GL_UNSIGNED_SHORT, indices.data());
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        // Placed objects (lit, flat-shaded, single stony material color)
        if (!objectVerts.empty()) {
            glColor3f(0.52f, 0.47f, 0.42f);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glVertexPointer(3, GL_FLOAT, 24, objectVerts.data());
            glNormalPointer(GL_FLOAT, 24, objectVerts.data() + 3);
            glDrawArrays(GL_TRIANGLES, 0,
                         static_cast<GLsizei>(objectVerts.size() / 6));
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
        }

        // Water plane
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        const float S = FOG_END * 1.5f, wl = static_cast<float>(onv::WATER_LEVEL);
        glColor4f(0.16f, 0.33f, 0.43f, 0.62f);
        glBegin(GL_QUADS);
        glVertex3f(p.x - S, wl, p.z - S);
        glVertex3f(p.x + S, wl, p.z - S);
        glVertex3f(p.x + S, wl, p.z + S);
        glVertex3f(p.x - S, wl, p.z + S);
        glEnd();
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);

        SDL_GL_SwapWindow(win);

        if (++frames % 30 == 0) {
            char title[160];
            std::snprintf(title, sizeof title,
                          "Open New Vegas — Walker | world (%.1f, %.1f) | elev %.0f m"
                          " | %.0f fps",
                          p.x / UNIT_M, -p.z / UNIT_M, p.y - EYE,
                          30000.0f / std::max(1u, now - fpsT0));
            SDL_SetWindowTitle(win, title);
            fpsT0 = now;
        }

        if (headless && frames >= 3) {
            glFinish();
            const bool ok = writePpm(screenshotPath, W, H);
            std::printf("%s %s\n", ok ? "wrote" : "FAILED to write",
                        screenshotPath.c_str());
            running = false;
        }
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

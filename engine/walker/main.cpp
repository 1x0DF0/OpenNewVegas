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

#include "../terrain/terrain.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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

float sampleHeight(float x, float z) {
    const float n = -z; // northing
    return static_cast<float>(onv::elevation(x / UNIT_M, n / UNIT_M)) + detail(x, n);
}

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

} // namespace

int main(int argc, char** argv) {
    std::string screenshotPath;
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], "--screenshot") == 0) screenshotPath = argv[i + 1];
    const bool headless = !screenshotPath.empty();

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

// Open New Vegas — engine-side terrain model (port of map/js/terrain.js).

#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace onv {
namespace {

constexpr std::uint32_t SEED = 0x57e6a5; // "Vegas" — must match terrain.js

// ── Seeded noise (bit-identical to the JS hash) ───────────────────────────
// Unsigned arithmetic wraps mod 2^32, matching the JS `| 0` / Math.imul path.
double hash2(std::int32_t ix, std::int32_t iy) {
    std::uint32_t h = static_cast<std::uint32_t>(ix) * 374761393u +
                      static_cast<std::uint32_t>(iy) * 668265263u +
                      SEED * 144665u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h / 4294967295.0;
}

double smoothstep(double t) { return t * t * (3.0 - 2.0 * t); }

double valueNoise(double x, double y) {
    const double fxf = std::floor(x), fyf = std::floor(y);
    const auto ix = static_cast<std::int32_t>(fxf);
    const auto iy = static_cast<std::int32_t>(fyf);
    const double fx = smoothstep(x - fxf), fy = smoothstep(y - fyf);
    const double a = hash2(ix, iy), b = hash2(ix + 1, iy);
    const double c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a + (b - a) * fx) * (1.0 - fy) + (c + (d - c) * fx) * fy;
}

double fbm(double x, double y, int octaves) {
    double sum = 0.0, amp = 0.5, freq = 1.0, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * (valueNoise(x * freq, y * freq) * 2.0 - 1.0);
        norm += amp;
        amp *= 0.5;
        freq *= 2.07;
    }
    return sum / norm;
}

// ── Geometry helpers ──────────────────────────────────────────────────────
struct Pt { double x, y; };

double distToSegment(double px, double py, Pt a, Pt b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 == 0.0 ? 0.0 : ((px - a.x) * dx + (py - a.y) * dy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const double ex = a.x + t * dx - px, ey = a.y + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

double distToPolyline(double px, double py, const std::vector<Pt>& pts) {
    double d = 1e30;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        d = std::min(d, distToSegment(px, py, pts[i], pts[i + 1]));
    return d;
}

double gauss(double d, double w) { return std::exp(-(d * d) / (w * w)); }

// ── Mojave features (must match terrain.js) ───────────────────────────────
struct Ridge { std::vector<Pt> pts; double h, w; };
struct Peak  { double x, y, h, w; };
struct Basin { double x, y, d, w; };
struct Blob  { double x, y, w; };

const std::vector<Ridge> RIDGES = {
    {{{6, 28}, {9, 45}, {11, 62}, {13, 76}, {16, 90}}, 1700, 7}, // Spring Mtns
    {{{36, 1}, {44, 9}, {54, 12}, {63, 7}}, 850, 5},             // McCullough
    {{{77, 4}, {81, 18}, {84, 32}, {82, 42}}, 800, 4},           // Eldorado Mtns
    {{{70, 42}, {75, 50}, {78, 56}}, 650, 4},                    // River Mtns
    {{{46, 90}, {58, 95}, {72, 97}}, 1200, 6},                   // Sheep Range
    {{{18, 36}, {22, 40}}, 450, 4},                              // Goodsprings hills
};

const std::vector<Peak> PEAKS = {
    {12, 78, 1500, 5},  // Mt. Charleston
    {45, 25, 750, 3.5}, // Black Mountain
    {66, 70, 850, 3},   // Frenchman Mountain
    {89, 60, 500, 3},   // Fortification Hill
};

const std::vector<Basin> BASINS = {
    {52, 66, 260, 14}, // Las Vegas valley
    {26, 13, 160, 6},  // Ivanpah dry lake
    {27, 26, 110, 4},  // Jean dry lake
    {62, 38, 130, 8},  // Eldorado valley
};

const std::vector<Pt> RIVER = {
    {87, 54}, {89, 46}, {87, 37}, {90, 27}, {88, 16}, {91, 7}, {90, -2}};

const std::vector<Blob> LAKE = {
    {76, 70, 6}, {80, 67, 6}, {84, 64, 6}, {88, 62, 5.5},
    {91, 67, 5}, {86, 70, 5.5}, {84, 75, 5}, {87, 57, 3.5}};

} // namespace

double elevation(double x, double y) {
    double e = 700.0 + fbm(x * 0.045, y * 0.045, 5) * 260.0;

    for (const auto& r : RIDGES) {
        const double d = distToPolyline(x, y, r.pts);
        e += r.h * gauss(d, r.w) * (0.8 + 0.4 * valueNoise(x * 0.3, y * 0.3));
    }
    for (const auto& p : PEAKS)
        e += p.h * gauss(std::hypot(x - p.x, y - p.y), p.w);
    for (const auto& b : BASINS)
        e -= b.d * gauss(std::hypot(x - b.x, y - b.y), b.w);

    const double dr = distToPolyline(x, y, RIVER);
    if (dr < 6.0) {
        const double t = gauss(dr, 1.6);
        e = e * (1.0 - t) + 250.0 * t;
    }
    for (const auto& l : LAKE) {
        const double d = std::hypot(x - l.x, y - l.y);
        if (d < l.w * 2.2) {
            const double t = gauss(d, l.w * 0.75);
            e = e * (1.0 - t) + 295.0 * t;
        }
    }
    return e;
}

} // namespace onv

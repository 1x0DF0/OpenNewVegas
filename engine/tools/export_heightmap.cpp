// Open New Vegas — heightmap exporter.
//
// Renders the world's elevation model to a 16-bit PGM heightmap suitable for
// import into a game engine (Godot, Unreal, custom). Usage:
//
//   export_heightmap [resolution] [out.pgm]
//
// Row 0 of the output is the NORTH edge of the world (matching the viewer).
// Elevation is mapped linearly: 0 m -> 0, 4500 m -> 65535.

#include "../terrain/terrain.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 2048;
    const std::string out = argc > 2 ? argv[2] : "mojave_heightmap.pgm";
    if (n < 2 || n > 16384) {
        std::fprintf(stderr, "resolution must be in [2, 16384]\n");
        return 1;
    }

    constexpr double MAX_ELEV = 4500.0;
    std::vector<std::uint16_t> px(static_cast<std::size_t>(n) * n);

    double lo = 1e30, hi = -1e30;
    for (int j = 0; j < n; ++j) {
        const double wy = (1.0 - static_cast<double>(j) / (n - 1)) * onv::WORLD_SIZE;
        for (int i = 0; i < n; ++i) {
            const double wx = static_cast<double>(i) / (n - 1) * onv::WORLD_SIZE;
            const double e = onv::elevation(wx, wy);
            lo = std::min(lo, e);
            hi = std::max(hi, e);
            const double v = std::clamp(e / MAX_ELEV, 0.0, 1.0);
            px[static_cast<std::size_t>(j) * n + i] =
                static_cast<std::uint16_t>(v * 65535.0 + 0.5);
        }
    }

    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", out.c_str());
        return 1;
    }
    std::fprintf(f, "P5\n%d %d\n65535\n", n, n);
    // PGM is big-endian for 16-bit samples
    for (const std::uint16_t v : px) {
        const unsigned char b[2] = {static_cast<unsigned char>(v >> 8),
                                    static_cast<unsigned char>(v & 0xff)};
        std::fwrite(b, 1, 2, f);
    }
    std::fclose(f);

    std::printf("wrote %s (%dx%d, 16-bit)\n", out.c_str(), n, n);
    std::printf("elevation range: %.0f m .. %.0f m (water level %.0f m)\n",
                lo, hi, onv::WATER_LEVEL);
    return 0;
}

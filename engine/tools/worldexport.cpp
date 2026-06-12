// Open New Vegas — worldspace terrain exporter.
//
// Reads a plugin the user supplies from their own copy of the game, stitches
// the LAND heightmaps of one worldspace into a single grid, and writes a
// JSON terrain bundle the first-person walker (walk/index.html) can load.
//
//   worldexport <plugin.esm> <worldspaceEditorID> <out.json> [radiusCells]
//
// Example (point it at your own install):
//   worldexport ".../Data/FalloutNV.esm" WastelandNV mojave_real.json 16
//
// Bundle schema ("onv-terrain-1"):
//   spacing  meters between height posts
//   originX  east-coordinate (m) of the west edge
//   originY  north-coordinate (m) of the south edge
//   width, height   posts per row / number of rows
//   heights  row-major floats (m), row 0 = SOUTH edge

#include "../records/records.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using onv::records::CELL_SIZE_UNITS;
using onv::records::LAND_GRID;
using onv::records::UNITS_TO_METERS;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: worldexport <plugin.esm> <worldspaceEditorID> "
                     "<out.json> [radiusCells=16]\n");
        return 1;
    }
    const std::string pluginPath = argv[1];
    const std::string worldId = argv[2];
    const std::string outPath = argv[3];
    const int radius = argc > 4 ? std::atoi(argv[4]) : 16;

    try {
        std::printf("loading %s ...\n", pluginPath.c_str());
        const auto world = onv::records::loadWorld(pluginPath);

        std::printf("worldspaces found:\n");
        for (const auto& w : world.worldspaces)
            if (!w.cells.empty())
                std::printf("  %-24s %-28s %zu cells\n", w.editorId.c_str(),
                            w.fullName.c_str(), w.cells.size());

        const auto* ws = world.findWorldspace(worldId);
        if (!ws) {
            std::fprintf(stderr, "worldspace not found: %s\n", worldId.c_str());
            return 1;
        }

        // Keep cells with terrain inside the radius (centered on cell 0,0)
        std::vector<const onv::records::ExteriorCell*> cells;
        std::int32_t minX = std::numeric_limits<std::int32_t>::max(), minY = minX;
        std::int32_t maxX = std::numeric_limits<std::int32_t>::min(), maxY = maxX;
        std::size_t refCount = 0;
        for (const auto& [grid, cell] : ws->cells) {
            refCount += cell.refs.size();
            if (!cell.land) continue;
            if (std::abs(grid.first) > radius || std::abs(grid.second) > radius)
                continue;
            cells.push_back(&cell);
            minX = std::min(minX, grid.first);
            maxX = std::max(maxX, grid.first);
            minY = std::min(minY, grid.second);
            maxY = std::max(maxY, grid.second);
        }
        if (cells.empty()) {
            std::fprintf(stderr, "no terrain cells within radius %d\n", radius);
            return 1;
        }

        // Stitch: cells share edge posts, so the grid is 32 posts per cell + 1
        const int cellsX = maxX - minX + 1, cellsY = maxY - minY + 1;
        const int width = cellsX * (LAND_GRID - 1) + 1;
        const int height = cellsY * (LAND_GRID - 1) + 1;
        const float NO_DATA = -1000.0f;
        std::vector<float> grid(static_cast<std::size_t>(width) * height, NO_DATA);

        for (const auto* cell : cells) {
            const int baseX = (cell->gridX - minX) * (LAND_GRID - 1);
            const int baseY = (cell->gridY - minY) * (LAND_GRID - 1);
            for (int r = 0; r < LAND_GRID; ++r)
                for (int c = 0; c < LAND_GRID; ++c)
                    grid[static_cast<std::size_t>(baseY + r) * width + baseX + c] =
                        static_cast<float>(cell->land->at(r, c) * UNITS_TO_METERS);
        }

        float lo = 1e9f, hi = -1e9f;
        for (const float v : grid)
            if (v != NO_DATA) { lo = std::min(lo, v); hi = std::max(hi, v); }

        const double spacing = CELL_SIZE_UNITS / (LAND_GRID - 1) * UNITS_TO_METERS;
        const double originX = minX * CELL_SIZE_UNITS * UNITS_TO_METERS;
        const double originY = minY * CELL_SIZE_UNITS * UNITS_TO_METERS;

        std::FILE* f = std::fopen(outPath.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "cannot open %s\n", outPath.c_str());
            return 1;
        }
        std::fprintf(f,
                     "{\n\"format\":\"onv-terrain-1\",\n\"source\":\"%s\",\n"
                     "\"worldspace\":\"%s\",\n\"spacing\":%.4f,\n"
                     "\"originX\":%.2f,\n\"originY\":%.2f,\n"
                     "\"width\":%d,\n\"height\":%d,\n\"noData\":%.1f,\n\"heights\":[",
                     worldId.c_str(), ws->fullName.c_str(), spacing, originX,
                     originY, width, height, NO_DATA);
        for (std::size_t i = 0; i < grid.size(); ++i)
            std::fprintf(f, i ? ",%.2f" : "%.2f", grid[i]);
        std::fprintf(f, "]\n}\n");
        std::fclose(f);

        std::printf("exported %s: %d x %d posts (%d x %d cells, %.1f m spacing)\n",
                    outPath.c_str(), width, height, cellsX, cellsY, spacing);
        std::printf("height range %.1f .. %.1f m | %zu placed refs in worldspace "
                    "(not yet exported)\n", lo, hi, refCount);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

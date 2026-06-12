// Open New Vegas — typed record schemas for world geometry.
//
// Decodes the record types a walkable worldspace is made of:
//   WRLD — a worldspace (e.g. the Mojave exterior)
//   CELL — one exterior cell on the worldspace grid (4096 game units square)
//   LAND — the cell's terrain: a 33x33 grid of height posts
//   REFR — a placed object reference (position / rotation / scale)
//   STAT — a static object base record (model path)
//
// Layouts are community-documented (UESP, xEdit). Clean-room implementation.

#pragma once

#include "../formats/esm.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace onv::records {

// 1 game unit in meters (Gamebryo scale: 64 units = 1 yard)
inline constexpr double UNITS_TO_METERS = 0.9144 / 64.0;
// Exterior cell edge length in game units
inline constexpr double CELL_SIZE_UNITS = 4096.0;
// LAND height grid is 33x33 posts per cell
inline constexpr int LAND_GRID = 33;

struct LandHeights {
    // Height in game units at post [row * 33 + col]; row 0 = south edge,
    // col 0 = west edge.
    std::vector<float> heights;

    float at(int row, int col) const { return heights[row * LAND_GRID + col]; }
};

struct PlacedRef {
    std::uint32_t formId = 0;
    std::uint32_t baseFormId = 0;       // what is placed (STAT, NPC_, ...)
    float x = 0, y = 0, z = 0;          // game units
    float rotX = 0, rotY = 0, rotZ = 0; // radians
    float scale = 1.0f;
};

struct ExteriorCell {
    std::uint32_t formId = 0;
    std::int32_t gridX = 0, gridY = 0;
    std::optional<LandHeights> land;
    std::vector<PlacedRef> refs;
};

struct Worldspace {
    std::uint32_t formId = 0;
    std::string editorId;
    std::string fullName;
    // Keyed by (gridX, gridY)
    std::map<std::pair<std::int32_t, std::int32_t>, ExteriorCell> cells;
};

struct StaticObject {
    std::uint32_t formId = 0;
    std::string editorId;
    std::string modelPath; // e.g. "meshes\\landscape\\rocks\\..."
};

// ── Individual decoders (throw std::runtime_error on malformed data) ──────
LandHeights decodeLand(const esm::Record& rec);
PlacedRef decodeRef(const esm::Record& rec);
StaticObject decodeStatic(const esm::Record& rec);
// Returns grid coords from a CELL's XCLC subrecord, or nullopt for interiors.
std::optional<std::pair<std::int32_t, std::int32_t>> cellGrid(const esm::Record& rec);

// ── Whole-plugin world loader ──────────────────────────────────────────────
// Streams the plugin and assembles every worldspace with its exterior cells,
// terrain, and placed references, plus the STAT base-object table.
struct World {
    std::vector<Worldspace> worldspaces;
    std::map<std::uint32_t, StaticObject> statics;

    const Worldspace* findWorldspace(const std::string& editorId) const;
};

World loadWorld(const std::string& pluginPath);

} // namespace onv::records

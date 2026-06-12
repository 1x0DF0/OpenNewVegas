// Open New Vegas — typed record decoders implementation.

#include "records.hpp"
#include "../formats/binary_reader.hpp"

#include <cstring>
#include <stdexcept>

namespace onv::records {
namespace {

const esm::Subrecord& require(const esm::Record& rec, const char* type) {
    const auto* sub = rec.find(type);
    if (!sub)
        throw std::runtime_error(std::string(rec.type) + " record missing " +
                                 type + " subrecord");
    return *sub;
}

} // namespace

LandHeights decodeLand(const esm::Record& rec) {
    // VHGT: float base offset, then 33x33 signed byte gradients (+3 pad).
    // Heights accumulate: each row's first value offsets from the previous
    // row's start; values within a row offset from the previous column.
    // Final height in game units = accumulated value * 8.
    const auto& vhgt = require(rec, "VHGT");
    constexpr std::size_t GRID = LAND_GRID * LAND_GRID;
    if (vhgt.data.size() < 4 + GRID)
        throw std::runtime_error("VHGT subrecord too small");

    float offset;
    std::memcpy(&offset, vhgt.data.data(), 4);
    const auto* grad = reinterpret_cast<const std::int8_t*>(vhgt.data.data() + 4);

    LandHeights out;
    out.heights.resize(GRID);
    float rowStart = offset;
    for (int r = 0; r < LAND_GRID; ++r) {
        rowStart += grad[r * LAND_GRID];
        float v = rowStart;
        out.heights[r * LAND_GRID] = v * 8.0f;
        for (int c = 1; c < LAND_GRID; ++c) {
            v += grad[r * LAND_GRID + c];
            out.heights[r * LAND_GRID + c] = v * 8.0f;
        }
    }
    return out;
}

PlacedRef decodeRef(const esm::Record& rec) {
    PlacedRef ref;
    ref.formId = rec.formId;

    const auto& name = require(rec, "NAME");
    if (name.data.size() < 4) throw std::runtime_error("REFR NAME too small");
    std::memcpy(&ref.baseFormId, name.data.data(), 4);

    const auto& data = require(rec, "DATA");
    if (data.data.size() < 24) throw std::runtime_error("REFR DATA too small");
    float v[6];
    std::memcpy(v, data.data.data(), 24);
    ref.x = v[0]; ref.y = v[1]; ref.z = v[2];
    ref.rotX = v[3]; ref.rotY = v[4]; ref.rotZ = v[5];

    if (const auto* xscl = rec.find("XSCL"); xscl && xscl->data.size() >= 4)
        std::memcpy(&ref.scale, xscl->data.data(), 4);
    return ref;
}

StaticObject decodeStatic(const esm::Record& rec) {
    StaticObject s;
    s.formId = rec.formId;
    s.editorId = rec.editorId();
    if (const auto* modl = rec.find("MODL")) s.modelPath = modl->asString();
    return s;
}

std::optional<std::pair<std::int32_t, std::int32_t>> cellGrid(const esm::Record& rec) {
    const auto* xclc = rec.find("XCLC");
    if (!xclc || xclc->data.size() < 8) return std::nullopt; // interior cell
    std::int32_t xy[2];
    std::memcpy(xy, xclc->data.data(), 8);
    return std::make_pair(xy[0], xy[1]);
}

const Worldspace* World::findWorldspace(const std::string& editorId) const {
    for (const auto& w : worldspaces)
        if (w.editorId == editorId) return &w;
    return nullptr;
}

World loadWorld(const std::string& pluginPath) {
    World world;
    Worldspace* curWorld = nullptr;
    ExteriorCell* curCell = nullptr;

    // The plugin stores each WRLD followed by its children group, each CELL
    // followed by its children group (LAND, REFR, ...), in file order — so a
    // streaming walk can attach records to the most recent parent.
    esm::walk(pluginPath, [&](const esm::Record& rec) {
        if (rec.type == "WRLD") {
            Worldspace w;
            w.formId = rec.formId;
            w.editorId = rec.editorId();
            if (const auto* full = rec.find("FULL")) w.fullName = full->asString();
            world.worldspaces.push_back(std::move(w));
            curWorld = &world.worldspaces.back();
            curCell = nullptr;
        } else if (rec.type == "CELL") {
            curCell = nullptr;
            if (!curWorld) return; // interior cell block (no worldspace parent)
            if (const auto grid = cellGrid(rec)) {
                ExteriorCell cell;
                cell.formId = rec.formId;
                cell.gridX = grid->first;
                cell.gridY = grid->second;
                auto [it, inserted] = curWorld->cells.emplace(*grid, std::move(cell));
                curCell = &it->second;
            }
        } else if (rec.type == "LAND") {
            if (curCell) curCell->land = decodeLand(rec);
        } else if (rec.type == "REFR") {
            if (curCell) curCell->refs.push_back(decodeRef(rec));
        } else if (rec.type == "STAT") {
            auto s = decodeStatic(rec);
            world.statics[s.formId] = std::move(s);
        }
    }, {"WRLD", "CELL", "LAND", "REFR", "STAT"});

    return world;
}

} // namespace onv::records

// Open New Vegas — record schema tests.
//
// Builds a synthetic plugin containing a worldspace with one exterior cell,
// a LAND heightmap, a placed REFR, and a STAT base object — then verifies
// loadWorld() assembles it all correctly. No game data involved.

#include "../records/records.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

struct Builder {
    std::vector<std::uint8_t> buf;
    void u8(std::uint8_t v) { buf.push_back(v); }
    void i8(std::int8_t v) { buf.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void tag(const char* t) { raw(t, 4); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void bytes(const std::vector<std::uint8_t>& v) { raw(v.data(), v.size()); }
    void zstring(const std::string& s) { raw(s.c_str(), s.size() + 1); }
    std::size_t size() const { return buf.size(); }
};

void subHeader(Builder& b, const char* type, std::uint16_t size) {
    b.tag(type);
    b.u16(size);
}

void subString(Builder& b, const char* type, const std::string& s) {
    subHeader(b, type, static_cast<std::uint16_t>(s.size() + 1));
    b.zstring(s);
}

void record(Builder& b, const char* type, std::uint32_t formId,
            const Builder& body) {
    b.tag(type);
    b.u32(static_cast<std::uint32_t>(body.size()));
    b.u32(0); // flags
    b.u32(formId);
    b.u32(0); // vc info
    b.u16(15);
    b.u16(0);
    b.bytes(body.buf);
}

void group(Builder& b, const char* label, std::int32_t groupType,
           const Builder& contents) {
    b.tag("GRUP");
    b.u32(static_cast<std::uint32_t>(24 + contents.size()));
    b.tag(label);
    b.i32(groupType);
    b.u32(0);
    b.u32(0);
    b.bytes(contents.buf);
}

} // namespace

int main() {
    using namespace onv::records;

    // ── LAND body: VHGT with a known accumulation pattern ──
    // offset 100; gradient[0][1] = 2 (col step); gradient[1][0] = 3 (row step)
    Builder landBody;
    subHeader(landBody, "VHGT", 4 + 33 * 33 + 3);
    landBody.f32(100.0f);
    for (int i = 0; i < 33 * 33; ++i) {
        if (i == 1) landBody.i8(2);
        else if (i == 33) landBody.i8(3);
        else landBody.i8(0);
    }
    landBody.u8(0); landBody.u8(0); landBody.u8(0); // pad

    // ── REFR body: a rock placed at a known position with scale ──
    Builder refrBody;
    subHeader(refrBody, "NAME", 4);
    refrBody.u32(0x2000); // base = our STAT
    subHeader(refrBody, "DATA", 24);
    refrBody.f32(4096.0f); refrBody.f32(8192.0f); refrBody.f32(800.0f);
    refrBody.f32(0.0f); refrBody.f32(0.0f); refrBody.f32(1.5708f);
    subHeader(refrBody, "XSCL", 4);
    refrBody.f32(2.0f);

    // ── CELL body: exterior at grid (2, -1) ──
    Builder cellBody;
    subString(cellBody, "EDID", "TestCell");
    subHeader(cellBody, "XCLC", 8);
    cellBody.i32(2);
    cellBody.i32(-1);

    // ── STAT body ──
    Builder statBody;
    subString(statBody, "EDID", "RockBoulder01");
    subString(statBody, "MODL", "meshes\\rocks\\boulder01.nif");

    // ── WRLD body ──
    Builder wrldBody;
    subString(wrldBody, "EDID", "TestWasteland");
    subString(wrldBody, "FULL", "Test Wasteland");

    // ── Assemble: TES4, STAT top group, WRLD top group with nesting ──
    Builder cellChildren; // group type 6: cell children
    record(cellChildren, "LAND", 0x3000, landBody);
    record(cellChildren, "REFR", 0x4000, refrBody);
    Builder cellChildrenGrp;
    group(cellChildrenGrp, "\x00\x10\x00\x00", 6, cellChildren);

    Builder worldChildren; // group type 1: world children
    record(worldChildren, "CELL", 0x1000, cellBody);
    worldChildren.bytes(cellChildrenGrp.buf);
    Builder worldChildrenGrp;
    group(worldChildrenGrp, "\x00\x05\x00\x00", 1, worldChildren);

    Builder statTop;
    record(statTop, "STAT", 0x2000, statBody);

    Builder wrldTop;
    record(wrldTop, "WRLD", 0x0500, wrldBody);
    wrldTop.bytes(worldChildrenGrp.buf);

    Builder plugin;
    record(plugin, "TES4", 0, Builder{});
    group(plugin, "STAT", 0, statTop);
    group(plugin, "WRLD", 0, wrldTop);

    const std::string path = "/tmp/onv_fixtures/world.esm";
    if (system("mkdir -p /tmp/onv_fixtures") != 0) return 1;
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(plugin.buf.data()),
                static_cast<std::streamsize>(plugin.size()));
    }

    // ── Load and verify ──
    const auto world = loadWorld(path);

    CHECK(world.worldspaces.size() == 1);
    const auto* ws = world.findWorldspace("TestWasteland");
    CHECK(ws != nullptr);
    if (!ws) return 1;
    CHECK(ws->fullName == "Test Wasteland");
    CHECK(ws->cells.size() == 1);

    const auto it = ws->cells.find({2, -1});
    CHECK(it != ws->cells.end());
    const auto& cell = it->second;
    CHECK(cell.gridX == 2 && cell.gridY == -1);

    // LAND accumulation: h[0][0] = 100*8; h[0][1] = 102*8 (col gradient);
    // row 1 starts at 103 (row gradient), so h[1][0] = 103*8, h[1][1] = 103*8
    CHECK(cell.land.has_value());
    if (cell.land) {
        CHECK(cell.land->at(0, 0) == 800.0f);
        CHECK(cell.land->at(0, 1) == 816.0f);
        CHECK(cell.land->at(1, 0) == 824.0f);
        CHECK(cell.land->at(1, 1) == 824.0f);
        CHECK(cell.land->at(32, 32) == 824.0f); // pattern stays flat after
    }

    CHECK(cell.refs.size() == 1);
    if (!cell.refs.empty()) {
        const auto& ref = cell.refs[0];
        CHECK(ref.baseFormId == 0x2000);
        CHECK(ref.x == 4096.0f && ref.y == 8192.0f && ref.z == 800.0f);
        CHECK(std::fabs(ref.rotZ - 1.5708f) < 1e-6);
        CHECK(ref.scale == 2.0f);
        // Base object resolves through the STAT table
        const auto stat = world.statics.find(ref.baseFormId);
        CHECK(stat != world.statics.end());
        if (stat != world.statics.end())
            CHECK(stat->second.modelPath == "meshes\\rocks\\boulder01.nif");
    }

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all record tests passed (worldspace/cell/land/refr/stat)\n");
    return 0;
}

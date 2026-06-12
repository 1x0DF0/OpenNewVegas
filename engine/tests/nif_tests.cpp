// Open New Vegas — NIF reader tests.
//
// We never ship game data, so these tests build minimal synthetic NIF buffers
// (Builder-style, matching format_tests.cpp) faithful to the Gamebryo
// 20.2.0.7 / user 11 / BS 34 layout the decoder implements against, then
// assert that decode() returns the exact vertices and triangle indices.

#include "../formats/nif.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
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

// ── Little-endian byte buffer builder ─────────────────────────────────────
struct Builder {
    std::vector<std::uint8_t> buf;

    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void bytes(const std::vector<std::uint8_t>& v) { raw(v.data(), v.size()); }
    void headerLine(const std::string& s) { raw(s.data(), s.size()); u8('\n'); }
    void sizedString(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void exportString(const std::string& s) {
        // byte length includes the trailing NUL
        u8(static_cast<std::uint8_t>(s.size() + 1));
        raw(s.data(), s.size());
        u8(0);
    }
    void vector3(float x, float y, float z) { f32(x); f32(y); f32(z); }
    std::size_t size() const { return buf.size(); }
};

bool feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// Build a NiGeometryData base (no normals/colors/uv) for `verts` vertices and
// the given Num Triangles value. Returns the block body fragment.
void writeGeometryBase(Builder& b, const std::vector<float>& verts,
                       std::uint16_t numTriangles) {
    const std::uint16_t numVerts =
        static_cast<std::uint16_t>(verts.size() / 3);
    b.i32(0);              // Group ID
    b.u16(numVerts);       // Num Vertices
    b.u8(0);               // Keep Flags
    b.u8(0);               // Compress Flags
    b.u8(1);               // Has Vertices
    for (std::size_t i = 0; i < numVerts; ++i)
        b.vector3(verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
    b.u16(0);              // BS Data Flags (no UV, no tangents)
    b.u8(0);               // Has Normals = false
    b.vector3(0, 0, 0);    // Center
    b.f32(0);              // Radius
    b.u8(0);               // Has Vertex Colors = false
    // UV sets = 0 (BS Data Flags has no Has-UV bit)
    b.u16(0);              // Consistency Flags
    b.i32(-1);             // Additional Data ref
    b.u16(numTriangles);   // Num Triangles (NiTriBasedGeomData)
}

// Build a full NIF with two data blocks: a NiTriShapeData and a
// NiTriStripsData. The header carries a faithful 20.2.0.7 stream header and a
// per-block size table so the decoder resyncs between blocks.
std::vector<std::uint8_t> buildNif(
    const std::vector<float>& shapeVerts,
    const std::vector<std::uint16_t>& shapeTris,
    const std::vector<float>& stripVerts,
    const std::vector<std::uint16_t>& strip) {

    // ── Block 0: NiTriShapeData body ──
    Builder block0;
    writeGeometryBase(block0, shapeVerts,
                      static_cast<std::uint16_t>(shapeTris.size() / 3));
    block0.u32(static_cast<std::uint32_t>(shapeTris.size())); // Num Triangle Points
    block0.u8(1); // Has Triangles
    for (std::uint16_t idx : shapeTris) block0.u16(idx);
    block0.u16(0); // Num Match Groups

    // ── Block 1: NiTriStripsData body ──
    Builder block1;
    // numTriangles for a single strip = len - 2
    const std::uint16_t numTris =
        strip.size() >= 2 ? static_cast<std::uint16_t>(strip.size() - 2) : 0;
    writeGeometryBase(block1, stripVerts, numTris);
    block1.u16(1); // Num Strips
    block1.u16(static_cast<std::uint16_t>(strip.size())); // Strip Lengths[0]
    block1.u8(1);  // Has Points
    for (std::uint16_t idx : strip) block1.u16(idx);

    // ── Header ──
    Builder b;
    b.headerLine("Gamebryo File Format, Version 20.2.0.7");
    b.u32(0x14020007);     // Version
    b.u8(1);               // Endian (little)
    b.u32(11);             // User Version
    b.u32(2);              // Num Blocks

    // Bethesda stream header
    b.u32(34);             // BS Version
    b.exportString("OpenNewVegas test");  // Author
    b.exportString("");                   // Process Script
    b.exportString("");                   // Export Script

    // Block type table
    b.u16(2);              // Num Block Types
    b.sizedString("NiTriShapeData");
    b.sizedString("NiTriStripsData");
    b.u16(0);              // Block Type Index[0] -> NiTriShapeData
    b.u16(1);              // Block Type Index[1] -> NiTriStripsData

    // Block sizes
    b.u32(static_cast<std::uint32_t>(block0.size()));
    b.u32(static_cast<std::uint32_t>(block1.size()));

    // String table (empty)
    b.u32(0);              // Num Strings
    b.u32(0);              // Max String Length

    // Groups (none)
    b.u32(0);              // Num Groups

    // Block bodies
    b.bytes(block0.buf);
    b.bytes(block1.buf);

    return b.buf;
}

void testTriShapeAndStrips() {
    // Shape: 4 vertices, 2 triangles forming a quad.
    const std::vector<float> shapeVerts = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    const std::vector<std::uint16_t> shapeTris = {0, 1, 2, 0, 2, 3};

    // Strip: 5 vertices, one strip [0,1,2,3,4] -> 3 triangles after de-strip.
    const std::vector<float> stripVerts = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f,
        2.0f, 0.0f, 0.0f,
        3.0f, 0.0f, 1.0f,
        4.0f, 0.0f, 0.0f,
    };
    const std::vector<std::uint16_t> strip = {0, 1, 2, 3, 4};

    const auto nif = buildNif(shapeVerts, shapeTris, stripVerts, strip);
    const auto meshes = onv::nif::decode(nif);

    CHECK(meshes.size() == 2);
    if (meshes.size() != 2) return;

    // ── NiTriShapeData ──
    const auto& shape = meshes[0];
    CHECK(shape.vertices.size() == shapeVerts.size());
    for (std::size_t i = 0; i < shapeVerts.size(); ++i)
        CHECK(feq(shape.vertices[i], shapeVerts[i]));
    CHECK(shape.indices == shapeTris);

    // ── NiTriStripsData (de-stripped) ──
    // strip [0,1,2,3,4] -> triangles:
    //   i=0 (even): (0,1,2)
    //   i=1 (odd):  (1,3,2)
    //   i=2 (even): (2,3,4)
    const std::vector<std::uint16_t> expectedStripTris = {
        0, 1, 2,
        1, 3, 2,
        2, 3, 4,
    };
    const auto& strips = meshes[1];
    CHECK(strips.vertices.size() == stripVerts.size());
    for (std::size_t i = 0; i < stripVerts.size(); ++i)
        CHECK(feq(strips.vertices[i], stripVerts[i]));
    CHECK(strips.indices == expectedStripTris);

    std::printf("NIF: NiTriShapeData (4v/2t) + NiTriStripsData de-strip (5v->3t) exact\n");
}

// A strip containing a degenerate (repeated) index should drop that triangle.
void testStripDegenerate() {
    const std::vector<float> verts = {
        0, 0, 0, 1, 0, 0, 2, 0, 0, 2, 0, 0, 3, 0, 0,
    };
    // strip [0,1,2,2,4]: triangle from i=1 is (1,2,2) degenerate -> dropped,
    // i=2 is (2,2,4) degenerate -> dropped. Only i=0 (0,1,2) survives.
    const std::vector<std::uint16_t> strip = {0, 1, 2, 2, 4};

    const auto nif = buildNif({0, 0, 0, 1, 0, 0, 2, 0, 0}, {0, 1, 2},
                              verts, strip);
    const auto meshes = onv::nif::decode(nif);
    CHECK(meshes.size() == 2);
    if (meshes.size() != 2) return;
    const std::vector<std::uint16_t> expected = {0, 1, 2};
    CHECK(meshes[1].indices == expected);
    std::printf("NIF: degenerate strip triangles dropped (5v strip -> 1t)\n");
}

void testRejectsBadInput() {
    bool threw = false;
    try {
        std::vector<std::uint8_t> garbage(64, 0x55);
        onv::nif::decode(garbage);
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    // Right magic, wrong version.
    threw = false;
    try {
        Builder b;
        b.headerLine("Gamebryo File Format, Version 10.0.1.0");
        b.u32(0x0A000100);
        const auto out = b.buf;
        onv::nif::decode(out);
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    std::printf("NIF: bad magic + wrong version both throw\n");
}

} // namespace

int main() {
    testTriShapeAndStrips();
    testStripDegenerate();
    testRejectsBadInput();

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all nif tests passed\n");
    return 0;
}

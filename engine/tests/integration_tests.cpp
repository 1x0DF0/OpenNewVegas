// Open New Vegas — end-to-end integration test.
//
// Builds a complete synthetic mini game install on disk:
//
//   /tmp/onv_e2e/ROOT/Data/FalloutNV.esm   (STAT + WRLD/CELL/LAND/2x REFR)
//   /tmp/onv_e2e/ROOT/Data/Test.bsa        (v104, two folders:
//                                            meshes\e2e\rock.nif
//                                            textures\e2e\stone.dds)
//
// then drives the whole engine pipeline through public APIs and asserts every
// link in the chain:
//
//   platform locator (ONV_FNV_PATH) -> records::loadWorld -> assets::DataFiles
//   -> scene::SceneStreamer + makeNifModelLoader -> render::TextureCache
//   -> scene::buildScene
//
// No game data involved: every byte of the fixture is synthesized here.

#include "../assets/data_files.hpp"
#include "../formats/bsa.hpp"
#include "../platform/game_locator.hpp"
#include "../records/records.hpp"
#include "../render/texture_cache.hpp"
#include "../scene/scene.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

bool feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// Per-stage progress reporting: remembers the failure count when the stage
// began and prints ok/FAILED when it ends.
struct Stage {
    const char* name;
    int before;
    explicit Stage(const char* n) : name(n), before(failures) {}
    ~Stage() {
        std::printf("[stage] %-38s %s\n", name,
                    failures == before ? "ok" : "FAILED");
    }
};

// ── Little-endian byte buffer builder (same idiom as the other tests) ──────
struct Builder {
    std::vector<std::uint8_t> buf;
    void u8(std::uint8_t v) { buf.push_back(v); }
    void i8(std::int8_t v) { buf.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void tag(const char* t) { raw(t, 4); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void bytes(const std::vector<std::uint8_t>& v) { raw(v.data(), v.size()); }
    void zstring(const std::string& s) { raw(s.c_str(), s.size() + 1); }
    void bzstring(const std::string& s) {
        u8(static_cast<std::uint8_t>(s.size() + 1));
        zstring(s);
    }
    void sizedString(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void exportString(const std::string& s) {
        u8(static_cast<std::uint8_t>(s.size() + 1));
        raw(s.data(), s.size());
        u8(0);
    }
    void headerLine(const std::string& s) { raw(s.data(), s.size()); u8('\n'); }
    void vector3(float x, float y, float z) { f32(x); f32(y); f32(z); }
    std::size_t size() const { return buf.size(); }

    void writeFile(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
    }
};

// ════════════════════════════════════════════════════════════════════════════
// NIF fixture — Gamebryo 20.2.0.7 / user 11 / BS 34, like nif_tests.cpp:
// one NiTriShapeData (known triangle + UVs) and one BSShaderTextureSet whose
// index-0 (diffuse) path is `diffusePath`.
// ════════════════════════════════════════════════════════════════════════════

constexpr std::uint16_t BSGDF_HAS_UV = 0x0001;

std::vector<std::uint8_t> buildNif(const std::vector<float>& verts,
                                   const std::vector<std::uint16_t>& tris,
                                   const std::vector<float>& uvs,
                                   const std::string& diffusePath) {
    // ── Block 0: NiTriShapeData body ──
    Builder block0;
    const std::uint16_t numVerts = static_cast<std::uint16_t>(verts.size() / 3);
    block0.i32(0);             // Group ID
    block0.u16(numVerts);      // Num Vertices
    block0.u8(0);              // Keep Flags
    block0.u8(0);              // Compress Flags
    block0.u8(1);              // Has Vertices
    for (std::uint16_t i = 0; i < numVerts; ++i)
        block0.vector3(verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]);
    block0.u16(BSGDF_HAS_UV);  // BS Data Flags (one UV set, no tangents)
    block0.u8(0);              // Has Normals = false
    block0.vector3(0, 0, 0);   // Center
    block0.f32(0);             // Radius
    block0.u8(0);              // Has Vertex Colors = false
    for (std::uint16_t i = 0; i < numVerts; ++i) {
        block0.f32(uvs[i * 2 + 0]);
        block0.f32(uvs[i * 2 + 1]);
    }
    block0.u16(0);             // Consistency Flags
    block0.i32(-1);            // Additional Data ref
    block0.u16(static_cast<std::uint16_t>(tris.size() / 3)); // Num Triangles
    block0.u32(static_cast<std::uint32_t>(tris.size()));     // Num Triangle Points
    block0.u8(1);              // Has Triangles
    for (std::uint16_t idx : tris) block0.u16(idx);
    block0.u16(0);             // Num Match Groups

    // ── Block 1: BSShaderTextureSet body ──
    Builder block1;
    block1.u32(2);                              // Num Textures
    block1.sizedString(diffusePath);            // [0] diffuse
    block1.sizedString("textures\\e2e\\stone_n.dds"); // [1] normal map

    // ── Header ──
    Builder b;
    b.headerLine("Gamebryo File Format, Version 20.2.0.7");
    b.u32(0x14020007);         // Version
    b.u8(1);                   // Endian (little)
    b.u32(11);                 // User Version
    b.u32(2);                  // Num Blocks
    b.u32(34);                 // BS Version
    b.exportString("OpenNewVegas e2e"); // Author
    b.exportString("");        // Process Script
    b.exportString("");        // Export Script
    b.u16(2);                  // Num Block Types
    b.sizedString("NiTriShapeData");
    b.sizedString("BSShaderTextureSet");
    b.u16(0);                  // Block Type Index[0]
    b.u16(1);                  // Block Type Index[1]
    b.u32(static_cast<std::uint32_t>(block0.size()));
    b.u32(static_cast<std::uint32_t>(block1.size()));
    b.u32(0);                  // Num Strings
    b.u32(0);                  // Max String Length
    b.u32(0);                  // Num Groups
    b.bytes(block0.buf);
    b.bytes(block1.buf);
    return b.buf;
}

// ════════════════════════════════════════════════════════════════════════════
// DDS fixture — small uncompressed 32-bit BGRA, like dds_tests.cpp. Pixel
// (0,0) carries the sentinel RGBA.
// ════════════════════════════════════════════════════════════════════════════

constexpr std::uint8_t SENTINEL_R = 0xAB, SENTINEL_G = 0xCD,
                       SENTINEL_B = 0xEF, SENTINEL_A = 0x77;

std::vector<std::uint8_t> buildDds() {
    constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
    constexpr std::uint32_t DDPF_RGB = 0x40;

    Builder b;
    b.tag("DDS ");
    b.u32(124);          // dwSize
    b.u32(0x1007);       // dwFlags (CAPS|HEIGHT|WIDTH|PIXELFORMAT)
    b.u32(2);            // height
    b.u32(2);            // width
    b.u32(0);            // pitchOrLinearSize
    b.u32(0);            // depth
    b.u32(1);            // mipMapCount
    for (int i = 0; i < 11; ++i) b.u32(0); // dwReserved1[11]
    b.u32(32);           // pfSize
    b.u32(DDPF_RGB | DDPF_ALPHAPIXELS);
    b.u32(0);            // fourCC (none)
    b.u32(32);           // rgbBitCount
    b.u32(0x00FF0000);   // R mask
    b.u32(0x0000FF00);   // G mask
    b.u32(0x000000FF);   // B mask
    b.u32(0xFF000000);   // A mask
    for (int i = 0; i < 5; ++i) b.u32(0);  // dwCaps[4] + dwReserved2

    // 4 pixels, little-endian uint32 = (A<<24)|(R<<16)|(G<<8)|B.
    struct P { std::uint8_t r, g, b, a; };
    const P pixels[4] = {
        {SENTINEL_R, SENTINEL_G, SENTINEL_B, SENTINEL_A}, // (0,0) sentinel
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12},
    };
    for (const auto& p : pixels)
        b.u32((static_cast<std::uint32_t>(p.a) << 24) |
              (static_cast<std::uint32_t>(p.r) << 16) |
              (static_cast<std::uint32_t>(p.g) << 8) |
              static_cast<std::uint32_t>(p.b));
    return b.buf;
}

// ════════════════════════════════════════════════════════════════════════════
// BSA fixture — v104, MULTIPLE folders. Generalizes data_files_tests.cpp's
// single-folder writer. Layout (per bsa.cpp's reader):
//   header (36 bytes)
//   folder records: 16 bytes each (hash u64, fileCount u32, offset u32),
//       where `offset` = absolute file-record-block offset + totalFileNameLength
//   per-folder file record blocks: bzstring folder name, then per file
//       hash u64, size u32, absolute data offset u32
//   file name block: fileCount zstrings in folder/file order
//   data
// Folders are written pre-sorted (deterministic; the reader doesn't need hash
// order). All files stored uncompressed.
// ════════════════════════════════════════════════════════════════════════════

struct BsaEntry {
    std::string name;                  // e.g. "rock.nif"
    std::vector<std::uint8_t> data;
};
struct BsaFolder {
    std::string name;                  // e.g. "meshes\\e2e"
    std::vector<BsaEntry> files;
};

std::uint64_t fileHash(const std::string& fileName) {
    const auto dot = fileName.rfind('.');
    const std::string stem =
        dot == std::string::npos ? fileName : fileName.substr(0, dot);
    const std::string ext =
        dot == std::string::npos ? "" : fileName.substr(dot);
    return onv::bsa::hashPart(stem, ext);
}

void writeBsa(const std::string& path, const std::vector<BsaFolder>& folders) {
    std::uint32_t fileCount = 0;
    std::uint32_t totalFolderNameLength = 0;
    std::string names; // file name block: zstrings in folder/file order
    for (const auto& fo : folders) {
        fileCount += static_cast<std::uint32_t>(fo.files.size());
        totalFolderNameLength += static_cast<std::uint32_t>(fo.name.size()) + 1;
        for (const auto& fi : fo.files) names += fi.name + std::string(1, '\0');
    }
    const auto totalFileNameLength = static_cast<std::uint32_t>(names.size());

    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlocksStart =
        folderRecOffset + 16 * static_cast<std::uint32_t>(folders.size());

    // Per-folder file-record block offsets/sizes.
    std::vector<std::uint32_t> blockOffset(folders.size());
    std::uint32_t cursor = fileRecBlocksStart;
    for (std::size_t i = 0; i < folders.size(); ++i) {
        blockOffset[i] = cursor;
        cursor += 1 + static_cast<std::uint32_t>(folders[i].name.size()) + 1 +
                  16 * static_cast<std::uint32_t>(folders[i].files.size());
    }
    const std::uint32_t nameBlockStart = cursor;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;

    // Per-file absolute data offsets, in folder/file order.
    std::uint32_t dataCursor = dataStart;

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);                          // version
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2);                    // dir names + file names, uncompressed
    b.u32(static_cast<std::uint32_t>(folders.size()));
    b.u32(fileCount);
    b.u32(totalFolderNameLength);
    b.u32(totalFileNameLength);
    b.u32(0);                            // fileFlags

    // Folder records (offset field includes totalFileNameLength by spec).
    for (std::size_t i = 0; i < folders.size(); ++i) {
        b.u64(onv::bsa::hashPart(folders[i].name, ""));
        b.u32(static_cast<std::uint32_t>(folders[i].files.size()));
        b.u32(blockOffset[i] + totalFileNameLength);
    }

    // File record blocks.
    for (const auto& fo : folders) {
        b.bzstring(fo.name);
        for (const auto& fi : fo.files) {
            b.u64(fileHash(fi.name));
            b.u32(static_cast<std::uint32_t>(fi.data.size()));
            b.u32(dataCursor);
            dataCursor += static_cast<std::uint32_t>(fi.data.size());
        }
    }

    // File name block, then data.
    b.raw(names.data(), names.size());
    CHECK(b.size() == dataStart);        // layout arithmetic self-check
    for (const auto& fo : folders)
        for (const auto& fi : fo.files) b.bytes(fi.data);

    b.writeFile(path);
}

// ════════════════════════════════════════════════════════════════════════════
// ESM fixture — like records_tests.cpp: TES4, STAT top group, WRLD top group
// containing one exterior CELL at (0,0) with LAND and two REFRs.
// ════════════════════════════════════════════════════════════════════════════

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

// FormIds shared between fixture construction and assertions.
constexpr std::uint32_t STAT_ID = 0x2000;
constexpr std::uint32_t WRLD_ID = 0x0500;
constexpr std::uint32_t CELL_ID = 0x1000;
constexpr std::uint32_t LAND_ID = 0x3000;
constexpr std::uint32_t REFR1_ID = 0x4000;
constexpr std::uint32_t REFR2_ID = 0x4001;

std::vector<std::uint8_t> buildEsm() {
    // LAND: VHGT offset 100; col gradient +2 at post (0,1); row gradient +3 at
    // row 1. Decoded heights (game units = value * 8):
    //   at(0,0)=800, at(0,1)=816, at(1,0)=824, at(1,1)=824, at(32,32)=824.
    Builder landBody;
    subHeader(landBody, "VHGT", 4 + 33 * 33 + 3);
    landBody.f32(100.0f);
    for (int i = 0; i < 33 * 33; ++i) {
        if (i == 1) landBody.i8(2);
        else if (i == 33) landBody.i8(3);
        else landBody.i8(0);
    }
    landBody.u8(0); landBody.u8(0); landBody.u8(0); // pad

    // REFR 1: scaled and rotated.
    Builder refr1Body;
    subHeader(refr1Body, "NAME", 4);
    refr1Body.u32(STAT_ID);
    subHeader(refr1Body, "DATA", 24);
    refr1Body.f32(1024.0f); refr1Body.f32(2048.0f); refr1Body.f32(850.0f);
    refr1Body.f32(0.0f); refr1Body.f32(0.0f); refr1Body.f32(1.5708f);
    subHeader(refr1Body, "XSCL", 4);
    refr1Body.f32(2.0f);

    // REFR 2: no XSCL -> default scale 1.0.
    Builder refr2Body;
    subHeader(refr2Body, "NAME", 4);
    refr2Body.u32(STAT_ID);
    subHeader(refr2Body, "DATA", 24);
    refr2Body.f32(-512.0f); refr2Body.f32(256.0f); refr2Body.f32(800.5f);
    refr2Body.f32(0.25f); refr2Body.f32(0.0f); refr2Body.f32(0.0f);

    // CELL: exterior at grid (0, 0).
    Builder cellBody;
    subString(cellBody, "EDID", "E2ECell");
    subHeader(cellBody, "XCLC", 8);
    cellBody.i32(0);
    cellBody.i32(0);

    // STAT: model path deliberately WITHOUT the "meshes\\" prefix, to exercise
    // makeNifModelLoader's prefix fallback.
    Builder statBody;
    subString(statBody, "EDID", "E2ERock01");
    subString(statBody, "MODL", "e2e\\rock.nif");

    Builder wrldBody;
    subString(wrldBody, "EDID", "WastelandNV");
    subString(wrldBody, "FULL", "E2E Wasteland");

    Builder cellChildren; // group type 6: cell children
    record(cellChildren, "LAND", LAND_ID, landBody);
    record(cellChildren, "REFR", REFR1_ID, refr1Body);
    record(cellChildren, "REFR", REFR2_ID, refr2Body);
    Builder cellChildrenGrp;
    group(cellChildrenGrp, "\x00\x10\x00\x00", 6, cellChildren); // CELL_ID LE

    Builder worldChildren; // group type 1: world children
    record(worldChildren, "CELL", CELL_ID, cellBody);
    worldChildren.bytes(cellChildrenGrp.buf);
    Builder worldChildrenGrp;
    group(worldChildrenGrp, "\x00\x05\x00\x00", 1, worldChildren); // WRLD_ID LE

    Builder statTop;
    record(statTop, "STAT", STAT_ID, statBody);

    Builder wrldTop;
    record(wrldTop, "WRLD", WRLD_ID, wrldBody);
    wrldTop.bytes(worldChildrenGrp.buf);

    Builder plugin;
    record(plugin, "TES4", 0, Builder{});
    group(plugin, "STAT", 0, statTop);
    group(plugin, "WRLD", 0, wrldTop);
    return plugin.buf;
}

// ════════════════════════════════════════════════════════════════════════════

// Known fixture geometry (shared between NIF construction and assertions).
const std::vector<float> kVerts = {
    0.0f,   0.0f, 0.0f,
    100.0f, 0.0f, 0.0f,
    0.0f, 100.0f, 50.0f,
};
const std::vector<std::uint16_t> kTris = {0, 1, 2};
const std::vector<float> kUvs = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.5f, 1.0f,
};
const std::string kDiffuse = "textures\\e2e\\stone.dds";

void checkModel(const onv::scene::Model* model) {
    CHECK(model != nullptr);
    if (!model) return;
    CHECK(model->meshes.size() == 1);
    if (model->meshes.size() != 1) return;
    const auto& mesh = model->meshes[0];
    CHECK(mesh.vertices.size() == kVerts.size());
    for (std::size_t i = 0; i < kVerts.size() && i < mesh.vertices.size(); ++i)
        CHECK(feq(mesh.vertices[i], kVerts[i]));
    CHECK(mesh.indices == kTris);
    CHECK(mesh.uvs.size() == kUvs.size());
    for (std::size_t i = 0; i < kUvs.size() && i < mesh.uvs.size(); ++i)
        CHECK(feq(mesh.uvs[i], kUvs[i]));
    CHECK(mesh.diffuseTexture == kDiffuse);
}

} // namespace

int main() {
    const fs::path root = "/tmp/onv_e2e/ROOT";
    const fs::path dataDir = root / "Data";

    // ── Fixture: clean slate, then a full mini-install ──
    std::vector<std::uint8_t> nifBytes, ddsBytes;
    {
        Stage stage("fixture: mini install on disk");
        std::error_code ec;
        fs::remove_all("/tmp/onv_e2e", ec);
        fs::create_directories(dataDir, ec);
        CHECK(!ec);

        nifBytes = buildNif(kVerts, kTris, kUvs, kDiffuse);
        ddsBytes = buildDds();

        writeBsa((dataDir / "Test.bsa").string(),
                 {{"meshes\\e2e", {{"rock.nif", nifBytes}}},
                  {"textures\\e2e", {{"stone.dds", ddsBytes}}}});

        Builder esm;
        esm.buf = buildEsm();
        esm.writeFile((dataDir / "FalloutNV.esm").string());

        CHECK(fs::exists(dataDir / "FalloutNV.esm"));
        CHECK(fs::exists(dataDir / "Test.bsa"));
    }

    // ── Stage 1: platform locator honours ONV_FNV_PATH ──
    {
        Stage stage("platform: ONV_FNV_PATH locator");
        setenv("ONV_FNV_PATH", root.string().c_str(), 1);

        const auto data = onv::platform::findFalloutNVData();
        CHECK(data.has_value());
        if (data) CHECK(*data == dataDir.string());

        const auto esm = onv::platform::findFalloutNVMasterEsm();
        CHECK(esm.has_value());
        if (esm) CHECK(*esm == (dataDir / "FalloutNV.esm").string());
    }

    // ── Stage 2: records — load the world from the located ESM ──
    const auto esmPath = onv::platform::findFalloutNVMasterEsm();
    if (!esmPath) {
        std::printf("cannot continue without master ESM\n");
        return 1;
    }
    const auto world = onv::records::loadWorld(*esmPath);
    {
        Stage stage("records: loadWorld(FalloutNV.esm)");
        CHECK(world.worldspaces.size() == 1);
        const auto* ws = world.findWorldspace("WastelandNV");
        CHECK(ws != nullptr);
        if (ws) {
            CHECK(ws->fullName == "E2E Wasteland");
            CHECK(ws->cells.size() == 1);
            const auto it = ws->cells.find({0, 0});
            CHECK(it != ws->cells.end());
            if (it != ws->cells.end()) {
                const auto& cell = it->second;
                CHECK(cell.gridX == 0 && cell.gridY == 0);

                // LAND VHGT accumulation (heights are stored value * 8).
                CHECK(cell.land.has_value());
                if (cell.land) {
                    CHECK(cell.land->at(0, 0) == 800.0f);
                    CHECK(cell.land->at(0, 1) == 816.0f);
                    CHECK(cell.land->at(1, 0) == 824.0f);
                    CHECK(cell.land->at(1, 1) == 824.0f);
                    CHECK(cell.land->at(32, 32) == 824.0f);
                }

                CHECK(cell.refs.size() == 2);
                if (cell.refs.size() == 2) {
                    const auto& r1 = cell.refs[0];
                    CHECK(r1.formId == REFR1_ID);
                    CHECK(r1.baseFormId == STAT_ID);
                    CHECK(r1.x == 1024.0f && r1.y == 2048.0f && r1.z == 850.0f);
                    CHECK(feq(r1.rotZ, 1.5708f));
                    CHECK(r1.scale == 2.0f);

                    const auto& r2 = cell.refs[1];
                    CHECK(r2.formId == REFR2_ID);
                    CHECK(r2.baseFormId == STAT_ID);
                    CHECK(r2.x == -512.0f && r2.y == 256.0f && r2.z == 800.5f);
                    CHECK(feq(r2.rotX, 0.25f));
                    CHECK(r2.scale == 1.0f); // no XSCL -> default
                }
            }
        }
        // STAT base-object table: model path stored WITHOUT meshes\ prefix.
        const auto stat = world.statics.find(STAT_ID);
        CHECK(stat != world.statics.end());
        if (stat != world.statics.end())
            CHECK(stat->second.modelPath == "e2e\\rock.nif");
    }

    // ── Stage 3: assets — DataFiles VFS over the located Data dir ──
    const auto dataPath = onv::platform::findFalloutNVData();
    if (!dataPath) {
        std::printf("cannot continue without Data dir\n");
        return 1;
    }
    onv::assets::DataFiles vfs(*dataPath);
    {
        Stage stage("assets: DataFiles VFS over Data/");
        CHECK(vfs.archiveCount() == 1);
        CHECK(vfs.archivedFileCount() == 2);

        const auto nif = vfs.resolve("meshes\\e2e\\rock.nif");
        CHECK(nif.has_value() && *nif == nifBytes);

        const auto dds = vfs.resolve("textures\\e2e\\stone.dds");
        CHECK(dds.has_value() && *dds == ddsBytes);

        // Case/separator insensitivity through the archive index.
        CHECK(vfs.contains("MESHES/E2E/Rock.NIF"));
        CHECK(vfs.contains("Textures/e2e/STONE.dds"));
        CHECK(!vfs.resolve("meshes\\e2e\\missing.nif").has_value());
    }

    // ── Stage 4: scene — SceneStreamer with the production NIF loader ──
    auto loader = onv::scene::makeNifModelLoader(vfs);
    {
        Stage stage("scene: SceneStreamer + NIF loader");
        onv::scene::SceneStreamer streamer(world, "", loader);
        CHECK(streamer.worldspaceEditorId() == "WastelandNV");

        const std::vector<std::pair<int, int>> expectedCells = {{0, 0}};
        CHECK(streamer.populatedCells() == expectedCells);

        const auto& instances = streamer.cellInstances(0, 0);
        CHECK(instances.size() == 2);
        if (instances.size() == 2) {
            const auto& i1 = instances[0];
            CHECK(i1.refrFormId == REFR1_ID);
            CHECK(i1.baseFormId == STAT_ID);
            CHECK(i1.modelPath == "e2e\\rock.nif");
            CHECK(i1.x == 1024.0f && i1.y == 2048.0f && i1.z == 850.0f);
            CHECK(feq(i1.rotZ, 1.5708f));
            CHECK(i1.scale == 2.0f);

            const auto& i2 = instances[1];
            CHECK(i2.refrFormId == REFR2_ID);
            CHECK(i2.x == -512.0f && i2.y == 256.0f && i2.z == 800.5f);
            CHECK(feq(i2.rotX, 0.25f));
            CHECK(i2.scale == 1.0f);
        }

        // Model loaded via the "meshes\\" prefix fallback (the STAT path has
        // no prefix; only "meshes\\e2e\\rock.nif" exists in the BSA).
        checkModel(streamer.model("e2e\\rock.nif"));

        // Empty/unknown cell streams an empty (cached) list, not a crash.
        CHECK(streamer.cellInstances(5, -7).empty());
    }

    // ── Stage 5: render — TextureCache resolves the mesh's diffuse map ──
    {
        Stage stage("render: TextureCache diffuse lookup");
        onv::scene::SceneStreamer streamer(world, "", loader);
        streamer.cellInstances(0, 0); // trigger lazy model load
        const auto* model = streamer.model("e2e\\rock.nif");
        CHECK(model != nullptr);

        onv::render::TextureCache textures(vfs);
        const std::string texPath =
            model && !model->meshes.empty() ? model->meshes[0].diffuseTexture
                                            : kDiffuse;
        const auto* img = textures.get(texPath);
        CHECK(img != nullptr);
        if (img) {
            CHECK(img->width == 2 && img->height == 2);
            CHECK(img->rgba.size() == 2 * 2 * 4);
            // Sentinel pixel at (0,0), decoded from BGRA masks to RGBA.
            CHECK(img->rgba[0] == SENTINEL_R);
            CHECK(img->rgba[1] == SENTINEL_G);
            CHECK(img->rgba[2] == SENTINEL_B);
            CHECK(img->rgba[3] == SENTINEL_A);
            // Neighbour pixel round-trips too.
            CHECK(img->rgba[4] == 1 && img->rgba[5] == 2 &&
                  img->rgba[6] == 3 && img->rgba[7] == 4);
        }
        // Cached: same pointer on repeat lookup.
        CHECK(textures.get(texPath) == img);
    }

    // ── Stage 6: scene — whole-world buildScene over the same inputs ──
    {
        Stage stage("scene: buildScene whole-world pass");
        const auto scene = onv::scene::buildScene(world, "", loader);
        CHECK(scene.worldspaceEditorId == "WastelandNV");
        CHECK(scene.instances.size() == 2);
        CHECK(scene.models.size() == 1); // one unique model, deduped
        CHECK(scene.skippedNonStatic == 0);
        CHECK(scene.missingModel == 0);
        const auto it = scene.models.find("e2e\\rock.nif");
        CHECK(it != scene.models.end());
        if (it != scene.models.end()) checkModel(&it->second);
    }

    unsetenv("ONV_FNV_PATH");

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all integration tests passed "
                "(locator -> esm -> bsa -> scene -> texture)\n");
    return 0;
}

// Open New Vegas — texture cache tests.
//
// Builds a real on-disk Data fixture: a synthetic BSA (writeBsa technique from
// data_files_tests.cpp) containing one small uncompressed-BGRA DDS surface
// (writeHeader/Builder technique from dds_tests.cpp) under the internal path
// "textures\\test\\stone.dds". Then exercises the TextureCache: prefix
// fallback, case-insensitivity, cached failures, and entry counting. Ships no
// game data.

#include "../assets/data_files.hpp"
#include "../formats/bsa.hpp"
#include "../render/texture_cache.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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

// ── Little-endian byte buffer builder (matches the other test files) ───────
struct Builder {
    std::vector<std::uint8_t> buf;
    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
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
    std::size_t size() const { return buf.size(); }
};

constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
constexpr std::uint32_t DDPF_RGB         = 0x40;

// Emit "DDS " magic + a 124-byte DDS_HEADER (dds_tests.cpp technique).
void writeHeader(Builder& b, std::uint32_t width, std::uint32_t height,
                 std::uint32_t pfFlags, std::uint32_t rgbBitCount,
                 std::uint32_t rMask, std::uint32_t gMask, std::uint32_t bMask,
                 std::uint32_t aMask) {
    b.tag("DDS ");
    b.u32(124);          // dwSize
    b.u32(0x1007);       // dwFlags
    b.u32(height);
    b.u32(width);
    b.u32(0);            // pitchOrLinearSize
    b.u32(0);            // depth
    b.u32(1);            // mipMapCount
    for (int i = 0; i < 11; ++i) b.u32(0); // dwReserved1[11]

    // DDS_PIXELFORMAT (32 bytes)
    b.u32(32);           // pfSize
    b.u32(pfFlags);
    b.u32(0);            // fourCC (unused for uncompressed RGB)
    b.u32(rgbBitCount);
    b.u32(rMask);
    b.u32(gMask);
    b.u32(bMask);
    b.u32(aMask);

    for (int i = 0; i < 5; ++i) b.u32(0); // dwCaps[4] + dwReserved2
}

// A 2x2 uncompressed 32-bit BGRA DDS. Pixel (0,0) is a known sentinel colour.
std::vector<std::uint8_t> makeStoneDds() {
    struct P { std::uint8_t r, g, b, a; };
    const P pixels[4] = {
        {11, 22, 33, 255}, // (0,0) — the pixel we assert on
        {200, 150, 100, 50},
        {0, 0, 0, 255},
        {255, 255, 255, 0},
    };
    Builder b;
    writeHeader(b, 2, 2, DDPF_RGB | DDPF_ALPHAPIXELS, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    for (const auto& p : pixels) {
        const std::uint32_t v = (static_cast<std::uint32_t>(p.a) << 24) |
                                (static_cast<std::uint32_t>(p.r) << 16) |
                                (static_cast<std::uint32_t>(p.g) << 8) |
                                static_cast<std::uint32_t>(p.b);
        b.u32(v);
    }
    return b.buf;
}

// Write a minimal BSA v104 with one folder and one stored (uncompressed) file
// (writeBsa technique from data_files_tests.cpp, generalized over names/data).
void writeBsa(const std::string& path, const std::string& folderName,
              const std::string& fileName,
              const std::vector<std::uint8_t>& fileData) {
    const std::string names = fileName + std::string(1, '\0');
    const std::uint32_t totalFileNameLength =
        static_cast<std::uint32_t>(names.size());

    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlockStart = folderRecOffset + 16;
    const std::uint32_t fileRecBlockSize =
        1 + static_cast<std::uint32_t>(folderName.size()) + 1 + 16;
    const std::uint32_t nameBlockStart = fileRecBlockStart + fileRecBlockSize;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2);              // dir names + file names, uncompressed
    b.u32(1);                      // folderCount
    b.u32(1);                      // fileCount
    b.u32(static_cast<std::uint32_t>(folderName.size()) + 1);
    b.u32(totalFileNameLength);
    b.u32(0);                      // fileFlags

    // Folder record (hash value is not used for VFS lookup — path is name-based)
    b.u64(onv::bsa::hashPart("test", ""));
    b.u32(1);                      // file count
    b.u32(fileRecBlockStart + totalFileNameLength);

    // File record block
    b.bzstring(folderName);
    b.u64(onv::bsa::hashPart("stone", ".dds"));
    b.u32(static_cast<std::uint32_t>(fileData.size()));
    b.u32(dataStart);

    // Name block + data
    b.raw(names.data(), names.size());
    b.bytes(fileData);

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.buf.data()),
            static_cast<std::streamsize>(b.size()));
}

} // namespace

int main() {
    const fs::path data = "/tmp/onv_tex_fixtures/Data";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);

    const auto dds = makeStoneDds();
    // Internal path becomes "textures\\test\\stone.dds".
    writeBsa((data / "Test.bsa").string(), "textures\\test", "stone.dds", dds);

    onv::assets::DataFiles vfs(data.string());
    onv::render::TextureCache cache(vfs);

    // 1) Full path resolves with expected dimensions + a known pixel.
    const onv::dds::Image* full = cache.get("textures\\test\\stone.dds");
    CHECK(full != nullptr);
    if (full) {
        CHECK(full->width == 2 && full->height == 2);
        // Pixel (0,0) = {11, 22, 33, 255} in RGBA order.
        CHECK(full->rgba.size() == 16);
        CHECK(full->rgba[0] == 11 && full->rgba[1] == 22 &&
              full->rgba[2] == 33 && full->rgba[3] == 255);
    }
    CHECK(cache.size() == 1);

    // 2) No-prefix path resolves via the "textures\\" fallback.
    const onv::dds::Image* noPrefix = cache.get("test\\stone.dds");
    CHECK(noPrefix != nullptr);
    // Distinct cache key ("test\\stone.dds") -> a second entry, same content.
    CHECK(cache.size() == 2);
    if (noPrefix) {
        CHECK(noPrefix->width == 2 && noPrefix->height == 2);
        CHECK(noPrefix->rgba[0] == 11 && noPrefix->rgba[3] == 255);
    }

    // 3) Case-insensitive: shares the entry created by call (1).
    const onv::dds::Image* upper = cache.get("TEXTURES\\Test\\STONE.DDS");
    CHECK(upper == full);          // same normalized key -> same cached pointer
    CHECK(cache.size() == 2);      // no new entry

    // 4) Forward slashes normalize identically too.
    const onv::dds::Image* slashes = cache.get("textures/test/stone.dds");
    CHECK(slashes == full);
    CHECK(cache.size() == 2);

    // 5) Missing path returns nullptr; asking twice is safe (failure cached).
    const onv::dds::Image* miss1 = cache.get("textures\\nope\\absent.dds");
    CHECK(miss1 == nullptr);
    CHECK(cache.size() == 3);      // the failure is now cached
    const onv::dds::Image* miss2 = cache.get("textures\\nope\\absent.dds");
    CHECK(miss2 == nullptr);
    CHECK(cache.size() == 3);      // still cached, no new entry, no crash

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all texture-cache tests passed "
                "(prefix fallback, ci keys, cached failures)\n");
    return 0;
}

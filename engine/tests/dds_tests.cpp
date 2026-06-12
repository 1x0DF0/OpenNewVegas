// Open New Vegas — DDS decoder tests.
//
// We never ship game data, so these tests build small synthetic DDS byte
// buffers (Builder-style, matching format_tests.cpp) and assert the decoder
// produces the exact RGBA pixels demanded by the documented BCn interpolation
// rules and uncompressed bit-mask extraction.

#include "../formats/dds.hpp"

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
    void tag(const char* t) { raw(t, 4); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    std::size_t size() const { return buf.size(); }
};

constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
constexpr std::uint32_t DDPF_FOURCC      = 0x4;
constexpr std::uint32_t DDPF_RGB         = 0x40;

// Emit "DDS " magic + a 124-byte DDS_HEADER. The pixel-format fields vary per
// surface type; everything else is filled with plausible/zero values.
void writeHeader(Builder& b, std::uint32_t width, std::uint32_t height,
                 std::uint32_t pfFlags, const char* fourCC,
                 std::uint32_t rgbBitCount, std::uint32_t rMask,
                 std::uint32_t gMask, std::uint32_t bMask, std::uint32_t aMask) {
    b.tag("DDS ");
    b.u32(124);          // dwSize
    b.u32(0x1007);       // dwFlags (CAPS|HEIGHT|WIDTH|PIXELFORMAT)
    b.u32(height);
    b.u32(width);
    b.u32(0);            // pitchOrLinearSize
    b.u32(0);            // depth
    b.u32(1);            // mipMapCount
    for (int i = 0; i < 11; ++i) b.u32(0); // dwReserved1[11]

    // DDS_PIXELFORMAT (32 bytes)
    b.u32(32);           // pfSize
    b.u32(pfFlags);
    if (pfFlags & DDPF_FOURCC) b.tag(fourCC);
    else b.u32(0);
    b.u32(rgbBitCount);
    b.u32(rMask);
    b.u32(gMask);
    b.u32(bMask);
    b.u32(aMask);

    // dwCaps[4] + dwReserved2 (5 * uint32)
    for (int i = 0; i < 5; ++i) b.u32(0);
}

std::uint16_t pack565(std::uint8_t r5, std::uint8_t g6, std::uint8_t b5) {
    return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

// Expected exact 8-bit reconstruction of a 5/6-bit endpoint, matching the
// decoder's bit-replication (e.g. 5-bit 0x1F -> 0xFF, 0x00 -> 0x00).
std::uint8_t expand5(std::uint8_t v5) {
    return static_cast<std::uint8_t>((v5 << 3) | (v5 >> 2));
}
std::uint8_t expand6(std::uint8_t v6) {
    return static_cast<std::uint8_t>((v6 << 2) | (v6 >> 4));
}

const std::uint8_t* px(const onv::dds::Image& img, int x, int y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
}

// ── Test 1: a single 4x4 DXT1 block, 4-colour mode (c0 > c1) ───────────────
// Endpoints chosen so the interpolated colours are easy to verify, and the
// 16 indices laid out so each of the 4 palette entries lands on a known texel.
void testDxt1Block() {
    // c0 = pure red (max), c1 = pure blue (max). c0 > c1 numerically, so the
    // block is in opaque 4-colour mode.
    const std::uint16_t c0 = pack565(0x1F, 0x00, 0x00); // red
    const std::uint16_t c1 = pack565(0x00, 0x00, 0x1F); // blue
    // 4-colour palette:
    //   0 = c0 (red), 1 = c1 (blue),
    //   2 = (2*c0 + c1)/3, 3 = (c0 + 2*c1)/3
    // All opaque (alpha 255).

    // Index layout (texel order is row-major, 2 bits each, low texel = bit 0):
    // We place index 0 at texel(0,0), index 1 at texel(3,0),
    // index 2 at texel(0,3), index 3 at texel(3,3); rest = 0.
    std::uint32_t bits = 0;
    auto setIdx = [&](int x, int y, std::uint8_t sel) {
        const int i = y * 4 + x;
        bits |= static_cast<std::uint32_t>(sel & 0x3) << (2 * i);
    };
    setIdx(0, 0, 0);
    setIdx(3, 0, 1);
    setIdx(0, 3, 2);
    setIdx(3, 3, 3);

    Builder b;
    writeHeader(b, 4, 4, DDPF_FOURCC, "DXT1", 0, 0, 0, 0, 0);
    b.u16(c0);
    b.u16(c1);
    b.u32(bits);

    const auto img = onv::dds::decode(b.buf);
    CHECK(img.width == 4 && img.height == 4);

    // Endpoint reconstructions.
    const std::uint8_t rMax = expand5(0x1F); // 255
    const std::uint8_t bMax = expand5(0x1F); // 255

    // texel(0,0) = c0 = red, opaque
    const std::uint8_t* p00 = px(img, 0, 0);
    CHECK(p00[0] == rMax && p00[1] == 0 && p00[2] == 0 && p00[3] == 255);

    // texel(3,0) = c1 = blue, opaque
    const std::uint8_t* p30 = px(img, 3, 0);
    CHECK(p30[0] == 0 && p30[1] == 0 && p30[2] == bMax && p30[3] == 255);

    // texel(0,3) = index 2 = (2*c0 + c1)/3.
    // Endpoint RGB8: c0=(255,0,0), c1=(0,0,255).
    const std::uint8_t expR2 = static_cast<std::uint8_t>((2 * rMax + 0) / 3);
    const std::uint8_t expB2 = static_cast<std::uint8_t>((0 + bMax) / 3);
    const std::uint8_t* p03 = px(img, 0, 3);
    CHECK(p03[0] == expR2 && p03[1] == 0 && p03[2] == expB2 && p03[3] == 255);

    // texel(3,3) = index 3 = (c0 + 2*c1)/3.
    const std::uint8_t expR3 = static_cast<std::uint8_t>((rMax + 0) / 3);
    const std::uint8_t expB3 = static_cast<std::uint8_t>((0 + 2 * bMax) / 3);
    const std::uint8_t* p33 = px(img, 3, 3);
    CHECK(p33[0] == expR3 && p33[1] == 0 && p33[2] == expB3 && p33[3] == 255);

    std::printf("DXT1 4-colour block: endpoints + 1/3,2/3 interpolants exact\n");
}

// ── Test 2: a 4x4 DXT1 block in 3-colour / punch-through mode (c0 <= c1) ───
// index 2 = (c0+c1)/2, index 3 = transparent black.
void testDxt1PunchThrough() {
    // c0 = blue, c1 = red, so c0 < c1 -> 3-colour mode.
    const std::uint16_t c0 = pack565(0x00, 0x00, 0x1F); // blue (smaller value)
    const std::uint16_t c1 = pack565(0x1F, 0x00, 0x00); // red

    std::uint32_t bits = 0;
    auto setIdx = [&](int x, int y, std::uint8_t sel) {
        const int i = y * 4 + x;
        bits |= static_cast<std::uint32_t>(sel & 0x3) << (2 * i);
    };
    setIdx(0, 0, 2); // half-blend
    setIdx(1, 0, 3); // transparent black

    Builder b;
    writeHeader(b, 4, 4, DDPF_FOURCC, "DXT1", 0, 0, 0, 0, 0);
    b.u16(c0);
    b.u16(c1);
    b.u32(bits);

    const auto img = onv::dds::decode(b.buf);
    const std::uint8_t cMax = expand5(0x1F); // 255

    // index 2 = (c0+c1)/2: c0=(0,0,255), c1=(255,0,0) -> (127,0,127), opaque.
    const std::uint8_t* p00 = px(img, 0, 0);
    CHECK(p00[0] == static_cast<std::uint8_t>(cMax / 2));
    CHECK(p00[1] == 0);
    CHECK(p00[2] == static_cast<std::uint8_t>(cMax / 2));
    CHECK(p00[3] == 255);

    // index 3 = transparent black.
    const std::uint8_t* p10 = px(img, 1, 0);
    CHECK(p10[0] == 0 && p10[1] == 0 && p10[2] == 0 && p10[3] == 0);

    std::printf("DXT1 3-colour block: half-blend + transparent-black exact\n");
}

// ── Test 3: a 4x4 DXT5 block with interpolated alpha (a0 > a1) ─────────────
void testDxt5Alpha() {
    // Alpha endpoints a0=255, a1=0, a0>a1 -> 8-value interpolation.
    // alpha palette: a[0]=255, a[1]=0, a[2..7] = ((7-i)*255 + i*0)/7.
    const std::uint8_t a0 = 255, a1 = 0;

    // Put alpha index 0 at texel(0,0) -> 255, index 1 at texel(1,0) -> 0,
    // index 2 at texel(2,0) -> (6*255)/7 = 218.
    std::uint64_t aBits = 0;
    auto setAIdx = [&](int x, int y, std::uint8_t sel) {
        const int i = y * 4 + x;
        aBits |= static_cast<std::uint64_t>(sel & 0x7) << (3 * i);
    };
    setAIdx(0, 0, 0);
    setAIdx(1, 0, 1);
    setAIdx(2, 0, 2);

    // Colour block: c0 green > c1 black, opaque 4-colour. We only check colours
    // at the alpha-test texels; index all of them to 0 = c0 = green.
    const std::uint16_t c0 = pack565(0x00, 0x3F, 0x00); // green
    const std::uint16_t c1 = pack565(0x00, 0x00, 0x00); // black
    const std::uint32_t colorBits = 0; // every texel selects index 0 = c0

    Builder b;
    writeHeader(b, 4, 4, DDPF_FOURCC, "DXT5", 0, 0, 0, 0, 0);
    // Alpha block: a0, a1, then 6 bytes of 3-bit indices (little-endian).
    b.u8(a0);
    b.u8(a1);
    for (int i = 0; i < 6; ++i)
        b.u8(static_cast<std::uint8_t>((aBits >> (8 * i)) & 0xFF));
    // Colour block.
    b.u16(c0);
    b.u16(c1);
    b.u32(colorBits);

    const auto img = onv::dds::decode(b.buf);
    const std::uint8_t gMax = expand6(0x3F); // 255

    // All colour texels are c0 = green.
    const std::uint8_t* p00 = px(img, 0, 0);
    CHECK(p00[0] == 0 && p00[1] == gMax && p00[2] == 0);
    CHECK(p00[3] == 255);            // alpha index 0 -> a0 = 255

    const std::uint8_t* p10 = px(img, 1, 0);
    CHECK(p10[3] == 0);              // alpha index 1 -> a1 = 0

    const std::uint8_t* p20 = px(img, 2, 0);
    CHECK(p20[3] == static_cast<std::uint8_t>((6 * 255 + 0) / 7)); // index 2

    std::printf("DXT5 alpha block: 8-value interpolation exact (255/0/218)\n");
}

// ── Test 4: uncompressed 32-bit BGRA round-trips exactly ───────────────────
void testUncompressedBgra() {
    // 2x2 BGRA8888: masks B=0x000000FF, G=0x0000FF00, R=0x00FF0000,
    // A=0xFF000000. Pixels stored as little-endian uint32 = (A<<24)|(R<<16)|
    // (G<<8)|B.
    struct P { std::uint8_t r, g, b, a; };
    const P pixels[4] = {
        {10, 20, 30, 40},
        {200, 150, 100, 50},
        {0, 0, 0, 255},
        {255, 255, 255, 0},
    };

    Builder b;
    writeHeader(b, 2, 2, DDPF_RGB | DDPF_ALPHAPIXELS, nullptr, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    for (const auto& p : pixels) {
        const std::uint32_t v = (static_cast<std::uint32_t>(p.a) << 24) |
                                (static_cast<std::uint32_t>(p.r) << 16) |
                                (static_cast<std::uint32_t>(p.g) << 8) |
                                static_cast<std::uint32_t>(p.b);
        b.u32(v);
    }

    const auto img = onv::dds::decode(b.buf);
    CHECK(img.width == 2 && img.height == 2);
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t* p = img.rgba.data() + i * 4;
        CHECK(p[0] == pixels[i].r);
        CHECK(p[1] == pixels[i].g);
        CHECK(p[2] == pixels[i].b);
        CHECK(p[3] == pixels[i].a);
    }
    std::printf("Uncompressed 32-bit BGRA: 4 pixels round-tripped exactly\n");
}

// ── Test 5: malformed input throws ─────────────────────────────────────────
void testErrors() {
    bool threw = false;
    try {
        std::vector<std::uint8_t> garbage = {'X', 'Y', 'Z', '!', 0, 1, 2, 3};
        onv::dds::decode(garbage);
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    // Unsupported FourCC (BC5 "ATI2") should throw.
    threw = false;
    try {
        Builder b;
        writeHeader(b, 4, 4, DDPF_FOURCC, "ATI2", 0, 0, 0, 0, 0);
        for (int i = 0; i < 16; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    std::printf("Error paths: bad magic + unsupported FourCC both throw\n");
}

} // namespace

int main() {
    testDxt1Block();
    testDxt1PunchThrough();
    testDxt5Alpha();
    testUncompressedBgra();
    testErrors();

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all dds tests passed\n");
    return 0;
}

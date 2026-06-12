// Open New Vegas — DDS texture decoder implementation.
//
// Container: 4-byte magic "DDS ", then a 124-byte DDS_HEADER, then surface
// data (top mip first). The header embeds a 32-byte DDS_PIXELFORMAT that tells
// us whether the surface is uncompressed (with channel bit masks) or
// block-compressed (with a FourCC such as "DXT1"/"DXT3"/"DXT5").
//
// We decode only the top mip to RGBA8 (top-down). Supported:
//   - Uncompressed 32-bit BGRA / RGBA / BGRX / RGBX (via the bit masks)
//   - BC1 / DXT1  (1-bit punch-through alpha)
//   - BC2 / DXT3  (explicit 4-bit alpha)
//   - BC3 / DXT5  (interpolated alpha)
//
// NOT handled (throws): DX10-extended header (FourCC "DX10"), BC4/BC5/BC7,
// 16/24-bit uncompressed surfaces, paletted surfaces, cubemaps/volumes beyond
// their first 2D face. These are rare-to-absent in FNV's texture set.

#include "dds.hpp"
#include "binary_reader.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace onv::dds {
namespace {

// DDS_PIXELFORMAT.dwFlags bits (Microsoft DDS docs)
constexpr std::uint32_t DDPF_ALPHAPIXELS = 0x1;
constexpr std::uint32_t DDPF_FOURCC      = 0x4;
constexpr std::uint32_t DDPF_RGB         = 0x40;

std::uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
}

struct PixelFormat {
    std::uint32_t flags = 0;
    std::uint32_t fourCC = 0;
    std::uint32_t rgbBitCount = 0;
    std::uint32_t rMask = 0, gMask = 0, bMask = 0, aMask = 0;
};

struct Header {
    std::uint32_t height = 0;
    std::uint32_t width = 0;
    PixelFormat pf;
};

// A single RGBA pixel write into a top-down image at (x,y), clamped so blocks
// that overhang the image edge (non-multiple-of-4 dimensions) are ignored.
inline void put(Image& img, int x, int y, std::uint8_t r, std::uint8_t g,
                std::uint8_t b, std::uint8_t a) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    img.rgba[i + 0] = r;
    img.rgba[i + 1] = g;
    img.rgba[i + 2] = b;
    img.rgba[i + 3] = a;
}

// Expand a 16-bit 5:6:5 colour to 8-bit RGB. The 5/6-bit values are scaled to
// the full 0..255 range by replicating the high bits into the low bits, which
// is the standard exact-endpoint reconstruction (e.g. 0x1F -> 0xFF).
struct Rgb { std::uint8_t r, g, b; };
inline Rgb unpack565(std::uint16_t c) {
    std::uint8_t r5 = (c >> 11) & 0x1F;
    std::uint8_t g6 = (c >> 5) & 0x3F;
    std::uint8_t b5 = c & 0x1F;
    Rgb o;
    o.r = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
    o.g = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
    o.b = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
    return o;
}

// Decode the 4 colour endpoints/interpolants of a BC1 colour block.
// `opaqueMode` is true when c0 > c1 (4-colour mode); when false the block uses
// the 3-colour mode where index 3 is transparent black. For BC2/BC3 the colour
// block always uses the 4-colour interpolation regardless of endpoint order.
struct ColorTable {
    std::array<Rgb, 4> color;
    std::array<std::uint8_t, 4> alpha; // BC1 punch-through; 255 elsewhere
};

ColorTable decodeColorBlock(std::uint16_t c0, std::uint16_t c1,
                            bool bc1PunchThrough) {
    ColorTable t;
    const Rgb a = unpack565(c0);
    const Rgb b = unpack565(c1);
    t.color[0] = a;
    t.color[1] = b;
    t.alpha = {255, 255, 255, 255};

    if (bc1PunchThrough && c0 <= c1) {
        // 3-colour mode: 1/2 blend, plus a transparent-black slot.
        t.color[2] = {static_cast<std::uint8_t>((a.r + b.r) / 2),
                      static_cast<std::uint8_t>((a.g + b.g) / 2),
                      static_cast<std::uint8_t>((a.b + b.b) / 2)};
        t.color[3] = {0, 0, 0};
        t.alpha[3] = 0;
    } else {
        // 4-colour mode: 1/3 and 2/3 blends.
        t.color[2] = {static_cast<std::uint8_t>((2 * a.r + b.r) / 3),
                      static_cast<std::uint8_t>((2 * a.g + b.g) / 3),
                      static_cast<std::uint8_t>((2 * a.b + b.b) / 3)};
        t.color[3] = {static_cast<std::uint8_t>((a.r + 2 * b.r) / 3),
                      static_cast<std::uint8_t>((a.g + 2 * b.g) / 3),
                      static_cast<std::uint8_t>((a.b + 2 * b.b) / 3)};
    }
    return t;
}

// Decode the colour portion of one 4x4 block (8 bytes) into `img`, applying an
// optional per-texel alpha array (16 entries) supplied by the caller (BC2/BC3).
// When `alphaOverride` is null, BC1 punch-through alpha is used.
void decodeColorTexels(BinaryReader& r, Image& img, int bx, int by,
                       bool bc1PunchThrough,
                       const std::array<std::uint8_t, 16>* alphaOverride) {
    const std::uint16_t c0 = r.read<std::uint16_t>();
    const std::uint16_t c1 = r.read<std::uint16_t>();
    const std::uint32_t bits = r.read<std::uint32_t>();
    const ColorTable t = decodeColorBlock(c0, c1, bc1PunchThrough);

    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            const int idx = (py * 4 + px);
            const std::uint8_t sel = (bits >> (2 * idx)) & 0x3;
            const Rgb& col = t.color[sel];
            std::uint8_t alpha = alphaOverride ? (*alphaOverride)[idx]
                                               : t.alpha[sel];
            put(img, bx + px, by + py, col.r, col.g, col.b, alpha);
        }
    }
}

// BC3/DXT5 alpha block: two 8-bit endpoints a0,a1 followed by 16 3-bit indices
// packed into 6 bytes (48 bits). Produces a per-texel alpha array.
std::array<std::uint8_t, 16> decodeDxt5Alpha(BinaryReader& r) {
    const std::uint8_t a0 = r.read<std::uint8_t>();
    const std::uint8_t a1 = r.read<std::uint8_t>();

    std::array<std::uint8_t, 8> a{};
    a[0] = a0;
    a[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i)
            a[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
    } else {
        for (int i = 1; i <= 4; ++i)
            a[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        a[6] = 0;
        a[7] = 255;
    }

    // 48 bits of 3-bit indices, little-endian byte order.
    std::uint8_t raw[6];
    r.readBytes(raw, 6);
    std::uint64_t packed = 0;
    for (int i = 0; i < 6; ++i)
        packed |= static_cast<std::uint64_t>(raw[i]) << (8 * i);

    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 16; ++i) {
        const std::uint8_t sel = (packed >> (3 * i)) & 0x7;
        out[i] = a[sel];
    }
    return out;
}

// BC2/DXT3 alpha block: 16 explicit 4-bit alpha values (8 bytes), one nibble
// per texel in row-major order, low nibble first.
std::array<std::uint8_t, 16> decodeDxt3Alpha(BinaryReader& r) {
    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t byte = r.read<std::uint8_t>();
        const std::uint8_t lo = byte & 0x0F;
        const std::uint8_t hi = (byte >> 4) & 0x0F;
        // Scale 4-bit (0..15) to 8-bit (0..255) by nibble replication.
        out[2 * i + 0] = static_cast<std::uint8_t>((lo << 4) | lo);
        out[2 * i + 1] = static_cast<std::uint8_t>((hi << 4) | hi);
    }
    return out;
}

Header readHeader(BinaryReader& r) {
    if (r.readTag() != std::string("DDS ", 4))
        throw std::runtime_error("not a DDS file (missing 'DDS ' magic)");

    const std::uint32_t headerSize = r.read<std::uint32_t>();
    if (headerSize != 124)
        throw std::runtime_error("bad DDS header size (expected 124, got " +
                                 std::to_string(headerSize) + ")");

    r.read<std::uint32_t>(); // flags
    Header h;
    h.height = r.read<std::uint32_t>();
    h.width = r.read<std::uint32_t>();
    r.read<std::uint32_t>(); // pitchOrLinearSize
    r.read<std::uint32_t>(); // depth
    r.read<std::uint32_t>(); // mipMapCount
    for (int i = 0; i < 11; ++i) r.read<std::uint32_t>(); // dwReserved1[11]

    // DDS_PIXELFORMAT (32 bytes)
    const std::uint32_t pfSize = r.read<std::uint32_t>();
    if (pfSize != 32)
        throw std::runtime_error("bad DDS pixelformat size (expected 32)");
    h.pf.flags = r.read<std::uint32_t>();
    h.pf.fourCC = r.read<std::uint32_t>();
    h.pf.rgbBitCount = r.read<std::uint32_t>();
    h.pf.rMask = r.read<std::uint32_t>();
    h.pf.gMask = r.read<std::uint32_t>();
    h.pf.bMask = r.read<std::uint32_t>();
    h.pf.aMask = r.read<std::uint32_t>();

    // Remainder of DDS_HEADER: dwCaps[4] + dwReserved2 (5 * uint32).
    for (int i = 0; i < 5; ++i) r.read<std::uint32_t>();

    if (h.width == 0 || h.height == 0)
        throw std::runtime_error("DDS has zero dimension");
    return h;
}

// Number of right-shifts needed to move a mask's lowest set bit to bit 0.
int maskShift(std::uint32_t mask) {
    if (mask == 0) return 0;
    int s = 0;
    while ((mask & 1) == 0) { mask >>= 1; ++s; }
    return s;
}

// Extract an 8-bit channel value given a mask. Masks narrower than 8 bits are
// scaled up to the full 0..255 range by replicating the value's high bits into
// the freed low bits (the same exact-endpoint reconstruction used for 565).
std::uint8_t extractChannel(std::uint32_t pixel, std::uint32_t mask) {
    if (mask == 0) return 0;
    const int shift = maskShift(mask);
    const std::uint32_t v = (pixel & mask) >> shift;
    const std::uint32_t maxVal = mask >> shift; // all-ones for the channel
    // Number of significant bits in the channel.
    int bits = 0;
    std::uint32_t w = maxVal;
    while (w) { ++bits; w >>= 1; }
    if (bits >= 8) return static_cast<std::uint8_t>(v >> (bits - 8));
    // Scale v (range 0..2^bits-1) to 0..255 with rounding.
    const std::uint32_t scaled = (v * 255 + maxVal / 2) / maxVal;
    return static_cast<std::uint8_t>(scaled & 0xFF);
}

void decodeUncompressed32(BinaryReader& r, const Header& h, Image& img) {
    const PixelFormat& pf = h.pf;
    const bool hasAlpha = pf.flags & DDPF_ALPHAPIXELS;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const std::uint32_t pixel = r.read<std::uint32_t>();
            const std::uint8_t rr = extractChannel(pixel, pf.rMask);
            const std::uint8_t gg = extractChannel(pixel, pf.gMask);
            const std::uint8_t bb = extractChannel(pixel, pf.bMask);
            const std::uint8_t aa =
                hasAlpha ? extractChannel(pixel, pf.aMask) : 255;
            put(img, x, y, rr, gg, bb, aa);
        }
    }
}

void decodeBlockCompressed(BinaryReader& r, const Header& h, Image& img,
                           int format /* 1,3,5 */) {
    const int blocksX = (img.width + 3) / 4;
    const int blocksY = (img.height + 3) / 4;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            if (format == 1) {
                decodeColorTexels(r, img, bx * 4, by * 4,
                                  /*bc1PunchThrough=*/true, nullptr);
            } else if (format == 3) {
                const auto alpha = decodeDxt3Alpha(r);
                decodeColorTexels(r, img, bx * 4, by * 4, false, &alpha);
            } else { // format == 5
                const auto alpha = decodeDxt5Alpha(r);
                decodeColorTexels(r, img, bx * 4, by * 4, false, &alpha);
            }
        }
    }
}

} // namespace

Image decode(const std::vector<std::uint8_t>& bytes) {
    BinaryReader r(bytes);
    const Header h = readHeader(r);

    Image img;
    img.width = static_cast<int>(h.width);
    img.height = static_cast<int>(h.height);
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);

    const PixelFormat& pf = h.pf;
    if (pf.flags & DDPF_FOURCC) {
        if (pf.fourCC == fourcc('D', 'X', 'T', '1'))
            decodeBlockCompressed(r, h, img, 1);
        else if (pf.fourCC == fourcc('D', 'X', 'T', '3'))
            decodeBlockCompressed(r, h, img, 3);
        else if (pf.fourCC == fourcc('D', 'X', 'T', '5'))
            decodeBlockCompressed(r, h, img, 5);
        else {
            char c[5] = {static_cast<char>(pf.fourCC & 0xFF),
                         static_cast<char>((pf.fourCC >> 8) & 0xFF),
                         static_cast<char>((pf.fourCC >> 16) & 0xFF),
                         static_cast<char>((pf.fourCC >> 24) & 0xFF), 0};
            throw std::runtime_error(std::string("unsupported DDS FourCC '") +
                                     c + "'");
        }
    } else if (pf.flags & DDPF_RGB) {
        if (pf.rgbBitCount != 32)
            throw std::runtime_error(
                "unsupported uncompressed DDS bit depth " +
                std::to_string(pf.rgbBitCount) + " (only 32-bit supported)");
        decodeUncompressed32(r, h, img);
    } else {
        throw std::runtime_error("unsupported DDS pixel format (neither "
                                 "FourCC nor RGB flag set)");
    }

    return img;
}

} // namespace onv::dds

// Open New Vegas — NIF (NetImmerse/Gamebryo) mesh reader implementation.
//
// Target format: Gamebryo NIF as written by Fallout: New Vegas —
//   header string "Gamebryo File Format, Version 20.2.0.7"
//   Version       0x14020007
//   User Version  11
//   BS Version    34   (Bethesda stream header)
//
// Clean-room from the niftools nif.xml schema for this exact version. We parse
// the full header (so we can locate and size every block precisely) and then
// walk the blocks, extracting geometry from the two static-mesh data blocks:
//   - NiTriShapeData  : an explicit triangle list
//   - NiTriStripsData : triangle strips, converted here to a triangle list
//
// WHAT IS HANDLED
//   * Header: header string line, version, endian byte, user version,
//     num blocks, Bethesda stream header (BS version + Author/Process/Export
//     export-strings), num block types, block-type strings, per-block type
//     index, per-block size, the string table, and groups.
//   * Geometry: vertex positions (Vector3) and triangle indices from
//     NiTriShapeData and NiTriStripsData. Strips are de-stripped to a list.
//   * Block-size table is used to skip every block we don't decode, so unknown
//     block types never derail parsing.
//
// WHAT IS NOT HANDLED (intentionally, for this milestone)
//   * Materials, textures, shaders (BSLightingShaderProperty, etc.).
//   * Normals, tangents, bitangents, vertex colours, UV coordinates — we read
//     past them to reach the triangle data, but do not surface them.
//   * Skinning / animation / morph data, NiSkinInstance, controllers.
//   * The scene-graph transform hierarchy: vertices are returned in their
//     local NiTriShape space (no NiNode world-transform composition yet).
//   * Big-endian files (Xbox 360 / PS3 assets); only endian byte 1 (little)
//     is accepted. Other NIF versions are rejected.
//   * Compressed/packed vertex data (Compress Flags != 0) — rejected.

#include "nif.hpp"
#include "binary_reader.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace onv::nif {
namespace {

constexpr std::uint32_t VERSION_20_2_0_7 = 0x14020007;

// BSGeometryDataFlags bits (niftools nif.xml, BSGeometryDataFlags).
constexpr std::uint16_t BSGDF_HAS_UV       = 0x0001;
constexpr std::uint16_t BSGDF_HAS_TANGENTS = 0x1000;

// A NIF header string line is terminated by a single 0x0A newline (no length
// prefix). Read up to and consuming the newline.
std::string readHeaderLine(BinaryReader& r) {
    std::string s;
    while (true) {
        const char c = r.read<char>();
        if (c == '\n') break;
        s.push_back(c);
    }
    return s;
}

// SizedString: uint32 length followed by that many chars (no NUL).
std::string readSizedString(BinaryReader& r) {
    const std::uint32_t len = r.read<std::uint32_t>();
    std::string s(len, '\0');
    if (len) r.readBytes(s.data(), len);
    return s;
}

// ExportString (Bethesda export info): a single byte length that INCLUDES the
// trailing NUL, followed by that many chars.
std::string readExportString(BinaryReader& r) {
    const std::uint8_t len = r.read<std::uint8_t>();
    std::string s(len, '\0');
    if (len) r.readBytes(s.data(), len);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

struct Header {
    std::uint32_t version = 0;
    std::uint32_t userVersion = 0;
    std::uint32_t bsVersion = 0;
    std::uint32_t numBlocks = 0;
    std::vector<std::string> blockTypes;       // distinct type names
    std::vector<std::uint16_t> blockTypeIndex; // per-block -> index into above
    std::vector<std::uint32_t> blockSizes;     // per-block byte size
};

Header readHeader(BinaryReader& r) {
    Header h;

    const std::string line = readHeaderLine(r);
    // Expected: "Gamebryo File Format, Version 20.2.0.7"
    if (line.find("Gamebryo File Format") == std::string::npos)
        throw std::runtime_error("not a Gamebryo NIF (header string: '" + line + "')");

    h.version = r.read<std::uint32_t>();
    if (h.version != VERSION_20_2_0_7)
        throw std::runtime_error(
            "unsupported NIF version 0x" + [](std::uint32_t v) {
                static const char* hex = "0123456789ABCDEF";
                std::string s(8, '0');
                for (int i = 7; i >= 0; --i) { s[i] = hex[v & 0xF]; v >>= 4; }
                return s;
            }(h.version) + " (only 20.2.0.7 / Fallout: New Vegas supported)");

    const std::uint8_t endian = r.read<std::uint8_t>();
    if (endian != 1)
        throw std::runtime_error("big-endian NIF not supported");

    h.userVersion = r.read<std::uint32_t>();
    h.numBlocks = r.read<std::uint32_t>();

    // Bethesda stream header (present for 20.2.0.7 with user version >= 3):
    // BS version (uint32), then export strings Author / Process / Export.
    h.bsVersion = r.read<std::uint32_t>();
    readExportString(r); // Author
    if (h.bsVersion < 131) readExportString(r); // Process Script (FNV: yes)
    readExportString(r); // Export Script

    // Block type table.
    const std::uint16_t numBlockTypes = r.read<std::uint16_t>();
    h.blockTypes.reserve(numBlockTypes);
    for (std::uint16_t i = 0; i < numBlockTypes; ++i)
        h.blockTypes.push_back(readSizedString(r));

    h.blockTypeIndex.reserve(h.numBlocks);
    for (std::uint32_t i = 0; i < h.numBlocks; ++i)
        h.blockTypeIndex.push_back(r.read<std::uint16_t>());

    // Per-block sizes (present since 20.2.0.5): lets us skip blocks we don't
    // decode without understanding their internals.
    h.blockSizes.reserve(h.numBlocks);
    for (std::uint32_t i = 0; i < h.numBlocks; ++i)
        h.blockSizes.push_back(r.read<std::uint32_t>());

    // String table (since 20.1.0.1).
    const std::uint32_t numStrings = r.read<std::uint32_t>();
    r.read<std::uint32_t>(); // Max String Length (unused)
    for (std::uint32_t i = 0; i < numStrings; ++i)
        readSizedString(r);

    // Groups (since 5.0.0.6).
    const std::uint32_t numGroups = r.read<std::uint32_t>();
    for (std::uint32_t i = 0; i < numGroups; ++i)
        r.read<std::uint32_t>();

    return h;
}

// Read the NiGeometryData base portion (common to NiTriShapeData and
// NiTriStripsData) up to and including the NiTriBasedGeomData "Num Triangles"
// field. Fills `vertices` (x,y,z triples) and returns numTriangles.
//
// Layout for 20.2.0.7 / BS 34 (niftools nif.xml NiGeometryData):
//   int32   Group ID
//   uint16  Num Vertices
//   uint8   Keep Flags
//   uint8   Compress Flags
//   bool    Has Vertices            (1 byte)
//   Vector3 Vertices[Num Vertices]  (if Has Vertices)
//   uint16  BS Data Flags
//   bool    Has Normals             (1 byte)
//   Vector3 Normals[N]              (if Has Normals)
//   Vector3 Tangents[N]             (if Has Normals && tangents bit)
//   Vector3 Bitangents[N]           (if Has Normals && tangents bit)
//   Vector3 Center; float Radius    (bounding sphere)
//   bool    Has Vertex Colors       (1 byte)
//   Color4  Vertex Colors[N]        (16 bytes each, if Has Vertex Colors)
//   TexCoord UV[N]                  (8 bytes each, count from flags)
//   uint16  Consistency Flags
//   int32   Additional Data (ref)
//   uint16  Num Triangles           (NiTriBasedGeomData)
std::uint16_t readGeometryBase(BinaryReader& r, std::vector<float>& vertices) {
    r.read<std::int32_t>();                              // Group ID
    const std::uint16_t numVertices = r.read<std::uint16_t>();
    r.read<std::uint8_t>();                              // Keep Flags
    const std::uint8_t compressFlags = r.read<std::uint8_t>();
    if (compressFlags != 0)
        throw std::runtime_error("compressed NIF vertex data not supported");

    const std::uint8_t hasVertices = r.read<std::uint8_t>();
    vertices.clear();
    if (hasVertices) {
        vertices.resize(static_cast<std::size_t>(numVertices) * 3);
        for (std::uint16_t i = 0; i < numVertices; ++i) {
            vertices[i * 3 + 0] = r.read<float>();
            vertices[i * 3 + 1] = r.read<float>();
            vertices[i * 3 + 2] = r.read<float>();
        }
    }

    const std::uint16_t bsDataFlags = r.read<std::uint16_t>();

    const std::uint8_t hasNormals = r.read<std::uint8_t>();
    if (hasNormals) {
        r.skip(static_cast<std::size_t>(numVertices) * 12); // Normals
        if (bsDataFlags & BSGDF_HAS_TANGENTS) {
            r.skip(static_cast<std::size_t>(numVertices) * 12); // Tangents
            r.skip(static_cast<std::size_t>(numVertices) * 12); // Bitangents
        }
    }

    r.skip(12 + 4); // Center (Vector3) + Radius (float)

    const std::uint8_t hasColors = r.read<std::uint8_t>();
    if (hasColors)
        r.skip(static_cast<std::size_t>(numVertices) * 16); // Color4[N]

    // UV-set count: for BS202 the generic Data Flags is 0, so the only UV bit
    // is BS Data Flags "Has UV" (0x0001) -> 0 or 1 set of Num Vertices coords.
    const int numUvSets = (bsDataFlags & BSGDF_HAS_UV) ? 1 : 0;
    r.skip(static_cast<std::size_t>(numUvSets) * numVertices * 8); // TexCoord

    r.read<std::uint16_t>(); // Consistency Flags
    r.read<std::int32_t>();  // Additional Data (ref)

    return r.read<std::uint16_t>(); // Num Triangles (NiTriBasedGeomData)
}

// NiTriShapeData specific fields (after the base + Num Triangles):
//   uint32 Num Triangle Points
//   bool   Has Triangles
//   Triangle Triangles[Num Triangles]   (3 * uint16 each, if Has Triangles)
//   uint16 Num Match Groups
//   MatchGroup Match Groups[...]
Mesh readTriShapeData(BinaryReader& r) {
    Mesh m;
    const std::uint16_t numTriangles = readGeometryBase(r, m.vertices);

    r.read<std::uint32_t>(); // Num Triangle Points (= numTriangles * 3)
    const std::uint8_t hasTriangles = r.read<std::uint8_t>();
    if (hasTriangles) {
        m.indices.resize(static_cast<std::size_t>(numTriangles) * 3);
        for (std::size_t i = 0; i < m.indices.size(); ++i)
            m.indices[i] = r.read<std::uint16_t>();
    }
    // Match groups (shared-normal vertex sets) are not needed for geometry.
    return m;
}

// NiTriStripsData specific fields (after the base + Num Triangles):
//   uint16 Num Strips
//   uint16 Strip Lengths[Num Strips]
//   bool   Has Points
//   uint16 Points[strip][Strip Lengths[strip]]   (if Has Points)
//
// We convert each strip to a triangle list. A triangle strip emits one
// triangle per index past the first two; winding alternates each step, and
// degenerate triangles (two equal indices) are dropped — this is the standard
// strip-to-list expansion.
Mesh readTriStripsData(BinaryReader& r) {
    Mesh m;
    readGeometryBase(r, m.vertices); // Num Triangles is informational here

    const std::uint16_t numStrips = r.read<std::uint16_t>();
    std::vector<std::uint16_t> stripLengths(numStrips);
    for (std::uint16_t i = 0; i < numStrips; ++i)
        stripLengths[i] = r.read<std::uint16_t>();

    const std::uint8_t hasPoints = r.read<std::uint8_t>();
    if (hasPoints) {
        for (std::uint16_t s = 0; s < numStrips; ++s) {
            const std::uint16_t len = stripLengths[s];
            std::vector<std::uint16_t> strip(len);
            for (std::uint16_t i = 0; i < len; ++i)
                strip[i] = r.read<std::uint16_t>();

            for (std::uint16_t i = 0; i + 2 < len; ++i) {
                const std::uint16_t a = strip[i];
                const std::uint16_t b = strip[i + 1];
                const std::uint16_t c = strip[i + 2];
                if (a == b || b == c || a == c) continue; // degenerate
                if (i & 1) { // odd: swap to keep consistent winding
                    m.indices.push_back(a);
                    m.indices.push_back(c);
                    m.indices.push_back(b);
                } else {
                    m.indices.push_back(a);
                    m.indices.push_back(b);
                    m.indices.push_back(c);
                }
            }
        }
    }
    return m;
}

} // namespace

std::vector<Mesh> decode(const std::vector<std::uint8_t>& bytes) {
    BinaryReader r(bytes);
    const Header h = readHeader(r);

    std::vector<Mesh> meshes;
    for (std::uint32_t i = 0; i < h.numBlocks; ++i) {
        const std::size_t blockStart = r.pos();
        const std::size_t blockSize = h.blockSizes[i];
        const std::uint16_t typeIdx = h.blockTypeIndex[i];
        const std::string& type =
            typeIdx < h.blockTypes.size() ? h.blockTypes[typeIdx] : std::string();

        if (type == "NiTriShapeData") {
            meshes.push_back(readTriShapeData(r));
        } else if (type == "NiTriStripsData") {
            meshes.push_back(readTriStripsData(r));
        }
        // Always resync to the next block via the size table, so partially-read
        // or unknown blocks cannot desynchronise the stream.
        r.seek(blockStart + blockSize);
    }
    return meshes;
}

} // namespace onv::nif

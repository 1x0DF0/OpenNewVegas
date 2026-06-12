// Open New Vegas — NIF (NetImmerse/Gamebryo) mesh reader.
//
// Fallout: New Vegas stores its 3D models as Gamebryo NIF files inside the BSA
// archives, with header string "Gamebryo File Format, Version 20.2.0.7"
// (version 0x14020007), user version 11, Bethesda stream version 34.
//
// Clean-room: implemented from the publicly-documented NIF layout (the
// niftools nif.xml schema for version 20.2.0.7 / user 11 / BS 34). We scope
// this reader to reliably extracting *static* mesh geometry — vertex positions
// and triangle indices — from NiTriShapeData and NiTriStripsData blocks. See
// nif.cpp for the precise list of what is and isn't handled.

#pragma once

#include <cstdint>
#include <vector>

namespace onv::nif {

struct Mesh {
    std::vector<float> vertices;          // x,y,z triples (game units)
    std::vector<std::uint16_t> indices;   // triangle list
};

// Decode the geometry meshes from a NIF file's bytes.
// Throws std::runtime_error on unsupported version or malformed input.
std::vector<Mesh> decode(const std::vector<std::uint8_t>& bytes);

} // namespace onv::nif

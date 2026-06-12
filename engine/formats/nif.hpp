// Open New Vegas — NIF (NetImmerse/Gamebryo) mesh reader.
// STUB — to be implemented.
#pragma once

#include <cstdint>
#include <vector>

namespace onv::nif {

struct Mesh {
    std::vector<float> vertices;          // x,y,z triples (game units)
    std::vector<std::uint16_t> indices;   // triangle list
};

// Decode the geometry meshes from a NIF file's bytes.
std::vector<Mesh> decode(const std::vector<std::uint8_t>& bytes);

} // namespace onv::nif

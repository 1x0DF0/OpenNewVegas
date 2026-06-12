// Open New Vegas — DDS texture decoder.
// STUB — to be implemented.
#pragma once

#include <cstdint>
#include <vector>

namespace onv::dds {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width*height*4, top-down
};

// Decode a DDS file's bytes to an RGBA image (top mip).
Image decode(const std::vector<std::uint8_t>& bytes);

} // namespace onv::dds

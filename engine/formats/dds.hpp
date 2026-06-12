// Open New Vegas — DDS texture decoder.
//
// Decodes Microsoft DirectDraw Surface (.dds) files to RGBA8. Fallout: New
// Vegas stores its textures as DDS inside the BSA archives, overwhelmingly in
// the block-compressed BC1/BC2/BC3 formats (FourCC "DXT1"/"DXT3"/"DXT5") with
// a handful of uncompressed 32-bit surfaces.
//
// Clean-room: implemented from the publicly-documented DDS container layout
// (Microsoft DDS docs) and the S3TC/BCn block layout (community-documented).

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
// Throws std::runtime_error on unsupported or malformed input.
Image decode(const std::vector<std::uint8_t>& bytes);

} // namespace onv::dds

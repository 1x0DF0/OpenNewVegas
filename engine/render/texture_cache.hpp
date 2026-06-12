// Open New Vegas — texture cache.
//
// Resolves a texture path (from a NIF shape's texture set) to a decoded RGBA
// image, pulling the .dds bytes from the Data virtual filesystem and decoding
// them once. Results — including failures — are cached by path so repeated
// requests are cheap. CPU-side only; the renderer turns the RGBA into a GPU
// texture. Ships no game data.

#pragma once

#include "../assets/data_files.hpp"
#include "../formats/dds.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace onv::render {

class TextureCache {
public:
    explicit TextureCache(const assets::DataFiles& vfs);

    // Decode (once, then cache) the texture at `path` to RGBA. Tries the bare
    // path and a "textures\\" prefix, case-insensitively. Returns nullptr if
    // the texture is unavailable or cannot be decoded.
    const dds::Image* get(const std::string& path);

    std::size_t size() const { return cache_.size(); }

private:
    const assets::DataFiles& vfs_;
    std::unordered_map<std::string, std::optional<dds::Image>> cache_;
};

} // namespace onv::render

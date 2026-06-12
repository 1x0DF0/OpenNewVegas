// Open New Vegas — texture cache.
// STUB — implemented by the textures work stream.

#include "texture_cache.hpp"

namespace onv::render {

TextureCache::TextureCache(const assets::DataFiles& vfs) : vfs_(vfs) {}

const dds::Image* TextureCache::get(const std::string&) { return nullptr; }

} // namespace onv::render

// Open New Vegas — texture cache implementation.
//
// Resolves a NIF texture-set path to a decoded RGBA image and memoizes the
// result by a normalized key. NIF texture paths come in two shapes: some
// already carry the "textures\\" prefix, some are relative to it; both must
// resolve. Failures are cached as std::nullopt so a missing or undecodable
// texture is never re-resolved.

#include "texture_cache.hpp"

#include <cctype>

namespace onv::render {
namespace {

// Normalize a texture path to its cache key: '/' -> '\\', lowercased, no
// leading separator. This mirrors the VFS's own normalization so that
// "Rock.DDS", "rock.dds" and "/rock.dds" share one cache entry.
std::string normalizeKey(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (c == '/') c = '\\';
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    std::size_t start = 0;
    while (start < s.size() && s[start] == '\\') ++start;
    return s.substr(start);
}

// True if `key` (already normalized) begins with the "textures\\" prefix.
bool hasTexturesPrefix(const std::string& key) {
    static const std::string prefix = "textures\\";
    return key.size() >= prefix.size() &&
           key.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

TextureCache::TextureCache(const assets::DataFiles& vfs) : vfs_(vfs) {}

const dds::Image* TextureCache::get(const std::string& path) {
    const std::string key = normalizeKey(path);

    // Memoized result (hit or cached miss/failure) — never re-resolve.
    if (const auto it = cache_.find(key); it != cache_.end())
        return it->second ? &*it->second : nullptr;

    // Try the bare path first; on a miss, fall back to the "textures\\" prefix
    // (NIF texture-set entries are sometimes relative to the textures dir).
    auto bytes = vfs_.resolve(key);
    if (!bytes && !hasTexturesPrefix(key))
        bytes = vfs_.resolve("textures\\" + key);

    std::optional<dds::Image> decoded;
    if (bytes) {
        try {
            decoded = dds::decode(*bytes);
        } catch (...) {
            // Undecodable surface: cache the failure as std::nullopt.
            decoded = std::nullopt;
        }
    }

    // Insert and return a pointer into the cache. std::unordered_map guarantees
    // reference/pointer stability across insertions and rehashes, so the
    // returned pointer stays valid for the cache's lifetime.
    const auto [it, _] = cache_.emplace(key, std::move(decoded));
    return it->second ? &*it->second : nullptr;
}

} // namespace onv::render

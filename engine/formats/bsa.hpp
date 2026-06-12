// Open New Vegas — BSA archive reader (version 104, Fallout 3 / New Vegas).
//
// BSA is the archive format Bethesda games store their assets in (meshes,
// textures, sounds, voice files). The layout is publicly documented by the
// modding community (UESP / BSArch / xEdit). This is a clean-room reader:
// it parses archives the *user* supplies from their own copy of the game.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onv::bsa {

struct FileEntry {
    std::string folder;   // lowercase, backslash-separated (as stored)
    std::string name;     // file name
    std::uint64_t nameHash = 0;
    std::uint32_t size = 0;     // stored size (compression bit stripped)
    std::uint32_t offset = 0;   // absolute offset of the data block
    bool compressed = false;    // zlib-compressed on disk

    std::string path() const { return folder.empty() ? name : folder + "\\" + name; }
};

class Archive {
public:
    // Throws std::runtime_error on malformed input.
    explicit Archive(const std::string& filePath);

    const std::vector<FileEntry>& files() const { return files_; }
    std::uint32_t version() const { return version_; }
    bool defaultCompressed() const { return defaultCompressed_; }

    // Case-insensitive lookup by "folder\\name" path. Returns nullptr if absent.
    const FileEntry* find(const std::string& path) const;

    // Extract one file's bytes (decompressing if needed).
    std::vector<std::uint8_t> extract(const FileEntry& entry) const;

private:
    std::string filePath_;
    std::uint32_t version_ = 0;
    bool defaultCompressed_ = false;
    bool embeddedNames_ = false;
    std::vector<FileEntry> files_;
};

// Bethesda's 64-bit name hash, used for lookups inside BSAs.
std::uint64_t hashPart(const std::string& nameLower, const std::string& extLower);

} // namespace onv::bsa

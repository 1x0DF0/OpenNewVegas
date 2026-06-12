// Open New Vegas — BSA v104 archive reader implementation.

#include "bsa.hpp"
#include "binary_reader.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace onv::bsa {
namespace {

constexpr std::uint32_t FLAG_INCLUDE_DIR_NAMES = 0x1;
constexpr std::uint32_t FLAG_INCLUDE_FILE_NAMES = 0x2;
constexpr std::uint32_t FLAG_DEFAULT_COMPRESSED = 0x4;
constexpr std::uint32_t FLAG_EMBEDDED_NAMES = 0x100;

// File-record size bit 30 toggles compression relative to the archive default
constexpr std::uint32_t SIZE_COMPRESSION_TOGGLE = 0x40000000;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::uint8_t> readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open: " + path);
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> buf(size);
    if (size) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

std::vector<std::uint8_t> zlibInflate(const std::uint8_t* src, std::size_t srcLen,
                                      std::size_t dstLen) {
    std::vector<std::uint8_t> out(dstLen);
    uLongf outLen = dstLen;
    const int rc = uncompress(out.data(), &outLen, src, srcLen);
    if (rc != Z_OK) throw std::runtime_error("zlib inflate failed");
    out.resize(outLen);
    return out;
}

} // namespace

std::uint64_t hashPart(const std::string& nameLower, const std::string& extLower) {
    // Bethesda's BSA name hash (community-documented algorithm)
    std::uint64_t hash = 0;
    if (!nameLower.empty()) {
        const auto& n = nameLower;
        hash = static_cast<std::uint8_t>(n[n.size() - 1]);
        hash |= (n.size() > 2 ? static_cast<std::uint8_t>(n[n.size() - 2]) : 0) << 8;
        hash |= static_cast<std::uint64_t>(n.size()) << 16;
        hash |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(n[0])) << 24;
    }
    if (extLower == ".kf") hash |= 0x80;
    else if (extLower == ".nif") hash |= 0x8000;
    else if (extLower == ".dds") hash |= 0x8080;
    else if (extLower == ".wav") hash |= 0x80000000;

    std::uint32_t hash2 = 0;
    for (std::size_t i = 1; i + 1 < nameLower.size(); ++i)
        hash2 = hash2 * 0x1003f + static_cast<std::uint8_t>(nameLower[i]);
    std::uint32_t hash3 = 0;
    for (const char c : extLower)
        hash3 = hash3 * 0x1003f + static_cast<std::uint8_t>(c);
    return (static_cast<std::uint64_t>(hash2 + hash3) << 32) | hash;
}

Archive::Archive(const std::string& filePath) : filePath_(filePath) {
    const auto buf = readWholeFile(filePath);
    BinaryReader r(buf);

    if (r.readTag() != std::string("BSA\0", 4))
        throw std::runtime_error("not a BSA archive: " + filePath);
    version_ = r.read<std::uint32_t>();
    if (version_ != 104)
        throw std::runtime_error("unsupported BSA version " +
                                 std::to_string(version_) + " (want 104 = FO3/FNV)");

    const auto folderRecordOffset = r.read<std::uint32_t>();
    const auto archiveFlags = r.read<std::uint32_t>();
    const auto folderCount = r.read<std::uint32_t>();
    const auto fileCount = r.read<std::uint32_t>();
    r.read<std::uint32_t>(); // totalFolderNameLength
    const auto totalFileNameLength = r.read<std::uint32_t>();
    r.read<std::uint32_t>(); // fileFlags (content-type hints)

    defaultCompressed_ = archiveFlags & FLAG_DEFAULT_COMPRESSED;
    embeddedNames_ = archiveFlags & FLAG_EMBEDDED_NAMES;
    const bool dirNames = archiveFlags & FLAG_INCLUDE_DIR_NAMES;
    const bool fileNames = archiveFlags & FLAG_INCLUDE_FILE_NAMES;

    // Folder records: hash(8), fileCount(4), offset(4)
    struct FolderRec { std::uint64_t hash; std::uint32_t count, offset; };
    r.seek(folderRecordOffset);
    std::vector<FolderRec> folders(folderCount);
    for (auto& fr : folders) {
        fr.hash = r.read<std::uint64_t>();
        fr.count = r.read<std::uint32_t>();
        fr.offset = r.read<std::uint32_t>(); // includes totalFileNameLength
    }

    // File record blocks: optional folder name, then per-file hash(8), size(4), offset(4)
    files_.reserve(fileCount);
    for (const auto& fr : folders) {
        r.seek(fr.offset - totalFileNameLength);
        std::string folderName;
        if (dirNames) folderName = r.readBzString();
        for (std::uint32_t i = 0; i < fr.count; ++i) {
            FileEntry e;
            e.folder = folderName;
            e.nameHash = r.read<std::uint64_t>();
            auto rawSize = r.read<std::uint32_t>();
            e.offset = r.read<std::uint32_t>();
            const bool toggled = rawSize & SIZE_COMPRESSION_TOGGLE;
            e.size = rawSize & ~SIZE_COMPRESSION_TOGGLE;
            e.compressed = defaultCompressed_ != toggled;
            files_.push_back(std::move(e));
        }
    }

    // File name block: fileCount zstrings in file-record order
    if (fileNames) {
        for (auto& e : files_) e.name = r.readZString();
    }
}

const FileEntry* Archive::find(const std::string& path) const {
    const auto want = toLower(path);
    for (const auto& e : files_) {
        if (toLower(e.path()) == want) return &e;
    }
    return nullptr;
}

std::vector<std::uint8_t> Archive::extract(const FileEntry& entry) const {
    std::ifstream f(filePath_, std::ios::binary);
    if (!f) throw std::runtime_error("cannot reopen: " + filePath_);
    f.seekg(entry.offset);
    std::vector<std::uint8_t> raw(entry.size);
    f.read(reinterpret_cast<char*>(raw.data()), entry.size);
    if (!f) throw std::runtime_error("truncated archive data");

    BinaryReader r(raw);
    if (embeddedNames_) {
        r.readBzString(); // full path stored ahead of the data; skip it
    }
    if (entry.compressed) {
        const auto originalSize = r.read<std::uint32_t>();
        return zlibInflate(raw.data() + r.pos(), raw.size() - r.pos(), originalSize);
    }
    return std::vector<std::uint8_t>(raw.begin() + r.pos(), raw.end());
}

} // namespace onv::bsa

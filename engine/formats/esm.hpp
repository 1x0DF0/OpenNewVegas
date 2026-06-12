// Open New Vegas — ESM/ESP plugin reader (Fallout 3 / New Vegas layout).
//
// An ESM ("Elder Scrolls Master") file is a tree of GRUP containers holding
// records (WRLD = worldspace, CELL = cell, NPC_ = character, QUST = quest,
// DIAL/INFO = dialogue, ...). Each record holds typed subrecords. The layout
// is publicly documented by the modding community (UESP / xEdit). This is a
// clean-room reader for files the user supplies from their own game copy.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace onv::esm {

struct Subrecord {
    std::string type;               // 4-char tag, e.g. "EDID", "FULL"
    std::vector<std::uint8_t> data;

    std::string asString() const;   // zstring payload (trims trailing NUL)
};

struct Record {
    std::string type;               // 4-char tag, e.g. "WRLD"
    std::uint32_t flags = 0;
    std::uint32_t formId = 0;
    std::vector<Subrecord> subrecords;

    bool isCompressed() const { return flags & 0x00040000; }
    // First EDID subrecord payload, or "" if none.
    std::string editorId() const;
    const Subrecord* find(const std::string& type) const;
};

struct GroupHeader {
    std::uint32_t size = 0;         // including the 24-byte header
    char label[4] = {};             // meaning depends on groupType
    std::int32_t groupType = 0;     // 0 = top-level (label is a record type)
};

// Streaming walker: parses the plugin and invokes the callback for every
// record in file order (descending into nested groups). Compressed records
// are inflated transparently. Returns the number of records visited.
//
// `filter`: if non-empty, only records whose type is in the set are fully
// parsed into subrecords (others are still counted but skipped cheaply).
std::size_t walk(const std::string& filePath,
                 const std::function<void(const Record&)>& onRecord,
                 const std::vector<std::string>& filter = {});

// Convenience: count of records by type for a whole plugin.
std::map<std::string, std::size_t> recordCounts(const std::string& filePath);

} // namespace onv::esm

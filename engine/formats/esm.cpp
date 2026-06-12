// Open New Vegas — ESM/ESP plugin reader implementation.

#include "esm.hpp"
#include "binary_reader.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace onv::esm {
namespace {

constexpr std::size_t RECORD_HEADER_SIZE = 24; // FO3/FNV-era header

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
    if (uncompress(out.data(), &outLen, src, srcLen) != Z_OK)
        throw std::runtime_error("zlib inflate failed (compressed record)");
    out.resize(outLen);
    return out;
}

// Parse the subrecord stream inside a record's (decompressed) data block.
void parseSubrecords(const std::uint8_t* data, std::size_t size, Record& rec) {
    BinaryReader r(data, size);
    std::uint32_t overrideSize = 0; // set by an XXXX subrecord for the next one
    while (!r.eof()) {
        Subrecord sub;
        sub.type = r.readTag();
        std::uint32_t len = r.read<std::uint16_t>();
        if (sub.type == "XXXX") {
            // Payload is a uint32 giving the true size of the NEXT subrecord
            overrideSize = r.read<std::uint32_t>();
            continue;
        }
        if (overrideSize) {
            len = overrideSize;
            overrideSize = 0;
        }
        sub.data = r.readVector(len);
        rec.subrecords.push_back(std::move(sub));
    }
}

struct Walker {
    const std::function<void(const Record&)>& onRecord;
    const std::vector<std::string>& filter;
    std::size_t count = 0;

    bool wantType(const std::string& t) const {
        return filter.empty() ||
               std::find(filter.begin(), filter.end(), t) != filter.end();
    }

    // Walk one region of the buffer containing records/groups back to back.
    void walkRange(BinaryReader& r, std::size_t end) {
        while (r.pos() < end) {
            const auto tag = r.readTag();
            if (tag == "GRUP") {
                const auto groupSize = r.read<std::uint32_t>();
                if (groupSize < RECORD_HEADER_SIZE)
                    throw std::runtime_error("malformed GRUP size");
                r.skip(16); // label(4) groupType(4) stamp(2)+pad(2) version(2)+pad(2)
                walkRange(r, r.pos() + groupSize - RECORD_HEADER_SIZE);
            } else {
                const auto dataSize = r.read<std::uint32_t>();
                Record rec;
                rec.type = tag;
                rec.flags = r.read<std::uint32_t>();
                rec.formId = r.read<std::uint32_t>();
                r.skip(4); // version-control info
                r.skip(4); // form version + unknown
                ++count;

                if (wantType(tag)) {
                    const auto body = r.readVector(dataSize);
                    if (rec.isCompressed()) {
                        if (body.size() < 4)
                            throw std::runtime_error("malformed compressed record");
                        BinaryReader br(body);
                        const auto rawSize = br.read<std::uint32_t>();
                        const auto inflated =
                            zlibInflate(body.data() + 4, body.size() - 4, rawSize);
                        parseSubrecords(inflated.data(), inflated.size(), rec);
                    } else {
                        parseSubrecords(body.data(), body.size(), rec);
                    }
                    onRecord(rec);
                } else {
                    r.skip(dataSize);
                    onRecord(rec); // header-only record (no subrecords parsed)
                }
            }
        }
        if (r.pos() != end) throw std::runtime_error("group walk overran boundary");
    }
};

} // namespace

std::string Subrecord::asString() const {
    std::string s(data.begin(), data.end());
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::string Record::editorId() const {
    const auto* sub = find("EDID");
    return sub ? sub->asString() : std::string();
}

const Subrecord* Record::find(const std::string& type) const {
    for (const auto& s : subrecords)
        if (s.type == type) return &s;
    return nullptr;
}

std::size_t walk(const std::string& filePath,
                 const std::function<void(const Record&)>& onRecord,
                 const std::vector<std::string>& filter) {
    const auto buf = readWholeFile(filePath);
    BinaryReader r(buf);

    // A plugin must start with a TES4 header record
    if (buf.size() < RECORD_HEADER_SIZE ||
        std::string(reinterpret_cast<const char*>(buf.data()), 4) != "TES4")
        throw std::runtime_error("not an ESM/ESP plugin (missing TES4 header): " +
                                 filePath);

    Walker w{onRecord, filter};
    w.walkRange(r, buf.size());
    return w.count;
}

std::map<std::string, std::size_t> recordCounts(const std::string& filePath) {
    std::map<std::string, std::size_t> counts;
    walk(filePath, [&](const Record& rec) { ++counts[rec.type]; },
         {"\x01_no_match_"}); // header-only pass: skip subrecord parsing
    return counts;
}

} // namespace onv::esm

// Open New Vegas — format reader tests.
//
// We do not (and will never) ship game data, so these tests build small
// synthetic BSA and ESM fixtures in a temp directory, then verify the
// readers parse them correctly — including zlib compression, the BSA
// embedded-names flag, nested GRUPs, and XXXX large-subrecord handling.

#include "../formats/bsa.hpp"
#include "../formats/esm.hpp"

#include <zlib.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// ── Little-endian byte buffer builder ─────────────────────────────────────
struct Builder {
    std::vector<std::uint8_t> buf;

    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void tag(const char* t) { raw(t, 4); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    }
    void bytes(const std::vector<std::uint8_t>& v) { raw(v.data(), v.size()); }
    void zstring(const std::string& s) { raw(s.c_str(), s.size() + 1); }
    void bzstring(const std::string& s) {
        u8(static_cast<std::uint8_t>(s.size() + 1));
        zstring(s);
    }
    std::size_t size() const { return buf.size(); }

    void writeFile(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
    }
};

std::vector<std::uint8_t> deflateBytes(const std::vector<std::uint8_t>& src) {
    uLongf destLen = compressBound(static_cast<uLong>(src.size()));
    std::vector<std::uint8_t> out(destLen);
    const int rc = compress(out.data(), &destLen, src.data(),
                            static_cast<uLong>(src.size()));
    assert(rc == Z_OK);
    out.resize(destLen);
    return out;
}

// ── BSA fixture ───────────────────────────────────────────────────────────
// One folder ("meshes\\test") with two files: hello.nif (stored) and
// big.nif (zlib-compressed). Archive default = uncompressed, so big.nif
// sets the per-file compression toggle bit.
void testBsa(const std::string& dir) {
    const std::string folderName = "meshes\\test";
    const std::vector<std::uint8_t> helloData = {'h', 'e', 'l', 'l', 'o'};
    std::vector<std::uint8_t> bigData(3000);
    for (std::size_t i = 0; i < bigData.size(); ++i)
        bigData[i] = static_cast<std::uint8_t>(i % 251);
    const auto bigCompressed = deflateBytes(bigData);

    const std::string names = std::string("hello.nif") + '\0' + "big.nif" + '\0';
    const std::uint32_t totalFileNameLength = static_cast<std::uint32_t>(names.size());

    // Layout offsets (header 36, folder record 16)
    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlockStart = folderRecOffset + 16;
    // file record block = bzstring folder name + 2 file records of 16
    const std::uint32_t fileRecBlockSize =
        1 + static_cast<std::uint32_t>(folderName.size()) + 1 + 2 * 16;
    const std::uint32_t nameBlockStart = fileRecBlockStart + fileRecBlockSize;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);                  // version
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2);            // include dir names + file names, NOT compressed
    b.u32(1);                    // folderCount
    b.u32(2);                    // fileCount
    b.u32(static_cast<std::uint32_t>(folderName.size()) + 1);
    b.u32(totalFileNameLength);
    b.u32(0);                    // fileFlags

    // Folder record (offset field includes totalFileNameLength by spec)
    b.u64(onv::bsa::hashPart("test", ""));
    b.u32(2);
    b.u32(fileRecBlockStart + totalFileNameLength);

    // File record block
    b.bzstring(folderName);
    b.u64(onv::bsa::hashPart("hello", ".nif"));
    b.u32(static_cast<std::uint32_t>(helloData.size()));
    b.u32(dataStart);
    b.u64(onv::bsa::hashPart("big", ".nif"));
    // compressed: toggle bit set, stored size = 4 (orig size) + deflate stream
    b.u32(0x40000000u | static_cast<std::uint32_t>(4 + bigCompressed.size()));
    b.u32(dataStart + static_cast<std::uint32_t>(helloData.size()));

    // File name block
    b.raw(names.data(), names.size());

    // Data: hello (stored), then big (uint32 original size + zlib stream)
    CHECK(b.size() == dataStart);
    b.bytes(helloData);
    b.u32(static_cast<std::uint32_t>(bigData.size()));
    b.bytes(bigCompressed);

    const auto path = dir + "/fixture.bsa";
    b.writeFile(path);

    // ── Parse and verify ──
    onv::bsa::Archive a(path);
    CHECK(a.version() == 104);
    CHECK(a.files().size() == 2);
    CHECK(a.files()[0].path() == "meshes\\test\\hello.nif");
    CHECK(!a.files()[0].compressed);
    CHECK(a.files()[1].compressed);

    const auto* hello = a.find("MESHES\\Test\\HELLO.NIF"); // case-insensitive
    CHECK(hello != nullptr);
    if (hello) CHECK(a.extract(*hello) == helloData);

    const auto* big = a.find("meshes\\test\\big.nif");
    CHECK(big != nullptr);
    if (big) CHECK(a.extract(*big) == bigData);

    std::printf("BSA fixture: %zu files parsed, both payloads round-tripped\n",
                a.files().size());
}

// ── ESM fixture ───────────────────────────────────────────────────────────
// TES4 header + one top-level GRUP of WRLD containing:
//   - a plain record with EDID/FULL
//   - a zlib-compressed record
//   - a record using XXXX to declare an oversized subrecord
void writeRecordHeader(Builder& b, const char* type, std::uint32_t dataSize,
                       std::uint32_t flags, std::uint32_t formId) {
    b.tag(type);
    b.u32(dataSize);
    b.u32(flags);
    b.u32(formId);
    b.u32(0); // vc info
    b.u16(15); // form version
    b.u16(0);
}

void sub(Builder& b, const char* type, const std::string& payload) {
    b.tag(type);
    b.u16(static_cast<std::uint16_t>(payload.size() + 1));
    b.zstring(payload);
}

void testEsm(const std::string& dir) {
    // Record 1: plain WRLD
    Builder r1;
    sub(r1, "EDID", "WastelandNV");
    sub(r1, "FULL", "Mojave Wasteland");

    // Record 2: compressed WRLD (EDID only)
    Builder r2raw;
    sub(r2raw, "EDID", "CompressedWorld");
    const auto r2z = deflateBytes(r2raw.buf);

    // Record 3: XXXX oversized subrecord (size > 65535 forces XXXX in real
    // files; we use a small one — the mechanism is what's under test)
    std::vector<std::uint8_t> bigPayload(70000, 0xAB);
    Builder r3;
    r3.tag("XXXX");
    r3.u16(4);
    r3.u32(static_cast<std::uint32_t>(bigPayload.size()));
    r3.tag("DATA");
    r3.u16(0); // real size comes from XXXX
    r3.bytes(bigPayload);
    {
        Builder tail;
        sub(tail, "EDID", "AfterBig");
        r3.bytes(tail.buf);
    }

    Builder b;
    // TES4 header record with empty body
    writeRecordHeader(b, "TES4", 0, 0, 0);

    // Top GRUP of WRLD
    const std::uint32_t r2DataSize = static_cast<std::uint32_t>(4 + r2z.size());
    const std::uint32_t groupSize = 24 +
        24 + static_cast<std::uint32_t>(r1.size()) +
        24 + r2DataSize +
        24 + static_cast<std::uint32_t>(r3.size());
    b.tag("GRUP");
    b.u32(groupSize);
    b.tag("WRLD"); // label
    b.u32(0);      // groupType 0 = top level
    b.u32(0);      // stamp
    b.u32(0);      // version/unknown

    writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(r1.size()), 0, 0x1000);
    b.bytes(r1.buf);
    writeRecordHeader(b, "WRLD", r2DataSize, 0x00040000, 0x1001); // compressed
    b.u32(static_cast<std::uint32_t>(r2raw.size()));
    b.bytes(r2z);
    writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(r3.size()), 0, 0x1002);
    b.bytes(r3.buf);

    const auto path = dir + "/fixture.esm";
    b.writeFile(path);

    // ── Parse and verify ──
    std::vector<onv::esm::Record> wrlds;
    const auto total = onv::esm::walk(path, [&](const onv::esm::Record& rec) {
        if (rec.type == "WRLD") wrlds.push_back(rec);
    }, {"WRLD"});

    CHECK(total == 4); // TES4 + 3 WRLD
    CHECK(wrlds.size() == 3);
    CHECK(wrlds[0].editorId() == "WastelandNV");
    CHECK(wrlds[0].find("FULL")->asString() == "Mojave Wasteland");
    CHECK(wrlds[1].editorId() == "CompressedWorld"); // survived zlib
    const auto* data = wrlds[2].find("DATA");
    CHECK(data && data->data.size() == 70000 && data->data[69999] == 0xAB);
    CHECK(wrlds[2].editorId() == "AfterBig"); // parsing continued past XXXX

    const auto counts = onv::esm::recordCounts(path);
    CHECK(counts.at("WRLD") == 3);
    CHECK(counts.at("TES4") == 1);

    std::printf("ESM fixture: %zu records, compressed + XXXX paths verified\n",
                total);
}

} // namespace

int main() {
    const std::string dir = "/tmp/onv_fixtures";
    std::remove((dir + "/fixture.bsa").c_str());
    std::remove((dir + "/fixture.esm").c_str());
    if (system(("mkdir -p " + dir).c_str()) != 0) {
        std::printf("cannot create fixture dir\n");
        return 1;
    }

    testBsa(dir);
    testEsm(dir);

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all format tests passed\n");
    return 0;
}

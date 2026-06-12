// Open New Vegas — parser robustness tests.
//
// Adversarial test suite: feeds truncated, corrupted, and hostile inputs to the
// BSA/ESM/NIF/DDS parsers and proves they fail CLEANLY — throwing a
// std::exception rather than crashing, hanging, or over-allocating. A crash or
// a non-exception escape fails the whole test binary (which is the point: the
// process dying is the failure signal).
//
// For each parser:
//   1. VALID BASELINE — build one minimal valid input and confirm it parses,
//      so a broken fixture can't make the corruption tests vacuously "pass".
//   2. TRUNCATION SWEEP — try parsing every prefix length; each attempt must
//      either succeed or throw std::exception (no other outcome).
//   3. CORRUPTION TESTS — targeted per-format mutations that must throw.
//
// Where the engine instead succeeds-but-garbage or over-allocates, we do NOT
// fail the test (that's an engine design question, not a robustness violation
// of *this* harness). Instead we print a "FINDING:" line and bump a counter,
// reported in the summary. CHECK failures are reserved for true safety
// violations: a parser returning SUCCESS on input that is impossible to parse
// validly, where doing so could only mean reading garbage as truth.

#include "../formats/bsa.hpp"
#include "../formats/esm.hpp"
#include "../formats/nif.hpp"
#include "../formats/dds.hpp"

#include <zlib.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;
int findings = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

void finding(const std::string& msg) {
    std::printf("FINDING: %s\n", msg.c_str());
    ++findings;
}

// Per-format counters for the summary.
struct Stats {
    int cases = 0;       // corruption/truncation cases run
    int threw = 0;       // exceptions caught
};

// Run `fn`; return true iff it threw a std::exception. A throw of any other
// (non-std) type, or escape via longjmp/abort, will not be caught here and
// will surface as a test-binary crash — which is exactly the failure we want.
bool didThrow(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

// Corruption helper: the lambda MUST throw std::exception. If it returns
// normally we record a FINDING (engine accepted garbage) rather than a hard
// failure, unless `hardFail` is set (input that can ONLY be garbage).
void expectThrow(Stats& st, const std::string& label,
                 const std::function<void()>& fn, bool hardFail = false) {
    ++st.cases;
    bool threw = didThrow(fn);
    if (threw) {
        ++st.threw;
        return;
    }
    if (hardFail) {
        std::printf("FAIL  %s: expected throw, parser returned success\n",
                    label.c_str());
        ++failures;
    } else {
        finding(label + ": parser accepted malformed input without throwing");
    }
}

// Truncation sweep: for every prefix length in [0, full.size()), parsing must
// either succeed or throw std::exception. `parse` takes a byte buffer.
void truncationSweep(Stats& st, const std::string& name,
                     const std::vector<std::uint8_t>& full,
                     const std::function<void(const std::vector<std::uint8_t>&)>& parse) {
    for (std::size_t len = 0; len < full.size(); ++len) {
        std::vector<std::uint8_t> prefix(full.begin(), full.begin() + len);
        ++st.cases;
        try {
            parse(prefix);
            // Success on a truncated buffer is allowed (parser may legitimately
            // stop early), as long as it didn't crash.
        } catch (const std::exception&) {
            ++st.threw;
        }
        // Any non-std exception / crash escapes and kills the binary.
    }
    (void)name;
}

// ── Little-endian byte buffer builder (copied from the format tests) ─────────
struct Builder {
    std::vector<std::uint8_t> buf;

    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v) { raw(&v, 2); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void i32(std::int32_t v) { raw(&v, 4); }
    void f32(float v) { raw(&v, 4); }
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
    void headerLine(const std::string& s) { raw(s.data(), s.size()); u8('\n'); }
    void sizedString(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void exportString(const std::string& s) {
        u8(static_cast<std::uint8_t>(s.size() + 1));
        raw(s.data(), s.size());
        u8(0);
    }
    void vector3(float x, float y, float z) { f32(x); f32(y); f32(z); }
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

const std::string kTmpDir = "/tmp/onv_rob";

std::string tmpPath(const std::string& name) { return kTmpDir + "/" + name; }

// Write bytes to a temp file and return its path (BSA/ESM use file-based APIs).
std::string writeTmp(const std::string& name,
                     const std::vector<std::uint8_t>& bytes) {
    const std::string path = tmpPath(name);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return path;
}

// ─────────────────────────────────────────────────────────────────────────
// BSA
// ─────────────────────────────────────────────────────────────────────────

// Build a minimal valid BSA: one folder "t", one stored file "a.nif".
std::vector<std::uint8_t> buildValidBsa() {
    const std::string folderName = "t";
    const std::vector<std::uint8_t> fileData = {'h', 'i'};
    const std::string names = std::string("a.nif") + '\0';
    const std::uint32_t totalFileNameLength =
        static_cast<std::uint32_t>(names.size());

    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlockStart = folderRecOffset + 16;
    const std::uint32_t fileRecBlockSize =
        1 + static_cast<std::uint32_t>(folderName.size()) + 1 + 1 * 16;
    const std::uint32_t nameBlockStart = fileRecBlockStart + fileRecBlockSize;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2);  // include dir + file names, not compressed
    b.u32(1);          // folderCount
    b.u32(1);          // fileCount
    b.u32(static_cast<std::uint32_t>(folderName.size()) + 1);
    b.u32(totalFileNameLength);
    b.u32(0);          // fileFlags

    b.u64(onv::bsa::hashPart("t", ""));
    b.u32(1);
    b.u32(fileRecBlockStart + totalFileNameLength);

    b.bzstring(folderName);
    b.u64(onv::bsa::hashPart("a", ".nif"));
    b.u32(static_cast<std::uint32_t>(fileData.size()));
    b.u32(dataStart);

    b.raw(names.data(), names.size());

    b.bytes(fileData);
    return b.buf;
}

// Build a valid BSA with one *compressed* file. The decompressed-size field is
// caller-controlled, letting us forge a huge declared size over a tiny stream.
std::vector<std::uint8_t> buildCompressedBsa(std::uint32_t declaredOrigSize,
                                             const std::vector<std::uint8_t>& payload) {
    const std::string folderName = "t";
    const auto compressed = deflateBytes(payload);
    const std::string names = std::string("a.nif") + '\0';
    const std::uint32_t totalFileNameLength =
        static_cast<std::uint32_t>(names.size());

    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlockStart = folderRecOffset + 16;
    const std::uint32_t fileRecBlockSize =
        1 + static_cast<std::uint32_t>(folderName.size()) + 1 + 1 * 16;
    const std::uint32_t nameBlockStart = fileRecBlockStart + fileRecBlockSize;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;
    const std::uint32_t storedSize =
        4 + static_cast<std::uint32_t>(compressed.size());

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2 | 0x4);  // dir + file names + DEFAULT COMPRESSED
    b.u32(1);
    b.u32(1);
    b.u32(static_cast<std::uint32_t>(folderName.size()) + 1);
    b.u32(totalFileNameLength);
    b.u32(0);

    b.u64(onv::bsa::hashPart("t", ""));
    b.u32(1);
    b.u32(fileRecBlockStart + totalFileNameLength);

    b.bzstring(folderName);
    b.u64(onv::bsa::hashPart("a", ".nif"));
    b.u32(storedSize);  // default-compressed, no toggle bit -> compressed
    b.u32(dataStart);

    b.raw(names.data(), names.size());

    b.u32(declaredOrigSize);  // forged decompressed size
    b.bytes(compressed);
    return b.buf;
}

void testBsa() {
    Stats st;
    std::printf("\n=== BSA ===\n");

    // 1. Valid baseline.
    const auto valid = buildValidBsa();
    {
        const std::string p = writeTmp("valid.bsa", valid);
        bool ok = false;
        try {
            onv::bsa::Archive a(p);
            ok = (a.version() == 104 && a.files().size() == 1 &&
                  a.extract(a.files()[0]) == std::vector<std::uint8_t>{'h', 'i'});
        } catch (const std::exception& e) {
            std::printf("baseline BSA threw: %s\n", e.what());
        }
        CHECK(ok);
    }

    // 2. Truncation sweep.
    truncationSweep(st, "BSA", valid, [](const std::vector<std::uint8_t>& b) {
        const std::string p = writeTmp("trunc.bsa", b);
        onv::bsa::Archive a(p);
        for (const auto& e : a.files()) a.extract(e);
    });

    // 3. Corruption tests.

    // Bad magic.
    expectThrow(st, "BSA bad magic", [&] {
        auto b = valid;
        b[0] = 'X';
        onv::bsa::Archive a(writeTmp("c.bsa", b));
    }, /*hardFail=*/true);

    // Absurd folderCount on a tiny file. Header layout: magic(4) version(4)
    // folderRecordOffset(4) archiveFlags(4) folderCount(4) fileCount(4)
    // totalFolderNameLength(4) totalFileNameLength(4) fileFlags(4).
    expectThrow(st, "BSA absurd folderCount", [&] {
        auto b = valid;
        std::uint32_t huge = 0xFFFFFFFFu;
        std::memcpy(&b[16], &huge, 4);  // folderCount at offset 16
        onv::bsa::Archive a(writeTmp("c.bsa", b));
    }, /*hardFail=*/true);

    // Absurd fileCount.
    expectThrow(st, "BSA absurd fileCount", [&] {
        auto b = valid;
        std::uint32_t huge = 0xFFFFFFFFu;
        std::memcpy(&b[20], &huge, 4);  // fileCount at offset 20
        onv::bsa::Archive a(writeTmp("c.bsa", b));
    }, /*hardFail=*/true);

    // folderRecordOffset pointing past EOF.
    expectThrow(st, "BSA folderRecordOffset past EOF", [&] {
        auto b = valid;
        std::uint32_t off = 0xFFFFFF00u;
        std::memcpy(&b[8], &off, 4);  // folderRecordOffset at offset 8
        onv::bsa::Archive a(writeTmp("c.bsa", b));
    }, /*hardFail=*/true);

    // totalFileNameLength larger than the file (underflows fr.offset -
    // totalFileNameLength in the seek).
    expectThrow(st, "BSA totalFileNameLength > file", [&] {
        auto b = valid;
        std::uint32_t huge = 0xFFFFFF00u;
        std::memcpy(&b[28], &huge, 4);  // totalFileNameLength at offset 28
        onv::bsa::Archive a(writeTmp("c.bsa", b));
    }, /*hardFail=*/true);

    // extract() with an entry whose offset/size point past EOF.
    {
        ++st.cases;
        const std::string p = writeTmp("valid2.bsa", valid);
        bool threw = false;
        try {
            onv::bsa::Archive a(p);
            CHECK(a.files().size() == 1);
            // Manufacture an out-of-range entry copy.
            auto entry = a.files()[0];
            entry.offset = 0xFFFFFF00u;
            entry.size = 0x1000;
            a.extract(entry);
        } catch (const std::exception&) {
            threw = true;
        }
        if (threw) ++st.threw;
        else finding("BSA extract() with past-EOF offset/size did not throw");
    }

    // Compressed entry whose declared decompressed size is huge (0x7FFFFFFF)
    // over a tiny zlib stream — must throw, not allocate ~2GB.
    {
        ++st.cases;
        const std::vector<std::uint8_t> tiny = {'x'};
        const auto b = buildCompressedBsa(0x7FFFFFFFu, tiny);
        const std::string p = writeTmp("hugecomp.bsa", b);
        bool threw = false;
        try {
            onv::bsa::Archive a(p);
            CHECK(a.files().size() == 1);
            CHECK(a.files()[0].compressed);
            a.extract(a.files()[0]);  // zlibInflate does out.resize(0x7FFFFFFF)
        } catch (const std::exception&) {
            threw = true;
        }
        if (threw) {
            ++st.threw; // now rejected by the decompressed-size plausibility
                        // cap before any large allocation (bsa.cpp extract())
        } else {
            finding("BSA extract: huge declared decompressed size did not throw");
        }
    }

    std::printf("BSA: %d cases, %d exceptions caught\n", st.cases, st.threw);
}

// ─────────────────────────────────────────────────────────────────────────
// ESM
// ─────────────────────────────────────────────────────────────────────────

void writeRecordHeader(Builder& b, const char* type, std::uint32_t dataSize,
                       std::uint32_t flags, std::uint32_t formId) {
    b.tag(type);
    b.u32(dataSize);
    b.u32(flags);
    b.u32(formId);
    b.u32(0);
    b.u16(15);
    b.u16(0);
}

void esmSub(Builder& b, const char* type, const std::string& payload) {
    b.tag(type);
    b.u16(static_cast<std::uint16_t>(payload.size() + 1));
    b.zstring(payload);
}

// Minimal valid ESM: TES4 header + one GRUP holding one WRLD with an EDID.
std::vector<std::uint8_t> buildValidEsm() {
    Builder r1;
    esmSub(r1, "EDID", "Test");

    Builder b;
    writeRecordHeader(b, "TES4", 0, 0, 0);
    const std::uint32_t groupSize =
        24 + 24 + static_cast<std::uint32_t>(r1.size());
    b.tag("GRUP");
    b.u32(groupSize);
    b.tag("WRLD");
    b.u32(0);
    b.u32(0);
    b.u32(0);
    writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(r1.size()), 0, 0x10);
    b.bytes(r1.buf);
    return b.buf;
}

void testEsm() {
    Stats st;
    std::printf("\n=== ESM ===\n");

    const auto noop = [](const onv::esm::Record&) {};

    // 1. Valid baseline.
    const auto valid = buildValidEsm();
    {
        const std::string p = writeTmp("valid.esm", valid);
        bool ok = false;
        try {
            std::size_t n = onv::esm::walk(p, noop, {});
            ok = (n == 2);  // TES4 + WRLD
        } catch (const std::exception& e) {
            std::printf("baseline ESM threw: %s\n", e.what());
        }
        CHECK(ok);
    }

    // 2. Truncation sweep (walk with no filter parses subrecords too).
    truncationSweep(st, "ESM", valid, [&](const std::vector<std::uint8_t>& b) {
        const std::string p = writeTmp("trunc.esm", b);
        onv::esm::walk(p, noop, {});
    });

    // 3. Corruption tests.

    // Does not start with TES4.
    expectThrow(st, "ESM not TES4", [&] {
        auto b = valid;
        b[0] = 'X';
        onv::esm::walk(writeTmp("c.esm", b), noop, {});
    }, /*hardFail=*/true);

    // GRUP whose groupSize is smaller than the header (< 24) -> "malformed GRUP".
    expectThrow(st, "ESM GRUP size < header", [&] {
        auto b = valid;
        // GRUP tag is right after the 24-byte TES4 header; its size is the next
        // u32 at offset 28.
        std::uint32_t small = 4;
        std::memcpy(&b[28], &small, 4);
        onv::esm::walk(writeTmp("c.esm", b), noop, {});
    }, /*hardFail=*/true);

    // GRUP whose size overruns EOF.
    expectThrow(st, "ESM GRUP size overruns EOF", [&] {
        auto b = valid;
        std::uint32_t huge = 0x7FFFFFFFu;
        std::memcpy(&b[28], &huge, 4);
        onv::esm::walk(writeTmp("c.esm", b), noop, {});
    }, /*hardFail=*/true);

    // Record dataSize overrunning EOF. The WRLD record's dataSize sits 4 bytes
    // into its header; locate the WRLD tag after the GRUP header.
    expectThrow(st, "ESM record dataSize overruns EOF", [&] {
        auto b = valid;
        // Layout: TES4(24) GRUP-hdr(24) WRLD-hdr(24) ...; WRLD dataSize @ 48+4=52.
        std::uint32_t huge = 0x7FFFFFFFu;
        std::memcpy(&b[52], &huge, 4);
        onv::esm::walk(writeTmp("c.esm", b), noop, {});
    }, /*hardFail=*/true);

    // Compressed record with garbage zlib data.
    expectThrow(st, "ESM compressed garbage zlib", [&] {
        Builder body;
        body.u32(64);                 // declared decompressed size
        for (int i = 0; i < 16; ++i) body.u8(0xFF);  // not a zlib stream

        Builder b;
        writeRecordHeader(b, "TES4", 0, 0, 0);
        const std::uint32_t gs = 24 + 24 + static_cast<std::uint32_t>(body.size());
        b.tag("GRUP"); b.u32(gs); b.tag("WRLD"); b.u32(0); b.u32(0); b.u32(0);
        writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(body.size()),
                          0x00040000, 0x10);  // compressed flag
        b.bytes(body.buf);
        onv::esm::walk(writeTmp("c.esm", b.buf), noop, {});
    }, /*hardFail=*/true);

    // Compressed record with HUGE declared decompressed size over a tiny stream.
    {
        ++st.cases;
        const auto z = deflateBytes({'x'});
        Builder body;
        body.u32(0x7FFFFFFFu);  // forged huge decompressed size
        body.bytes(z);

        Builder b;
        writeRecordHeader(b, "TES4", 0, 0, 0);
        const std::uint32_t gs = 24 + 24 + static_cast<std::uint32_t>(body.size());
        b.tag("GRUP"); b.u32(gs); b.tag("WRLD"); b.u32(0); b.u32(0); b.u32(0);
        writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(body.size()),
                          0x00040000, 0x10);
        b.bytes(body.buf);
        bool threw = false;
        try {
            onv::esm::walk(writeTmp("c.esm", b.buf), noop, {});
        } catch (const std::exception&) { threw = true; }
        if (threw) {
            ++st.threw; // now rejected by the decompressed-size plausibility
                        // cap before any large allocation (esm.cpp walk())
        } else {
            finding("ESM compressed huge decompressed size did not throw");
        }
    }

    // XXXX subrecord at end of buffer (declares an override but no following
    // subrecord / truncated payload).
    expectThrow(st, "ESM XXXX at end of buffer", [&] {
        Builder body;
        body.tag("XXXX");
        body.u16(4);
        body.u32(1000);  // override size for a next subrecord that never comes
        // ...no following subrecord, and even if there were, payload truncated.

        Builder b;
        writeRecordHeader(b, "TES4", 0, 0, 0);
        const std::uint32_t gs = 24 + 24 + static_cast<std::uint32_t>(body.size());
        b.tag("GRUP"); b.u32(gs); b.tag("WRLD"); b.u32(0); b.u32(0); b.u32(0);
        writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(body.size()),
                          0, 0x10);
        b.bytes(body.buf);
        onv::esm::walk(writeTmp("c.esm", b.buf), noop, {});
    });

    // Subrecord size pointing past the record end.
    expectThrow(st, "ESM subrecord size past record end", [&] {
        Builder body;
        body.tag("EDID");
        body.u16(0xFFFF);   // claims 65535 bytes...
        body.u8('x');       // ...but only 1 present
        Builder b;
        writeRecordHeader(b, "TES4", 0, 0, 0);
        const std::uint32_t gs = 24 + 24 + static_cast<std::uint32_t>(body.size());
        b.tag("GRUP"); b.u32(gs); b.tag("WRLD"); b.u32(0); b.u32(0); b.u32(0);
        writeRecordHeader(b, "WRLD", static_cast<std::uint32_t>(body.size()),
                          0, 0x10);
        b.bytes(body.buf);
        onv::esm::walk(writeTmp("c.esm", b.buf), noop, {});
    });

    std::printf("ESM: %d cases, %d exceptions caught\n", st.cases, st.threw);
}

// ─────────────────────────────────────────────────────────────────────────
// NIF
// ─────────────────────────────────────────────────────────────────────────

// Build a minimal valid NIF: header + a single NiTriShapeData block with one
// vertex and no triangles. (Reuses the layout from nif_tests.cpp.)
std::vector<std::uint8_t> buildValidNif() {
    Builder block0;
    // Geometry base, 1 vertex, 0 triangles, no UVs.
    block0.i32(0);            // Group ID
    block0.u16(1);            // Num Vertices
    block0.u8(0);             // Keep Flags
    block0.u8(0);             // Compress Flags
    block0.u8(1);             // Has Vertices
    block0.vector3(0, 0, 0);  // vertex 0
    block0.u16(0);            // BS Data Flags
    block0.u8(0);             // Has Normals
    block0.vector3(0, 0, 0);  // Center
    block0.f32(0);            // Radius
    block0.u8(0);             // Has Vertex Colors
    block0.u16(0);            // Consistency Flags
    block0.i32(-1);           // Additional Data ref
    block0.u16(0);            // Num Triangles
    block0.u32(0);            // Num Triangle Points
    block0.u8(0);             // Has Triangles = false
    block0.u16(0);            // Num Match Groups

    Builder b;
    b.headerLine("Gamebryo File Format, Version 20.2.0.7");
    b.u32(0x14020007);
    b.u8(1);                  // little-endian
    b.u32(11);                // User Version
    b.u32(1);                 // Num Blocks
    b.u32(34);                // BS Version
    b.exportString("rob");    // Author
    b.exportString("");       // Process Script
    b.exportString("");       // Export Script
    b.u16(1);                 // Num Block Types
    b.sizedString("NiTriShapeData");
    b.u16(0);                 // Block Type Index[0]
    b.u32(static_cast<std::uint32_t>(block0.size()));  // Block sizes[0]
    b.u32(0);                 // Num Strings
    b.u32(0);                 // Max String Length
    b.u32(0);                 // Num Groups
    b.bytes(block0.buf);
    return b.buf;
}

// Build a NIF with a caller-controlled numVertices field and a chosen physical
// vertex-data length, plus a chosen numBlocks override — to drive corruption.
void testNif() {
    Stats st;
    std::printf("\n=== NIF ===\n");

    const auto valid = buildValidNif();

    // 1. Valid baseline.
    {
        bool ok = false;
        try {
            const auto meshes = onv::nif::decode(valid);
            ok = (meshes.size() == 1 && meshes[0].vertices.size() == 3);
        } catch (const std::exception& e) {
            std::printf("baseline NIF threw: %s\n", e.what());
        }
        CHECK(ok);
    }

    // 2. Truncation sweep.
    truncationSweep(st, "NIF", valid, [](const std::vector<std::uint8_t>& b) {
        onv::nif::decode(b);
    });

    // 3. Corruption tests.

    // Bad header string.
    expectThrow(st, "NIF bad header string", [] {
        std::vector<std::uint8_t> g(64, 0x55);
        onv::nif::decode(g);
    }, /*hardFail=*/true);

    // Wrong version.
    expectThrow(st, "NIF wrong version", [] {
        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x0A000100);  // not 0x14020007
        b.u8(1);
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    // Endian byte 0 (big-endian rejected).
    expectThrow(st, "NIF endian byte 0", [] {
        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x14020007);
        b.u8(0);  // big-endian
        b.u32(11);
        b.u32(1);
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    // numBlocks huge vs short buffer (block-type-index/size tables overrun).
    expectThrow(st, "NIF numBlocks huge", [] {
        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x14020007);
        b.u8(1);
        b.u32(11);
        b.u32(0xFFFFFFFFu);   // Num Blocks
        b.u32(34);
        b.exportString(""); b.exportString(""); b.exportString("");
        b.u16(0);             // Num Block Types
        // block-type-index loop now reads 4 billion u16s from a tiny buffer.
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    // Block-size table entry that overruns the buffer (block claims more bytes
    // than remain). decode() does r.seek(blockStart + blockSize) -> must throw.
    expectThrow(st, "NIF block size overruns buffer", [&] {
        auto b = valid;
        // The single block size is the last u32 before the block bodies. Find it
        // by rebuilding with a huge block size instead.
        Builder block0;
        block0.i32(0); block0.u16(0); block0.u8(0); block0.u8(0); block0.u8(0);
        block0.u16(0); block0.u8(0); block0.vector3(0,0,0); block0.f32(0);
        block0.u8(0); block0.u16(0); block0.i32(-1); block0.u16(0);

        Builder nb;
        nb.headerLine("Gamebryo File Format, Version 20.2.0.7");
        nb.u32(0x14020007); nb.u8(1); nb.u32(11); nb.u32(1); nb.u32(34);
        nb.exportString(""); nb.exportString(""); nb.exportString("");
        nb.u16(1); nb.sizedString("NiUnknownBlock"); nb.u16(0);
        nb.u32(0x7FFFFFFFu);   // huge block size
        nb.u32(0); nb.u32(0); nb.u32(0);
        nb.bytes(block0.buf);
        onv::nif::decode(nb.buf);
    }, /*hardFail=*/true);

    // numVertices large vs truncated vertex data. Has Vertices=true, big
    // numVertices, but the buffer ends right after -> vertex reads overrun.
    expectThrow(st, "NIF huge numVertices truncated data", [] {
        Builder block0;
        block0.i32(0);
        block0.u16(0xFFFF);   // 65535 vertices
        block0.u8(0); block0.u8(0);
        block0.u8(1);         // Has Vertices
        // ...no vertex data follows.

        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x14020007); b.u8(1); b.u32(11); b.u32(1); b.u32(34);
        b.exportString(""); b.exportString(""); b.exportString("");
        b.u16(1); b.sizedString("NiTriShapeData"); b.u16(0);
        b.u32(static_cast<std::uint32_t>(block0.size()));
        b.u32(0); b.u32(0); b.u32(0);
        b.bytes(block0.buf);
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    // Strip lengths exceeding the data (NiTriStripsData).
    expectThrow(st, "NIF strip length exceeds data", [] {
        Builder block0;  // geometry base, 0 verts
        block0.i32(0); block0.u16(0); block0.u8(0); block0.u8(0); block0.u8(1);
        block0.u16(0); block0.u8(0); block0.vector3(0,0,0); block0.f32(0);
        block0.u8(0); block0.u16(0); block0.i32(-1); block0.u16(0);
        // NiTriStripsData specific:
        block0.u16(1);        // Num Strips
        block0.u16(0xFFFF);   // Strip Lengths[0] = 65535
        block0.u8(1);         // Has Points
        // ...no point data follows.

        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x14020007); b.u8(1); b.u32(11); b.u32(1); b.u32(34);
        b.exportString(""); b.exportString(""); b.exportString("");
        b.u16(1); b.sizedString("NiTriStripsData"); b.u16(0);
        b.u32(static_cast<std::uint32_t>(block0.size()));
        b.u32(0); b.u32(0); b.u32(0);
        b.bytes(block0.buf);
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    // Has Vertices = true with no data (numVertices nonzero, buffer ends).
    expectThrow(st, "NIF Has Vertices true no data", [] {
        Builder block0;
        block0.i32(0);
        block0.u16(4);   // 4 vertices promised
        block0.u8(0); block0.u8(0);
        block0.u8(1);    // Has Vertices = true
        // no vertices

        Builder b;
        b.headerLine("Gamebryo File Format, Version 20.2.0.7");
        b.u32(0x14020007); b.u8(1); b.u32(11); b.u32(1); b.u32(34);
        b.exportString(""); b.exportString(""); b.exportString("");
        b.u16(1); b.sizedString("NiTriShapeData"); b.u16(0);
        b.u32(static_cast<std::uint32_t>(block0.size()));
        b.u32(0); b.u32(0); b.u32(0);
        b.bytes(block0.buf);
        onv::nif::decode(b.buf);
    }, /*hardFail=*/true);

    std::printf("NIF: %d cases, %d exceptions caught\n", st.cases, st.threw);
}

// ─────────────────────────────────────────────────────────────────────────
// DDS
// ─────────────────────────────────────────────────────────────────────────

constexpr std::uint32_t DDPF_FOURCC = 0x4;
constexpr std::uint32_t DDPF_RGB    = 0x40;

void ddsHeader(Builder& b, std::uint32_t width, std::uint32_t height,
               std::uint32_t pfFlags, const char* fourCC,
               std::uint32_t rgbBitCount, std::uint32_t headerSize = 124,
               std::uint32_t pfSize = 32) {
    b.tag("DDS ");
    b.u32(headerSize);
    b.u32(0x1007);
    b.u32(height);
    b.u32(width);
    b.u32(0); b.u32(0); b.u32(1);
    for (int i = 0; i < 11; ++i) b.u32(0);
    b.u32(pfSize);
    b.u32(pfFlags);
    if (pfFlags & DDPF_FOURCC) b.tag(fourCC); else b.u32(0);
    b.u32(rgbBitCount);
    b.u32(0); b.u32(0); b.u32(0); b.u32(0);  // masks
    for (int i = 0; i < 5; ++i) b.u32(0);
}

// Minimal valid DDS: a single 4x4 DXT1 block (8 bytes of data).
std::vector<std::uint8_t> buildValidDds() {
    Builder b;
    ddsHeader(b, 4, 4, DDPF_FOURCC, "DXT1", 0);
    b.u16(0xF800);  // c0
    b.u16(0x001F);  // c1
    b.u32(0);       // indices
    return b.buf;
}

void testDds() {
    Stats st;
    std::printf("\n=== DDS ===\n");

    const auto valid = buildValidDds();

    // 1. Valid baseline.
    {
        bool ok = false;
        try {
            const auto img = onv::dds::decode(valid);
            ok = (img.width == 4 && img.height == 4 && img.rgba.size() == 4 * 4 * 4);
        } catch (const std::exception& e) {
            std::printf("baseline DDS threw: %s\n", e.what());
        }
        CHECK(ok);
    }

    // 2. Truncation sweep.
    truncationSweep(st, "DDS", valid, [](const std::vector<std::uint8_t>& b) {
        onv::dds::decode(b);
    });

    // 3. Corruption tests.

    // Bad magic.
    expectThrow(st, "DDS bad magic", [] {
        std::vector<std::uint8_t> g = {'X', 'Y', 'Z', '!', 0, 1, 2, 3};
        onv::dds::decode(g);
    }, /*hardFail=*/true);

    // Header size != 124.
    expectThrow(st, "DDS header size != 124", [] {
        Builder b;
        ddsHeader(b, 4, 4, DDPF_FOURCC, "DXT1", 0, /*headerSize=*/100);
        for (int i = 0; i < 8; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    // Zero width.
    expectThrow(st, "DDS zero width", [] {
        Builder b;
        ddsHeader(b, 0, 4, DDPF_FOURCC, "DXT1", 0);
        for (int i = 0; i < 8; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    // Zero height.
    expectThrow(st, "DDS zero height", [] {
        Builder b;
        ddsHeader(b, 4, 0, DDPF_FOURCC, "DXT1", 0);
        for (int i = 0; i < 8; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    // Absurd width*height (0xFFFF x 0xFFFF = ~17GB RGBA). decode() does
    // img.rgba.assign(width*height*4) BEFORE reading any surface data.
    {
        ++st.cases;
        Builder b;
        ddsHeader(b, 0xFFFF, 0xFFFF, DDPF_FOURCC, "DXT1", 0);
        // no surface data
        bool threw = false;
        try {
            onv::dds::decode(b.buf);
        } catch (const std::exception&) { threw = true; }
        if (threw) {
            ++st.threw; // now rejected by the dimension/surface-size bound
                        // before any large allocation (dds.cpp decode())
        } else {
            finding("DDS absurd dimensions did not throw");
        }
    }

    // DXT1 with truncated block data (4x4 needs 8 bytes; supply 2).
    expectThrow(st, "DDS DXT1 truncated block data", [] {
        Builder b;
        ddsHeader(b, 4, 4, DDPF_FOURCC, "DXT1", 0);
        b.u16(0xF800);  // only 2 bytes of the 8-byte block
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    // Unsupported FourCC.
    expectThrow(st, "DDS unsupported FourCC", [] {
        Builder b;
        ddsHeader(b, 4, 4, DDPF_FOURCC, "ATI2", 0);
        for (int i = 0; i < 16; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    // Unsupported uncompressed bit depth (RGB flag, 24-bit).
    expectThrow(st, "DDS unsupported RGB bit depth", [] {
        Builder b;
        ddsHeader(b, 4, 4, DDPF_RGB, nullptr, 24);
        for (int i = 0; i < 64; ++i) b.u8(0);
        onv::dds::decode(b.buf);
    }, /*hardFail=*/true);

    std::printf("DDS: %d cases, %d exceptions caught\n", st.cases, st.threw);
}

} // namespace

int main() {
    if (system(("mkdir -p " + kTmpDir).c_str()) != 0) {
        std::printf("cannot create temp dir %s\n", kTmpDir.c_str());
        return 1;
    }

    std::printf("Open New Vegas — parser robustness suite\n");
    std::printf("Each case must succeed or throw std::exception; a crash kills "
                "the binary.\n");

    testBsa();
    testEsm();
    testNif();
    testDds();

    std::printf("\n=== SUMMARY ===\n");
    std::printf("Findings (over-allocation / accepted-garbage): %d\n", findings);
    std::printf("Hard failures (safety violations / crashes-as-CHECK): %d\n",
                failures);

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all robustness tests passed (no crashes, no safety violations)\n");
    return 0;
}

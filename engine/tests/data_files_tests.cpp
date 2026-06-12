// Open New Vegas — Data virtual-filesystem tests.
//
// Builds a synthetic BSA (one file) on disk plus a loose file, then checks the
// DataFiles VFS resolves archived content, is case/separator insensitive, and
// gives loose files precedence over archived ones — all without game data.

#include "../assets/data_files.hpp"
#include "../formats/bsa.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

struct Builder {
    std::vector<std::uint8_t> buf;
    void u8(std::uint8_t v) { buf.push_back(v); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
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
};

// Write a minimal BSA v104 with one folder and one stored (uncompressed) file.
void writeBsa(const std::string& path, const std::string& folderName,
              const std::string& fileName,
              const std::vector<std::uint8_t>& fileData) {
    const std::string names = fileName + std::string(1, '\0');
    const std::uint32_t totalFileNameLength =
        static_cast<std::uint32_t>(names.size());

    const std::uint32_t folderRecOffset = 36;
    const std::uint32_t fileRecBlockStart = folderRecOffset + 16;
    const std::uint32_t fileRecBlockSize =
        1 + static_cast<std::uint32_t>(folderName.size()) + 1 + 16;
    const std::uint32_t nameBlockStart = fileRecBlockStart + fileRecBlockSize;
    const std::uint32_t dataStart = nameBlockStart + totalFileNameLength;

    Builder b;
    b.raw("BSA\0", 4);
    b.u32(104);
    b.u32(folderRecOffset);
    b.u32(0x1 | 0x2);              // dir names + file names, uncompressed
    b.u32(1);                      // folderCount
    b.u32(1);                      // fileCount
    b.u32(static_cast<std::uint32_t>(folderName.size()) + 1);
    b.u32(totalFileNameLength);
    b.u32(0);                      // fileFlags

    // Folder record
    b.u64(onv::bsa::hashPart("test", "")); // hash value is not used for lookup
    b.u32(1);                      // file count
    b.u32(fileRecBlockStart + totalFileNameLength);

    // File record block
    b.bzstring(folderName);
    b.u64(onv::bsa::hashPart("rock", ".nif"));
    b.u32(static_cast<std::uint32_t>(fileData.size()));
    b.u32(dataStart);

    // Name block + data
    b.raw(names.data(), names.size());
    b.bytes(fileData);

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.buf.data()),
            static_cast<std::streamsize>(b.size()));
}

void writeLoose(const fs::path& p, const std::vector<std::uint8_t>& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

} // namespace

int main() {
    const fs::path data = "/tmp/onv_fixtures/Data";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);

    const std::vector<std::uint8_t> archived = {'A', 'R', 'C', 'H', 1, 2, 3};
    writeBsa((data / "Test.bsa").string(), "meshes\\test", "rock.nif", archived);

    // A loose texture that exists ONLY loose.
    const std::vector<std::uint8_t> looseOnly = {'L', 'O', 'O', 'S', 'E'};
    writeLoose(data / "textures" / "stone.dds", looseOnly);

    {
        onv::assets::DataFiles vfs(data.string());
        CHECK(vfs.archiveCount() == 1);
        CHECK(vfs.archivedFileCount() == 1);

        // Archived file resolves, with case/separator insensitivity.
        auto a = vfs.resolve("meshes\\test\\rock.nif");
        CHECK(a.has_value() && *a == archived);
        auto a2 = vfs.resolve("MESHES/Test/Rock.NIF");
        CHECK(a2.has_value() && *a2 == archived);

        // Loose-only file resolves.
        auto l = vfs.resolve("textures\\stone.dds");
        CHECK(l.has_value() && *l == looseOnly);

        // Missing file.
        CHECK(!vfs.resolve("meshes\\missing.nif").has_value());
    }

    // Loose precedence: a loose file shadowing the archived path wins.
    {
        const std::vector<std::uint8_t> override = {'N', 'E', 'W'};
        writeLoose(data / "meshes" / "test" / "rock.nif", override);
        onv::assets::DataFiles vfs(data.string());
        auto r = vfs.resolve("meshes\\test\\rock.nif");
        CHECK(r.has_value() && *r == override); // loose beats archived
        CHECK(vfs.looseCount() >= 2);
    }

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all data-files tests passed (bsa read, ci lookup, loose wins)\n");
    return 0;
}

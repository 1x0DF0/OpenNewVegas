// Open New Vegas — Large Address Aware patcher (the "4GB patch").
//
// FalloutNV.exe is a 32-bit program built without the Large Address Aware
// flag, so Windows caps it at 2 GB of address space — the main cause of
// late-game and modded-game crashes. Setting the LAA bit in the PE header
// lets the (64-bit-OS-hosted) process use 4 GB. This is the standard,
// community-accepted fix; this tool applies it to the user's own executable.
//
//   laa_patch <path-to-FalloutNV.exe>
//
// A one-time backup (<file>.laa.bak) is written next to the original.
// Note: Steam's "Verify integrity of game files" restores the original exe,
// so re-run this after verifying or after game updates.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020;

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

template <typename T>
T rd(const std::vector<std::uint8_t>& b, std::size_t off) {
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: laa_patch <path-to-exe>\n");
        return 1;
    }
    const std::string path = argv[1];
    auto buf = readFile(path);
    if (buf.empty()) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 1;
    }

    // DOS header: "MZ", e_lfanew (offset of PE header) at 0x3C
    if (buf.size() < 0x40 || buf[0] != 'M' || buf[1] != 'Z') {
        std::fprintf(stderr, "not a Windows executable (no MZ header)\n");
        return 1;
    }
    const auto peOff = rd<std::uint32_t>(buf, 0x3C);
    if (peOff + 26 > buf.size() ||
        std::memcmp(buf.data() + peOff, "PE\0\0", 4) != 0) {
        std::fprintf(stderr, "not a valid PE executable\n");
        return 1;
    }

    // COFF header follows the 4-byte signature; Characteristics is its
    // final uint16 (offset +18 from the COFF start)
    const std::size_t characteristicsOff = peOff + 4 + 18;
    auto characteristics = rd<std::uint16_t>(buf, characteristicsOff);

    // Optional header magic: 0x10B = PE32 (32-bit). LAA on a 64-bit exe is
    // meaningless; refuse so we only touch what this patch is for.
    const auto magic = rd<std::uint16_t>(buf, peOff + 24);
    if (magic != 0x10B) {
        std::fprintf(stderr, "not a 32-bit (PE32) executable — nothing to patch\n");
        return 1;
    }

    if (characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) {
        std::printf("already Large Address Aware — nothing to do\n");
        return 0;
    }

    // One-time backup
    const std::string bak = path + ".laa.bak";
    if (readFile(bak).empty()) {
        std::ofstream b(bak, std::ios::binary);
        if (!b) {
            std::fprintf(stderr, "cannot write backup %s — aborting\n", bak.c_str());
            return 1;
        }
        b.write(reinterpret_cast<const char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        std::printf("backup written: %s\n", bak.c_str());
    }

    characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
    std::memcpy(buf.data() + characteristicsOff, &characteristics, 2);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return 1;
    }
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    std::printf("patched: %s is now Large Address Aware (4 GB)\n", path.c_str());
    return 0;
}

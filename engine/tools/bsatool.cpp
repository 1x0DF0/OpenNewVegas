// Open New Vegas — BSA archive inspector/extractor.
//
//   bsatool list <archive.bsa>
//   bsatool extract <archive.bsa> <internal\path> <out-file>

#include "../formats/bsa.hpp"

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage:\n  bsatool list <archive.bsa>\n"
                     "  bsatool extract <archive.bsa> <internal\\path> <out>\n");
        return 1;
    }
    const std::string cmd = argv[1];

    try {
        onv::bsa::Archive archive(argv[2]);

        if (cmd == "list") {
            for (const auto& e : archive.files()) {
                std::printf("%10u  %s%s\n", e.size, e.path().c_str(),
                            e.compressed ? "  [z]" : "");
            }
            std::printf("-- %zu files (BSA v%u, default %s)\n",
                        archive.files().size(), archive.version(),
                        archive.defaultCompressed() ? "compressed" : "stored");
            return 0;
        }

        if (cmd == "extract" && argc == 5) {
            const auto* entry = archive.find(argv[3]);
            if (!entry) {
                std::fprintf(stderr, "not in archive: %s\n", argv[3]);
                return 1;
            }
            const auto bytes = archive.extract(*entry);
            std::ofstream out(argv[4], std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            std::printf("wrote %zu bytes to %s\n", bytes.size(), argv[4]);
            return 0;
        }

        std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

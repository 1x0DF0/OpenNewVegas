// Open New Vegas — ESM/ESP plugin inspector.
//
//   esmdump counts <plugin.esm>           record counts by type
//   esmdump list <plugin.esm> <TYPE>      EditorID + FormID of every TYPE record

#include "../formats/esm.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage:\n  esmdump counts <plugin.esm>\n"
                     "  esmdump list <plugin.esm> <TYPE>   (e.g. WRLD, QUST, NPC_)\n");
        return 1;
    }
    const std::string cmd = argv[1];

    try {
        if (cmd == "counts") {
            std::size_t total = 0;
            for (const auto& [type, n] : onv::esm::recordCounts(argv[2])) {
                std::printf("%s  %8zu\n", type.c_str(), n);
                total += n;
            }
            std::printf("-- %zu records total\n", total);
            return 0;
        }

        if (cmd == "list" && argc == 4) {
            const std::string type = argv[3];
            std::size_t shown = 0;
            onv::esm::walk(argv[2], [&](const onv::esm::Record& rec) {
                if (rec.type != type) return;
                const auto* full = rec.find("FULL");
                std::printf("%08X  %-30s %s\n", rec.formId,
                            rec.editorId().c_str(),
                            full ? full->asString().c_str() : "");
                ++shown;
            }, {type});
            std::printf("-- %zu %s records\n", shown, type.c_str());
            return 0;
        }

        std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

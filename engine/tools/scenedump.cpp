// Open New Vegas — scene inspector.
//
// Assembles the placed static objects of a worldspace from the user's own
// game files and reports what loaded. Run it against your install:
//
//   scenedump <DataDir> [worldspaceEditorID] [radiusCells]
//
// Example:
//   scenedump "D:/SteamLibrary/steamapps/common/Fallout New Vegas/Data" WastelandNV 2

#include "../assets/data_files.hpp"
#include "../scene/scene.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: scenedump <DataDir> [worldspaceEditorID] "
                     "[radiusCells=2]\n");
        return 1;
    }
    const std::string dataDir = argv[1];
    const std::string worldId = argc > 2 ? argv[2] : "";
    const int radius = argc > 3 ? std::atoi(argv[3]) : 2;

    try {
        const std::string esm = dataDir + "/FalloutNV.esm";
        std::printf("loading plugin %s ...\n", esm.c_str());
        const auto world = onv::records::loadWorld(esm);

        std::printf("mounting Data archives in %s ...\n", dataDir.c_str());
        onv::assets::DataFiles vfs(dataDir);
        std::printf("  %zu BSA archives, %zu archived files, %zu loose files\n",
                    vfs.archiveCount(), vfs.archivedFileCount(), vfs.looseCount());

        const auto loader = onv::scene::makeNifModelLoader(vfs);
        const auto scene = onv::scene::buildScene(world, worldId, loader, radius);

        std::printf("\nworldspace: %s\n", scene.worldspaceEditorId.c_str());
        std::printf("placed instances: %zu\n", scene.instances.size());
        std::printf("unique models loaded: %zu\n", scene.models.size());
        std::printf("skipped (non-static base): %zu\n", scene.skippedNonStatic);
        std::printf("missing/unresolvable model: %zu\n", scene.missingModel);

        std::printf("\nfirst models loaded:\n");
        std::size_t shown = 0;
        for (const auto& [path, model] : scene.models) {
            std::size_t verts = 0, tris = 0;
            for (const auto& m : model.meshes) {
                verts += m.vertices.size() / 3;
                tris += m.indices.size() / 3;
            }
            std::printf("  %-44s %zu meshes, %zu verts, %zu tris\n",
                        path.c_str(), model.meshes.size(), verts, tris);
            if (++shown >= 15) break;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

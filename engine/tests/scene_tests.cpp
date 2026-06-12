// Open New Vegas — scene assembly tests.
//
// Builds an in-memory decoded World (no files) and injects a fake ModelLoader,
// so the placement logic is verified independently of NIF/BSA decoding: the
// right instances at the right transforms, model de-duplication, and the
// skip/count behavior for references whose base isn't a known static.

#include "../scene/scene.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

bool feq(float a, float b) { return std::fabs(a - b) < 1e-5f; }

onv::records::PlacedRef makeRef(std::uint32_t formId, std::uint32_t base,
                                float x, float y, float z, float scale) {
    onv::records::PlacedRef r;
    r.formId = formId;
    r.baseFormId = base;
    r.x = x; r.y = y; r.z = z;
    r.scale = scale;
    return r;
}

} // namespace

int main() {
    using namespace onv;

    // ── Build an in-memory world ──
    // Two STATs (a rock and a shack), one base form that is NOT a static.
    records::World world;
    world.statics[0x100] = {0x100, "RockBoulder", "rocks\\boulder01.nif"};
    world.statics[0x101] = {0x101, "Shack",       "architecture\\shack.nif"};
    world.statics[0x102] = {0x102, "NoModelStat", ""}; // STAT without a model

    records::Worldspace ws;
    ws.formId = 0x10;
    ws.editorId = "WastelandNV";

    records::ExteriorCell cell;
    cell.gridX = 0; cell.gridY = 0;
    // Two rocks (same model -> should dedupe to one model entry), one shack,
    // one ref to a modelless STAT, and one ref to an unknown base (e.g. an NPC).
    cell.refs.push_back(makeRef(0xA0, 0x100, 100, 200, 30, 1.0f));
    cell.refs.push_back(makeRef(0xA1, 0x100, 400, 500, 60, 2.5f));
    cell.refs.push_back(makeRef(0xA2, 0x101, 700, 800, 90, 1.0f));
    cell.refs.push_back(makeRef(0xA3, 0x102, 0, 0, 0, 1.0f));   // modelless STAT
    cell.refs.push_back(makeRef(0xA4, 0x999, 0, 0, 0, 1.0f));   // unknown base
    ws.cells.emplace(std::make_pair(0, 0), std::move(cell));
    world.worldspaces.push_back(std::move(ws));

    // ── Fake loader: any path yields a one-triangle model; record calls ──
    int loadCalls = 0;
    auto loader = [&](const std::string& path) -> std::optional<scene::Model> {
        ++loadCalls;
        scene::Model m;
        nif::Mesh mesh;
        mesh.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        mesh.indices = {0, 1, 2};
        m.meshes.push_back(std::move(mesh));
        (void)path;
        return m;
    };

    const auto sc = scene::buildScene(world, "WastelandNV", loader);

    CHECK(sc.worldspaceEditorId == "WastelandNV");

    // Three placed instances: two rocks + one shack.
    CHECK(sc.instances.size() == 3);
    // Two distinct models (boulder + shack); the loader is called once per
    // unique model path, not once per instance.
    CHECK(sc.models.size() == 2);
    CHECK(loadCalls == 2);
    CHECK(sc.models.count("rocks\\boulder01.nif") == 1);
    CHECK(sc.models.count("architecture\\shack.nif") == 1);

    // The modelless STAT and the unknown base are skipped and counted.
    CHECK(sc.missingModel == 1);     // 0xA3 -> STAT with empty model
    CHECK(sc.skippedNonStatic == 1); // 0xA4 -> base not in statics

    // Transforms carried through verbatim from the REFRs.
    const scene::Instance* rock2 = nullptr;
    for (const auto& i : sc.instances)
        if (i.refrFormId == 0xA1) rock2 = &i;
    CHECK(rock2 != nullptr);
    if (rock2) {
        CHECK(rock2->baseFormId == 0x100);
        CHECK(rock2->modelPath == "rocks\\boulder01.nif");
        CHECK(feq(rock2->x, 400) && feq(rock2->y, 500) && feq(rock2->z, 60));
        CHECK(feq(rock2->scale, 2.5f));
    }

    // ── Radius filter: a far cell is excluded ──
    {
        records::World w2 = world;
        records::ExteriorCell far;
        far.gridX = 50; far.gridY = 50;
        far.refs.push_back(makeRef(0xB0, 0x100, 0, 0, 0, 1.0f));
        w2.worldspaces[0].cells.emplace(std::make_pair(50, 50), std::move(far));
        const auto near = scene::buildScene(w2, "WastelandNV", loader, 2);
        CHECK(near.instances.size() == 3); // far cell (50,50) excluded by radius 2
    }

    // ── Empty editorId auto-selects the worldspace with the most refs ──
    {
        const auto auto_ws = scene::buildScene(world, "", loader);
        CHECK(auto_ws.worldspaceEditorId == "WastelandNV");
        CHECK(auto_ws.instances.size() == 3);
    }

    // ── SceneStreamer: cell-at-a-time streaming with a shared model cache ──
    {
        // A worldspace with two populated cells and one empty cell. (0,0) holds
        // two rocks (shared model) + a shack; (1,0) holds one more rock (same
        // boulder model as (0,0), so it must reuse the cached model).
        records::World sw;
        sw.statics[0x100] = {0x100, "RockBoulder", "rocks\\boulder01.nif"};
        sw.statics[0x101] = {0x101, "Shack",       "architecture\\shack.nif"};
        sw.statics[0x102] = {0x102, "NoModelStat", ""}; // STAT, empty model

        records::Worldspace sws;
        sws.formId = 0x20;
        sws.editorId = "WastelandNV";

        records::ExteriorCell c00;
        c00.gridX = 0; c00.gridY = 0;
        c00.refs.push_back(makeRef(0xA0, 0x100, 100, 200, 30, 1.0f));
        c00.refs.push_back(makeRef(0xA1, 0x100, 400, 500, 60, 2.5f));
        c00.refs.push_back(makeRef(0xA2, 0x101, 700, 800, 90, 1.0f));
        c00.refs.push_back(makeRef(0xA3, 0x102, 0, 0, 0, 1.0f)); // modelless STAT
        c00.refs.push_back(makeRef(0xA4, 0x999, 0, 0, 0, 1.0f)); // unknown base
        sws.cells.emplace(std::make_pair(0, 0), std::move(c00));

        records::ExteriorCell c10;
        c10.gridX = 1; c10.gridY = 0;
        c10.refs.push_back(makeRef(0xB0, 0x100, 10, 20, 30, 1.0f)); // shared model
        sws.cells.emplace(std::make_pair(1, 0), std::move(c10));

        // A cell with no refs at all: must not appear in populatedCells().
        records::ExteriorCell empty;
        empty.gridX = 2; empty.gridY = 0;
        sws.cells.emplace(std::make_pair(2, 0), std::move(empty));

        sw.worldspaces.push_back(std::move(sws));

        int swCalls = 0;
        auto swLoader = [&](const std::string& path) -> std::optional<scene::Model> {
            ++swCalls;
            scene::Model m;
            nif::Mesh mesh;
            mesh.vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            mesh.indices = {0, 1, 2};
            m.meshes.push_back(std::move(mesh));
            (void)path;
            return m;
        };

        scene::SceneStreamer streamer(sw, "WastelandNV", swLoader);
        CHECK(streamer.worldspaceEditorId() == "WastelandNV");

        // populatedCells lists exactly the cells with REFRs (empty cell omitted),
        // sorted deterministically by (gx, gy).
        const auto& pop = streamer.populatedCells();
        CHECK(pop.size() == 2);
        if (pop.size() == 2) {
            CHECK(pop[0] == std::make_pair(0, 0));
            CHECK(pop[1] == std::make_pair(1, 0));
        }

        // Nothing loaded until a cell is streamed.
        CHECK(swCalls == 0);
        CHECK(streamer.model("rocks\\boulder01.nif") == nullptr);

        // Stream (0,0): two rocks + one shack = 3 instances; boulder loaded once.
        const auto& inst00 = streamer.cellInstances(0, 0);
        CHECK(inst00.size() == 3);
        CHECK(swCalls == 2); // boulder + shack, deduped within the cell

        // model() returns non-null after the cell that uses it is streamed.
        CHECK(streamer.model("rocks\\boulder01.nif") != nullptr);
        CHECK(streamer.model("architecture\\shack.nif") != nullptr);

        // Transforms carried through verbatim.
        const scene::Instance* rock2 = nullptr;
        for (const auto& i : inst00)
            if (i.refrFormId == 0xA1) rock2 = &i;
        CHECK(rock2 != nullptr);
        if (rock2) {
            CHECK(rock2->baseFormId == 0x100);
            CHECK(rock2->modelPath == "rocks\\boulder01.nif");
            CHECK(feq(rock2->x, 400) && feq(rock2->y, 500) && feq(rock2->z, 60));
            CHECK(feq(rock2->scale, 2.5f));
        }

        // Stream (1,0): one rock reusing the cached boulder model -> NO new load.
        const auto& inst10 = streamer.cellInstances(1, 0);
        CHECK(inst10.size() == 1);
        CHECK(swCalls == 2); // shared cache across cells: loader not called again
        if (inst10.size() == 1) {
            CHECK(inst10[0].refrFormId == 0xB0);
            CHECK(inst10[0].modelPath == "rocks\\boulder01.nif");
        }

        // Repeated request returns the cached result; loader not invoked again.
        const auto& inst00b = streamer.cellInstances(0, 0);
        CHECK(&inst00b == &inst00); // same cached vector reference
        CHECK(inst00b.size() == 3);
        CHECK(swCalls == 2);

        // An empty (no-ref) cell yields an empty instance list.
        const auto& instEmpty = streamer.cellInstances(2, 0);
        CHECK(instEmpty.empty());

        // An absent cell (not in the worldspace) also yields an empty list.
        const auto& instAbsent = streamer.cellInstances(99, 99);
        CHECK(instAbsent.empty());
        CHECK(swCalls == 2); // empty/absent cells trigger no loads
    }

    if (failures) {
        std::printf("%d FAILURES\n", failures);
        return 1;
    }
    std::printf("all scene tests passed (placement, dedupe, skip, radius, streamer)\n");
    return 0;
}

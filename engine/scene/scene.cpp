// Open New Vegas — scene assembly implementation.

#include "scene.hpp"

#include <algorithm>
#include <cctype>

namespace onv::scene {
namespace {

const records::Worldspace* pickWorldspace(const records::World& world,
                                          const std::string& editorId) {
    if (!editorId.empty()) return world.findWorldspace(editorId);

    if (const auto* w = world.findWorldspace("WastelandNV")) return w;

    const records::Worldspace* best = nullptr;
    std::size_t bestRefs = 0;
    for (const auto& w : world.worldspaces) {
        std::size_t refs = 0;
        for (const auto& [grid, cell] : w.cells) refs += cell.refs.size();
        if (refs >= bestRefs) {
            bestRefs = refs;
            best = &w;
        }
    }
    return best;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Ensure `modelPath`'s geometry is present in `models`, loading it once via
// `loader` on a cache miss. Returns true if a usable (non-empty) model is
// available afterwards; false if it couldn't be loaded.
bool ensureModelLoaded(std::map<std::string, Model>& models,
                       const ModelLoader& loader,
                       const std::string& modelPath) {
    if (models.find(modelPath) != models.end()) return true;
    auto model = loader(modelPath);
    if (!model || model->meshes.empty()) return false;
    models.emplace(modelPath, std::move(*model));
    return true;
}

Instance makeInstance(const records::PlacedRef& ref,
                      const std::string& modelPath) {
    Instance inst;
    inst.refrFormId = ref.formId;
    inst.baseFormId = ref.baseFormId;
    inst.modelPath = modelPath;
    inst.x = ref.x; inst.y = ref.y; inst.z = ref.z;
    inst.rotX = ref.rotX; inst.rotY = ref.rotY; inst.rotZ = ref.rotZ;
    inst.scale = ref.scale;
    return inst;
}

} // namespace

Scene buildScene(const records::World& world,
                 const std::string& worldspaceEditorId,
                 const ModelLoader& loader, int radiusCells) {
    Scene scene;
    const records::Worldspace* ws = pickWorldspace(world, worldspaceEditorId);
    if (!ws) return scene;
    scene.worldspaceEditorId = ws->editorId;

    for (const auto& [grid, cell] : ws->cells) {
        if (std::abs(grid.first) > radiusCells ||
            std::abs(grid.second) > radiusCells)
            continue;

        for (const auto& ref : cell.refs) {
            // Only static objects (STAT) are placed for now. References to
            // NPCs, creatures, doors, etc. are counted and skipped.
            const auto baseIt = world.statics.find(ref.baseFormId);
            if (baseIt == world.statics.end()) {
                ++scene.skippedNonStatic;
                continue;
            }
            const std::string& modelPath = baseIt->second.modelPath;
            if (modelPath.empty()) {
                ++scene.missingModel;
                continue;
            }

            if (!ensureModelLoaded(scene.models, loader, modelPath)) {
                ++scene.missingModel;
                continue;
            }

            scene.instances.push_back(makeInstance(ref, modelPath));
        }
    }
    return scene;
}

// ── SceneStreamer ────────────────────────────────────────────────────────────
SceneStreamer::SceneStreamer(const records::World& world,
                             const std::string& worldspaceEditorId,
                             ModelLoader loader)
    : world_(world), worldspaceEditorId_(worldspaceEditorId),
      loader_(std::move(loader)) {
    ws_ = pickWorldspace(world_, worldspaceEditorId_);
    if (!ws_) return;
    worldspaceEditorId_ = ws_->editorId;

    // Record every cell holding at least one REFR. ws_->cells is keyed by
    // (gridX, gridY) in a std::map, so iteration is already sorted; copying
    // those keys keeps populated_ deterministic.
    for (const auto& [grid, cell] : ws_->cells) {
        if (!cell.refs.empty()) populated_.emplace_back(grid.first, grid.second);
    }
}

const std::vector<Instance>& SceneStreamer::cellInstances(int gx, int gy) {
    const std::pair<int, int> key{gx, gy};

    const auto cached = cellCache_.find(key);
    if (cached != cellCache_.end()) return cached->second;

    // Insert the (initially empty) entry first so the returned reference is
    // stable: std::map references survive subsequent inserts, and an
    // empty/absent cell is cached as an empty vector.
    std::vector<Instance>& out = cellCache_[key];

    if (!ws_) return out;
    const auto cellIt = ws_->cells.find(
        {static_cast<std::int32_t>(gx), static_cast<std::int32_t>(gy)});
    if (cellIt == ws_->cells.end()) return out;

    for (const auto& ref : cellIt->second.refs) {
        // Only static objects (STAT) are placed. Other base types are skipped.
        const auto baseIt = world_.statics.find(ref.baseFormId);
        if (baseIt == world_.statics.end()) continue;
        const std::string& modelPath = baseIt->second.modelPath;
        if (modelPath.empty()) continue;

        // Lazily load into the shared model cache (loads once across all cells).
        if (!ensureModelLoaded(models_, loader_, modelPath)) continue;

        out.push_back(makeInstance(ref, modelPath));
    }
    return out;
}

const Model* SceneStreamer::model(const std::string& modelPath) const {
    const auto it = models_.find(modelPath);
    return it == models_.end() ? nullptr : &it->second;
}

const std::vector<std::pair<int, int>>& SceneStreamer::populatedCells() const {
    return populated_;
}

const std::string& SceneStreamer::worldspaceEditorId() const {
    return worldspaceEditorId_;
}

ModelLoader makeNifModelLoader(const assets::DataFiles& vfs) {
    return [&vfs](const std::string& modelPath) -> std::optional<Model> {
        // STAT model paths are stored relative to the meshes\ root and usually
        // omit it; try the bare path first, then the prefixed form.
        std::optional<std::vector<std::uint8_t>> bytes = vfs.resolve(modelPath);
        if (!bytes) {
            const std::string lower = toLower(modelPath);
            if (lower.rfind("meshes\\", 0) != 0)
                bytes = vfs.resolve("meshes\\" + modelPath);
        }
        if (!bytes) return std::nullopt;

        try {
            Model m;
            m.meshes = nif::decode(*bytes);
            if (m.meshes.empty()) return std::nullopt;
            return m;
        } catch (const std::exception&) {
            return std::nullopt; // unsupported/garbled NIF: skip this model
        }
    };
}

} // namespace onv::scene

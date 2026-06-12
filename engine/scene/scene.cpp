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

            if (scene.models.find(modelPath) == scene.models.end()) {
                auto model = loader(modelPath);
                if (!model || model->meshes.empty()) {
                    ++scene.missingModel;
                    continue;
                }
                scene.models.emplace(modelPath, std::move(*model));
            }

            Instance inst;
            inst.refrFormId = ref.formId;
            inst.baseFormId = ref.baseFormId;
            inst.modelPath = modelPath;
            inst.x = ref.x; inst.y = ref.y; inst.z = ref.z;
            inst.rotX = ref.rotX; inst.rotY = ref.rotY; inst.rotZ = ref.rotZ;
            inst.scale = ref.scale;
            scene.instances.push_back(std::move(inst));
        }
    }
    return scene;
}

// ── SceneStreamer ─── STUB: implemented by the streaming work stream. ────────
SceneStreamer::SceneStreamer(const records::World& world,
                             const std::string& worldspaceEditorId,
                             ModelLoader loader)
    : world_(world), worldspaceEditorId_(worldspaceEditorId),
      loader_(std::move(loader)) {
    ws_ = pickWorldspace(world_, worldspaceEditorId_);
    if (ws_) worldspaceEditorId_ = ws_->editorId;
}

const std::vector<Instance>& SceneStreamer::cellInstances(int gx, int gy) {
    static const std::vector<Instance> kEmpty;
    (void)gx; (void)gy;
    return kEmpty;
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

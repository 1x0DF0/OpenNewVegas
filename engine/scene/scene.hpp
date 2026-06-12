// Open New Vegas — scene assembly.
//
// Turns a decoded worldspace into a list of placed mesh instances: for every
// object reference (REFR) in the worldspace, resolve its base object (STAT) to
// a model path, load that model's geometry, and record where/how it is placed.
// This is the step that makes the world stop being bare terrain.
//
// Geometry loading is injected (ModelLoader) so the assembly logic is testable
// without game files. The production loader pulls NIF bytes from the Data
// virtual filesystem and decodes them; see makeNifModelLoader().

#pragma once

#include "../assets/data_files.hpp"
#include "../formats/nif.hpp"
#include "../records/records.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace onv::scene {

// A loaded model: the meshes of one NIF (vertices in local game units).
struct Model {
    std::vector<nif::Mesh> meshes;
};

// One placed object: a model plus its world placement, in raw game units /
// radians exactly as stored in the REFR (coordinate conversion is the
// renderer's job).
struct Instance {
    std::uint32_t refrFormId = 0;
    std::uint32_t baseFormId = 0;
    std::string modelPath;
    float x = 0, y = 0, z = 0;          // translation, game units
    float rotX = 0, rotY = 0, rotZ = 0; // radians
    float scale = 1.0f;
};

struct Scene {
    std::map<std::string, Model> models;   // modelPath -> geometry (deduped)
    std::vector<Instance> instances;
    std::size_t skippedNonStatic = 0;      // REFR whose base isn't a known STAT
    std::size_t missingModel = 0;          // STAT with no model / unresolvable NIF
    std::string worldspaceEditorId;
};

// path -> model geometry, or nullopt if it can't be loaded.
using ModelLoader = std::function<std::optional<Model>(const std::string&)>;

// Assemble a worldspace. If `worldspaceEditorId` is empty, picks "WastelandNV"
// if present, else the worldspace with the most references. `radiusCells`
// limits placement to cells within that Chebyshev distance of (0,0); a large
// default includes everything.
Scene buildScene(const records::World& world,
                 const std::string& worldspaceEditorId,
                 const ModelLoader& loader,
                 int radiusCells = 1 << 20);

// Production loader: resolve a STAT model path to NIF bytes via the Data VFS
// (trying both the bare path and a "meshes\\" prefix) and decode it.
ModelLoader makeNifModelLoader(const assets::DataFiles& vfs);

// Streams placed objects a cell at a time, so a renderer can load objects
// around the player instead of all at once. Models are loaded lazily and
// cached, shared across cells; per-cell instance lists are built on first
// request and cached. The referenced World and loader must outlive the
// streamer.
class SceneStreamer {
public:
    SceneStreamer(const records::World& world,
                  const std::string& worldspaceEditorId, ModelLoader loader);

    // Instances placed in exterior cell (gx, gy). Built and cached on first
    // request; returns an empty list for cells with no placeable statics.
    const std::vector<Instance>& cellInstances(int gx, int gy);

    // A model previously loaded during cellInstances(), or nullptr.
    const Model* model(const std::string& modelPath) const;

    // Every cell in the worldspace that holds at least one REFR.
    const std::vector<std::pair<int, int>>& populatedCells() const;

    const std::string& worldspaceEditorId() const;

private:
    const records::World& world_;
    const records::Worldspace* ws_ = nullptr;
    std::string worldspaceEditorId_;
    ModelLoader loader_;
    std::map<std::string, Model> models_;
    std::map<std::pair<int, int>, std::vector<Instance>> cellCache_;
    std::vector<std::pair<int, int>> populated_;
};

} // namespace onv::scene

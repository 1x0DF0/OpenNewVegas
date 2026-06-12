// Open New Vegas — Data-directory virtual filesystem.
//
// The game resolves an asset path like "meshes\\rocks\\boulder01.nif" by
// looking first at loose files under Data/, then inside the mounted BSA
// archives. This class reproduces that lookup over the user's own Data
// directory: it indexes every loose file and every BSA entry once, then
// resolves an internal path to bytes. Ships no game data — it reads the
// user's install.

#pragma once

#include "../formats/bsa.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace onv::assets {

class DataFiles {
public:
    // Index every *.bsa and loose file under `dataDir`. Throws if the
    // directory is unreadable; individual unreadable BSAs are skipped.
    explicit DataFiles(const std::string& dataDir);

    // Resolve an internal path (case-insensitive, '/' or '\\' separators) to
    // its bytes. Loose files take precedence over archived ones, as in-game.
    std::optional<std::vector<std::uint8_t>> resolve(const std::string& path) const;

    bool contains(const std::string& path) const;

    std::size_t archiveCount() const { return archives_.size(); }
    std::size_t looseCount() const { return loose_.size(); }
    std::size_t archivedFileCount() const { return archived_.size(); }

private:
    struct ArchivedRef {
        std::size_t archiveIndex;
        bsa::FileEntry entry;
    };

    std::string dataDir_;
    std::vector<std::unique_ptr<bsa::Archive>> archives_;
    std::unordered_map<std::string, std::string> loose_;      // key -> abs path
    std::unordered_map<std::string, ArchivedRef> archived_;   // key -> entry
};

} // namespace onv::assets

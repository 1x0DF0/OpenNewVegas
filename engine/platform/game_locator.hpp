// Open New Vegas — locate the user's Fallout: New Vegas install.
//
// The engine ships no game data; it reads the files from the user's own
// legally-owned copy. These helpers find that copy on disk: an explicit
// override via the ONV_FNV_PATH env var, then a list of well-known Steam /
// GOG / Proton install locations, then any extra Steam library folders
// discovered by parsing libraryfolders.vdf.
#pragma once

#include <optional>
#include <string>

namespace onv::platform {

// Absolute path to the FNV "Data" directory, or nullopt if not found.
std::optional<std::string> findFalloutNVData();

// Absolute path to FalloutNV.esm, or nullopt if not found.
std::optional<std::string> findFalloutNVMasterEsm();

} // namespace onv::platform

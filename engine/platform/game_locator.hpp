// Open New Vegas — locate the user's Fallout: New Vegas install.
// STUB — to be implemented.
#pragma once

#include <optional>
#include <string>

namespace onv::platform {

// Absolute path to the FNV "Data" directory, or nullopt if not found.
std::optional<std::string> findFalloutNVData();

// Absolute path to FalloutNV.esm, or nullopt if not found.
std::optional<std::string> findFalloutNVMasterEsm();

} // namespace onv::platform

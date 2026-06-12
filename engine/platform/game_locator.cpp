// Open New Vegas — locate the user's Fallout: New Vegas install.
//
// Detection order:
//   1. ONV_FNV_PATH env var — the install ROOT (folder containing Data/), or
//      a path that points directly at Data/.
//   2. A list of common install paths (Windows Steam across drive letters and
//      both "Steam"/"SteamLibrary" roots, GOG, Linux/Proton).
//   3. steamapps/libraryfolders.vdf parsing to discover extra Steam library
//      folders, then look for common/Fallout New Vegas/Data/FalloutNV.esm in
//      each.
//
// An install "exists" iff Data/FalloutNV.esm is present.
#include "game_locator.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace onv::platform {
namespace {

namespace fs = std::filesystem;

constexpr const char* kMasterEsm = "FalloutNV.esm";
constexpr const char* kGameDir = "Fallout New Vegas";

// True if `dataDir`/FalloutNV.esm exists.
bool dataHasMaster(const fs::path& dataDir) {
    std::error_code ec;
    return fs::exists(dataDir / kMasterEsm, ec);
}

// Expand a leading "~" to the user's home directory (HOME, then USERPROFILE).
std::string expandHome(const std::string& in) {
    if (in.empty() || in[0] != '~') return in;
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return in;
    return std::string(home) + in.substr(1);
}

// Given an install ROOT (folder containing Data/), return its Data dir if it
// holds FalloutNV.esm, else nullopt.
std::optional<fs::path> dataFromRoot(const fs::path& root) {
    const fs::path data = root / "Data";
    if (dataHasMaster(data)) return data;
    return std::nullopt;
}

// Accept a path that is either the install root or the Data dir itself.
std::optional<fs::path> dataFromRootOrData(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    // Pointing directly at Data/?
    if (dataHasMaster(p)) return p;
    // Pointing at the install root?
    if (auto d = dataFromRoot(p)) return d;
    return std::nullopt;
}

// Candidate install ROOTS to probe (each expected to contain Data/).
std::vector<std::string> commonInstallRoots() {
    std::vector<std::string> roots;

    // Windows Steam / SteamLibrary across common drive letters.
    const char* steamRoots[] = {"Steam", "SteamLibrary"};
    for (char drive = 'C'; drive <= 'F'; ++drive)
        for (const char* sr : steamRoots) {
            std::string p;
            p += drive;
            p += ":/";
            // Default Steam lives under Program Files (x86) on C:, but library
            // folders on other drives sit at the drive root. Probe both forms.
            if (std::string(sr) == "Steam") {
                roots.push_back(std::string(1, drive) +
                                ":/Program Files (x86)/Steam/steamapps/common/" +
                                kGameDir);
            }
            roots.push_back(std::string(1, drive) + ":/" + sr +
                            "/steamapps/common/" + kGameDir);
        }

    // Windows GOG.
    roots.push_back(std::string("C:/Program Files (x86)/GOG Galaxy/Games/") +
                    "Fallout New Vegas");
    roots.push_back("C:/GOG Games/Fallout New Vegas");

    // Linux / Proton Steam locations.
    const char* linuxSteam[] = {
        "~/.steam/steam/steamapps/common/",
        "~/.steam/root/steamapps/common/",
        "~/.local/share/Steam/steamapps/common/",
        "~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/",
    };
    for (const char* base : linuxSteam)
        roots.push_back(expandHome(std::string(base) + kGameDir));

    return roots;
}

// Steam roots whose steamapps/libraryfolders.vdf we should parse for extra
// library folders.
std::vector<fs::path> steamRootsForVdf() {
    std::vector<fs::path> roots;
    // Windows.
    const char* steamLabels[] = {"Steam", "SteamLibrary"};
    for (char drive = 'C'; drive <= 'F'; ++drive) {
        roots.push_back(std::string(1, drive) + ":/Program Files (x86)/Steam");
        for (const char* sr : steamLabels)
            roots.push_back(std::string(1, drive) + ":/" + sr);
    }
    // Linux / Proton.
    const char* linuxRoots[] = {
        "~/.steam/steam",
        "~/.steam/root",
        "~/.local/share/Steam",
        "~/.var/app/com.valvesoftware.Steam/.local/share/Steam",
    };
    for (const char* r : linuxRoots) roots.push_back(expandHome(r));
    return roots;
}

// Extract every "path" value from a libraryfolders.vdf. The VDF is a quoted
// key/value text format; library folder entries carry a "path" key whose value
// is the library root (the folder containing steamapps/).
std::vector<fs::path> parseLibraryFoldersVdf(const fs::path& vdf) {
    std::vector<fs::path> out;
    std::ifstream f(vdf);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        // Look for a "path" key on this line.
        const std::string key = "\"path\"";
        const auto kpos = line.find(key);
        if (kpos == std::string::npos) continue;
        // The value is the next quoted token after the key.
        const auto vstart = line.find('"', kpos + key.size());
        if (vstart == std::string::npos) continue;
        const auto vend = line.find('"', vstart + 1);
        if (vend == std::string::npos) continue;
        std::string val = line.substr(vstart + 1, vend - vstart - 1);
        // VDF escapes backslashes as "\\"; collapse to single.
        std::string cleaned;
        cleaned.reserve(val.size());
        for (std::size_t i = 0; i < val.size(); ++i) {
            if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == '\\') {
                cleaned += '\\';
                ++i;
            } else {
                cleaned += val[i];
            }
        }
        if (!cleaned.empty()) out.push_back(cleaned);
    }
    return out;
}

// Probe Steam libraries discovered via libraryfolders.vdf.
std::optional<fs::path> dataFromSteamLibraries() {
    std::error_code ec;
    for (const auto& root : steamRootsForVdf()) {
        const fs::path vdf = root / "steamapps" / "libraryfolders.vdf";
        if (!fs::exists(vdf, ec)) continue;
        for (const auto& lib : parseLibraryFoldersVdf(vdf)) {
            const fs::path install =
                fs::path(lib) / "steamapps" / "common" / kGameDir;
            if (auto d = dataFromRoot(install)) return d;
        }
    }
    return std::nullopt;
}

// Resolve the Data directory using the full detection order.
std::optional<fs::path> resolveDataDir() {
    // 1. Explicit override.
    if (const char* env = std::getenv("ONV_FNV_PATH")) {
        if (env[0] != '\0') {
            if (auto d = dataFromRootOrData(expandHome(env))) return d;
        }
    }
    // 2. Common install roots.
    for (const auto& root : commonInstallRoots())
        if (auto d = dataFromRoot(root)) return d;
    // 3. Extra Steam library folders.
    if (auto d = dataFromSteamLibraries()) return d;
    return std::nullopt;
}

} // namespace

std::optional<std::string> findFalloutNVData() {
    if (auto d = resolveDataDir()) return d->string();
    return std::nullopt;
}

std::optional<std::string> findFalloutNVMasterEsm() {
    if (auto d = resolveDataDir()) return (*d / kMasterEsm).string();
    return std::nullopt;
}

} // namespace onv::platform

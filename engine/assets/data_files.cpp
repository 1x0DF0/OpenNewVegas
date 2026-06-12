// Open New Vegas — Data-directory virtual filesystem implementation.

#include "data_files.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace onv::assets {
namespace {

namespace fs = std::filesystem;

// Normalize an internal asset path to the index key: backslash separators,
// lowercase, no leading separator.
std::string normalize(const std::string& in) {
    std::string s;
    s.reserve(in.size());
    for (char c : in) {
        if (c == '/') c = '\\';
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    std::size_t start = 0;
    while (start < s.size() && s[start] == '\\') ++start;
    return s.substr(start);
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read loose file: " + path);
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> buf(size);
    if (size) f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool hasExtensionCI(const fs::path& p, const char* ext) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return e == ext;
}

} // namespace

DataFiles::DataFiles(const std::string& dataDir) : dataDir_(dataDir) {
    std::error_code ec;
    const fs::path root(dataDir);
    if (!fs::is_directory(root, ec))
        throw std::runtime_error("not a directory: " + dataDir);

    // Mount BSAs first (top-level of Data/), then index their entries.
    std::vector<fs::path> bsaPaths;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_regular_file(ec) && hasExtensionCI(e.path(), ".bsa"))
            bsaPaths.push_back(e.path());
    std::sort(bsaPaths.begin(), bsaPaths.end()); // deterministic order

    for (const auto& bp : bsaPaths) {
        try {
            auto archive = std::make_unique<bsa::Archive>(bp.string());
            const std::size_t idx = archives_.size();
            for (const auto& entry : archive->files()) {
                // Earlier archives win ties (stable): only insert if absent.
                archived_.emplace(normalize(entry.path()), ArchivedRef{idx, entry});
            }
            archives_.push_back(std::move(archive));
        } catch (const std::exception&) {
            // Skip an unreadable/unsupported archive rather than failing the lot.
        }
    }

    // Index loose files recursively. These override archived entries.
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        if (hasExtensionCI(e.path(), ".bsa")) continue;
        const auto rel = fs::relative(e.path(), root, ec);
        if (ec || rel.empty()) continue;
        loose_[normalize(rel.string())] = e.path().string();
    }
}

bool DataFiles::contains(const std::string& path) const {
    const auto key = normalize(path);
    return loose_.count(key) || archived_.count(key);
}

std::optional<std::vector<std::uint8_t>>
DataFiles::resolve(const std::string& path) const {
    const auto key = normalize(path);

    if (const auto it = loose_.find(key); it != loose_.end())
        return readFile(it->second);

    if (const auto it = archived_.find(key); it != archived_.end()) {
        const auto& ref = it->second;
        return archives_[ref.archiveIndex]->extract(ref.entry);
    }
    return std::nullopt;
}

} // namespace onv::assets

// COPG open-source engine — GPU manifest (GPU/manifest.json) loader.
#include "gpu.hpp"
#include "strings.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "third_party/json.hpp"

namespace copg {
namespace {

// Cached manifest: key -> entry. Loaded lazily once per process.
struct GpuTable {
    bool loaded = false;
    std::map<std::string, GpuEntry> entries;
};

GpuTable &table() {
    static GpuTable t;
    return t;
}

void load(const std::string &manifest_path) {
    GpuTable &t = table();
    t.loaded = true;

    std::ifstream f(manifest_path, std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();

    try {
        auto j = nlohmann::json::parse(ss.str());
        if (!j.is_array()) return;
        for (const auto &v : j) {
            GpuEntry e;
            if (!v.contains("key") || !v["key"].is_string()) continue;
            e.key = v["key"].get<std::string>();
            if (v.contains("name") && v["name"].is_string()) e.name = v["name"].get<std::string>();
            if (v.contains("renderer") && v["renderer"].is_string())
                e.renderer = v["renderer"].get<std::string>();
            if (v.contains("vendor") && v["vendor"].is_string())
                e.vendor = v["vendor"].get<std::string>();
            if (!e.key.empty()) t.entries[e.key] = std::move(e);
        }
    } catch (const std::exception &) {
        // Malformed manifest: leave the table empty (callers get nullptr).
    }
}

}  // namespace

const GpuEntry *gpu_lookup(const std::string &key, const std::string &manifest_path) {
    GpuTable &t = table();
    if (!t.loaded) load(manifest_path);
    auto it = t.entries.find(key);
    return it == t.entries.end() ? nullptr : &it->second;
}

}  // namespace copg

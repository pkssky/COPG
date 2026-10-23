// COPG open-source engine — PROXY/manifest.json loader.
#include "proxy.hpp"
#include "strings.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "third_party/json.hpp"

namespace copg {
namespace {

// Cached manifest: key -> entry. Loaded lazily once per process.
struct ProxyTable {
    bool loaded = false;
    std::map<std::string, ProxyEntry> entries;
};

ProxyTable &table() {
    static ProxyTable t;
    return t;
}

void load(const std::string &manifest_path) {
    ProxyTable &t = table();
    t.loaded = true;

    std::ifstream f(manifest_path, std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();

    try {
        auto j = nlohmann::json::parse(ss.str());
        if (!j.is_array()) return;
        for (const auto &v : j) {
            ProxyEntry e;
            if (!v.contains("key") || !v["key"].is_string()) continue;
            e.key = v["key"].get<std::string>();
            if (v.contains("host") && v["host"].is_string()) e.host = v["host"].get<std::string>();
            if (v.contains("user") && v["user"].is_string()) e.user = v["user"].get<std::string>();
            if (v.contains("pass") && v["pass"].is_string()) e.pass = v["pass"].get<std::string>();
            if (v.contains("port") && v["port"].is_number_integer())
                e.port = v["port"].get<int>();
            if (!e.key.empty() && !e.host.empty() && e.port > 0 && e.port < 65536)
                t.entries[e.key] = std::move(e);
        }
    } catch (const std::exception &) {
        // Malformed manifest: leave the table empty (callers get nullptr).
    }
}

}  // namespace

const ProxyEntry *proxy_lookup(const std::string &key, const std::string &manifest_path) {
    ProxyTable &t = table();
    if (!t.loaded) load(manifest_path);
    auto it = t.entries.find(key);
    return it == t.entries.end() ? nullptr : &it->second;
}

}  // namespace copg

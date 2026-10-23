// COPG open-source engine — SENSOR/manifest.json loader.
#include "sensor.hpp"
#include "strings.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "third_party/json.hpp"

namespace copg {
namespace {

struct SensorTable {
    bool loaded = false;
    std::map<std::string, SensorEntry> entries;
};

SensorTable &table() {
    static SensorTable t;
    return t;
}

void load(const std::string &manifest_path) {
    SensorTable &t = table();
    t.loaded = true;

    std::ifstream f(manifest_path, std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();

    try {
        auto j = nlohmann::json::parse(ss.str());
        if (!j.is_array()) return;
        for (const auto &v : j) {
            SensorEntry e;
            if (!v.contains("key") || !v["key"].is_string()) continue;
            e.key = v["key"].get<std::string>();
            if (v.contains("name") && v["name"].is_string()) e.name = v["name"].get<std::string>();
            if (v.contains("hidden") && v["hidden"].is_array()) {
                for (const auto &h : v["hidden"]) {
                    if (h.is_number_integer()) e.hidden.push_back(h.get<int>());
                }
            }
            if (v.contains("rename") && v["rename"].is_object()) {
                for (auto it = v["rename"].begin(); it != v["rename"].end(); ++it) {
                    int type = 0;
                    if (std::sscanf(it.key().c_str(), "%d", &type) != 1 || type <= 0) continue;
                    SensorOverride ov;
                    const auto &o = it.value();
                    if (o.contains("name") && o["name"].is_string())
                        ov.name = o["name"].get<std::string>();
                    if (o.contains("vendor") && o["vendor"].is_string())
                        ov.vendor = o["vendor"].get<std::string>();
                    if (o.contains("string_type") && o["string_type"].is_string())
                        ov.string_type = o["string_type"].get<std::string>();
                    if (!ov.name.empty() || !ov.vendor.empty() || !ov.string_type.empty())
                        e.rename[type] = std::move(ov);
                }
            }
            if (!e.key.empty()) t.entries[e.key] = std::move(e);
        }
    } catch (const std::exception &) {
        // Malformed manifest: leave the table empty (callers get nullptr).
    }
}

}  // namespace

const SensorEntry *sensor_lookup(const std::string &key, const std::string &manifest_path) {
    SensorTable &t = table();
    if (!t.loaded) load(manifest_path);
    auto it = t.entries.find(key);
    return it == t.entries.end() ? nullptr : &it->second;
}

}  // namespace copg

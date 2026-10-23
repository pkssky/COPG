// COPG open-source engine — COPG.json parser.
//
// Uses nlohmann/json with ordered semantics: the WebUI documents that key
// insertion order is meaningful (first profile mentioning a package wins),
// so we parse with ordered_json to preserve file order.
#include "config.hpp"
#include "paths.hpp"
#include "strings.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "third_party/json.hpp"

namespace copg {
namespace {

using ordered_json = nlohmann::ordered_json;

// Parse "pkg:tag:tag:..." into package + tags. Unknown tags are ignored.
PackageEntry parse_entry(const std::string &raw) {
    PackageEntry entry;
    auto parts = split(raw, ':');
    if (parts.empty()) return entry;

    entry.package = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        const std::string &t = parts[i];
        if (t == "blocked") {
            entry.tags.blocked = true;
        } else if (t == "with_cpu") {
            entry.tags.with_cpu = true;
        } else if (t == "cow") {
            entry.tags.cow = true;
        } else if (t == "aid") {
            entry.tags.aid = true;
        } else if (starts_with(t, "cpu=")) {
            entry.tags.cpu_key = t.substr(4);
        } else if (starts_with(t, "gpu=")) {
            entry.tags.gpu_key = t.substr(4);
        } else if (starts_with(t, "user=")) {
            int id = -1;
            if (std::sscanf(t.c_str() + 5, "%d", &id) == 1) entry.tags.user_id = id;
        }
        // else: unknown tag — ignore for forward compatibility.
    }
    return entry;
}

bool parse_string_array(const ordered_json &j, std::vector<std::string> *out) {
    if (!j.is_array()) return false;
    for (const auto &v : j) {
        if (v.is_string()) out->push_back(v.get<std::string>());
    }
    return true;
}

}  // namespace

Config::Resolved Config::resolve(const std::string &package, int user_id) const {
    Resolved r;

    // Global cpu_only list: cpu spoof only, never device spoof.
    for (const auto &p : cpu_only_packages) {
        if (p == package) { r.cpu_only = true; break; }
    }

    // Global cpu blacklist: never spoof cpu for these packages.
    bool cpu_blacklisted = false;
    for (const auto &p : cpu_blacklist) {
        if (p == package) { cpu_blacklisted = true; break; }
    }

    // Scan profiles in JSON order; first mention of the package wins.
    for (const auto &key : profile_order) {
        auto pit = profiles.find(key);
        if (pit == profiles.end()) continue;
        for (const auto &entry : pit->second) {
            if (entry.package != package) continue;

            // user= filter (extension): skip entries bound to another user.
            if (entry.tags.user_id >= 0 && entry.tags.user_id != user_id) continue;

            if (!r.matched) {
                r.matched = true;
                r.tags = &entry.tags;
            }
            if (entry.tags.blocked) { r.blocked = true; return r; }
            if (r.device == nullptr) {
                auto dit = devices.find(key);
                if (dit != devices.end() && !dit->second.empty()) {
                    r.device = &dit->second;
                }
            }
            break;  // first entry for this package inside one profile wins
        }
        if (r.blocked) break;
    }

    // Apply the global cpu blacklist to the resolved tags (strip cpu spoof).
    if (cpu_blacklisted && r.tags && !r.tags->cpu_key.empty()) {
        // tags points into the config; copy, strip, and repoint.
        static thread_local PackageTags patched;
        patched = *r.tags;
        patched.cpu_key.clear();
        patched.with_cpu = false;
        r.tags = &patched;
    }
    return r;
}

bool parse_config(const std::string &text, Config *out, std::string *error) {
    ordered_json j;
    try {
        j = ordered_json::parse(text);
    } catch (const std::exception &e) {
        if (error) *error = std::string("JSON parse error: ") + e.what();
        return false;
    }
    if (!j.is_object()) {
        if (error) *error = "config root is not an object";
        return false;
    }

    out->profile_order.clear();

    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.key();

        if (key == "cpu_spoof") {
            if (it.value().contains("blacklist"))
                parse_string_array(it.value()["blacklist"], &out->cpu_blacklist);
            if (it.value().contains("cpu_only_packages"))
                parse_string_array(it.value()["cpu_only_packages"], &out->cpu_only_packages);
            continue;
        }

        if (!starts_with(key, kPkgPrefix)) continue;

        // "PACKAGES_<KEY>" vs "PACKAGES_<KEY>_DEVICE"
        if (ends_with(key, kDeviceSuffix)) {
            std::string profile = key.substr(strlen(kPkgPrefix),
                                             key.size() - strlen(kPkgPrefix) - strlen(kDeviceSuffix));
            DeviceProfile dev;
            if (it.value().is_object()) {
                for (auto dit = it.value().begin(); dit != it.value().end(); ++dit) {
                    if (dit.value().is_string()) dev[dit.key()] = dit.value().get<std::string>();
                }
            }
            if (!dev.empty()) out->devices[profile] = std::move(dev);
            continue;
        }

        std::string profile = key.substr(strlen(kPkgPrefix));
        std::vector<PackageEntry> entries;
        if (it.value().is_array()) {
            for (const auto &v : it.value()) {
                if (v.is_string()) entries.push_back(parse_entry(v.get<std::string>()));
            }
        }
        out->profiles[profile] = std::move(entries);
        out->profile_order.push_back(profile);
    }

    return true;
}

bool load_config(const std::string &path, Config *out, std::string *error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_config(ss.str(), out, error);
}

}  // namespace copg

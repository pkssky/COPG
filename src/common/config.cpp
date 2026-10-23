// COPG open-source engine — COPG.json parser.
//
// Uses nlohmann/json with ordered semantics: the WebUI documents that key
// insertion order is meaningful, so we parse with ordered_json to preserve
// file order. PACKAGES_THISDEVICE is additionally scanned first (documented
// precedence over every other profile).
#include "config.hpp"
#include "paths.hpp"
#include "strings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "third_party/json.hpp"

namespace copg {
namespace {

using ordered_json = nlohmann::ordered_json;

// Upstream PRO / comfort tags this engine does NOT implement. They are parsed
// (and reported by `controller status`) so configs stay forward compatible and
// users get a clear signal about which spoofs won't take effect. See
// docs/OPEN_SOURCE_ENGINE.md for why each one needs more than in-process hooks.
const char *kUnsupportedTags[] = {
    "gaid", "appset", "drm", "imei", "sensor", "webfp", "tensorfeat",
    "proxy", "mock", "vpn", "vpns", "hidedev",          // PRO identity / network
    "dnd", "dab", "kso",                                 // controller comfort tweaks
};
const char *kUnsupportedValueTags[] = {                 // value-carrying variants
    "sim=", "simx=", "lang=", "ua=", "uptime=", "gps=", "dpi=",
};

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
        } else if (t == "serial") {
            entry.tags.serial = true;
        } else if (t == "nolog") {
            entry.tags.nolog = true;
        } else if (starts_with(t, "cpu=")) {
            entry.tags.cpu_key = t.substr(4);
        } else if (starts_with(t, "gpu=")) {
            entry.tags.gpu_key = t.substr(4);
        } else if (starts_with(t, "tz=")) {
            entry.tags.tz = t.substr(3);
        } else if (starts_with(t, "user=")) {
            int id = -1;
            if (std::sscanf(t.c_str() + 5, "%d", &id) == 1) entry.tags.user_id = id;
        } else {
            // Known-but-unimplemented upstream tag: keep for diagnostics.
            bool known = false;
            for (const char *u : kUnsupportedTags)
                if (t == u) { known = true; break; }
            if (!known) {
                for (const char *u : kUnsupportedValueTags)
                    if (starts_with(t, u)) { known = true; break; }
            }
            if (known) entry.tags.unsupported.push_back(t);
            // else: truly unknown tag — ignore for forward compatibility.
        }
    }

    // Legacy back-compat: a bare "with_cpu" (no cpu=<key>) means the default
    // profile — byte-identical to the WebUI's `form.cpuModel || 'sd8elite'`.
    if (entry.tags.with_cpu && entry.tags.cpu_key.empty())
        entry.tags.cpu_key = kDefaultCpuKey;
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

    // Scan order: PACKAGES_THISDEVICE first (documented precedence), then the
    // remaining profiles in JSON appearance order.
    std::vector<std::string> order;
    order.reserve(profile_order.size() + 1);
    if (profiles.count(kThisDevice)) order.push_back(kThisDevice);
    for (const auto &key : profile_order) {
        if (key != kThisDevice) order.push_back(key);
    }

    // `blocked` = force real CPU: any entry (other than one that also carries an
    // explicit cpu=<key> — the WebUI's "with_cpu wins if both") disables cpu
    // spoof for the package, regardless of what lower-precedence profiles say.
    bool force_real_cpu = cpu_blacklisted;

    for (const auto &key : order) {
        auto pit = profiles.find(key);
        if (pit == profiles.end()) continue;
        for (const auto &entry : pit->second) {
            if (entry.package != package) continue;

            // user= filter (extension): skip entries bound to another user.
            if (entry.tags.user_id >= 0 && entry.tags.user_id != user_id) continue;

            r.matched = true;

            // ── cpu aspect ──────────────────────────────────────────────
            if (entry.tags.blocked && entry.tags.cpu_key.empty()) {
                force_real_cpu = true;  // blocked without cpu= : real cpu wins
            } else if (r.cpu_key.empty() && !entry.tags.cpu_key.empty()) {
                r.cpu_key = entry.tags.cpu_key;
            }

            // ── other aspects: first supplier wins ──────────────────────
            if (r.gpu_key.empty() && !entry.tags.gpu_key.empty()) r.gpu_key = entry.tags.gpu_key;
            if (!r.aid && entry.tags.aid) r.aid = true;
            if (!r.serial && entry.tags.serial) r.serial = true;
            if (r.tz.empty() && !entry.tags.tz.empty()) r.tz = entry.tags.tz;
            if (!r.cow && entry.tags.cow) r.cow = true;
            if (!r.nolog && entry.tags.nolog) r.nolog = true;
            for (const auto &u : entry.tags.unsupported) {
                if (std::find(r.unsupported.begin(), r.unsupported.end(), u) == r.unsupported.end())
                    r.unsupported.push_back(u);
            }

            // Device fields: the first scanned profile that mentions the package
            // supplies them — including an empty _DEVICE (THISDEVICE's usual
            // case), which means "no device spoof" and blocks lower profiles.
            if (r.device == nullptr) {
                auto dit = devices.find(key);
                if (dit != devices.end()) r.device = &dit->second;
            }
        }
    }

    if (force_real_cpu) r.cpu_key.clear();
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
            // Stored even when empty: presence marks the profile as supplying
            // the device aspect (empty = explicitly no device spoof).
            out->devices[profile] = std::move(dev);
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

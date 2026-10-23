// COPG open-source engine — configuration model.
//
// COPG.json shape (shared contract with the WebUI, see webroot/js/copg-data.js):
//
//   "cpu_spoof":       { blacklist: [pkg...], cpu_only_packages: [pkg...] }  // global, optional
//   "PACKAGES_<KEY>":        [ "com.x:blocked", "com.y:cpu=sd8elite" ]      // a device's package list
//   "PACKAGES_<KEY>_DEVICE": { BRAND, DEVICE, MANUFACTURER, MODEL, FINGERPRINT,
//                              PRODUCT, SERIAL?, ANDROID_ID?, ... }         // Build fields
//
// Package tags are colon suffixes:
//   blocked            — module stays completely inert for this package (denylist)
//   with_cpu           — legacy alias of cpu=<default> (kept for compatibility)
//   cow                — copy-on-write props: a no-op here, this engine's hooks
//                        are process-local by design (nothing to restore)
//   aid                — derive a per-app ANDROID_ID from ANDROID_ID + pkg
//   cpu=<key>          — spoof /proc/cpuinfo from CPU/cpuinfo_<key>
//   gpu=<key>          — spoof GL vendor/renderer from GPU/manifest.json
//   user=<id>          — extension: only apply for this Android user id
// Unknown tags are ignored (forward compatible).
//
// Insertion order of keys is meaningful and preserved: the first profile that
// mentions a package wins for device fields / cpu / gpu.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace copg {

// Parsed tags of one package entry.
struct PackageTags {
    bool blocked = false;
    bool with_cpu = false;   // legacy: cpu=<default profile>
    bool cow = false;
    bool aid = false;
    std::string cpu_key;     // "sd8elite" (from cpu=)
    std::string gpu_key;     // "adreno830" (from gpu=)
    int user_id = -1;        // from user=<id>; -1 = any user
};

// One "com.pkg:tag:tag" entry.
struct PackageEntry {
    std::string package;
    PackageTags tags;
};

// A device profile: the _DEVICE map of one profile, raw field names.
using DeviceProfile = std::map<std::string, std::string>;

// Fully parsed COPG.json.
struct Config {
    // profile key -> package entries
    std::map<std::string, std::vector<PackageEntry>> profiles;
    // profile key -> device fields (BRAND/DEVICE/...)
    std::map<std::string, DeviceProfile> devices;
    // Profile keys in JSON appearance order ("first mention wins" relies on it).
    std::vector<std::string> profile_order;

    // Global cpu_spoof.blacklist: never spoof cpu for these packages.
    std::vector<std::string> cpu_blacklist;
    // Global cpu_spoof.cpu_only_packages: cpu spoof only, no device spoof.
    std::vector<std::string> cpu_only_packages;

    // Merge semantics for one package (see file header): first profile in
    // insertion order that mentions the package wins per aspect.
    struct Resolved {
        bool matched = false;      // package is known to the config
        bool blocked = false;      // denylist hit
        bool cpu_only = false;     // global cpu_only_packages hit
        const PackageTags *tags = nullptr;       // merged tags
        const DeviceProfile *device = nullptr;   // merged device fields
    };

    // `package` is the process name; `user_id` the Android user (uid / 100000).
    Resolved resolve(const std::string &package, int user_id) const;
};

// Parse COPG.json text. Returns false and fills `error` on malformed input.
bool parse_config(const std::string &text, Config *out, std::string *error);

// Read + parse the config file. `error` gets a human-readable reason on failure.
bool load_config(const std::string &path, Config *out, std::string *error);

}  // namespace copg

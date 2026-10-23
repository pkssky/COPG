// COPG open-source engine — configuration model.
//
// COPG.json shape (shared contract with the WebUI, see webroot/js/copg-data.js):
//
//   "cpu_spoof":       { blacklist: [pkg...], cpu_only_packages: [pkg...] }  // global, optional
//   "PACKAGES_<KEY>":        [ "com.x:blocked", "com.y:cpu=sd8elite" ]      // a device's package list
//   "PACKAGES_<KEY>_DEVICE": { BRAND, DEVICE, MANUFACTURER, MODEL, FINGERPRINT,
//                              PRODUCT, SERIAL?, ANDROID_ID?, ... }         // Build fields
//
// Package tags are colon suffixes. Full upstream set (see README "Package tags");
// what THIS engine implements is marked [✓] / [~] (derived) / [✗] (see docs):
//
//   blocked      [✓] force REAL cpu — strips the cpu aspect only (NOT "inert");
//                     README: ":blocked (force real CPU)", WebUI: "Block CPU Spoof".
//                     A package tagged blocked anywhere gets no cpu spoof at all
//                     (blocked wins over cpu= across profiles, mirroring the
//                     global cpu_spoof.blacklist).
//   with_cpu     [✓] legacy alias of cpu=<default>; never written by the current
//                     WebUI, only parsed for old configs (default = sd8elite).
//   cpu=<key>    [✓] spoof /proc/cpuinfo from CPU/cpuinfo_<key>
//   gpu=<key>    [✓] spoof GL vendor/renderer from GPU/manifest.json
//   cow          [~] prop spoof — no-op here: this engine's hooks are
//                     process-local by design (nothing to restore)
//   aid          [~] per-app ANDROID_ID derived from the ANDROID_ID seed +
//                     package hash (Settings.Secure itself is not hookable
//                     in-process; see docs/OPEN_SOURCE_ENGINE.md)
//   serial       [~] per-app ro.serialno derived from seed + package hash
//   tz=<zone>    [~] per-app timezone (persist.sys.timezone spoof)
//   user=<id>    [✓] extension: only apply for this Android user id
//   nolog        [✓] comfort toggle honoured here: quiet per-package logging
//   proxy        [✓] per-app SOCKS5 proxy: connect() redirected through the
//   proxy=<key>      PROXY/manifest.json endpoint (bare = "default")
//   uptime=<s>   [✓] fake device uptime: clock_gettime(CLOCK_MONOTONIC/
//                     BOOTTIME) shifted so the app sees <s> seconds since boot
//   sensor       [✓] sensor-roster spoof: hide/rename sensors per
//   sensor=<key>     SENSOR/manifest.json profile (bare = "default")
//   vpn / vpns   [✓] hide VPN interfaces (tun*/ppp*) from getifaddrs
//   hidedev      [✓] spoof debuggability props (ro.debuggable=0, user build, ...)
//   gaid / appset / drm / imei / sim=<c> / simx=<c> / lang=<bcp47> / ua=<p> /
//   webfp / tensorfeat / gps=<lat,lng> / mock / dpi=<n>
//                [✗] upstream PRO tags that need system-service or ART hooks;
//                     parsed and reported as unsupported
//   dnd / dab / kso
//                [✗] closed-source controller comfort tweaks (parsed, reported
//                     by `controller status` as unsupported)
// Unknown tags are ignored (forward compatible).
//
// Merge semantics: aspects (device fields, cpu, gpu, aid, serial, tz, proxy,
// sensor, uptime, vpn, hidedev) resolve independently — the first profile in
// scan order that supplies an aspect wins for it. Scan order is
// PACKAGES_THISDEVICE first (documented precedence), then the remaining
// profiles in JSON appearance order. An empty _DEVICE map in a scanned profile
// still supplies the device aspect (as "no device spoof"), so THISDEVICE's
// usually-empty _DEVICE correctly suppresses lower profiles.
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
    bool serial = false;     // per-app derived serial
    bool nolog = false;      // comfort toggle honoured here (quiet logging)
    bool vpn = false;        // ":vpn" / ":vpns" — hide VPN interfaces from the app
    bool hidedev = false;    // ":hidedev" — spoof debuggability properties
    std::string tz;          // ":tz=<zone>" — per-app timezone
    std::string cpu_key;     // "sd8elite" (from cpu=)
    std::string gpu_key;     // "adreno830" (from gpu=)
    std::string proxy_key;   // "default" (bare :proxy) or explicit key
    std::string sensor_key;  // "default" (bare :sensor) or explicit key
    long long uptime_sec = 0;  // ":uptime=<sec>" — fake seconds since boot
    int user_id = -1;        // from user=<id>; -1 = any user
    // Upstream tags this engine does not implement (diagnostics only).
    std::vector<std::string> unsupported;
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
    // profile key -> device fields (BRAND/DEVICE/...). Stored even when empty:
    // presence means "this profile supplies the device aspect" (empty = none).
    std::map<std::string, DeviceProfile> devices;
    // Profile keys in JSON appearance order.
    std::vector<std::string> profile_order;

    // Global cpu_spoof.blacklist: never spoof cpu for these packages.
    std::vector<std::string> cpu_blacklist;
    // Global cpu_spoof.cpu_only_packages: cpu spoof only, no device spoof.
    std::vector<std::string> cpu_only_packages;

    // Per-aspect merge result for one package (see file header).
    struct Resolved {
        bool matched = false;       // package is known to the config
        bool cpu_only = false;      // global cpu_only_packages hit
        bool cow = false;
        bool aid = false;
        bool serial = false;
        bool nolog = false;
        bool vpn = false;           // hide VPN interfaces
        bool hidedev = false;       // spoof debuggability props
        std::string tz;             // ":tz=<zone>"
        std::string cpu_key;        // "" = no cpu spoof
        std::string gpu_key;        // "" = no gpu spoof
        std::string proxy_key;      // "" = no proxy
        std::string sensor_key;     // "" = no sensor spoof
        long long uptime_sec = 0;   // 0 = no uptime spoof
        const DeviceProfile *device = nullptr;  // merged device fields (may be empty)
        // Upstream tags seen for this package that this engine does not implement.
        std::vector<std::string> unsupported;
    };

    // `package` is the process name; `user_id` the Android user (uid / 100000).
    Resolved resolve(const std::string &package, int user_id) const;
};

// Parse COPG.json text. Returns false and fills `error` on malformed input.
bool parse_config(const std::string &text, Config *out, std::string *error);

// Read + parse the config file. `error` gets a human-readable reason on failure.
bool load_config(const std::string &path, Config *out, std::string *error);

}  // namespace copg

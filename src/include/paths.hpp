// COPG open-source engine — well-known paths and constants.
// Keep in sync with module/customize.sh (install layout) and module/service.sh.
#pragma once

namespace copg {

// Module install directory (Magisk / KernelSU / APatch all use this).
constexpr const char *kModuleDir   = "/data/adb/modules/COPG";
// User config: package lists + device profiles (written by the WebUI).
constexpr const char *kConfigPath  = "/data/adb/modules/COPG/COPG.json";
// Human-readable package names for the WebUI.
constexpr const char *kListPath    = "/data/adb/modules/COPG/list.json";
// Per-app CPU profiles: CPU/cpuinfo_<key> + CPU/manifest.json
constexpr const char *kCpuDir      = "/data/adb/modules/COPG/CPU";
constexpr const char *kCpuManifest = "/data/adb/modules/COPG/CPU/manifest.json";
// GPU spoof table: GPU/manifest.json
constexpr const char *kGpuManifest = "/data/adb/modules/COPG/GPU/manifest.json";
// Runtime state dir owned by the controller daemon.
constexpr const char *kStateDir    = "/data/adb/copg_state";

// The active device slot. Entries in PACKAGES_THISDEVICE take precedence over
// every other profile; its _DEVICE map is usually empty (= no device spoof).
constexpr const char *kThisDevice  = "THISDEVICE";

// Key prefixes inside COPG.json.
constexpr const char *kPkgPrefix    = "PACKAGES_";
constexpr const char *kDeviceSuffix = "_DEVICE";

}  // namespace copg

// COPG open-source engine — sensor-roster spoof table (SENSOR/manifest.json).
//
//   [ { "key": "default",
//       "name": "Google Pixel 8 Pro",
//       "hidden": [19, 34],
//       "rename": { "1": { "name": "...", "vendor": "...",
//                          "string_type": "android.sensor.accelerometer" } } }, ... ]
//
//   key         — ":sensor" (bare) uses "default"; ":sensor=<key>" picks one
//   hidden      — sensor TYPE values (android/sensor.h ASENSOR_TYPE_*) removed
//                 from the roster the app sees
//   rename      — per-TYPE string overrides for ASensor_getName / getVendor /
//                 getStringType (copy the values from the device you are
//                 impersonating, e.g. via `dumpsys sensorservice` there)
#pragma once

#include <map>
#include <string>
#include <vector>

namespace copg {

struct SensorOverride {
    std::string name;
    std::string vendor;
    std::string string_type;
};

struct SensorEntry {
    std::string key;
    std::string name;               // informational only
    std::vector<int> hidden;        // ASENSOR_TYPE_* values to hide
    std::map<int, SensorOverride> rename;  // per-type string overrides
};

// Look up a sensor profile by key. Returns nullptr if absent.
// `manifest_path` is SENSOR/manifest.json; an empty/missing file yields nullptr.
const SensorEntry *sensor_lookup(const std::string &key, const std::string &manifest_path);

}  // namespace copg

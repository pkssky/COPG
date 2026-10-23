// COPG open-source engine — sensor-roster spoofing hooks (:sensor).
//
// Arms a profile from SENSOR/manifest.json (see sensor.hpp) and rewrites the
// sensor roster the app sees: hidden types disappear from the list,
// renamed sensors report the profile's name/vendor/string_type.
//
// Covers both NDK apps (direct ASensorManager calls) and normal Java apps —
// android_hardware_SensorManager's list query goes through the same
// ASensorManager_getSensorList PLT entry, which is what we hook.
#pragma once

#include <string>

#include "zygisk.hpp"

namespace copg {
namespace sensor {

// Arm the profile for `key` ("default" or an explicit SENSOR/manifest.json
// key). Returns false when the key is absent.
bool set_key(const std::string &key);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace sensor
}  // namespace copg

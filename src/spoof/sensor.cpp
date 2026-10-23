// COPG open-source engine — sensor-roster spoofing implementation.
#include "log.hpp"
#include "paths.hpp"
#include "sensor.hpp"
#include "sensor_spoof.hpp"

#include <android/sensor.h>

#include <map>
#include <mutex>
#include <vector>

namespace copg {
namespace sensor {
namespace {

// NDK sensor API entry points (all PLT-hooked; see sensor_spoof.hpp).
using get_sensor_list_fn = int (*)(ASensorManager *, ASensorList *);
using get_default_sensor_fn = const ASensor *(*)(ASensorManager *, int);
using get_name_fn = const char *(*)(const ASensor *);
using get_vendor_fn = const char *(*)(const ASensor *);
using get_string_type_fn = const char *(*)(const ASensor *);
using get_type_fn = int (*)(const ASensor *);

get_sensor_list_fn g_real_get_sensor_list = nullptr;
get_default_sensor_fn g_real_get_default_sensor = nullptr;
get_name_fn g_real_get_name = nullptr;
get_vendor_fn g_real_get_vendor = nullptr;
get_string_type_fn g_real_get_string_type = nullptr;
get_type_fn g_real_get_type = nullptr;

bool g_armed = false;
SensorEntry g_profile;

std::mutex g_mu;
// Rebuilt on every getSensorList call (idempotent — the manager returns the
// same stable ASensor pointers for the process lifetime).
std::vector<const ASensor *> g_filtered;
std::map<const ASensor *, SensorOverride> g_renames;

bool type_hidden(int type) {
    for (int h : g_profile.hidden) {
        if (h == type) return true;
    }
    return false;
}

const SensorOverride *override_for(const ASensor *s) {
    auto it = g_renames.find(s);
    return it == g_renames.end() ? nullptr : &it->second;
}

int hook_get_type(const ASensor *sensor) {
    if (!g_real_get_type) return -1;  // ASENSOR_TYPE_INVALID? keep -1 (unknown)
    return g_real_get_type(sensor);
}

int hook_get_sensor_list(ASensorManager *manager, ASensorList *list) {
    if (!g_real_get_sensor_list) return 0;
    int n = g_real_get_sensor_list(manager, list);
    if (n <= 0 || !list || !g_armed) return n;

    const ASensor *const *real = *list;
    std::lock_guard<std::mutex> lock(g_mu);
    g_filtered.clear();
    g_renames.clear();
    for (int i = 0; i < n; ++i) {
        const ASensor *s = real[i];
        if (!s) continue;
        int type = g_real_get_type ? g_real_get_type(s) : -1;
        if (type_hidden(type)) continue;
        auto rit = g_profile.rename.find(type);
        if (rit != g_profile.rename.end()) g_renames[s] = rit->second;
        g_filtered.push_back(s);
    }
    // g_filtered's storage is stable until the next call; *list points into it.
    *list = g_filtered.data();
    return static_cast<int>(g_filtered.size());
}

const ASensor *hook_get_default_sensor(ASensorManager *manager, int type) {
    if (g_armed && type_hidden(type)) return nullptr;
    if (!g_real_get_default_sensor) return nullptr;
    return g_real_get_default_sensor(manager, type);
}

const char *hook_get_name(const ASensor *sensor) {
    if (g_armed && sensor) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (const SensorOverride *ov = override_for(sensor)) {
            if (!ov->name.empty()) return ov->name.c_str();
        }
    }
    return g_real_get_name ? g_real_get_name(sensor) : nullptr;
}

const char *hook_get_vendor(const ASensor *sensor) {
    if (g_armed && sensor) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (const SensorOverride *ov = override_for(sensor)) {
            if (!ov->vendor.empty()) return ov->vendor.c_str();
        }
    }
    return g_real_get_vendor ? g_real_get_vendor(sensor) : nullptr;
}

const char *hook_get_string_type(const ASensor *sensor) {
    if (g_armed && sensor) {
        std::lock_guard<std::mutex> lock(g_mu);
        if (const SensorOverride *ov = override_for(sensor)) {
            if (!ov->string_type.empty()) return ov->string_type.c_str();
        }
    }
    return g_real_get_string_type ? g_real_get_string_type(sensor) : nullptr;
}

}  // namespace

bool set_key(const std::string &key) {
    const SensorEntry *e = sensor_lookup(key, kSensorManifest);
    if (!e) {
        COPG_LOGW("sensor: key '%s' not found in %s", key.c_str(), kSensorManifest);
        return false;
    }
    g_profile = *e;
    g_armed = true;
    COPG_LOGI("sensor: profile armed: %s (%zu hidden, %zu renamed)",
              g_profile.key.c_str(), g_profile.hidden.size(), g_profile.rename.size());
    return true;
}

void register_hooks(zygisk::Api *api) {
    if (!g_armed) return;
    api->pltHookRegister(".*", "ASensorManager_getSensorList",
                         reinterpret_cast<void *>(hook_get_sensor_list),
                         reinterpret_cast<void **>(&g_real_get_sensor_list));
    api->pltHookRegister(".*", "ASensorManager_getDefaultSensor",
                         reinterpret_cast<void *>(hook_get_default_sensor),
                         reinterpret_cast<void **>(&g_real_get_default_sensor));
    api->pltHookRegister(".*", "ASensor_getName",
                         reinterpret_cast<void *>(hook_get_name),
                         reinterpret_cast<void **>(&g_real_get_name));
    api->pltHookRegister(".*", "ASensor_getVendor",
                         reinterpret_cast<void *>(hook_get_vendor),
                         reinterpret_cast<void **>(&g_real_get_vendor));
    api->pltHookRegister(".*", "ASensor_getStringType",
                         reinterpret_cast<void *>(hook_get_string_type),
                         reinterpret_cast<void **>(&g_real_get_string_type));
    // Forwarder only: gives us the real ASensor_getType to classify the roster.
    api->pltHookRegister(".*", "ASensor_getType",
                         reinterpret_cast<void *>(hook_get_type),
                         reinterpret_cast<void **>(&g_real_get_type));
}

}  // namespace sensor
}  // namespace copg

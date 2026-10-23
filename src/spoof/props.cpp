// COPG open-source engine — system property spoofing.
#include "props.hpp"
#include "log.hpp"
#include "strings.hpp"

#include <cstring>
#include <pthread.h>

namespace copg {
namespace props {
namespace {

// bionic's prop_info is opaque in public headers; the hooks never dereference
// it (we only forward it to the real implementation).
using prop_info_t = const void;

using read_callback_fn = void (*)(void *cookie, const char *name,
                                  const char *value, unsigned int serial);
using read_callback_entry = int (*)(prop_info_t *, read_callback_fn, void *cookie);
using property_get_fn = int (*)(const char *name, char *value);

read_callback_entry g_real_read_callback = nullptr;
property_get_fn g_real_property_get = nullptr;

// name -> spoofed value. Built once per process in set_state().
std::map<std::string, std::string> g_spoof;

// Field name (as written in _DEVICE) -> system properties it feeds.
struct FieldMap {
    const char *field;
    std::vector<const char *> props;
};

const FieldMap kFieldMap[] = {
    {"BRAND",         {"ro.product.brand", "ro.product.vendor.brand",
                       "ro.product.system.brand", "ro.product.odm.brand"}},
    {"DEVICE",        {"ro.product.device", "ro.product.vendor.device",
                       "ro.product.system.device", "ro.product.odm.device"}},
    {"MANUFACTURER",  {"ro.product.manufacturer", "ro.product.vendor.manufacturer",
                       "ro.product.system.manufacturer", "ro.product.odm.manufacturer"}},
    {"MODEL",         {"ro.product.model", "ro.product.vendor.model",
                       "ro.product.system.model", "ro.product.odm.model"}},
    {"PRODUCT",       {"ro.product.name", "ro.product.product.name",
                       "ro.build.product"}},
    {"FINGERPRINT",   {"ro.build.fingerprint", "ro.vendor.build.fingerprint",
                       "ro.system.build.fingerprint", "ro.odm.build.fingerprint"}},
    {"SERIAL",        {"ro.serialno", "ro.boot.serialno"}},
    {"ANDROID_VERSION", {"ro.build.version.release"}},
    {"SDK_INT",       {"ro.build.version.sdk"}},
    {"BOARD",         {"ro.product.board"}},
    {"HARDWARE",      {"ro.hardware"}},
    {"DISPLAY",       {"ro.build.display.id"}},
    {"ID",            {"ro.build.id"}},
    {"BOOTLOADER",    {"ro.bootloader"}},
    {"TAGS",          {"ro.build.tags"}},
    {"TYPE",          {"ro.build.type"}},
    {"SECURITY_PATCH", {"ro.build.version.security_patch"}},
    {"INCREMENTAL",   {"ro.build.version.incremental"}},
    {"CODENAME",      {"ro.build.version.codename"}},
    {"SOC_MANUFACTURER", {"ro.soc.manufacturer"}},
    {"SOC_MODEL",     {"ro.soc.model"}},
};

// Caller's callback for the in-flight __system_property_read_callback call.
// thread_local: the real implementation never re-enters on the same thread.
thread_local read_callback_fn t_caller_cb = nullptr;

int hook_read_callback(prop_info_t *pi, read_callback_fn cb, void *cookie) {
    t_caller_cb = cb;
    auto forward = [](void *c, const char *name, const char *value, unsigned int serial) {
        const char *sub = lookup(name);
        t_caller_cb(c, name, sub ? sub : value, serial);
    };
    int rc = g_real_read_callback(pi, forward, cookie);
    t_caller_cb = nullptr;
    return rc;
}

int hook_property_get(const char *name, char *value) {
    const char *sub = lookup(name);
    if (sub) {
        // bionic copies at most PROP_VALUE_MAX (92) bytes incl. NUL.
        size_t len = strlen(sub);
        if (len > 91) len = 91;
        memcpy(value, sub, len);
        value[len] = '\0';
        return static_cast<int>(len);
    }
    return g_real_property_get(name, value);
}

}  // namespace

const char *lookup(const char *name) {
    if (!name) return nullptr;
    auto it = g_spoof.find(name);
    return it == g_spoof.end() ? nullptr : it->second.c_str();
}

void set_state(const DeviceProfile *device, const std::string &android_id) {
    g_spoof.clear();
    if (device) {
        for (const auto &field : *device) {
            for (const auto &fm : kFieldMap) {
                if (field.first != fm.field) continue;
                for (const char *prop : fm.props) g_spoof[prop] = field.second;
            }
        }
    }
    if (!android_id.empty()) {
        // Derived per-app ANDROID_ID. Settings.Secure.ANDROID_ID itself is not
        // a system property (it lives in the settings DB), so it cannot be
        // spoofed with PLT hooks alone; we expose the derived value for
        // native readers and log it for debugging.
        g_spoof["persist.sys.copg.aid"] = android_id;
    }
    COPG_LOGI("props: %zu spoofed properties armed", g_spoof.size());
}

void register_hooks(zygisk::Api *api) {
    if (g_spoof.empty()) return;
    api->pltHookRegister(".*", "__system_property_read_callback",
                         reinterpret_cast<void *>(hook_read_callback),
                         reinterpret_cast<void **>(&g_real_read_callback));
    api->pltHookRegister(".*", "__system_property_get",
                         reinterpret_cast<void *>(hook_property_get),
                         reinterpret_cast<void **>(&g_real_property_get));
}

}  // namespace props
}  // namespace copg

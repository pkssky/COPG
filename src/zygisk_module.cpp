// COPG open-source engine — Zygisk module entry point.
//
// Loaded into every app process (via zygisk/<abi>.so). For processes whose
// package is configured in COPG.json, PLT hooks are installed in
// preAppSpecialize (zygote privilege, before any app code runs):
//
//   _DEVICE fields  ->  props::  (__system_property_* hooks)
//   :aid            ->  props::  (derived per-app ANDROID_ID, debug-exposed)
//   :serial         ->  props::  (derived per-app ro.serialno)
//   :tz=<zone>      ->  props::  (per-app persist.sys.timezone)
//   :hidedev        ->  props::  (non-debuggable user-build props)
//   cpu=<key>       ->  cpu::    (fopen/open redirect to CPU/cpuinfo_<key>)
//   gpu=<key>       ->  gpu::    (glGetString vendor/renderer)
//   :proxy          ->  proxy::  (connect() through a SOCKS5 endpoint)
//   :uptime=<s>     ->  uptime:: (clock_gettime shifted to a fake boot time)
//   :sensor         ->  sensor:: (sensor roster hidden/renamed)
//   :vpn / :vpns    ->  vpn::    (tun*/ppp* interfaces hidden)
//
// Packages absent from the config — or tagged :blocked / :nolog where
// applicable — get no hooks (or no logs), so the module stays invisible to
// everything the user did not opt in.
#include "config.hpp"
#include "cpu.hpp"
#include "gpu_spoof.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "props.hpp"
#include "proxy_spoof.hpp"
#include "sensor_spoof.hpp"
#include "strings.hpp"
#include "uptime.hpp"
#include "vpn.hpp"
#include "zygisk.hpp"

#include <jni.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace {

copg::Config g_config;
bool g_config_ready = false;

// Stable per-app value derivation: FNV-1a 64 over "<domain>:<seed>:<package>",
// rendered as 16 lowercase hex chars. Same inputs always produce the same
// value; different packages (or domains) get different values.
std::string derive_hex(const char *domain, const std::string &seed,
                       const std::string &package) {
    unsigned long long h = 1469598103934665603ULL;  // FNV-1a 64 offset basis
    std::string s = std::string(domain) + ":" + seed + ":" + package;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV-1a 64 prime
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", h);
    return std::string(buf, 16);
}

std::string derive_android_id(const std::string &seed, const std::string &package) {
    return derive_hex("aid", seed, package);
}

std::string derive_serial(const std::string &seed, const std::string &package) {
    return derive_hex("serial", seed, package);
}

class COPGModule : public zygisk::ModuleBase {
  public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        // Store the JavaVM, NOT the JNIEnv: onLoad runs on the zygote thread that
        // loads us, but preAppSpecialize runs on the forked child's main thread.
        // A JNIEnv is thread-bound and must not be reused across the fork — we
        // re-derive a per-thread env in preAppSpecialize via GetEnv().
        vm_ = (env && env->GetJavaVM(&vm_) == JNI_OK) ? vm_ : nullptr;
        COPG_LOGI("COPG engine loaded (api v%d)", ZYGISK_API_VERSION);
    }

    // Apps run preAppSpecialize; system_server uses the Server variants and
    // must never be hooked (it would spoof the whole system).
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (!api_ || !vm_ || !args) return;

        // Derive THIS thread's JNIEnv (the forked child's main thread). The zygote
        // thread that called onLoad is gone; reusing its env would be UB.
        JNIEnv *env = nullptr;
        if (vm_->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
            return;
        if (!args->nice_name) return;

        const char *raw = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw) return;
        std::string process(raw);
        env->ReleaseStringUTFChars(args->nice_name, raw);

        // "com.pkg:push" style subprocesses share the app's config entry.
        std::string package = process.substr(0, process.find(':'));
        int user_id = static_cast<int>(args->uid) / 100000;

        static std::once_flag once;
        std::call_once(once, [] {
            std::string err;
            g_config_ready = copg::load_config(copg::kConfigPath, &g_config, &err);
            if (!g_config_ready) COPG_LOGE("config load failed: %s", err.c_str());
        });
        if (!g_config_ready) return;

        auto r = g_config.resolve(package, user_id);
        if (!r.matched) {
            COPG_LOGD("%s: not configured, staying inert", package.c_str());
            return;
        }

        // :nolog — controller comfort toggle honoured here: no per-package INFO
        // logs for this app (warnings and errors still print).
        bool quiet = r.nolog;
        for (const auto &u : r.unsupported)
            COPG_LOGW("%s: tag ':%s' is not implemented by the open-source engine",
                      package.c_str(), u.c_str());

        bool hooked = false;

        // ── Device props (:aid / :serial / :tz / :hidedev feed one table) ───
        const copg::DeviceProfile *dev = r.cpu_only ? nullptr : r.device;
        bool needs_props = (dev && !dev->empty()) || r.aid || r.serial ||
                           !r.tz.empty() || r.hidedev;
        if (needs_props) {
            // _DEVICE's ANDROID_ID field doubles as the derivation seed.
            std::string seed = dev && dev->count("ANDROID_ID") ? dev->at("ANDROID_ID") : "";
            std::string aid, serial;
            if (r.aid) aid = derive_android_id(seed, package);
            // A static SERIAL field wins over the derived one.
            if (r.serial && !(dev && dev->count("SERIAL")))
                serial = derive_serial(seed, package);
            copg::props::set_state(dev, aid, serial, r.tz, r.hidedev);
            copg::props::register_hooks(api_);
            hooked = true;
            if (!quiet) {
                if (!aid.empty()) COPG_LOGI("%s: derived ANDROID_ID %s", package.c_str(), aid.c_str());
                if (!serial.empty()) COPG_LOGI("%s: derived serial %s", package.c_str(), serial.c_str());
                if (!r.tz.empty()) COPG_LOGI("%s: timezone -> %s", package.c_str(), r.tz.c_str());
                if (r.hidedev) COPG_LOGI("%s: developer-options props hidden", package.c_str());
            }
        }

        // ── CPU profile (cpu=<key>; :blocked / blacklist already stripped) ──
        if (!r.cpu_key.empty()) {
            if (copg::cpu::set_profile(r.cpu_key)) {
                copg::cpu::register_hooks(api_);
                hooked = true;
            }
        }

        // ── GPU identity ────────────────────────────────────────────────
        if (!r.gpu_key.empty()) {
            if (copg::gpu::set_key(r.gpu_key)) {
                copg::gpu::register_hooks(api_);
                hooked = true;
            }
        }

        // ── Per-app proxy (:proxy / :proxy=<key>) ───────────────────────
        if (!r.proxy_key.empty()) {
            if (copg::proxy::set_endpoint(r.proxy_key)) {
                copg::proxy::register_hooks(api_);
                hooked = true;
                if (!quiet) COPG_LOGI("%s: traffic via SOCKS5 '%s'", package.c_str(),
                                      r.proxy_key.c_str());
            }
        }

        // ── Fake uptime (:uptime=<sec>) ─────────────────────────────────
        if (r.uptime_sec > 0) {
            copg::uptime::set_offset(r.uptime_sec);
            copg::uptime::register_hooks(api_);
            hooked = true;
        }

        // ── Sensor roster (:sensor / :sensor=<key>) ─────────────────────
        if (!r.sensor_key.empty()) {
            if (copg::sensor::set_key(r.sensor_key)) {
                copg::sensor::register_hooks(api_);
                hooked = true;
                if (!quiet) COPG_LOGI("%s: sensor roster '%s'", package.c_str(),
                                      r.sensor_key.c_str());
            }
        }

        // ── VPN hiding (:vpn / :vpns) ───────────────────────────────────
        if (r.vpn) {
            copg::vpn::register_hooks(api_);
            hooked = true;
            if (!quiet) COPG_LOGI("%s: VPN interfaces hidden", package.c_str());
        }

        if (hooked) {
            // pltHookCommit must always run when hooks were registered; :nolog
            // only silences the per-package log line.
            bool ok = api_->pltHookCommit();
            if (!quiet)
                COPG_LOGI("%s (user %d): hooks committed=%d cpu=%s gpu=%s proxy=%s "
                          "sensor=%s uptime=%lld vpn=%d",
                          package.c_str(), user_id, ok ? 1 : 0,
                          r.cpu_key.empty() ? "-" : r.cpu_key.c_str(),
                          r.gpu_key.empty() ? "-" : r.gpu_key.c_str(),
                          r.proxy_key.empty() ? "-" : r.proxy_key.c_str(),
                          r.sensor_key.empty() ? "-" : r.sensor_key.c_str(),
                          r.uptime_sec, r.vpn ? 1 : 0);
        }
    }

  private:
    zygisk::Api *api_ = nullptr;
    JavaVM *vm_ = nullptr;   // set once in onLoad; per-thread JNIEnv derived in preAppSpecialize
};

}  // namespace

REGISTER_ZYGISK_MODULE(COPGModule)

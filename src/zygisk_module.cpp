// COPG open-source engine — Zygisk module entry point.
//
// Loaded into every app process (via zygisk/<abi>.so). For processes whose
// package is configured in COPG.json, PLT hooks are installed in
// preAppSpecialize (zygote privilege, before any app code runs):
//
//   _DEVICE fields  ->  props::  (__system_property_* hooks)
//   cpu=<key>       ->  cpu::    (fopen/open redirect to CPU/cpuinfo_<key>)
//   gpu=<key>       ->  gpu::    (glGetString vendor/renderer)
//
// Packages tagged "blocked" (or absent from the config) get no hooks at all,
// which also means the module is invisible to them.
#include "config.hpp"
#include "cpu.hpp"
#include "gpu_spoof.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "props.hpp"
#include "strings.hpp"
#include "zygisk.hpp"

#include <jni.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace {

copg::Config g_config;
bool g_config_ready = false;

// Derive a stable per-app ANDROID_ID (16 hex chars) from the profile seed and
// the package name: FNV-1a 64 over "<seed>:<package>". Same inputs always
// produce the same id, different packages get different ids.
std::string derive_android_id(const std::string &seed, const std::string &package) {
    unsigned long long h = 1469598103934665603ULL;  // FNV-1a 64 offset basis
    std::string s = seed + ":" + package;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV-1a 64 prime
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", h);
    return std::string(buf, 16);
}

class COPGModule : public zygisk::ModuleBase {
  public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
        COPG_LOGI("COPG engine loaded (api v%d)", ZYGISK_API_VERSION);
    }

    // Apps run preAppSpecialize; system_server uses the Server variants and
    // must never be hooked (it would spoof the whole system).
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (!api_ || !env_) return;

        const char *raw = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!raw) return;
        std::string process(raw);
        env_->ReleaseStringUTFChars(args->nice_name, raw);

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
        if (r.blocked) {
            COPG_LOGI("%s: blocked, staying inert", package.c_str());
            return;
        }

        bool hooked = false;

        // ── Device props ────────────────────────────────────────────────
        const copg::DeviceProfile *dev = r.cpu_only ? nullptr : r.device;
        if ((dev && !dev->empty()) || (r.tags && r.tags->aid)) {
            std::string aid;
            if (r.tags && r.tags->aid) {
                std::string seed = dev && dev->count("ANDROID_ID") ? dev->at("ANDROID_ID") : "";
                aid = derive_android_id(seed, package);
                COPG_LOGI("%s: derived ANDROID_ID %s", package.c_str(), aid.c_str());
            }
            copg::props::set_state(dev, aid);
            copg::props::register_hooks(api_);
            hooked = true;
        }

        // ── CPU profile ─────────────────────────────────────────────────
        if (r.tags && !r.tags->cpu_key.empty()) {
            if (copg::cpu::set_profile(r.tags->cpu_key)) {
                copg::cpu::register_hooks(api_);
                hooked = true;
            }
        }

        // ── GPU identity ────────────────────────────────────────────────
        if (r.tags && !r.tags->gpu_key.empty()) {
            if (copg::gpu::set_key(r.tags->gpu_key)) {
                copg::gpu::register_hooks(api_);
                hooked = true;
            }
        }

        if (hooked) {
            bool ok = api_->pltHookCommit();
            COPG_LOGI("%s (user %d): hooks committed=%d", package.c_str(), user_id,
                      ok ? 1 : 0);
        }
    }

  private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
};

}  // namespace

REGISTER_ZYGISK_MODULE(COPGModule)

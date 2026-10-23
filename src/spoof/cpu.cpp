// COPG open-source engine — /proc/cpuinfo spoofing.
#include "cpu.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "strings.hpp"

#include <fcntl.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

namespace copg {
namespace cpu {
namespace {

using fopen_fn = FILE *(*)(const char *, const char *);
using open_fn = int (*)(const char *, int, ...);
using openat_fn = int (*)(int, const char *, int, ...);

fopen_fn g_real_fopen = nullptr;
fopen_fn g_real_fopen64 = nullptr;
open_fn g_real_open = nullptr;
openat_fn g_real_openat = nullptr;

// Absolute path of the armed cpuinfo profile file ("" = not armed).
std::string g_profile_path;

// True when `path` is a read of the real cpuinfo.
bool is_cpuinfo(const char *path) {
    if (!path) return false;
    return strcmp(path, "/proc/cpuinfo") == 0 || strcmp(path, "/proc/self/cpuinfo") == 0;
}

FILE *hook_fopen(const char *path, const char *mode) {
    if (is_cpuinfo(path)) {
        COPG_LOGD("cpu: fopen(%s) -> %s", path, g_profile_path.c_str());
        return g_real_fopen(g_profile_path.c_str(), mode);
    }
    return g_real_fopen(path, mode);
}

FILE *hook_fopen64(const char *path, const char *mode) {
    if (is_cpuinfo(path)) return g_real_fopen64(g_profile_path.c_str(), mode);
    return g_real_fopen64(path, mode);
}

int hook_open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    if (is_cpuinfo(path)) {
        COPG_LOGD("cpu: open(%s) -> %s", path, g_profile_path.c_str());
        return g_real_open(g_profile_path.c_str(), flags, mode);
    }
    return g_real_open(path, flags, mode);
}

int hook_openat(int dirfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    // Only absolute paths can be /proc/cpuinfo; relative paths never are.
    if (is_cpuinfo(path)) {
        COPG_LOGD("cpu: openat(%s) -> %s", path, g_profile_path.c_str());
        return g_real_openat(dirfd, g_profile_path.c_str(), flags, mode);
    }
    return g_real_openat(dirfd, path, flags, mode);
}

}  // namespace

bool set_profile(const std::string &key) {
    std::string path = std::string(kCpuDir) + "/cpuinfo_" + key;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        COPG_LOGW("cpu: profile file missing: %s", path.c_str());
        return false;
    }
    g_profile_path = std::move(path);
    COPG_LOGI("cpu: profile armed: %s", g_profile_path.c_str());
    return true;
}

void register_hooks(zygisk::Api *api) {
    if (g_profile_path.empty()) return;
    api->pltHookRegister(".*", "fopen", reinterpret_cast<void *>(hook_fopen),
                         reinterpret_cast<void **>(&g_real_fopen));
    api->pltHookRegister(".*", "fopen64", reinterpret_cast<void *>(hook_fopen64),
                         reinterpret_cast<void **>(&g_real_fopen64));
    api->pltHookRegister(".*", "open", reinterpret_cast<void *>(hook_open),
                         reinterpret_cast<void **>(&g_real_open));
    api->pltHookRegister(".*", "openat", reinterpret_cast<void *>(hook_openat),
                         reinterpret_cast<void **>(&g_real_openat));
}

}  // namespace cpu
}  // namespace copg

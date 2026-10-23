// COPG open-source engine — fake uptime implementation.
#include "uptime.hpp"
#include "log.hpp"

#include <time.h>

#include <cerrno>
#include <cstdint>

namespace copg {
namespace uptime {
namespace {

using clock_gettime_fn = int (*)(clockid_t, struct timespec *);

// ABI-compatible stand-in for bionic's timespec64 (tv_sec is always 8 bytes
// under _TIME_BITS=64, tv_nsec is `long` on both ABIs). Declaring our own
// keeps this compiling on NDKs whose headers do not export the time64 types,
// while matching the real clock_gettime64 signature if the symbol exists.
struct timespec64_compat {
    long long tv_sec;
    long tv_nsec;
};
using clock_gettime64_fn = int (*)(clockid_t, struct timespec64_compat *);

clock_gettime_fn g_real_clock_gettime = nullptr;
clock_gettime64_fn g_real_clock_gettime64 = nullptr;

bool g_armed = false;
int64_t g_offset_ns = 0;   // now - fake_boot_time, subtracted on every read

// Only the monotonic family is shifted (wall clock must stay truthful).
bool is_shifted(clockid_t clk) {
    switch (clk) {
        case CLOCK_MONOTONIC:
        case CLOCK_BOOTTIME:
#if defined(CLOCK_MONOTONIC_RAW)
        case CLOCK_MONOTONIC_RAW:
#endif
#if defined(CLOCK_BOOTTIME_ALARM)
        case CLOCK_BOOTTIME_ALARM:
#endif
            return true;
        default:
            return false;
    }
}

void shift_timespec(struct timespec *ts) {
    if (!ts) return;
    int64_t ns = static_cast<int64_t>(ts->tv_sec) * 1000000000LL + ts->tv_nsec;
    ns -= g_offset_ns;
    if (ns < 0) ns = 0;   // never report a negative uptime
    ts->tv_sec = static_cast<time_t>(ns / 1000000000LL);
    ts->tv_nsec = static_cast<long>(ns % 1000000000LL);
}

void shift_timespec64(struct timespec64_compat *ts) {
    if (!ts) return;
    int64_t ns = static_cast<int64_t>(ts->tv_sec) * 1000000000LL + ts->tv_nsec;
    ns -= g_offset_ns;
    if (ns < 0) ns = 0;
    ts->tv_sec = static_cast<long long>(ns / 1000000000LL);
    ts->tv_nsec = static_cast<long>(ns % 1000000000LL);
}

int hook_clock_gettime(clockid_t clk, struct timespec *tp) {
    if (!g_real_clock_gettime) return -1;
    int rc = g_real_clock_gettime(clk, tp);
    if (rc == 0 && g_armed && tp && is_shifted(clk)) shift_timespec(tp);
    return rc;
}

int hook_clock_gettime64(clockid_t clk, struct timespec64_compat *tp) {
    if (!g_real_clock_gettime64) return -1;
    int rc = g_real_clock_gettime64(clk, tp);
    if (rc == 0 && g_armed && tp && is_shifted(clk)) shift_timespec64(tp);
    return rc;
}

}  // namespace

void set_offset(long long sec) {
    if (sec <= 0) return;
    // Sample the SAME clock family we shift. All monotonic clocks (MONOTONIC /
    // BOOTTIME / RAW) share the boot epoch, so MONOTONIC is the correct base.
    // (Sampling CLOCK_REALTIME here was a bug: the epoch-based wall clock made
    // the offset astronomically large — apps saw ~55 years of uptime.)
    // Runs before the hooks are registered, so this libc call cannot recurse.
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t mono_ns = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    g_offset_ns = mono_ns - static_cast<int64_t>(sec) * 1000000000LL;
    g_armed = true;
    COPG_LOGI("uptime: app will see %lld seconds since boot", sec);
}

void register_hooks(zygisk::Api *api) {
    if (!g_armed) return;
    api->pltHookRegister(".*", "clock_gettime",
                         reinterpret_cast<void *>(hook_clock_gettime),
                         reinterpret_cast<void **>(&g_real_clock_gettime));
    // 32-bit time_t variant (bionic exposes it as a separate symbol; the
    // registration is a harmless no-op where it does not exist).
    api->pltHookRegister(".*", "clock_gettime64",
                         reinterpret_cast<void *>(hook_clock_gettime64),
                         reinterpret_cast<void **>(&g_real_clock_gettime64));
}

}  // namespace uptime
}  // namespace copg

// COPG open-source engine — fake device uptime (:uptime=<sec>).
//
// Shifts the monotonic clocks so the app sees the device as if it had booted
// <sec> seconds ago. SystemClock.uptimeMillis() (CLOCK_MONOTONIC),
// elapsedRealtime() (CLOCK_BOOTTIME) and NDK clock_gettime() calls all funnel
// through the hooked PLT entries, so a single offset covers Java and native
// readers alike.
//
// CLOCK_REALTIME (wall clock) is never touched. The offset preserves all
// intervals — only the origin moves — so timers, animations and timeouts keep
// working; they just measure from a fake boot time.
#pragma once

#include "zygisk.hpp"

namespace copg {
namespace uptime {

// Arm the spoof: the app will see `sec` seconds since boot from now on.
void set_offset(long long sec);

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace uptime
}  // namespace copg

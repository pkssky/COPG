// COPG open-source engine — VPN interface hiding implementation.
//
// Apps detect "user is behind a VPN" by walking getifaddrs() and looking for
// tun*/ppp* interfaces. The hook rewrites those entries' names IN PLACE
// (ifa_name's buffer is at least IFNAMSIZ bytes, so "lo" always fits):
//
//   - no chain surgery: the ifaddrs allocation is handed to the app exactly
//     as the kernel built it, so freeifaddrs() stays correct and no node leaks
//   - no extra hooks: nothing to keep in sync across threads or calls
//
// Not covered (documented): if_nameindex()/netlink readers, and checks based
// on interface flags rather than names.
#include "log.hpp"
#include "vpn.hpp"

#include <ifaddrs.h>
#include <string.h>
#include <sys/types.h>

namespace copg {
namespace vpn {
namespace {

using getifaddrs_fn = int (*)(struct ifaddrs **);

getifaddrs_fn g_real_getifaddrs = nullptr;

// Interface names that betray a user-space VPN.
bool is_vpn_iface(const char *name) {
    if (!name) return false;
    return strncmp(name, "tun", 3) == 0 || strncmp(name, "ppp", 3) == 0;
}

int hook_getifaddrs(struct ifaddrs **ifap) {
    if (!g_real_getifaddrs) return -1;
    int rc = g_real_getifaddrs(ifap);
    if (rc != 0 || !ifap || !*ifap) return rc;

    int hidden = 0;
    for (struct ifaddrs *ifa = *ifap; ifa; ifa = ifa->ifa_next) {
        if (!is_vpn_iface(ifa->ifa_name) || !ifa->ifa_name) continue;
        // In-place rename: "tun0"/"ppp0" -> "lo". The buffer belongs to the
        // caller's own allocation and is IFNAMSIZ bytes, so this is safe.
        strcpy(ifa->ifa_name, "lo");
        ++hidden;
    }
    if (hidden > 0) COPG_LOGD("vpn: hid %d interface(s) from getifaddrs", hidden);
    return rc;
}

}  // namespace

void register_hooks(zygisk::Api *api) {
    api->pltHookRegister(".*", "getifaddrs", reinterpret_cast<void *>(hook_getifaddrs),
                         reinterpret_cast<void **>(&g_real_getifaddrs));
}

}  // namespace vpn
}  // namespace copg

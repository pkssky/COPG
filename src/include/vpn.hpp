// COPG open-source engine — VPN interface hiding (:vpn / :vpns).
//
// Many apps detect "user is behind a VPN" by walking getifaddrs() and looking
// for tun*/ppp* interfaces. This hook removes those entries from the list the
// app sees while keeping the real allocation intact (freeifaddrs frees the
// original chain, so nothing leaks).
#pragma once

#include "zygisk.hpp"

namespace copg {
namespace vpn {

// Register the PLT hooks (call before zygisk::Api::pltHookCommit()).
void register_hooks(zygisk::Api *api);

}  // namespace vpn
}  // namespace copg
